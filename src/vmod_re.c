/*-
 * Copyright (c) 2013 - 2014 UPLEX Nils Goroll Systemoptimierung
 * All rights reserved
 *
 * Authors: Geoffrey Simmons <geoffrey.simmons@uplex.de>
 *	    Nils Goroll <nils.goroll@uplex.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"

#include <stdlib.h>
#include <pthread.h>

#include "vre.h"
#include "vrt.h"
#include "cache/cache.h"
#include "vas.h"
#include "miniobj.h"

#include "vcc_if.h"

/* pcreapi(3):
 * 
 * The first two-thirds of the vector is used to pass back captured
 * substrings, each substring using a pair of integers. The remaining
 * third of the vector is used as workspace by pcre_exec() while matching
 * capturing subpatterns, and is not available for passing back
 * information.
 *
 * XXX: if vre were to expose the pcre and pcre_extra objects, then we
 * could use pcre_fullinfo() to determine the highest backref for each
 * regex, and wouldn't need this arbitrary limit ...
 */
#define MAX_MATCHES	11
#define MAX_OV		((MAX_MATCHES) * 3)
#define MAX_OV_USED	((MAX_MATCHES) * 2)

struct vmod_re_regex {
	unsigned	magic;
#define VMOD_RE_REGEX_MAGIC 0x955706ee
	vre_t		*vre;
	pthread_key_t	ovk;
	int		erroffset;
	const char	*error;
};

typedef struct ov_s {
	unsigned	magic;
#define OV_MAGIC 0x844bfa39
	const char	*subject;
	int		ovector[MAX_OV_USED];
} ov_t;

static char c;
static const void *match_failed = (void *) &c;

VCL_VOID
vmod_regex__init(const struct vrt_ctx *ctx, struct vmod_re_regex **rep,
		 const char *vcl_name, VCL_STRING pattern)
{
	struct vmod_re_regex *re;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
#if 0
	AN(ctx->vsl); /* XXX: ctx->vsl is NULL?! */
#endif
	AN(rep);
	AZ(*rep);
	AN(vcl_name);
	AN(pattern);
	ALLOC_OBJ(re, VMOD_RE_REGEX_MAGIC);
	AN(re);
	*rep = re;

	AZ(pthread_key_create(&re->ovk, free));
	re->erroffset = 0;
	re->error = NULL;

	re->vre = VRE_compile(pattern, 0, &re->error, &re->erroffset);
#if 0
	if (re->vre == NULL)
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: error compiling regex \"%s\" in VCL \"%s\": "
		     "%s (position %d)", pattern, vcl_name, error, erroffset);
#endif
}

VCL_BOOL
vmod_regex_failed(const struct vrt_ctx *ctx, struct vmod_re_regex *re)
{
	(void) ctx;
	CHECK_OBJ_NOTNULL(re, VMOD_RE_REGEX_MAGIC);
	return (re->error != NULL);
}

VCL_STRING
vmod_regex_error(const struct vrt_ctx *ctx, struct vmod_re_regex *re)
{
	VCL_STRING error;

	CHECK_OBJ_NOTNULL(re, VMOD_RE_REGEX_MAGIC);
	if (re->error == NULL)
		return "";

	error = (VCL_STRING) WS_Printf(ctx->ws, "%s (position %d)", re->error,
				       re->erroffset);
	if (error == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: insufficient workspace for error message");
		return "insufficient workspace for error message";
	}
	return(error);
}

VCL_VOID
vmod_regex__fini(struct vmod_re_regex **rep)
{
	struct vmod_re_regex *re;

	re = *rep;
	*rep = NULL;
	CHECK_OBJ_NOTNULL(re, VMOD_RE_REGEX_MAGIC);
	if (re->vre != NULL)
		VRE_free(&re->vre);
	FREE_OBJ(re);
}

static inline VCL_BOOL
match(const struct vrt_ctx *ctx, struct vmod_re_regex *re, vre_t *vre,
      VCL_STRING subject)
{
	ov_t *ov;
	int s, nov[MAX_OV];
	char *snap;
	size_t cp;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(re, VMOD_RE_REGEX_MAGIC);

	if (vre == NULL)
		vre = re->vre;

	AZ(pthread_setspecific(re->ovk, match_failed));

	/* compilation error at init time */
	if (vre == NULL) {
		AN(re->error);
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: error compiling regex: %s (position %d)",
		     re->error, re->erroffset);
		return 0;
	}

	if (subject == NULL)
		subject = "";

	/* XXX: cache_param->vre_limits incorrect?! */
	s = VRE_exec(vre, subject, strlen(subject), 0, 0, nov, MAX_OV,
		     NULL);
#if 0
		     &cache_param->vre_limits);
#endif
	if (s <= VRE_ERROR_NOMATCH) {
		if (s < VRE_ERROR_NOMATCH)
			VSLb(ctx->vsl, SLT_VCL_Error,
			     "vmod re: regex match returned %d", s);
		return 0;
	}
	if (s == 0) {
		VSLb(ctx->vsl, SLT_VCL_Error,
	     	     "vmod re: capturing substrings exceed max %d",
		     MAX_MATCHES - 1);
		s = MAX_MATCHES;
	}

	snap = WS_Snapshot(ctx->ws);
	ov = (ov_t *) WS_Alloc(ctx->ws, sizeof(ov_t));
	if (ov == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: insufficient workspace");
		return 0;
	}
	ov->subject = WS_Copy(ctx->ws, (const void *) subject, -1);
	if (ov->subject == NULL) {
		WS_Reset(ctx->ws, snap);
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: insufficient workspace");
		return 0;
	}
	ov->magic = OV_MAGIC;
	memset(ov->ovector, -1, sizeof(ov->ovector));
	cp = s * 2 * sizeof(*nov);
	assert(cp <= sizeof(nov));
	memcpy(ov->ovector, nov, cp);
	AZ(pthread_setspecific(re->ovk, (const void *) ov));
	return 1;
}

VCL_BOOL
vmod_regex_match(const struct vrt_ctx *ctx, struct vmod_re_regex *re,
		 VCL_STRING subject)
{
	return match(ctx, re, NULL, subject);
}

VCL_BOOL
vmod_regex_match_dyn(const struct vrt_ctx *ctx, struct vmod_re_regex *re,
		     VCL_STRING pattern, VCL_STRING subject)
{
	vre_t *vre;
	int erroffset;
	const char *error;

	AN(pattern);
	vre = VRE_compile(pattern, 0, &error, &erroffset);
	if (vre == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: error compiling regex \"%s\": %s (position %d)",
		     pattern, error, erroffset);
		return 0;
	}
	return match(ctx, re, vre, subject);
}

VCL_STRING
vmod_regex_backref(const struct vrt_ctx *ctx, struct vmod_re_regex *re,
		   VCL_INT refnum, VCL_STRING fallback)
{
	void *p;
	ov_t *ov;
	char *substr;
	unsigned l;
	size_t len;

	CHECK_OBJ_NOTNULL(ctx, VRT_CTX_MAGIC);
	CHECK_OBJ_NOTNULL(re, VMOD_RE_REGEX_MAGIC);
	AN(fallback);
	assert(refnum >= 0);

	if (refnum >= MAX_MATCHES) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: backref %ld out of range", refnum);
		return fallback;
	}

	p = pthread_getspecific(re->ovk);
	if (p == NULL) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: backref called without prior match");
		return fallback;
	}
	if (p == match_failed)
		return fallback;

	CAST_OBJ(ov, p, OV_MAGIC);
	assert((char *)ov >= ctx->ws->s && (char *)ov < ctx->ws->e);
	assert(ov->subject >= ctx->ws->s && ov->subject < ctx->ws->e);

	refnum <<= 1;
	assert(refnum + 1 < MAX_OV_USED);
	if (ov->ovector[refnum] == -1)
		return fallback;

	l = WS_Reserve(ctx->ws, 0);
	substr = ctx->ws->f;

	/* cf. pcre_copy_substring */
	len = ov->ovector[refnum+1] - ov->ovector[refnum];
	if (len + 1 > l) {
		VSLb(ctx->vsl, SLT_VCL_Error,
		     "vmod re: insufficient workspace");
		WS_Release(ctx->ws, 0);
		return fallback;
	}
	assert(len <= strlen(ov->subject + ov->ovector[refnum]));
	memcpy(substr, ov->subject + ov->ovector[refnum], len);
	substr[len] = '\0';

	WS_Release(ctx->ws, len + 1);
	return substr;
}

VCL_STRING
vmod_version(const struct vrt_ctx *ctx __attribute__((unused)))
{
	return VERSION;
}

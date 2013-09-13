/*-
 * Copyright (c) 2013 UPLEX Nils Goroll Systemoptimierung
 * All rights reserved
 *
 * Author: Geoffrey Simmons <geoffrey.simmons@uplex.de>
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

#include <stdlib.h>
#include <pthread.h>
#include <sys/resource.h>

#include "pcre.h"

#include "vre.h"
#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vas.h"
#include "miniobj.h"

#include "vcc_if.h"
#include "config.h"

#define MAX_OV 33

typedef struct sess_ov {
	unsigned	magic;
#define SESS_OV_MAGIC 0x844bfa39
	const char	*pattern;
	char		*subject;
	int		ovector[MAX_OV];
	int		count;
} sess_ov;

struct sess_tbl {
	unsigned	magic;
#define SESS_TBL_MAGIC 0x0cd0c7ca
	int		nsess;
	sess_ov		**sess;
};

/*
 * cf. libvmod_header: a single, global lock that prevents two threads
 * from compiling regexen at the same time
 */
pthread_mutex_t re_mutex;

static void
free_sess_tbl(void *priv)
{
	struct sess_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, priv, SESS_TBL_MAGIC);
	for (int i = 0; i < tbl->nsess; i++)
		if (tbl->sess[i] != NULL)
			free(tbl->sess[i]);
	free(tbl->sess);
	free(tbl);
}

/*
 * Set up a table of ov structures for each session, large enough to fit
 * max_open_files.
 */
int __match_proto__()
re_init(struct vmod_priv *priv,
        const struct VCL_conf *vcl_conf __attribute__((unused)))
{
	struct sess_tbl	*tbl;
	struct rlimit	nfd, rltest;

	AN(priv);
	
	AZ(pthread_mutex_init(&re_mutex, NULL));

	AZ(getrlimit(RLIMIT_NOFILE, &nfd));

	rltest.rlim_cur = nfd.rlim_cur;
	rltest.rlim_max = nfd.rlim_max + 1;
	AN(setrlimit(RLIMIT_NOFILE, &rltest));
	assert(errno == EPERM);

	ALLOC_OBJ(tbl, SESS_TBL_MAGIC);
	XXXAN(tbl);
	tbl->nsess = nfd.rlim_max;

	tbl->sess = calloc(tbl->nsess, sizeof(sess_ov *));
	XXXAN(tbl->sess);

	priv->priv = tbl;
	priv->free = free_sess_tbl;
	return (0);
}

static unsigned
match(struct sess *sp, struct vmod_priv *priv_vcl, struct vmod_priv *priv_call,
      const char *str, const char *pattern, int dynamic)
{
	vre_t *re;
	struct sess_tbl *tbl;
	sess_ov *ov;
	int s;

	AN(pattern);
	if (pattern == '\0')
		return 1;
	
	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	CAST_OBJ_NOTNULL(tbl, priv_vcl->priv, SESS_TBL_MAGIC);
	assert(sp->id < tbl->nsess);
	if (tbl->sess[sp->id] == NULL) {
		ALLOC_OBJ(tbl->sess[sp->id], SESS_OV_MAGIC);
		XXXAN(tbl->sess[sp->id]);
	}
	CHECK_OBJ(tbl->sess[sp->id], SESS_OV_MAGIC);
	ov = tbl->sess[sp->id];

	if (priv_call->priv == NULL
	    || (dynamic && strcmp(pattern, ov->pattern) != 0)) {
		int erroffset = 0;
		const char *error = NULL;
		
		AZ(pthread_mutex_lock(&re_mutex));
		/*
		 * Double-check the lock. For dynamic, the pointer would
		 * have changed by now if another thread was already here,
		 * so strcmp doesn't have to be repeated.
		 */
		if (priv_call->priv == NULL
		    || (dynamic && (pattern != ov->pattern))) {
			priv_call->priv = VRE_compile(pattern, 0, &error,
						      &erroffset);
			if (priv_call->priv == NULL)
				WSP(sp, SLT_VCL_error,
				    "vmod re: error compiling regex \"%s\": "
				    "%s (position %d)", pattern, error,
				    erroffset);
			else
				priv_call->free = VRT_re_fini;
		}
		if (dynamic)
			ov->pattern = WS_Dup(sp->wrk->ws, pattern);
		AZ(pthread_mutex_unlock(&re_mutex));
	}
	re = (vre_t *) priv_call->priv;
	if (re == NULL)
		return 0;

	if (str == NULL)
		str = "";
	s = VRE_exec(re, str, strlen(str), 0, 0, &ov->ovector[0],
	             MAX_OV, &params->vre_limits);
	ov->count = s;
	if (s < VRE_ERROR_NOMATCH) {
		WSP(sp, SLT_VCL_error, "vmod re: regex match returned %d", s);
		return 0;
	}
	if (s == VRE_ERROR_NOMATCH)
		return 0;
	
	ov->subject = WS_Dup(sp->wrk->ws, str);
	return 1;
}

unsigned __match_proto__()
vmod_match(struct sess *sp, struct vmod_priv *priv_vcl,
           struct vmod_priv *priv_call, const char *str, const char *pattern)
{
	return(match(sp, priv_vcl, priv_call, str, pattern, 0));
}

unsigned __match_proto__()
vmod_match_dyn(struct sess *sp, struct vmod_priv *priv_vcl,
           struct vmod_priv *priv_call, const char *str, const char *pattern)
{
	return(match(sp, priv_vcl, priv_call, str, pattern, 1));
}

const char * __match_proto__()
vmod_backref(struct sess *sp, struct vmod_priv *priv_vcl, int refnum,
             const char *fallback)
{
	struct sess_tbl *tbl;
	struct sess_ov *ov;
	char *substr;
	unsigned l;
	int s;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(priv_vcl);
	AN(fallback);
	CAST_OBJ_NOTNULL(tbl, priv_vcl->priv, SESS_TBL_MAGIC);
	assert(sp->id < tbl->nsess);
	CHECK_OBJ_NOTNULL(tbl->sess[sp->id], SESS_OV_MAGIC);
	ov = tbl->sess[sp->id];

	if (ov->count <= VRE_ERROR_NOMATCH)
		return fallback;
	
	AN(ov->subject);

	l = WS_Reserve(sp->wrk->ws, 0);
	substr = sp->wrk->ws->f;

	s = pcre_copy_substring(ov->subject, ov->ovector, ov->count, refnum,
	                        substr, l);
	if (s < 0) {
		WSP(sp, SLT_VCL_error, "vmod re: backref returned %d", s);
		WS_Release(sp->wrk->ws, 0);
		return fallback;
	}
	WS_Release(sp->wrk->ws, s);
	return substr;
}

const char * __match_proto__()
vmod_version(struct sess *sp __attribute__((unused)))
{
	return VERSION;
}

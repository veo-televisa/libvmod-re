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
#include <sys/resource.h>

#include "pcre.h"

#include "vre.h"
#include "vrt.h"
#include "bin/varnishd/cache.h"
#include "vas.h"
#include "miniobj.h"
#include "vmb.h"

#include "vcc_if.h"

/* pcreapi(3):
 * 
 * The first two-thirds of the vector is used to pass back captured
 * substrings, each substring using a pair of integers. The remaining
 * third of the vector is used as workspace by pcre_exec() while matching
 * capturing subpatterns, and is not available for passing back
 * information.
 */

#define MAX_MATCHES	11
#define MAX_OV		((MAX_MATCHES) * 3)
#define MAX_OV_USED	((MAX_MATCHES) * 2)

/* 
 * XXX we don't need the re_t obj at the moment, should
 * we keep it ?
 */
typedef struct re_t {
	unsigned	magic;
#define RE_MAGIC 0xd361bdcb	
	vre_t		*vre;
} re_t;

typedef struct sess_ov {
	unsigned	magic;
#define SESS_OV_MAGIC 0x844bfa39
	const char	*subject;
	int		ovector[MAX_OV_USED];
	int		count;
	unsigned	xid;
	void		*ws;
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
free_re(void *priv)
{
	struct re_t *re;
	
	CAST_OBJ_NOTNULL(re, priv, RE_MAGIC);
	if (re->vre != NULL)
		VRE_free(&re->vre);
	FREE_OBJ(re);
}

static void
free_sess_tbl(void *priv)
{
	struct sess_tbl *tbl;

	CAST_OBJ_NOTNULL(tbl, priv, SESS_TBL_MAGIC);
	for (int i = 0; i < tbl->nsess; i++)
		if (tbl->sess[i] != NULL)
			FREE_OBJ(tbl->sess[i]);
	free(tbl->sess);
	FREE_OBJ(tbl);
}

static inline void 
init_ov(struct sess_ov *ov, struct sess *sp)
{
	ov->subject = NULL;
	ov->count = -1;
	ov->xid = sp->xid;
	ov->ws = sp->ws;
}

static inline sess_ov *
get_ov(struct sess *sp, struct vmod_priv *priv_vcl, const int init)
{
	struct sess_tbl *tbl;
	struct sess_ov *ov;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(priv_vcl);

	CAST_OBJ_NOTNULL(tbl, priv_vcl->priv, SESS_TBL_MAGIC);
	assert(sp->id < tbl->nsess);
	assert(sp->id >= 0);
	if (tbl->sess[sp->id] == NULL) {
		if (init == 0)
			return NULL;

		ALLOC_OBJ(tbl->sess[sp->id], SESS_OV_MAGIC);
		XXXAN(tbl->sess[sp->id]);

		init_ov(tbl->sess[sp->id], sp);

		return (tbl->sess[sp->id]);
	}

	CHECK_OBJ(tbl->sess[sp->id], SESS_OV_MAGIC);
	ov = tbl->sess[sp->id];

	/*
	 * we need to make sure that we do not back-ref matches from a
	 * previous session on the same sp->id, so check the xid and the
	 * worker ws just in case we have non-unique xids (some versions
	 * of varnish3)
	 */

	if ((ov->xid != sp->xid) || (ov->ws != sp->ws)) {
		if (init == 0)
			return NULL;
		else
			init_ov(ov, sp);
	}

	return (ov);
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
	struct rlimit nfd;

	AN(priv);
	
	AZ(pthread_mutex_init(&re_mutex, NULL));

	AZ(getrlimit(RLIMIT_NOFILE, &nfd));

#ifndef DISABLE_MAXFD_TEST
	struct rlimit rltest;
	rltest.rlim_cur = nfd.rlim_cur;
	rltest.rlim_max = nfd.rlim_max + 1;
	AN(setrlimit(RLIMIT_NOFILE, &rltest));
	assert(errno == EPERM);
#endif

	ALLOC_OBJ(tbl, SESS_TBL_MAGIC);
	XXXAN(tbl);
	tbl->nsess = nfd.rlim_max;

	tbl->sess = calloc(tbl->nsess, sizeof(sess_ov *));
	XXXAN(tbl->sess);

	priv->priv = tbl;
	priv->free = free_sess_tbl;
	return (0);
}

static inline unsigned
match(struct sess *sp, struct vmod_priv *priv_vcl, struct vmod_priv *priv_call,
      const char *str, const char *pattern, int dynamic)
{
	vre_t *vre;
	sess_ov *ov;
	int s, nov[MAX_OV];
	unsigned result;
	size_t cp;

	AN(pattern);

	/* a null pattern always matches */
	if (pattern == '\0')
		return 1;
	
	if (dynamic) {
		/*
		 * should we cache here?
		 *
		 * if we wanted to use priv_call, we would need to run the
		 * match under the lock or would need refcounting with
		 * delayed free.
		 *
		 * A real LRU/MFU pattern -> vre cache probably would be a
		 * better idea
		 *
		 * also, as long as we don't cache, we will always
		 * recompile a bad re
		 */
		int erroffset = 0;
		const char *error = NULL;

		vre = VRE_compile(pattern, 0, &error, &erroffset);
		if (vre == NULL)
			WSP(sp, SLT_VCL_error,
			    "vmod re: error compiling dynamic regex \"%s\": "
			    "%s (position %d)", pattern, error,
			    erroffset);
	}
	else if (priv_call->priv) {
		re_t *re;

		CAST_OBJ(re, priv_call->priv, RE_MAGIC);
		vre = re->vre;
	}
	else {
		re_t *re;
		int erroffset = 0;
		const char *error = NULL;

		AZ(pthread_mutex_lock(&re_mutex));
		/* re-check under the lock */
		if (priv_call->priv) {
			CAST_OBJ(re, priv_call->priv, RE_MAGIC);
			vre = re->vre;
		}
		else {
			ALLOC_OBJ(re, RE_MAGIC);
			XXXAN(re);
			
			vre = VRE_compile(pattern, 0, &error, &erroffset);
			if (vre == NULL)
				WSP(sp, SLT_VCL_error,
				    "vmod re: error compiling regex \"%s\": "
				    "%s (position %d)", pattern, error,
				    erroffset);
			re->vre = vre;
			/*
			 * make sure the re obj is complete before we commit
			 * the pointer
			 */
			VMB();
			priv_call->priv = re;
			priv_call->free = free_re;
		}
		AZ(pthread_mutex_unlock(&re_mutex));
	}
		

	/* compilation error */
	if (vre == NULL)
		return 0;

	/* get the ov only once we actually have a vre */
	ov = get_ov(sp, priv_vcl, 1);
	AN(ov);

	/* 
	 * we must not touch the ov unless we have a successful match, so
	 * vmod_backref will always reference the last successful match
	 */

	if (str == NULL)
		str = "";

	assert(sizeof(nov) == (sizeof(nov[0]) * MAX_OV));

	s = VRE_exec(vre, str, strlen(str), 0, 0, nov,
		     MAX_OV, &params->vre_limits);

	if (s > VRE_ERROR_NOMATCH) {
		if (s == 0)
			cp = sizeof(nov);	/* ov overflow */
		else
			cp = s * 2 * sizeof(*nov);

		assert(cp <= sizeof(nov));

		ov->subject = str;
		memcpy(ov->ovector, nov, cp);
		ov->count = s;
		result = 1;
	}
	else {
		if (s < VRE_ERROR_NOMATCH)
			WSP(sp, SLT_VCL_error,
			    "vmod re: regex match returned %d", s);
		result = 0;
	}

	if (dynamic)
		VRE_free(&vre);
	return result;
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
	struct sess_ov *ov;
	char *substr;
	unsigned l;
	int s;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(fallback);

	ov = get_ov(sp, priv_vcl, 0);

	if ((ov == NULL) || (ov->count < 0)) {
		WSP(sp, SLT_VCL_error,
		    "vmod re: backref called without prior match in the "
		    "session");
		return fallback;
	}
	
	AN(ov->subject);

	l = WS_Reserve(sp->ws, 0);
	substr = sp->ws->f;

	s = pcre_copy_substring(ov->subject, ov->ovector, ov->count, refnum,
	                        substr, l);
	if (s < 0) {
		WSP(sp, SLT_VCL_error, "vmod re: backref returned %d", s);
		WS_Release(sp->ws, 0);
		return fallback;
	}
	WS_Release(sp->ws, s + 1);
	return substr;
}

const char * __match_proto__()
vmod_version(struct sess *sp __attribute__((unused)))
{
	return VERSION;
}

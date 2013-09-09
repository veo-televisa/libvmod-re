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

struct sess_ov {
	unsigned	magic;
#define SESS_OV_MAGIC 0x844bfa39
	char		*subject;
	int		ovector[MAX_OV];
	int		count;
};

struct sess_tbl {
	unsigned	magic;
#define SESS_TBL_MAGIC 0x0cd0c7ca
	int		nsess;
	struct sess_ov	*sess;
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
	free(tbl->sess);
	free(tbl);
}

/*
 * Set up a table of ovectors for each session, large enough to
 * fit max_open_files.
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

	tbl->sess = calloc(tbl->nsess, sizeof(struct sess_ov));
	XXXAN(tbl->sess);
	for (int i = 0; i < tbl->nsess; i++)
		tbl->sess[i].magic = SESS_OV_MAGIC;

	priv->priv = tbl;
	priv->free = free_sess_tbl;
	return (0);
}

unsigned __match_proto__()
vmod_match(struct sess *sp, struct vmod_priv *priv_vcl,
           struct vmod_priv *priv_call, const char *str, const char *pattern)
{
	vre_t *re;
	struct sess_tbl *tbl;
	int s;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(pattern);
	CAST_OBJ_NOTNULL(tbl, priv_vcl->priv, SESS_TBL_MAGIC);
	assert(sp->id < tbl->nsess);
	CHECK_OBJ_NOTNULL(&tbl->sess[sp->id], SESS_OV_MAGIC);
	if (str == NULL)
		str = "";
	
	if (priv_call->priv == NULL) {
		AZ(pthread_mutex_lock(&re_mutex));
		if (priv_call->priv == NULL) {
			VRT_re_init(&priv_call->priv, pattern);
			priv_call->free = VRT_re_fini;
		}
		AZ(pthread_mutex_unlock(&re_mutex));
	}
	re = (vre_t *) priv_call->priv;
	AN(re);

	s = VRE_exec(re, str, strlen(str), 0, 0, &tbl->sess[sp->id].ovector[0],
	             MAX_OV, &params->vre_limits);
	tbl->sess[sp->id].count = s;
	if (s < VRE_ERROR_NOMATCH) {
		WSP(sp, SLT_VCL_error, "vmod re: regex match returned %d", s);
		return 0;
	}
	if (s == VRE_ERROR_NOMATCH)
		return 0;
	
	tbl->sess[sp->id].subject = WS_Dup(sp->wrk->ws, str);
	return 1;
}

const char * __match_proto__()
vmod_backref(struct sess *sp, struct vmod_priv *priv_vcl, int refnum,
             const char *fallback)
{
	struct sess_tbl *tbl;
	char *substr;
	unsigned l;
	int s;

	CHECK_OBJ_NOTNULL(sp, SESS_MAGIC);
	AN(priv_vcl);
	AN(fallback);
	CAST_OBJ_NOTNULL(tbl, priv_vcl->priv, SESS_TBL_MAGIC);
	assert(sp->id < tbl->nsess);
	CHECK_OBJ_NOTNULL(&tbl->sess[sp->id], SESS_OV_MAGIC);

	if (tbl->sess[sp->id].count <= VRE_ERROR_NOMATCH)
		return fallback;
	
	AN(tbl->sess[sp->id].subject);

	l = WS_Reserve(sp->wrk->ws, 0);
	substr = sp->wrk->ws->f;

	s = pcre_copy_substring(tbl->sess[sp->id].subject,
	                        tbl->sess[sp->id].ovector,
	                        tbl->sess[sp->id].count, refnum,
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

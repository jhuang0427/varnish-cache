/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2009 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
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
 *
 * This file parses the real action of the VCL code, the procedure
 * statements which do the actual work.
 */

#include "config.h"

#include "svnid.h"
SVNID("$Id$")

#include <stdio.h>
#include <string.h>

#include "vsb.h"

#include "vcc_priv.h"
#include "vcc_compile.h"
#include "libvarnish.h"

/*--------------------------------------------------------------------*/

static void
parse_call(struct vcc *tl)
{

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vcc_AddCall(tl, tl->t);
	vcc_AddRef(tl, tl->t, R_SUB);
	Fb(tl, 1, "if (VGC_function_%.*s(sp))\n", PF(tl->t));
	Fb(tl, 1, "\treturn (1);\n");
	vcc_NextToken(tl);
	return;
}

/*--------------------------------------------------------------------*/

static void
parse_error(struct vcc *tl)
{
	struct var *vp;

	vcc_NextToken(tl);
	if (tl->t->tok == ID) {
		vp = vcc_FindVar(tl, tl->t, vcc_vars, 0, "cannot be read");
		ERRCHK(tl);
		assert(vp != NULL);
		if (vp->fmt == INT) {
			Fb(tl, 1, "VRT_error(sp, %s", vp->rname);
			vcc_NextToken(tl);
		} else {
			Fb(tl, 1, "VRT_error(sp, 0");
		}
	} else if (tl->t->tok == CNUM) {
		Fb(tl, 1, "VRT_error(sp, %u", vcc_UintVal(tl));
	} else
		Fb(tl, 1, "VRT_error(sp, 0");
	if (tl->t->tok == CSTR) {
		Fb(tl, 0, ", %.*s", PF(tl->t));
		vcc_NextToken(tl);
	} else if (tl->t->tok == ID) {
		Fb(tl, 0, ", ");
		if (!vcc_StringVal(tl)) {
			ERRCHK(tl);
			vcc_ExpectedStringval(tl);
			return;
		}
	} else {
		Fb(tl, 0, ", (const char *)0");
	}
	Fb(tl, 0, ");\n");
	Fb(tl, 1, "VRT_done(sp, VCL_RET_ERROR);\n");
}

/*--------------------------------------------------------------------*/

static void
illegal_assignment(const struct vcc *tl, const char *type)
{

	vsb_printf(tl->sb, "Invalid assignment operator ");
	vcc_ErrToken(tl, tl->t);
	vsb_printf(tl->sb,
	    " only '=' is legal for %s\n", type);
}

static void
parse_set(struct vcc *tl)
{
	struct var *vp;
	struct token *at, *vt;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vt = tl->t;
	vp = vcc_FindVar(tl, tl->t, vcc_vars, 1, "cannot be set");
	ERRCHK(tl);
	assert(vp != NULL);
	Fb(tl, 1, "%s", vp->lname);
	vcc_NextToken(tl);
	switch (vp->fmt) {
	case INT:
//	case SIZE:
	case TIME:
	case DURATION:
//	case FLOAT:
		if (tl->t->tok != '=')
			Fb(tl, 0, "%s %c ", vp->rname, *tl->t->b);
		at = tl->t;
		vcc_NextToken(tl);
		switch (at->tok) {
		case T_MUL:
		case T_DIV:
			Fb(tl, 0, "%g", vcc_DoubleVal(tl));
			break;
		case T_INCR:
		case T_DECR:
		case '=':
			vcc_VarVal(tl, vp, vt);
			ERRCHK(tl);
			break;
		default:
			vsb_printf(tl->sb, "Invalid assignment operator.\n");
			vcc_ErrWhere(tl, at);
			return;
		}
		Fb(tl, 0, ");\n");
		break;
#if 0	/* XXX: enable if we find a legit use */
	case IP:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "IP numbers");
			return;
		}
		vcc_NextToken(tl);
		u = vcc_vcc_IpVal(tl);
		Fb(tl, 0, "= %uU; /* %u.%u.%u.%u */\n",
		    u,
		    (u >> 24) & 0xff,
		    (u >> 16) & 0xff,
		    (u >> 8) & 0xff,
		    u & 0xff);
		break;
#endif
	case BACKEND:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "backend");
			return;
		}
		vcc_NextToken(tl);
		vcc_ExpectCid(tl);
		ERRCHK(tl);
		vcc_AddRef(tl, tl->t, R_BACKEND);
		Fb(tl, 0, "VGCDIR(_%.*s)", PF(tl->t));
		vcc_NextToken(tl);
		Fb(tl, 0, ");\n");
		break;
	case STRING:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "strings");
			return;
		}
		vcc_NextToken(tl);
		if (!vcc_StringVal(tl)) {
			ERRCHK(tl);
			vcc_ExpectedStringval(tl);
			return;
		}
		do
			Fb(tl, 0, ", ");
		while (vcc_StringVal(tl));
		if (tl->t->tok != ';') {
			ERRCHK(tl);
			vsb_printf(tl->sb,
			    "Expected variable, string or semicolon\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		Fb(tl, 0, "vrt_magic_string_end);\n");
		break;
	case BOOL:
		if (tl->t->tok != '=') {
			illegal_assignment(tl, "boolean");
			return;
		}
		vcc_NextToken(tl);
		ExpectErr(tl, ID);
		if (vcc_IdIs(tl->t, "true")) {
			Fb(tl, 0, " 1);\n", vp->lname);
		} else if (vcc_IdIs(tl->t, "false")) {
			Fb(tl, 0, " 0);\n", vp->lname);
		} else {
			vsb_printf(tl->sb,
			    "Expected true or false\n");
			vcc_ErrWhere(tl, tl->t);
			return;
		}
		vcc_NextToken(tl);
		break;
	default:
		vsb_printf(tl->sb,
		    "Assignments not possible for type of '%s'\n", vp->name);
		vcc_ErrWhere(tl, tl->t);
		return;
	}
}

/*--------------------------------------------------------------------*/

static void
parse_unset(struct vcc *tl)
{
	struct var *vp;

	vcc_NextToken(tl);
	ExpectErr(tl, ID);
	vp = vcc_FindVar(tl, tl->t, vcc_vars, 1, "cannot be unset");
	ERRCHK(tl);
	assert(vp != NULL);
	if (vp->fmt != STRING || vp->hdr == NULL) {
		vsb_printf(tl->sb,
		    "Only http header variables can be unset.\n");
		vcc_ErrWhere(tl, tl->t);
		return;
	}
	ERRCHK(tl);
	Fb(tl, 1, "%s0);\n", vp->lname);
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static const struct purge_var {
	const char	*name;
	unsigned	flag;
} purge_var[] = {
#define PVAR(a, b, c)   { (a), (b) },
#include "purge_vars.h"
#undef PVAR
        { 0, 0 }
};

static void
parse_purge(struct vcc *tl)
{
	const struct purge_var *pv;

	vcc_NextToken(tl);

	ExpectErr(tl, '(');
	vcc_NextToken(tl);

	if (tl->t->tok == ID) {
		Fb(tl, 1, "VRT_ban(sp,\n");
		tl->indent += INDENT;
		while (1) {
			ExpectErr(tl, ID);

			/* Check valididity of purge variable */
			for (pv = purge_var; pv->name != NULL; pv++) {
				if (!strncmp(pv->name, tl->t->b,
				    strlen(pv->name)))
					break;
			}
			if (pv->name == NULL) {
				vsb_printf(tl->sb, "Unknown purge variable.");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			if ((pv->flag & PVAR_HTTP) &&
			    tl->t->b + strlen(pv->name) >= tl->t->e) {
				vsb_printf(tl->sb, "Missing header name.");
				vcc_ErrWhere(tl, tl->t);
				return;
			}

			Fb(tl, 1, "  \"%.*s\",\n", PF(tl->t));
			vcc_NextToken(tl);
			switch(tl->t->tok) {
			case '~':
			case T_NOMATCH:
			case T_EQ:
			case T_NEQ:
				Fb(tl, 1, "  \"%.*s\",\n", PF(tl->t));
				break;
			default:
				vsb_printf(tl->sb,
				    "Expected ~, !~, == or !=.\n");
				vcc_ErrWhere(tl, tl->t);
				return;
			}
			vcc_NextToken(tl);
			Fb(tl, 1, "  ");
			if (!vcc_StringVal(tl)) {
				vcc_ExpectedStringval(tl);
				return;
			}
			Fb(tl, 0, ",\n");
			if (tl->t->tok == ')')
				break;
			ExpectErr(tl, T_CAND);
			Fb(tl, 1, "\"%.*s\",\n", PF(tl->t));
			vcc_NextToken(tl);
		}
		Fb(tl, 1, "0);\n");
		tl->indent -= INDENT;
	} else {
		Fb(tl, 1, "VRT_ban_string(sp, ");
		if (!vcc_StringVal(tl)) {
			vcc_ExpectedStringval(tl);
			return;
		}
		do
			Fb(tl, 0, ", ");
		while (vcc_StringVal(tl));
		Fb(tl, 0, "vrt_magic_string_end);\n");
	}

	ExpectErr(tl, ')');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
parse_purge_url(struct vcc *tl)
{

	vcc_NextToken(tl);
	ExpectErr(tl, '(');
	vcc_NextToken(tl);

	Fb(tl, 1, "VRT_ban(sp, \"req.url\", \"~\", ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	ExpectErr(tl, ')');
	vcc_NextToken(tl);
	Fb(tl, 0, ", 0);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_esi(struct vcc *tl)
{

	vcc_NextToken(tl);
	Fb(tl, 1, "VRT_ESI(sp);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_new_syntax(struct vcc *tl)
{

	vsb_printf(tl->sb, "Please change \"%.*s\" to \"return(%.*s)\".\n",
	    PF(tl->t), PF(tl->t));
	vcc_ErrWhere(tl, tl->t);
}

/*--------------------------------------------------------------------*/

static void
parse_hash_data(struct vcc *tl)
{
	vcc_NextToken(tl);
	SkipToken(tl, '(');

	Fb(tl, 1, "VRT_hashdata(sp, ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	do
		Fb(tl, 0, ", ");
	while (vcc_StringVal(tl));
	Fb(tl, 0, " vrt_magic_string_end);\n");
	SkipToken(tl, ')');
}

/*--------------------------------------------------------------------*/

static void
parse_panic(struct vcc *tl)
{
	vcc_NextToken(tl);

	Fb(tl, 1, "VRT_panic(sp, ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	do
		Fb(tl, 0, ", ");
	while (vcc_StringVal(tl));
	Fb(tl, 0, " vrt_magic_string_end);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_return(struct vcc *tl)
{
	int retval = 0;

	vcc_NextToken(tl);
	ExpectErr(tl, '(');
	vcc_NextToken(tl);
	ExpectErr(tl, ID);

#define VCL_RET_MAC(l, U, B)						\
	do {								\
		if (vcc_IdIs(tl->t, #l)) {				\
			Fb(tl, 1, "VRT_done(sp, VCL_RET_" #U ");\n");	\
			vcc_ProcAction(tl->curproc, VCL_RET_##U, tl->t);\
			retval = 1;					\
		}							\
	} while (0);
#include "vcl_returns.h"
#undef VCL_RET_MAC
	if (!retval) {
		vsb_printf(tl->sb, "Expected return action name.\n");
		vcc_ErrWhere(tl, tl->t);
		ERRCHK(tl);
	}
	vcc_NextToken(tl);
	ExpectErr(tl, ')');
	vcc_NextToken(tl);
}

/*--------------------------------------------------------------------*/

static void
parse_rollback(struct vcc *tl)
{

	vcc_NextToken(tl);
	Fb(tl, 1, "VRT_Rollback(sp);\n");
}

/*--------------------------------------------------------------------*/

static void
parse_synthetic(struct vcc *tl)
{
	vcc_NextToken(tl);

	Fb(tl, 1, "VRT_synth_page(sp, 0, ");
	if (!vcc_StringVal(tl)) {
		vcc_ExpectedStringval(tl);
		return;
	}
	do
		Fb(tl, 0, ", ");
	while (vcc_StringVal(tl));
	Fb(tl, 0, " vrt_magic_string_end);\n");
}

/*--------------------------------------------------------------------*/

typedef void action_f(struct vcc *tl);

static struct action_table {
	const char		*name;
	action_f		*func;
	unsigned		bitmask;
} action_table[] = {
	{ "error",		parse_error },

#define VCL_RET_MAC(l, U, B)						\
	{ #l,			parse_new_syntax },
#include "vcl_returns.h"
#undef VCL_RET_MAC

	/* Keep list sorted from here */
	{ "call",		parse_call },
	{ "esi",		parse_esi, VCL_MET_FETCH },
	{ "hash_data",		parse_hash_data, VCL_MET_HASH },
	{ "panic",		parse_panic },
	{ "purge",		parse_purge },
	{ "purge_url",		parse_purge_url },
	{ "remove",		parse_unset }, /* backward compatibility */
	{ "return",		parse_return },
	{ "rollback",		parse_rollback },
	{ "set",		parse_set },
	{ "synthetic",		parse_synthetic },
	{ "unset",		parse_unset },
	{ NULL,			NULL }
};

int
vcc_ParseAction(struct vcc *tl)
{
	struct token *at;
	struct action_table *atp;

	at = tl->t;
	assert (at->tok == ID);
	for(atp = action_table; atp->name != NULL; atp++) {
		if (vcc_IdIs(at, atp->name)) {
			if (atp->bitmask != 0)
				vcc_AddUses(tl, at, atp->bitmask,
				    "not a valid action");
			atp->func(tl);
			return(1);
		}
	}
	return (0);
}

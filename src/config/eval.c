/*	eval.c
 *
 *	Expression evaluation functions
 *
 *	written 1986 by Daniel Lawrence
 *	modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "evar.h"
#include "line.h"
#include "string_utils.h"
#include "util.h"
#include "version.h"
#include "memory.h"
#include "error.h"
#include "magic_constants.h"
#include "terminal/input_state.h"

#define	MAXVARS	255

/* User variables */
static struct user_variable uv[MAXVARS + 1];

/* Initialize the user variable list. */
void varinit(void)
{
	int i;
	for (i = 0; i < MAXVARS; i++)
		uv[i].u_name[0] = 0;
}

/*
 * Evaluate a function.
 *
 * @fname: name of function to evaluate.
 */
const char *eval_get_function(char *fname)
{
	size_t fnum;	/* index to function to eval */
	int status;	/* return status */
	const char *tsp;	/* temporary string pointer */
	char arg1[NSTRING];	/* value of first argument */
	char arg2[NSTRING];	/* value of second argument */
	char arg3[NSTRING];	/* value of third argument */
	static _Thread_local char result[2 * NSTRING];	/* Thread-safe string result */

	/* look the function up in the function table */
	fname[3] = 0;		/* only first 3 chars significant */
	string_to_lower(fname);		/* and let it be upper or lower case */
	for (fnum = 0; fnum < ARRAY_SIZE(funcs); fnum++)
		if (strcmp(fname, funcs[fnum].f_name) == 0)
			break;

	/* return errorm on a bad reference */
	if (fnum == ARRAY_SIZE(funcs))
		return errorm;

	/* if needed, retrieve the first argument */
	if (funcs[fnum].f_type >= MONAMIC) {
		if ((status = macarg(arg1)) != true)
			return errorm;

		/* if needed, retrieve the second argument */
		if (funcs[fnum].f_type >= DYNAMIC) {
			if ((status = macarg(arg2)) != true)
				return errorm;

			/* if needed, retrieve the third argument */
			if (funcs[fnum].f_type >= TRINAMIC)
				if ((status = macarg(arg3)) != true)
					return errorm;
		}
	}


	/* and now evaluate it! */
	switch (fnum) {
	case UFADD:
		return int_to_string(safe_atoi(arg1, 0) + safe_atoi(arg2, 0));
	case UFSUB:
		return int_to_string(safe_atoi(arg1, 0) - safe_atoi(arg2, 0));
	case UFTIMES:
		return int_to_string(safe_atoi(arg1, 0) * safe_atoi(arg2, 0));
	case UFDIV:
		return int_to_string(safe_atoi(arg1, 0) / safe_atoi(arg2, 0));
	case UFMOD:
		return int_to_string(safe_atoi(arg1, 0) % safe_atoi(arg2, 0));
	case UFNEG:
		return int_to_string(-safe_atoi(arg1, 0));
    case UFCAT:
        safe_strcpy(result, arg1, sizeof(result));
        safe_strcat(result, arg2, sizeof(result));
        return result;
    case UFLEFT:
        {
            int n = safe_atoi(arg2, 0);
            if (n < 0) n = 0;
            if ((size_t)n > sizeof(result) - 1) n = (int)sizeof(result) - 1;
            safe_snprintf(result, sizeof(result), "%.*s", n, arg1);
            return result;
        }
    case UFRIGHT:
        {
            int n = safe_atoi(arg2, 0);
            size_t len = strlen(arg1);
            if (n < 0) n = 0;
            size_t start = (n > (int)len) ? 0 : len - (size_t)n;
            safe_strcpy(result, &arg1[start], sizeof(result));
            return result;
        }
    case UFMID:
        {
            int start = safe_atoi(arg2, 0) - 1;
            int n = safe_atoi(arg3, 0);
            size_t len = strlen(arg1);
            if (start < 0) start = 0;
            if ((size_t)start > len) start = (int)len;
            if (n < 0) n = 0;
            if ((size_t)n > sizeof(result) - 1) n = (int)sizeof(result) - 1;
            safe_snprintf(result, sizeof(result), "%.*s", n, &arg1[start]);
            return result;
        }
	case UFNOT:
		return bool_to_string(string_to_bool(arg1) == false);
	case UFEQUAL:
		return bool_to_string(safe_atoi(arg1, 0) == safe_atoi(arg2, 0));
	case UFLESS:
		return bool_to_string(safe_atoi(arg1, 0) < safe_atoi(arg2, 0));
	case UFGREATER:
		return bool_to_string(safe_atoi(arg1, 0) > safe_atoi(arg2, 0));
	case UFSEQUAL:
		return bool_to_string(strcmp(arg1, arg2) == 0);
	case UFSLESS:
		return bool_to_string(strcmp(arg1, arg2) < 0);
	case UFSGREAT:
		return bool_to_string(strcmp(arg1, arg2) > 0);
	case UFIND:
		return getval(arg1, result, sizeof(result));
	case UFAND:
		return bool_to_string(string_to_bool(arg1) && string_to_bool(arg2));
	case UFOR:
		return bool_to_string(string_to_bool(arg1) || string_to_bool(arg2));
	case UFLENGTH:
		return int_to_string((int)strlen(arg1));
	case UFUPPER:
		safe_strcpy(result, string_to_upper(arg1), sizeof(result));
		return result;
	case UFLOWER:
		safe_strcpy(result, string_to_lower(arg1), sizeof(result));
		return result;
	case UFTRUTH:
		return bool_to_string(safe_atoi(arg1, 0) == 42);
	case UFASCII:
		return int_to_string((int) arg1[0]);
	case UFCHR:
		result[0] = (char)safe_atoi(arg1, 0);
		result[1] = 0;
		return result;
	case UFGTKEY:
		result[0] = (char)input_read_byte();
		result[1] = 0;
		return result;
	case UFRND:
		return int_to_string((editor_random() % abs(safe_atoi(arg1, 0))) + 1);
	case UFABS:
		return int_to_string(abs(safe_atoi(arg1, 0)));
	case UFSINDEX:
		return int_to_string(string_find_index(arg1, arg2));
	case UFENV:
		tsp = getenv(arg1);
		return tsp == nullptr ? "" : tsp;
	case UFBIND:
		return transbind(arg1);
	case UFEXIST:
		return bool_to_string(fexist(arg1));
	case UFFIND:
		tsp = flook(arg1, true);
		return tsp == nullptr ? "" : tsp;
	case UFBAND:
		return int_to_string(safe_atoi(arg1, 0) & safe_atoi(arg2, 0));
	case UFBOR:
		return int_to_string(safe_atoi(arg1, 0) | safe_atoi(arg2, 0));
	case UFBXOR:
		return int_to_string(safe_atoi(arg1, 0) ^ safe_atoi(arg2, 0));
	case UFBNOT:
		return int_to_string(~safe_atoi(arg1, 0));
	case UFXLATE:
		return string_translate(arg1, arg2, arg3);
	}

	exit(-11);		/* never should get here */
}

/*
 * look up a user var's value
 *
 * char *vname;			name of user variable to fetch
 */
char *eval_get_user_var(char *vname)
{

	int vnum;	/* ordinal number of user var */

	/* scan the list looking for the user var name */
	for (vnum = 0; vnum < MAXVARS; vnum++) {
		if (uv[vnum].u_name[0] == 0)
			return errorm;
		if (strcmp(vname, uv[vnum].u_name) == 0)
			return uv[vnum].u_value;
	}

	/* return errorm if we run off the end */
	return errorm;
}

extern char *getkill(void);

/*
 * eval_get_env_var()
 *
 * char *vname;			name of environment variable to retrieve
 */
char *eval_get_env_var(const char *vname)
{
	size_t vnum;	/* ordinal number of var refrenced */

	/* scan the list, looking for the referenced name */
	for (vnum = 0; vnum < ARRAY_SIZE(envars); vnum++)
		if (strcmp(vname, envars[vnum]) == 0)
			break;

	/* return errorm on a bad reference */
	if (vnum == ARRAY_SIZE(envars)) {
		char *ename = getenv(vname);

		if (ename != nullptr)
			return ename;
		else
			return errorm;
	}

	/* otherwise, fetch the appropriate value */
	switch (vnum) {
	case EVFILLCOL:
		return int_to_string(fillcol);
	case EVPAGELEN:
		return int_to_string(term.t_nrow + 1);
	case EVCURCOL:
		return int_to_string(getccol(false));
	case EVCURLINE:
		return int_to_string(getcline());
	case EVRAM:
		return "0";  /* envram removed - always returns 0 */
	case EVCURWIDTH:
		return int_to_string(term.t_ncol);
	case EVCBUFNAME:
		return curbp->b_bname;
	case EVCFNAME:
		return curbp->b_fname;
	case EVSRES:
		return sres;
	case EVDEBUG:
		return bool_to_string(macbug);
	case EVSTATUS:
		return bool_to_string(cmdstatus);
	case EVPALETTE:
		return palstr;
	case EVASAVE:
		return int_to_string(gasave);
	case EVACOUNT:
		return int_to_string(gacount);
	case EVLASTKEY:
		return int_to_string(lastkey);
	case EVCURCHAR:
		return (llength(curwp->w_dotp) ==
			curwp->w_doto ? int_to_string('\n') :
			int_to_string(lgetc(curwp->w_dotp, curwp->w_doto)));
	case EVDISCMD:
		return bool_to_string(discmd);
	case EVVERSION:
		return VERSION;
	case EVPROGNAME:
		return PROGRAM_NAME_LONG;
	case EVSEED:
		return "0";  /* seed is now local to editor_random() */
	case EVDISINP:
		return bool_to_string(disinp);
	case EVWLINE:
		return int_to_string(curwp->w_ntrows);
	case EVCWLINE:
		return int_to_string(window_get_position());
	case EVTARGET:
		saveflag = lastflag;
		return int_to_string(curgoal);
	case EVSEARCH:
		return pat;
	case EVREPLACE:
		return rpat;
	case EVMATCH:
		return (patmatch == nullptr) ? "" : patmatch;
	case EVKILL:
		return getkill();
	case EVCMODE:
		return int_to_string((int)curbp->b_mode);
	case EVGMODE:
		return int_to_string(gmode);
	case EVTPAUSE:
		return int_to_string(term.t_pause);
	case EVPENDING:
		return bool_to_string(input_pending());
	case EVLWIDTH:
		return int_to_string(llength(curwp->w_dotp));
	case EVLINE:
		return getctext();
	case EVGFLAGS:
		return int_to_string(gflags);
	case EVRVAL:
		return int_to_string(rval);
	case EVTAB:
		return int_to_string(tabmask + 1);
	case EVOVERLAP:
		return int_to_string(overlap);
	case EVSCROLLCOUNT:
		return int_to_string(scrollcount);
	case EVSCROLL:
		return bool_to_string(term.t_scroll != nullptr);
	}
	exit(-12);		/* again, we should never get here */
}

/*
 * return some of the contents of the kill buffer
 */
char *getkill(void)
{
	int size;	/* max number of chars to return */
	static _Thread_local char value[NSTRING];	/* Thread-safe temp buffer */

	if (temp_kill_len == 0)
		value[0] = 0;
	else {
		if (temp_kill_len < NSTRING)
			size = (int)temp_kill_len;
		else
			size = NSTRING - 1;
        if (size > 0) {
            if ((size_t)size > sizeof(value) - 1) size = (int)sizeof(value) - 1;
            memcpy(value, temp_kill_buf, (size_t)size);
            value[size] = '\0';
        } else {
            value[0] = '\0';
        }
	}

	/* and return the constructed value */
	return value;
}

/*
 * set a variable
 *
 * int f;		default flag
 * int n;		numeric arg (can overide prompted value)
 */
int setvar(int f, int n)
{
	int status;	/* status return */
#if	DEBUGM
	char *sp;	/* temp string pointer */
	char *ep;	/* ptr to end of outline */
#endif
	struct variable_description vd;		/* variable num/type */
	char var[NVSIZE + 1];	/* name of variable to fetch */
	char value[NSTRING];	/* value to set variable to */

	/* first get the variable to set.. */
	if (clexec == false) {
		status = minibuf_read("VARIABLE TO SET: ", &var[0], NVSIZE);
		if (status != true)
			return status;
	} else {		/* macro line argument */
		/* grab token and skip it */
		execstr = token(execstr, var, NVSIZE + 1);
	}

	/* check the legality and find the var */
	findvar(var, &vd, NVSIZE + 1);

	/* if its not legal....bitch */
	if (vd.v_type == -1) {
		REPORT_ERROR(ERR_SYNTAX_ERROR, var);
		return false;
	}

	/* get the value for that variable */
    if (f == true)
        safe_strcpy(value, int_to_string(n), sizeof(value));
	else {
		status = minibuf_read("VALUE: ", &value[0], NSTRING);
		if (status != true)
			return status;
	}

	/* and set the appropriate value */
	status = svar(&vd, value);

	/* if $debug == true, every assignment will echo a statement here. */
	if (macbug) {
		char dbg[NSTRING];
		char *sp_local;
		char *ep_local;
		safe_strcpy(dbg, "(((", sizeof(dbg));

		/* assignment status */
		safe_strcat(dbg, bool_to_string(status), sizeof(dbg));
		safe_strcat(dbg, ":", sizeof(dbg));

		/* variable name */
		safe_strcat(dbg, var, sizeof(dbg));
		safe_strcat(dbg, ":", sizeof(dbg));

		/* and lastly the value we tried to assign */
		safe_strcat(dbg, value, sizeof(dbg));
		safe_strcat(dbg, "))) ", sizeof(dbg));

		/* expand '%' to "%%" so mlwrite wont bitch */
		sp_local = dbg;
		while (*sp_local) {
			if (*sp_local++ == '%') {
				ep_local = --sp_local;
				while (*ep_local++)
					;
				*(ep_local + 1) = 0;
				while (ep_local-- > sp_local)
					*(ep_local + 1) = *ep_local;
				sp_local += 2;
			}
		}

		/* write out the debug line */
		mlforce(dbg);
		update(true);

		/* and get the keystroke to hold the output */
		input_key_event_t dbg_evt;
		bool aborted = (input_read_event(&dbg_evt) < 0) ||
		               evt_is_ctrl(&dbg_evt, 'G');  /* C-g is abort */
		if (aborted) {
			mlforce("(Macro aborted)");
			status = false;
		}
	}

	/* and return it */
	return status;
}

/*
 * Find a variables type and name.
 *
 * @var: name of variable to get.
 * @vd: structure to hold type and pointer.
 * @size: size of variable array.
 */
void findvar(char *var, struct variable_description *vd, int size)
{
	size_t vnum;	/* subscript in variable arrays */
	int vtype;	/* type to return */

	vnum = (size_t)-1;
fvar:
	vtype = -1;
	switch (var[0]) {

	case '$':		/* check for legal enviromnent var */
		for (vnum = 0; vnum < ARRAY_SIZE(envars); vnum++)
			if (strcmp(&var[1], envars[vnum]) == 0) {
				vtype = TKENV;
				break;
			}
		break;

	case '%':		/* check for existing legal user variable */
		for (vnum = 0; vnum < MAXVARS; vnum++)
			if (strcmp(&var[1], uv[vnum].u_name) == 0) {
				vtype = TKVAR;
				break;
			}
		if (vnum < MAXVARS)
			break;

		/* create a new one??? */
		for (vnum = 0; vnum < MAXVARS; vnum++)
			if (uv[vnum].u_name[0] == 0) {
				vtype = TKVAR;
                safe_strcpy(uv[vnum].u_name, &var[1], sizeof(uv[vnum].u_name));
				break;
			}
		break;

	case '&':		/* indirect operator? */
		var[4] = 0;
		if (strcmp(&var[1], "ind") == 0) {
			/* grab token, and eval it */
			execstr = token(execstr, var, size);
			getval(var, var, size);
			goto fvar;
		}
	}

	/* return the results */
	vd->v_num = (int)vnum;
	vd->v_type = vtype;
	return;
}

/*
 * Set a variable.
 *
 * @var: variable to set.
 * @value: value to set to.
 */
int svar(struct variable_description *var, const char *value)
{
	int vnum;	/* ordinal number of var refrenced */
	int vtype;	/* type of variable to set */
	int status;	/* status return */
	int c;		/* translated character */
	char *sp;	/* scratch string pointer */

	/* simplify the vd structure (we are gonna look at it a lot) */
	vnum = var->v_num;
	vtype = var->v_type;

	/* and set the appropriate value */
	status = true;
	switch (vtype) {
	case TKVAR:		/* set a user variable */
		if (uv[vnum].u_value != nullptr)
            SAFE_FREE(uv[vnum].u_value);
        sp = (char*)safe_alloc(strlen(value) + 1, "user variable value", __FILE__, __LINE__);
        if (sp == nullptr) {
            REPORT_ERROR(ERR_MEMORY, "FAILED TO ALLOCATE MEMORY FOR USER VARIABLE VALUE");
            return false;
        }
        {
            size_t len = strlen(value);
            memcpy(sp, value, len + 1);
        }
        uv[vnum].u_value = sp;
        break;

	case TKENV:		/* set an environment variable */
		status = true;	/* by default */
		switch (vnum) {
		case EVFILLCOL:
			fillcol = safe_atoi(value, 0);
			break;
		case EVPAGELEN:
			status = newsize(true, safe_atoi(value, 0));
			break;
		case EVCURCOL:
			status = setccol(safe_atoi(value, 0));
			break;
		case EVCURLINE:
			status = gotoline(true, safe_atoi(value, 0));
			break;
		case EVRAM:
			break;  /* read-only */
		case EVCURWIDTH:
			status = newwidth(true, safe_atoi(value, 0));
			break;
        case EVCBUFNAME:
            safe_strcpy(curbp->b_bname, value, NBUFN);
            curwp->w_flag |= WFMODE;
            break;
        case EVCFNAME:
            safe_strcpy(curbp->b_fname, value, NFILEN);
            curwp->w_flag |= WFMODE;
            break;
		case EVSRES:
			status = TTrez(value);
			break;
		case EVDEBUG:
			macbug = string_to_bool(value);
			break;
		case EVSTATUS:
			cmdstatus = string_to_bool(value);
			break;
		case EVASAVE:
			gasave = safe_atoi(value, 0);
			break;
		case EVACOUNT:
			gacount = safe_atoi(value, 0);
			break;
		case EVLASTKEY:
			lastkey = safe_atoi(value, 0);
			break;
		case EVCURCHAR:
			ldelchar(1, false);	/* delete 1 char */
			c = safe_atoi(value, 0);
			if (c == '\n')
				lnewline();
			else
				linsert(1, c);
			move_char_backward(false, 1);
			break;
		case EVDISCMD:
			discmd = string_to_bool(value);
			break;
		case EVVERSION:
			break;
		case EVPROGNAME:
			break;
		case EVSEED:
			break;  /* seed is now local to editor_random() */
		case EVDISINP:
			disinp = string_to_bool(value);
			break;
		case EVWLINE:
			status = resize(true, safe_atoi(value, 0));
			break;
		case EVCWLINE:
			status = cursor_down(true, safe_atoi(value, 0) - window_get_position());
			break;
		case EVTARGET:
			curgoal = safe_atoi(value, 0);
			thisflag = saveflag;
			break;
        case EVSEARCH:
            safe_strcpy(pat, value, NPAT);
            rvstrcpy(tap, pat, NPAT);
			mcclear();
			break;
        case EVREPLACE:
            safe_strcpy(rpat, value, NPAT);
            break;
		case EVMATCH:
			break;
		case EVKILL:
			break;
		case EVCMODE:
			curbp->b_mode = (uint32_t)safe_atoi(value, 0);
			curwp->w_flag |= WFMODE;
			break;
		case EVGMODE:
			gmode = safe_atoi(value, 0);
			break;
		case EVTPAUSE:
			term.t_pause = safe_atoi(value, 0);
			break;
		case EVPENDING:
			break;
		case EVLWIDTH:
			break;
		case EVLINE:
			putctext(value);
			break;
		case EVGFLAGS:
			gflags = safe_atoi(value, 0);
			break;
		case EVRVAL:
			break;
		case EVTAB:
			tabmask = safe_atoi(value, 0) - 1;
			if (tabmask != TABMASK_8 && tabmask != TABMASK_4)
				tabmask = TABMASK_8;
			curwp->w_flag |= WFHARD;
			break;
		case EVOVERLAP:
			overlap = safe_atoi(value, 0);
			break;
		case EVSCROLLCOUNT:
			scrollcount = safe_atoi(value, 0);
			break;
		case EVSCROLL:
			if (!string_to_bool(value))
				term.t_scroll = nullptr;
			break;
		case EVHILINE:
			highlight_current_line = string_to_bool(value) ? 1 : 0;
			sgarbf = true; /* force full redraw */
			break;
		case EVRULER:
			column_ruler_enabled = string_to_bool(value) ? 1 : 0;
			sgarbf = true;
			break;
		case EVRULERCOL:
			{
				int col = safe_atoi(value, 0);
				if (col < 1) col = 1;
				if (col > 500) col = 500;
				column_ruler_column = col;
				sgarbf = true;
			}
			break;
        case EVHILINESTYLE:
            {
                int v = safe_atoi(value, 0);
                if (v < 0) { v = 0; }
                if (v > 4) { v = 4; } // 4 = reverse
                hiline_style = v;
                sgarbf = true;
            }
            break;
        case EVRULERSTYLE:
            {
                int v = safe_atoi(value, 0);
                if (v < 0) { v = 0; }
                if (v > 4) { v = 4; } // 4 = reverse
                ruler_style = v;
                sgarbf = true;
            }
            break;
		}
		break;
	}
	return status;
}

/*
 * itoa:
 *	integer to ascii string.......... This is too
 *	inconsistant to use the system's
 *
 * int i;		integer to translate to a string
 */
char *int_to_string(int i)
{
	int digit;	/* current digit being used */
	char *sp;	/* pointer into result */
	int sign;	/* sign of resulting number */
	static _Thread_local char result[INTWIDTH + 1];	/* Thread-safe result */

	/* record the sign... */
	sign = 1;
	if (i < 0) {
		sign = -1;
		i = -i;
	}

	/* and build the string (backwards!) */
	sp = result + INTWIDTH;
	*sp = 0;
	do {
		digit = i % 10;
		*(--sp) = (char)('0' + digit);	/* and install the new digit */
		i = i / 10;
	} while (i);

	/* and fix the sign */
	if (sign == -1) {
		*(--sp) = '-';	/* and install the minus sign */
	}

	return sp;
}

/*
 * find the type of a passed token
 *
 * char *token;		token to analyze
 */
int token_get_type(char *token)
{
	char c;	/* first char in token */

	/* grab the first char (this is all we need) */
	c = *token;

	/* no blanks!!! */
	if (c == 0)
		return TKNUL;

	/* a numeric literal? */
	if (c >= '0' && c <= '9')
		return TKLIT;

	switch (c) {
	case '"':
		return TKSTR;

	case '!':
		return TKDIR;
	case '@':
		return TKARG;
	case '#':
		return TKBUF;
	case '$':
		return TKENV;
	case '%':
		return TKVAR;
	case '&':
		return TKFUN;
	case '*':
		return TKLBL;

	default:
		return TKCMD;
	}
}

/*
 * find the value of a token
 *
 * char *token;		token to evaluate
 */
static const char *internal_getval(char *token)
{
	int status;	/* error return */
	struct buffer *bp;	/* temp buffer pointer */
	int blen;	/* length of buffer argument */
	int distmp;	/* temporary discmd flag */
	static _Thread_local char buf[NSTRING];	/* Thread-safe string buffer */

	switch (token_get_type(token)) {
	case TKNUL:
		return "";

	case TKARG:		/* interactive argument */
		getval(token+1, token, -1);
		distmp = discmd;	/* echo it always! */
		discmd = true;
		status = minibuf_read(token, buf, NSTRING);
		discmd = distmp;
		if (status == ABORT)
			return errorm;
		return buf;

	case TKBUF:		/* buffer contents fetch */

		/* grab the right buffer */
		getval(token+1, token, -1);
		bp = bfind(token, false, 0);
		if (bp == nullptr)
			return errorm;

		/* if the buffer is displayed, get the window
		   vars instead of the buffer vars */
		if (bp->b_nwnd > 0) {
			curbp->b_dotp = curwp->w_dotp;
			curbp->b_doto = curwp->w_doto;
		}

		/* make sure we are not at the end */
		if (bp->b_linep == bp->b_dotp)
			return errorm;

		/* grab the line as an argument */
		blen = llength(bp->b_dotp) - bp->b_doto;
		if (blen > NSTRING)
			blen = NSTRING;
        if (blen > 0) {
            size_t maxlen = NSTRING - 1;
            size_t cpy = (size_t)blen;
            if (cpy > maxlen) cpy = maxlen;
            /* Extract from gap buffer */
            for (size_t i = 0; i < cpy; i++) {
                buf[i] = (char)lgetc(bp->b_dotp, bp->b_doto + (int)i);
            }
            buf[cpy] = '\0';
        } else {
            buf[0] = '\0';
        }
		buf[blen] = 0;

		/* and step the buffer's line ptr ahead a line */
		bp->b_dotp = bp->b_dotp->l_fp;
		bp->b_doto = 0;

		/* if displayed buffer, reset window ptr vars */
		if (bp->b_nwnd > 0) {
			curwp->w_dotp = curbp->b_dotp;
			curwp->w_doto = 0;
			curwp->w_flag |= WFMOVE;
		}

		/* and return the spoils */
		return buf;

	case TKVAR:
		return eval_get_user_var(token + 1);
	case TKENV:
		return eval_get_env_var(token + 1);
	case TKFUN:
		return eval_get_function(token + 1);
	case TKDIR:
		return errorm;
	case TKLBL:
		return errorm;
	case TKLIT:
		return token;
	case TKSTR:
		return token + 1;
	case TKCMD:
		return token;
	}
	return errorm;
}

char *getval(char *token, char *dst, int size)
{
	const char *res = internal_getval(token);
	mystrscpy(dst, res, size);
	return dst;
}

/*
 * convert a string to a numeric logical
 *
 * char *val;		value to check for stol
 */
int string_to_bool(const char *val)
{
	/* check for logical values */
	if (val[0] == 'F')
		return false;
	if (val[0] == 'T')
		return true;

	/* check for numeric truth (!= 0) */
	return (safe_atoi(val, 0) != 0);
}

/*
 * numeric logical to string logical
 *
 * int val;		value to translate
 */
char *bool_to_string(int val)
{
	if (val)
		return truem;
	else
		return falsem;
}

/*
 * make a string upper case
 *
 * char *str;		string to upper case
 */
char *string_to_upper(char *str)
{
	char *sp;

	sp = str;
	while (*sp) {
		if ('a' <= *sp && *sp <= 'z')
			*sp += 'A' - 'a';
		++sp;
	}
	return str;
}

/*
 * make a string lower case
 *
 * char *str;		string to lower case
 */
char *string_to_lower(char *str)
{
	char *sp;

	sp = str;
	while (*sp) {
		if ('A' <= *sp && *sp <= 'Z')
			*sp += 'a' - 'A';
		++sp;
	}
	return str;
}

/*
 * take the absolute value of an integer
 */
int abs(int x)
{
	return x < 0 ? -x : x;
}

/*
 * returns a random integer
 */
int editor_random(void)
{
	static int seed = 0;
	seed = abs(seed * 1721 + 10007);
	return seed;
}

/*
 * find pattern within source
 *
 * char *source;	source string to search
 * char *pattern;	string to look for
 */
int string_find_index(const char *source, const char *pattern)
{
	const char *sp;		/* ptr to current position to scan */
	const char *csp;		/* ptr to source string during comparison */
	const char *cp;		/* ptr to place to check for equality */

	/* scanning through the source string */
	sp = source;
	while (*sp) {
		/* scan through the pattern */
		cp = pattern;
		csp = sp;
		while (*cp) {
			if (!eq((unsigned char)*cp, (unsigned char)*csp))
				break;
			++cp;
			++csp;
		}

		/* was it a match? */
		if (*cp == 0)
			return (int) (sp - source) + 1;
		++sp;
	}

	/* no match at all.. */
	return 0;
}

/*
 * Filter a string through a translation table
 *
 * char *source;	string to filter
 * char *lookup;	characters to translate
 * char *trans;		resulting translated characters
 */
char *string_translate(char *source, char *lookup, char *trans)
{
	char *sp;	/* pointer into source table */
	char *lp;	/* pointer into lookup table */
	char *rp;	/* pointer into result */
	static _Thread_local char result[NSTRING];	/* Thread-safe result */

	/* scan source string */
	sp = source;
	rp = result;
	while (*sp) {
		/* scan lookup table for a match */
		lp = lookup;
		while (*lp) {
			if (*sp == *lp) {
				*rp++ = trans[lp - lookup];
				goto xnext;
			}
			++lp;
		}

		/* no match, copy in the source char untranslated */
		*rp++ = *sp;

	      xnext:++sp;
	}

	/* terminate and return the result */
	*rp = 0;
	return result;
}

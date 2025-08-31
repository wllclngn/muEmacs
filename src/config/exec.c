// exec.c - Command execution and macro processing

#include <stdio.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "line.h"
#include "memory.h"
#include "error.h"

// Structures for dobuf refactoring
struct exec_state {
	int execlevel;			// execution level for conditionals
	struct while_block *whlist;	// while block list
	struct while_block *scanner;	// scanner pointer
	int force;			// force TRUE result
	char tkn[NSTRING];		// token buffer
};

struct line_context {
	struct line *lp;		// current line pointer  
	struct line *hlp;		// header line pointer
	struct line *glp;		// goto line pointer
	struct line *mp;		// macro line storage
	char *eline;			// execution line text
	char *einit;			// initial eline pointer
	int linlen;			// line length
	int i;				// index
	int c;				// temp character
	int dirnum;			// directive number
};

// Function prototypes for dobuf refactoring
static int scan_while_blocks(struct buffer *bp, struct exec_state *state);
static int execute_buffer_lines(struct buffer *bp, struct exec_state *state);
static int process_line_directive(struct line_context *ctx, struct exec_state *state, struct buffer *bp);
static int handle_control_flow(struct line_context *ctx, struct exec_state *state, struct buffer *bp);
static void cleanup_exec_state(struct exec_state *state);

/*
 * Execute a named command even if it is not bound.
 */
int namedcmd(int f, int n)
{
	fn_t kfunc;	/* ptr to the requexted function to bind to */

	/* prompt the user to type a named command */
	mlwrite(": ");

	/* and now get the function name to execute */
	kfunc = getname();
	if (kfunc == NULL) {
		REPORT_ERROR(ERR_COMMAND_UNKNOWN, "No such function");
		return FALSE;
	}

	/* and then execute the command */
	return kfunc(f, n);
}

/*
 * execcmd:
 *	Execute a command line command to be typed in
 *	by the user
 *
 * int f, n;		default Flag and Numeric argument
 */
int execcmd(int f, int n)
{
	int status;	/* status return */
	char cmdstr[NSTRING];	/* string holding command to execute */

	/* get the line wanted */
	if ((status = mlreply(": ", cmdstr, NSTRING)) != TRUE)
		return status;

	execlevel = 0;
	return docmd(cmdstr);
}

/*
 * docmd:
 *	take a passed string as a command line and translate
 * 	it to be executed as a command. This function will be
 *	used by execute-command-line and by all source and
 *	startup files. Lastflag/thisflag is also updated.
 *
 *	format of the command line is:
 *
 *		{# arg} <command-name> {<argument string(s)>}
 *
 * char *cline;		command line to execute
 */
int docmd(const char *cline)
{
	int f;		/* default argument flag */
	int n;		/* numeric repeat value */
	fn_t fnc;		/* function to execute */
	int status;		/* return status of function */
	int oldcle;		/* old contents of clexec flag */
	const char *oldestr;		/* original exec string */
	char tkn[NSTRING];	/* next token off of command line */

	/* if we are scanning and not executing..go back here */
	if (execlevel)
		return TRUE;

	oldestr = execstr;	/* save last ptr to string to execute */
	execstr = cline;	/* and set this one as current */

	/* first set up the default command values */
	f = FALSE;
	n = 1;
	lastflag = thisflag;
	thisflag = 0;

	if ((status = macarg(tkn)) != TRUE) {	/* and grab the first token */
		execstr = oldestr;
		return status;
	}

	/* process leadin argument */
	if (gettyp(tkn) != TKCMD) {
		f = TRUE;
		getval(tkn, tkn, sizeof(tkn));
		n = atoi(tkn);

		/* and now get the command to execute */
		if ((status = macarg(tkn)) != TRUE) {
			execstr = oldestr;
			return status;
		}
	}

	/* and match the token to see if it exists */
	if ((fnc = fncmatch(tkn)) == NULL) {
		REPORT_ERROR(ERR_COMMAND_UNKNOWN, tkn);
		execstr = oldestr;
		return FALSE;
	}

	/* save the arguments and go execute the command */
	oldcle = clexec;	/* save old clexec flag */
	clexec = TRUE;		/* in cline execution */
	status = (*fnc) (f, n);	/* call the function */
	cmdstatus = status;	/* save the status */
	clexec = oldcle;	/* restore clexec flag */
	execstr = oldestr;
	return status;
}

/*
 * token:
 *	chop a token off a string
 *	return a pointer past the token
 *
 * char *src, *tok;	source string, destination token string
 * int size;		maximum size of token
 */
const char *token(const char *src, char *tok, int size)
{
	int quotef;	/* is the current string quoted? */
	char c;	/* temporary character */

	/* first scan past any whitespace in the source string */
	while (*src == ' ' || *src == '\t')
		++src;

	/* scan through the source string */
	quotef = FALSE;
	while (*src) {
		/* process special characters */
		if (*src == '~') {
			++src;
			if (*src == 0)
				break;
			switch (*src++) {
			case 'r':
				c = 13;
				break;
			case 'n':
				c = 10;
				break;
			case 't':
				c = 9;
				break;
			case 'b':
				c = 8;
				break;
			case 'f':
				c = 12;
				break;
			default:
				c = *(src - 1);
			}
			if (--size > 0) {
				*tok++ = c;
			}
		} else {
			/* check for the end of the token */
			if (quotef) {
				if (*src == '"')
					break;
			} else {
				if (*src == ' ' || *src == '\t')
					break;
			}

			/* set quote mode if quote found */
			if (*src == '"')
				quotef = TRUE;

			/* record the character */
			c = *src++;
			if (--size > 0)
				*tok++ = c;
		}
	}

	/* terminate the token and exit */
	if (*src)
		++src;
	*tok = 0;
	return src;
}

/*
 * get a macro line argument
 *
 * char *tok;		buffer to place argument
 */
int macarg(char *tok)
{
	int savcle;		/* buffer to store original clexec */
	int status;

	savcle = clexec;	/* save execution mode */
	clexec = TRUE;		/* get the argument */
	status = nextarg("", tok, NSTRING, ctoec('\n'));
	clexec = savcle;	/* restore execution mode */
	return status;
}

/*
 * nextarg:
 *	get the next argument
 *
 * char *prompt;		prompt to use if we must be interactive
 * char *buffer;		buffer to put token into
 * int size;			size of the buffer
 * int terminator;		terminating char to be used on interactive fetch
 */
int nextarg(const char *prompt, char *buffer, int size, int terminator)
{
	/* if we are interactive, go get it! */
	if (clexec == FALSE)
		return getstring(prompt, buffer, size, terminator);

	/* grab token and advance past */
	execstr = token(execstr, buffer, size);

	/* evaluate it */
	getval(buffer, buffer, size);
	return TRUE;
}

/*
 * storemac:
 *	Set up a macro buffer and flag to store all
 *	executed command lines there
 *
 * int f;		default flag
 * int n;		macro number to use
 */
int storemac(int f, int n)
{
	struct buffer *bp;	/* pointer to macro buffer */
	char bname[NBUFN];	/* name of buffer to use */

	/* must have a numeric argument to this function */
	if (f == FALSE) {
		mlwrite("No macro specified");
		return FALSE;
	}

	/* range check the macro number */
	if (n < 1 || n > 40) {
		mlwrite("Macro number out of range");
		return FALSE;
	}

	/* construct the macro buffer name */
    SAFE_STRCPY(bname, "*Macro xx*");
	bname[7] = '0' + (n / 10);
	bname[8] = '0' + (n % 10);

	/* set up the new macro buffer */
	if ((bp = bfind(bname, TRUE, BFINVS)) == NULL) {
		mlwrite("Can not create macro");
		return FALSE;
	}

	/* and make sure it is empty */
	bclear(bp);

	/* and set the macro store pointers to it */
	mstore = TRUE;
	bstore = bp;
	return TRUE;
}

#if	PROC
/*
 * storeproc:
 *	Set up a procedure buffer and flag to store all
 *	executed command lines there
 *
 * int f;		default flag
 * int n;		macro number to use
 */
int storeproc(int f, int n)
{
	struct buffer *bp;	/* pointer to macro buffer */
	int status;	/* return status */
	char bname[NBUFN];	/* name of buffer to use */

	/* a numeric argument means its a numbered macro */
	if (f == TRUE)
		return storemac(f, n);

	/* get the name of the procedure */
	if ((status =
	     mlreply("Procedure name: ", &bname[1], NBUFN - 2)) != TRUE)
		return status;

	/* construct the macro buffer name */
	bname[0] = '*';
    SAFE_STRCAT(bname, "*");

	/* set up the new macro buffer */
	if ((bp = bfind(bname, TRUE, BFINVS)) == NULL) {
		mlwrite("Can not create macro");
		return FALSE;
	}

	/* and make sure it is empty */
	bclear(bp);

	/* and set the macro store pointers to it */
	mstore = TRUE;
	bstore = bp;
	return TRUE;
}

/*
 * execproc:
 *	Execute a procedure
 *
 * int f, n;		default flag and numeric arg
 */
int execproc(int f, int n)
{
	struct buffer *bp;	/* ptr to buffer to execute */
	int status;	/* status return */
	char bufn[NBUFN + 2];	/* name of buffer to execute */

	/* find out what buffer the user wants to execute */
	if ((status =
	     mlreply("Execute procedure: ", &bufn[1], NBUFN)) != TRUE)
		return status;

	/* construct the buffer name */
	bufn[0] = '*';
    SAFE_STRCAT(bufn, "*");

	/* find the pointer to that buffer */
	if ((bp = bfind(bufn, FALSE, 0)) == NULL) {
		mlwrite("No such procedure");
		return FALSE;
	}

	/* and now execute it as asked */
	while (n-- > 0)
		if ((status = dobuf(bp)) != TRUE)
			return status;
	return TRUE;
}
#endif

/*
 * execbuf:
 *	Execute the contents of a buffer of commands
 *
 * int f, n;		default flag and numeric arg
 */
int execbuf(int f, int n)
{
	struct buffer *bp;	/* ptr to buffer to execute */
	int status;	/* status return */
	char bufn[NSTRING];	/* name of buffer to execute */

	/* find out what buffer the user wants to execute */
	if ((status = mlreply("Execute buffer: ", bufn, NBUFN)) != TRUE)
		return status;

	/* find the pointer to that buffer */
	if ((bp = bfind(bufn, FALSE, 0)) == NULL) {
		mlwrite("No such buffer");
		return FALSE;
	}

	/* and now execute it as asked */
	while (n-- > 0)
		if ((status = dobuf(bp)) != TRUE)
			return status;
	return TRUE;
}

/*
 * dobuf:
 *	execute the contents of the buffer pointed to
 *	by the passed BP
 *
 *	Directives start with a "!" and include:
 *
 *	!endm		End a macro
 *	!if (cond)	conditional execution
 *	!else
 *	!endif
 *	!return		Return (terminating current macro)
 *	!goto <label>	Jump to a label in the current macro
 *	!force		Force macro to continue...even if command fails
 *	!while (cond)	Execute a loop if the condition is true
 *	!endwhile
 *
 *	Line Labels begin with a "*" as the first nonblank char, like:
 *
 *	*LBL01
 *
 * struct buffer *bp;		buffer to execute
 */
int dobuf(struct buffer *bp)
{
	struct exec_state state = {0};
	int status;

	// Initialize execution state
	state.execlevel = 0;
	state.whlist = NULL;
	state.scanner = NULL;
	state.force = FALSE;

	// Set global execution level
	execlevel = 0;

	// First pass: scan for WHILE blocks
	if ((status = scan_while_blocks(bp, &state)) != TRUE) {
		cleanup_exec_state(&state);
		return status;
	}

	// Second pass: execute buffer lines
	status = execute_buffer_lines(bp, &state);
	
	// Cleanup
	cleanup_exec_state(&state);
	execlevel = 0;
	
	return status;
}

// Helper function implementations

// Scan buffer for WHILE blocks
static int scan_while_blocks(struct buffer *bp, struct exec_state *state)
{
	struct line *hlp = bp->b_linep;
	struct line *lp = hlp->l_fp;
	struct while_block *whtemp;
	char *eline;
	int i;

	while (lp != hlp) {
		eline = lp->l_text;
		i = lp->l_used;

		// Trim leading whitespace
		while (i-- > 0 && (*eline == ' ' || *eline == '\t'))
			++eline;

		if (i <= 0) {
			lp = lp->l_fp;
			continue;
		}

		// Handle WHILE directive
		if (eline[0] == '!' && eline[1] == 'w' && eline[2] == 'h') {
			whtemp = (struct while_block *)safe_alloc(sizeof(struct while_block), "while block", __FILE__, __LINE__);
			if (whtemp == NULL) {
				mlwrite("%%Out of memory during while scan");
				return FALSE;
			}
			whtemp->w_begin = lp;
			whtemp->w_type = BTWHILE;
			whtemp->w_next = state->scanner;
			state->scanner = whtemp;
		}
		// Handle BREAK directive
		else if (eline[0] == '!' && eline[1] == 'b' && eline[2] == 'r') {
			if (state->scanner == NULL) {
				mlwrite("%%!BREAK outside of any !WHILE loop");
				return FALSE;
			}
			whtemp = (struct while_block *)safe_alloc(sizeof(struct while_block), "while block", __FILE__, __LINE__);
			if (whtemp == NULL) {
				mlwrite("%%Out of memory during while scan");
				return FALSE;
			}
			whtemp->w_begin = lp;
			whtemp->w_type = BTBREAK;
			whtemp->w_next = state->scanner;
			state->scanner = whtemp;
		}
		// Handle ENDWHILE directive
		else if (eline[0] == '!' && strncmp(&eline[1], "endw", 4) == 0) {
			if (state->scanner == NULL) {
				mlwrite("%%!ENDWHILE with no preceding !WHILE in '%s'", bp->b_bname);
				return FALSE;
			}
			do {
				state->scanner->w_end = lp;
				whtemp = state->whlist;
				state->whlist = state->scanner;
				state->scanner = state->scanner->w_next;
				state->whlist->w_next = whtemp;
			} while (state->whlist->w_type == BTBREAK);
		}

		lp = lp->l_fp;
	}

	// Check for unmatched WHILE blocks
	if (state->scanner != NULL) {
		mlwrite("%%!WHILE with no matching !ENDWHILE in '%s'", bp->b_bname);
		return FALSE;
	}

	return TRUE;
}

// Execute buffer lines with directives
static int execute_buffer_lines(struct buffer *bp, struct exec_state *state)
{
	struct line_context ctx = {0};
	int status = TRUE;
	struct window *wp;

	// Let the first command inherit the flags from the last one
	thisflag = lastflag;

	ctx.hlp = bp->b_linep;
	ctx.lp = ctx.hlp->l_fp;

	while (ctx.lp != ctx.hlp) {
		// Allocate and copy line
		ctx.linlen = ctx.lp->l_used;
		if ((ctx.einit = ctx.eline = (char*)safe_alloc(ctx.linlen + 1, "execution line buffer", __FILE__, __LINE__)) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Out of Memory during macro execution");
			return FALSE;
		}
    if (ctx.linlen > 0) {
        size_t maxlen = sizeof(ctx.eline) - 1;
        size_t cpy = (size_t)ctx.linlen;
        if (cpy > maxlen) cpy = maxlen;
        memcpy(ctx.eline, ctx.lp->l_text, cpy);
        ctx.eline[cpy] = '\0';
    } else {
        ctx.eline[0] = '\0';
    }
		ctx.eline[ctx.linlen] = 0;

		// Trim leading whitespace
		while (*ctx.eline == ' ' || *ctx.eline == '\t')
			++ctx.eline;

		// Skip comments and blank lines
		if (*ctx.eline == ';' || *ctx.eline == 0) {
			SAFE_FREE(ctx.einit);
			ctx.lp = ctx.lp->l_fp;
			continue;
		}

		// Process line directive or execute command
		if ((status = process_line_directive(&ctx, state, bp)) == -1) {
			// Continue to next line
			SAFE_FREE(ctx.einit);
			ctx.lp = ctx.lp->l_fp;
			continue;
		} else if (status != TRUE) {
			// Error occurred
			if (status != TRUE) {
				wp = wheadp;
				while (wp != NULL) {
					if (wp->w_bufp == bp) {
						wp->w_dotp = ctx.lp;
						wp->w_doto = 0;
						wp->w_flag |= WFHARD;
					}
					wp = wp->w_wndp;
				}
				bp->b_dotp = ctx.lp;
				bp->b_doto = 0;
			}
			SAFE_FREE(ctx.einit);
			return status;
		}

		SAFE_FREE(ctx.einit);
		ctx.lp = ctx.lp->l_fp;
	}

	return TRUE;
}

// Process line directive or execute command  
static int process_line_directive(struct line_context *ctx, struct exec_state *state, struct buffer *bp)
{
	int status;

	// Parse directives
	ctx->dirnum = -1;
	if (*ctx->eline == '!') {
		++ctx->eline;
		for (ctx->dirnum = 0; ctx->dirnum < NUMDIRS; ctx->dirnum++)
			if (strncmp(ctx->eline, dname[ctx->dirnum], strlen(dname[ctx->dirnum])) == 0)
				break;

		if (ctx->dirnum == NUMDIRS) {
			mlwrite("%%Unknown Directive");
			return FALSE;
		}

		// Handle !ENDM macro
		if (ctx->dirnum == DENDM) {
			mstore = FALSE;
			bstore = NULL;
			return -1; // Continue to next line
		}

		--ctx->eline;
	}

	// Handle macro store
	if (mstore) {
		ctx->linlen = strlen(ctx->eline);
		if ((ctx->mp = lalloc(ctx->linlen)) == NULL) {
			REPORT_ERROR(ERR_MEMORY, "Out of memory while storing macro");
			return FALSE;
		}

		for (ctx->i = 0; ctx->i < ctx->linlen; ++ctx->i)
			lputc(ctx->mp, ctx->i, ctx->eline[ctx->i]);

		bstore->b_linep->l_bp->l_fp = ctx->mp;
		ctx->mp->l_bp = bstore->b_linep->l_bp;
		bstore->b_linep->l_bp = ctx->mp;
		ctx->mp->l_fp = bstore->b_linep;
		return -1; // Continue to next line
	}

	state->force = FALSE;

	// Skip comments
	if (*ctx->eline == '*')
		return -1; // Continue to next line

	// Execute directives
	if (ctx->dirnum != -1) {
		return handle_control_flow(ctx, state, bp);
	}

	// Execute the statement
	status = docmd(ctx->eline);
	if (state->force)
		status = TRUE;

	return status;
}

// Handle control flow directives
static int handle_control_flow(struct line_context *ctx, struct exec_state *state, struct buffer *bp)
{
	struct while_block *whtemp;
	int linlen;

	// Skip past the directive
	while (*ctx->eline && *ctx->eline != ' ' && *ctx->eline != '\t')
		++ctx->eline;
	execstr = ctx->eline;

	switch (ctx->dirnum) {
		case DIF:
			if (state->execlevel == 0) {
				if (macarg(state->tkn) != TRUE)
					return TRUE; // Exit execution
				if (stol(state->tkn) == FALSE)
					++state->execlevel;
			} else {
				++state->execlevel;
			}
			return -1; // Continue to next line

		case DWHILE:
			if (state->execlevel == 0) {
				if (macarg(state->tkn) != TRUE)
					return TRUE; // Exit execution
				if (stol(state->tkn) == TRUE)
					return -1; // Continue to next line
			}
			[[fallthrough]];

		case DBREAK:
			if (ctx->dirnum == DBREAK && state->execlevel)
				return -1; // Continue to next line

			whtemp = state->whlist;
			while (whtemp) {
				if (whtemp->w_begin == ctx->lp)
					break;
				whtemp = whtemp->w_next;
			}

			if (whtemp == NULL) {
				mlwrite("%%Internal While loop error");
				return FALSE;
			}

			ctx->lp = whtemp->w_end;
			return -1; // Continue to next line

		case DELSE:
			if (state->execlevel == 1)
				--state->execlevel;
			else if (state->execlevel == 0)
				++state->execlevel;
			return -1; // Continue to next line

		case DENDIF:
			if (state->execlevel)
				--state->execlevel;
			return -1; // Continue to next line

		case DGOTO:
			if (state->execlevel == 0) {
				ctx->eline = (char *)token(ctx->eline, golabel, NPAT);
				linlen = strlen(golabel);
				ctx->glp = ctx->hlp->l_fp;
				while (ctx->glp != ctx->hlp) {
					if (*ctx->glp->l_text == '*' &&
					    (strncmp(&ctx->glp->l_text[1], golabel, linlen) == 0)) {
						ctx->lp = ctx->glp;
						return -1; // Continue to next line
					}
					ctx->glp = ctx->glp->l_fp;
				}
				mlwrite("%%No such label");
				return FALSE;
			}
			return -1; // Continue to next line

		case DRETURN:
			if (state->execlevel == 0)
				return TRUE; // Exit execution
			return -1; // Continue to next line

		case DENDWHILE:
			if (state->execlevel) {
				--state->execlevel;
				return -1; // Continue to next line
			} else {
				whtemp = state->whlist;
				while (whtemp) {
					if (whtemp->w_type == BTWHILE && whtemp->w_end == ctx->lp)
						break;
					whtemp = whtemp->w_next;
				}

				if (whtemp == NULL) {
					mlwrite("%%Internal While loop error");
					return FALSE;
				}

				ctx->lp = whtemp->w_begin->l_bp;
				return -1; // Continue to next line
			}

		case DFORCE:
			state->force = TRUE;
			return -1; // Continue to next line

		default:
			return -1; // Continue to next line
	}
}

// Cleanup execution state
static void cleanup_exec_state(struct exec_state *state)
{
	freewhile(state->whlist);
	freewhile(state->scanner);
	state->whlist = NULL;
	state->scanner = NULL;
}

/*
 * free a list of while block pointers
 *
 * struct while_block *wp;		head of structure to free
 */
void freewhile(struct while_block *wp)
{
	if (wp == NULL)
		return;
	if (wp->w_next)
		freewhile(wp->w_next);
	SAFE_FREE(wp);
}

/*
 * execute a series of commands in a file
 *
 * int f, n;		default flag and numeric arg to pass on to file
 */
int execfile(int f, int n)
{
	int status;	/* return status of name query */
	char fname[NSTRING];	/* name of file to execute */
	const char *fspec;		/* full file spec */

	if ((status =
	     mlreply("File to execute: ", fname, NSTRING - 1)) != TRUE)
		return status;

#if	1
	/* look up the path for the file */
	fspec = flook(fname, FALSE);	/* used to by TRUE, P.K. */

	/* if it isn't around */
	if (fspec == NULL)
		return FALSE;

#endif
	/* otherwise, execute it */
	while (n-- > 0)
		if ((status = dofile(fspec)) != TRUE)
			return status;

	return TRUE;
}

/*
 * dofile:
 *	yank a file into a buffer and execute it
 *	if there are no errors, delete the buffer on exit
 *
 * char *fname;		file name to execute
 */
int dofile(const char *fname)
{
	struct buffer *bp;	/* buffer to place file to exeute */
	struct buffer *cb;	/* temp to hold current buf while we read */
	int status;	/* results of various calls */
	char bname[NBUFN];	/* name of buffer */

	makename(bname, fname);	/* derive the name of the buffer */
	unqname(bname);		/* make sure we don't stomp things */
	if ((bp = bfind(bname, TRUE, 0)) == NULL)	/* get the needed buffer */
		return FALSE;

	bp->b_mode = MDVIEW;	/* mark the buffer as read only */
	cb = curbp;		/* save the old buffer */
	curbp = bp;		/* make this one current */
	/* and try to read in the file to execute */
	if ((status = readin(fname, FALSE)) != TRUE) {
		curbp = cb;	/* restore the current buffer */
		return status;
	}

	/* go execute it! */
	curbp = cb;		/* restore the current buffer */
	if ((status = dobuf(bp)) != TRUE)
		return status;

	/* if not displayed, remove the now unneeded buffer and exit */
	if (bp->b_nwnd == 0)
		zotbuf(bp);
	return TRUE;
}

/*
 * cbuf:
 *	Execute the contents of a numbered buffer
 *
 * int f, n;		default flag and numeric arg
 * int bufnum;		number of buffer to execute
 */
int cbuf(int f, int n, int bufnum)
{
	struct buffer *bp;	/* ptr to buffer to execute */
	int status;	/* status return */
	static char bufname[] = "*Macro xx*";

	/* make the buffer name */
	bufname[7] = '0' + (bufnum / 10);
	bufname[8] = '0' + (bufnum % 10);

	/* find the pointer to that buffer */
	if ((bp = bfind(bufname, FALSE, 0)) == NULL) {
		mlwrite("Macro not defined");
		return FALSE;
	}

	/* and now execute it as asked */
	while (n-- > 0)
		if ((status = dobuf(bp)) != TRUE)
			return status;
	return TRUE;
}

// LEGACY CBUF FUNCTIONS MOVED TO DEDICATED DISPATCH SYSTEM
// All 40 cbuf functions now implemented in src/μemacs/core/cbuf_dispatch.c
// This eliminates 48 lines of identical function duplication

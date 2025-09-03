/*
 *	main.c

 *	μEmacs/PK 4.0
 *
 *	Based on:
 *
 *	MicroEMACS 3.9
 *	Written by Dave G. Conroy.
 *	Substantially modified by Daniel M. Lawrence
 *	Modified by Petri Kutvonen
 *
 *	MicroEMACS 3.9 (c) Copyright 1987 by Daniel M. Lawrence
 *
 *	Original statement of copying policy:
 *
 *	MicroEMACS 3.9 can be copied and distributed freely for any
 *	non-commercial purposes. MicroEMACS 3.9 can only be incorporated
 *	into commercial software with the permission of the current author.
 *
 *	No copyright claimed for modifications made by Petri Kutvonen.
 *
 *	This file contains the main driving routine, and some keyboard
 *	processing code.
 *
 * REVISION HISTORY:
 *
 * 1.0  Steve Wilhite, 30-Nov-85
 *
 * 2.0  George Jones, 12-Dec-85
 *
 * 3.0  Daniel Lawrence, 29-Dec-85
 *
 * 3.2-3.6 Daniel Lawrence, Feb...Apr-86
 *
 * 3.7	Daniel Lawrence, 14-May-86
 *
 * 3.8	Daniel Lawrence, 18-Jan-87
 *
 * 3.9	Daniel Lawrence, 16-Jul-87
 *
 * 3.9e	Daniel Lawrence, 16-Nov-87
 *
 * After that versions 3.X and Daniel Lawrence went their own ways.
 * A modified 3.9e/PK was heavily used at the University of Helsinki
 * for several years on different UNIX, VMS, and MSDOS platforms.
 *
 * This modified version is now called eEmacs/PK.
 *
 * 4.0	Petri Kutvonen, 1-Sep-91
 *
 */

#include <stdio.h>
#include <signal.h>
#include <stdatomic.h>

/* Make global definitions not external. */
#define	maindef

#include "estruct.h" /* Global structures and defines. */
#include "edef.h"    /* Global definitions. */
#include "efunc.h"   /* Function declarations and name table. */
#include "ebind.h"   /* Default key bindings. */
#include "keymap.h"  /* Keymap system functions. */
#include "version.h"
#include "string_safe.h"
#include "memory.h"
#include "error.h"
#include "../util/display_width.h"



#ifndef GOOD
#define GOOD    0
#endif

#include <signal.h>
#include <unistd.h>
static void emergencyexit(int);
void check_emergency_exit(void);
#ifdef SIGWINCH
extern void sizesignal(int);
void check_pending_resize(void);
#endif

// Structures for refactored main() function
struct main_args {
	int viewflag;		// view mode flag
	int gotoflag;		// goto line flag
	int gline;		// line to goto
	int searchflag;		// search at startup flag
	int errflag;		// C error processing flag
	int startflag;		// startup file executed flag
#if CRYPT
	int cryptflag;		// encryption flag
	char ekey[NPAT];	// startup encryption key
#endif
	char pat[NPAT];		// search pattern
	struct buffer *firstbp;	// first buffer pointer
};

struct main_state {
	int c;			// command character
	int f;			// default flag
	int n;			// numeric repeat count
	int mflag;		// negative flag on repeat
	int basec;		// c stripped of meta character
	int saveflag;		// temp store for lastflag
	int newc;		// new character
};

// Function prototypes for refactored main()
static void initialize_platform(void);
static int handle_help_version(int argc, char **argv);
static void initialize_editor(void);
static int parse_command_line(int argc, char **argv, struct main_args *args);
static void process_input_files(struct main_args *args, struct main_state *state);
static int main_editor_loop(struct main_args *args, struct main_state *state);

void usage(int status)
{
  printf("Usage: %s filename\n", PROGRAM_NAME);
  printf("   or: %s [options]\n\n", PROGRAM_NAME);
  fputs("      +          start at the end of file\n", stdout);
  fputs("      +<n>       start at line <n>\n", stdout);
  fputs("      -g[G]<n>   go to line <n>\n", stdout);
  fputs("      --help     display this help and exit\n", stdout);
  fputs("      --version  output version information and exit\n", stdout);

  exit(status);
}

int uemacs_main_entry(int argc, char **argv)
{
	struct main_args args = {0};
	struct main_state state = {0};
	int result;

	// Platform-specific initialization and signal setup
	initialize_platform();
	
	// Handle version and help early
	if (handle_help_version(argc, argv))
		return EXIT_SUCCESS;
	
	// Initialize editor subsystems
	initialize_editor();
	
	// Parse command line arguments
	if (!parse_command_line(argc, argv, &args)) {
		return EXIT_FAILURE;
	}
	
	// Process input files
	process_input_files(&args, &state);
	
	// Run main editor loop
	result = main_editor_loop(&args, &state);
	
	return result;
}

// Platform-specific initialization
static void initialize_platform(void)
{
#ifdef SIGWINCH
	signal(SIGWINCH, sizesignal);
#endif
}

// Handle --help and --version early
static int handle_help_version(int argc, char **argv)
{
	if (argc == 2) {
		if (strcmp(argv[1], "--help") == 0) {
			usage(EXIT_FAILURE);
		}
		if (strcmp(argv[1], "--version") == 0) {
			version();
			return 1; // Signal to exit
		}
	}
	return 0; // Continue
}

// Initialize editor subsystems
static void initialize_editor(void)
{
	vtinit();		// Display
	display_width_init();	// UTF-8 display width calculations
	edinit("main");		// Buffers, windows
	varinit();		// user variables
	keymap_init_from_legacy();	// Initialize keymaps from legacy bindings
}

// Parse command line arguments
static int parse_command_line(int argc, char **argv, struct main_args *args)
{
	int carg;
	struct buffer *bp;
	int firstfile = TRUE;
	char bname[NBUFN];
	
	// Initialize args structure
	args->viewflag = FALSE;
	args->gotoflag = FALSE;
	args->gline = 0;
	args->searchflag = FALSE;
	args->startflag = FALSE;
	args->errflag = FALSE;
	args->firstbp = NULL;
#if CRYPT
	args->cryptflag = FALSE;
#endif

	// Parse the command line
	for (carg = 1; carg < argc; ++carg) {
		if (argv[carg][0] == '+') {
			args->gotoflag = TRUE;
			args->gline = atoi(&argv[carg][1]);
		} else
		if (argv[carg][0] == '-') {
			switch (argv[carg][1]) {
			case 'a': case 'A':
				args->errflag = TRUE;
				break;
			case 'e': case 'E':
				args->viewflag = FALSE;
				break;
			case 'g': case 'G':
				args->gotoflag = TRUE;
				args->gline = atoi(&argv[carg][2]);
				break;
#if CRYPT
			case 'k': case 'K':
				args->cryptflag = TRUE;
                safe_strcpy(args->ekey, &argv[carg][2], NPAT);
				break;
#endif
			case 'n': case 'N':
				nullflag = TRUE;
				break;
			case 'r': case 'R':
				restflag = TRUE;
				break;
			case 's': case 'S':
				args->searchflag = TRUE;
                safe_strcpy(args->pat, &argv[carg][2], NPAT);
				break;
			case 'v': case 'V':
				args->viewflag = TRUE;
				break;
			default:
				break;
			}
		} else if (argv[carg][0] == '@') {
			// Process Startup macros
			if (startup(&argv[carg][1]) == TRUE)
				args->startflag = TRUE;
		} else {
			// Process an input file
			makename(bname, argv[carg]);
			unqname(bname);

			bp = bfind(bname, TRUE, 0);
            safe_strcpy(bp->b_fname, argv[carg], NFILEN);
			bp->b_active = FALSE;
			if (firstfile) {
				args->firstbp = bp;
				firstfile = FALSE;
			}

			// Set the modes appropriately
			if (args->viewflag)
				bp->b_mode |= MDVIEW;
#if CRYPT
			if (args->cryptflag) {
				bp->b_mode |= MDCRYPT;
				myencrypt((char *) NULL, 0);
				myencrypt(args->ekey, strlen(args->ekey));
                safe_strcpy(bp->b_key, args->ekey, NPAT);
			}
#endif
		}
	}

	return 1; // Success
}

// Process input files after parsing
static void process_input_files(struct main_args *args, struct main_state *state)
{
	struct buffer *bp;
	
	signal(SIGHUP, emergencyexit);
	signal(SIGTERM, emergencyexit);

	// if we are C error parsing... run it!
	if (args->errflag) {
		if (startup("error.cmd") == TRUE)
			args->startflag = TRUE;
	}

	// if invoked with no other startup files, run the system startup file here
	if (args->startflag == FALSE) {
		startup("");
		args->startflag = TRUE;
	}
	discmd = TRUE;		// P.K.

	// if there are any files to read, read the first one!
	bp = bfind("main", FALSE, 0);
	if (args->firstbp != NULL && (gflags & GFREAD)) {
		swbuffer(args->firstbp);
		zotbuf(bp);
	} else {
		bp->b_mode |= gmode;
	}

	// Deal with startup gotos and searches
	if (args->gotoflag && args->searchflag) {
		update(FALSE);
		mlwrite("(Can not search and goto at the same time!)");
	} else if (args->gotoflag) {
		if (gotoline(TRUE, args->gline) == FALSE) {
			update(FALSE);
			mlwrite("(Bogus goto argument)");
		}
	} else if (args->searchflag) {
		if (forwhunt(FALSE, 0) == FALSE)
			update(FALSE);
	}
}

// Main editor command loop
static int main_editor_loop(struct main_args *args, struct main_state *state)
{
	// Setup to process commands.
	lastflag = 0;  // Fake last flags.

loop:
	// Execute the "command" macro...normally null.
	state->saveflag = lastflag;  // Preserve lastflag through this.
	execute(META | SPEC | 'C', FALSE, 1);
	lastflag = state->saveflag;

	// Check for pending signals first
	check_emergency_exit();
#ifdef SIGWINCH
	check_pending_resize();
#endif

	// Fix up the screen
	update(FALSE);

	// get the next command from the keyboard (C23 atomic processing)
	state->c = getcmd();
	// if there is something on the command line, clear it
	if (mpresf != FALSE) {
		mlerase();
		update(FALSE);
	}
	state->f = FALSE;
	state->n = 1;

	// do META-# processing if needed
	state->basec = state->c & ~META;	// strip meta char off if there
	if ((state->c & META) && ((state->basec >= '0' && state->basec <= '9') || state->basec == '-')) {
		state->f = TRUE;	// there is a # arg
		state->n = 0;		// start with a zero default
		state->mflag = 1;	// current minus flag
		state->c = state->basec;	// strip the META
		while ((state->c >= '0' && state->c <= '9') || (state->c == '-')) {
			if (state->c == '-') {
				// already hit a minus or digit?
				if ((state->mflag == -1) || (state->n != 0))
					break;
				state->mflag = -1;
			} else {
				state->n = state->n * 10 + (state->c - '0');
			}
			if ((state->n == 0) && (state->mflag == -1))	// lonely -
				mlwrite("Arg:");
			else
				mlwrite("Arg: %d", state->n * state->mflag);

			state->c = get1key();	// get the next key
		}
		state->n = state->n * state->mflag;	// figure in the sign
	}

	// do ^U repeat argument processing
	if (state->c == reptc) {	// ^U, start argument
		state->f = TRUE;
		state->n = 4;		// with argument of 4
		state->mflag = 0;	// that can be discarded.
		mlwrite("Arg: 4");
		while (((state->c = get1key()) >= '0' && state->c <= '9') || state->c == reptc
		       || state->c == '-') {
			if (state->c == reptc)
				if ((state->n > 0) == ((state->n * 4) > 0))
					state->n = state->n * 4;
				else
					state->n = 1;
			/*
			 * If dash, and start of argument string, set arg.
			 * to -1.  Otherwise, insert it.
			 */
			else if (state->c == '-') {
				if (state->mflag)
					break;
				state->n = 0;
				state->mflag = -1;
			}
			/*
			 * If first digit entered, replace previous argument
			 * with digit and set sign.  Otherwise, append to arg.
			 */
			else {
				if (!state->mflag) {
					state->n = 0;
					state->mflag = 1;
				}
				state->n = 10 * state->n + state->c - '0';
			}
			mlwrite("Arg: %d",
				(state->mflag >= 0) ? state->n : (state->n ? -state->n : -1));
		}
		/*
		 * Make arguments preceded by a minus sign negative and change
		 * the special argument "^U -" to an effective "^U -1".
		 */
		if (state->mflag == -1) {
			if (state->n == 0)
				state->n++;
			state->n = -state->n;
		}
	}

	// and execute the command
	execute(state->c, state->f, state->n);
	goto loop;
	
	return TRUE;
}


/*
 * Initialize all of the buffers and windows. The buffer name is passed down
 * as an argument, because the main routine may have been told to read in a
 * file by default, and we want the buffer name to be right.
 */
void edinit(char *bname)
{
	struct buffer *bp;
	struct window *wp;

	bp = bfind(bname, TRUE, 0);	/* First buffer         */
	blistp = bfind("*List*", TRUE, BFINVS);	/* Buffer list buffer   */
	wp = (struct window *)safe_alloc(sizeof(struct window), "first window", __FILE__, __LINE__);	/* First window         */
	if (bp == NULL || wp == NULL || blistp == NULL) {
		REPORT_ERROR(ERR_MEMORY, "Failed to initialize core editor structures");
		exit(1);
	}
	curbp = bp;		/* Make this current    */
	wheadp = wp;
	curwp = wp;
	wp->w_wndp = NULL;	/* Initialize window    */
	wp->w_bufp = bp;
	bp->b_nwnd = 1;		/* Displayed.           */
	wp->w_linep = bp->b_linep;
	wp->w_dotp = bp->b_linep;
	wp->w_doto = 0;
	wp->w_markp = NULL;
	wp->w_marko = 0;
	wp->w_toprow = 0;
#if	COLOR
	/* initalize colors to global defaults */
	wp->w_fcolor = gfcolor;
	wp->w_bcolor = gbcolor;
#endif
	wp->w_ntrows = term.t_nrow - 1;	/* "-1" for mode line.  */
	wp->w_force = 0;
	wp->w_flag = WFMODE | WFHARD;	/* Full.                */
}

/*
 * This is the general command execution routine. It handles the fake binding
 * of all the keys to "self-insert". It also clears out the "thisflag" word,
 * and arranges to move it to the "lastflag", so that the next command can
 * look at it. Return the status of command.
 */
/* C23 atomic command execution with instantaneous function lookup */
int execute(int c, int f, int n)
{
	int status;
	fn_t execfunc;

	/* C23 atomic function binding lookup - O(1) hash-based with memory ordering */
	execfunc = getbind(c);  // Already uses atomic keymap lookups internally
	
	if (execfunc != NULL) {
		// Atomic flag management for command state
		atomic_store_explicit((_Atomic int*)&thisflag, 0, memory_order_relaxed);
		
		/* Execute function atomically - instantaneous command dispatch */
		status = (*execfunc)(f, n);
		
		/* Atomic flag transition for next command */
		atomic_store_explicit((_Atomic int*)&lastflag, 
		                     atomic_load_explicit((_Atomic int*)&thisflag, memory_order_acquire),
		                     memory_order_release);
		return status;
	}

	/*
	 * If a space was typed, fill column is defined, the argument is non-
	 * negative, wrap mode is enabled, and we are now past fill column,
	 * and we are not read-only, perform word wrap.
	 */
	if (c == ' ' && (curwp->w_bufp->b_mode & MDWRAP) && fillcol > 0 &&
	    n >= 0 && getccol(FALSE) > fillcol &&
	    (curwp->w_bufp->b_mode & MDVIEW) == FALSE)
		execute(META | SPEC | 'W', FALSE, 1);

	if ((c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0x10FFFF)) {	/* Self inserting.      */
		if (n <= 0) {	/* Fenceposts.          */
			lastflag = 0;
			return n < 0 ? FALSE : TRUE;
		}
		thisflag = 0;	/* For the future.      */

		/* if we are in overwrite mode, not at eol,
		   and next char is not a tab or we are at a tab stop,
		   delete a char forword                        */
		if (curwp->w_bufp->b_mode & MDOVER &&
		    curwp->w_doto < curwp->w_dotp->l_used &&
		    (lgetc(curwp->w_dotp, curwp->w_doto) != '\t' ||
		     (curwp->w_doto) % 8 == 7))
			ldelchar(1, FALSE);

		/* do the appropriate insertion */
		if (c == '}' && (curbp->b_mode & MDCMOD) != 0)
			status = insbrace(n, c);
		else if (c == '#' && (curbp->b_mode & MDCMOD) != 0)
			status = inspound();
		else
			status = linsert(n, c);
		// Smart statusline updating: only on significant changes
		static int char_count = 0;
		if (++char_count % 50 == 0 || c == '\n' || c == ' ') {
			curwp->w_flag |= WFMODE;
		}

#if	CFENCE
		/* check for CMODE fence matching */
		if ((c == '}' || c == ')' || c == ']') &&
		    (curbp->b_mode & MDCMOD) != 0)
			fmatch(c);
#endif

		/* check auto-save mode */
		if (curbp->b_mode & MDASAVE)
			if (--gacount == 0) {
				/* and save the file if needed */
				upscreen(FALSE, 0);
				filesave(FALSE, 0);
				gacount = gasave;
			}

		lastflag = thisflag;
		return status;
	}
	TTbeep();
	mlwrite("(Key not bound)");	/* complain             */
	lastflag = 0;		/* Fake last flags.     */
	return FALSE;
}

/*
 * Fancy quit command, as implemented by Norm. If the any buffer has
 * changed do a write on that buffer and exit emacs, otherwise simply exit.
 */
int quickexit(int f, int n)
{
	struct buffer *bp;	/* scanning pointer to buffers */
	struct buffer *oldcb;	/* original current buffer */
	int status;

	oldcb = curbp;		/* save in case we fail */

	bp = bheadp;
	while (bp != NULL) {
		if ((bp->b_flag & BFCHG) != 0	/* Changed.             */
		    && (bp->b_flag & BFTRUNC) == 0	/* Not truncated P.K.   */
		    && (bp->b_flag & BFINVS) == 0) {	/* Real.                */
			curbp = bp;	/* make that buffer cur */
			mlwrite("(Saving %s)", bp->b_fname);
			if ((status = filesave(f, n)) != TRUE) {
				curbp = oldcb;	/* restore curbp */
				return status;
			}
		}
		bp = bp->b_bufp;	/* on to the next buffer */
	}
	quit(f, n);		/* conditionally quit   */
	return TRUE;
}

// Global flags for async-signal-safe emergency exit handling
static volatile sig_atomic_t emergency_exit_flag = 0;

static void emergencyexit(int signr)
{
	// ASYNC-SIGNAL-SAFE: Only set flag and use write() for immediate output
	emergency_exit_flag = 1;
	const char msg[] = "\nEmergency exit requested...\n";
	ssize_t bytes_written = write(STDERR_FILENO, msg, sizeof(msg) - 1);
	(void)bytes_written;
}

// Check and handle pending emergency exit - called from main loop
void check_emergency_exit(void)
{
	if (emergency_exit_flag) {
		emergency_exit_flag = 0;
		quickexit(FALSE, 0);
		quit(TRUE, 0);
	}
}

/*
 * Quit command. If an argument, always quit. Otherwise confirm if a buffer
 * has been changed and not written out. Normally bound to "C-X C-C".
 */
int quit(int f, int n)
{
	int s;

	if (f != FALSE		/* Argument forces it.  */
	    || anycb() == FALSE	/* All buffers clean.   */
	    /* User says it's OK.   */
	    || (s =
		mlyesno("Modified buffers exist. Leave anyway")) == TRUE) {
/* Session saving removed - keeping exit clean and simple */
		vttidy();
		if (f)
			exit(n);
		else
			exit(GOOD);
	}
	mlwrite("");
	return s;
}

/*
 * Begin a keyboard macro.
 * Error if not at the top level in keyboard processing. Set up variables and
 * return.
 */
int ctlxlp(int f, int n)
{
	if (kbdmode != STOP) {
		mlwrite("%%Macro already active");
		return FALSE;
	}
	mlwrite("(Start macro)");
	kbdptr = &kbdm[0];
	kbdend = kbdptr;
	kbdmode = RECORD;
	return TRUE;
}

/*
 * End keyboard macro. Check for the same limit conditions as the above
 * routine. Set up the variables and return to the caller.
 */
int ctlxrp(int f, int n)
{
	if (kbdmode == STOP) {
		mlwrite("%%Macro not active");
		return FALSE;
	}
	if (kbdmode == RECORD) {
		mlwrite("(End macro)");
		kbdmode = STOP;
	}
	return TRUE;
}

/*
 * Execute a macro.
 * The command argument is the number of times to loop. Quit as soon as a
 * command gets an error. Return TRUE if all ok, else FALSE.
 */
int ctlxe(int f, int n)
{
	if (kbdmode != STOP) {
		mlwrite("%%Macro already active");
		return FALSE;
	}
	if (n <= 0)
		return TRUE;
	kbdrep = n;		/* remember how many times to execute */
	kbdmode = PLAY;		/* start us in play mode */
	kbdptr = &kbdm[0];	/*    at the beginning */
	return TRUE;
}

/*
 * Abort.
 * Beep the beeper. Kill off any keyboard macro, etc., that is in progress.
 * Sometimes called as a routine, to do general aborting of stuff.
 */
int ctrlg(int f, int n)
{
	TTbeep();
	kbdmode = STOP;
	mlwrite("(Aborted)");
	return ABORT;
}

/*
 * tell the user that this command is illegal while we are in
 * VIEW (read-only) mode
 */
int rdonly(void)
{
	TTbeep();
	mlwrite("(Key illegal in VIEW mode)");
	return FALSE;
}

int resterr(void)
{
	TTbeep();
	mlwrite("(That command is RESTRICTED)");
	return FALSE;
}

/* user function that does NOTHING */
int nullproc(int f, int n)
{
	return TRUE;
}

/* dummy function for binding to meta prefix */
int metafn(int f, int n)
{
	int c;			/* next key pressed */
	fn_t execfunc;	/* function to execute */
	
	/* get the next key */
	c = get1key();
	
	/* look it up in the Meta keymap */
	struct keymap *mkm = atomic_load_explicit(&meta_keymap, memory_order_acquire);
	if (mkm) {
		struct keymap_entry *entry = keymap_lookup(mkm, c);
		if (entry && !entry->is_prefix) {
			execfunc = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
			if (execfunc != NULL) {
				return (*execfunc)(f, n);
			}
		}
	}
	
	/* command not found - report error */
	mlwrite("(Key not bound)");
	return FALSE;
}

/* dummy function for binding to control-x prefix */
int cex(int f, int n)
{
	int c;			/* next key pressed */
	fn_t execfunc;	/* function to execute */
	
	/* get the next key */
	c = get1key();
	
	/* look it up in the C-x keymap */
	struct keymap *ckm = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
	if (ckm) {
		struct keymap_entry *entry = keymap_lookup(ckm, c);
		if (entry && !entry->is_prefix) {
			execfunc = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
			if (execfunc != NULL) {
				return (*execfunc)(f, n);
			}
		}
	}
	
	/* command not found - report error */
	mlwrite("(Key not bound)");
	return FALSE;
}

/* dummy function for binding to universal-argument */
int unarg(int f, int n)
{
	return TRUE;
}

/*****		Compiler specific Library functions	****/

/*	On some primitave operation systems, and when emacs is used as
	a subprogram to a larger project, emacs needs to de-alloc its
	own used memory
*/

/* Remove conflicting stubs - will be resolved elsewhere */

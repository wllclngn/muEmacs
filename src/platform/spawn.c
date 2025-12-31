/*	spawn.c
 *
 *	Various operating system access commands.
 *
 *	Modified by Petri Kutvonen
 *	C23 modernization: Uses posix_spawn() instead of fork()/exec()
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <spawn.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "string_utils.h"

extern char **environ;

#ifdef SIGWINCH
extern int chg_width, chg_height;
extern void sizesignal(int);
#endif

/*
 * run_shell - Execute a shell command using posix_spawn()
 *
 * This is a modern replacement for fork()/exec() patterns.
 * If shell is NULL, uses $SHELL or falls back to /bin/sh.
 * If cmd is NULL, spawns an interactive shell.
 */
static int run_shell(const char *shell, const char *cmd)
{
    pid_t pid;
    int status;
    const char *sh = shell;

    /* Determine shell to use */
    if (!sh || *sh == '\0') {
        sh = getenv("SHELL");
        if (!sh || *sh == '\0') {
            sh = "/bin/sh";
        }
    }

    char *argv[4];
    if (cmd) {
        /* Run command: sh -c "cmd" */
        argv[0] = (char *)sh;
        argv[1] = "-c";
        argv[2] = (char *)cmd;
        argv[3] = NULL;
    } else {
        /* Interactive shell */
        argv[0] = (char *)sh;
        argv[1] = NULL;
    }

    if (posix_spawn(&pid, sh, NULL, NULL, argv, environ) != 0) {
        return -1;
    }

    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/*
 * Create a subjob with a copy of the command interpreter in it. When the
 * command interpreter exits, mark the screen as garbage so that you do a full
 * repaint. Bound to "^X C".
 */
int spawncli([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	movecursor(term.t_nrow, 0);	/* Seek to last line.   */
	TTflush();
	TTclose();		/* stty to old settings */
	TTkclose();		/* Close "keyboard" */

	/* Spawn interactive shell using posix_spawn */
	run_shell(NULL, NULL);

	sgarbf = true;
	sleep(2);
	TTopen();
	TTkopen();
#ifdef SIGWINCH
	/*
	 * This fools the update routines to force a full
	 * redraw with complete window size checking.
	 *		-lbt
	 */
	chg_width = term.t_ncol;
	chg_height = term.t_nrow + 1;
	term.t_nrow = term.t_ncol = 0;
#endif
	return true;
}

int bktoshell([[maybe_unused]] int f, [[maybe_unused]] int n)
{				/* suspend MicroEMACS and wait to wake up */
	vttidy();
	kill(0, SIGTSTP);
	return true;
}

void rtfrmshell(void)
{
	TTopen();
	/* Preserve WFTERM if set (terminal window marker) */
	curwp->w_flag = (curwp->w_flag & WFTERM) | WFHARD;
	sgarbf = true;
}

/*
 * Run a one-liner in a subjob. When the command returns, wait for a single
 * character to be typed, then mark the screen as garbage so a full repaint is
 * done. Bound to "C-X !".
 */
int spawn([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	int s;
	char line[NLINE];

	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	if ((s = minibuf_read("!", line, NLINE)) != true)
		return s;
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();

	/* Execute command using posix_spawn */
	run_shell(NULL, line);

	fflush(stdout);		/* to be sure P.K.      */
	TTopen();

	if (clexec == false) {
		mlputs("(End)");	/* Pause.               */
		TTflush();
		while ((s = input_read_byte()) != '\r' && s != ' ');
		mlputs("\r\n");
	}
	TTkopen();
	sgarbf = true;
	return true;
}

/*
 * Run an external program with arguments. When it returns, wait for a single
 * character to be typed, then mark the screen as garbage so a full repaint is
 * done. Bound to "C-X $".
 */
int execprg([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	int s;
	char line[NLINE];

	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	if ((s = minibuf_read("!", line, NLINE)) != true)
		return s;
	TTputc('\n');		/* Already have '\r'    */
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();

	/* Execute command using posix_spawn */
	run_shell(NULL, line);

	fflush(stdout);		/* to be sure P.K.      */
	TTopen();
	mlputs("(End)");	/* Pause.               */
	TTflush();
	while ((s = input_read_byte()) != '\r' && s != ' ');
	sgarbf = true;
	return true;
}

/*
 * Pipe a one line command into a window
 * Bound to ^X @
 */
int pipecmd([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	int s;		/* return status from CLI */
	struct window *wp;	/* pointer to new window */
	struct buffer *bp;	/* pointer to buffer to zot */
	char line[NLINE];	/* command line send to shell */
	static char bname[] = "command";

	static char filnam[NSTRING] = "command";

	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	/* get the command to pipe in */
	if ((s = minibuf_read("@", line, NLINE)) != true)
		return s;

	/* get rid of the command output buffer if it exists */
	if ((bp = bfind(bname, false, 0)) != false) {
		/* try to make sure we are off screen */
		wp = wheadp;
		while (wp != nullptr) {
			if (wp->w_bufp == bp) {
				if (wp == curwp)
					window_delete(false, 1);
				else
					window_only(false, 1);
				break;
			}
			wp = wp->w_wndp;
		}
		if (zotbuf(bp) != true)

			return false;
	}

	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();
	safe_strcat(line, " >", NLINE);
	safe_strcat(line, filnam, NLINE);

	/* Execute command with output redirection using posix_spawn */
	run_shell(NULL, line);

	TTopen();
	TTkopen();
	TTflush();
	sgarbf = true;
	s = true;

	if (s != true)
		return s;

	/* split the current window to make room for the command output */
	if (window_split(false, 1) == false)
		return false;

	/* and read the stuff in */
	if (getfile(filnam, false) == false)
		return false;

	/* make this window in VIEW mode, update all mode lines */
	curwp->w_bufp->b_mode |= MDVIEW;
	wp = wheadp;
	while (wp != nullptr) {
		wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}

	/* and get rid of the temporary file */
	unlink(filnam);
	return true;
}

/*
 * filter a buffer through an external DOS program
 * Bound to ^X #
 */
int filter_buffer([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	int s;		/* return status from CLI */
	struct buffer *bp;	/* pointer to buffer to zot */
	char line[NLINE];	/* command line send to shell */
	char tmpnam[NFILEN];	/* place to store real file name */
	static char bname1[] = "fltinp";

	static char filnam1[] = "fltinp";
	static char filnam2[] = "fltout";

	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
		return rdonly();	/* we are in read only mode     */

	/* get the filter name and its args */
	if ((s = minibuf_read("#", line, NLINE)) != true)
		return s;

	/* setup the proper file names */
	bp = curbp;
	safe_strcpy(tmpnam, bp->b_fname, sizeof(tmpnam));	/* save the original name */
	safe_strcpy(bp->b_fname, bname1, NFILEN);	/* set it to our new one */

	/* write it out, checking for errors */
	if (writeout(filnam1) != true) {
		mlwrite("[CANNOT WRITE FILTER FILE]");
		safe_strcpy(bp->b_fname, tmpnam, NFILEN);
		return false;
	}

	TTputc('\n');		/* Already have '\r'    */
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();
	safe_strcat(line, " <fltinp >fltout", NLINE);

	/* Execute filter command with I/O redirection using posix_spawn */
	run_shell(NULL, line);

	TTopen();
	TTkopen();
	TTflush();
	sgarbf = true;
	s = true;

	/* on failure, escape gracefully */
	if (s != true || (readin(filnam2, false) == false)) {
		mlwrite("[EXECUTION FAILED]");
		safe_strcpy(bp->b_fname, tmpnam, NFILEN);
		unlink(filnam1);
		unlink(filnam2);
		return s;
	}

	/* reset file name */
    safe_strcpy(bp->b_fname, tmpnam, NFILEN);	/* restore name */
	bp->b_flag |= BFCHG;	/* flag it as changed */

	/* and get rid of the temporary file */
	unlink(filnam1);
	unlink(filnam2);
	return true;
}

/*	spaw.c
 *
 *	Various operating system access commands.
 *
 *	<odified by Petri Kutvonen
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "string_safe.h"

#ifdef SIGWINCH
extern int chg_width, chg_height;
extern void sizesignal(int);
#endif

/*
 * Create a subjob with a copy of the command intrepreter in it. When the
 * command interpreter exits, mark the screen as garbage so that you do a full
 * repaint. Bound to "^X C".
 */
int spawncli([[maybe_unused]] int f, [[maybe_unused]] int n)
{
	const char *restrict cp;

	/* don't allow this command if restricted */
	if (restflag)
		return resterr();

	movecursor(term.t_nrow, 0);	/* Seek to last line.   */
	TTflush();
	TTclose();		/* stty to old settings */
	TTkclose();		/* Close "keyboard" */
	if ((cp = getenv("SHELL")) != nullptr && *cp != '\0') {
		pid_t pid = fork();
		if (pid == 0) {
			// Child process
			execl(cp, cp, (char *)nullptr);
			// If execl fails, try /bin/sh
			execl("/bin/sh", "sh", (char *)nullptr);
			_exit(127); // exec failed
		} else if (pid > 0) {
			// Parent process
			waitpid(pid, nullptr, 0);
		}
	} else {
		pid_t pid = fork();
		if (pid == 0) {
			execl("/bin/sh", "sh", (char *)nullptr);
			_exit(127);
		} else if (pid > 0) {
			waitpid(pid, nullptr, 0);
		}
	}
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
	curwp->w_flag = WFHARD;
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

	if ((s = mlreply("!", line, NLINE)) != true)
		return s;
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();
	
	// Safe command execution using sh -c
	pid_t pid = fork();
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", line, (char *)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
	
	fflush(stdout);		/* to be sure P.K.      */
	TTopen();

	if (clexec == false) {
		mlputs("(End)");	/* Pause.               */
		TTflush();
		while ((s = tgetc()) != '\r' && s != ' ');
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

	if ((s = mlreply("!", line, NLINE)) != true)
		return s;
	TTputc('\n');		/* Already have '\r'    */
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();
	
	// Safe command execution using sh -c
	pid_t pid = fork();
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", line, (char *)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
	
	fflush(stdout);		/* to be sure P.K.      */
	TTopen();
	mlputs("(End)");	/* Pause.               */
	TTflush();
	while ((s = tgetc()) != '\r' && s != ' ');
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
	if ((s = mlreply("@", line, NLINE)) != true)
		return s;

	/* get rid of the command output buffer if it exists */
	if ((bp = bfind(bname, false, 0)) != false) {
		/* try to make sure we are off screen */
		wp = wheadp;
		while (wp != nullptr) {
			if (wp->w_bufp == bp) {
				if (wp == curwp)
					delwind(false, 1);
				else
					onlywind(false, 1);
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
	
	// Safe command execution with output redirection
	pid_t pid = fork();
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", line, (char *)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
	
	TTopen();
	TTkopen();
	TTflush();
	sgarbf = true;
	s = true;

	if (s != true)
		return s;

	/* split the current window to make room for the command output */
	if (splitwind(false, 1) == false)
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
	if ((s = mlreply("#", line, NLINE)) != true)
		return s;

	/* setup the proper file names */
	bp = curbp;
	safe_strcpy(tmpnam, bp->b_fname, sizeof(tmpnam));	/* save the original name */
	safe_strcpy(bp->b_fname, bname1, NFILEN);	/* set it to our new one */

	/* write it out, checking for errors */
	if (writeout(filnam1) != true) {
		mlwrite("(Cannot write filter file)");
		safe_strcpy(bp->b_fname, tmpnam, NFILEN);
		return false;
	}

	TTputc('\n');		/* Already have '\r'    */
	TTflush();
	TTclose();		/* stty to old modes    */
	TTkclose();
    safe_strcat(line, " <fltinp >fltout", NLINE);
	
	// Safe command execution with I/O redirection
	pid_t pid = fork();
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", line, (char *)nullptr);
		_exit(127);
	} else if (pid > 0) {
		waitpid(pid, nullptr, 0);
	}
	
	TTopen();
	TTkopen();
	TTflush();
	sgarbf = true;
	s = true;

	/* on failure, escape gracefully */
	if (s != true || (readin(filnam2, false) == false)) {
		mlwrite("(Execution failed)");
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

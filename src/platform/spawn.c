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
#include <fcntl.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "string_utils.h"
#include "line.h"
#include "memory.h"
#include "gapbuffer.h"

/*
 * set_editor_env - Set $UE_* environment variables for spawned commands
 *
 * Enables shell scripts and external tools to access editor state:
 *   $UE_FILENAME  - Current file path (empty for scratch buffers)
 *   $UE_FILETYPE  - Detected filetype (e.g., "c", "python")
 *   $UE_LINE      - Cursor line (1-indexed)
 *   $UE_COL       - Cursor column (0-indexed)
 *   $UE_BUFNAME   - Buffer name
 *   $UE_MODIFIED  - "1" if buffer modified, "0" otherwise
 */
static void set_editor_env(void) {
    char buf[32];

    /* Current filename (may be empty for scratch buffers) */
    if (curbp && curbp->b_fname[0]) {
        setenv("UE_FILENAME", curbp->b_fname, 1);
    } else {
        setenv("UE_FILENAME", "", 1);
    }

    /* Buffer name */
    if (curbp && curbp->b_bname[0]) {
        setenv("UE_BUFNAME", curbp->b_bname, 1);
    } else {
        setenv("UE_BUFNAME", "", 1);
    }

    /* Cursor line (1-indexed) */
    snprintf(buf, sizeof(buf), "%d", getcline());
    setenv("UE_LINE", buf, 1);

    /* Cursor column (0-indexed) */
    snprintf(buf, sizeof(buf), "%d", getccol(false));
    setenv("UE_COL", buf, 1);

    /* Buffer modified state */
    setenv("UE_MODIFIED", (curbp && (curbp->b_flag & BFCHG)) ? "1" : "0", 1);

    /* Filetype detection - simple extension-based */
    if (curbp && curbp->b_fname[0]) {
        const char *ext = strrchr(curbp->b_fname, '.');
        if (ext && ext[1]) {
            ext++;  /* Skip the dot */
            /* Map common extensions to filetypes */
            if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) {
                setenv("UE_FILETYPE", "c", 1);
            } else if (strcmp(ext, "cpp") == 0 || strcmp(ext, "hpp") == 0 ||
                       strcmp(ext, "cc") == 0 || strcmp(ext, "cxx") == 0) {
                setenv("UE_FILETYPE", "cpp", 1);
            } else if (strcmp(ext, "py") == 0) {
                setenv("UE_FILETYPE", "python", 1);
            } else if (strcmp(ext, "rs") == 0) {
                setenv("UE_FILETYPE", "rust", 1);
            } else if (strcmp(ext, "go") == 0) {
                setenv("UE_FILETYPE", "go", 1);
            } else if (strcmp(ext, "js") == 0) {
                setenv("UE_FILETYPE", "javascript", 1);
            } else if (strcmp(ext, "ts") == 0) {
                setenv("UE_FILETYPE", "typescript", 1);
            } else if (strcmp(ext, "sh") == 0 || strcmp(ext, "bash") == 0) {
                setenv("UE_FILETYPE", "sh", 1);
            } else if (strcmp(ext, "md") == 0) {
                setenv("UE_FILETYPE", "markdown", 1);
            } else if (strcmp(ext, "toml") == 0) {
                setenv("UE_FILETYPE", "toml", 1);
            } else if (strcmp(ext, "json") == 0) {
                setenv("UE_FILETYPE", "json", 1);
            } else if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) {
                setenv("UE_FILETYPE", "yaml", 1);
            } else {
                setenv("UE_FILETYPE", ext, 1);  /* Use extension as-is */
            }
        } else {
            setenv("UE_FILETYPE", "", 1);
        }
    } else {
        setenv("UE_FILETYPE", "", 1);
    }
}

#ifdef SIGWINCH
extern int chg_width, chg_height;
extern void sizesignal(int);
#endif

/*
 * run_shell - Execute a shell command using posix_spawn()
 *
 * This is a modern replacement for fork()/exec() patterns.
 * If shell is nullptr, uses $SHELL or falls back to /bin/sh.
 * If cmd is nullptr, spawns an interactive shell.
 *
 * Sets $UE_* environment variables so shell scripts can access editor state.
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
        argv[3] = nullptr;
    } else {
        /* Interactive shell */
        argv[0] = (char *)sh;
        argv[1] = nullptr;
    }

    /* Set editor environment variables for child process */
    set_editor_env();

    if (posix_spawn(&pid, sh, nullptr, nullptr, argv, environ) != 0) {
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
	run_shell(nullptr, nullptr);

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
	run_shell(nullptr, line);

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
	run_shell(nullptr, line);

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
	run_shell(nullptr, line);

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
 * filter_direct - Pipe input through a command and capture output
 *
 * Uses direct pipe I/O instead of temp files for efficiency.
 * Returns dynamically allocated output buffer (caller must free).
 *
 * cmd: Shell command to execute
 * input: Input data to send to command's stdin
 * input_len: Length of input data
 * output: Output pointer set to result (caller frees)
 * output_len: Output length
 *
 * Returns: 0 on success, -1 on error
 */
static int filter_direct(const char *cmd, const char *input, size_t input_len,
                         char **output, size_t *output_len) {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    pid_t pid;
    int status = -1;

    *output = nullptr;
    *output_len = 0;

    /* Create pipes */
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        goto cleanup;
    }

    /* Set close-on-exec for pipe ends */
    fcntl(stdin_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdin_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[1], F_SETFD, FD_CLOEXEC);

    pid = fork();
    if (pid < 0) {
        goto cleanup;
    }

    if (pid == 0) {
        /* Child process */

        /* Redirect stdin from pipe */
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        /* Redirect stdout to pipe */
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        /* Get shell */
        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";

        /* Execute command */
        execlp(shell, shell, "-c", cmd, (char *)nullptr);
        _exit(127);
    }

    /* Parent process */

    /* Close unused pipe ends */
    close(stdin_pipe[0]);
    stdin_pipe[0] = -1;
    close(stdout_pipe[1]);
    stdout_pipe[1] = -1;

    /* Write input to child's stdin */
    size_t written = 0;
    while (written < input_len) {
        ssize_t n = write(stdin_pipe[1], input + written, input_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;  /* Write error - child may have closed stdin */
        }
        written += (size_t)n;
    }
    close(stdin_pipe[1]);
    stdin_pipe[1] = -1;

    /* Read output from child's stdout */
    size_t out_capacity = 4096;
    size_t out_len = 0;
    char *out_buf = SAFE_ALLOC_SIZED(char, out_capacity, "spawn output buffer");
    if (!out_buf) {
        waitpid(pid, &status, 0);
        goto cleanup;
    }

    for (;;) {
        if (out_len + 4096 > out_capacity) {
            out_capacity *= 2;
            char *new_buf = SAFE_REALLOC(out_buf, out_capacity, "spawn output grow");
            if (!new_buf) {
                SAFE_FREE(out_buf);
                waitpid(pid, &status, 0);
                goto cleanup;
            }
            out_buf = new_buf;
        }

        ssize_t n = read(stdout_pipe[0], out_buf + out_len, 4096);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;  /* EOF */
        out_len += (size_t)n;
    }

    close(stdout_pipe[0]);
    stdout_pipe[0] = -1;

    /* Wait for child */
    waitpid(pid, &status, 0);

    /* Null-terminate for convenience */
    if (out_len + 1 > out_capacity) {
        char *new_buf = SAFE_REALLOC(out_buf, out_len + 1, "spawn output terminate");
        if (new_buf) out_buf = new_buf;
    }
    out_buf[out_len] = '\0';

    *output = out_buf;
    *output_len = out_len;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;

cleanup:
    if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
    if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    return -1;
}

/*
 * filter a buffer through an external program
 * Bound to ^X #
 *
 * Uses direct pipe I/O for efficiency - no temp files.
 */
int filter_buffer([[maybe_unused]] int f, [[maybe_unused]] int n)
{
    int s;
    char line[NLINE];
    char tmpnam[NFILEN];

    /* don't allow this command if restricted */
    if (restflag)
        return resterr();

    if (curbp->b_mode & MDVIEW)
        return rdonly();

    /* get the filter name and its args */
    if ((s = minibuf_read("#", line, NLINE)) != true)
        return s;

    struct buffer *bp = curbp;
    safe_strcpy(tmpnam, bp->b_fname, sizeof(tmpnam));

    /* Collect buffer contents into a string */
    size_t total_len = 0;
    struct line *lp;

    /* First pass: calculate total size */
    for (lp = lforw(bp->b_linep); lp != bp->b_linep; lp = lforw(lp)) {
        total_len += (size_t)llength(lp) + 1;  /* +1 for newline */
    }

    /* Allocate buffer for content */
    char *content = SAFE_ALLOC_SIZED(char, total_len + 1, "filter buffer content");
    if (!content) {
        mlwrite("[OUT OF MEMORY]");
        return false;
    }

    /* Second pass: copy content using gap buffer API */
    char *p = content;
    for (lp = lforw(bp->b_linep); lp != bp->b_linep; lp = lforw(lp)) {
        int len = llength(lp);
        if (len > 0) {
            TS_GET_TEXT(lp->storage, 0, (size_t)len, p, (size_t)len);
            p += len;
        }
        *p++ = '\n';
    }
    *p = '\0';

    /* Set editor environment variables */
    set_editor_env();

    /* Run filter with direct pipe I/O */
    char *output = nullptr;
    size_t output_len = 0;
    int result = filter_direct(line, content, total_len, &output, &output_len);
    free(content);

    if (result < 0 || !output) {
        mlwrite("[FILTER FAILED]");
        if (output) free(output);
        return false;
    }

    /* Clear buffer and insert filtered output */
    if (bclear(bp) != true) {
        free(output);
        mlwrite("[CANNOT CLEAR BUFFER]");
        return false;
    }

    /* Parse output line by line and insert into buffer */
    char *line_start = output;
    char *end = output + output_len;

    while (line_start < end) {
        /* Find end of line */
        char *line_end = line_start;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }

        /* Create line and insert */
        int line_len = (int)(line_end - line_start);
        struct line *newlp = lalloc(0);  /* Create empty line */
        if (newlp) {
            /* Insert text into the line's gap buffer */
            if (line_len > 0) {
                TS_INSERT(newlp->storage, 0, line_start, (size_t)line_len);
            }
            /* Insert before header line */
            struct line *last = lback(bp->b_linep);
            newlp->l_fp = bp->b_linep;
            newlp->l_bp = last;
            last->l_fp = newlp;
            bp->b_linep->l_bp = newlp;
        }

        /* Move to next line */
        line_start = line_end + 1;
    }

    free(output);

    /* Reset buffer state */
    safe_strcpy(bp->b_fname, tmpnam, NFILEN);
    bp->b_dotp = lforw(bp->b_linep);
    bp->b_doto = 0;
    bp->b_flag |= BFCHG;

    /* Update window */
    curwp->w_linep = lforw(bp->b_linep);
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    curwp->w_flag |= WFHARD;

    sgarbf = true;
    mlwrite("[Filtered through: %s]", line);
    return true;
}

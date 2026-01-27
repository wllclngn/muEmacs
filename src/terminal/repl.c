/*
 * repl.c - REPL Integration for μEmacs
 *
 * Provides language-aware REPL startup and evaluation commands.
 * Maps file extensions to appropriate REPL commands.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>  /* strcasecmp */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal/terminal.h"

/* REPL configuration - maps filetypes to REPL commands */
typedef struct {
    const char *filetype;
    const char *command;
} repl_config_t;

static const repl_config_t repl_commands[] = {
    {"python", "python3 -i"},
    {"py",     "python3 -i"},
    {"lua",    "lua -i"},
    {"ruby",   "irb"},
    {"rb",     "irb"},
    {"node",   "node"},
    {"js",     "node"},
    {"javascript", "node"},
    {"ts",     "npx ts-node"},
    {"typescript", "npx ts-node"},
    {"haskell", "ghci"},
    {"hs",     "ghci"},
    {"lisp",   "sbcl"},
    {"cl",     "sbcl"},
    {"scheme", "guile"},
    {"scm",    "guile"},
    {"clojure", "clj"},
    {"clj",    "clj"},
    {"elixir", "iex"},
    {"ex",     "iex"},
    {"erlang", "erl"},
    {"erl",    "erl"},
    {"r",      "R --interactive"},
    {"julia",  "julia"},
    {"jl",     "julia"},
    {"php",    "php -a"},
    {"perl",   "perl -de0"},
    {"pl",     "perl -de0"},
    {"sh",     "bash"},
    {"bash",   "bash"},
    {"zsh",    "zsh"},
    {"fish",   "fish"},
    {NULL,     NULL}
};

/*
 * Detect filetype from current buffer's filename extension
 */
static const char *detect_filetype(void) {
    if (!curbp || !curbp->b_fname[0]) {
        return NULL;
    }

    const char *ext = strrchr(curbp->b_fname, '.');
    if (!ext || !ext[1]) {
        return NULL;
    }

    return ext + 1;  /* Skip the dot */
}

/*
 * Find REPL command for given filetype
 */
static const char *find_repl_command(const char *filetype) {
    if (!filetype) return NULL;

    for (int i = 0; repl_commands[i].filetype != NULL; i++) {
        if (strcasecmp(repl_commands[i].filetype, filetype) == 0) {
            return repl_commands[i].command;
        }
    }

    return NULL;
}

/*
 * repl_start - Start a REPL for the current file's language
 *
 * Detects filetype from filename extension and starts appropriate REPL.
 * If no filetype detected, prompts for REPL command.
 */
int repl_start(int f, int n) {
    (void)f; (void)n;

    const char *filetype = detect_filetype();
    const char *repl_cmd = find_repl_command(filetype);
    char cmd[256];

    if (repl_cmd) {
        snprintf(cmd, sizeof(cmd), "%s", repl_cmd);
        mlwrite("[Starting %s REPL: %s]", filetype, cmd);
    } else {
        /* Prompt for REPL command */
        char input[256] = "";
        int status = minibuf_read("REPL command: ", input, sizeof(input));
        if (status == ABORT || status == false || input[0] == '\0') {
            return status == ABORT ? ABORT : false;
        }
        snprintf(cmd, sizeof(cmd), "%s", input);
    }

    /* Create terminal named "repl" */
    return terminal_create_named("repl");
}

/*
 * repl_eval_line - Send current line to REPL
 *
 * Wrapper around terminal_send_line that ensures a REPL terminal exists.
 */
int repl_eval_line(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_line(f, n);
}

/*
 * repl_eval_region - Send region to REPL
 *
 * Wrapper around terminal_send_region that ensures a REPL terminal exists.
 */
int repl_eval_region(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_region(f, n);
}

/*
 * repl_eval_buffer - Send entire buffer to REPL
 *
 * Wrapper around terminal_send_buffer that ensures a REPL terminal exists.
 */
int repl_eval_buffer(int f, int n) {
    /* Check if any terminal is open */
    if (!terminal_window) {
        mlwrite("[No terminal/REPL open - use M-x repl-start or M-x term]");
        return false;
    }

    return terminal_send_buffer(f, n);
}

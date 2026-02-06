/*
 * build.c - Build Integration for μEmacs
 *
 * Provides build system detection, command execution, and error navigation.
 * Parses GCC/Clang/Rust error output for quick jump-to-error.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "line.h"
#include "string_utils.h"
#include "gapbuffer.h"
#include "terminal/terminal.h"
#include "terminal/pty.h"

/* Build configuration - detects build system from project files */
typedef struct {
    const char *marker_file;
    const char *command;
} build_config_t;

static const build_config_t build_commands[] = {
    {"Cargo.toml",     "cargo build"},
    {"package.json",   "npm run build"},
    {"Makefile",       "make"},
    {"makefile",       "make"},
    {"GNUmakefile",    "make"},
    {"CMakeLists.txt", "cmake --build build"},
    {"meson.build",    "meson compile -C build"},
    {"build.gradle",   "./gradlew build"},
    {"pom.xml",        "mvn compile"},
    {"go.mod",         "go build"},
    {"build.zig",      "zig build"},
    {"dune-project",   "dune build"},
    {"stack.yaml",     "stack build"},
    {"*.cabal",        "cabal build"},
    {"setup.py",       "python setup.py build"},
    {"pyproject.toml", "python -m build"},
    {nullptr,             nullptr}
};

/* Error location storage */
typedef struct {
    char filename[NFILEN];
    int line;
    int col;
} error_location_t;

#define MAX_ERRORS 256
static error_location_t error_list[MAX_ERRORS];
static int error_count = 0;
static int current_error = -1;

/*
 * Detect build command based on project files in current directory
 */
static const char *detect_build_command(void) {
    /* Get directory from current file or use cwd */
    char dir[NFILEN] = ".";
    if (curbp && curbp->b_fname[0]) {
        safe_strcpy(dir, curbp->b_fname, sizeof(dir));
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        else safe_strcpy(dir, ".", sizeof(dir));
    }

    /* Check for each build marker file */
    for (int i = 0; build_commands[i].marker_file != nullptr; i++) {
        char path[NFILEN * 2];  /* Extra space for dir + marker */
        snprintf(path, sizeof(path), "%s/%s", dir, build_commands[i].marker_file);

        if (access(path, F_OK) == 0) {
            return build_commands[i].command;
        }
    }

    return "make";  /* Default fallback */
}

/*
 * Parse GCC/Clang style error: "file:line:col: error: message"
 */
static int parse_error_line(const char *line, error_location_t *err) {
    /* Pattern: filename:line:col: */
    const char *p = line;

    /* Skip whitespace */
    while (*p && (*p == ' ' || *p == '\t')) p++;

    /* Find first colon (after filename) */
    const char *colon1 = strchr(p, ':');
    if (!colon1 || colon1 == p) return 0;

    /* Copy filename */
    size_t fname_len = (size_t)(colon1 - p);
    if (fname_len >= NFILEN) fname_len = NFILEN - 1;
    memcpy(err->filename, p, fname_len);
    err->filename[fname_len] = '\0';

    /* Parse line number */
    const char *colon2 = strchr(colon1 + 1, ':');
    if (!colon2) return 0;

    err->line = safe_atoi(colon1 + 1, 0);
    if (err->line <= 0) return 0;

    /* Parse column (optional) */
    err->col = safe_atoi(colon2 + 1, 0);
    if (err->col <= 0) err->col = 0;

    return 1;
}

/*
 * Parse Rust-style error: " --> file:line:col"
 */
static int parse_rust_error(const char *line, error_location_t *err) {
    const char *arrow = strstr(line, " --> ");
    if (!arrow) return 0;

    return parse_error_line(arrow + 5, err);
}

/*
 * Scan terminal buffer for error locations
 */
static void scan_build_errors(void) {
    error_count = 0;
    current_error = -1;

    /* Find first terminal buffer */
    struct buffer *bp = terminal_get_first_buffer();
    if (!bp) return;

    /* Scan each line */
    struct line *lp;
    for (lp = lforw(bp->b_linep); lp != bp->b_linep && error_count < MAX_ERRORS; lp = lforw(lp)) {
        int len = llength(lp);
        if (len == 0) continue;

        char *text = SAFE_ALLOC_SIZED(char, (size_t)len + 1, "terminal error parse");
        if (!text) continue;

        TS_GET_TEXT(lp->storage, 0, (size_t)len, text, (size_t)len);
        text[len] = '\0';

        error_location_t err;

        /* Try different error formats */
        if (parse_error_line(text, &err) ||
            parse_rust_error(text, &err)) {
            /* Only add if file exists */
            if (access(err.filename, F_OK) == 0) {
                error_list[error_count++] = err;
            }
        }

        SAFE_FREE(text);
    }
}

/*
 * build_run - Run build command and capture output
 *
 * Detects build system and runs appropriate command in terminal.
 * Parses output for error locations.
 */
int build_run(int f, int n) {
    (void)f; (void)n;

    const char *cmd = detect_build_command();

    /* Prompt to confirm or modify command */
    char build_cmd[256];
    safe_strcpy(build_cmd, cmd, sizeof(build_cmd));

    int status = minibuf_read("Build: ", build_cmd, sizeof(build_cmd));
    if (status == ABORT) return ABORT;
    if (status == false || build_cmd[0] == '\0') {
        safe_strcpy(build_cmd, cmd, sizeof(build_cmd));
    }

    /* Create or switch to build terminal */
    struct buffer *bp = terminal_find("build");
    if (!bp) {
        terminal_create_named("build");
        bp = terminal_find("build");
    }

    if (!bp) {
        mlwrite("[Cannot create build terminal]");
        return false;
    }

    /* Send build command to terminal */
    int fd = terminal_get_pty_fd(bp);
    if (fd >= 0) {
        pty_write(fd, build_cmd, strlen(build_cmd));
        pty_write(fd, "\r", 1);
    }

    mlwrite("[Building: %s]", build_cmd);

    /* Note: Error parsing happens when user calls build-next-error */
    return true;
}

/*
 * build_next_error - Jump to next error location
 */
int build_next_error(int f, int n) {
    (void)f; (void)n;

    /* Rescan errors if first time or at end */
    if (current_error < 0 || current_error >= error_count - 1) {
        scan_build_errors();
    }

    if (error_count == 0) {
        mlwrite("[No errors found]");
        return false;
    }

    current_error++;
    if (current_error >= error_count) {
        current_error = 0;  /* Wrap around */
    }

    error_location_t *err = &error_list[current_error];

    /* Open file and go to line */
    if (getfile(err->filename, true) != true) {
        mlwrite("[Cannot open %s]", err->filename);
        return false;
    }

    /* Go to line */
    gotoline(true, err->line);

    /* Go to column if specified */
    if (err->col > 0) {
        goto_line_start(false, 1);
        move_char_forward(true, err->col - 1);
    }

    mlwrite("[Error %d/%d: %s:%d]", current_error + 1, error_count,
            err->filename, err->line);
    return true;
}

/*
 * build_prev_error - Jump to previous error location
 */
int build_prev_error(int f, int n) {
    (void)f; (void)n;

    if (error_count == 0) {
        scan_build_errors();
    }

    if (error_count == 0) {
        mlwrite("[No errors found]");
        return false;
    }

    current_error--;
    if (current_error < 0) {
        current_error = error_count - 1;  /* Wrap around */
    }

    error_location_t *err = &error_list[current_error];

    /* Open file and go to line */
    if (getfile(err->filename, true) != true) {
        mlwrite("[Cannot open %s]", err->filename);
        return false;
    }

    /* Go to line */
    gotoline(true, err->line);

    /* Go to column if specified */
    if (err->col > 0) {
        goto_line_start(false, 1);
        move_char_forward(true, err->col - 1);
    }

    mlwrite("[Error %d/%d: %s:%d]", current_error + 1, error_count,
            err->filename, err->line);
    return true;
}

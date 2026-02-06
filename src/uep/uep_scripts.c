/*
 * uep_scripts.c - Layer 2: Script Extensions
 *
 * Auto-discover and register scripts as M-x commands.
 * Any executable in the scripts directory becomes an M-x command.
 *
 * Protocol:
 *   - Script receives buffer content on stdin
 *   - Script outputs result on stdout
 *   - Exit 0 = success, non-zero = error
 *   - Single line output → message line
 *   - Multi-line output → *Script Output* buffer
 *
 * Environment variables passed to script:
 *   MUEMACS_BUFFER_NAME  - Current buffer name
 *   MUEMACS_BUFFER_FILE  - Current buffer filename (if any)
 *   MUEMACS_LINE         - Current line number
 *   MUEMACS_COLUMN       - Current column number
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/uep_scripts.h"
#include "internal/line.h"
#include "internal/memory.h"
#include "util/logger.h"

/* Maximum number of scripts we can load */
#define MAX_SCRIPTS 128

/* Script entry */
typedef struct {
    char *name;       /* Command name (filename minus extension) */
    char *path;       /* Full path to script */
    bool active;
} script_entry_t;

static script_entry_t scripts[MAX_SCRIPTS];
static int script_count = 0;
static char scripts_dir[512] = {0};
static bool initialized = false;

/* Forward declarations */
static int script_command_handler(int f, int n);
static char *get_buffer_contents(size_t *len);
static void show_output(const char *output, size_t len, const char *script_name);

/* Currently executing script (for the generic handler) */
static const char *current_script_path = nullptr;

/*
 * Expand ~ to home directory
 */
static void expand_path(const char *src, char *dst, size_t dst_size) {
    if (src[0] == '~' && (src[1] == '/' || src[1] == '\0')) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(dst, dst_size, "%s%s", home, src + 1);
            return;
        }
    }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/*
 * Extract command name from filename
 * "word-count.py" -> "word-count"
 * "my-tool" -> "my-tool"
 */
static char *extract_command_name(const char *filename) {
    char *name = SAFE_STRDUP(filename, "script command name");
    if (!name) return nullptr;

    /* Remove extension if present */
    char *dot = strrchr(name, '.');
    if (dot && dot != name) {
        *dot = '\0';
    }

    return name;
}

/*
 * Check if file is executable
 */
static bool is_executable(const char *path) {
    return access(path, X_OK) == 0;
}

/*
 * Register a single script as a command
 */
static int register_script(const char *name, const char *path) {
    if (script_count >= MAX_SCRIPTS) {
        LOG_ERROR("uep_scripts: Maximum scripts reached");
        return -1;
    }

    /* Check if already registered */
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (scripts[i].active && strcmp(scripts[i].name, name) == 0) {
            LOG_WARNF("uep_scripts: Script '%s' already registered", name);
            return -1;
        }
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (!scripts[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        LOG_ERROR("uep_scripts: No free slots");
        return -1;
    }

    scripts[slot].name = SAFE_STRDUP(name, "script name");
    scripts[slot].path = SAFE_STRDUP(path, "script path");
    scripts[slot].active = true;
    script_count++;

    LOG_INFOF("uep_scripts: Registered script '%s' -> %s", name, path);
    return 0;
}

/*
 * Unregister a script
 */
static void unregister_script(int slot) {
    if (slot < 0 || slot >= MAX_SCRIPTS || !scripts[slot].active) return;

    free(scripts[slot].name);
    free(scripts[slot].path);
    scripts[slot].name = nullptr;
    scripts[slot].path = nullptr;
    scripts[slot].active = false;
    script_count--;
}

void uep_scripts_init(void) {
    memset(scripts, 0, sizeof(scripts));
    script_count = 0;
    initialized = true;
    LOG_INFO("uep_scripts: Initialized");
}

void uep_scripts_set_dir(const char *dir) {
    if (!dir) return;
    expand_path(dir, scripts_dir, sizeof(scripts_dir));
    LOG_INFOF("uep_scripts: Scripts directory set to '%s'", scripts_dir);
}

int uep_scripts_load(void) {
    if (!initialized) {
        uep_scripts_init();
    }

    if (scripts_dir[0] == '\0') {
        /* Default to ~/.config/muemacs/scripts */
        const char *home = getenv("HOME");
        if (home) {
            snprintf(scripts_dir, sizeof(scripts_dir),
                     "%s/.config/muemacs/scripts", home);
        } else {
            LOG_WARN("uep_scripts: No scripts directory configured");
            return 0;
        }
    }

    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(scripts_dir, &st) != 0) {
        if (mkdir(scripts_dir, 0755) != 0 && errno != EEXIST) {
            LOG_WARNF("uep_scripts: Cannot create scripts directory: %s", scripts_dir);
            return 0;
        }
        LOG_INFOF("uep_scripts: Created scripts directory: %s", scripts_dir);
    }

    /* Scan directory */
    DIR *dir = opendir(scripts_dir);
    if (!dir) {
        LOG_WARNF("uep_scripts: Cannot open scripts directory: %s", scripts_dir);
        return 0;
    }

    int loaded = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != nullptr) {
        /* Skip hidden files and . / .. */
        if (entry->d_name[0] == '.') continue;

        /* Build full path */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", scripts_dir, entry->d_name);

        /* Check if it's a regular file and executable */
        struct stat fst;
        if (stat(path, &fst) != 0) continue;
        if (!S_ISREG(fst.st_mode)) continue;
        if (!is_executable(path)) continue;

        /* Extract command name */
        char *name = extract_command_name(entry->d_name);
        if (!name) continue;

        /* Register it */
        if (register_script(name, path) == 0) {
            loaded++;
        }

        free(name);
    }

    closedir(dir);

    if (loaded > 0) {
        LOG_INFOF("uep_scripts: Loaded %d scripts from %s", loaded, scripts_dir);
        mlwrite("Loaded %d scripts from %s", loaded, scripts_dir);
    }

    return loaded;
}

int uep_scripts_reload(void) {
    /* Clear existing scripts */
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (scripts[i].active) {
            unregister_script(i);
        }
    }

    /* Reload */
    return uep_scripts_load();
}

int uep_scripts_list(void) {
    if (script_count == 0) {
        mlwrite("No scripts loaded");
        return 0;
    }

    /* Create a list buffer */
    struct buffer *bp = bfind("*Scripts*", true, 0);
    if (!bp) {
        mlwrite("Cannot create scripts list buffer");
        return 0;
    }

    /* Clear and switch to it */
    if (swbuffer(bp) != true)
        return 0;

    /* Delete existing content */
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        struct line *next = lforw(lp);
        lfree(lp);
        lp = next;
    }
    bp->b_linep->l_fp = bp->b_linep;
    bp->b_linep->l_bp = bp->b_linep;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    /* Insert header */
    char header[256];
    snprintf(header, sizeof(header), "=== %d Scripts Loaded ===\n\n", script_count);
    for (size_t i = 0; i < strlen(header); i++) {
        if (header[i] == '\n') lnewline();
        else linsert(1, header[i]);
    }

    /* Insert script info */
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (!scripts[i].active) continue;

        char line[1024];
        snprintf(line, sizeof(line), "M-x %-20s  %s\n",
                 scripts[i].name, scripts[i].path);

        for (size_t j = 0; j < strlen(line); j++) {
            if (line[j] == '\n') lnewline();
            else linsert(1, line[j]);
        }
    }

    /* Go to top */
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    bp->b_flag &= ~BFCHG;

    return script_count;
}

void uep_scripts_cleanup(void) {
    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (scripts[i].active) {
            unregister_script(i);
        }
    }
    initialized = false;
    LOG_INFO("uep_scripts: Cleaned up");
}

const char *uep_scripts_find(const char *name) {
    if (!name) return nullptr;

    for (int i = 0; i < MAX_SCRIPTS; i++) {
        if (scripts[i].active && strcmp(scripts[i].name, name) == 0) {
            return scripts[i].path;
        }
    }
    return nullptr;
}

/*
 * Get current buffer contents as a string
 */
static char *get_buffer_contents(size_t *len) {
    if (!curbp) {
        if (len) *len = 0;
        return nullptr;
    }

    /* Calculate total size */
    size_t total = 0;
    struct line *lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        total += (size_t)llength(lp) + 1;  /* +1 for newline */
        lp = lforw(lp);
    }

    if (total == 0) {
        if (len) *len = 0;
        return SAFE_STRDUP("", "script empty buffer");
    }

    /* Allocate buffer */
    char *buf = SAFE_ALLOC_SIZED(char, total + 1, "script buffer contents");
    if (!buf) {
        if (len) *len = 0;
        return nullptr;
    }

    /* Copy lines */
    size_t pos = 0;
    lp = lforw(curbp->b_linep);
    while (lp != curbp->b_linep) {
        int line_len = llength(lp);
        for (int i = 0; i < line_len; i++) {
            buf[pos++] = (char)lgetc(lp, i);
        }
        buf[pos++] = '\n';
        lp = lforw(lp);
    }

    buf[pos] = '\0';
    if (len) *len = pos;
    return buf;
}

/*
 * Show script output
 */
static void show_output(const char *output, size_t len, const char *script_name) {
    if (!output || len == 0) {
        mlwrite("[%s] (no output)", script_name);
        return;
    }

    /* Count newlines */
    int newlines = 0;
    for (size_t i = 0; i < len; i++) {
        if (output[i] == '\n') newlines++;
    }

    /* Single line (or no newlines) -> message line */
    if (newlines <= 1) {
        /* Trim trailing newline */
        char *trimmed = SAFE_STRDUP(output, "script output trimmed");
        if (trimmed) {
            size_t tlen = strlen(trimmed);
            while (tlen > 0 && (trimmed[tlen-1] == '\n' || trimmed[tlen-1] == '\r')) {
                trimmed[--tlen] = '\0';
            }
            mlwrite("[%s] %s", script_name, trimmed);
            SAFE_FREE(trimmed);
        }
        return;
    }

    /* Multi-line -> output buffer */
    struct buffer *bp = bfind("*Script Output*", true, 0);
    if (!bp) {
        mlwrite("[%s] Cannot create output buffer", script_name);
        return;
    }

    (void)swbuffer(bp);  // display buffer; failure non-fatal

    /* Clear buffer */
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        struct line *next = lforw(lp);
        lfree(lp);
        lp = next;
    }
    bp->b_linep->l_fp = bp->b_linep;
    bp->b_linep->l_bp = bp->b_linep;
    curwp->w_dotp = bp->b_linep;
    curwp->w_doto = 0;

    /* Insert header */
    char header[256];
    snprintf(header, sizeof(header), "=== Output from %s ===\n\n", script_name);
    for (size_t i = 0; i < strlen(header); i++) {
        if (header[i] == '\n') lnewline();
        else linsert(1, header[i]);
    }

    /* Insert output */
    for (size_t i = 0; i < len; i++) {
        if (output[i] == '\n') lnewline();
        else if (output[i] != '\r') linsert(1, output[i]);
    }

    /* Go to top */
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;
    bp->b_flag &= ~BFCHG;

    mlwrite("[%s] Output in *Script Output* buffer", script_name);
}

int uep_scripts_exec(const char *name) {
    const char *path = uep_scripts_find(name);
    if (!path) {
        mlwrite("Script not found: %s", name);
        return 0;
    }

    /* Get buffer contents */
    size_t input_len = 0;
    char *input = get_buffer_contents(&input_len);

    /* Set up environment variables */
    char line_str[32], col_str[32];
    snprintf(line_str, sizeof(line_str), "%ld",
             curbp ? getlinenum(curbp, curwp->w_dotp) : 1);
    snprintf(col_str, sizeof(col_str), "%d",
             curwp ? curwp->w_doto + 1 : 1);

    setenv("MUEMACS_BUFFER_NAME", curbp ? curbp->b_bname : "", 1);
    setenv("MUEMACS_BUFFER_FILE", curbp && curbp->b_fname[0] ? curbp->b_fname : "", 1);
    setenv("MUEMACS_LINE", line_str, 1);
    setenv("MUEMACS_COLUMN", col_str, 1);

    /* Create pipes */
    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        LOG_ERROR("uep_scripts: pipe failed");
        free(input);
        return 0;
    }

    mlwrite("Running %s...", name);
    update(true);

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("uep_scripts: fork failed");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        free(input);
        return 0;
    }

    if (pid == 0) {
        /* Child process */
        close(stdin_pipe[1]);   /* Close write end of stdin pipe */
        close(stdout_pipe[0]);  /* Close read end of stdout pipe */

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        execl(path, path, (char *)nullptr);
        _exit(127);
    }

    /* Parent process */
    close(stdin_pipe[0]);   /* Close read end of stdin pipe */
    close(stdout_pipe[1]);  /* Close write end of stdout pipe */

    /* Write input to script's stdin */
    if (input && input_len > 0) {
        if (write(stdin_pipe[1], input, input_len) < 0) { /* ignore */ }
    }
    close(stdin_pipe[1]);
    SAFE_FREE(input);

    /* Read output */
    size_t capacity = 4096;
    size_t pos = 0;
    char *output = SAFE_ALLOC_SIZED(char, capacity, "script output");

    if (output) {
        struct pollfd pfd = { .fd = stdout_pipe[0], .events = POLLIN };

        for (;;) {
            int poll_ret = poll(&pfd, 1, 10000);  /* 10 second timeout */
            if (poll_ret <= 0) break;

            if (pos + 4096 > capacity) {
                capacity *= 2;
                char *new_output = SAFE_REALLOC(output, capacity, "script output grow");
                if (!new_output) break;
                output = new_output;
            }

            ssize_t n = read(stdout_pipe[0], output + pos, 4096);
            if (n <= 0) break;
            pos += (size_t)n;
        }

        output[pos] = '\0';
    }

    close(stdout_pipe[0]);

    int status;
    waitpid(pid, &status, 0);

    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    if (exit_code != 0) {
        mlwrite("[%s] Exit code %d", name, exit_code);
        if (output) {
            LOG_WARNF("uep_scripts: %s failed: %s", name, output);
        }
    } else {
        show_output(output, pos, name);
    }

    SAFE_FREE(output);

    /* Clean up environment */
    unsetenv("MUEMACS_BUFFER_NAME");
    unsetenv("MUEMACS_BUFFER_FILE");
    unsetenv("MUEMACS_LINE");
    unsetenv("MUEMACS_COLUMN");

    return exit_code == 0 ? 1 : 0;
}

/*
 * Command: Reload scripts
 */
int uep_scripts_reload_cmd(int f, int n) {
    (void)f;
    (void)n;

    int count = uep_scripts_reload();
    mlwrite("Reloaded %d scripts", count);
    return 1;
}

/*
 * Command: List scripts
 */
int uep_scripts_list_cmd(int f, int n) {
    (void)f;
    (void)n;

    uep_scripts_list();
    return 1;
}

/*
 * Look up a script command by name.
 * Called from fncmatch() in bind.c.
 * Returns a function pointer that will execute the script.
 */
typedef int (*fn_t)(int, int);

/* We need a way to execute a specific script when its command is invoked.
 * Since we can't pass the script name to a generic handler, we use a
 * lookup table approach where we return a unique handler per script.
 *
 * Actually, we'll use the extension API's dynamic command system.
 * But for now, let's hook into bind.c differently - we'll add a
 * uep_scripts_execute() that takes the command name.
 */

/*
 * Try to execute a command as a script.
 * Returns 1 if executed, 0 if not a script.
 */
int uep_scripts_try_execute(const char *name) {
    if (!name) return 0;

    const char *path = uep_scripts_find(name);
    if (!path) return 0;

    return uep_scripts_exec(name);
}

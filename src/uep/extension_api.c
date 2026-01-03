/*
 * extension_api.c - μEmacs Editor API Implementation
 *
 * Implements the API struct passed to extensions.
 * Bridges extension calls to internal μEmacs functions.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"
#include "internal/memory.h"
#include "internal/line.h"
#include "util/logger.h"

/* Dynamic command registry - extensions can register commands here */
#define MAX_DYNAMIC_COMMANDS 256

typedef struct {
    char *name;
    uemacs_cmd_fn func;
    bool active;
} dynamic_command_t;

static dynamic_command_t dynamic_commands[MAX_DYNAMIC_COMMANDS];
static int dynamic_command_count = 0;

/* Event hook registrations */
#define MAX_HOOKS 32

static uemacs_buffer_cb buffer_save_hooks[MAX_HOOKS];
static int buffer_save_hook_count = 0;

static uemacs_buffer_cb buffer_load_hooks[MAX_HOOKS];
static int buffer_load_hook_count = 0;

static uemacs_key_cb key_hooks[MAX_HOOKS];
static int key_hook_count = 0;

static uemacs_idle_cb idle_hooks[MAX_HOOKS];
static int idle_hook_count = 0;

static uemacs_char_transform_cb char_transform_hooks[MAX_HOOKS];
static int char_transform_hook_count = 0;

/* === Command Registration === */

static int api_register_command(const char *name, uemacs_cmd_fn func) {
    if (!name || !func) return -1;

    /* Check if already registered */
    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            LOG_WARNF("Extension API: Command already registered: %s", name);
            return -1;
        }
    }

    /* Find free slot */
    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (!dynamic_commands[i].active) {
            dynamic_commands[i].name = strdup(name);
            dynamic_commands[i].func = func;
            dynamic_commands[i].active = true;
            dynamic_command_count++;
            LOG_INFOF("Extension API: Registered command: %s", name);
            return 0;
        }
    }

    LOG_ERROR("Extension API: Maximum dynamic commands reached");
    return -1;
}

static int api_unregister_command(const char *name) {
    if (!name) return -1;

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            free(dynamic_commands[i].name);
            dynamic_commands[i].name = nullptr;
            dynamic_commands[i].func = nullptr;
            dynamic_commands[i].active = false;
            dynamic_command_count--;
            LOG_INFOF("Extension API: Unregistered command: %s", name);
            return 0;
        }
    }

    LOG_WARNF("Extension API: Command not found: %s", name);
    return -1;
}

/* Look up dynamic command by name (called by exec.c) */
uemacs_cmd_fn extension_find_command(const char *name) {
    if (!name) return nullptr;

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            return dynamic_commands[i].func;
        }
    }
    return nullptr;
}

/* === Event Hooks === */

static int api_on_buffer_save(uemacs_buffer_cb cb) {
    if (!cb || buffer_save_hook_count >= MAX_HOOKS) return -1;
    buffer_save_hooks[buffer_save_hook_count++] = cb;
    return 0;
}

static int api_on_buffer_load(uemacs_buffer_cb cb) {
    if (!cb || buffer_load_hook_count >= MAX_HOOKS) return -1;
    buffer_load_hooks[buffer_load_hook_count++] = cb;
    return 0;
}

static int api_on_key(uemacs_key_cb cb) {
    if (!cb || key_hook_count >= MAX_HOOKS) return -1;
    key_hooks[key_hook_count++] = cb;
    return 0;
}

static int api_on_idle(uemacs_idle_cb cb) {
    if (!cb || idle_hook_count >= MAX_HOOKS) return -1;
    idle_hooks[idle_hook_count++] = cb;
    return 0;
}

static int api_on_char_transform(uemacs_char_transform_cb cb) {
    if (!cb || char_transform_hook_count >= MAX_HOOKS) return -1;
    char_transform_hooks[char_transform_hook_count++] = cb;
    return 0;
}

static int api_off_buffer_save(uemacs_buffer_cb cb) {
    for (int i = 0; i < buffer_save_hook_count; i++) {
        if (buffer_save_hooks[i] == cb) {
            memmove(&buffer_save_hooks[i], &buffer_save_hooks[i+1],
                    (buffer_save_hook_count - i - 1) * sizeof(uemacs_buffer_cb));
            buffer_save_hook_count--;
            return 0;
        }
    }
    return -1;
}

static int api_off_buffer_load(uemacs_buffer_cb cb) {
    for (int i = 0; i < buffer_load_hook_count; i++) {
        if (buffer_load_hooks[i] == cb) {
            memmove(&buffer_load_hooks[i], &buffer_load_hooks[i+1],
                    (buffer_load_hook_count - i - 1) * sizeof(uemacs_buffer_cb));
            buffer_load_hook_count--;
            return 0;
        }
    }
    return -1;
}

static int api_off_key(uemacs_key_cb cb) {
    for (int i = 0; i < key_hook_count; i++) {
        if (key_hooks[i] == cb) {
            memmove(&key_hooks[i], &key_hooks[i+1],
                    (key_hook_count - i - 1) * sizeof(uemacs_key_cb));
            key_hook_count--;
            return 0;
        }
    }
    return -1;
}

static int api_off_idle(uemacs_idle_cb cb) {
    for (int i = 0; i < idle_hook_count; i++) {
        if (idle_hooks[i] == cb) {
            memmove(&idle_hooks[i], &idle_hooks[i+1],
                    (idle_hook_count - i - 1) * sizeof(uemacs_idle_cb));
            idle_hook_count--;
            return 0;
        }
    }
    return -1;
}

static int api_off_char_transform(uemacs_char_transform_cb cb) {
    for (int i = 0; i < char_transform_hook_count; i++) {
        if (char_transform_hooks[i] == cb) {
            memmove(&char_transform_hooks[i], &char_transform_hooks[i+1],
                    (char_transform_hook_count - i - 1) * sizeof(uemacs_char_transform_cb));
            char_transform_hook_count--;
            return 0;
        }
    }
    return -1;
}

/* Called by μEmacs internals to fire hooks */
void extension_fire_buffer_save(struct buffer *bp) {
    for (int i = 0; i < buffer_save_hook_count; i++) {
        buffer_save_hooks[i](bp);
    }
}

void extension_fire_buffer_load(struct buffer *bp) {
    for (int i = 0; i < buffer_load_hook_count; i++) {
        buffer_load_hooks[i](bp);
    }
}

bool extension_fire_key(int key) {
    for (int i = 0; i < key_hook_count; i++) {
        if (key_hooks[i](key)) {
            return true;  /* Key was consumed by this hook */
        }
    }
    return false;  /* No hook consumed the key */
}

void extension_fire_idle(void) {
    for (int i = 0; i < idle_hook_count; i++) {
        idle_hooks[i]();
    }
}

/*
 * Fire char transform hooks - called from main.c self_insert
 * Returns: 0 = no transform, 1 = use *out, -1 = delete prev char then insert *out
 */
int extension_fire_char_transform(int c, int *out) {
    for (int i = 0; i < char_transform_hook_count; i++) {
        int result = char_transform_hooks[i](c, out);
        if (result != 0) {
            return result;  /* First hook to transform wins */
        }
    }
    return 0;  /* No transform */
}

/* === Buffer Operations === */

static struct buffer *api_current_buffer(void) {
    return curbp;
}

static struct buffer *api_find_buffer(const char *name) {
    if (!name) return nullptr;

    struct buffer *bp = bheadp;
    while (bp) {
        if (strcmp(bp->b_bname, name) == 0) {
            return bp;
        }
        bp = bp->b_bufp;
    }
    return nullptr;
}

static char *api_buffer_contents(struct buffer *bp, size_t *len) {
    if (!bp) {
        if (len) *len = 0;
        return nullptr;
    }

    /* Calculate total size */
    size_t total = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        total += llength(lp) + 1;  /* +1 for newline */
        lp = lforw(lp);
    }

    if (total == 0) {
        if (len) *len = 0;
        return strdup("");
    }

    /* Allocate buffer */
    char *buf = malloc(total + 1);
    if (!buf) {
        if (len) *len = 0;
        return nullptr;
    }

    /* Copy lines */
    size_t pos = 0;
    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        int line_len = llength(lp);
        for (int i = 0; i < line_len; i++) {
            buf[pos++] = lgetc(lp, i);
        }
        buf[pos++] = '\n';
        lp = lforw(lp);
    }

    /* Remove trailing newline if present */
    if (pos > 0 && buf[pos - 1] == '\n') {
        pos--;
    }

    buf[pos] = '\0';
    if (len) *len = pos;
    return buf;
}

static const char *api_buffer_filename(struct buffer *bp) {
    return bp ? bp->b_fname : nullptr;
}

static const char *api_buffer_name(struct buffer *bp) {
    return bp ? bp->b_bname : nullptr;
}

static bool api_buffer_modified(struct buffer *bp) {
    return bp ? (bp->b_flag & BFCHG) != 0 : false;
}

static int api_buffer_insert(const char *text, size_t len) {
    if (!text || len == 0) return 0;

    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (lnewline() != true) return -1;
        } else {
            if (linsert(1, text[i]) != true) return -1;
        }
    }
    return 0;
}

static int api_buffer_insert_at(struct buffer *bp, int line, int col,
                                const char *text, size_t len) {
    if (!bp || !text || len == 0) return 0;
    if (line < 1 || col < 1) return -1;

    /* Save current position */
    struct buffer *saved_bp = curbp;
    struct line *saved_dotp = curwp->w_dotp;
    int saved_doto = curwp->w_doto;

    /* Switch to target buffer if needed */
    if (bp != curbp) {
        curbp = bp;
    }

    /* Navigate to the target line */
    struct line *lp = lforw(bp->b_linep);
    for (int i = 1; i < line && lp != bp->b_linep; i++) {
        lp = lforw(lp);
    }

    if (lp == bp->b_linep) {
        /* Line doesn't exist - restore and fail */
        curbp = saved_bp;
        curwp->w_dotp = saved_dotp;
        curwp->w_doto = saved_doto;
        LOG_WARNF("Extension API: buffer_insert_at line %d doesn't exist", line);
        return -1;
    }

    /* Set point to target position */
    curwp->w_dotp = lp;
    int offset = col - 1;  /* Convert to 0-based */
    if (offset < 0) offset = 0;
    if (offset > llength(lp)) offset = llength(lp);
    curwp->w_doto = offset;

    /* Insert text */
    int result = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (lnewline() != true) { result = -1; break; }
        } else {
            if (linsert(1, text[i]) != true) { result = -1; break; }
        }
    }

    /* Restore original position */
    curbp = saved_bp;
    curwp->w_dotp = saved_dotp;
    curwp->w_doto = saved_doto;

    if (result == 0) {
        LOG_DEBUGF("Extension API: Inserted %zu bytes at line %d col %d", len, line, col);
    }

    return result;
}

/* === Cursor/Point Operations === */

static void api_get_point(int *line, int *col) {
    if (line) *line = (int)getlinenum(curbp, curwp->w_dotp);
    if (col) *col = curwp->w_doto + 1;  /* 1-based for extensions */
}

static void api_set_point(int line, int col) {
    /* Go to line */
    struct line *lp = lforw(curbp->b_linep);
    for (int i = 1; i < line && lp != curbp->b_linep; i++) {
        lp = lforw(lp);
    }

    if (lp != curbp->b_linep) {
        curwp->w_dotp = lp;
        int offset = col - 1;  /* Convert to 0-based */
        if (offset < 0) offset = 0;
        if (offset > llength(lp)) offset = llength(lp);
        curwp->w_doto = offset;
        curwp->w_flag |= WFMOVE;
    }
}

static int api_get_line_count(struct buffer *bp) {
    if (!bp) return 0;

    int count = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        count++;
        lp = lforw(lp);
    }
    return count;
}

static struct buffer *api_buffer_create(const char *name) {
    if (!name) return nullptr;
    return bfind((char *)name, true, 0);
}

static int api_buffer_switch(struct buffer *bp) {
    if (!bp) return -1;
    return swbuffer(bp) == true ? 0 : -1;
}

static int api_buffer_clear(struct buffer *bp) {
    if (!bp) return -1;

    /* Delete all lines in buffer */
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        struct line *next = lforw(lp);
        lfree(lp);
        lp = next;
    }

    /* Reset buffer state */
    bp->b_linep->l_fp = bp->b_linep;
    bp->b_linep->l_bp = bp->b_linep;
    bp->b_dotp = bp->b_linep;
    bp->b_doto = 0;
    bp->b_markp = nullptr;
    bp->b_marko = 0;
    bp->b_flag &= ~BFCHG;  /* Mark as not changed */

    /* Update window if showing this buffer */
    struct window *wp = wheadp;
    while (wp) {
        if (wp->w_bufp == bp) {
            wp->w_dotp = bp->b_linep;
            wp->w_doto = 0;
            wp->w_markp = nullptr;
            wp->w_marko = 0;
            wp->w_flag |= WFHARD;
        }
        wp = wp->w_wndp;
    }

    return 0;
}

static char *api_get_word_at_point(void) {
    if (!curwp || !curwp->w_dotp) return nullptr;

    struct line *lp = curwp->w_dotp;
    int offset = curwp->w_doto;
    int len = llength(lp);

    if (offset >= len) return nullptr;

    /* Check if we're on a word character */
    char c = lgetc(lp, offset);
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
        return nullptr;
    }

    /* Find word start */
    int start = offset;
    while (start > 0) {
        c = lgetc(lp, start - 1);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        start--;
    }

    /* Find word end */
    int end = offset;
    while (end < len) {
        c = lgetc(lp, end);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        end++;
    }

    /* Extract word */
    int word_len = end - start;
    if (word_len <= 0) return nullptr;

    char *word = malloc(word_len + 1);
    if (!word) return nullptr;

    for (int i = 0; i < word_len; i++) {
        word[i] = lgetc(lp, start + i);
    }
    word[word_len] = '\0';

    return word;
}

static char *api_get_current_line(void) {
    if (!curwp || !curwp->w_dotp) return nullptr;

    struct line *lp = curwp->w_dotp;
    int len = llength(lp);

    char *line = malloc(len + 1);
    if (!line) return nullptr;

    for (int i = 0; i < len; i++) {
        line[i] = lgetc(lp, i);
    }
    line[len] = '\0';

    return line;
}

static int api_find_file_line(const char *path, int line) {
    if (!path) return -1;

    /* Open the file */
    if (getfile(path, true) != true) {
        return -1;
    }

    /* Go to line if specified */
    if (line > 0) {
        gotoline(true, line);
    }

    return 0;
}

/* === Window Operations === */

static struct window *api_current_window(void) {
    return curwp;
}

static int api_window_count(void) {
    int count = 0;
    struct window *wp = wheadp;
    while (wp) {
        count++;
        wp = wp->w_wndp;
    }
    return count;
}

static int api_window_set_wrap_col(struct window *wp, int col) {
    if (!wp) wp = curwp;
    if (!wp) return -1;
    wp->w_wrap_col = col;
    wp->w_flag |= WFHARD;  /* Force redraw */
    return 0;
}

/* === User Interface === */

static void api_message(const char *fmt, ...) {
    if (!fmt) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    mlwrite("%s", buf);
}

static void api_vmessage(const char *fmt, va_list ap) {
    if (!fmt) return;

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    mlwrite("%s", buf);
}

static int api_prompt(const char *prompt, char *buf, size_t buflen) {
    if (!prompt || !buf || buflen == 0) return -1;
    return minibuf_read(prompt, buf, (int)buflen) == true ? 0 : -1;
}

static int api_prompt_yn(const char *prompt) {
    if (!prompt) return -1;
    return mlyesno(prompt) == true ? 1 : 0;
}

static void api_update_display(void) {
    update(true);
}

/* === Shell Integration === */

static int api_shell_command(const char *cmd, char **output, size_t *len) {
    if (!cmd) return -1;
    if (output) *output = nullptr;
    if (len) *len = 0;

    int pipefd[2];
    if (pipe(pipefd) < 0) {
        LOG_ERROR("Extension API: pipe failed");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        LOG_ERROR("Extension API: fork failed");
        return -1;
    }

    if (pid == 0) {
        /* Child */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";
        execlp(shell, shell, "-c", cmd, (char *)nullptr);
        _exit(127);
    }

    /* Parent */
    close(pipefd[1]);

    /* Read output */
    size_t capacity = 4096;
    size_t pos = 0;
    char *buf = malloc(capacity);

    if (buf) {
        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };

        for (;;) {
            int poll_ret = poll(&pfd, 1, 5000);  /* 5 second timeout */
            if (poll_ret <= 0) break;

            if (pos + 4096 > capacity) {
                capacity *= 2;
                char *new_buf = realloc(buf, capacity);
                if (!new_buf) break;
                buf = new_buf;
            }

            ssize_t n = read(pipefd[0], buf + pos, 4096);
            if (n <= 0) break;
            pos += n;
        }

        buf[pos] = '\0';
        if (output) *output = buf;
        else free(buf);
        if (len) *len = pos;
    }

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* === Memory Helpers === */

static void *api_alloc(size_t size) {
    return malloc(size);
}

static void api_free(void *ptr) {
    free(ptr);
}

static char *api_strdup(const char *s) {
    return s ? strdup(s) : nullptr;
}

/* === Logging === */

static void api_log_info(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG_INFOF("Extension: %s", buf);
}

static void api_log_warn(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG_WARNF("Extension: %s", buf);
}

static void api_log_error(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG_ERRORF("Extension: %s", buf);
}

static void api_log_debug(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG_DEBUGF("Extension: %s", buf);
}

/* === The Global API Instance === */

static struct uemacs_api global_api = {
    .api_version = UEMACS_API_VERSION,

    /* Command registration */
    .register_command = api_register_command,
    .unregister_command = api_unregister_command,

    /* Event hooks */
    .on_buffer_save = api_on_buffer_save,
    .on_buffer_load = api_on_buffer_load,
    .on_key = api_on_key,
    .on_idle = api_on_idle,
    .on_char_transform = api_on_char_transform,
    .off_buffer_save = api_off_buffer_save,
    .off_buffer_load = api_off_buffer_load,
    .off_key = api_off_key,
    .off_idle = api_off_idle,
    .off_char_transform = api_off_char_transform,

    /* Buffer operations */
    .current_buffer = api_current_buffer,
    .find_buffer = api_find_buffer,
    .buffer_contents = api_buffer_contents,
    .buffer_filename = api_buffer_filename,
    .buffer_name = api_buffer_name,
    .buffer_modified = api_buffer_modified,
    .buffer_insert = api_buffer_insert,
    .buffer_insert_at = api_buffer_insert_at,
    .buffer_create = api_buffer_create,
    .buffer_switch = api_buffer_switch,
    .buffer_clear = api_buffer_clear,

    /* Cursor/point */
    .get_point = api_get_point,
    .set_point = api_set_point,
    .get_line_count = api_get_line_count,
    .get_word_at_point = api_get_word_at_point,
    .get_current_line = api_get_current_line,

    /* Window operations */
    .current_window = api_current_window,
    .window_count = api_window_count,
    .window_set_wrap_col = api_window_set_wrap_col,

    /* User interface */
    .message = api_message,
    .vmessage = api_vmessage,
    .prompt = api_prompt,
    .prompt_yn = api_prompt_yn,
    .update_display = api_update_display,

    /* File operations */
    .find_file_line = api_find_file_line,

    /* Shell integration */
    .shell_command = api_shell_command,

    /* Memory helpers */
    .alloc = api_alloc,
    .free = api_free,
    .strdup = api_strdup,

    /* Logging */
    .log_info = api_log_info,
    .log_warn = api_log_warn,
    .log_error = api_log_error,
    .log_debug = api_log_debug,
};

struct uemacs_api *uemacs_get_api(void) {
    return &global_api;
}

/* Initialize API subsystem (called from main) */
void extension_api_init(void) {
    memset(dynamic_commands, 0, sizeof(dynamic_commands));
    dynamic_command_count = 0;

    memset(buffer_save_hooks, 0, sizeof(buffer_save_hooks));
    buffer_save_hook_count = 0;

    memset(buffer_load_hooks, 0, sizeof(buffer_load_hooks));
    buffer_load_hook_count = 0;

    memset(key_hooks, 0, sizeof(key_hooks));
    key_hook_count = 0;

    memset(idle_hooks, 0, sizeof(idle_hooks));
    idle_hook_count = 0;

    memset(char_transform_hooks, 0, sizeof(char_transform_hooks));
    char_transform_hook_count = 0;

    LOG_INFO("Extension API: Initialized");
}

/* Cleanup API subsystem */
void extension_api_cleanup(void) {
    /* Free dynamic command names */
    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active) {
            free(dynamic_commands[i].name);
        }
    }
    memset(dynamic_commands, 0, sizeof(dynamic_commands));
    dynamic_command_count = 0;

    LOG_INFO("Extension API: Cleaned up");
}

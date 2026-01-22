/*
 * ext_runner.c - Out-of-Process Extension Runner (Atomic IPC)
 *
 * Standalone executable spawned by ext_host for extensions requiring isolation.
 * Loads extension .so and proxies API calls back to editor via atomic ring buffers.
 *
 * Usage: uemacs-ext-runner --memfd N --ext PATH
 *
 * C23 compliant
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "uep/ext_ipc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"

/* =========================================================================
 * Local utility functions
 * ========================================================================= */

static int safe_atoi(const char *str, int default_val)
{
    if (!str || !*str) return default_val;
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno == ERANGE || val > INT_MAX || val < INT_MIN) return default_val;
    if (endptr == str) return default_val;
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    return (int)val;
}

/* =========================================================================
 * Global state
 * ========================================================================= */

static ext_ipc_channel_t *g_ipc = NULL;
static void *g_ext_handle = NULL;
static volatile sig_atomic_t g_running = 1;
static int g_death_fd = -1;  /* Read end of death pipe - POLLHUP when parent dies */

/* Local command registry */
typedef struct {
    char name[64];
    uemacs_cmd_fn fn;
    bool active;
} local_command_t;

#define MAX_LOCAL_COMMANDS 64
static local_command_t g_local_commands[MAX_LOCAL_COMMANDS];
static int g_num_commands = 0;

/* Local event handler registry */
typedef struct {
    char event[64];
    uemacs_event_fn handler;
    void *user_data;
    int priority;
    bool active;
} local_handler_t;

#define MAX_LOCAL_HANDLERS 128
static local_handler_t g_local_handlers[MAX_LOCAL_HANDLERS];
static int g_num_handlers = 0;

/* =========================================================================
 * Signal handling
 * ========================================================================= */

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* =========================================================================
 * IPC-based logging (atomic, goes through main editor)
 * ========================================================================= */

static void ipc_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (g_ipc && g_ipc->shm) {
        /* Send via IPC - main editor will write to log atomically */
        ext_ipc_ring_t *ring = &g_ipc->shm->to_editor;
        int slot_idx = ext_ipc_find_empty_slot(ring);
        if (slot_idx >= 0) {
            ext_ipc_slot_write(&ring->slots[slot_idx], EXT_MSG_LOG, buf, strlen(buf) + 1);
        }
    }
    /* If IPC not ready, log is silently dropped - early startup logs not critical */
}

/* =========================================================================
 * Local command management
 * ========================================================================= */

static uemacs_cmd_fn find_local_command(const char *name)
{
    for (int i = 0; i < MAX_LOCAL_COMMANDS; i++) {
        if (g_local_commands[i].active &&
            strcmp(g_local_commands[i].name, name) == 0) {
            return g_local_commands[i].fn;
        }
    }
    return NULL;
}

/* =========================================================================
 * Atomic IPC Helpers
 *
 * Extension -> Editor calls: write to to_editor ring, spin for COMPLETE
 * Editor -> Extension calls: received from to_ext ring
 * ========================================================================= */

/* Send request to editor and spin-wait for response */
static int call_editor(uint32_t msg_type, const void *req, uint32_t req_len,
                       void *resp, uint32_t *resp_len)
{
    ext_ipc_ring_t *ring = &g_ipc->shm->to_editor;

    /* Find empty slot */
    int slot_idx = ext_ipc_find_empty_slot(ring);
    if (slot_idx < 0) {
        /* Ring full - spin briefly and retry */
        for (int retry = 0; retry < 100 && slot_idx < 0; retry++) {
            sched_yield();
            slot_idx = ext_ipc_find_empty_slot(ring);
        }
        if (slot_idx < 0) return -1;  /* Still full */
    }

    ext_ipc_slot_t *slot = &ring->slots[slot_idx];

    /* Write request */
    ext_ipc_slot_write(slot, msg_type, req, req_len);

    /* Spin-wait for response (30 second timeout) */
    if (!ext_ipc_slot_wait_complete(slot, 30000)) {
        ext_ipc_slot_release(slot);
        return -2;  /* Timeout */
    }

    /* Copy response if requested */
    if (resp && resp_len) {
        uint32_t copy_len = slot->payload_len;
        if (copy_len > *resp_len) copy_len = *resp_len;
        memcpy(resp, slot->payload, copy_len);
        *resp_len = copy_len;
    }

    int32_t result = slot->result;
    ext_ipc_slot_release(slot);
    return result;
}

/* Send notification (no response expected) */
static void notify_editor(uint32_t msg_type, const void *data, uint32_t len)
{
    ext_ipc_ring_t *ring = &g_ipc->shm->to_editor;
    int slot_idx = ext_ipc_find_empty_slot(ring);
    if (slot_idx < 0) return;

    ext_ipc_slot_t *slot = &ring->slots[slot_idx];
    ext_ipc_slot_write(slot, msg_type, data, len);

    /* For notifications, we still wait for acknowledgement to avoid overrun */
    ext_ipc_slot_wait_complete(slot, 5000);
    ext_ipc_slot_release(slot);
}

/* =========================================================================
 * Proxy API Implementation
 *
 * Each function sends a message to editor and waits for response.
 * ========================================================================= */

static int proxy_on(const char *event, uemacs_event_fn handler, void *user_data, int priority)
{
    /* Register locally */
    for (int i = 0; i < MAX_LOCAL_HANDLERS; i++) {
        if (!g_local_handlers[i].active) {
            strncpy(g_local_handlers[i].event, event, sizeof(g_local_handlers[i].event) - 1);
            g_local_handlers[i].handler = handler;
            g_local_handlers[i].user_data = user_data;
            g_local_handlers[i].priority = priority;
            g_local_handlers[i].active = true;
            g_num_handlers++;
            break;
        }
    }

    /* Notify editor */
    ext_ipc_event_sub_t req = { .priority = priority };
    strncpy(req.event_name, event, sizeof(req.event_name) - 1);
    return call_editor(EXT_MSG_EVENT_SUBSCRIBE, &req, sizeof(req), NULL, NULL);
}

static int proxy_off(const char *event, uemacs_event_fn handler)
{
    /* Remove from local registry */
    for (int i = 0; i < MAX_LOCAL_HANDLERS; i++) {
        if (g_local_handlers[i].active &&
            strcmp(g_local_handlers[i].event, event) == 0 &&
            g_local_handlers[i].handler == handler) {
            g_local_handlers[i].active = false;
            g_num_handlers--;
            break;
        }
    }

    ext_ipc_event_sub_t req = {0};
    strncpy(req.event_name, event, sizeof(req.event_name) - 1);
    return call_editor(EXT_MSG_EVENT_UNSUBSCRIBE, &req, sizeof(req), NULL, NULL);
}

static bool proxy_emit(const char *event, void *data)
{
    (void)data;
    notify_editor(EXT_MSG_EVENT_EMIT, event, strlen(event) + 1);
    return false;
}

static int proxy_config_int(const char *ext_name, const char *key, int default_val)
{
    struct {
        char ext_name[64];
        char key[64];
        int32_t default_val;
    } req = { .default_val = default_val };
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);

    int32_t result;
    uint32_t resp_len = sizeof(result);
    if (call_editor(EXT_MSG_CONFIG_INT, &req, sizeof(req), &result, &resp_len) < 0)
        return default_val;
    return result;
}

static bool proxy_config_bool(const char *ext_name, const char *key, bool default_val)
{
    struct {
        char ext_name[64];
        char key[64];
        int32_t default_val;
    } req = { .default_val = default_val };
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);

    int32_t result;
    uint32_t resp_len = sizeof(result);
    if (call_editor(EXT_MSG_CONFIG_BOOL, &req, sizeof(req), &result, &resp_len) < 0)
        return default_val;
    return result != 0;
}

static const char *proxy_config_string(const char *ext_name, const char *key, const char *default_val)
{
    static char response_buf[256];
    struct {
        char ext_name[64];
        char key[64];
        char default_val[128];
    } req = {0};
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);
    if (default_val) strncpy(req.default_val, default_val, sizeof(req.default_val) - 1);

    uint32_t resp_len = sizeof(response_buf);
    if (call_editor(EXT_MSG_CONFIG_STRING, &req, sizeof(req), response_buf, &resp_len) < 0)
        return default_val;
    return response_buf;
}

static int proxy_register_command(const char *name, uemacs_cmd_fn func)
{
    /* Store locally */
    for (int i = 0; i < MAX_LOCAL_COMMANDS; i++) {
        if (!g_local_commands[i].active) {
            strncpy(g_local_commands[i].name, name, sizeof(g_local_commands[i].name) - 1);
            g_local_commands[i].fn = func;
            g_local_commands[i].active = true;
            g_num_commands++;
            break;
        }
    }

    /* Notify editor */
    ext_ipc_cmd_register_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.name, name, sizeof(req.name) - 1);
    return call_editor(EXT_MSG_REGISTER_CMD, &req, sizeof(req), NULL, NULL);
}

static int proxy_unregister_command(const char *name)
{
    /* Remove from local registry */
    for (int i = 0; i < MAX_LOCAL_COMMANDS; i++) {
        if (g_local_commands[i].active &&
            strcmp(g_local_commands[i].name, name) == 0) {
            g_local_commands[i].active = false;
            g_num_commands--;
            break;
        }
    }

    ext_ipc_cmd_register_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.name, name, sizeof(req.name) - 1);
    return call_editor(EXT_MSG_UNREGISTER_CMD, &req, sizeof(req), NULL, NULL);
}

static struct buffer *proxy_current_buffer(void)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    call_editor(EXT_MSG_CURRENT_BUFFER, NULL, 0, &handle, &resp_len);
    return (struct buffer *)handle;
}

static struct buffer *proxy_find_buffer(const char *name)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    call_editor(EXT_MSG_FIND_BUFFER, name, strlen(name) + 1, &handle, &resp_len);
    return (struct buffer *)handle;
}

static char *proxy_buffer_contents(struct buffer *bp, size_t *len)
{
    static char *contents_buf = NULL;
    static size_t contents_size = 0;

    if (!contents_buf) {
        contents_size = 64 * 1024;
        contents_buf = malloc(contents_size);
    }

    uint32_t resp_len = contents_size;
    uintptr_t handle = (uintptr_t)bp;
    int result = call_editor(EXT_MSG_BUFFER_CONTENTS, &handle, sizeof(handle),
                             contents_buf, &resp_len);
    if (result < 0) {
        if (len) *len = 0;
        return NULL;
    }
    if (len) *len = resp_len;
    return contents_buf;
}

static const char *proxy_buffer_filename(struct buffer *bp)
{
    static char filename_buf[512];
    uint32_t resp_len = sizeof(filename_buf);
    uintptr_t handle = (uintptr_t)bp;
    if (call_editor(EXT_MSG_BUFFER_FILENAME, &handle, sizeof(handle),
                    filename_buf, &resp_len) < 0)
        return NULL;
    return filename_buf;
}

static const char *proxy_buffer_name(struct buffer *bp)
{
    static char name_buf[256];
    uint32_t resp_len = sizeof(name_buf);
    uintptr_t handle = (uintptr_t)bp;
    if (call_editor(EXT_MSG_BUFFER_NAME, &handle, sizeof(handle), name_buf, &resp_len) < 0)
        return NULL;
    return name_buf;
}

static bool proxy_buffer_modified(struct buffer *bp)
{
    int32_t result = 0;
    uint32_t resp_len = sizeof(result);
    uintptr_t handle = (uintptr_t)bp;
    call_editor(EXT_MSG_BUFFER_MODIFIED, &handle, sizeof(handle), &result, &resp_len);
    return result != 0;
}

static int proxy_buffer_insert(const char *text, size_t len)
{
    return call_editor(EXT_MSG_BUFFER_INSERT, text, len, NULL, NULL);
}

static int proxy_buffer_insert_at(struct buffer *bp, int line, int col,
                                  const char *text, size_t len)
{
    size_t req_size = sizeof(uintptr_t) + sizeof(int32_t) * 2 + len;
    char *req = malloc(req_size);
    if (!req) return -1;

    char *p = req;
    *(uintptr_t *)p = (uintptr_t)bp; p += sizeof(uintptr_t);
    *(int32_t *)p = line; p += sizeof(int32_t);
    *(int32_t *)p = col; p += sizeof(int32_t);
    memcpy(p, text, len);

    int result = call_editor(EXT_MSG_BUFFER_INSERT, req, req_size, NULL, NULL);
    free(req);
    return result;
}

static struct buffer *proxy_buffer_create(const char *name)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    call_editor(EXT_MSG_BUFFER_CREATE, name, strlen(name) + 1, &handle, &resp_len);
    return (struct buffer *)handle;
}

static int proxy_buffer_switch(struct buffer *bp)
{
    uintptr_t handle = (uintptr_t)bp;
    return call_editor(EXT_MSG_BUFFER_SWITCH, &handle, sizeof(handle), NULL, NULL);
}

static int proxy_buffer_clear(struct buffer *bp)
{
    uintptr_t handle = (uintptr_t)bp;
    return call_editor(EXT_MSG_BUFFER_CLEAR, &handle, sizeof(handle), NULL, NULL);
}

static void proxy_get_point(int *line, int *col)
{
    ext_ipc_point_t point = {0};
    uint32_t resp_len = sizeof(point);
    call_editor(EXT_MSG_GET_POINT, NULL, 0, &point, &resp_len);
    if (line) *line = point.line;
    if (col) *col = point.col;
}

static void proxy_set_point(int line, int col)
{
    ext_ipc_point_t point = { .line = line, .col = col };
    call_editor(EXT_MSG_SET_POINT, &point, sizeof(point), NULL, NULL);
}

static int proxy_get_line_count(struct buffer *bp)
{
    int32_t count = 0;
    uint32_t resp_len = sizeof(count);
    uintptr_t handle = (uintptr_t)bp;
    call_editor(EXT_MSG_BUFFER_CONTENTS, &handle, sizeof(handle), &count, &resp_len);
    return count;
}

static char *proxy_get_word_at_point(void)
{
    static char word_buf[256];
    uint32_t resp_len = sizeof(word_buf);
    if (call_editor(EXT_MSG_GET_WORD, NULL, 0, word_buf, &resp_len) < 0)
        return NULL;
    return word_buf;
}

static char *proxy_get_current_line(void)
{
    static char line_buf[4096];
    uint32_t resp_len = sizeof(line_buf);
    if (call_editor(EXT_MSG_GET_LINE, NULL, 0, line_buf, &resp_len) < 0)
        return NULL;
    return line_buf;
}

static char *proxy_get_line_at(struct buffer *bp, int line_num)
{
    (void)bp; (void)line_num;
    return NULL;  /* TODO: implement if needed */
}

/* Window operations - not applicable for out-of-process */
static struct window *proxy_current_window(void) { return NULL; }
static int proxy_window_count(void) { return 1; }
static int proxy_window_set_wrap_col(struct window *wp, int col) { (void)wp; (void)col; return 0; }
static struct window *proxy_window_at_row(int screen_row) { (void)screen_row; return NULL; }
static int proxy_window_switch(struct window *wp) { (void)wp; return 0; }

/* Screen/cursor helpers - not applicable for out-of-process */
static int proxy_screen_to_buffer_pos(struct window *wp, int screen_row, int screen_col,
                                      int *buf_line, int *buf_offset)
{
    (void)wp; (void)screen_row; (void)screen_col;
    if (buf_line) *buf_line = 0;
    if (buf_offset) *buf_offset = 0;
    return -1;
}
static int proxy_set_mark(void) { return 0; }
static int proxy_scroll_up(int lines) { (void)lines; return 0; }
static int proxy_scroll_down(int lines) { (void)lines; return 0; }

/* UI */
static void proxy_message(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.text, buf, sizeof(msg.text) - 1);
    notify_editor(EXT_MSG_MESSAGE, &msg, sizeof(msg));
}

static void proxy_vmessage(const char *fmt, va_list ap)
{
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    ext_ipc_message_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.text, buf, sizeof(msg.text) - 1);
    notify_editor(EXT_MSG_MESSAGE, &msg, sizeof(msg));
}

static int proxy_prompt(const char *prompt_text, char *buf, size_t buflen)
{
    ext_ipc_prompt_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.prompt, prompt_text, sizeof(req.prompt) - 1);

    ext_ipc_prompt_resp_t resp;
    memset(&resp, 0, sizeof(resp));
    uint32_t resp_len = sizeof(resp);
    int result = call_editor(EXT_MSG_PROMPT, &req, sizeof(req), &resp, &resp_len);

    if (result < 0 || resp.cancelled) return -1;
    strncpy(buf, resp.response, buflen - 1);
    buf[buflen - 1] = '\0';
    return 0;
}

static int proxy_prompt_yn(const char *prompt_text)
{
    ext_ipc_prompt_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.prompt, prompt_text, sizeof(req.prompt) - 1);

    int32_t result = 0;
    uint32_t resp_len = sizeof(result);
    call_editor(EXT_MSG_PROMPT_YN, &req, sizeof(req), &result, &resp_len);
    return result;
}

static void proxy_update_display(void)
{
    /* No-op for out-of-process - editor handles display */
}

/* File operations */
static int proxy_find_file_line(const char *path, int line)
{
    size_t path_len = strlen(path) + 1;
    size_t req_size = path_len + sizeof(int32_t);
    char *req = malloc(req_size);
    if (!req) return -1;

    memcpy(req, path, path_len);
    *(int32_t *)(req + path_len) = line;

    int result = call_editor(EXT_MSG_FIND_FILE, req, req_size, NULL, NULL);
    free(req);
    return result;
}

/* Shell integration */
static int proxy_shell_command(const char *cmd, char **output, size_t *len)
{
    static char output_buf[64 * 1024];

    uint32_t resp_len = sizeof(output_buf);
    int result = call_editor(EXT_MSG_SHELL_COMMAND, cmd, strlen(cmd) + 1,
                             output_buf, &resp_len);

    if (output) *output = (result >= 0) ? output_buf : NULL;
    if (len) *len = (result >= 0) ? resp_len : 0;
    return result;
}

/* Memory helpers */
static void *proxy_alloc(size_t size) {
    return malloc(size);
}
static void proxy_free(void *ptr) {
    free(ptr);
}
static char *proxy_strdup(const char *s) {
    return s ? strdup(s) : NULL;
}

/* Logging */
static void proxy_log_info(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_editor(EXT_MSG_LOG, buf, strlen(buf) + 1);
}

static void proxy_log_warn(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_editor(EXT_MSG_LOG, buf, strlen(buf) + 1);
}

static void proxy_log_error(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_editor(EXT_MSG_LOG, buf, strlen(buf) + 1);
}

static void proxy_log_debug(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    notify_editor(EXT_MSG_LOG, buf, strlen(buf) + 1);
}

/* Syntax highlighting - minimal support for out-of-process */
static int proxy_syntax_register_lexer(const char *name, const char **patterns,
                                       uemacs_syntax_lex_fn lex_fn, void *user_data)
{
    (void)name; (void)patterns; (void)lex_fn; (void)user_data;
    return -1;
}

static int proxy_syntax_unregister_lexer(const char *name) { (void)name; return -1; }

static int proxy_syntax_add_token(uemacs_line_tokens_t *tokens, int end_col, int face)
{
    (void)tokens; (void)end_col; (void)face;
    return -1;
}

static void proxy_syntax_invalidate_buffer(struct buffer *bp) { (void)bp; }

/* Modeline */
static int proxy_modeline_register(const char *name, uemacs_modeline_fn format_fn,
                                   void *user_data, int urgency)
{
    (void)format_fn; (void)user_data;
    ext_ipc_modeline_req_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.name, name, sizeof(req.name) - 1);
    req.priority = urgency;
    return call_editor(EXT_MSG_MODELINE_REGISTER, &req, sizeof(req), NULL, NULL);
}

static int proxy_modeline_unregister(const char *name)
{
    return call_editor(EXT_MSG_MODELINE_REGISTER, name, strlen(name) + 1, NULL, NULL);
}

static void proxy_modeline_refresh(void)
{
    /* No-op - editor handles refresh */
}

/* ABI-stable function lookup */
typedef void (*generic_fn_t)(void);
static generic_fn_t proxy_get_function(const char *name);

/* =========================================================================
 * Proxy API struct
 * ========================================================================= */

static struct uemacs_api proxy_api = {
    .api_version = UEMACS_API_VERSION,

    .on = proxy_on,
    .off = proxy_off,
    .emit = proxy_emit,
    .config_int = proxy_config_int,
    .config_bool = proxy_config_bool,
    .config_string = proxy_config_string,
    .register_command = proxy_register_command,
    .unregister_command = proxy_unregister_command,
    .current_buffer = proxy_current_buffer,
    .find_buffer = proxy_find_buffer,
    .buffer_contents = proxy_buffer_contents,
    .buffer_filename = proxy_buffer_filename,
    .buffer_name = proxy_buffer_name,
    .buffer_modified = proxy_buffer_modified,
    .buffer_insert = proxy_buffer_insert,
    .buffer_insert_at = proxy_buffer_insert_at,
    .buffer_create = proxy_buffer_create,
    .buffer_switch = proxy_buffer_switch,
    .buffer_clear = proxy_buffer_clear,
    .get_point = proxy_get_point,
    .set_point = proxy_set_point,
    .get_line_count = proxy_get_line_count,
    .get_word_at_point = proxy_get_word_at_point,
    .get_current_line = proxy_get_current_line,
    .get_line_at = proxy_get_line_at,
    .current_window = proxy_current_window,
    .window_count = proxy_window_count,
    .window_set_wrap_col = proxy_window_set_wrap_col,
    .window_at_row = proxy_window_at_row,
    .window_switch = proxy_window_switch,
    .screen_to_buffer_pos = proxy_screen_to_buffer_pos,
    .set_mark = proxy_set_mark,
    .scroll_up = proxy_scroll_up,
    .scroll_down = proxy_scroll_down,
    .message = proxy_message,
    .vmessage = proxy_vmessage,
    .prompt = proxy_prompt,
    .prompt_yn = proxy_prompt_yn,
    .update_display = proxy_update_display,
    .find_file_line = proxy_find_file_line,
    .shell_command = proxy_shell_command,
    .alloc = proxy_alloc,
    .free = proxy_free,
    .strdup = proxy_strdup,
    .log_info = proxy_log_info,
    .log_warn = proxy_log_warn,
    .log_error = proxy_log_error,
    .log_debug = proxy_log_debug,
    .syntax_register_lexer = proxy_syntax_register_lexer,
    .syntax_unregister_lexer = proxy_syntax_unregister_lexer,
    .syntax_add_token = proxy_syntax_add_token,
    .syntax_invalidate_buffer = proxy_syntax_invalidate_buffer,
    .modeline_register = proxy_modeline_register,
    .modeline_unregister = proxy_modeline_unregister,
    .modeline_refresh = proxy_modeline_refresh,
    .struct_size = sizeof(struct uemacs_api),
    .get_function = proxy_get_function,
};

/* Implementation of proxy_get_function */
static generic_fn_t proxy_get_function(const char *name)
{
    if (!name) return NULL;

    #define ENTRY(n, fn) if (strcmp(name, #n) == 0) return (generic_fn_t)(fn)

    ENTRY(on, proxy_on);
    ENTRY(off, proxy_off);
    ENTRY(emit, proxy_emit);
    ENTRY(config_int, proxy_config_int);
    ENTRY(config_bool, proxy_config_bool);
    ENTRY(config_string, proxy_config_string);
    ENTRY(register_command, proxy_register_command);
    ENTRY(unregister_command, proxy_unregister_command);
    ENTRY(current_buffer, proxy_current_buffer);
    ENTRY(find_buffer, proxy_find_buffer);
    ENTRY(buffer_contents, proxy_buffer_contents);
    ENTRY(buffer_filename, proxy_buffer_filename);
    ENTRY(buffer_name, proxy_buffer_name);
    ENTRY(buffer_modified, proxy_buffer_modified);
    ENTRY(buffer_insert, proxy_buffer_insert);
    ENTRY(buffer_insert_at, proxy_buffer_insert_at);
    ENTRY(buffer_create, proxy_buffer_create);
    ENTRY(buffer_switch, proxy_buffer_switch);
    ENTRY(buffer_clear, proxy_buffer_clear);
    ENTRY(get_point, proxy_get_point);
    ENTRY(set_point, proxy_set_point);
    ENTRY(get_line_count, proxy_get_line_count);
    ENTRY(get_word_at_point, proxy_get_word_at_point);
    ENTRY(get_current_line, proxy_get_current_line);
    ENTRY(get_line_at, proxy_get_line_at);
    ENTRY(current_window, proxy_current_window);
    ENTRY(window_count, proxy_window_count);
    ENTRY(window_set_wrap_col, proxy_window_set_wrap_col);
    ENTRY(window_at_row, proxy_window_at_row);
    ENTRY(window_switch, proxy_window_switch);
    ENTRY(screen_to_buffer_pos, proxy_screen_to_buffer_pos);
    ENTRY(set_mark, proxy_set_mark);
    ENTRY(scroll_up, proxy_scroll_up);
    ENTRY(scroll_down, proxy_scroll_down);
    ENTRY(message, proxy_message);
    ENTRY(vmessage, proxy_vmessage);
    ENTRY(prompt, proxy_prompt);
    ENTRY(prompt_yn, proxy_prompt_yn);
    ENTRY(update_display, proxy_update_display);
    ENTRY(find_file_line, proxy_find_file_line);
    ENTRY(shell_command, proxy_shell_command);
    ENTRY(alloc, proxy_alloc);
    ENTRY(free, proxy_free);
    ENTRY(strdup, proxy_strdup);
    ENTRY(log_info, proxy_log_info);
    ENTRY(log_warn, proxy_log_warn);
    ENTRY(log_error, proxy_log_error);
    ENTRY(log_debug, proxy_log_debug);
    ENTRY(syntax_register_lexer, proxy_syntax_register_lexer);
    ENTRY(syntax_unregister_lexer, proxy_syntax_unregister_lexer);
    ENTRY(syntax_add_token, proxy_syntax_add_token);
    ENTRY(syntax_invalidate_buffer, proxy_syntax_invalidate_buffer);
    ENTRY(modeline_register, proxy_modeline_register);
    ENTRY(modeline_unregister, proxy_modeline_unregister);
    ENTRY(modeline_refresh, proxy_modeline_refresh);

    #undef ENTRY
    return NULL;
}

/* =========================================================================
 * Command Handler
 * ========================================================================= */

static void handle_command(ext_ipc_slot_t *slot)
{
    ext_ipc_cmd_invoke_t *req = (ext_ipc_cmd_invoke_t *)slot->payload;

    ipc_log("ext_runner[%d]: Executing command: %s", getpid(), req->name);

    uemacs_cmd_fn fn = find_local_command(req->name);
    int result = fn ? fn(req->f, req->n) : -1;

    ipc_log("ext_runner[%d]: Command complete: %s = %d", getpid(), req->name, result);

    /* Send completion message */
    ext_ipc_ring_t *ring = &g_ipc->shm->to_editor;
    int resp_slot = ext_ipc_find_empty_slot(ring);
    if (resp_slot >= 0) {
        ext_ipc_slot_t *rslot = &ring->slots[resp_slot];
        ext_ipc_cmd_response_t resp = { .result = result };
        ext_ipc_slot_write(rslot, EXT_MSG_CMD_COMPLETE, &resp, sizeof(resp));
    }

    /* Mark command slot as complete (editor will release it) */
    ext_ipc_slot_complete(slot, result, NULL, 0);
}

/* =========================================================================
 * Main Loop - Spin-based
 * ========================================================================= */

static void extension_main_loop(void)
{
    ext_ipc_ring_t *cmd_ring = &g_ipc->shm->to_ext;

    /* Set up poll for death pipe - when parent dies, we get POLLHUP */
    struct pollfd pfd = {
        .fd = g_death_fd,
        .events = POLLIN,  /* Request POLLIN so POLLHUP is reliably reported */
    };

    /* Verify death_fd is valid before entering loop */
    if (g_death_fd >= 0) {
        int flags = fcntl(g_death_fd, F_GETFD);
        if (flags < 0) {
            /* Death fd is invalid - exit immediately */
            return;
        }
    }

    while (g_running && !atomic_load(&g_ipc->shm->shutdown)) {
        /* Check death pipe FIRST - before scanning slots
         * This ensures we detect parent death even if shared memory is corrupted */
        if (g_death_fd >= 0) {
            int ret = poll(&pfd, 1, 0);  /* Non-blocking check */
            if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
                /* Parent died or fd invalid - exit immediately */
                g_running = 0;
                break;
            }
        }

        /* Scan for pending commands */
        int processed = 0;
        for (int s = 0; s < EXT_IPC_RING_SLOTS && processed < 8; s++) {
            ext_ipc_slot_t *slot = &cmd_ring->slots[s];
            if (atomic_load_explicit(&slot->state, memory_order_acquire) == EXT_SLOT_PENDING) {
                processed++;
                if (slot->msg_type == EXT_MSG_INVOKE_CMD) {
                    handle_command(slot);
                } else if (slot->msg_type == EXT_MSG_SHUTDOWN) {
                    g_running = 0;
                    ext_ipc_slot_complete(slot, 0, NULL, 0);
                    break;
                } else if (slot->msg_type == EXT_MSG_EVENT_EMIT) {
                    /* Dispatch to local handlers */
                    const char *event_name = (const char *)slot->payload;
                    uemacs_event_t event = {
                        .name = event_name,
                        .data = NULL,
                        .data_size = 0,
                        .consumed = false,
                    };
                    for (int i = 0; i < MAX_LOCAL_HANDLERS && !event.consumed; i++) {
                        if (g_local_handlers[i].active &&
                            strcmp(g_local_handlers[i].event, event_name) == 0) {
                            event.consumed = g_local_handlers[i].handler(
                                &event, g_local_handlers[i].user_data);
                        }
                    }
                    ext_ipc_slot_complete(slot, event.consumed ? 1 : 0, NULL, 0);
                } else {
                    ext_ipc_slot_complete(slot, -1, NULL, 0);
                }
            }
        }

        /* Sleep/poll with timeout - ensures we don't spin at 100% CPU */
        if (g_death_fd >= 0) {
            int ret = poll(&pfd, 1, 10);  /* 10ms timeout */
            if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
                /* Parent died - exit immediately */
                g_running = 0;
                break;
            }
        } else {
            /* Fallback: no death pipe, just sleep */
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000 };  /* 10ms */
            nanosleep(&ts, NULL);
        }
    }
}

/* =========================================================================
 * Main
 * ========================================================================= */

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s --memfd N --ext PATH --deathfd N\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --memfd N    Shared memory file descriptor\n");
    fprintf(stderr, "  --ext PATH   Path to extension .so\n");
    fprintf(stderr, "  --deathfd N  Death pipe fd (exits when parent dies)\n");
}

int main(int argc, char **argv)
{
    int memfd = -1;
    const char *ext_path = NULL;

    static struct option long_options[] = {
        {"memfd",   required_argument, 0, 'm'},
        {"ext",     required_argument, 0, 'e'},
        {"deathfd", required_argument, 0, 'd'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:e:d:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'm': memfd = safe_atoi(optarg, -1); break;
        case 'e': ext_path = optarg; break;
        case 'd': g_death_fd = safe_atoi(optarg, -1); break;
        case 'h':
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (memfd < 0 || !ext_path) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Signal handlers - use sigaction for defined behavior */
    struct sigaction sa = {
        .sa_handler = signal_handler,
        .sa_flags = SA_RESTART
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /* Attach to IPC channel */
    g_ipc = ext_ipc_attach(memfd);
    if (!g_ipc) {
        /* Can't use ipc_log yet - IPC not attached */
        return 1;
    }
    ipc_log("ext_runner[%d]: IPC attached (magic=0x%x)", getpid(), g_ipc->shm->magic);

    /* Load extension .so */
    g_ext_handle = dlopen(ext_path, RTLD_NOW | RTLD_LOCAL);
    if (!g_ext_handle) {
        ipc_log("ext_runner[%d]: dlopen failed: %s", getpid(), dlerror());
        ext_ipc_destroy(g_ipc);
        return 1;
    }
    ipc_log("ext_runner[%d]: Extension loaded", getpid());

    /* Get extension entry point */
    typedef struct uemacs_extension *(*entry_fn)(void);
    entry_fn get_extension = (entry_fn)dlsym(g_ext_handle, "uemacs_extension_entry");
    if (!get_extension) {
        ipc_log("ext_runner[%d]: No entry point", getpid());
        dlclose(g_ext_handle);
        ext_ipc_destroy(g_ipc);
        return 1;
    }

    struct uemacs_extension *ext = get_extension();
    if (!ext) {
        ipc_log("ext_runner[%d]: Entry returned NULL", getpid());
        dlclose(g_ext_handle);
        ext_ipc_destroy(g_ipc);
        return 1;
    }

    /* Initialize extension (may register commands via proxy_api) */
    if (ext->init) {
        ipc_log("ext_runner[%d]: Calling init", getpid());
        ext->init(&proxy_api);
        ipc_log("ext_runner[%d]: Init complete, %d commands registered",
                getpid(), g_num_commands);
    }

    /* Signal ready to editor */
    atomic_store_explicit(&g_ipc->shm->ext_ready, 1, memory_order_release);
    ipc_log("ext_runner[%d]: Signaled READY", getpid());

    /* Enter main loop */
    extension_main_loop();

    ipc_log("ext_runner[%d]: Exiting main loop", getpid());

    /* Cleanup */
    ipc_log("ext_runner[%d]: Calling ext cleanup", getpid());
    if (ext->cleanup) {
        ext->cleanup();
    }

    ipc_log("ext_runner[%d]: Calling dlclose", getpid());
    dlclose(g_ext_handle);

    ipc_log("ext_runner[%d]: Calling ipc_destroy", getpid());
    ext_ipc_destroy(g_ipc);

    /* Close death pipe */
    if (g_death_fd >= 0) close(g_death_fd);

    /* Note: Can't log after ipc_destroy - IPC channel is gone */

    return 0;
}

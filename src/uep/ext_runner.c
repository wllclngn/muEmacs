/*
 * ext_runner.c - Out-of-Process Extension Runner
 *
 * Standalone executable spawned by ext_host for extensions requiring isolation.
 * Loads extension .so and proxies API calls back to editor via IPC.
 *
 * Usage: uemacs-ext-runner --memfd N --evreq N --evresp N --size N --ext PATH
 *
 * C23 compliant
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uep/ext_ipc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"

/* =========================================================================
 * Global state
 * ========================================================================= */

static ext_ipc_channel_t *g_ipc = NULL;
static void *g_ext_handle = NULL;
static volatile sig_atomic_t g_running = 1;

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
 * Proxy API Implementation
 *
 * Each function sends an IPC message to the editor and waits for response.
 * ========================================================================= */

/*
 * Event registration - local + notify editor
 */
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

    /* Notify editor - use ext_ipc_call_editor for Extension->Editor RPC */
    ext_ipc_event_sub_t req = { .priority = priority };
    strncpy(req.event_name, event, sizeof(req.event_name) - 1);

    return ext_ipc_call_editor(g_ipc, EXT_MSG_EVENT_SUBSCRIBE, &req, sizeof(req), NULL, NULL);
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

    /* Notify editor - use ext_ipc_call_editor for Extension->Editor RPC */
    ext_ipc_event_sub_t req = {0};
    strncpy(req.event_name, event, sizeof(req.event_name) - 1);

    return ext_ipc_call_editor(g_ipc, EXT_MSG_EVENT_UNSUBSCRIBE, &req, sizeof(req), NULL, NULL);
}

static bool proxy_emit(const char *event, void *data)
{
    /* For extension-to-extension events, just send to editor to dispatch */
    (void)data;  /* TODO: serialize data */
    ext_ipc_notify(g_ipc, EXT_MSG_EVENT_EMIT, event, strlen(event) + 1);
    return false;
}

/*
 * Configuration access
 */
static int proxy_config_int(const char *ext_name, const char *key, int default_val)
{
    ext_ipc_config_req_t req = { .int_default = default_val };
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);

    int32_t result;
    uint32_t resp_len = sizeof(result);
    int ipc_result = ext_ipc_call_editor(g_ipc, EXT_MSG_CONFIG_INT, &req, sizeof(req),
                                         &result, &resp_len);
    if (ipc_result < 0) return default_val;
    return result;
}

static bool proxy_config_bool(const char *ext_name, const char *key, bool default_val)
{
    ext_ipc_config_req_t req = { .bool_default = default_val };
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);

    int32_t result;
    uint32_t resp_len = sizeof(result);
    int ipc_result = ext_ipc_call_editor(g_ipc, EXT_MSG_CONFIG_BOOL, &req, sizeof(req),
                                         &result, &resp_len);
    if (ipc_result < 0) return default_val;
    return result != 0;
}

static const char *proxy_config_string(const char *ext_name, const char *key, const char *default_val)
{
    static char response_buf[256];

    ext_ipc_config_req_t req = {0};
    strncpy(req.ext_name, ext_name, sizeof(req.ext_name) - 1);
    strncpy(req.key, key, sizeof(req.key) - 1);
    if (default_val) {
        strncpy(req.str_default, default_val, sizeof(req.str_default) - 1);
    }

    uint32_t resp_len = sizeof(response_buf);
    int ipc_result = ext_ipc_call_editor(g_ipc, EXT_MSG_CONFIG_STRING, &req, sizeof(req),
                                         response_buf, &resp_len);
    if (ipc_result < 0) return default_val;
    return response_buf;
}

/*
 * Command registration
 */
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

    /* Notify editor - use ext_ipc_call_editor for Extension->Editor RPC */
    ext_ipc_cmd_register_t req;
    strncpy(req.name, name, sizeof(req.name) - 1);
    req.name[sizeof(req.name) - 1] = '\0';

    return ext_ipc_call_editor(g_ipc, EXT_MSG_REGISTER_CMD, &req, sizeof(req), NULL, NULL);
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

    /* Notify editor - use ext_ipc_call_editor for Extension->Editor RPC */
    ext_ipc_cmd_register_t req;
    strncpy(req.name, name, sizeof(req.name) - 1);

    return ext_ipc_call_editor(g_ipc, EXT_MSG_UNREGISTER_CMD, &req, sizeof(req), NULL, NULL);
}

/*
 * Buffer operations - all use ext_ipc_call_editor for Extension->Editor RPC
 */
static struct buffer *proxy_current_buffer(void)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    ext_ipc_call_editor(g_ipc, EXT_MSG_CURRENT_BUFFER, NULL, 0, &handle, &resp_len);
    return (struct buffer *)handle;
}

static struct buffer *proxy_find_buffer(const char *name)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    ext_ipc_call_editor(g_ipc, EXT_MSG_FIND_BUFFER, name, strlen(name) + 1, &handle, &resp_len);
    return (struct buffer *)handle;
}

static char *proxy_buffer_contents(struct buffer *bp, size_t *len)
{
    static char *contents_buf = NULL;
    static size_t contents_size = 0;

    /* Ensure we have a buffer */
    if (!contents_buf) {
        contents_size = 64 * 1024;  /* 64KB initial */
        contents_buf = malloc(contents_size);
    }

    uint32_t resp_len = contents_size;
    uintptr_t handle = (uintptr_t)bp;
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_CONTENTS,
                                     &handle, sizeof(handle),
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
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_FILENAME,
                                     &handle, sizeof(handle),
                                     filename_buf, &resp_len);
    if (result < 0) return NULL;
    return filename_buf;
}

static const char *proxy_buffer_name(struct buffer *bp)
{
    static char name_buf[256];
    uint32_t resp_len = sizeof(name_buf);
    uintptr_t handle = (uintptr_t)bp;
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_NAME,
                                     &handle, sizeof(handle),
                                     name_buf, &resp_len);
    if (result < 0) return NULL;
    return name_buf;
}

static bool proxy_buffer_modified(struct buffer *bp)
{
    int32_t result = 0;
    uint32_t resp_len = sizeof(result);
    uintptr_t handle = (uintptr_t)bp;
    ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_MODIFIED, &handle, sizeof(handle), &result, &resp_len);
    return result != 0;
}

static int proxy_buffer_insert(const char *text, size_t len)
{
    return ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_INSERT, text, len, NULL, NULL);
}

static int proxy_buffer_insert_at(struct buffer *bp, int line, int col,
                                  const char *text, size_t len)
{
    /* Pack request: handle + line + col + text */
    size_t req_size = sizeof(uintptr_t) + sizeof(int32_t) * 2 + len;
    char *req = malloc(req_size);
    if (!req) return -1;

    char *p = req;
    *(uintptr_t *)p = (uintptr_t)bp; p += sizeof(uintptr_t);
    *(int32_t *)p = line; p += sizeof(int32_t);
    *(int32_t *)p = col; p += sizeof(int32_t);
    memcpy(p, text, len);

    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_INSERT, req, req_size, NULL, NULL);
    free(req);
    return result;
}

static struct buffer *proxy_buffer_create(const char *name)
{
    uintptr_t handle = 0;
    uint32_t resp_len = sizeof(handle);
    ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_CREATE, name, strlen(name) + 1, &handle, &resp_len);
    return (struct buffer *)handle;
}

static int proxy_buffer_switch(struct buffer *bp)
{
    uintptr_t handle = (uintptr_t)bp;
    return ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_SWITCH, &handle, sizeof(handle), NULL, NULL);
}

static int proxy_buffer_clear(struct buffer *bp)
{
    uintptr_t handle = (uintptr_t)bp;
    return ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_CLEAR, &handle, sizeof(handle), NULL, NULL);
}

/*
 * Cursor/point operations - all use ext_ipc_call_editor for Extension->Editor RPC
 */
static void proxy_get_point(int *line, int *col)
{
    ext_ipc_point_t point = {0};
    uint32_t resp_len = sizeof(point);
    ext_ipc_call_editor(g_ipc, EXT_MSG_GET_POINT, NULL, 0, &point, &resp_len);
    if (line) *line = point.line;
    if (col) *col = point.col;
}

static void proxy_set_point(int line, int col)
{
    ext_ipc_point_t point = { .line = line, .col = col };
    ext_ipc_call_editor(g_ipc, EXT_MSG_SET_POINT, &point, sizeof(point), NULL, NULL);
}

static int proxy_get_line_count(struct buffer *bp)
{
    int32_t count = 0;
    uint32_t resp_len = sizeof(count);
    uintptr_t handle = (uintptr_t)bp;
    ext_ipc_call_editor(g_ipc, EXT_MSG_BUFFER_CONTENTS, &handle, sizeof(handle), &count, &resp_len);
    return count;
}

static char *proxy_get_word_at_point(void)
{
    static char word_buf[256];
    uint32_t resp_len = sizeof(word_buf);
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_GET_WORD, NULL, 0, word_buf, &resp_len);
    if (result < 0) return NULL;
    return word_buf;
}

static char *proxy_get_current_line(void)
{
    static char line_buf[4096];
    uint32_t resp_len = sizeof(line_buf);
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_GET_LINE, NULL, 0, line_buf, &resp_len);
    if (result < 0) return NULL;
    return line_buf;
}

/*
 * Window operations - return NULL/0 for out-of-process (windows are editor-local)
 */
static struct window *proxy_current_window(void) { return NULL; }
static int proxy_window_count(void) { return 1; }
static int proxy_window_set_wrap_col(struct window *wp, int col) { (void)wp; (void)col; return 0; }
static struct window *proxy_window_at_row(int screen_row) { (void)screen_row; return NULL; }
static int proxy_window_switch(struct window *wp) { (void)wp; return 0; }

/*
 * Screen/cursor helpers - not applicable for out-of-process
 */
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

/*
 * User interface
 */
static void proxy_message(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_message_t msg;
    strncpy(msg.text, buf, sizeof(msg.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_MESSAGE, &msg, sizeof(msg));
}

static void proxy_vmessage(const char *fmt, va_list ap)
{
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);

    ext_ipc_message_t msg;
    strncpy(msg.text, buf, sizeof(msg.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_MESSAGE, &msg, sizeof(msg));
}

static int proxy_prompt(const char *prompt_text, char *buf, size_t buflen)
{
    ext_ipc_prompt_req_t req;
    strncpy(req.prompt, prompt_text, sizeof(req.prompt) - 1);

    ext_ipc_prompt_resp_t resp;
    uint32_t resp_len = sizeof(resp);
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_PROMPT, &req, sizeof(req), &resp, &resp_len);

    if (result < 0 || resp.cancelled) {
        return -1;
    }

    strncpy(buf, resp.response, buflen - 1);
    buf[buflen - 1] = '\0';
    return 0;
}

static int proxy_prompt_yn(const char *prompt_text)
{
    ext_ipc_prompt_req_t req;
    strncpy(req.prompt, prompt_text, sizeof(req.prompt) - 1);

    int32_t result = 0;
    uint32_t resp_len = sizeof(result);
    ext_ipc_call_editor(g_ipc, EXT_MSG_PROMPT_YN, &req, sizeof(req), &result, &resp_len);
    return result;
}

static void proxy_update_display(void)
{
    /* No-op for out-of-process - editor handles display */
}

/*
 * File operations - use ext_ipc_call_editor for Extension->Editor RPC
 */
static int proxy_find_file_line(const char *path, int line)
{
    size_t path_len = strlen(path) + 1;
    size_t req_size = path_len + sizeof(int32_t);
    char *req = malloc(req_size);
    if (!req) return -1;

    memcpy(req, path, path_len);
    *(int32_t *)(req + path_len) = line;

    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_FIND_FILE, req, req_size, NULL, NULL);
    free(req);
    return result;
}

/*
 * Shell integration - use ext_ipc_call_editor for Extension->Editor RPC
 */
static int proxy_shell_command(const char *cmd, char **output, size_t *len)
{
    static char output_buf[64 * 1024];

    uint32_t resp_len = sizeof(output_buf);
    int result = ext_ipc_call_editor(g_ipc, EXT_MSG_SHELL_COMMAND,
                                     cmd, strlen(cmd) + 1,
                                     output_buf, &resp_len);

    if (output) *output = (result >= 0) ? output_buf : NULL;
    if (len) *len = (result >= 0) ? resp_len : 0;
    return result;
}

/*
 * Memory helpers - use standard allocators
 */
static void *proxy_alloc(size_t size) { return malloc(size); }
static void proxy_free(void *ptr) { free(ptr); }
static char *proxy_strdup(const char *s) { return s ? strdup(s) : NULL; }

/*
 * Logging
 */
static void proxy_log_info(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_log_t log = {0};
    strncpy(log.text, buf, sizeof(log.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_LOG_INFO, &log, sizeof(log));
}

static void proxy_log_warn(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_log_t log = {0};
    strncpy(log.text, buf, sizeof(log.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_LOG_WARN, &log, sizeof(log));
}

static void proxy_log_error(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_log_t log = {0};
    strncpy(log.text, buf, sizeof(log.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_LOG_ERROR, &log, sizeof(log));
}

static void proxy_log_debug(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    ext_ipc_log_t log = {0};
    strncpy(log.text, buf, sizeof(log.text) - 1);
    ext_ipc_notify(g_ipc, EXT_MSG_LOG_DEBUG, &log, sizeof(log));
}

/*
 * Syntax highlighting - minimal support for out-of-process
 */
static int proxy_syntax_register_lexer(const char *name, const char **patterns,
                                       uemacs_syntax_lex_fn lex_fn, void *user_data)
{
    (void)name; (void)patterns; (void)lex_fn; (void)user_data;
    /* TODO: Syntax lexers require tight integration; may not work OOP */
    return -1;
}

static int proxy_syntax_unregister_lexer(const char *name)
{
    (void)name;
    return -1;
}

static int proxy_syntax_add_token(uemacs_line_tokens_t *tokens, int end_col, int face)
{
    (void)tokens; (void)end_col; (void)face;
    return -1;
}

static void proxy_syntax_invalidate_buffer(struct buffer *bp)
{
    (void)bp;
}

/*
 * Modeline - notify editor (use ext_ipc_call_editor for Extension->Editor RPC)
 */
static int proxy_modeline_register(const char *name, uemacs_modeline_fn format_fn,
                                   void *user_data, int urgency)
{
    (void)format_fn; (void)user_data;
    /* Modeline callbacks can't work OOP - they need synchronous calls */
    /* TODO: Could store state and send updates periodically */
    struct {
        char name[64];
        int urgency;
    } req;
    strncpy(req.name, name, sizeof(req.name) - 1);
    req.urgency = urgency;
    return ext_ipc_call_editor(g_ipc, EXT_MSG_MODELINE_REGISTER, &req, sizeof(req), NULL, NULL);
}

static int proxy_modeline_unregister(const char *name)
{
    return ext_ipc_call_editor(g_ipc, EXT_MSG_MODELINE_REGISTER, name, strlen(name) + 1, NULL, NULL);
}

static void proxy_modeline_refresh(void)
{
    ext_ipc_notify(g_ipc, EXT_MSG_MODELINE_REFRESH, NULL, 0);
}

/* =========================================================================
 * Proxy API struct
 * ========================================================================= */

static struct uemacs_api proxy_api = {
    .api_version = UEMACS_API_VERSION,

    /* Event system */
    .on = proxy_on,
    .off = proxy_off,
    .emit = proxy_emit,

    /* Configuration */
    .config_int = proxy_config_int,
    .config_bool = proxy_config_bool,
    .config_string = proxy_config_string,

    /* Commands */
    .register_command = proxy_register_command,
    .unregister_command = proxy_unregister_command,

    /* Buffer operations */
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

    /* Cursor/point */
    .get_point = proxy_get_point,
    .set_point = proxy_set_point,
    .get_line_count = proxy_get_line_count,
    .get_word_at_point = proxy_get_word_at_point,
    .get_current_line = proxy_get_current_line,

    /* Window operations */
    .current_window = proxy_current_window,
    .window_count = proxy_window_count,
    .window_set_wrap_col = proxy_window_set_wrap_col,
    .window_at_row = proxy_window_at_row,
    .window_switch = proxy_window_switch,

    /* Screen/cursor helpers */
    .screen_to_buffer_pos = proxy_screen_to_buffer_pos,
    .set_mark = proxy_set_mark,
    .scroll_up = proxy_scroll_up,
    .scroll_down = proxy_scroll_down,

    /* UI */
    .message = proxy_message,
    .vmessage = proxy_vmessage,
    .prompt = proxy_prompt,
    .prompt_yn = proxy_prompt_yn,
    .update_display = proxy_update_display,

    /* File operations */
    .find_file_line = proxy_find_file_line,

    /* Shell */
    .shell_command = proxy_shell_command,

    /* Memory */
    .alloc = proxy_alloc,
    .free = proxy_free,
    .strdup = proxy_strdup,

    /* Logging */
    .log_info = proxy_log_info,
    .log_warn = proxy_log_warn,
    .log_error = proxy_log_error,
    .log_debug = proxy_log_debug,

    /* Syntax */
    .syntax_register_lexer = proxy_syntax_register_lexer,
    .syntax_unregister_lexer = proxy_syntax_unregister_lexer,
    .syntax_add_token = proxy_syntax_add_token,
    .syntax_invalidate_buffer = proxy_syntax_invalidate_buffer,

    /* Modeline */
    .modeline_register = proxy_modeline_register,
    .modeline_unregister = proxy_modeline_unregister,
    .modeline_refresh = proxy_modeline_refresh,
};

/* =========================================================================
 * Message loop
 * ========================================================================= */

static void handle_message(ext_ipc_msg_t *msg)
{
    switch (msg->msg_type) {
    case EXT_MSG_INVOKE_CMD: {
        ext_ipc_cmd_invoke_t *req = (ext_ipc_cmd_invoke_t *)msg->payload;
        uemacs_cmd_fn fn = find_local_command(req->name);
        int result = fn ? fn(req->f, req->n) : -1;
        ext_ipc_respond(g_ipc, result, NULL, 0);
        break;
    }

    case EXT_MSG_EVENT_EMIT: {
        /* Dispatch to local handlers */
        const char *event_name = (const char *)msg->payload;
        uemacs_event_t event = {
            .name = event_name,
            .data = NULL,  /* TODO: deserialize event data */
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

        ext_ipc_respond(g_ipc, event.consumed ? 1 : 0, NULL, 0);
        break;
    }

    case EXT_MSG_SHUTDOWN:
        g_running = 0;
        ext_ipc_respond(g_ipc, 0, NULL, 0);
        break;

    default:
        /* Unknown message */
        ext_ipc_respond(g_ipc, -1, NULL, 0);
        break;
    }
}

/* =========================================================================
 * Main
 * ========================================================================= */

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s --memfd N --evreq N --evresp N --size N --ext PATH\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --memfd N   Shared memory file descriptor\n");
    fprintf(stderr, "  --evreq N   Request eventfd (editor -> extension)\n");
    fprintf(stderr, "  --evresp N  Response eventfd (extension -> editor)\n");
    fprintf(stderr, "  --size N    Shared memory size\n");
    fprintf(stderr, "  --ext PATH  Path to extension .so\n");
}

int main(int argc, char **argv)
{
    int memfd = -1, evreq = -1, evresp = -1;
    size_t shm_size = EXT_IPC_DEFAULT_SHM_SIZE;
    const char *ext_path = NULL;

    static struct option long_options[] = {
        {"memfd",  required_argument, 0, 'm'},
        {"evreq",  required_argument, 0, 'r'},
        {"evresp", required_argument, 0, 's'},
        {"size",   required_argument, 0, 'z'},
        {"ext",    required_argument, 0, 'e'},
        {"help",   no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "m:r:s:z:e:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'm': memfd = atoi(optarg); break;
        case 'r': evreq = atoi(optarg); break;
        case 's': evresp = atoi(optarg); break;
        case 'z': shm_size = strtoul(optarg, NULL, 10); break;
        case 'e': ext_path = optarg; break;
        case 'h':
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (memfd < 0 || evreq < 0 || evresp < 0 || !ext_path) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Log to dedicated file since we're a separate process */
    FILE *logf = fopen("/tmp/uemacs_runner.log", "a");
    if (logf) {
        fprintf(logf, "[%d] Starting: memfd=%d evreq=%d evresp=%d size=%zu ext=%s\n",
                getpid(), memfd, evreq, evresp, shm_size, ext_path);
        fflush(logf);
    }

    /* Set up signal handlers */
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    /* Attach to IPC channel */
    g_ipc = ext_ipc_attach(memfd, evreq, evresp, shm_size);
    if (!g_ipc) {
        if (logf) fprintf(logf, "[%d] Failed to attach IPC (memfd=%d)\n", getpid(), memfd);
        if (logf) fclose(logf);
        return 1;
    }
    if (logf) { fprintf(logf, "[%d] IPC attached\n", getpid()); fflush(logf); }

    /* Load extension .so */
    g_ext_handle = dlopen(ext_path, RTLD_NOW | RTLD_LOCAL);
    if (!g_ext_handle) {
        if (logf) fprintf(logf, "[%d] dlopen failed: %s\n", getpid(), dlerror());
        if (logf) fclose(logf);
        ext_ipc_destroy(g_ipc);
        return 1;
    }
    if (logf) { fprintf(logf, "[%d] Extension loaded\n", getpid()); fflush(logf); }

    /* Get extension entry point */
    typedef struct uemacs_extension *(*entry_fn)(void);
    entry_fn get_extension = (entry_fn)dlsym(g_ext_handle, "uemacs_extension_entry");
    if (!get_extension) {
        fprintf(stderr, "Extension has no uemacs_extension_entry symbol\n");
        dlclose(g_ext_handle);
        ext_ipc_destroy(g_ipc);
        return 1;
    }

    struct uemacs_extension *ext = get_extension();
    if (!ext) {
        fprintf(stderr, "uemacs_extension_entry returned NULL\n");
        dlclose(g_ext_handle);
        ext_ipc_destroy(g_ipc);
        return 1;
    }

    /* Send READY message and wait for acknowledgment before init.
     * This prevents a race where init() overwrites shared memory before
     * the parent reads READY. */
    if (logf) { fprintf(logf, "[%d] Sending READY\n", getpid()); fflush(logf); }
    int ready_result = ext_ipc_call_editor(g_ipc, EXT_MSG_READY, NULL, 0, NULL, NULL);
    if (logf) { fprintf(logf, "[%d] READY acknowledged (result=%d)\n", getpid(), ready_result); fflush(logf); }

    /* Initialize extension with proxy API (may send registration messages) */
    if (ext->init) {
        if (logf) { fprintf(logf, "[%d] Calling init\n", getpid()); fflush(logf); }
        ext->init(&proxy_api);
        if (logf) { fprintf(logf, "[%d] Init complete\n", getpid()); fflush(logf); }
    }

    if (logf) fclose(logf);

    /* Message loop */
    while (g_running) {
        ext_ipc_msg_t *msg;
        int result = ext_ipc_recv(g_ipc, &msg, 1000);  /* 1 second timeout */

        if (result == EXT_IPC_OK) {
            handle_message(msg);
        } else if (result == EXT_IPC_ERR_CLOSED) {
            /* Editor closed connection */
            break;
        }
        /* Timeout is normal, just continue */
    }

    /* Cleanup extension */
    if (ext->cleanup) {
        ext->cleanup();
    }

    dlclose(g_ext_handle);
    ext_ipc_destroy(g_ipc);

    return 0;
}

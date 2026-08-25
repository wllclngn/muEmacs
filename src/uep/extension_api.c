/*
 * extension_api.c - μEmacs Editor API Implementation
 *
 * API Version 3: Generic Event Bus
 * - Replaced 6 static hook arrays with single event bus
 * - Removed ~350 lines of redundant on/off/fire functions
 * - Added configuration access API
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
#include <pthread.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"
#include "internal/event_bus.h"
#include "internal/memory.h"
#include "internal/line.h"
#include "internal/utf8.h"
#include "../util/display_width.h"
#include "internal/syntax.h"
#include "μemacs/keymap.h"
#include "editor_mode.h"
#include "terminal/input_state.h"
#include "terminal/palette.h"
#include "util/logger.h"

/* Dynamic Command Registry */

#define MAX_DYNAMIC_COMMANDS 256

typedef struct {
    char *name;
    uemacs_cmd_fn func;
    bool active;
} dynamic_command_t;

static dynamic_command_t dynamic_commands[MAX_DYNAMIC_COMMANDS];
static int dynamic_command_count = 0;
static pthread_mutex_t dynamic_commands_mutex = PTHREAD_MUTEX_INITIALIZER;

static int api_register_command(const char *name, uemacs_cmd_fn func) {
    if (!name || !func) return -1;

    pthread_mutex_lock(&dynamic_commands_mutex);

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            LOG_WARNF("Extension API: Command already registered: %s", name);
            pthread_mutex_unlock(&dynamic_commands_mutex);
            return -1;
        }
    }

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (!dynamic_commands[i].active) {
            dynamic_commands[i].name = SAFE_STRDUP(name, "dynamic command name");
            if (!dynamic_commands[i].name) {
                pthread_mutex_unlock(&dynamic_commands_mutex);
                return -1;
            }
            dynamic_commands[i].func = func;
            dynamic_commands[i].active = true;
            dynamic_command_count++;
            pthread_mutex_unlock(&dynamic_commands_mutex);
            LOG_INFOF("Extension API: Registered command: %s", name);
            return 0;
        }
    }

    pthread_mutex_unlock(&dynamic_commands_mutex);
    LOG_ERROR("Extension API: Maximum dynamic commands reached");
    return -1;
}

static int api_unregister_command(const char *name) {
    if (!name) return -1;

    pthread_mutex_lock(&dynamic_commands_mutex);

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            free(dynamic_commands[i].name);
            dynamic_commands[i].name = nullptr;
            dynamic_commands[i].func = nullptr;
            dynamic_commands[i].active = false;
            dynamic_command_count--;
            pthread_mutex_unlock(&dynamic_commands_mutex);
            LOG_INFOF("Extension API: Unregistered command: %s", name);
            return 0;
        }
    }

    pthread_mutex_unlock(&dynamic_commands_mutex);
    LOG_WARNF("Extension API: Command not found: %s", name);
    return -1;
}

/* Look up dynamic command by name (called by exec.c) */
uemacs_cmd_fn extension_find_command(const char *name) {
    if (!name) return nullptr;

    pthread_mutex_lock(&dynamic_commands_mutex);

    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active &&
            strcmp(dynamic_commands[i].name, name) == 0) {
            uemacs_cmd_fn func = dynamic_commands[i].func;
            pthread_mutex_unlock(&dynamic_commands_mutex);
            return func;
        }
    }

    pthread_mutex_unlock(&dynamic_commands_mutex);
    return nullptr;
}

/* ABI-Stable Function Registry
 *
 * Maps function names to pointers for dynamic lookup.
 * Extensions using get_function() are immune to struct layout changes. */

/* Forward declarations for all API functions */
static int api_on(const char *event, uemacs_event_fn handler, void *user_data, int priority);
static int api_off(const char *event, uemacs_event_fn handler);
static bool api_emit(const char *event, void *data);
/* Config getters - also used by ext_host for IPC */
int extension_config_get_int(const char *ext_name, const char *key, int default_val);
bool extension_config_get_bool(const char *ext_name, const char *key, bool default_val);
const char *extension_config_get_string(const char *ext_name, const char *key, const char *default_val);
static struct buffer *api_current_buffer(void);
static struct buffer *api_find_buffer(const char *name);
static char *api_buffer_contents(struct buffer *bp, size_t *len);
static const char *api_buffer_filename(struct buffer *bp);
static const char *api_buffer_name(struct buffer *bp);
static bool api_buffer_modified(struct buffer *bp);
static void api_buffer_set_unmodified(struct buffer *bp);
static void api_buffer_set_scratch(struct buffer *bp);
static int api_buffer_insert(const char *text, size_t len);
static int api_buffer_insert_at(struct buffer *bp, int line, int col, const char *text, size_t len);
static struct buffer *api_buffer_create(const char *name);
static int api_buffer_switch(struct buffer *bp);
static int api_buffer_clear(struct buffer *bp);
static struct buffer *api_buffer_first(void);
static struct buffer *api_buffer_next(struct buffer *bp);
static void api_get_point(int *line, int *col);
static void api_set_point(int line, int col);
static int api_get_line_count(struct buffer *bp);
static char *api_get_word_at_point(void);
static char *api_get_current_line(void);
static char *api_get_line_at(struct buffer *bp, int line_num);
static struct window *api_current_window(void);
static int api_window_count(void);
static int api_window_set_wrap_col(struct window *wp, int col);
static struct window *api_window_at_row(int screen_row);
static int api_window_switch(struct window *wp);
static int api_screen_to_buffer_pos(struct window *wp, int screen_row, int screen_col, int *buf_line, int *buf_offset);
static int api_set_mark(void);
static int api_scroll_up(int lines);
static int api_scroll_down(int lines);
static void api_message(const char *fmt, ...);
static void api_vmessage(const char *fmt, va_list ap);
static int api_prompt(const char *prompt, char *buf, size_t buflen);
static int api_prompt_yn(const char *prompt);
static void api_update_display(void);
static int api_find_file_line(const char *path, int line);
static int api_shell_command(const char *cmd, char **output, size_t *len);
static void *api_alloc(size_t size);
static void api_free(void *ptr);
static char *api_strdup(const char *s);
static void api_log_info(const char *fmt, ...);
static void api_log_warn(const char *fmt, ...);
static void api_log_error(const char *fmt, ...);
static void api_log_debug(const char *fmt, ...);
static int api_syntax_register_lexer(const char *name, const char **patterns, uemacs_syntax_lex_fn lex_fn, void *user_data);
static int api_syntax_unregister_lexer(const char *name);
static int api_syntax_add_token(uemacs_line_tokens_t *tokens, int end_col, int face);
static void api_syntax_invalidate_buffer(struct buffer *bp);
static int api_modeline_register(const char *name, uemacs_modeline_fn format_fn, void *user_data, int urgency);
static int api_modeline_unregister(const char *name);
static void api_modeline_refresh(void);
static int api_delete_chars(int n);
static int api_kill_line(void);

/* Keymap API (Phase 2: Extension keymap registration) */
static struct keymap *api_keymap_create(const char *name);
static void api_keymap_destroy(struct keymap *km);
static int api_keymap_bind(struct keymap *km, uint32_t code, uint16_t mods, command_fn cmd);
static int api_keymap_unbind(struct keymap *km, uint32_t code, uint16_t mods);

/* Vim infrastructure accessors */
static struct keymap *api_get_vim_normal_keymap(void);
static struct keymap *api_get_vim_visual_keymap(void);
static void api_set_vim_mode_active(int active);
static int api_get_vim_mode_active(void);
static void *api_get_vim_state(void);

/* Vim state accessors — typed public surface (task #5) */
static uemacs_vim_mode_t api_vim_get_mode(void);
static void api_vim_set_mode(uemacs_vim_mode_t mode);
static int api_vim_get_count(void);
static void api_vim_set_count(int count);
static uemacs_vim_operator_t api_vim_get_pending_op(void);
static int api_vim_get_last_change(char *buf, int max_len);
static bool api_vim_get_mark(char name, int *line_out, int *col_out);
static bool api_vim_set_mark(char name, int line_num, int col);

/* Register + undo-boundary accessors — task #7 */
static int  api_vim_get_register(char name, char *buf, int max_len, bool *linewise_out);
static int  api_vim_set_register(char name, const char *text, int len, bool linewise);
static int  api_clipboard_set(const char *text, int len);
static void api_undo_group_begin(struct buffer *bp);
static void api_undo_group_end(struct buffer *bp);

/* Display setting accessors (for c_linus and similar extensions) */
static void api_set_highlight_line(int enabled);
static void api_set_ruler(int enabled);
static void api_set_syntax_enabled(int enabled);

/* Command lookup */
static fn_t api_find_command(const char *name);

/* Generic function pointer type for ISO C compliant storage.
 * All function pointers can be converted to/from this type. */
typedef void (*generic_fn_t)(void);

/* Function registry entry */
typedef struct {
    const char *name;
    generic_fn_t func;
} api_registry_entry_t;

/* Cast helper for registry initialization */
#define FN(f) ((generic_fn_t)(f))

/* The registry - maps names to function pointers. */
static const api_registry_entry_t api_registry[] = {
    /* Event system */
    {"on", FN(api_on)},
    {"off", FN(api_off)},
    {"emit", FN(api_emit)},

    /* Configuration */
    {"config_int", FN(extension_config_get_int)},
    {"config_bool", FN(extension_config_get_bool)},
    {"config_string", FN(extension_config_get_string)},

    /* Command registration */
    {"register_command", FN(api_register_command)},
    {"unregister_command", FN(api_unregister_command)},

    /* Buffer operations */
    {"current_buffer", FN(api_current_buffer)},
    {"find_buffer", FN(api_find_buffer)},
    {"buffer_contents", FN(api_buffer_contents)},
    {"buffer_filename", FN(api_buffer_filename)},
    {"buffer_name", FN(api_buffer_name)},
    {"buffer_modified", FN(api_buffer_modified)},
    {"buffer_set_unmodified", FN(api_buffer_set_unmodified)},
    {"buffer_set_scratch", FN(api_buffer_set_scratch)},
    {"buffer_insert", FN(api_buffer_insert)},
    {"buffer_insert_at", FN(api_buffer_insert_at)},
    {"buffer_create", FN(api_buffer_create)},
    {"buffer_switch", FN(api_buffer_switch)},
    {"buffer_clear", FN(api_buffer_clear)},
    {"buffer_first", FN(api_buffer_first)},
    {"buffer_next", FN(api_buffer_next)},

    /* Cursor/point */
    {"get_point", FN(api_get_point)},
    {"set_point", FN(api_set_point)},
    {"get_line_count", FN(api_get_line_count)},
    {"get_word_at_point", FN(api_get_word_at_point)},
    {"get_current_line", FN(api_get_current_line)},
    {"get_line_at", FN(api_get_line_at)},

    /* Window operations */
    {"current_window", FN(api_current_window)},
    {"window_count", FN(api_window_count)},
    {"window_set_wrap_col", FN(api_window_set_wrap_col)},
    {"window_at_row", FN(api_window_at_row)},
    {"window_switch", FN(api_window_switch)},

    /* Mouse/cursor helpers */
    {"screen_to_buffer_pos", FN(api_screen_to_buffer_pos)},
    {"set_mark", FN(api_set_mark)},
    {"scroll_up", FN(api_scroll_up)},
    {"scroll_down", FN(api_scroll_down)},

    /* UI */
    {"message", FN(api_message)},
    {"vmessage", FN(api_vmessage)},
    {"prompt", FN(api_prompt)},
    {"prompt_yn", FN(api_prompt_yn)},
    {"update_display", FN(api_update_display)},

    /* File operations */
    {"find_file_line", FN(api_find_file_line)},

    /* Shell */
    {"shell_command", FN(api_shell_command)},

    /* Memory */
    {"alloc", FN(api_alloc)},
    {"free", FN(api_free)},
    {"strdup", FN(api_strdup)},

    /* Logging */
    {"log_info", FN(api_log_info)},
    {"log_warn", FN(api_log_warn)},
    {"log_error", FN(api_log_error)},
    {"log_debug", FN(api_log_debug)},

    /* Syntax highlighting */
    {"syntax_register_lexer", FN(api_syntax_register_lexer)},
    {"syntax_unregister_lexer", FN(api_syntax_unregister_lexer)},
    {"syntax_add_token", FN(api_syntax_add_token)},
    {"syntax_invalidate_buffer", FN(api_syntax_invalidate_buffer)},

    /* Modeline */
    {"modeline_register", FN(api_modeline_register)},
    {"modeline_unregister", FN(api_modeline_unregister)},
    {"modeline_refresh", FN(api_modeline_refresh)},

    /* Text manipulation */
    {"delete_chars", FN(api_delete_chars)},
    {"kill_line", FN(api_kill_line)},

    /* Keymap API */
    {"keymap_create", FN(api_keymap_create)},
    {"keymap_destroy", FN(api_keymap_destroy)},
    {"keymap_bind", FN(api_keymap_bind)},
    {"keymap_unbind", FN(api_keymap_unbind)},

    /* Vim infrastructure */
    {"get_vim_normal_keymap", FN(api_get_vim_normal_keymap)},
    {"get_vim_visual_keymap", FN(api_get_vim_visual_keymap)},
    {"set_vim_mode_active", FN(api_set_vim_mode_active)},
    {"get_vim_mode_active", FN(api_get_vim_mode_active)},
    {"get_vim_state", FN(api_get_vim_state)},

    /* Vim state typed accessors (task #5 — opaque-struct public surface) */
    {"vim_get_mode", FN(api_vim_get_mode)},
    {"vim_set_mode", FN(api_vim_set_mode)},
    {"vim_get_count", FN(api_vim_get_count)},
    {"vim_set_count", FN(api_vim_set_count)},
    {"vim_get_pending_op", FN(api_vim_get_pending_op)},
    {"vim_get_last_change", FN(api_vim_get_last_change)},
    {"vim_get_mark", FN(api_vim_get_mark)},
    {"vim_set_mark", FN(api_vim_set_mark)},

    /* Registers + undo-boundary (task #7) */
    {"vim_get_register", FN(api_vim_get_register)},
    {"vim_set_register", FN(api_vim_set_register)},
    {"clipboard_set", FN(api_clipboard_set)},
    {"undo_group_begin", FN(api_undo_group_begin)},
    {"undo_group_end", FN(api_undo_group_end)},

    /* Display settings */
    {"set_highlight_line", FN(api_set_highlight_line)},
    {"set_ruler", FN(api_set_ruler)},
    {"set_syntax_enabled", FN(api_set_syntax_enabled)},

    /* Command lookup */
    {"find_command", FN(api_find_command)},

    /* Sentinel */
    {nullptr, nullptr}
};

#undef FN

/* Look up function by name - O(n) but only called at extension init.
 * Returns generic function pointer for ISO C compliance. */
static generic_fn_t api_get_function(const char *name) {
    if (!name) return nullptr;

    for (int i = 0; api_registry[i].name != nullptr; i++) {
        if (strcmp(api_registry[i].name, name) == 0) {
            return api_registry[i].func;
        }
    }

    LOG_WARNF("Extension API: Unknown function requested: %s", name);
    return nullptr;
}

/* Generic Event System (Wrappers to event_bus.c) */

static int api_on(const char *event, uemacs_event_fn handler,
                  void *user_data, int priority) {
    return event_bus_on(event, (event_handler_fn)handler, user_data, priority);
}

static int api_off(const char *event, uemacs_event_fn handler) {
    return event_bus_off(event, (event_handler_fn)handler);
}

static bool api_emit(const char *event, void *data) {
    return event_bus_emit(event, data, 0);
}

/* Configuration Storage (populated by settings.c TOML parser) */

/* Simple hash-based config store for extension settings */
#define EXT_CONFIG_HASH_SIZE 127
#define EXT_CONFIG_KEY_MAX   128

typedef enum {
    EXT_CONFIG_INT,
    EXT_CONFIG_BOOL,
    EXT_CONFIG_STRING,
} ext_config_type_t;

typedef struct ext_config_entry {
    char key[EXT_CONFIG_KEY_MAX];  /* "ext_name.key" */
    ext_config_type_t type;
    union {
        int int_val;
        bool bool_val;
        char *str_val;
    };
    struct ext_config_entry *next;
} ext_config_entry_t;

static ext_config_entry_t *config_buckets[EXT_CONFIG_HASH_SIZE];

/* djb2 hash for config keys */
static unsigned config_hash(const char *key) {
    unsigned hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + (unsigned)c;
    }
    return hash % EXT_CONFIG_HASH_SIZE;
}

/* Build full key "ext_name.key" */
static void make_config_key(char *buf, size_t buflen,
                            const char *ext_name, const char *key) {
    snprintf(buf, buflen, "%s.%s", ext_name, key);
}

/* Find config entry */
static ext_config_entry_t *find_config_entry(const char *full_key) {
    unsigned idx = config_hash(full_key);
    ext_config_entry_t *entry = config_buckets[idx];
    while (entry) {
        if (strcmp(entry->key, full_key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return nullptr;
}

/* Create or update config entry */
static ext_config_entry_t *get_or_create_config_entry(const char *full_key) {
    ext_config_entry_t *entry = find_config_entry(full_key);
    if (entry) {
        /* Free old string value if replacing */
        if (entry->type == EXT_CONFIG_STRING && entry->str_val) {
            free(entry->str_val);
            entry->str_val = nullptr;
        }
        return entry;
    }

    /* Create new entry */
    entry = SAFE_ALLOC(ext_config_entry_t, "ext config entry");
    if (!entry) return nullptr;

    snprintf(entry->key, EXT_CONFIG_KEY_MAX, "%s", full_key);

    /* Insert at head of bucket */
    unsigned idx = config_hash(full_key);
    entry->next = config_buckets[idx];
    config_buckets[idx] = entry;

    return entry;
}

/*
 * Public setters - called by settings.c during TOML parsing
 */
void extension_config_set_int(const char *ext_name, const char *key, int value) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = get_or_create_config_entry(full_key);
    if (entry) {
        entry->type = EXT_CONFIG_INT;
        entry->int_val = value;
        LOG_DEBUGF("ext_config: set %s = %d (int)", full_key, value);
    }
}

void extension_config_set_bool(const char *ext_name, const char *key, bool value) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = get_or_create_config_entry(full_key);
    if (entry) {
        entry->type = EXT_CONFIG_BOOL;
        entry->bool_val = value;
        LOG_DEBUGF("ext_config: set %s = %s (bool)", full_key, value ? "true" : "false");
    }
}

void extension_config_set_string(const char *ext_name, const char *key, const char *value) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = get_or_create_config_entry(full_key);
    if (entry) {
        entry->type = EXT_CONFIG_STRING;
        entry->str_val = value ? SAFE_STRDUP(value, "ext config string") : nullptr;
        LOG_DEBUGF("ext_config: set %s = \"%s\" (string)", full_key, value ? value : "(null)");
    }
}

/* API getters - called by extensions (and ext_host for IPC) */
int extension_config_get_int(const char *ext_name, const char *key, int default_val) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = find_config_entry(full_key);
    if (entry && entry->type == EXT_CONFIG_INT) {
        return entry->int_val;
    }
    return default_val;
}

bool extension_config_get_bool(const char *ext_name, const char *key, bool default_val) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = find_config_entry(full_key);
    if (entry && entry->type == EXT_CONFIG_BOOL) {
        return entry->bool_val;
    }
    return default_val;
}

const char *extension_config_get_string(const char *ext_name, const char *key,
                                        const char *default_val) {
    char full_key[EXT_CONFIG_KEY_MAX];
    make_config_key(full_key, sizeof(full_key), ext_name, key);

    ext_config_entry_t *entry = find_config_entry(full_key);
    if (entry && entry->type == EXT_CONFIG_STRING) {
        return entry->str_val ? entry->str_val : default_val;
    }
    return default_val;
}

/* Cleanup config storage */
static void extension_config_cleanup(void) {
    for (int i = 0; i < EXT_CONFIG_HASH_SIZE; i++) {
        ext_config_entry_t *entry = config_buckets[i];
        while (entry) {
            ext_config_entry_t *next = entry->next;
            if (entry->type == EXT_CONFIG_STRING && entry->str_val) {
                free(entry->str_val);
            }
            free(entry);
            entry = next;
        }
        config_buckets[i] = nullptr;
    }
}

/* Declarative Event Hooks (TOML-based command execution on events) */

/* Maximum declarative hooks and commands per hook */
#define MAX_DECL_HOOKS      32
#define MAX_COMMANDS_PER_HOOK 8

typedef struct {
    char event_name[64];        /* Event to listen for (e.g., "buffer:save:after") */
    char *commands[MAX_COMMANDS_PER_HOOK];  /* Commands to execute */
    int command_count;
    bool registered;            /* Whether event handler is registered */
} decl_hook_t;

static decl_hook_t decl_hooks[MAX_DECL_HOOKS];
static int decl_hook_count = 0;

/* Map TOML hook names to event bus event names */
static const char *hook_name_to_event(const char *hook_name) {
    static const struct { const char *hook; const char *event; } map[] = {
        {"buffer_save",   EVT_BUFFER_SAVE},
        {"buffer_load",   EVT_BUFFER_LOAD},
        {"buffer_create", EVT_BUFFER_CREATE},
        {"cursor_move",   EVT_CURSOR_MOVE},
        {"idle",          EVT_IDLE},
        {"startup",       EVT_STARTUP},
        {"shutdown",      EVT_SHUTDOWN},
        {nullptr, nullptr}
    };

    for (int i = 0; map[i].hook; i++) {
        if (strcmp(hook_name, map[i].hook) == 0) {
            return map[i].event;
        }
    }
    return nullptr;
}

/* Event handler for declarative hooks - executes configured commands */
static bool decl_hook_handler(uemacs_event_t *event, void *user_data) {
    decl_hook_t *hook = (decl_hook_t *)user_data;

    LOG_DEBUGF("decl_hook: executing %d commands for event '%s'",
               hook->command_count, event->name);

    for (int i = 0; i < hook->command_count; i++) {
        const char *cmd_name = hook->commands[i];
        fn_t func = fncmatch(cmd_name);
        if (func) {
            LOG_DEBUGF("decl_hook: running command '%s'", cmd_name);
            func(false, 1);  /* Execute with default args */
        } else {
            LOG_WARNF("decl_hook: command '%s' not found", cmd_name);
        }
    }

    return false;  /* Don't consume - allow other handlers */
}

/* Find or create a declarative hook entry */
static decl_hook_t *find_or_create_decl_hook(const char *event_name) {
    /* Check if already exists */
    for (int i = 0; i < decl_hook_count; i++) {
        if (strcmp(decl_hooks[i].event_name, event_name) == 0) {
            return &decl_hooks[i];
        }
    }

    /* Create new entry */
    if (decl_hook_count >= MAX_DECL_HOOKS) {
        LOG_ERROR("decl_hook: max hooks reached");
        return nullptr;
    }

    decl_hook_t *hook = &decl_hooks[decl_hook_count++];
    memset(hook, 0, sizeof(*hook));
    strncpy(hook->event_name, event_name, sizeof(hook->event_name) - 1);

    return hook;
}

/*
 * Register a declarative hook from TOML.
 * Called by settings.c for [hooks.<event_name>] sections.
 *
 * hook_name: TOML hook name (e.g., "buffer_save")
 * command:   Command to execute when event fires
 */
void extension_register_hook_command(const char *hook_name, const char *command) {
    const char *event_name = hook_name_to_event(hook_name);
    if (!event_name) {
        LOG_WARNF("decl_hook: unknown hook name '%s'", hook_name);
        return;
    }

    decl_hook_t *hook = find_or_create_decl_hook(event_name);
    if (!hook) return;

    if (hook->command_count >= MAX_COMMANDS_PER_HOOK) {
        LOG_WARNF("decl_hook: max commands reached for '%s'", event_name);
        return;
    }

    hook->commands[hook->command_count++] = SAFE_STRDUP(command, "decl hook command");
    LOG_DEBUGF("decl_hook: added command '%s' for event '%s'", command, event_name);

    /* Register event handler if not already registered */
    if (!hook->registered) {
        event_bus_on(event_name, (event_handler_fn)decl_hook_handler, hook, 50);
        hook->registered = true;
        LOG_DEBUGF("decl_hook: registered handler for '%s'", event_name);
    }
}

/* Cleanup declarative hooks */
static void decl_hooks_cleanup(void) {
    for (int i = 0; i < decl_hook_count; i++) {
        for (int j = 0; j < decl_hooks[i].command_count; j++) {
            free(decl_hooks[i].commands[j]);
        }
        if (decl_hooks[i].registered) {
            event_bus_off(decl_hooks[i].event_name,
                         (event_handler_fn)decl_hook_handler);
        }
    }
    memset(decl_hooks, 0, sizeof(decl_hooks));
    decl_hook_count = 0;
}

/* Buffer Operations */

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

    size_t total = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        total += (size_t)llength(lp) + 1;
        lp = lforw(lp);
    }

    if (total == 0) {
        if (len) *len = 0;
        return SAFE_STRDUP("", "api empty buffer");
    }

    char *buf = SAFE_ALLOC_SIZED(char, total + 1, "api buffer contents");
    if (!buf) {
        if (len) *len = 0;
        return nullptr;
    }

    size_t pos = 0;
    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        int line_len = llength(lp);
        for (int i = 0; i < line_len; i++) {
            buf[pos++] = (char)lgetc(lp, i);
        }
        buf[pos++] = '\n';
        lp = lforw(lp);
    }

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

static void api_buffer_set_unmodified(struct buffer *bp) {
    if (bp) bp->b_flag &= ~BFCHG;
}

static void api_buffer_set_scratch(struct buffer *bp) {
    if (bp) bp->b_flag |= BFINVS;  /* Mark as invisible/scratch - no save prompt */
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

    struct buffer *saved_bp = curbp;
    struct line *saved_dotp = curwp->w_dotp;
    int saved_doto = curwp->w_doto;

    if (bp != curbp) {
        curbp = bp;
    }

    struct line *lp = lforw(bp->b_linep);
    for (int i = 1; i < line && lp != bp->b_linep; i++) {
        lp = lforw(lp);
    }

    if (lp == bp->b_linep) {
        curbp = saved_bp;
        curwp->w_dotp = saved_dotp;
        curwp->w_doto = saved_doto;
        return -1;
    }

    curwp->w_dotp = lp;
    int offset = col - 1;
    if (offset < 0) offset = 0;
    if (offset > llength(lp)) offset = llength(lp);
    curwp->w_doto = offset;

    int result = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            if (lnewline() != true) { result = -1; break; }
        } else {
            if (linsert(1, text[i]) != true) { result = -1; break; }
        }
    }

    curbp = saved_bp;
    curwp->w_dotp = saved_dotp;
    curwp->w_doto = saved_doto;

    /* The dot is restored exactly, so the updpos dot-change chokepoint
     * never fires — but inserting lines above it changed its absolute
     * line number. Invalidate every window showing this buffer. */
    for (struct window *wp = wheadp; wp; wp = wp->w_wndp) {
        if (wp->w_bufp == bp)
            invalidate_line_cache(wp);
    }

    return result;
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

    /* Route through bclear so every cache resets with the lines:
     * b_line_count/b_byte_count/b_word_count, b_stats_dirty, the line
     * index, the undo stack and the piece-table storage. The
     * hand-rolled version here skipped all of those, leaving
     * b_line_index[] entries pointing at freed lines. Clearing BFCHG
     * first suppresses bclear's interactive discard prompt — a
     * programmatic clear never prompts. */
    bp->b_flag &= ~BFCHG;
    if (bclear(bp) != true) return -1;

    /* bclear leaves window fixup to the caller. */
    struct window *wp = wheadp;
    while (wp) {
        if (wp->w_bufp == bp) {
            wp->w_dotp = bp->b_linep;
            wp->w_doto = 0;
            wp->w_markp = nullptr;
            wp->w_marko = 0;
            wp->w_flag |= WFHARD;
            invalidate_line_cache(wp);
        }
        wp = wp->w_wndp;
    }

    return 0;
}

static struct buffer *api_buffer_first(void) {
    return bheadp;
}

static struct buffer *api_buffer_next(struct buffer *bp) {
    return bp ? bp->b_bufp : nullptr;
}

/* Cursor/Point Operations */

static void api_get_point(int *line, int *col) {
    if (line) *line = (int)getlinenum(curbp, curwp->w_dotp);
    if (col) *col = curwp->w_doto + 1;
}

static void api_set_point(int line, int col) {
    struct line *lp = lforw(curbp->b_linep);
    for (int i = 1; i < line && lp != curbp->b_linep; i++) {
        lp = lforw(lp);
    }

    if (lp != curbp->b_linep) {
        curwp->w_dotp = lp;
        int offset = col - 1;
        if (offset < 0) offset = 0;
        if (offset > llength(lp)) offset = llength(lp);
        curwp->w_doto = offset;
        curwp->w_flag |= WFMOVE;
        /* Core motions invalidate this on their own paths; without it
         * the modeline's cached line number goes stale on every
         * extension-driven jump. */
        invalidate_line_cache(curwp);
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

static char *api_get_word_at_point(void) {
    if (!curwp || !curwp->w_dotp) return nullptr;

    struct line *lp = curwp->w_dotp;
    int offset = curwp->w_doto;
    int len = llength(lp);

    if (offset >= len) return nullptr;

    char c = (char)lgetc(lp, offset);
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
        return nullptr;
    }

    int start = offset;
    while (start > 0) {
        c = (char)lgetc(lp, start - 1);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        start--;
    }

    int end = offset;
    while (end < len) {
        c = (char)lgetc(lp, end);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        end++;
    }

    int word_len = end - start;
    if (word_len <= 0) return nullptr;

    char *word = SAFE_ALLOC_SIZED(char, (size_t)word_len + 1, "api word at point");
    if (!word) return nullptr;

    for (int i = 0; i < word_len; i++) {
        word[i] = (char)lgetc(lp, start + i);
    }
    word[word_len] = '\0';

    return word;
}

static char *api_get_current_line(void) {
    if (!curwp || !curwp->w_dotp) return nullptr;

    struct line *lp = curwp->w_dotp;
    int len = llength(lp);

    char *line = SAFE_ALLOC_SIZED(char, (size_t)len + 1, "api current line");
    if (!line) return nullptr;

    for (int i = 0; i < len; i++) {
        line[i] = (char)lgetc(lp, i);
    }
    line[len] = '\0';

    return line;
}

static char *api_get_line_at(struct buffer *bp, int line_num) {
    if (!bp) bp = curbp;
    if (!bp || !bp->b_linep || line_num < 1) return nullptr;

    /* Navigate to the requested line (1-based) */
    struct line *lp = lforw(bp->b_linep);  /* First real line */
    int current = 1;

    while (lp != bp->b_linep && current < line_num) {
        lp = lforw(lp);
        current++;
    }

    /* Check if we found the line */
    if (lp == bp->b_linep || current != line_num) return nullptr;

    int len = llength(lp);
    char *line = SAFE_ALLOC_SIZED(char, (size_t)len + 1, "api line at");
    if (!line) return nullptr;

    for (int i = 0; i < len; i++) {
        line[i] = (char)lgetc(lp, i);
    }
    line[len] = '\0';

    return line;
}

static int api_find_file_line(const char *path, int line) {
    if (!path) return -1;

    if (getfile(path, true) != true) {
        return -1;
    }

    if (line > 0) {
        gotoline(true, line);
    }

    return 0;
}

/* Window Operations */

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
    wp->w_flag |= WFHARD;
    return 0;
}

static struct window *api_window_at_row(int screen_row) {
    struct window *wp = wheadp;
    while (wp) {
        if (screen_row >= wp->w_toprow &&
            screen_row < wp->w_toprow + wp->w_ntrows) {
            return wp;
        }
        wp = wp->w_wndp;
    }
    return nullptr;
}

static int api_window_switch(struct window *wp) {
    if (!wp) return -1;

    struct window *check = wheadp;
    while (check) {
        if (check == wp) {
            curwp = wp;
            curbp = wp->w_bufp;
            return 0;
        }
        check = check->w_wndp;
    }
    return -1;
}

static int api_screen_to_buffer_pos(struct window *wp, int screen_row, int screen_col,
                                     int *buf_line, int *buf_offset) {
    if (!wp) return -1;

    int row_in_window = screen_row - wp->w_toprow;
    if (row_in_window < 0 || row_in_window >= wp->w_ntrows) {
        return -1;
    }

    struct line *lp = wp->w_linep;
    int line_num = (int)getlinenum(wp->w_bufp, wp->w_linep);

    for (int i = 0; i < row_in_window && lp != wp->w_bufp->b_linep; i++) {
        lp = lforw(lp);
        line_num++;
    }

    if (lp == wp->w_bufp->b_linep) {
        return -1;
    }

    if (buf_line) *buf_line = line_num;

    if (buf_offset) {
        /* Display-width-aware column walk: multibyte UTF-8 advances by
         * codepoint (double-width CJK counts 2 columns) and tabs honor
         * the configured width. Wrapped and horizontally scrolled rows
         * are not yet mapped (see ROADMAP). */
        char line_buf[NSTRING];
        int len = llength(lp);
        if (len > NSTRING - 1) len = NSTRING - 1;
        lget_text(lp, 0, (size_t)len, line_buf, sizeof(line_buf));

        int tab_width = tabmask + 1;
        unsigned offset = 0;
        int col = 0;

        while ((int)offset < len && col < screen_col) {
            if (line_buf[offset] == '\t') {
                col = ((col / tab_width) + 1) * tab_width;
                offset++;
            } else {
                unicode_t uc;
                unsigned step = utf8_to_unicode(line_buf, offset,
                                                (unsigned)len, &uc);
                if (step == 0) { offset++; col++; continue; }
                int w = unicode_display_width(uc);
                col += (w > 0) ? w : 1;
                offset += step;
            }
        }
        *buf_offset = (int)offset;
    }

    return 0;
}

static int api_set_mark(void) {
    curwp->w_markp = curwp->w_dotp;
    curwp->w_marko = curwp->w_doto;
    return 0;
}

static int api_scroll_up(int lines) {
    if (lines <= 0) return 0;

    for (int i = 0; i < lines; i++) {
        if (curwp->w_linep == curbp->b_linep) {
            break;
        }
        curwp->w_linep = lback(curwp->w_linep);
    }

    curwp->w_flag |= WFHARD;
    return 0;
}

static int api_scroll_down(int lines) {
    if (lines <= 0) return 0;

    for (int i = 0; i < lines; i++) {
        struct line *next = lforw(curwp->w_linep);
        if (next == curbp->b_linep) {
            break;
        }
        curwp->w_linep = next;
    }

    curwp->w_flag |= WFHARD;
    return 0;
}

/* User Interface */

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

/* Shell Integration */

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
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";
        execlp(shell, shell, "-c", cmd, (char *)nullptr);
        _exit(127);
    }

    close(pipefd[1]);

    size_t capacity = 4096;
    size_t pos = 0;
    char *buf = SAFE_ALLOC_SIZED(char, capacity, "api shell output");

    if (buf) {
        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };

        for (;;) {
            int poll_ret = poll(&pfd, 1, 5000);
            if (poll_ret <= 0) break;

            if (pos + 4096 > capacity) {
                capacity *= 2;
                char *new_buf = SAFE_REALLOC(buf, capacity, "api shell output grow");
                if (!new_buf) break;
                buf = new_buf;
            }

            ssize_t n = read(pipefd[0], buf + pos, 4096);
            if (n <= 0) break;
            pos += (size_t)n;
        }

        buf[pos] = '\0';
        if (output) *output = buf;
        else SAFE_FREE(buf);
        if (len) *len = pos;
    }

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Memory Helpers */

static void *api_alloc(size_t size) {
    return safe_alloc(size, "extension api alloc", __FILE__, __LINE__);
}

static void api_free(void *ptr) {
    safe_free(&ptr);
}

static char *api_strdup(const char *s) {
    return s ? SAFE_STRDUP(s, "extension api strdup") : nullptr;
}

/* Logging */

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

/* Syntax Highlighting API */

static int api_syntax_register_lexer(
    const char *name,
    const char **patterns,
    uemacs_syntax_lex_fn lex_fn,
    void *user_data
) {
    if (!name || !patterns || !lex_fn) {
        LOG_ERROR("Extension API: syntax_register_lexer: invalid arguments");
        return -1;
    }

    /*
     * The uemacs_syntax_lex_fn and syntax_lex_fn have identical signatures
     * and binary-compatible state structs, but are defined as separate types
     * for API encapsulation. Suppress the function-type warning for this cast.
     */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    int lang_id = syntax_register_language(name, patterns,
        (syntax_lex_fn)lex_fn, user_data);
#pragma GCC diagnostic pop

    if (lang_id >= 0) {
        LOG_INFOF("Extension API: Registered syntax lexer '%s'", name);
    } else {
        LOG_ERRORF("Extension API: Failed to register syntax lexer '%s'", name);
    }

    return lang_id;
}

static int api_syntax_unregister_lexer(const char *name) {
    if (!name) return -1;

    int result = syntax_unregister_language(name);
    if (result == 0) {
        LOG_INFOF("Extension API: Unregistered syntax lexer '%s'", name);
    }
    return result;
}

static int api_syntax_add_token(uemacs_line_tokens_t *tokens, int end_col, int face) {
    if (!tokens || end_col < 0 || face < 0) return -1;
    return line_tokens_add((line_tokens_t *)tokens, (uint16_t)end_col, (uint16_t)face);
}

static void api_syntax_invalidate_buffer(struct buffer *bp) {
    if (!bp || !bp->b_syntax) return;
    bp->b_syntax->dirty = true;
    struct window *wp = wheadp;
    while (wp) {
        if (wp->w_bufp == bp) {
            wp->w_flag |= WFHARD;
        }
        wp = wp->w_wndp;
    }
}

/* Modeline Extension Segments */

#define MAX_MODELINE_SEGMENTS 16
#define MODELINE_NAME_MAX     32

typedef struct {
    char name[MODELINE_NAME_MAX];
    uemacs_modeline_fn format_fn;
    void *user_data;
    int urgency;
    bool active;
} modeline_segment_t;

static modeline_segment_t modeline_segments[MAX_MODELINE_SEGMENTS];
static int modeline_segment_count = 0;

static int api_modeline_register(const char *name, uemacs_modeline_fn format_fn,
                                  void *user_data, int urgency) {
    if (!name || !format_fn) return -1;
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (modeline_segments[i].active &&
            strcmp(modeline_segments[i].name, name) == 0) {
            return -1;
        }
    }
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (!modeline_segments[i].active) {
            strncpy(modeline_segments[i].name, name, MODELINE_NAME_MAX - 1);
            modeline_segments[i].name[MODELINE_NAME_MAX - 1] = '\0';
            modeline_segments[i].format_fn = format_fn;
            modeline_segments[i].user_data = user_data;
            modeline_segments[i].urgency = urgency;
            modeline_segments[i].active = true;
            modeline_segment_count++;
            return 1;
        }
    }
    return -1;
}

static int api_modeline_unregister(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (modeline_segments[i].active &&
            strcmp(modeline_segments[i].name, name) == 0) {
            modeline_segments[i].active = false;
            modeline_segment_count--;
            return 0;
        }
    }
    return -1;
}

static void api_modeline_refresh(void) {
    struct window *wp = wheadp;
    while (wp) {
        wp->w_flag |= WFMODE;
        wp = wp->w_wndp;
    }
}

static int api_delete_chars(int n) {
    if (n <= 0) return 0;
    return ldelete((long)n, false) == true ? 0 : -1;
}

static int api_kill_line(void) {
    return kill_to_eol(false, 1) == true ? 0 : -1;
}

/* Keymap API - Extension keymap registration */

static struct keymap *api_keymap_create(const char *name) {
    if (!name) return nullptr;
    return keymap_create(name);
}

static void api_keymap_destroy(struct keymap *km) {
    if (km) keymap_destroy(km);
}

static int api_keymap_bind(struct keymap *km, uint32_t code, uint16_t mods, command_fn cmd) {
    if (!km || !cmd) return -1;
    return keymap_bind(km, keymap_key_make(code, mods), cmd);
}

static int api_keymap_unbind(struct keymap *km, uint32_t code, uint16_t mods) {
    if (!km) return -1;
    return keymap_unbind(km, keymap_key_make(code, mods));
}

/* Vim Infrastructure Accessors */

static struct keymap *api_get_vim_normal_keymap(void) {
    return atomic_load(&vim_normal_keymap);
}

static struct keymap *api_get_vim_visual_keymap(void) {
    return atomic_load(&vim_visual_keymap);
}

static void api_set_vim_mode_active(int active) {
    atomic_store(&vim_mode_active, active);
}

static int api_get_vim_mode_active(void) {
    return atomic_load(&vim_mode_active);
}

static void *api_get_vim_state(void) {
    return &g_vim_state;
}

/* Vim typed accessors — task #5.
 *
 * These let extensions query/mutate vim modal-editing state without including
 * internal/editor_mode.h. The enum values in the public API header mirror the
 * internal enum editor_mode exactly, so a plain integer cast is safe. If the
 * internal enum ever grows values the public enum doesn't have, widen the
 * public enum first, never narrow the cast. */

static uemacs_vim_mode_t api_vim_get_mode(void) {
    return (uemacs_vim_mode_t)atomic_load(&g_vim_state.current_mode);
}

static void api_vim_set_mode(uemacs_vim_mode_t mode) {
    atomic_store(&g_vim_state.current_mode, (enum editor_mode)mode);
}

static int api_vim_get_count(void) {
    return atomic_load(&g_vim_state.count);
}

static void api_vim_set_count(int count) {
    /* Counts are non-negative; 0 means "no count pending". */
    atomic_store(&g_vim_state.count, count > 0 ? count : 0);
}

static uemacs_vim_operator_t api_vim_get_pending_op(void) {
    int op = atomic_load(&g_vim_state.pending_op);
    /* Internal codes (OP_NONE=0, OP_DELETE=1, OP_CHANGE=2, OP_YANK=3) map
     * directly to UEMACS_VIM_OP_* values. Defensive clamp on unexpected input. */
    if (op < 0 || op > UEMACS_VIM_OP_YANK) return UEMACS_VIM_OP_NONE;
    return (uemacs_vim_operator_t)op;
}

static int api_vim_get_last_change(char *buf, int max_len) {
    if (!buf || max_len <= 0) return 0;
    int len = g_vim_state.last_change_len;
    if (len < 0) len = 0;
    if (len > max_len) len = max_len;
    memcpy(buf, g_vim_state.last_change, (size_t)len);
    return len;
}

static bool api_vim_get_mark(char name, int *line_out, int *col_out) {
    if (name < 'a' || name > 'z') return false;
    int idx = name - 'a';
    struct line *lp = g_vim_state.marks[idx].line;
    if (!lp) return false;
    if (line_out) *line_out = (int)getlinenum(curbp, lp);
    if (col_out) *col_out = g_vim_state.marks[idx].offset;
    return true;
}

static bool api_vim_set_mark(char name, int line_num, int col) {
    if (name < 'a' || name > 'z') return false;
    if (line_num < 1) return false;
    /* Walk to the target line. getlinenum is O(1) with the index;
     * translate back via linear scan (acceptable — marks set rarely). */
    struct line *lp = lforw(curbp->b_linep);
    int n = 1;
    while (lp != curbp->b_linep && n < line_num) {
        lp = lforw(lp);
        n++;
    }
    if (lp == curbp->b_linep) return false;
    int idx = name - 'a';
    g_vim_state.marks[idx].line = lp;
    g_vim_state.marks[idx].offset = col;
    return true;
}

/* Registers + undo-boundary — task #7.
 *
 * Register naming:
 *   '0'..'9'  → numbered registers, stored at registers[0..9]
 *   'a'..'z'  → named registers, stored at registers[10..35]
 *   'A'..'Z'  → same as lowercase (case-insensitive, vim-style)
 *
 * Storage: g_vim_state.registers is a fixed array of 36 slots. Each slot owns
 * a malloc'd char* text buffer; we free/replace on set.
 *
 * System clipboard: api_clipboard_set emits an OSC 52 escape sequence, which
 * modern terminals (kitty, wezterm, iTerm2, alacritty, xterm with
 * allowWindowOps, foot, etc.) recognize and pipe to the system clipboard.
 * Clipboard READ via OSC 52 is not implemented — it requires parsing a
 * terminal response which conflicts with the input state machine. */

static int register_name_to_index(char name) {
    if (name >= '0' && name <= '9') return name - '0';
    if (name >= 'a' && name <= 'z') return 10 + (name - 'a');
    if (name >= 'A' && name <= 'Z') return 10 + (name - 'A');
    return -1;
}

static int api_vim_get_register(char name, char *buf, int max_len, bool *linewise_out) {
    int idx = register_name_to_index(name);
    if (idx < 0 || !buf || max_len <= 0) return 0;

    const char *src = g_vim_state.registers[idx].text;
    int src_len = g_vim_state.registers[idx].len;
    if (!src || src_len <= 0) {
        if (linewise_out) *linewise_out = false;
        return 0;
    }

    int copy_len = (src_len < max_len) ? src_len : max_len;
    memcpy(buf, src, (size_t)copy_len);
    if (linewise_out) *linewise_out = (g_vim_state.registers[idx].linewise != 0);
    return copy_len;
}

static int api_vim_set_register(char name, const char *text, int len, bool linewise) {
    int idx = register_name_to_index(name);
    if (idx < 0 || len < 0) return -1;

    /* Free any previous content so the register owns exactly one buffer. */
    char *old = g_vim_state.registers[idx].text;
    if (old) SAFE_FREE(old);

    if (text && len > 0) {
        char *copy = SAFE_ARRAY(char, len + 1, "vim register");
        if (!copy) {
            g_vim_state.registers[idx].text = nullptr;
            g_vim_state.registers[idx].len = 0;
            g_vim_state.registers[idx].linewise = 0;
            return -1;
        }
        memcpy(copy, text, (size_t)len);
        copy[len] = '\0';
        g_vim_state.registers[idx].text = copy;
        g_vim_state.registers[idx].len = len;
        g_vim_state.registers[idx].linewise = linewise ? 1 : 0;
    } else {
        g_vim_state.registers[idx].text = nullptr;
        g_vim_state.registers[idx].len = 0;
        g_vim_state.registers[idx].linewise = 0;
    }
    return 0;
}

/* Base64-encode text for OSC 52. Returns bytes written (not including null). */
static int base64_encode(const char *src, int src_len, char *dst, int dst_max) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int out = 0;
    int i = 0;
    while (i + 3 <= src_len && out + 4 < dst_max) {
        uint32_t v = ((uint32_t)(unsigned char)src[i] << 16) |
                     ((uint32_t)(unsigned char)src[i+1] << 8) |
                      (uint32_t)(unsigned char)src[i+2];
        dst[out++] = alphabet[(v >> 18) & 0x3F];
        dst[out++] = alphabet[(v >> 12) & 0x3F];
        dst[out++] = alphabet[(v >> 6)  & 0x3F];
        dst[out++] = alphabet[ v        & 0x3F];
        i += 3;
    }
    int remain = src_len - i;
    if (remain > 0 && out + 4 < dst_max) {
        uint32_t v = (uint32_t)(unsigned char)src[i] << 16;
        if (remain == 2) v |= (uint32_t)(unsigned char)src[i+1] << 8;
        dst[out++] = alphabet[(v >> 18) & 0x3F];
        dst[out++] = alphabet[(v >> 12) & 0x3F];
        dst[out++] = (remain == 2) ? alphabet[(v >> 6) & 0x3F] : '=';
        dst[out++] = '=';
    }
    if (out < dst_max) dst[out] = '\0';
    return out;
}

static int api_clipboard_set(const char *text, int len) {
    if (!text || len < 0) return -1;
    /* OSC 52 payload size is bounded by terminal implementations (kitty 8KB,
     * wezterm larger). Hard cap at 64KB base64 output ≈ 48KB input to keep
     * us from blasting multi-MB documents through the PTY. */
    if (len > 49152) len = 49152;

    int enc_cap = ((len + 2) / 3) * 4 + 1;
    char *b64 = SAFE_ARRAY(char, enc_cap, "osc52 clipboard");
    if (!b64) return -1;
    int enc_len = base64_encode(text, len, b64, enc_cap);

    /* Emit "\x1b]52;c;<base64>\x07". Use TTputc so it goes through the normal
     * terminal-write path. */
    TTputc(0x1B); TTputc(']');
    TTputc('5'); TTputc('2'); TTputc(';');
    TTputc('c'); TTputc(';');
    for (int i = 0; i < enc_len; i++) TTputc((unsigned char)b64[i]);
    TTputc(0x07);
    TTflush();

    SAFE_FREE(b64);
    return 0;
}

static void api_undo_group_begin(struct buffer *bp) {
    undo_group_begin(bp ? bp : curbp);
}

static void api_undo_group_end(struct buffer *bp) {
    undo_group_end(bp ? bp : curbp);
}

/* Display Setting Accessors */

static void api_set_highlight_line(int enabled) {
    highlight_current_line = enabled;
}

static void api_set_ruler(int enabled) {
    column_ruler_enabled = enabled;
}

static void api_set_syntax_enabled(int enabled) {
    g_palette.syntax_enabled = (bool)enabled;
}

/* Command Lookup */

static fn_t api_find_command(const char *name) {
    if (!name) return nullptr;
    /* Check static command table first */
    fn_t cmd = fncmatch(name);
    if (cmd) return cmd;
    /* Check dynamic (extension) commands */
    return (fn_t)extension_find_command(name);
}

char *extension_get_modeline_segments(int urgency) {
    if (modeline_segment_count == 0) return nullptr;
    char *parts[MAX_MODELINE_SEGMENTS];
    int part_count = 0;
    size_t total_len = 0;
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (!modeline_segments[i].active) continue;
        if (urgency >= 0 && modeline_segments[i].urgency != urgency) continue;
        char *text = modeline_segments[i].format_fn(modeline_segments[i].user_data);
        if (text && text[0] != '\0') {
            parts[part_count++] = text;
            total_len += strlen(text) + 1;
        } else if (text) {
            SAFE_FREE(text);
        }
    }
    if (part_count == 0) return nullptr;
    char *result = SAFE_ALLOC_SIZED(char, total_len + 1, "modeline format result");
    if (!result) {
        for (int i = 0; i < part_count; i++) SAFE_FREE(parts[i]);
        return nullptr;
    }
    char *p = result;
    for (int i = 0; i < part_count; i++) {
        if (i > 0) *p++ = ' ';
        size_t len = strlen(parts[i]);
        memcpy(p, parts[i], len);
        p += len;
        SAFE_FREE(parts[i]);
    }
    *p = '\0';
    return result;
}

/* Check if any extension wants full modeline control */
bool extension_modeline_has_full_override(void) {
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (modeline_segments[i].active &&
            modeline_segments[i].urgency == UEMACS_MODELINE_URGENCY_FULL) {
            return true;
        }
    }
    return false;
}

/* Get full modeline from override extension (caller frees) */
char *extension_get_modeline_full(void) {
    for (int i = 0; i < MAX_MODELINE_SEGMENTS; i++) {
        if (modeline_segments[i].active &&
            modeline_segments[i].urgency == UEMACS_MODELINE_URGENCY_FULL) {
            return modeline_segments[i].format_fn(modeline_segments[i].user_data);
        }
    }
    return nullptr;
}

/* The Global API Instance */

static struct muemacs_api global_api = {
    .api_version = MUEMACS_API_VERSION,

    /* Generic event system */
    .on = api_on,
    .off = api_off,
    .emit = api_emit,

    /* Configuration access */
    .config_int = extension_config_get_int,
    .config_bool = extension_config_get_bool,
    .config_string = extension_config_get_string,

    /* Command registration */
    .register_command = api_register_command,
    .unregister_command = api_unregister_command,

    /* Buffer operations */
    .current_buffer = api_current_buffer,
    .find_buffer = api_find_buffer,
    .buffer_contents = api_buffer_contents,
    .buffer_filename = api_buffer_filename,
    .buffer_name = api_buffer_name,
    .buffer_modified = api_buffer_modified,
    .buffer_set_unmodified = api_buffer_set_unmodified,
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
    .get_line_at = api_get_line_at,

    /* Window operations */
    .current_window = api_current_window,
    .window_count = api_window_count,
    .window_set_wrap_col = api_window_set_wrap_col,
    .window_at_row = api_window_at_row,
    .window_switch = api_window_switch,

    /* Mouse/cursor helpers */
    .screen_to_buffer_pos = api_screen_to_buffer_pos,
    .set_mark = api_set_mark,
    .scroll_up = api_scroll_up,
    .scroll_down = api_scroll_down,

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

    /* Syntax highlighting */
    .syntax_register_lexer = api_syntax_register_lexer,
    .syntax_unregister_lexer = api_syntax_unregister_lexer,
    .syntax_add_token = api_syntax_add_token,
    .syntax_invalidate_buffer = api_syntax_invalidate_buffer,

    /* Modeline extension segments */
    .modeline_register = api_modeline_register,
    .modeline_unregister = api_modeline_unregister,
    .modeline_refresh = api_modeline_refresh,

    /* Text manipulation */
    .delete_chars = api_delete_chars,

    /* ABI-stable extension fields (added after initial release) */
    .buffer_first = api_buffer_first,
    .buffer_next = api_buffer_next,

    /* ABI-stable function lookup (API v4) */
    .struct_size = sizeof(struct muemacs_api),
    .get_function = api_get_function,
};

struct muemacs_api *muemacs_get_api(void) {
    return &global_api;
}

/* Initialization and Cleanup */

void extension_api_init(void) {
    memset(dynamic_commands, 0, sizeof(dynamic_commands));
    dynamic_command_count = 0;

    /* Initialize the event bus */
    event_bus_init();

    LOG_INFO("Extension API v3: Initialized with generic event bus");
}

void extension_api_cleanup(void) {
    for (int i = 0; i < MAX_DYNAMIC_COMMANDS; i++) {
        if (dynamic_commands[i].active) {
            free(dynamic_commands[i].name);
        }
    }
    memset(dynamic_commands, 0, sizeof(dynamic_commands));
    dynamic_command_count = 0;

    /* Cleanup extension config storage */
    extension_config_cleanup();

    /* Cleanup declarative hooks */
    decl_hooks_cleanup();

    /* Shutdown the event bus */
    event_bus_shutdown();

    LOG_INFO("Extension API: Cleaned up");
}

/* Event Dispatch (called from core) */

/*
 * These functions are called from core files (main.c, file.c) to emit events.
 * They replace the old extension_fire_* functions.
 */

void extension_emit_buffer_save(struct buffer *bp) {
    event_bus_emit(UEMACS_EVT_BUFFER_SAVE, bp, 0);
}

void extension_emit_buffer_load(struct buffer *bp) {
    event_bus_emit(UEMACS_EVT_BUFFER_LOAD, bp, 0);
}

bool extension_emit_key(int key) {
    return event_bus_emit(UEMACS_EVT_INPUT_KEY, &key, sizeof(key));
}

/* Emit the raw-input event fired BEFORE binding dispatch. Extensions doing
 * modal editing (vim operator-pending, count prefixes, ex-command mode) hook
 * this to claim keys before the core keymap looks them up.
 *
 * Maps the internal input_key_event_t to the narrower public
 * uemacs_raw_key_event_t so extensions don't need internal headers. */
bool extension_emit_raw_key(const struct input_key_event *evt) {
    if (!evt) return false;

    uemacs_raw_key_event_t payload = {
        .code      = evt->code,
        .modifiers = evt->modifiers,
        .kind      = UEMACS_KEY_KIND_CHAR,  /* default; overridden below */
    };

    switch (evt->type) {
    case KEY_CHAR:        payload.kind = UEMACS_KEY_KIND_CHAR;        break;
    case KEY_SPECIAL:     payload.kind = UEMACS_KEY_KIND_SPECIAL;     break;
    case KEY_PASTE_START: payload.kind = UEMACS_KEY_KIND_PASTE_START; break;
    case KEY_PASTE_END:   payload.kind = UEMACS_KEY_KIND_PASTE_END;   break;
    case KEY_FOCUS_IN:    payload.kind = UEMACS_KEY_KIND_FOCUS_IN;    break;
    case KEY_FOCUS_OUT:   payload.kind = UEMACS_KEY_KIND_FOCUS_OUT;   break;
    default:
        /* Unhandled event types (KEY_MOUSE, KEY_OSC_RECEIVED, KEY_CSI_UNKNOWN,
         * KEY_NONE) do not fire input:raw. Mouse has its own event; the
         * rest aren't meaningful to modal-editing extensions. */
        return false;
    }

    return event_bus_emit(UEMACS_EVT_INPUT_RAW, &payload, sizeof(payload));
}

void extension_emit_idle(void) {
    event_bus_emit(UEMACS_EVT_IDLE, nullptr, 0);
}

bool extension_emit_mouse(struct input_key_event *evt) {
    return event_bus_emit(UEMACS_EVT_INPUT_MOUSE, evt, sizeof(*evt));
}

/*
 * Character insert event - allows extensions to transform characters.
 * Returns: 0 = no transform, 1 = use transformed, -1 = delete prev + insert,
 *          2 = extension handled completely (don't insert anything)
 */
int extension_emit_char_insert(int c, int *out) {
    event_char_insert_t evt = {
        .character = c,
        .transformed = c,
        .cancel = false,
    };

    bool consumed = event_bus_emit(UEMACS_EVT_CHAR_INSERT, &evt, sizeof(evt));

    if (consumed && out) {
        *out = evt.transformed;
        /* transformed == 0 means extension handled it (e.g., inserted 5 spaces for tab) */
        if (evt.transformed == 0) {
            return 2;  /* Extension handled completely */
        }
        return evt.cancel ? -1 : 1;
    }
    return 0;
}

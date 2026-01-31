/*
 * extension_api.h - μEmacs Editor API for Extensions
 *
 * This struct is passed to extensions during init().
 * Extensions use these function pointers to interact with the editor.
 *
 * API Version 4: ABI-Stable Named Lookup
 * - Extensions use get_function("name") instead of struct field offsets
 * - Immune to struct layout changes across versions
 * - Extensions remain compatible as long as function signatures don't change
 *
 * C23 compliant
 */

#ifndef UEMACS_EXTENSION_API_H
#define UEMACS_EXTENSION_API_H

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>

/* Current API version */
#define MUEMACS_API_VERSION 4

/* Forward declarations */
struct buffer;
struct window;
struct line;
struct line_tokens;
struct syntax_language;
struct input_key_event;
struct uemacs_event;
struct muemacs_api;

/* ============================================================================
 * Event System Types
 * ============================================================================ */

/*
 * Generic event structure passed to all handlers.
 * The 'data' pointer type depends on event name.
 */
typedef struct uemacs_event {
    const char *name;       /* Event name (e.g., "input:mouse") */
    void *data;             /* Event-specific data */
    size_t data_size;       /* Size of data (0 if pointer-only) */
    bool consumed;          /* Set to true to stop propagation */
} uemacs_event_t;

/*
 * Generic event handler callback.
 * Returns true if event was consumed (stops further handlers).
 */
typedef bool (*uemacs_event_fn)(uemacs_event_t *event, void *user_data);

/* Standard event names (use these constants for type safety) */
#define UEMACS_EVT_INPUT_KEY       "input:key"
#define UEMACS_EVT_INPUT_MOUSE     "input:mouse"
#define UEMACS_EVT_BUFFER_LOAD     "buffer:load"
#define UEMACS_EVT_BUFFER_SAVE     "buffer:save:after"
#define UEMACS_EVT_BUFFER_SAVE_PRE "buffer:save:before"
#define UEMACS_EVT_IDLE            "idle"
#define UEMACS_EVT_CHAR_INSERT     "char:insert"
#define UEMACS_EVT_CURSOR_MOVE     "cursor:move"
#define UEMACS_EVT_DISPLAY_PRE     "display:pre"
#define UEMACS_EVT_DISPLAY_POST    "display:post"
#define UEMACS_EVT_DISPLAY_LINE    "display:line"     /* Before rendering each line */
#define UEMACS_EVT_DISPLAY_GUTTER  "display:gutter"   /* Before rendering gutter */
#define UEMACS_EVT_CONFIG_CHANGED  "config:changed"

/* ============================================================================
 * Display Line Event Types (for folding, narrowing, org-mode, etc.)
 * ============================================================================ */

/* What the display loop should do with this line */
typedef enum {
    UEMACS_DISPLAY_RENDER,      /* Render line normally (default) */
    UEMACS_DISPLAY_SKIP,        /* Don't render - line is hidden (folding) */
    UEMACS_DISPLAY_SUBSTITUTE,  /* Replace with substitute text */
} uemacs_display_action_t;

/* Event data for UEMACS_EVT_DISPLAY_LINE */
typedef struct {
    struct buffer *buffer;              /* Buffer being displayed */
    struct line *line;                  /* Line about to be rendered (opaque) */
    int line_num;                       /* 0-based line number in buffer */
    int screen_row;                     /* Screen row where it would render */

    /* Extension response fields - modify these to control rendering */
    uemacs_display_action_t action;     /* What to do with this line */
    const char *substitute;             /* Text if action == SUBSTITUTE */
    int substitute_face;                /* Syntax face for substitute text */
} uemacs_display_line_event_t;

/* ============================================================================
 * Syntax Highlighting Types (for extension lexers)
 * ============================================================================ */

/* Face IDs - what kind of token is this? */
typedef enum {
    UEMACS_FACE_DEFAULT = 0,
    UEMACS_FACE_KEYWORD,
    UEMACS_FACE_STRING,
    UEMACS_FACE_COMMENT,
    UEMACS_FACE_NUMBER,
    UEMACS_FACE_TYPE,
    UEMACS_FACE_FUNCTION,
    UEMACS_FACE_OPERATOR,
    UEMACS_FACE_PREPROCESSOR,
    UEMACS_FACE_CONSTANT,
    UEMACS_FACE_VARIABLE,
    UEMACS_FACE_ATTRIBUTE,
    UEMACS_FACE_ESCAPE,
    UEMACS_FACE_REGEX,
    UEMACS_FACE_SPECIAL,
} uemacs_face_t;

/* Lexer state - passed between lines for multi-line constructs */
typedef struct {
    int mode;           /* 0=normal, 1=string, 2=comment, etc. */
    int nest_depth;     /* for nested comments/strings */
    char string_delim;  /* which quote started string */
    uint32_t state_hash;
} uemacs_lexer_state_t;

/* Initial state for start of file */
#define UEMACS_LEXER_STATE_INIT ((uemacs_lexer_state_t){0, 0, 0, 0})

/* Line tokens - opaque handle, use API functions to manipulate */
typedef struct line_tokens uemacs_line_tokens_t;

/*
 * Lexer callback signature.
 *
 * Note: The first parameter receives the syntax_language pointer from the
 * internal system. Extensions that registered with user_data can cast it
 * or use muemacs_get_api() to access the editor API.
 */
typedef uemacs_lexer_state_t (*uemacs_syntax_lex_fn)(
    const struct syntax_language *lang,
    struct buffer *buffer,
    int line_num,
    const char *line,
    int len,
    uemacs_lexer_state_t prev_state,
    uemacs_line_tokens_t *out
);

/* Command function signature (matches μEmacs fn_t) */
typedef int (*uemacs_cmd_fn)(int f, int n);

/* Modeline segment callback - returns malloc'd string (editor frees), or NULL to hide */
typedef char *(*uemacs_modeline_fn)(void *user_data);

/* Modeline urgency levels */
#define UEMACS_MODELINE_URGENCY_LOW  0  /* Informational (right side in auto mode) */
#define UEMACS_MODELINE_URGENCY_HIGH 1  /* Mode indicator (left side in auto mode) */
#define UEMACS_MODELINE_URGENCY_FULL 2  /* Full takeover - extension renders entire modeline */

/* ============================================================================
 * The Editor API
 * ============================================================================ */

struct muemacs_api {
    /* API version for compatibility checking */
    int api_version;

    /* ─────────────────────────────────────────────────────────────────────────
     * Generic Event System (NEW - replaces individual hook functions)
     * ───────────────────────────────────────────────────────────────────────── */

    /*
     * Register an event handler.
     *
     * event:     Event name (e.g., "input:mouse", "buffer:save:after")
     * handler:   Callback function
     * user_data: Opaque pointer passed to handler
     * priority:  Execution order (-100 = first, 0 = normal, 100 = last)
     *
     * Returns: 0 on success, -1 on error
     */
    int (*on)(const char *event, uemacs_event_fn handler, void *user_data, int priority);

    /*
     * Unregister an event handler.
     *
     * event:   Event name
     * handler: The callback to remove
     *
     * Returns: 0 on success, -1 if not found
     */
    int (*off)(const char *event, uemacs_event_fn handler);

    /*
     * Emit an event (for extension-to-extension communication).
     *
     * event: Event name
     * data:  Event-specific data
     *
     * Returns: true if any handler consumed the event
     */
    bool (*emit)(const char *event, void *data);

    /* ─────────────────────────────────────────────────────────────────────────
     * Configuration Access (NEW - read extension config from TOML)
     * ───────────────────────────────────────────────────────────────────────── */

    int (*config_int)(const char *ext_name, const char *key, int default_val);
    bool (*config_bool)(const char *ext_name, const char *key, bool default_val);
    const char *(*config_string)(const char *ext_name, const char *key, const char *default_val);

    /* ─────────────────────────────────────────────────────────────────────────
     * Command Registration
     * ───────────────────────────────────────────────────────────────────────── */

    int (*register_command)(const char *name, uemacs_cmd_fn func);
    int (*unregister_command)(const char *name);

    /* ─────────────────────────────────────────────────────────────────────────
     * Buffer Operations
     * ───────────────────────────────────────────────────────────────────────── */

    struct buffer *(*current_buffer)(void);
    struct buffer *(*find_buffer)(const char *name);
    char *(*buffer_contents)(struct buffer *bp, size_t *len);  /* Caller must free */
    const char *(*buffer_filename)(struct buffer *bp);
    const char *(*buffer_name)(struct buffer *bp);
    bool (*buffer_modified)(struct buffer *bp);
    void (*buffer_set_unmodified)(struct buffer *bp);
    int (*buffer_insert)(const char *text, size_t len);
    int (*buffer_insert_at)(struct buffer *bp, int line, int col,
                            const char *text, size_t len);
    struct buffer *(*buffer_create)(const char *name);
    int (*buffer_switch)(struct buffer *bp);
    int (*buffer_clear)(struct buffer *bp);

    /* ─────────────────────────────────────────────────────────────────────────
     * Cursor/Point Operations
     * ───────────────────────────────────────────────────────────────────────── */

    void (*get_point)(int *line, int *col);
    void (*set_point)(int line, int col);
    int (*get_line_count)(struct buffer *bp);
    char *(*get_word_at_point)(void);
    char *(*get_current_line)(void);
    char *(*get_line_at)(struct buffer *bp, int line_num);  /* 1-based, caller must free */

    /* ─────────────────────────────────────────────────────────────────────────
     * Window Operations
     * ───────────────────────────────────────────────────────────────────────── */

    struct window *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(struct window *wp, int col);
    struct window *(*window_at_row)(int screen_row);
    int (*window_switch)(struct window *wp);

    /* ─────────────────────────────────────────────────────────────────────────
     * Mouse/Cursor Helpers
     * ───────────────────────────────────────────────────────────────────────── */

    int (*screen_to_buffer_pos)(struct window *wp, int screen_row, int screen_col,
                                int *buf_line, int *buf_offset);
    int (*set_mark)(void);
    int (*scroll_up)(int lines);
    int (*scroll_down)(int lines);

    /* ─────────────────────────────────────────────────────────────────────────
     * User Interface
     * ───────────────────────────────────────────────────────────────────────── */

    void (*message)(const char *fmt, ...);
    void (*vmessage)(const char *fmt, va_list ap);
    int (*prompt)(const char *prompt, char *buf, size_t buflen);
    int (*prompt_yn)(const char *prompt);
    void (*update_display)(void);

    /* ─────────────────────────────────────────────────────────────────────────
     * File Operations
     * ───────────────────────────────────────────────────────────────────────── */

    int (*find_file_line)(const char *path, int line);

    /* ─────────────────────────────────────────────────────────────────────────
     * Shell Integration
     * ───────────────────────────────────────────────────────────────────────── */

    int (*shell_command)(const char *cmd, char **output, size_t *len);

    /* ─────────────────────────────────────────────────────────────────────────
     * Memory Helpers
     * ───────────────────────────────────────────────────────────────────────── */

    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    char *(*strdup)(const char *s);

    /* ─────────────────────────────────────────────────────────────────────────
     * Logging
     * ───────────────────────────────────────────────────────────────────────── */

    void (*log_info)(const char *fmt, ...);
    void (*log_warn)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);

    /* ─────────────────────────────────────────────────────────────────────────
     * Syntax Highlighting
     * ───────────────────────────────────────────────────────────────────────── */

    int (*syntax_register_lexer)(
        const char *name,
        const char **patterns,
        uemacs_syntax_lex_fn lex_fn,
        void *user_data
    );
    int (*syntax_unregister_lexer)(const char *name);
    int (*syntax_add_token)(uemacs_line_tokens_t *tokens, int end_col, int face);
    void (*syntax_invalidate_buffer)(struct buffer *bp);

    /* ─────────────────────────────────────────────────────────────────────────
     * Modeline Extension Segments
     * ───────────────────────────────────────────────────────────────────────── */

    int (*modeline_register)(const char *name, uemacs_modeline_fn format_fn,
                             void *user_data, int urgency);
    int (*modeline_unregister)(const char *name);
    void (*modeline_refresh)(void);

    /* ─────────────────────────────────────────────────────────────────────────
     * Text Manipulation
     * ───────────────────────────────────────────────────────────────────────── */

    int (*delete_chars)(int n);  /* Delete n chars forward from point */

    /* ─────────────────────────────────────────────────────────────────────────
     * ABI-Stable Extension Fields (added after initial release)
     *
     * NEW FIELDS GO HERE - at the END of the struct to preserve backward compat.
     * Extensions compiled against older headers won't access these fields.
     * ───────────────────────────────────────────────────────────────────────── */

    struct buffer *(*buffer_first)(void);
    struct buffer *(*buffer_next)(struct buffer *bp);
    /* NOTE: buffer_set_scratch available via get_function("buffer_set_scratch") */

    /* ─────────────────────────────────────────────────────────────────────────
     * ABI-Stable Function Lookup (API v4)
     *
     * struct_size and get_function MUST be last for version detection.
     * ───────────────────────────────────────────────────────────────────────── */

    size_t struct_size;  /* sizeof(struct muemacs_api) - for version detection */

    /*
     * Look up API function by name. Returns NULL if not found.
     * This is ABI-stable: adding/removing/reordering other functions won't
     * break extensions that use named lookup.
     *
     * Returns a generic function pointer that must be cast to the correct type.
     * Using void(*)(void) instead of void* for ISO C compliance.
     *
     * Example:
     *   typedef int (*register_cmd_fn)(const char*, uemacs_cmd_fn);
     *   register_cmd_fn reg = (register_cmd_fn)api->get_function("register_command");
     *   if (reg) reg("my-cmd", my_handler);
     */
    void (*(*get_function)(const char *name))(void);
};

/* Get the global API instance */
struct muemacs_api *muemacs_get_api(void);

/* Get extension modeline segments (called by display.c) */
char *extension_get_modeline_segments(int urgency);

/* Check if any extension wants full modeline control */
bool extension_modeline_has_full_override(void);

/* Get full modeline from override extension (caller frees) */
char *extension_get_modeline_full(void);

/* ============================================================================
 * Extension Configuration Setters (called by settings.c during TOML parsing)
 * ============================================================================ */

/*
 * Set extension configuration values.
 * Called during settings.toml parsing for [extensions.<name>] sections.
 *
 * ext_name: Extension name (e.g., "mouse", "lsp")
 * key:      Setting key within that extension
 * value:    The value to set
 */
void extension_config_set_int(const char *ext_name, const char *key, int value);
void extension_config_set_bool(const char *ext_name, const char *key, bool value);
void extension_config_set_string(const char *ext_name, const char *key, const char *value);

/* ============================================================================
 * Declarative Event Hooks (called by settings.c for [hooks.*] sections)
 * ============================================================================ */

/*
 * Register a command to execute when an event fires.
 * Multiple commands can be registered for the same hook.
 *
 * hook_name: TOML hook name (e.g., "buffer_save", "idle", "startup")
 * command:   Editor command to execute (e.g., "trim-whitespace")
 *
 * Supported hook names:
 *   buffer_save   - After buffer is saved to file
 *   buffer_load   - After file is loaded into buffer
 *   buffer_create - When new buffer is created
 *   cursor_move   - When cursor position changes
 *   idle          - When editor is idle (no input)
 *   startup       - When editor starts up
 *   shutdown      - When editor is shutting down
 */
void extension_register_hook_command(const char *hook_name, const char *command);

#endif /* UEMACS_EXTENSION_API_H */

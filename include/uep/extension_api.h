/*
 * extension_api.h - μEmacs Editor API for Extensions
 *
 * This struct is passed to extensions during init().
 * Extensions use these function pointers to interact with the editor.
 *
 * C23 compliant
 */

#ifndef UEMACS_EXTENSION_API_H
#define UEMACS_EXTENSION_API_H

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/* Forward declarations */
struct buffer;
struct window;

/* Command function signature (matches μEmacs fn_t) */
typedef int (*uemacs_cmd_fn)(int f, int n);

/* Event callback types */
typedef void (*uemacs_buffer_cb)(struct buffer *bp);
typedef int (*uemacs_key_cb)(int key);  /* Return 1 if key consumed, 0 to pass through */
typedef void (*uemacs_idle_cb)(void);

/*
 * Character transform callback for smart typography extensions.
 * Called during self-insert before the character is inserted.
 *
 * Parameters:
 *   c   - The character being inserted
 *   out - Output: transformed Unicode codepoint (only used if return != 0)
 *
 * Returns:
 *   0  - No transform, insert original character
 *   1  - Use *out instead of c
 *  -1  - Delete previous char, then insert *out (for ligatures like -- → —)
 */
typedef int (*uemacs_char_transform_cb)(int c, int *out);

/* The Editor API - passed to extensions on init */
struct uemacs_api {
    /* API version for compatibility checking */
    int api_version;

    /*
     * Command Registration
     * Extensions can register new M-x commands
     */
    int (*register_command)(const char *name, uemacs_cmd_fn func);
    int (*unregister_command)(const char *name);

    /*
     * Event Hooks
     * Extensions can subscribe to editor events
     */
    int (*on_buffer_save)(uemacs_buffer_cb cb);
    int (*on_buffer_load)(uemacs_buffer_cb cb);
    int (*on_key)(uemacs_key_cb cb);
    int (*on_idle)(uemacs_idle_cb cb);
    int (*on_char_transform)(uemacs_char_transform_cb cb);

    /* Remove event hooks */
    int (*off_buffer_save)(uemacs_buffer_cb cb);
    int (*off_buffer_load)(uemacs_buffer_cb cb);
    int (*off_key)(uemacs_key_cb cb);
    int (*off_idle)(uemacs_idle_cb cb);
    int (*off_char_transform)(uemacs_char_transform_cb cb);

    /*
     * Buffer Operations
     */
    struct buffer *(*current_buffer)(void);
    struct buffer *(*find_buffer)(const char *name);
    char *(*buffer_contents)(struct buffer *bp, size_t *len);  /* Caller must free */
    const char *(*buffer_filename)(struct buffer *bp);
    const char *(*buffer_name)(struct buffer *bp);
    bool (*buffer_modified)(struct buffer *bp);
    int (*buffer_insert)(const char *text, size_t len);
    int (*buffer_insert_at)(struct buffer *bp, int line, int col,
                            const char *text, size_t len);
    struct buffer *(*buffer_create)(const char *name);  /* Create or find buffer */
    int (*buffer_switch)(struct buffer *bp);            /* Switch to buffer */
    int (*buffer_clear)(struct buffer *bp);             /* Clear buffer contents */

    /*
     * Cursor/Point Operations
     */
    void (*get_point)(int *line, int *col);
    void (*set_point)(int line, int col);
    int (*get_line_count)(struct buffer *bp);
    char *(*get_word_at_point)(void);                   /* Caller must free */
    char *(*get_current_line)(void);                    /* Caller must free */

    /*
     * Window Operations
     */
    struct window *(*current_window)(void);
    int (*window_count)(void);
    int (*window_set_wrap_col)(struct window *wp, int col);  /* Set soft wrap column (0=disable) */

    /*
     * User Interface
     */
    void (*message)(const char *fmt, ...);      /* Display in message line */
    void (*vmessage)(const char *fmt, va_list ap);
    int (*prompt)(const char *prompt, char *buf, size_t buflen);  /* Minibuffer input */
    int (*prompt_yn)(const char *prompt);       /* Yes/no prompt, returns 1=yes, 0=no */
    void (*update_display)(void);               /* Force screen refresh */

    /*
     * File Operations
     */
    int (*find_file_line)(const char *path, int line);  /* Open file at line */

    /*
     * Shell Integration
     */
    int (*shell_command)(const char *cmd, char **output, size_t *len);  /* Caller must free output */

    /*
     * Memory Helpers (use μEmacs allocator)
     */
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    char *(*strdup)(const char *s);

    /*
     * Logging (uses μEmacs logging system)
     */
    void (*log_info)(const char *fmt, ...);
    void (*log_warn)(const char *fmt, ...);
    void (*log_error)(const char *fmt, ...);
    void (*log_debug)(const char *fmt, ...);
};

/* Get the global API instance (for use by extensions) */
struct uemacs_api *uemacs_get_api(void);

#endif /* UEMACS_EXTENSION_API_H */

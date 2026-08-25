/*
 * event_bus.h - Generic Event Bus for μEmacs Extension System
 *
 * Provides a flexible, string-based event system that replaces
 * the static hook arrays. Extensions register for events by name,
 * and core emits events at well-defined points.
 *
 * C23 compliant
 */

#ifndef UEMACS_EVENT_BUS_H
#define UEMACS_EVENT_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Event Data Structure */

/*
 * Generic event structure passed to all handlers.
 * The 'data' pointer is event-specific (cast based on event_name).
 */
typedef struct uemacs_event {
    const char *name;       /* Event name (e.g., "input:mouse") */
    void *data;             /* Event-specific data */
    size_t data_size;       /* Size of data (0 if pointer-only) */
    bool consumed;          /* Set to true to stop propagation */
} uemacs_event_t;

/*
 * Event handler callback signature.
 * Returns true if event was consumed (stops propagation).
 */
typedef bool (*event_handler_fn)(uemacs_event_t *event, void *user_data);

/* Standard Event Names */

/* Input events */
#define EVT_INPUT_RAW       "input:raw"         /* Raw bytes before parsing */
#define EVT_INPUT_KEY       "input:key"         /* Parsed key event */
#define EVT_INPUT_MOUSE     "input:mouse"       /* Mouse event (subset of key) */

/* Buffer events */
#define EVT_BUFFER_CREATE   "buffer:create"     /* New buffer created */
#define EVT_BUFFER_DESTROY  "buffer:destroy"    /* Buffer about to be destroyed */
#define EVT_BUFFER_LOAD     "buffer:load"       /* File loaded into buffer */
#define EVT_BUFFER_SAVE_PRE "buffer:save:before" /* Before saving */
#define EVT_BUFFER_SAVE     "buffer:save:after" /* After saving */
#define EVT_BUFFER_SWITCH   "buffer:switch"     /* Active buffer changed */
#define EVT_BUFFER_MODIFIED "buffer:modified"   /* Buffer content changed */

/* Cursor/point events */
#define EVT_CURSOR_MOVE     "cursor:move"       /* Cursor position changed */

/* Display events */
#define EVT_DISPLAY_PRE     "display:pre"       /* Before screen update */
#define EVT_DISPLAY_POST    "display:post"      /* After screen update */
#define EVT_DISPLAY_LINE    "display:line"      /* Before rendering each line */
#define EVT_DISPLAY_GUTTER  "display:gutter"    /* Before rendering gutter */

/* Editor events */
#define EVT_IDLE            "idle"              /* No input for idle period */
#define EVT_CHAR_INSERT     "char:insert"       /* Character about to insert */
#define EVT_CONFIG_CHANGED  "config:changed"    /* Settings reloaded */
#define EVT_STARTUP         "startup"           /* Editor starting up */
#define EVT_SHUTDOWN        "shutdown"          /* Editor shutting down */

/* Window events */
#define EVT_WINDOW_RESIZE   "window:resize"     /* Terminal resized */
#define EVT_WINDOW_SPLIT    "window:split"      /* Window split */
#define EVT_WINDOW_CLOSE    "window:close"      /* Window closed */

/* Event Bus API */

/*
 * Initialize the event bus.
 * Called once at startup.
 */
void event_bus_init(void);

/*
 * Shutdown the event bus.
 * Frees all registered handlers.
 */
void event_bus_shutdown(void);

/*
 * Register an event handler.
 *
 * event:     Event name to listen for (e.g., "input:mouse")
 * handler:   Callback function
 * user_data: Opaque pointer passed to handler
 * priority:  Execution order (-100 = first, 0 = normal, 100 = last)
 *
 * Returns: 0 on success, -1 on error
 */
int event_bus_on(const char *event, event_handler_fn handler,
                 void *user_data, int priority);

/*
 * Unregister an event handler.
 *
 * event:   Event name
 * handler: The callback to remove
 *
 * Returns: 0 on success, -1 if not found
 */
int event_bus_off(const char *event, event_handler_fn handler);

/*
 * Emit an event to all registered handlers.
 *
 * event:     Event name
 * data:      Event-specific data (can be nullptr)
 * data_size: Size of data (0 if not applicable)
 *
 * Returns: true if any handler consumed the event, false otherwise
 */
bool event_bus_emit(const char *event, void *data, size_t data_size);

/*
 * Emit an event with simple pointer data.
 * Convenience wrapper for event_bus_emit(event, data, 0).
 */
static inline bool event_bus_emit_ptr(const char *event, void *data) {
    return event_bus_emit(event, data, 0);
}

/*
 * Check if any handlers are registered for an event.
 *
 * Returns: Number of handlers registered for this event
 */
int event_bus_handler_count(const char *event);

/*
 * Get list of all registered event names.
 * Caller must free the returned array (but not the strings).
 *
 * count: Output parameter for number of events
 *
 * Returns: Array of event name strings, or nullptr if none
 */
const char **event_bus_list_events(int *count);

/* Event Data Helpers */

/*
 * Common event data structures.
 * Cast event->data to the appropriate type based on event->name.
 */

/* For EVT_CHAR_INSERT */
typedef struct {
    int character;          /* Character to insert (Unicode codepoint) */
    int transformed;        /* Output: transformed character (if modified) */
    bool cancel;            /* Set to true to prevent insertion */
} event_char_insert_t;

/* For EVT_CURSOR_MOVE */
typedef struct {
    int old_line, old_col;
    int new_line, new_col;
} event_cursor_move_t;

/* For EVT_WINDOW_RESIZE */
typedef struct {
    int old_rows, old_cols;
    int new_rows, new_cols;
} event_window_resize_t;

/* For EVT_DISPLAY_LINE - generic display hook for folding, narrowing, etc. */
typedef enum {
    DISPLAY_RENDER,         /* Render line normally (default) */
    DISPLAY_SKIP,           /* Don't render this line (folding/narrowing) */
    DISPLAY_SUBSTITUTE,     /* Replace with substitute text */
} display_line_action_t;

typedef struct {
    struct buffer *buffer;          /* Buffer being displayed */
    struct line *line;              /* Line about to be rendered */
    int line_num;                   /* 0-based line number in buffer */
    int screen_row;                 /* Screen row where it would render */

    /* Extension response fields (modify these) */
    display_line_action_t action;   /* What to do with this line */
    const char *substitute;         /* Text if action == DISPLAY_SUBSTITUTE */
    int substitute_face;            /* Syntax face for substitute text */
} event_display_line_t;

/*
 * Fast-path helper: Check if any handlers exist for an event.
 * Use this before constructing expensive event data.
 */
static inline bool event_bus_has_handlers(const char *event) {
    return event_bus_handler_count(event) > 0;
}

#endif /* UEMACS_EVENT_BUS_H */

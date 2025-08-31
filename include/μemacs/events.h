// events.h - Event-driven architecture for modern responsiveness
// Replaces polling with asynchronous event handling

#ifndef UEMACS_EVENTS_H
#define UEMACS_EVENTS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

// Forward declarations
struct buffer;
struct window;

// Event types
typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_PRESS,        // Keyboard input
    EVENT_MOUSE_CLICK,      // Mouse events
    EVENT_MOUSE_MOVE,
    EVENT_WINDOW_RESIZE,    // Terminal size change
    EVENT_BUFFER_CHANGE,    // Text modification
    EVENT_CURSOR_MOVE,      // Cursor position change
    EVENT_MODE_CHANGE,      // Editor mode switch
    EVENT_FILE_WATCH,       // External file modification
    EVENT_TIMER,            // Timer expiration
    EVENT_SIGNAL,           // System signal
    EVENT_CUSTOM,           // User-defined events
    EVENT_MAX
} event_type_t;

// Event priority levels
typedef enum {
    EVENT_PRIORITY_LOW = 0,
    EVENT_PRIORITY_NORMAL = 1,
    EVENT_PRIORITY_HIGH = 2,
    EVENT_PRIORITY_CRITICAL = 3
} event_priority_t;

// Key event data
struct key_event_data {
    uint32_t keycode;       // Key code with modifiers
    uint8_t modifiers;      // Ctrl, Alt, Shift flags
    char utf8_seq[8];       // UTF-8 sequence
    size_t utf8_len;        // Sequence length
};

// Mouse event data
struct mouse_event_data {
    int x, y;               // Screen coordinates
    uint8_t buttons;        // Button state
    uint8_t action;         // Click, release, drag
};

// Window resize data
struct resize_event_data {
    int new_width;
    int new_height;
    int old_width;
    int old_height;
};

// Buffer change data
struct buffer_change_data {
    struct buffer *bp;      // Modified buffer
    size_t offset;          // Change position
    size_t old_len;         // Length before change
    size_t new_len;         // Length after change
    char *old_text;         // Original text (if saved)
    char *new_text;         // New text (if saved)
};

// Cursor move data
struct cursor_move_data {
    struct window *wp;      // Window with cursor
    size_t old_line;        // Previous line number
    size_t old_col;         // Previous column
    size_t new_line;        // New line number
    size_t new_col;         // New column
};

// Timer event data
struct timer_event_data {
    uint64_t timer_id;      // Timer identifier
    uint64_t interval_ms;   // Timer interval
    bool repeating;         // One-shot or repeating
};

// Generic event structure
struct event {
    event_type_t type;              // Event type
    event_priority_t priority;      // Processing priority
    uint64_t timestamp_ns;          // Nanosecond timestamp
    uint64_t sequence;              // Global sequence number
    
    union {
        struct key_event_data key;
        struct mouse_event_data mouse;
        struct resize_event_data resize;
        struct buffer_change_data buffer;
        struct cursor_move_data cursor;
        struct timer_event_data timer;
        void *custom_data;          // Custom event data
    } data;
    
    // Event metadata
    bool consumed;                  // Event processed flag
    struct event *next;             // Queue linkage
};

// Event handler function type
typedef int (*event_handler_fn)(struct event *evt, void *user_data);

// Event handler registration
struct event_handler {
    event_type_t type;              // Event type to handle
    event_priority_t min_priority;  // Minimum priority to process
    event_handler_fn handler;       // Handler function
    void *user_data;               // User data pointer
    bool active;                   // Handler enabled flag
    struct event_handler *next;     // Handler chain
};

// Event queue with priority scheduling
struct event_queue {
    struct event *head[EVENT_PRIORITY_CRITICAL + 1]; // Priority queues
    struct event *tail[EVENT_PRIORITY_CRITICAL + 1];
    _Atomic size_t count;           // Total events queued
    _Atomic size_t dropped;         // Events dropped due to overflow
    size_t max_size;               // Maximum queue size
    _Atomic uint64_t sequence;      // Global sequence counter
};

// Timer management
struct timer {
    uint64_t id;                   // Unique timer ID
    uint64_t interval_ms;          // Interval in milliseconds
    uint64_t next_fire_ns;         // Next expiration time
    bool repeating;                // Repeat flag
    bool active;                   // Timer active
    event_handler_fn callback;     // Timer callback
    void *user_data;              // Callback data
    struct timer *next;           // Timer list linkage
};

// Event system state
struct event_system {
    struct event_queue queue;       // Main event queue
    struct event_handler *handlers[EVENT_MAX]; // Handler chains
    struct timer *timers;          // Active timers
    _Atomic uint64_t timer_id_seq; // Timer ID sequence
    bool running;                  // System active flag
    bool shutdown_requested;       // Shutdown flag
    
    // Performance statistics
    _Atomic size_t events_processed;
    _Atomic size_t events_dropped;
    _Atomic size_t handlers_called;
    _Atomic uint64_t processing_time_ns;
};

// Global event system
extern struct event_system *global_event_system;

// Event system management
int event_system_init(size_t max_queue_size);
void event_system_shutdown(void);
int event_system_run(void);
void event_system_stop(void);

// Event queue operations
int event_queue_push(struct event *evt);
struct event *event_queue_pop(void);
struct event *event_queue_peek(void);
void event_queue_clear(void);
size_t event_queue_size(void);

// Event creation and management
struct event *event_create(event_type_t type, event_priority_t priority);
void event_destroy(struct event *evt);
int event_post(event_type_t type, event_priority_t priority, void *data);
int event_post_immediate(event_type_t type, void *data);

// Specific event posting functions
int event_post_key(uint32_t keycode, uint8_t modifiers, const char *utf8_seq, size_t utf8_len);
int event_post_mouse(int x, int y, uint8_t buttons, uint8_t action);
int event_post_resize(int new_width, int new_height, int old_width, int old_height);
int event_post_buffer_change(struct buffer *bp, size_t offset, size_t old_len, size_t new_len);
int event_post_cursor_move(struct window *wp, size_t old_line, size_t old_col, size_t new_line, size_t new_col);

// Event handler management
int event_handler_register(event_type_t type, event_priority_t min_priority,
                          event_handler_fn handler, void *user_data);
int event_handler_unregister(event_type_t type, event_handler_fn handler);
void event_handler_enable(event_type_t type, event_handler_fn handler, bool enabled);

// Timer management
uint64_t uemacs_timer_create(uint64_t interval_ms, bool repeating, event_handler_fn callback, void *user_data);
int uemacs_timer_destroy(uint64_t timer_id);
int uemacs_timer_start(uint64_t timer_id);
int uemacs_timer_stop(uint64_t timer_id);
int uemacs_timer_reset(uint64_t timer_id);
void timer_process(void);

// Event processing
int event_process_one(void);
int event_process_all(void);
int event_process_with_timeout(uint64_t timeout_ms);

// Event filtering and routing
bool event_should_process(struct event *evt);
int event_dispatch(struct event *evt);

// Utility functions
uint64_t get_current_time_ns(void);
const char *event_type_name(event_type_t type);
const char *event_priority_name(event_priority_t priority);

// Performance monitoring
struct event_stats {
    _Atomic size_t total_events;
    _Atomic size_t events_by_type[EVENT_MAX];
    _Atomic size_t events_by_priority[EVENT_PRIORITY_CRITICAL + 1];
    _Atomic size_t queue_overflows;
    _Atomic size_t processing_errors;
    _Atomic uint64_t avg_processing_time_ns;
    _Atomic uint64_t peak_queue_size;
};

extern struct event_stats global_event_stats;

void event_stats_reset(void);
void event_stats_update(event_type_t type, event_priority_t priority, uint64_t processing_time_ns);

// Debug support
#ifdef DEBUG
void event_dump_queue(void);
void event_dump_handlers(void);
void event_dump_timers(void);
void event_dump_stats(void);
void event_validate_queue(void);
#endif

// Error codes
#define EVENT_SUCCESS          0
#define EVENT_ERROR           -1
#define EVENT_OUT_OF_MEMORY   -2
#define EVENT_INVALID_PARAM   -3
#define EVENT_QUEUE_FULL      -4
#define EVENT_NOT_FOUND       -5
#define EVENT_TIMEOUT         -6

// Configuration constants
#define EVENT_QUEUE_DEFAULT_SIZE    1024
#define EVENT_QUEUE_MAX_SIZE        16384
#define EVENT_HANDLER_MAX_CHAIN     64
#define EVENT_TIMER_MAX_ACTIVE      256
#define EVENT_PROCESSING_TIMEOUT_MS 100

#endif // UEMACS_EVENTS_H
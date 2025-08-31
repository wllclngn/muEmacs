// events.c - Event-driven architecture implementation

#include "μemacs/events.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"

// Global event system and statistics
struct event_system *global_event_system = NULL;
struct event_stats global_event_stats = {0};

// Event type names for debugging
static const char *event_type_names[EVENT_MAX] = {
    "NONE", "KEY_PRESS", "MOUSE_CLICK", "MOUSE_MOVE", "WINDOW_RESIZE",
    "BUFFER_CHANGE", "CURSOR_MOVE", "MODE_CHANGE", "FILE_WATCH",
    "TIMER", "SIGNAL", "CUSTOM"
};

static const char *event_priority_names[] = {
    "LOW", "NORMAL", "HIGH", "CRITICAL"
};

// Get current time in nanoseconds
uint64_t get_current_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Initialize event system
int event_system_init(size_t max_queue_size) {
    if (global_event_system) {
        return EVENT_SUCCESS; // Already initialized
    }
    
    global_event_system = safe_alloc(sizeof(struct event_system),
                                    "event system", __FILE__, __LINE__);
    if (!global_event_system) {
        return EVENT_OUT_OF_MEMORY;
    }
    
    // Initialize event queue
    memset(global_event_system->queue.head, 0, sizeof(global_event_system->queue.head));
    memset(global_event_system->queue.tail, 0, sizeof(global_event_system->queue.tail));
    atomic_init(&global_event_system->queue.count, 0);
    atomic_init(&global_event_system->queue.dropped, 0);
    atomic_init(&global_event_system->queue.sequence, 0);
    global_event_system->queue.max_size = max_queue_size ? max_queue_size : EVENT_QUEUE_DEFAULT_SIZE;
    
    // Initialize handler chains
    memset(global_event_system->handlers, 0, sizeof(global_event_system->handlers));
    
    // Initialize timer system
    global_event_system->timers = NULL;
    atomic_init(&global_event_system->timer_id_seq, 1);
    
    // Initialize state
    global_event_system->running = false;
    global_event_system->shutdown_requested = false;
    
    // Initialize statistics
    atomic_init(&global_event_system->events_processed, 0);
    atomic_init(&global_event_system->events_dropped, 0);
    atomic_init(&global_event_system->handlers_called, 0);
    atomic_init(&global_event_system->processing_time_ns, 0);
    
    return EVENT_SUCCESS;
}

// Shutdown event system
void event_system_shutdown(void) {
    if (!global_event_system) return;
    
    global_event_system->shutdown_requested = true;
    global_event_system->running = false;
    
    // Clear event queue
    event_queue_clear();
    
    // Free handler chains
    for (int i = 0; i < EVENT_MAX; i++) {
        struct event_handler *handler = global_event_system->handlers[i];
        while (handler) {
            struct event_handler *next = handler->next;
            SAFE_FREE(handler);
            handler = next;
        }
        global_event_system->handlers[i] = NULL;
    }
    
    // Free timers
    struct timer *timer = global_event_system->timers;
    while (timer) {
        struct timer *next = timer->next;
        SAFE_FREE(timer);
        timer = next;
    }
    global_event_system->timers = NULL;
    
    SAFE_FREE(global_event_system);
}

// Create new event
struct event *event_create(event_type_t type, event_priority_t priority) {
    struct event *evt = safe_alloc(sizeof(struct event), "event", __FILE__, __LINE__);
    if (!evt) return NULL;
    
    evt->type = type;
    evt->priority = priority;
    evt->timestamp_ns = get_current_time_ns();
    evt->sequence = atomic_fetch_add(&global_event_system->queue.sequence, 1);
    evt->consumed = false;
    evt->next = NULL;
    
    memset(&evt->data, 0, sizeof(evt->data));
    
    return evt;
}

// Destroy event
void event_destroy(struct event *evt) {
    if (!evt) return;
    
    // Free any allocated data in the event
    if (evt->type == EVENT_BUFFER_CHANGE) {
        SAFE_FREE(evt->data.buffer.old_text);
        SAFE_FREE(evt->data.buffer.new_text);
    }
    
    SAFE_FREE(evt);
}

// Push event to queue
int event_queue_push(struct event *evt) {
    if (!global_event_system || !evt) {
        return EVENT_INVALID_PARAM;
    }
    
    // Check queue size limit
    if (atomic_load(&global_event_system->queue.count) >= global_event_system->queue.max_size) {
        atomic_fetch_add(&global_event_system->queue.dropped, 1);
        atomic_fetch_add(&global_event_stats.queue_overflows, 1);
        event_destroy(evt);
        return EVENT_QUEUE_FULL;
    }
    
    // Add to priority queue
    event_priority_t priority = evt->priority;
    if (priority > EVENT_PRIORITY_CRITICAL) {
        priority = EVENT_PRIORITY_CRITICAL;
    }
    
    if (global_event_system->queue.tail[priority]) {
        global_event_system->queue.tail[priority]->next = evt;
    } else {
        global_event_system->queue.head[priority] = evt;
    }
    global_event_system->queue.tail[priority] = evt;
    
    atomic_fetch_add(&global_event_system->queue.count, 1);
    atomic_fetch_add(&global_event_stats.total_events, 1);
    atomic_fetch_add(&global_event_stats.events_by_type[evt->type], 1);
    atomic_fetch_add(&global_event_stats.events_by_priority[priority], 1);
    
    // Update peak queue size
    size_t current_size = atomic_load(&global_event_system->queue.count);
    size_t peak = atomic_load(&global_event_stats.peak_queue_size);
    if (current_size > peak) {
        atomic_store(&global_event_stats.peak_queue_size, current_size);
    }
    
    return EVENT_SUCCESS;
}

// Pop event from queue (highest priority first)
struct event *event_queue_pop(void) {
    if (!global_event_system) return NULL;
    
    // Check priorities from highest to lowest
    for (int priority = EVENT_PRIORITY_CRITICAL; priority >= EVENT_PRIORITY_LOW; priority--) {
        struct event *evt = global_event_system->queue.head[priority];
        if (evt) {
            global_event_system->queue.head[priority] = evt->next;
            if (!evt->next) {
                global_event_system->queue.tail[priority] = NULL;
            }
            evt->next = NULL;
            atomic_fetch_sub(&global_event_system->queue.count, 1);
            return evt;
        }
    }
    
    return NULL;
}

// Clear event queue
void event_queue_clear(void) {
    if (!global_event_system) return;
    
    struct event *evt;
    while ((evt = event_queue_pop()) != NULL) {
        event_destroy(evt);
    }
}

// Get queue size
size_t event_queue_size(void) {
    return global_event_system ? atomic_load(&global_event_system->queue.count) : 0;
}

// Post generic event
int event_post(event_type_t type, event_priority_t priority, void *data) {
    struct event *evt = event_create(type, priority);
    if (!evt) return EVENT_OUT_OF_MEMORY;
    
    // Copy data based on event type
    switch (type) {
        case EVENT_KEY_PRESS:
            if (data) {
                memcpy(&evt->data.key, data, sizeof(struct key_event_data));
            }
            break;
        case EVENT_MOUSE_CLICK:
        case EVENT_MOUSE_MOVE:
            if (data) {
                memcpy(&evt->data.mouse, data, sizeof(struct mouse_event_data));
            }
            break;
        case EVENT_WINDOW_RESIZE:
            if (data) {
                memcpy(&evt->data.resize, data, sizeof(struct resize_event_data));
            }
            break;
        case EVENT_BUFFER_CHANGE:
            if (data) {
                memcpy(&evt->data.buffer, data, sizeof(struct buffer_change_data));
                // Note: Caller must manage text memory
            }
            break;
        case EVENT_CURSOR_MOVE:
            if (data) {
                memcpy(&evt->data.cursor, data, sizeof(struct cursor_move_data));
            }
            break;
        case EVENT_TIMER:
            if (data) {
                memcpy(&evt->data.timer, data, sizeof(struct timer_event_data));
            }
            break;
        case EVENT_CUSTOM:
            evt->data.custom_data = data;
            break;
        default:
            break;
    }
    
    return event_queue_push(evt);
}

// Post key event
int event_post_key(uint32_t keycode, uint8_t modifiers, const char *utf8_seq, size_t utf8_len) {
    struct key_event_data key_data = {0};
    key_data.keycode = keycode;
    key_data.modifiers = modifiers;
    key_data.utf8_len = utf8_len < sizeof(key_data.utf8_seq) ? utf8_len : sizeof(key_data.utf8_seq) - 1;
    if (utf8_seq && key_data.utf8_len > 0) {
        memcpy(key_data.utf8_seq, utf8_seq, key_data.utf8_len);
        key_data.utf8_seq[key_data.utf8_len] = '\0';
    }
    
    return event_post(EVENT_KEY_PRESS, EVENT_PRIORITY_HIGH, &key_data);
}

// Register event handler
int event_handler_register(event_type_t type, event_priority_t min_priority,
                          event_handler_fn handler, void *user_data) {
    if (!global_event_system || type >= EVENT_MAX || !handler) {
        return EVENT_INVALID_PARAM;
    }
    
    struct event_handler *new_handler = safe_alloc(sizeof(struct event_handler),
                                                  "event handler", __FILE__, __LINE__);
    if (!new_handler) return EVENT_OUT_OF_MEMORY;
    
    new_handler->type = type;
    new_handler->min_priority = min_priority;
    new_handler->handler = handler;
    new_handler->user_data = user_data;
    new_handler->active = true;
    new_handler->next = global_event_system->handlers[type];
    global_event_system->handlers[type] = new_handler;
    
    return EVENT_SUCCESS;
}

// Unregister event handler
int event_handler_unregister(event_type_t type, event_handler_fn handler) {
    if (!global_event_system || type >= EVENT_MAX || !handler) {
        return EVENT_INVALID_PARAM;
    }
    
    struct event_handler **current = &global_event_system->handlers[type];
    while (*current) {
        if ((*current)->handler == handler) {
            struct event_handler *to_remove = *current;
            *current = to_remove->next;
            SAFE_FREE(to_remove);
            return EVENT_SUCCESS;
        }
        current = &(*current)->next;
    }
    
    return EVENT_NOT_FOUND;
}

// Dispatch event to handlers
int event_dispatch(struct event *evt) {
    if (!global_event_system || !evt || evt->type >= EVENT_MAX) {
        return EVENT_INVALID_PARAM;
    }
    
    int handlers_called = 0;
    uint64_t start_time = get_current_time_ns();
    
    struct event_handler *handler = global_event_system->handlers[evt->type];
    while (handler && !evt->consumed) {
        if (handler->active && evt->priority >= handler->min_priority) {
            int result = handler->handler(evt, handler->user_data);
            handlers_called++;
            atomic_fetch_add(&global_event_system->handlers_called, 1);
            
            if (result != EVENT_SUCCESS) {
                atomic_fetch_add(&global_event_stats.processing_errors, 1);
            }
            
            // Handler can mark event as consumed to stop propagation
            if (result == EVENT_SUCCESS && evt->consumed) {
                break;
            }
        }
        handler = handler->next;
    }
    
    uint64_t processing_time = get_current_time_ns() - start_time;
    event_stats_update(evt->type, evt->priority, processing_time);
    
    return handlers_called > 0 ? EVENT_SUCCESS : EVENT_NOT_FOUND;
}

// Process single event
int event_process_one(void) {
    if (!global_event_system) return EVENT_ERROR;
    
    struct event *evt = event_queue_pop();
    if (!evt) return EVENT_NOT_FOUND;
    
    int result = event_dispatch(evt);
    atomic_fetch_add(&global_event_system->events_processed, 1);
    
    event_destroy(evt);
    return result;
}

// Process all pending events
int event_process_all(void) {
    if (!global_event_system) return EVENT_ERROR;
    
    int processed = 0;
    while (event_queue_size() > 0) {
        if (event_process_one() == EVENT_SUCCESS) {
            processed++;
        }
        
        // Prevent infinite loop if events keep getting posted
        if (processed > 10000) break;
    }
    
    return processed;
}

// Update event statistics
void event_stats_update(event_type_t type, event_priority_t priority, uint64_t processing_time_ns) {
    // Update average processing time using exponential moving average
    uint64_t current_avg = atomic_load(&global_event_stats.avg_processing_time_ns);
    uint64_t new_avg = (current_avg * 15 + processing_time_ns) / 16; // Alpha = 1/16
    atomic_store(&global_event_stats.avg_processing_time_ns, new_avg);
}

// Timer creation
uint64_t uemacs_timer_create(uint64_t interval_ms, bool repeating, event_handler_fn callback, void *user_data) {
    if (!global_event_system || !callback) return 0;
    
    struct timer *new_timer = safe_alloc(sizeof(struct timer), "timer", __FILE__, __LINE__);
    if (!new_timer) return 0;
    
    new_timer->id = atomic_fetch_add(&global_event_system->timer_id_seq, 1);
    new_timer->interval_ms = interval_ms;
    new_timer->next_fire_ns = get_current_time_ns() + (interval_ms * 1000000ULL);
    new_timer->repeating = repeating;
    new_timer->active = true;
    new_timer->callback = callback;
    new_timer->user_data = user_data;
    new_timer->next = global_event_system->timers;
    global_event_system->timers = new_timer;
    
    return new_timer->id;
}

// Process timers
void timer_process(void) {
    if (!global_event_system) return;
    
    uint64_t current_time = get_current_time_ns();
    struct timer *timer = global_event_system->timers;
    
    while (timer) {
        if (timer->active && current_time >= timer->next_fire_ns) {
            // Create timer event
            struct timer_event_data timer_data = {
                .timer_id = timer->id,
                .interval_ms = timer->interval_ms,
                .repeating = timer->repeating
            };
            
            // Fire callback directly or post event
            if (timer->callback) {
                struct event evt = {0};
                evt.type = EVENT_TIMER;
                evt.data.timer = timer_data;
                timer->callback(&evt, timer->user_data);
            }
            
            // Reschedule if repeating
            if (timer->repeating) {
                timer->next_fire_ns = current_time + (timer->interval_ms * 1000000ULL);
            } else {
                timer->active = false;
            }
        }
        timer = timer->next;
    }
}

// Utility functions
const char *event_type_name(event_type_t type) {
    return (type < EVENT_MAX) ? event_type_names[type] : "UNKNOWN";
}

const char *event_priority_name(event_priority_t priority) {
    return (priority <= EVENT_PRIORITY_CRITICAL) ? event_priority_names[priority] : "UNKNOWN";
}

void event_dump_stats(void) {
    mlwrite("Event System Statistics:");
    mlwrite("  Total events: %zu", atomic_load(&global_event_stats.total_events));
    mlwrite("  Queue overflows: %zu", atomic_load(&global_event_stats.queue_overflows));
    mlwrite("  Processing errors: %zu", atomic_load(&global_event_stats.processing_errors));
    mlwrite("  Average processing time: %lu ns", atomic_load(&global_event_stats.avg_processing_time_ns));
    mlwrite("  Peak queue size: %zu", atomic_load(&global_event_stats.peak_queue_size));
    
    mlwrite("Events by type:");
    for (int i = 0; i < EVENT_MAX; i++) {
        size_t count = atomic_load(&global_event_stats.events_by_type[i]);
        if (count > 0) {
            mlwrite("  %s: %zu", event_type_name(i), count);
        }
    }
}

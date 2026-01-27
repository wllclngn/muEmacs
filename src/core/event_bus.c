/*
 * event_bus.c - Generic Event Bus Implementation
 *
 * Replaces the static hook arrays with a flexible, string-based
 * event system. Uses a hash table for O(1) event lookup and
 * priority-sorted linked lists for handler chains.
 *
 * C23 compliant
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "internal/event_bus.h"
#include "util/logger.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/* Hash table size (prime for better distribution) */
static constexpr int HASH_TABLE_SIZE = 67;

/* Maximum handlers per event (sanity limit) */
static constexpr int MAX_HANDLERS_PER_EVENT = 64;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Single event handler registration */
typedef struct event_handler {
    event_handler_fn callback;
    void *user_data;
    int priority;
    struct event_handler *next;
} event_handler_t;

/* Event bucket (one per unique event name) */
typedef struct event_bucket {
    char *event_name;
    event_handler_t *handlers;      /* Priority-sorted list */
    int handler_count;
    struct event_bucket *next;      /* Hash collision chain */
} event_bucket_t;

/* Global event bus state */
static struct {
    event_bucket_t *buckets[HASH_TABLE_SIZE];
    int total_handlers;
    int total_events;
    bool initialized;
} g_event_bus = {0};

/* ============================================================================
 * Hash Function
 * ============================================================================ */

/*
 * djb2 hash - fast and good distribution for strings
 */
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  /* hash * 33 + c */
    }
    return hash;
}

static int hash_index(const char *event_name) {
    return (int)(hash_string(event_name) % HASH_TABLE_SIZE);
}

/* ============================================================================
 * Bucket Management
 * ============================================================================ */

/*
 * Find or create a bucket for an event name.
 */
static event_bucket_t *find_or_create_bucket(const char *event_name, bool create) {
    int idx = hash_index(event_name);
    event_bucket_t *bucket = g_event_bus.buckets[idx];

    /* Search collision chain */
    while (bucket) {
        if (strcmp(bucket->event_name, event_name) == 0) {
            return bucket;
        }
        bucket = bucket->next;
    }

    if (!create) {
        return nullptr;
    }

    /* Create new bucket */
    bucket = calloc(1, sizeof(event_bucket_t));
    if (!bucket) {
        LOG_ERROR("event_bus: failed to allocate bucket");
        return nullptr;
    }

    bucket->event_name = strdup(event_name);
    if (!bucket->event_name) {
        free(bucket);
        LOG_ERROR("event_bus: failed to duplicate event name");
        return nullptr;
    }

    /* Insert at head of collision chain */
    bucket->next = g_event_bus.buckets[idx];
    g_event_bus.buckets[idx] = bucket;
    g_event_bus.total_events++;

    LOG_DEBUGF("event_bus: created bucket for '%s'", event_name);
    return bucket;
}

/*
 * Free a bucket and all its handlers.
 */
static void free_bucket(event_bucket_t *bucket) {
    if (!bucket) return;

    event_handler_t *handler = bucket->handlers;
    while (handler) {
        event_handler_t *next = handler->next;
        free(handler);
        handler = next;
    }

    free(bucket->event_name);
    free(bucket);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

void event_bus_init(void) {
    if (g_event_bus.initialized) {
        LOG_WARN("event_bus: already initialized");
        return;
    }

    memset(&g_event_bus, 0, sizeof(g_event_bus));
    g_event_bus.initialized = true;

    LOG_INFO("event_bus: initialized");
}

void event_bus_shutdown(void) {
    if (!g_event_bus.initialized) {
        return;
    }

    /* Free all buckets */
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        event_bucket_t *bucket = g_event_bus.buckets[i];
        while (bucket) {
            event_bucket_t *next = bucket->next;
            free_bucket(bucket);
            bucket = next;
        }
        g_event_bus.buckets[i] = nullptr;
    }

    int events = g_event_bus.total_events;
    int handlers = g_event_bus.total_handlers;

    g_event_bus.total_handlers = 0;
    g_event_bus.total_events = 0;
    g_event_bus.initialized = false;

    LOG_INFOF("event_bus: shutdown (freed %d events, %d handlers)", events, handlers);
}

int event_bus_on(const char *event, event_handler_fn handler,
                 void *user_data, int priority) {
    if (!event || !handler) {
        return -1;
    }

    if (!g_event_bus.initialized) {
        /* Auto-initialize */
        memset(&g_event_bus, 0, sizeof(g_event_bus));
        g_event_bus.initialized = true;
        LOG_INFO("event_bus: auto-initialized");
    }

    event_bucket_t *bucket = find_or_create_bucket(event, true);
    if (!bucket) {
        return -1;
    }

    if (bucket->handler_count >= MAX_HANDLERS_PER_EVENT) {
        LOG_ERRORF("event_bus: max handlers reached for '%s'", event);
        return -1;
    }

    /* Check for duplicate registration */
    event_handler_t *existing = bucket->handlers;
    while (existing) {
        if (existing->callback == handler && existing->user_data == user_data) {
            LOG_WARNF("event_bus: handler already registered for '%s'", event);
            return -1;
        }
        existing = existing->next;
    }

    /* Create new handler */
    event_handler_t *new_handler = calloc(1, sizeof(event_handler_t));
    if (!new_handler) {
        LOG_ERROR("event_bus: failed to allocate handler");
        return -1;
    }

    new_handler->callback = handler;
    new_handler->user_data = user_data;
    new_handler->priority = priority;

    /* Insert in priority order (lower priority = earlier execution) */
    event_handler_t **pp = &bucket->handlers;
    while (*pp && (*pp)->priority <= priority) {
        pp = &(*pp)->next;
    }
    new_handler->next = *pp;
    *pp = new_handler;

    bucket->handler_count++;
    g_event_bus.total_handlers++;

    LOG_DEBUGF("event_bus: registered handler for '%s' (priority %d)",
               event, priority);
    return 0;
}

int event_bus_off(const char *event, event_handler_fn handler) {
    if (!event || !handler) {
        return -1;
    }

    if (!g_event_bus.initialized) {
        return -1;
    }

    event_bucket_t *bucket = find_or_create_bucket(event, false);
    if (!bucket) {
        return -1;
    }

    event_handler_t **pp = &bucket->handlers;
    while (*pp) {
        if ((*pp)->callback == handler) {
            event_handler_t *to_remove = *pp;
            *pp = to_remove->next;
            free(to_remove);

            bucket->handler_count--;
            g_event_bus.total_handlers--;

            LOG_DEBUGF("event_bus: unregistered handler for '%s'", event);
            return 0;
        }
        pp = &(*pp)->next;
    }

    LOG_WARNF("event_bus: handler not found for '%s'", event);
    return -1;
}

bool event_bus_emit(const char *event, void *data, size_t data_size) {
    if (!event) {
        return false;
    }

    if (!g_event_bus.initialized) {
        return false;
    }

    event_bucket_t *bucket = find_or_create_bucket(event, false);
    if (!bucket || !bucket->handlers) {
        return false;  /* No handlers registered */
    }

    /* Build event structure */
    uemacs_event_t evt = {
        .name = event,
        .data = data,
        .data_size = data_size,
        .consumed = false,
    };

    /* Call handlers in priority order */
    event_handler_t *handler = bucket->handlers;
    while (handler) {
        bool consumed = handler->callback(&evt, handler->user_data);

        if (consumed || evt.consumed) {
            LOG_DEBUGF("event_bus: '%s' consumed by handler", event);
            return true;
        }

        handler = handler->next;
    }

    return false;
}

int event_bus_handler_count(const char *event) {
    if (!event || !g_event_bus.initialized) {
        return 0;
    }

    event_bucket_t *bucket = find_or_create_bucket(event, false);
    return bucket ? bucket->handler_count : 0;
}

const char **event_bus_list_events(int *count) {
    if (!g_event_bus.initialized || g_event_bus.total_events == 0) {
        if (count) *count = 0;
        return nullptr;
    }

    const char **events = calloc(g_event_bus.total_events, sizeof(char *));
    if (!events) {
        if (count) *count = 0;
        return nullptr;
    }

    int idx = 0;
    for (int i = 0; i < HASH_TABLE_SIZE && idx < g_event_bus.total_events; i++) {
        event_bucket_t *bucket = g_event_bus.buckets[i];
        while (bucket && idx < g_event_bus.total_events) {
            events[idx++] = bucket->event_name;
            bucket = bucket->next;
        }
    }

    if (count) *count = idx;
    return events;
}

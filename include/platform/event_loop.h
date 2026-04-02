/*
 * event_loop.h - Backend-agnostic event loop for μEmacs
 *
 * Provides a unified API for fd monitoring, timers, and signal handling
 * with three-tier backend selection: io_uring > epoll > poll.
 *
 * C23 compliant
 */

#ifndef UEMACS_EVENT_LOOP_H
#define UEMACS_EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EVT_LOOP_POLL   = 0,    /* poll() fallback -- always available */
    EVT_LOOP_EPOLL  = 1,    /* epoll -- Linux 2.6+ */
    EVT_LOOP_URING  = 2,    /* io_uring -- Linux 5.10+ */
} evt_loop_backend_t;

typedef enum {
    EVT_READABLE  = 0x01,
    EVT_WRITABLE  = 0x02,
    EVT_HANGUP    = 0x04,
    EVT_ERROR     = 0x08,
    EVT_TIMER     = 0x10,
    EVT_SIGNAL    = 0x20,
} evt_flags_t;

/* Opaque identifier for registered event sources */
typedef uint64_t evt_token_t;
#define EVT_TOKEN_INVALID ((evt_token_t)-1)

/* Callback for events */
typedef void (*evt_callback_t)(evt_token_t token, evt_flags_t flags, void *userdata);

/* Event loop lifecycle */
int  evt_loop_init(void);
void evt_loop_destroy(void);
evt_loop_backend_t evt_loop_backend(void);
bool evt_loop_available(void);

/* FD registration */
evt_token_t evt_loop_add_fd(int fd, evt_flags_t interest,
                             evt_callback_t cb, void *ud);
void        evt_loop_remove(evt_token_t token);
void        evt_loop_modify(evt_token_t token, evt_flags_t new_interest);

/* Timer registration (creates timerfd internally) */
evt_token_t evt_loop_add_timer_ms(uint64_t initial_ms, uint64_t interval_ms,
                                   evt_callback_t cb, void *ud);
void        evt_loop_cancel_timer(evt_token_t token);

/* Signal registration (creates signalfd internally) */
evt_token_t evt_loop_add_signal(int signum, evt_callback_t cb, void *ud);

/* Main wait -- dispatches callbacks, returns number of events or -1 on error */
int evt_loop_wait(int timeout_ms);

/* Backend vtable (internal, exposed for backend implementations) */
typedef struct evt_loop_ops {
    int  (*init)(void);
    void (*destroy)(void);
    int  (*add_fd)(int slot_idx, int fd, evt_flags_t interest);
    void (*remove_fd)(int slot_idx, int fd);
    void (*modify_fd)(int slot_idx, int fd, evt_flags_t interest);
    int  (*wait)(int timeout_ms);  /* returns number of ready fds */
} evt_loop_ops_t;

/* Backend registration (called from backend init files) */
void evt_loop_set_backend(evt_loop_backend_t type, const evt_loop_ops_t *ops);

/* Maximum number of concurrent event sources */
#define EVT_MAX_SOURCES 64

#endif /* UEMACS_EVENT_LOOP_H */

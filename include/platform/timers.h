/*
 * timers.h - timerfd-based precision timers for μEmacs
 *
 * C23 compliant
 */

#ifndef UEMACS_TIMERS_H
#define UEMACS_TIMERS_H

#include <stdint.h>

typedef enum {
    TIMER_AUTOSAVE        = 0,
    TIMER_CURSOR_BLINK    = 1,
    TIMER_RESIZE_DEBOUNCE = 2,
    TIMER_EXT_TIMEOUT     = 3,
    TIMER_MAX             = 4,
} timer_id_t;

/* Initialize timer subsystem. Creates timerfds. Returns 0 on success. */
int  timers_init(void);

/* Cleanup all timers. */
void timers_cleanup(void);

/* Arm a timer. initial_ms=0 means disarm. interval_ms=0 means one-shot. */
int  timer_arm(timer_id_t id, uint64_t initial_ms, uint64_t interval_ms);

/* Disarm a timer without destroying it. */
int  timer_disarm(timer_id_t id);

/* Get the timerfd for a timer (for adding to event loop). Returns -1 if not created. */
int  timer_get_fd(timer_id_t id);

/* Check if timerfd is available on this system. */
int  timers_available(void);

#endif /* UEMACS_TIMERS_H */

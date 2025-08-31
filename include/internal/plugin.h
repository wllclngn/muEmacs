/*
 * plugin.h - μEmacs plugin/hook API (initial version)
 *
 * Allows external modules to register hooks for editor events.
 * Events: on-save, on-open, on-command (expandable)
 */

#ifndef UEMACS_PLUGIN_H
#define UEMACS_PLUGIN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UEMACS_EVENT_ON_SAVE,
    UEMACS_EVENT_ON_OPEN,
    UEMACS_EVENT_ON_COMMAND,
    UEMACS_EVENT_COUNT
} uemacs_event_t;

typedef void (*uemacs_hook_fn)(uemacs_event_t event, void* context);

/* Register a hook for a given event. Returns true on success. */
bool uemacs_register_hook(uemacs_event_t event, uemacs_hook_fn fn, void* context);

/* Unregister a hook for a given event. */
bool uemacs_unregister_hook(uemacs_event_t event, uemacs_hook_fn fn, void* context);

/* Internal: call all hooks for an event. */
void uemacs_invoke_hooks(uemacs_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* UEMACS_PLUGIN_H */

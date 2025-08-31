/*
 * plugin.c - μEmacs plugin/hook API implementation (initial version)
 *
 * Allows external modules to register hooks for editor events.
 */

#include "internal/plugin.h"
#include <stddef.h>

#define MAX_HOOKS_PER_EVENT 8

typedef struct {
    uemacs_hook_fn fn;
    void* context;
} hook_entry_t;

static hook_entry_t hooks[UEMACS_EVENT_COUNT][MAX_HOOKS_PER_EVENT];

bool uemacs_register_hook(uemacs_event_t event, uemacs_hook_fn fn, void* context) {
    if (event < 0 || event >= UEMACS_EVENT_COUNT || !fn) return false;
    for (int i = 0; i < MAX_HOOKS_PER_EVENT; ++i) {
        if (!hooks[event][i].fn) {
            hooks[event][i].fn = fn;
            hooks[event][i].context = context;
            return true;
        }
    }
    return false; // No space
}

bool uemacs_unregister_hook(uemacs_event_t event, uemacs_hook_fn fn, void* context) {
    if (event < 0 || event >= UEMACS_EVENT_COUNT || !fn) return false;
    for (int i = 0; i < MAX_HOOKS_PER_EVENT; ++i) {
        if (hooks[event][i].fn == fn && hooks[event][i].context == context) {
            hooks[event][i].fn = NULL;
            hooks[event][i].context = NULL;
            return true;
        }
    }
    return false;
}

void uemacs_invoke_hooks(uemacs_event_t event) {
    if (event < 0 || event >= UEMACS_EVENT_COUNT) return;
    for (int i = 0; i < MAX_HOOKS_PER_EVENT; ++i) {
        if (hooks[event][i].fn) {
            hooks[event][i].fn(event, hooks[event][i].context);
        }
    }
}

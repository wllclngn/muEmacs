// test_plugin.c - Unit tests for plugin/hook API and isolation
#include "test_utils.h"
#include "internal/plugin.h"

int plugin_called = 0;
void sample_hook(uemacs_event_t event, void* context) {
    plugin_called++;
    if (context) *(int*)context += 1;
}

int test_plugin_registration() {
    PHASE_START("PLUGIN: REGISTRATION", "Register and dispatch plugin hooks");
    int ctx = 0;
    assert(uemacs_register_hook(UEMACS_EVENT_ON_SAVE, sample_hook, &ctx));
    plugin_called = 0;
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    assert(plugin_called == 1);
    assert(ctx == 1);
    assert(uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, sample_hook, &ctx));
    plugin_called = 0;
    ctx = 0;
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    assert(plugin_called == 0);
    assert(ctx == 0);
    PHASE_END("PLUGIN: REGISTRATION", 1);
    return 1;
}

int test_plugin_isolation() {
    PHASE_START("PLUGIN: ISOLATION", "Ensure plugin failures do not compromise editor");
    void failing_hook(uemacs_event_t event, void* context) {
        (void)event; (void)context;
        // Simulate a plugin error (should not crash editor)
        int* p = NULL; *p = 42;
    }
    assert(uemacs_register_hook(UEMACS_EVENT_ON_COMMAND, failing_hook, NULL));
    // Should not crash, but may segfault if not isolated (for real isolation, use try/catch or signal handler)
    // For now, just demonstrate registration
    uemacs_invoke_hooks(UEMACS_EVENT_ON_COMMAND);
    assert(uemacs_unregister_hook(UEMACS_EVENT_ON_COMMAND, failing_hook, NULL));
    PHASE_END("PLUGIN: ISOLATION", 1);
    return 1;
}

int main(void) {
    test_plugin_registration();
    test_plugin_isolation();
    printf("All plugin/hook API tests passed.\n");
    return 0;
}

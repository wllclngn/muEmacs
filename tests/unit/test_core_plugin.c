// test_core_plugin.c - Unit tests for plugin/hook system
// Tests src/core/plugin.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "internal/plugin.h"

// Test hook invocation counters
static int hook1_count = 0;
static int hook2_count = 0;
static int hook3_count = 0;

// Test hook functions
static void test_hook1(uemacs_event_t event, void* context) {
    (void)event;
    (void)context;
    hook1_count++;
}

static void test_hook2(uemacs_event_t event, void* context) {
    (void)event;
    int *ctx = (int*)context;
    if (ctx) (*ctx)++;
    hook2_count++;
}

static void test_hook3(uemacs_event_t event, void* context) {
    (void)event;
    (void)context;
    hook3_count++;
}

// Reset hook counters
static void reset_hook_counters(void) {
    hook1_count = 0;
    hook2_count = 0;
    hook3_count = 0;
}

// Test basic hook registration
static int test_plugin_register_basic(void) {
    int ok = 1;
    PHASE_START("PLUGIN: REGISTER", "Basic hook registration");

    reset_hook_counters();

    // Register first hook
    if (!uemacs_register_hook(UEMACS_EVENT_ON_SAVE, test_hook1, NULL)) {
        LOG_ERROR("[FAIL] Failed to register first hook");
        ok = 0;
    }

    // Invoke hooks and verify
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    if (hook1_count != 1) {
        LOG_ERRORF("[FAIL] Hook1 not invoked (count=%d, expected 1)", hook1_count);
        ok = 0;
    }

    // Register second hook to same event
    int context_val = 0;
    if (!uemacs_register_hook(UEMACS_EVENT_ON_SAVE, test_hook2, &context_val)) {
        LOG_ERROR("[FAIL] Failed to register second hook");
        ok = 0;
    }

    // Invoke and verify both hooks called
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    if (hook1_count != 2) {
        LOG_ERRORF("[FAIL] Hook1 second invocation failed (count=%d)", hook1_count);
        ok = 0;
    }
    if (hook2_count != 1 || context_val != 1) {
        LOG_ERROR("[FAIL] Hook2 not invoked or context not passed");
        ok = 0;
    }

    // Cleanup
    uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, test_hook1, NULL);
    uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, test_hook2, &context_val);

    PHASE_END("PLUGIN: REGISTER", ok);
    return ok;
}

// Test MAX_HOOKS_PER_EVENT saturation (8 hooks)
static int test_plugin_max_hooks(void) {
    int ok = 1;
    PHASE_START("PLUGIN: MAXHOOKS", "MAX_HOOKS_PER_EVENT saturation");

    reset_hook_counters();

    // Register 8 hooks (MAX_HOOKS_PER_EVENT)
    void *contexts[8];
    for (int i = 0; i < 8; i++) {
        contexts[i] = (void*)(uintptr_t)i;
        if (!uemacs_register_hook(UEMACS_EVENT_ON_COMMAND, test_hook1, contexts[i])) {
            LOG_ERRORF("[FAIL] Failed to register hook %d/8", i + 1);
            ok = 0;
        }
    }

    // Verify all 8 hooks registered
    uemacs_invoke_hooks(UEMACS_EVENT_ON_COMMAND);
    if (hook1_count != 8) {
        LOG_ERRORF("[FAIL] Expected 8 hooks invoked, got %d", hook1_count);
        ok = 0;
    }

    // Cleanup
    for (int i = 0; i < 8; i++) {
        uemacs_unregister_hook(UEMACS_EVENT_ON_COMMAND, test_hook1, contexts[i]);
    }

    PHASE_END("PLUGIN: MAXHOOKS", ok);
    return ok;
}

// Test 9th hook registration fails
static int test_plugin_overflow(void) {
    int ok = 1;
    PHASE_START("PLUGIN: OVERFLOW", "9th hook registration rejection");

    reset_hook_counters();

    // Register 8 hooks (MAX_HOOKS_PER_EVENT)
    // Use test_hook1 which doesn't dereference context (safe with dummy values)
    void *contexts[9];
    for (int i = 0; i < 8; i++) {
        contexts[i] = (void*)(uintptr_t)i;
        if (!uemacs_register_hook(UEMACS_EVENT_ON_OPEN, test_hook1, contexts[i])) {
            LOG_ERRORF("[FAIL] Failed to register hook %d/8", i + 1);
            ok = 0;
        }
    }

    // Try to register 9th hook - should fail
    contexts[8] = (void*)(uintptr_t)8;
    if (uemacs_register_hook(UEMACS_EVENT_ON_OPEN, test_hook3, contexts[8])) {
        LOG_ERROR("[FAIL] 9th hook registration should have failed but succeeded");
        ok = 0;
    }

    // Verify only 8 hooks invoked
    uemacs_invoke_hooks(UEMACS_EVENT_ON_OPEN);
    if (hook1_count != 8) {
        LOG_ERRORF("[FAIL] Expected 8 hooks, got %d", hook1_count);
        ok = 0;
    }
    if (hook3_count != 0) {
        LOG_ERRORF("[FAIL] 9th hook was invoked (should be 0, got %d)", hook3_count);
        ok = 0;
    }

    // Cleanup
    for (int i = 0; i < 8; i++) {
        uemacs_unregister_hook(UEMACS_EVENT_ON_OPEN, test_hook1, contexts[i]);
    }

    PHASE_END("PLUGIN: OVERFLOW", ok);
    return ok;
}

// Test unregister non-existent hook
static int test_plugin_unregister_nonexistent(void) {
    int ok = 1;
    PHASE_START("PLUGIN: UNREGNONEXIST", "Unregister non-existent hook");

    reset_hook_counters();

    // Try to unregister a hook that was never registered
    if (uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, test_hook1, NULL)) {
        LOG_ERROR("[FAIL] Unregister non-existent hook should return false");
        ok = 0;
    }

    // Register a hook with different context
    int context1 = 42;
    if (!uemacs_register_hook(UEMACS_EVENT_ON_SAVE, test_hook1, &context1)) {
        LOG_ERROR("[FAIL] Failed to register hook");
        ok = 0;
    }

    // Try to unregister with wrong context - should fail
    int context2 = 99;
    if (uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, test_hook1, &context2)) {
        LOG_ERROR("[FAIL] Unregister with wrong context should fail");
        ok = 0;
    }

    // Verify hook still registered
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    if (hook1_count != 1) {
        LOG_ERROR("[FAIL] Hook was incorrectly unregistered");
        ok = 0;
    }

    // Unregister with correct context
    if (!uemacs_unregister_hook(UEMACS_EVENT_ON_SAVE, test_hook1, &context1)) {
        LOG_ERROR("[FAIL] Failed to unregister hook with correct context");
        ok = 0;
    }

    // Verify hook no longer called
    uemacs_invoke_hooks(UEMACS_EVENT_ON_SAVE);
    if (hook1_count != 1) {
        LOG_ERROR("[FAIL] Hook still invoked after unregister");
        ok = 0;
    }

    PHASE_END("PLUGIN: UNREGNONEXIST", ok);
    return ok;
}

// Test hook invocation sequence
static int test_plugin_invocation_sequence(void) {
    int ok = 1;
    PHASE_START("PLUGIN: SEQUENCE", "Hook invocation order");

    reset_hook_counters();

    // Register hooks in specific order
    if (!uemacs_register_hook(UEMACS_EVENT_ON_COMMAND, test_hook1, NULL)) {
        LOG_ERROR("[FAIL] Failed to register hook1");
        ok = 0;
    }
    if (!uemacs_register_hook(UEMACS_EVENT_ON_COMMAND, test_hook2, NULL)) {
        LOG_ERROR("[FAIL] Failed to register hook2");
        ok = 0;
    }
    if (!uemacs_register_hook(UEMACS_EVENT_ON_COMMAND, test_hook3, NULL)) {
        LOG_ERROR("[FAIL] Failed to register hook3");
        ok = 0;
    }

    // Invoke all hooks
    uemacs_invoke_hooks(UEMACS_EVENT_ON_COMMAND);

    // Verify all hooks called exactly once
    if (hook1_count != 1 || hook2_count != 1 || hook3_count != 1) {
        LOG_ERRORF("[FAIL] Hook counts incorrect: h1=%d h2=%d h3=%d", hook1_count, hook2_count, hook3_count);
        ok = 0;
    }

    // Invoke again
    uemacs_invoke_hooks(UEMACS_EVENT_ON_COMMAND);

    // Verify all hooks called twice
    if (hook1_count != 2 || hook2_count != 2 || hook3_count != 2) {
        LOG_ERRORF("[FAIL] Second invocation failed: h1=%d h2=%d h3=%d", hook1_count, hook2_count, hook3_count);
        ok = 0;
    }

    // Cleanup
    uemacs_unregister_hook(UEMACS_EVENT_ON_COMMAND, test_hook1, NULL);
    uemacs_unregister_hook(UEMACS_EVENT_ON_COMMAND, test_hook2, NULL);
    uemacs_unregister_hook(UEMACS_EVENT_ON_COMMAND, test_hook3, NULL);

    PHASE_END("PLUGIN: SEQUENCE", ok);
    return ok;
}

// Entry point for plugin tests
int test_core_plugin(void) {
    int result = 1;
    result &= test_plugin_register_basic();
    result &= test_plugin_max_hooks();
    result &= test_plugin_overflow();
    result &= test_plugin_unregister_nonexistent();
    result &= test_plugin_invocation_sequence();
    return result;
}

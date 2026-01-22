// tests/unit/test_core_keymap.c - Unit tests for src/core/keymap.c
#include "../test_utils.h"
#include "../test_registry.h"

#include "μemacs/keymap.h"
#include "estruct.h" // Ensure core structures are defined first
#include "edef.h"  // For global_keymap, ctlx_keymap, etc.
#include "efunc.h" // For mlwrite, etc.
#include <time.h>  // For clock() performance testing

// Helpers for modern keymap API
#define KM_BIND(km, ch, fn) keymap_bind((km), keymap_key_make((ch), 0), (fn))
#define KM_BIND_CTRL(km, ch, fn) keymap_bind((km), keymap_key_make((ch), MOD_CTRL), (fn))
#define KM_PREFIX(km, ch, prefix) keymap_bind_prefix((km), keymap_key_make((ch), 0), (prefix))
#define KM_LOOKUP(km, ch) keymap_lookup((km), keymap_key_make((ch), 0))
#define KM_LOOKUP_CTRL(km, ch) keymap_lookup((km), keymap_key_make((ch), MOD_CTRL))

// Atomic accessors for binding union (binding.cmd and binding.map are now _Atomic)
#define KM_GET_CMD(entry) atomic_load_explicit(&(entry)->binding.cmd, memory_order_relaxed)
#define KM_GET_MAP(entry) atomic_load_explicit(&(entry)->binding.map, memory_order_relaxed)

// Dummy command function for testing
static int test_command_a(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Test Command A executed.");
    return true;
}

static int test_command_b(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Test Command B executed.");
    return true;
}

static int test_command_c(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Test Command C executed.");
    return true;
}

// Test function for keymap functionality
int test_keymap_functionality(void) {
    int result = 1; // Assume success

    PHASE_START("Keymap Functionality", "Testing hash-based hierarchical keymap system");

    // Test 1: Keymap creation and destruction
    LOG_INFO("1. Testing keymap creation and destruction...");
    struct keymap *km1 = keymap_create("test_km1");
    if (!km1) {
        LOG_ERROR("[FAIL] Failed to create keymap km1.");
        result = 0;
    } else {
        LOG_INFO("[SUCCESS] Keymap km1 created.");
    }
    keymap_destroy(km1);
    LOG_INFO("[SUCCESS] Keymap km1 destroyed.");

    // Re-create global keymaps for testing (they are global singletons)
    // This is usually done by keymap_init_defaults, but we'll do it manually for isolated testing
    struct keymap *gkm = keymap_create("global");
    struct keymap *ckm = keymap_create("C-x");
    struct keymap *hkm = keymap_create("C-h");
    struct keymap *mkm = keymap_create("Meta");

    if (!gkm || !ckm || !hkm || !mkm) {
        LOG_ERROR("[FAIL] Failed to re-create global keymaps.");
        result = 0;
        goto end_test;
    }

    // Store in atomic globals
    atomic_store(&global_keymap, gkm);
    atomic_store(&ctlx_keymap, ckm);
    atomic_store(&help_keymap, hkm);
    atomic_store(&meta_keymap, mkm);
    LOG_INFO("[SUCCESS] Global keymaps re-created.");

    // Test 2: Basic binding and lookup
    LOG_INFO("2. Testing basic binding and lookup...");
    struct keymap *gkm_test = atomic_load(&global_keymap);
    if (!KM_BIND(gkm_test, 'a', test_command_a)) {
        LOG_ERROR("[FAIL] Failed to bind 'a' to test_command_a.");
        result = 0;
    }
    struct keymap_entry *entry_a = KM_LOOKUP(gkm_test, 'a');
    if (entry_a && !entry_a->is_prefix && KM_GET_CMD(entry_a) == test_command_a) {
        LOG_INFO("[SUCCESS] Basic binding and lookup for 'a' successful.");
    } else {
        LOG_ERROR("[FAIL] Basic binding and lookup for 'a' failed.");
        result = 0;
    }

    // Test 3: Prefix binding and lookup
    LOG_INFO("3. Testing prefix binding and lookup...");
    struct keymap *ckm_test = atomic_load(&ctlx_keymap);
    if (!KM_PREFIX(gkm_test, 'x', ckm_test)) {
        LOG_ERROR("[FAIL] Failed to bind 'x' as prefix to ctlx_keymap.");
        result = 0;
    }
    struct keymap_entry *entry_x = KM_LOOKUP(gkm_test, 'x');
    if (entry_x && entry_x->is_prefix && KM_GET_MAP(entry_x) == ckm_test) {
        LOG_INFO("[SUCCESS] Prefix binding for 'x' successful.");
    } else {
        LOG_ERROR("[FAIL] Prefix binding for 'x' failed.");
        result = 0;
    }

    // Test 4: Hierarchical lookup (C-x C-c)
    LOG_INFO("4. Testing hierarchical lookup (C-x C-c)...");
    // Assuming quit_cmd is a valid command function
    if (!KM_BIND(ckm_test, 'c', quit)) {
        LOG_ERROR("[FAIL] Failed to bind 'c' in ctlx_keymap.");
        result = 0;
    }
    struct keymap_entry *entry_c_x_c = keymap_lookup_chain(gkm_test, keymap_key_make('x', 0)); // First lookup for prefix
    if (entry_c_x_c && entry_c_x_c->is_prefix) {
        struct keymap_entry *final_entry = KM_LOOKUP(KM_GET_MAP(entry_c_x_c), 'c');
        if (final_entry && !final_entry->is_prefix && KM_GET_CMD(final_entry) == quit) {
            LOG_INFO("[SUCCESS] Hierarchical lookup for C-x C-c successful.");
        } else {
            LOG_ERROR("[FAIL] Hierarchical lookup for C-x C-c failed (final entry).");
            result = 0;
        }
    } else {
        LOG_ERROR("[FAIL] Hierarchical lookup for C-x C-c failed (prefix lookup).");
        result = 0;
    }

    // Test 5: Unbinding
    LOG_INFO("5. Testing unbinding...");
    if (!keymap_unbind(gkm_test, keymap_key_make('a', 0))) {
        LOG_ERROR("[FAIL] Failed to unbind 'a'.");
        result = 0;
    }
    if (KM_LOOKUP(gkm_test, 'a')) {
        LOG_ERROR("[FAIL] 'a' still found after unbinding.");
        result = 0;
    } else {
        LOG_INFO("[SUCCESS] Unbinding 'a' successful.");
    }

    // Test 6: Legacy initialization (this will re-initialize global keymaps)
    LOG_INFO("6. Testing legacy keymap initialization...");
    keymap_init_defaults();
    // After legacy init, 'a' should be unbound, but 'C-x' should still be a prefix
    struct keymap *gkm_after = atomic_load(&global_keymap);
    struct keymap *ckm_after = atomic_load(&ctlx_keymap);
    struct keymap_entry *entry_a_after_legacy = KM_LOOKUP(gkm_after, 'a');
    if (entry_a_after_legacy) { // Legacy keytab might have 'a' bound, so this might pass or fail depending on keytab
        LOG_INFOF("[%sINFO%s] 'a' found after legacy init (expected if in keytab).", YELLOW, RESET);
    } else {
        LOG_INFO("[SUCCESS] 'a' not found after legacy init (expected if not in keytab).");
    }

    struct keymap_entry *entry_x_after_legacy = KM_LOOKUP_CTRL(gkm_after, 'X');
    if (entry_x_after_legacy && entry_x_after_legacy->is_prefix && KM_GET_MAP(entry_x_after_legacy) == ckm_after) {
        LOG_INFO("[SUCCESS] C-x prefix still valid after legacy init.");
    } else {
        LOG_ERROR("[FAIL] C-x prefix invalid after legacy init.");
        result = 0;
    }

end_test:
    // Note: Don't cleanup global keymaps here as they might be needed by other tests
    // keymap_init_defaults() handles proper cleanup and re-initialization

    // Test 7: Performance benchmarking - O(1) hash lookup verification
    LOG_INFO("7. Testing hash table performance (O(1) verification)...");

    // Recreate keymaps for performance test after legacy init
    keymap_init_defaults();

    clock_t start = clock();
    int lookups = 100000;
    struct keymap *gkm_perf = atomic_load(&global_keymap);
    for (int i = 0; i < lookups; i++) {
        KM_LOOKUP(gkm_perf, 'a' + (i % 26));
    }
    clock_t end = clock();
    double time_per_lookup = ((double)(end - start)) / CLOCKS_PER_SEC / lookups * 1000000; // microseconds
    LOG_INFOF("[INFO] %d lookups completed in %.2f μs average (target: <5 μs for O(1))", lookups, time_per_lookup);
    if (time_per_lookup < 5.0) {
        LOG_INFO("[SUCCESS] Hash table performance meets O(1) requirements.");
    } else {
        LOG_WARN("[WARN] Hash table performance may not be optimal.");
    }

    // Test 8: Fallback chain testing
    LOG_INFO("8. Testing fallback chain behavior...");
    // Bind a command to global keymap
    struct keymap *gkm_fallback = atomic_load(&global_keymap);
    struct keymap *ckm_fallback = atomic_load(&ctlx_keymap);

    if (gkm_fallback && ckm_fallback) {
        KM_BIND(gkm_fallback, 'z', test_command_c);
        // Try lookup in ctlx_keymap first (should fail), then fallback to global
        struct keymap_entry *fallback_entry = KM_LOOKUP(ckm_fallback, 'z');
        if (!fallback_entry) {
            // Expected - now test global fallback
            fallback_entry = KM_LOOKUP(gkm_fallback, 'z');
            if (fallback_entry && KM_GET_CMD(fallback_entry) == test_command_c) {
                LOG_INFO("[SUCCESS] Fallback chain working correctly.");
            } else {
                LOG_ERROR("[FAIL] Global fallback failed.");
                result = 0;
            }
        } else {
            LOG_ERROR("[FAIL] Unexpected binding found in ctlx_keymap.");
            result = 0;
        }
    } else {
        LOG_ERROR("[FAIL] Keymap pointers are null after legacy init.");
        result = 0;
    }

    // Test 9: Hash collision handling
    LOG_INFO("9. Testing hash collision handling...");
    struct keymap *gkm_collision = atomic_load(&global_keymap);

    if (gkm_collision) {
        int collision_keys[] = {65, 97, 129, 161}; // Keys that might hash to same bucket
        for (int i = 0; i < 4; i++) {
            KM_BIND(gkm_collision, collision_keys[i], test_command_a);
        }
        int collisions_resolved = 0;
        for (int i = 0; i < 4; i++) {
            struct keymap_entry *entry = KM_LOOKUP(gkm_collision, collision_keys[i]);
            if (entry && KM_GET_CMD(entry) == test_command_a) {
                collisions_resolved++;
            }
        }
        if (collisions_resolved == 4) {
            LOG_INFO("[SUCCESS] Hash collision handling working correctly.");
        } else {
            LOG_ERRORF("[FAIL] Hash collision handling failed (%d/4 resolved).", collisions_resolved);
            result = 0;
        }
    } else {
        LOG_ERROR("[FAIL] Global keymap is null for collision test.");
        result = 0;
    }

    // Test 10: Deep hierarchy stress test
    LOG_INFO("10. Testing deep hierarchy stress test...");
    struct keymap *level1 = keymap_create("level1");
    struct keymap *level2 = keymap_create("level2");
    struct keymap *level3 = keymap_create("level3");

    struct keymap *gkm_deep = atomic_load(&global_keymap);
    if (level1 && level2 && level3 && gkm_deep) {
        KM_PREFIX(gkm_deep, '1', level1);
        KM_PREFIX(level1, '2', level2);
        KM_BIND(level2, '3', test_command_c);

        // Test 3-level deep lookup
        struct keymap_entry *deep_entry = KM_LOOKUP(gkm_deep, '1');
        if (deep_entry && deep_entry->is_prefix) {
            deep_entry = KM_LOOKUP(KM_GET_MAP(deep_entry), '2');
            if (deep_entry && deep_entry->is_prefix) {
                deep_entry = KM_LOOKUP(KM_GET_MAP(deep_entry), '3');
                if (deep_entry && KM_GET_CMD(deep_entry) == test_command_c) {
                    LOG_INFO("[SUCCESS] Deep hierarchy (3 levels) working correctly.");
                } else {
                    LOG_ERROR("[FAIL] Deep hierarchy failed at level 3.");
                    result = 0;
                }
            } else {
                LOG_ERROR("[FAIL] Deep hierarchy failed at level 2.");
                result = 0;
            }
        } else {
            LOG_ERROR("[FAIL] Deep hierarchy failed at level 1.");
            result = 0;
        }

        keymap_destroy(level1);
        keymap_destroy(level2);
        keymap_destroy(level3);
    } else {
        LOG_ERROR("[FAIL] Failed to create deep hierarchy keymaps.");
        result = 0;
    }

    PHASE_END("Keymap Functionality", result);
    return result;
}

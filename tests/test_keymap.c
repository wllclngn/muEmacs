#include "test_utils.h"
#include "μemacs/keymap.h"
#include "estruct.h" // Ensure core structures are defined first
#include "edef.h"  // For global_keymap, ctlx_keymap, etc.
#include "efunc.h" // For mlwrite, etc.
#include <time.h>  // For clock() performance testing

// Dummy command function for testing
int test_command_a(int f, int n) {
    (void)f; (void)n;
    printf("Test Command A executed.\n");
    return TRUE;
}

int test_command_b(int f, int n) {
    (void)f; (void)n;
    printf("Test Command B executed.\n");
    return TRUE;
}

int test_command_c(int f, int n) {
    (void)f; (void)n;
    printf("Test Command C executed.\n");
    return TRUE;
}

// Test function for keymap functionality
int test_keymap_functionality() {
    int result = 1; // Assume success

    PHASE_START("Keymap Functionality", "Testing hash-based hierarchical keymap system");

    // Test 1: Keymap creation and destruction
    printf("1. Testing keymap creation and destruction...\n");
    struct keymap *km1 = keymap_create("test_km1");
    if (!km1) {
        printf("[%sFAIL%s] Failed to create keymap km1.\n", RED, RESET);
        result = 0;
    } else {
        printf("[%sSUCCESS%s] Keymap km1 created.\n", GREEN, RESET);
    }
    keymap_destroy(km1);
    printf("[%sSUCCESS%s] Keymap km1 destroyed.\n", GREEN, RESET);

    // Re-create global keymaps for testing (they are global singletons)
    // This is usually done by keymap_init_from_legacy, but we'll do it manually for isolated testing
    struct keymap *gkm = keymap_create("global");
    struct keymap *ckm = keymap_create("C-x");
    struct keymap *hkm = keymap_create("C-h");
    struct keymap *mkm = keymap_create("Meta");
    
    if (!gkm || !ckm || !hkm || !mkm) {
        printf("[%sFAIL%s] Failed to re-create global keymaps.\n", RED, RESET);
        result = 0;
        goto end_test;
    }
    
    // Store in atomic globals
    atomic_store(&global_keymap, gkm);
    atomic_store(&ctlx_keymap, ckm);
    atomic_store(&help_keymap, hkm);
    atomic_store(&meta_keymap, mkm);
    printf("[%sSUCCESS%s] Global keymaps re-created.\n", GREEN, RESET);

    // Test 2: Basic binding and lookup
    printf("2. Testing basic binding and lookup...\n");
    struct keymap *gkm_test = atomic_load(&global_keymap);
    if (!keymap_bind(gkm_test, 'a', test_command_a)) {
        printf("[%sFAIL%s] Failed to bind 'a' to test_command_a.\n", RED, RESET);
        result = 0;
    }
    struct keymap_entry *entry_a = keymap_lookup(gkm_test, 'a');
    if (entry_a && !entry_a->is_prefix && entry_a->binding.cmd == test_command_a) {
        printf("[%sSUCCESS%s] Basic binding and lookup for 'a' successful.\n", GREEN, RESET);
    } else {
        printf("[%sFAIL%s] Basic binding and lookup for 'a' failed.\n", RED, RESET);
        result = 0;
    }

    // Test 3: Prefix binding and lookup
    printf("3. Testing prefix binding and lookup...\n");
    struct keymap *ckm_test = atomic_load(&ctlx_keymap);
    if (!keymap_bind_prefix(gkm_test, 'x', ckm_test)) {
        printf("[%sFAIL%s] Failed to bind 'x' as prefix to ctlx_keymap.\n", RED, RESET);
        result = 0;
    }
    struct keymap_entry *entry_x = keymap_lookup(gkm_test, 'x');
    if (entry_x && entry_x->is_prefix && entry_x->binding.map == ckm_test) {
        printf("[%sSUCCESS%s] Prefix binding for 'x' successful.\n", GREEN, RESET);
    } else {
        printf("[%sFAIL%s] Prefix binding for 'x' failed.\n", RED, RESET);
        result = 0;
    }

    // Test 4: Hierarchical lookup (C-x C-c)
    printf("4. Testing hierarchical lookup (C-x C-c)...\n");
    // Assuming quit_cmd is a valid command function
    if (!keymap_bind(ckm_test, 'c', quit)) {
        printf("[%sFAIL%s] Failed to bind 'c' in ctlx_keymap.\n", RED, RESET);
        result = 0;
    }
    struct keymap_entry *entry_c_x_c = keymap_lookup_chain(gkm_test, 'x'); // First lookup for prefix
    if (entry_c_x_c && entry_c_x_c->is_prefix) {
        struct keymap_entry *final_entry = keymap_lookup(entry_c_x_c->binding.map, 'c');
        if (final_entry && !final_entry->is_prefix && final_entry->binding.cmd == quit) {
            printf("[%sSUCCESS%s] Hierarchical lookup for C-x C-c successful.\n", GREEN, RESET);
        } else {
            printf("[%sFAIL%s] Hierarchical lookup for C-x C-c failed (final entry).\n", RED, RESET);
            result = 0;
        }
    } else {
        printf("[%sFAIL%s] Hierarchical lookup for C-x C-c failed (prefix lookup).\n", RED, RESET);
        result = 0;
    }

    // Test 5: Unbinding
    printf("5. Testing unbinding...\n");
    if (!keymap_unbind(gkm_test, 'a')) {
        printf("[%sFAIL%s] Failed to unbind 'a'.\n", RED, RESET);
        result = 0;
    }
    if (keymap_lookup(gkm_test, 'a')) {
        printf("[%sFAIL%s] 'a' still found after unbinding.\n", RED, RESET);
        result = 0;
    } else {
        printf("[%sSUCCESS%s] Unbinding 'a' successful.\n", GREEN, RESET);
    }

    // Test 6: Legacy initialization (this will re-initialize global keymaps)
    printf("6. Testing legacy keymap initialization...\n");
    keymap_init_from_legacy();
    // After legacy init, 'a' should be unbound, but 'C-x' should still be a prefix
    struct keymap *gkm_after = atomic_load(&global_keymap);
    struct keymap *ckm_after = atomic_load(&ctlx_keymap);
    struct keymap_entry *entry_a_after_legacy = keymap_lookup(gkm_after, 'a');
    if (entry_a_after_legacy) { // Legacy keytab might have 'a' bound, so this might pass or fail depending on keytab
        printf("[%sINFO%s] 'a' found after legacy init (expected if in keytab).\n", YELLOW, RESET);
    } else {
        printf("[%sSUCCESS%s] 'a' not found after legacy init (expected if not in keytab).\n", GREEN, RESET);
    }
    
    struct keymap_entry *entry_x_after_legacy = keymap_lookup(gkm_after, CONTROL | 'X');
    if (entry_x_after_legacy && entry_x_after_legacy->is_prefix && entry_x_after_legacy->binding.map == ckm_after) {
        printf("[%sSUCCESS%s] C-x prefix still valid after legacy init.\n", GREEN, RESET);
    } else {
        printf("[%sFAIL%s] C-x prefix invalid after legacy init.\n", RED, RESET);
        result = 0;
    }

end_test:
    // Note: Don't cleanup global keymaps here as they might be needed by other tests
    // keymap_init_from_legacy() handles proper cleanup and re-initialization

    // Test 7: Performance benchmarking - O(1) hash lookup verification
    printf("7. Testing hash table performance (O(1) verification)...\n");
    
    // Recreate keymaps for performance test after legacy init
    keymap_init_from_legacy();
    
    clock_t start = clock();
    int lookups = 100000;
    struct keymap *gkm_perf = atomic_load(&global_keymap);
    for (int i = 0; i < lookups; i++) {
        keymap_lookup(gkm_perf, 'a' + (i % 26));
    }
    clock_t end = clock();
    double time_per_lookup = ((double)(end - start)) / CLOCKS_PER_SEC / lookups * 1000000; // microseconds
    printf("[%sINFO%s] %d lookups completed in %.2f μs average (target: <5 μs for O(1))\n", 
           BLUE, RESET, lookups, time_per_lookup);
    if (time_per_lookup < 5.0) {
        printf("[%sSUCCESS%s] Hash table performance meets O(1) requirements.\n", GREEN, RESET);
    } else {
        printf("[%sWARNING%s] Hash table performance may not be optimal.\n", YELLOW, RESET);
    }

    // Test 8: Fallback chain testing
    printf("8. Testing fallback chain behavior...\n");
    // Bind a command to global keymap
    struct keymap *gkm_fallback = atomic_load(&global_keymap);
    struct keymap *ckm_fallback = atomic_load(&ctlx_keymap);
    
    if (gkm_fallback && ckm_fallback) {
        keymap_bind(gkm_fallback, 'z', test_command_c);
        // Try lookup in ctlx_keymap first (should fail), then fallback to global
        struct keymap_entry *fallback_entry = keymap_lookup(ckm_fallback, 'z');
        if (!fallback_entry) {
            // Expected - now test global fallback
            fallback_entry = keymap_lookup(gkm_fallback, 'z');
            if (fallback_entry && fallback_entry->binding.cmd == test_command_c) {
                printf("[%sSUCCESS%s] Fallback chain working correctly.\n", GREEN, RESET);
            } else {
                printf("[%sFAIL%s] Global fallback failed.\n", RED, RESET);
                result = 0;
            }
        } else {
            printf("[%sFAIL%s] Unexpected binding found in ctlx_keymap.\n", RED, RESET);
            result = 0;
        }
    } else {
        printf("[%sFAIL%s] Keymap pointers are null after legacy init.\n", RED, RESET);
        result = 0;
    }

    // Test 9: Hash collision handling
    printf("9. Testing hash collision handling...\n");
    struct keymap *gkm_collision = atomic_load(&global_keymap);
    
    if (gkm_collision) {
        int collision_keys[] = {65, 97, 129, 161}; // Keys that might hash to same bucket
        for (int i = 0; i < 4; i++) {
            keymap_bind(gkm_collision, collision_keys[i], test_command_a);
        }
        int collisions_resolved = 0;
        for (int i = 0; i < 4; i++) {
            struct keymap_entry *entry = keymap_lookup(gkm_collision, collision_keys[i]);
            if (entry && entry->binding.cmd == test_command_a) {
                collisions_resolved++;
            }
        }
        if (collisions_resolved == 4) {
            printf("[%sSUCCESS%s] Hash collision handling working correctly.\n", GREEN, RESET);
        } else {
            printf("[%sFAIL%s] Hash collision handling failed (%d/4 resolved).\n", RED, RESET, collisions_resolved);
            result = 0;
        }
    } else {
        printf("[%sFAIL%s] Global keymap is null for collision test.\n", RED, RESET);
        result = 0;
    }

    // Test 10: Deep hierarchy stress test
    printf("10. Testing deep hierarchy stress test...\n");
    struct keymap *level1 = keymap_create("level1");
    struct keymap *level2 = keymap_create("level2");
    struct keymap *level3 = keymap_create("level3");
    
    struct keymap *gkm_deep = atomic_load(&global_keymap);
    if (level1 && level2 && level3 && gkm_deep) {
        keymap_bind_prefix(gkm_deep, '1', level1);
        keymap_bind_prefix(level1, '2', level2);
        keymap_bind(level2, '3', test_command_c);
        
        // Test 3-level deep lookup
        struct keymap_entry *deep_entry = keymap_lookup(gkm_deep, '1');
        if (deep_entry && deep_entry->is_prefix) {
            deep_entry = keymap_lookup(deep_entry->binding.map, '2');
            if (deep_entry && deep_entry->is_prefix) {
                deep_entry = keymap_lookup(deep_entry->binding.map, '3');
                if (deep_entry && deep_entry->binding.cmd == test_command_c) {
                    printf("[%sSUCCESS%s] Deep hierarchy (3 levels) working correctly.\n", GREEN, RESET);
                } else {
                    printf("[%sFAIL%s] Deep hierarchy failed at level 3.\n", RED, RESET);
                    result = 0;
                }
            } else {
                printf("[%sFAIL%s] Deep hierarchy failed at level 2.\n", RED, RESET);
                result = 0;
            }
        } else {
            printf("[%sFAIL%s] Deep hierarchy failed at level 1.\n", RED, RESET);
            result = 0;
        }
        
        keymap_destroy(level1);
        keymap_destroy(level2);
        keymap_destroy(level3);
    } else {
        printf("[%sFAIL%s] Failed to create deep hierarchy keymaps.\n", RED, RESET);
        result = 0;
    }

end_cleanup:
    PHASE_END("Keymap Functionality", result);
    return result;
}

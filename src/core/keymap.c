// keymap.c - Hierarchical keymap implementation with O(1) hash table lookup

#include "keymap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "efunc.h"  // for deskey, desbind, backdel

// External reference to legacy keytab
extern struct key_tab keytab[];

// C23 atomic global keymaps - instantaneous access
_Atomic(struct keymap *) global_keymap = nullptr;
_Atomic(struct keymap *) ctlx_keymap = nullptr;
_Atomic(struct keymap *) help_keymap = nullptr;
_Atomic(struct keymap *) meta_keymap = nullptr;

// Hook lists
struct hook_list pre_command_hooks = {0};
struct hook_list post_command_hooks = {0};

// Statistics
struct keymap_stats keymap_global_stats = {0};

// Fast hash function for key codes
static inline uint32_t hash_key(uint32_t key) {
	// MurmurHash-inspired mixing
	key ^= key >> 16;
	key *= 0x85ebca6b;
	key ^= key >> 13;
	key *= 0xc2b2ae35;
	key ^= key >> 16;
	return key & KEYMAP_HASH_MASK;
}

// Create a new keymap
struct keymap *keymap_create(const char *name) {
	struct keymap *km = safe_alloc(sizeof(struct keymap), "keymap", __FILE__, __LINE__);
	if (!km) return nullptr;
	
	if (name) {
		km->name = safe_strdup(name, "keymap name");
		if (!km->name) {
			SAFE_FREE(km);
			return nullptr;
		}
	}
	
	atomic_init(&km->generation, 0);
	return km;
}

// Destroy a keymap and free all resources
void keymap_destroy(struct keymap *km) {
	if (!km) return;
	
    // Null global pointers if this is one of the global keymaps to prevent use-after-free
    if (km == global_keymap) global_keymap = nullptr;
    if (km == ctlx_keymap) ctlx_keymap = nullptr;
    if (km == help_keymap) help_keymap = nullptr;
    if (km == meta_keymap) meta_keymap = nullptr;
	
	// Free all hash table entries
	for (int i = 0; i < KEYMAP_HASH_SIZE; i++) {
		struct keymap_entry *entry = km->table[i];
		while (entry) {
			struct keymap_entry *next = entry->next;
			SAFE_FREE(entry);
			entry = next;
		}
	}
	
	SAFE_FREE(km->name);
	SAFE_FREE(km);
}

// Bind a key to a command - O(1) average case
int keymap_bind(struct keymap *km, uint32_t key, command_fn cmd) {
	if (!km || !cmd) return false;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	
	// Check if key already exists
	while (entry) {
		if (entry->key == key) {
			// Update existing binding
			entry->binding.cmd = cmd;
			entry->is_prefix = 0;
			atomic_fetch_add(&km->generation, 1);
			return true;
		}
		entry = entry->next;
	}
	
	// Create new entry
	entry = safe_alloc(sizeof(struct keymap_entry), "keymap entry", __FILE__, __LINE__);
	if (!entry) return false;
	
	entry->key = key;
	entry->binding.cmd = cmd;
	entry->is_prefix = 0;
	entry->next = km->table[hash];
	km->table[hash] = entry;
	
	km->binding_count++;
	atomic_fetch_add(&km->generation, 1);
	
	// Update statistics
	if (entry->next) {
		atomic_fetch_add(&keymap_global_stats.collisions, 1);
	}
	
	return true;
}

// Bind a key to a prefix keymap
int keymap_bind_prefix(struct keymap *km, uint32_t key, struct keymap *prefix) {
	if (!km || !prefix) return false;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	
	// Check if key already exists
	while (entry) {
		if (entry->key == key) {
			// Update existing binding
			entry->binding.map = prefix;
			entry->is_prefix = 1;
			atomic_fetch_add(&km->generation, 1);
			return true;
		}
		entry = entry->next;
	}
	
	// Create new entry
	entry = safe_alloc(sizeof(struct keymap_entry), "keymap entry", __FILE__, __LINE__);
	if (!entry) return false;
	
	entry->key = key;
	entry->binding.map = prefix;
	entry->is_prefix = 1;
	entry->next = km->table[hash];
	km->table[hash] = entry;
	
	km->binding_count++;
	atomic_fetch_add(&km->generation, 1);
	
	return true;
}

// Lookup a key binding - O(1) average case
struct keymap_entry *keymap_lookup(struct keymap *km, uint32_t key) {
	if (!km) return nullptr;
	
	atomic_fetch_add(&keymap_global_stats.lookups, 1);
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	
	while (entry) {
		if (entry->key == key) {
			atomic_fetch_add(&keymap_global_stats.hits, 1);
			return entry;
		}
		entry = entry->next;
	}
	
	atomic_fetch_add(&keymap_global_stats.misses, 1);
	return nullptr;
}

// Lookup with inheritance chain
struct keymap_entry *keymap_lookup_chain(struct keymap *km, uint32_t key) {
	while (km) {
		struct keymap_entry *entry = keymap_lookup(km, key);
		if (entry) return entry;
		km = km->parent;
	}
	return nullptr;
}

// Remove a key binding
int keymap_unbind(struct keymap *km, uint32_t key) {
	if (!km) return false;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	struct keymap_entry *prev = nullptr;
	
	while (entry) {
		if (entry->key == key) {
			if (prev) {
				prev->next = entry->next;
			} else {
				km->table[hash] = entry->next;
			}
			SAFE_FREE(entry);
			km->binding_count--;
			atomic_fetch_add(&km->generation, 1);
			return true;
		}
		prev = entry;
		entry = entry->next;
	}
	
	return false;
}

// Initialize keymaps from legacy keytab
void keymap_init_from_legacy(void) {
	// Cleanup existing keymaps first to prevent memory leaks
	if (global_keymap) keymap_destroy(global_keymap);
	if (ctlx_keymap) keymap_destroy(ctlx_keymap);
	if (help_keymap) keymap_destroy(help_keymap);
	if (meta_keymap) keymap_destroy(meta_keymap);
	
	// Create global keymaps with C23 atomic stores - instantaneous
	struct keymap *gkm = keymap_create("global");
	struct keymap *ckm = keymap_create("C-x");
	struct keymap *hkm = keymap_create("C-h");
	struct keymap *mkm = keymap_create("Meta");
	
	if (!gkm || !ckm || !hkm || !mkm) {
		mlwrite("Failed to initialize keymaps!");
		return;
	}
	
	// Atomic publication of keymaps - instantaneous and visible to all threads
	atomic_store_explicit(&global_keymap, gkm, memory_order_release);
	atomic_store_explicit(&ctlx_keymap, ckm, memory_order_release);
	atomic_store_explicit(&help_keymap, hkm, memory_order_release);
	atomic_store_explicit(&meta_keymap, mkm, memory_order_release);
	
    // Import bindings from legacy keytab
    extern struct key_tab keytab[];
    struct key_tab *ktp = &keytab[0];
    
    while (ktp->k_fp != nullptr) {
        uint32_t code = ktp->k_code;
        
        // Route to appropriate keymap based on prefix - C23 atomic loads
        if (code & CTLX) {
            // C-x prefix binding
            code &= ~CTLX;  // Remove CTLX bit
            struct keymap *ckm = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
            keymap_bind(ckm, code, ktp->k_fp);
        } else if (code & META) {
            // Meta prefix binding
            uint32_t meta_code = code & ~META;
            struct keymap *mkm = atomic_load_explicit(&meta_keymap, memory_order_acquire);
            keymap_bind(mkm, meta_code, ktp->k_fp);
        } else {
            // Global binding
            struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
            keymap_bind(gkm, code, ktp->k_fp);
        }
        
        ktp++;
    }
	
    // Seed helpful entries in the help keymap (invoked only if user enables prefix)
    struct keymap *hkm_setup = atomic_load_explicit(&help_keymap, memory_order_acquire);
    if (hkm_setup) {
        keymap_bind(hkm_setup, 'k', deskey);   // C-h k -> describe-key
        keymap_bind(hkm_setup, 'b', desbind);  // C-h b -> describe-bindings
    }

    // Ensure legacy-essential defaults exist even if import changes: M-? -> help, C-x C-c -> quit
    struct keymap *mkm_fix = atomic_load_explicit(&meta_keymap, memory_order_acquire);
    if (mkm_fix) {
        keymap_bind(mkm_fix, '?', help);
    }
    struct keymap *ckm_fix = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
    if (ckm_fix) {
        keymap_bind(ckm_fix, CONTROL | 'C', quit);
    }

    // Set up prefix maps in global keymap - C23 atomic loads for prefix setup
    struct keymap *gkm_prefixes = atomic_load_explicit(&global_keymap, memory_order_acquire);
    struct keymap *ckm_prefixes = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
    struct keymap *hkm_prefixes = atomic_load_explicit(&help_keymap, memory_order_acquire);
    struct keymap *mkm_prefixes = atomic_load_explicit(&meta_keymap, memory_order_acquire);
	
    keymap_bind_prefix(gkm_prefixes, CONTROL | 'X', ckm_prefixes);
    // Do NOT hijack C-h as a help prefix by default; in μEmacs it's commonly Backspace.
    // Help is available via M-? and M-x describe-* commands.
    // If a dedicated help prefix is desired, it should be enabled explicitly via config.
    keymap_bind_prefix(gkm_prefixes, 0x1B, mkm_prefixes);  // ESC key

	// C23 atomic store of current keymap - instantaneous activation
	atomic_store_explicit(&current_keymap, gkm_prefixes, memory_order_release);
}

// Runtime toggle: enable help prefix on C-h (overrides backspace behavior)
int help_prefix_enable(int f, int n) {
    (void)f; (void)n;
    struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
    struct keymap *hkm = atomic_load_explicit(&help_keymap, memory_order_acquire);
    if (!gkm || !hkm) return false;
    if (!keymap_bind_prefix(gkm, CONTROL | 'H', hkm)) return false;
    mlwrite("Help prefix enabled on C-h (Backspace overridden)");
    return true;
}

// Runtime toggle: disable help prefix on C-h and restore Backspace behavior
int help_prefix_disable(int f, int n) {
    (void)f; (void)n;
    struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
    if (!gkm) return false;
    keymap_unbind(gkm, CONTROL | 'H');
    // Restore traditional Backspace binding
    if (!keymap_bind(gkm, CONTROL | 'H', backdel)) return false;
    mlwrite("Help prefix disabled on C-h (Backspace restored)");
    return true;
}

// Legacy compatibility: get binding for old-style key code
struct keymap_entry *keymap_get_binding(int legacy_code) {
	struct keymap_entry *entry = nullptr;
	
	// Check for prefix keymaps first
    if (legacy_code & CTLX) {
        uint32_t code = legacy_code & ~CTLX;
        struct keymap *ckm = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
        entry = keymap_lookup(ckm, code);
    } else if (legacy_code & META) {
        uint32_t code = legacy_code & ~META;
        struct keymap *mkm = atomic_load_explicit(&meta_keymap, memory_order_acquire);
        entry = keymap_lookup(mkm, code);
    } else {
        struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
        entry = keymap_lookup(gkm, legacy_code);
    }
	
	return entry;
}

// Hook management
int hook_add(struct hook_list *list, command_hook hook) {
	if (!list || !hook) return false;
	
	// Resize if needed
	if (list->count >= list->capacity) {
		size_t new_capacity = list->capacity ? list->capacity * 2 : 4;
		command_hook *new_hooks = safe_realloc(list->hooks, 
										  new_capacity * sizeof(command_hook), "command hooks");
		if (!new_hooks) return false;
		list->hooks = new_hooks;
		list->capacity = new_capacity;
	}
	
	list->hooks[list->count++] = hook;
	return true;
}

int hook_remove(struct hook_list *list, command_hook hook) {
	if (!list || !hook) return false;
	
	for (size_t i = 0; i < list->count; i++) {
		if (list->hooks[i] == hook) {
			// Shift remaining hooks
			memmove(&list->hooks[i], &list->hooks[i + 1],
					(list->count - i - 1) * sizeof(command_hook));
			list->count--;
			return true;
		}
	}
	
	return false;
}

int hook_run_pre(command_fn cmd, int f, int n) {
	for (size_t i = 0; i < pre_command_hooks.count; i++) {
		int result = pre_command_hooks.hooks[i](cmd, f, n);
		if (result != true) return result;
	}
	return true;
}

int hook_run_post(command_fn cmd, int f, int n, int result) {
	for (size_t i = 0; i < post_command_hooks.count; i++) {
		post_command_hooks.hooks[i](cmd, f, n);
	}
	return result;
}

// Debug: dump keymap statistics
void keymap_dump_stats(void) {
	mlwrite("Keymap Statistics:");
	mlwrite("  Lookups: %zu", atomic_load(&keymap_global_stats.lookups));
	mlwrite("  Hits: %zu", atomic_load(&keymap_global_stats.hits));
	mlwrite("  Misses: %zu", atomic_load(&keymap_global_stats.misses));
	mlwrite("  Collisions: %zu", atomic_load(&keymap_global_stats.collisions));
	
	double hit_rate = 0.0;
	size_t lookups = atomic_load(&keymap_global_stats.lookups);
	if (lookups > 0) {
		hit_rate = (double)atomic_load(&keymap_global_stats.hits) / lookups * 100.0;
	}
	mlwrite("  Hit rate: %.2f%%", hit_rate);
}

// Validate keymap integrity
void keymap_validate(struct keymap *km) {
	if (!km) return;
	
	size_t count = 0;
	for (int i = 0; i < KEYMAP_HASH_SIZE; i++) {
		struct keymap_entry *entry = km->table[i];
		while (entry) {
			count++;
			entry = entry->next;
		}
	}
	
	if (count != km->binding_count) {
		mlwrite("Keymap validation failed: counted %zu, expected %zu", count, km->binding_count);
	}
}

// User-facing command: show keymap lookup statistics
int keymap_stats_cmd(int f, int n) {
    (void)f; (void)n;
    size_t lookups = atomic_load(&keymap_global_stats.lookups);
    size_t hits = atomic_load(&keymap_global_stats.hits);
    size_t misses = atomic_load(&keymap_global_stats.misses);
    size_t collisions = atomic_load(&keymap_global_stats.collisions);
    double hit_rate = (lookups > 0) ? ((double)hits / (double)lookups) * 100.0 : 0.0;
    mlwrite("Keymap stats: lookups=%zu hits=%zu misses=%zu collisions=%zu hit-rate=%.2f%%",
            lookups, hits, misses, collisions, hit_rate);
    return true;
}

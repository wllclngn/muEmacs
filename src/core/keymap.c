// keymap.c - Hierarchical keymap implementation with O(1) hash table lookup

#include "μemacs/keymap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"

// External reference to legacy keytab
extern struct key_tab keytab[];

// C23 atomic global keymaps - instantaneous access
_Atomic(struct keymap *) global_keymap = NULL;
_Atomic(struct keymap *) ctlx_keymap = NULL;
_Atomic(struct keymap *) help_keymap = NULL;
_Atomic(struct keymap *) meta_keymap = NULL;

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
	if (!km) return NULL;
	
	if (name) {
		km->name = safe_strdup(name, "keymap name");
		if (!km->name) {
			SAFE_FREE(km);
			return NULL;
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
	if (!km || !cmd) return FALSE;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	
	// Check if key already exists
	while (entry) {
		if (entry->key == key) {
			// Update existing binding
			entry->binding.cmd = cmd;
			entry->is_prefix = 0;
			atomic_fetch_add(&km->generation, 1);
			return TRUE;
		}
		entry = entry->next;
	}
	
	// Create new entry
	entry = safe_alloc(sizeof(struct keymap_entry), "keymap entry", __FILE__, __LINE__);
	if (!entry) return FALSE;
	
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
	
	return TRUE;
}

// Bind a key to a prefix keymap
int keymap_bind_prefix(struct keymap *km, uint32_t key, struct keymap *prefix) {
	if (!km || !prefix) return FALSE;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	
	// Check if key already exists
	while (entry) {
		if (entry->key == key) {
			// Update existing binding
			entry->binding.map = prefix;
			entry->is_prefix = 1;
			atomic_fetch_add(&km->generation, 1);
			return TRUE;
		}
		entry = entry->next;
	}
	
	// Create new entry
	entry = safe_alloc(sizeof(struct keymap_entry), "keymap entry", __FILE__, __LINE__);
	if (!entry) return FALSE;
	
	entry->key = key;
	entry->binding.map = prefix;
	entry->is_prefix = 1;
	entry->next = km->table[hash];
	km->table[hash] = entry;
	
	km->binding_count++;
	atomic_fetch_add(&km->generation, 1);
	
	return TRUE;
}

// Lookup a key binding - O(1) average case
struct keymap_entry *keymap_lookup(struct keymap *km, uint32_t key) {
	if (!km) return NULL;
	
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
	return NULL;
}

// Lookup with inheritance chain
struct keymap_entry *keymap_lookup_chain(struct keymap *km, uint32_t key) {
	while (km) {
		struct keymap_entry *entry = keymap_lookup(km, key);
		if (entry) return entry;
		km = km->parent;
	}
	return NULL;
}

// Remove a key binding
int keymap_unbind(struct keymap *km, uint32_t key) {
	if (!km) return FALSE;
	
	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	struct keymap_entry *prev = NULL;
	
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
			return TRUE;
		}
		prev = entry;
		entry = entry->next;
	}
	
	return FALSE;
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
	
	while (ktp->k_fp != NULL) {
		uint32_t code = ktp->k_code;
		
		// Route to appropriate keymap based on prefix - C23 atomic loads
		if (code & CTLX) {
			// C-x prefix binding
			code &= ~CTLX;  // Remove CTLX bit
			struct keymap *ckm = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
			keymap_bind(ckm, code, ktp->k_fp);
		} else if ((code & 0xFF) == ('H' - '@') && (code & CONTROL)) {
			// C-h prefix (help)
			struct keymap *hkm = atomic_load_explicit(&help_keymap, memory_order_acquire);
			keymap_bind(hkm, code, ktp->k_fp);
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
	
	// Set up prefix maps in global keymap - C23 atomic loads for prefix setup
	struct keymap *gkm_prefixes = atomic_load_explicit(&global_keymap, memory_order_acquire);
	struct keymap *ckm_prefixes = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
	struct keymap *hkm_prefixes = atomic_load_explicit(&help_keymap, memory_order_acquire);
	struct keymap *mkm_prefixes = atomic_load_explicit(&meta_keymap, memory_order_acquire);
	
	keymap_bind_prefix(gkm_prefixes, CONTROL | 'X', ckm_prefixes);
	keymap_bind_prefix(gkm_prefixes, CONTROL | 'H', hkm_prefixes);
	keymap_bind_prefix(gkm_prefixes, 0x1B, mkm_prefixes);  // ESC key

	// C23 atomic store of current keymap - instantaneous activation
	atomic_store_explicit(&current_keymap, gkm_prefixes, memory_order_release);
}

// Legacy compatibility: get binding for old-style key code
struct keymap_entry *keymap_get_binding(int legacy_code) {
	struct keymap_entry *entry = NULL;
	
	// Check for prefix keymaps first
	if (legacy_code & CTLX) {
		uint32_t code = legacy_code & ~CTLX;
		entry = keymap_lookup(ctlx_keymap, code);
	} else if (legacy_code & META) {
		uint32_t code = legacy_code & ~META;
		entry = keymap_lookup(meta_keymap, code);
	} else {
		entry = keymap_lookup(global_keymap, legacy_code);
	}
	
	return entry;
}

// Hook management
int hook_add(struct hook_list *list, command_hook hook) {
	if (!list || !hook) return FALSE;
	
	// Resize if needed
	if (list->count >= list->capacity) {
		size_t new_capacity = list->capacity ? list->capacity * 2 : 4;
		command_hook *new_hooks = safe_realloc(list->hooks, 
										  new_capacity * sizeof(command_hook), "command hooks");
		if (!new_hooks) return FALSE;
		list->hooks = new_hooks;
		list->capacity = new_capacity;
	}
	
	list->hooks[list->count++] = hook;
	return TRUE;
}

int hook_remove(struct hook_list *list, command_hook hook) {
	if (!list || !hook) return FALSE;
	
	for (size_t i = 0; i < list->count; i++) {
		if (list->hooks[i] == hook) {
			// Shift remaining hooks
			memmove(&list->hooks[i], &list->hooks[i + 1],
					(list->count - i - 1) * sizeof(command_hook));
			list->count--;
			return TRUE;
		}
	}
	
	return FALSE;
}

int hook_run_pre(command_fn cmd, int f, int n) {
	for (size_t i = 0; i < pre_command_hooks.count; i++) {
		int result = pre_command_hooks.hooks[i](cmd, f, n);
		if (result != TRUE) return result;
	}
	return TRUE;
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

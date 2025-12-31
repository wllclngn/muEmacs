// keymap.c - Hierarchical keymap implementation with O(1) hash table lookup

#include "keymap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"

// C23 atomic global keymaps - instantaneous access
_Atomic(struct keymap *) global_keymap = nullptr;
_Atomic(struct keymap *) ctlx_keymap = nullptr;
_Atomic(struct keymap *) help_keymap = nullptr;
_Atomic(struct keymap *) meta_keymap = nullptr;

// Statistics
struct keymap_stats keymap_global_stats = {0};

// Hash function for keymap keys - uses inline from keymap.h
static inline uint32_t hash_key(keymap_key_t key) {
	return keymap_key_hash(key) & KEYMAP_HASH_MASK;
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
    // Use atomic operations for C23 compliance
    if (km == atomic_load(&global_keymap)) atomic_store(&global_keymap, nullptr);
    if (km == atomic_load(&ctlx_keymap)) atomic_store(&ctlx_keymap, nullptr);
    if (km == atomic_load(&help_keymap)) atomic_store(&help_keymap, nullptr);
    if (km == atomic_load(&meta_keymap)) atomic_store(&meta_keymap, nullptr);
	
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
int keymap_bind(struct keymap *km, keymap_key_t key, command_fn cmd) {
	if (!km || !cmd) return false;

	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];

	// Check if key already exists
	while (entry) {
		if (keymap_key_eq(entry->key, key)) {
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
int keymap_bind_prefix(struct keymap *km, keymap_key_t key, struct keymap *prefix) {
	if (!km || !prefix) return false;

	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];

	// Check if key already exists
	while (entry) {
		if (keymap_key_eq(entry->key, key)) {
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
struct keymap_entry *keymap_lookup(struct keymap *km, keymap_key_t key) {
	if (!km) return nullptr;

	atomic_fetch_add(&keymap_global_stats.lookups, 1);

	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];

	while (entry) {
		if (keymap_key_eq(entry->key, key)) {
			atomic_fetch_add(&keymap_global_stats.hits, 1);
			return entry;
		}
		entry = entry->next;
	}

	atomic_fetch_add(&keymap_global_stats.misses, 1);
	return nullptr;
}

// Lookup with inheritance chain
struct keymap_entry *keymap_lookup_chain(struct keymap *km, keymap_key_t key) {
	while (km) {
		struct keymap_entry *entry = keymap_lookup(km, key);
		if (entry) return entry;
		km = km->parent;
	}
	return nullptr;
}

// Remove a key binding
int keymap_unbind(struct keymap *km, keymap_key_t key) {
	if (!km) return false;

	uint32_t hash = hash_key(key);
	struct keymap_entry *entry = km->table[hash];
	struct keymap_entry *prev = nullptr;

	while (entry) {
		if (keymap_key_eq(entry->key, key)) {
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

// Initialize keymap infrastructure - NO command bindings (those come from TOML)
void keymap_init_defaults(void) {
	// Cleanup existing keymaps first to prevent memory leaks
	if (global_keymap) keymap_destroy(global_keymap);
	if (ctlx_keymap) keymap_destroy(ctlx_keymap);
	if (help_keymap) keymap_destroy(help_keymap);
	if (meta_keymap) keymap_destroy(meta_keymap);

	// Create global keymaps with C23 atomic stores
	struct keymap *gkm = keymap_create("global");
	struct keymap *ckm = keymap_create("C-x");
	struct keymap *hkm = keymap_create("C-h");
	struct keymap *mkm = keymap_create("Meta");

	if (!gkm || !ckm || !hkm || !mkm) {
		mlwrite("FAILED TO INITIALIZE KEYMAPS!");
		return;
	}

	// Atomic publication of keymaps - visible to all threads
	atomic_store_explicit(&global_keymap, gkm, memory_order_release);
	atomic_store_explicit(&ctlx_keymap, ckm, memory_order_release);
	atomic_store_explicit(&help_keymap, hkm, memory_order_release);
	atomic_store_explicit(&meta_keymap, mkm, memory_order_release);

	// ONLY prefix bindings stay in code - they are structural routing, not commands
	// All command bindings come from settings.toml
	keymap_bind_prefix(gkm, keymap_key_make('X', MOD_CTRL), ckm);   // C-x prefix
	keymap_bind_prefix(gkm, keymap_key_make('[', MOD_CTRL), mkm);   // Ctrl+[ (ESC via C0)
	keymap_bind_prefix(gkm, keymap_key_make(0x1B, 0), mkm);         // Raw ESC

	// C23 atomic store of current keymap
	atomic_store_explicit(&current_keymap, gkm, memory_order_release);
}

// Runtime toggle: enable help prefix on C-h (overrides backspace behavior)
int help_prefix_enable(int f, int n) {
    (void)f; (void)n;
    struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
    struct keymap *hkm = atomic_load_explicit(&help_keymap, memory_order_acquire);
    if (!gkm || !hkm) return false;
    if (!keymap_bind_prefix(gkm, keymap_key_make('H', MOD_CTRL), hkm)) return false;
    mlwrite("HELP PREFIX ENABLED ON C-H [BACKSPACE OVERRIDDEN]");
    return true;
}

// Runtime toggle: disable help prefix on C-h and restore Backspace behavior
int help_prefix_disable(int f, int n) {
    (void)f; (void)n;
    struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
    if (!gkm) return false;
    keymap_unbind(gkm, keymap_key_make('H', MOD_CTRL));
    // Restore traditional Backspace binding
    if (!keymap_bind(gkm, keymap_key_make('H', MOD_CTRL), delete_char_backward)) return false;
    mlwrite("HELP PREFIX DISABLED ON C-H [BACKSPACE RESTORED]");
    return true;
}

// Debug: dump keymap statistics
void keymap_dump_stats(void) {
	mlwrite("KEYMAP STATISTICS:");
	mlwrite("  LOOKUPS: %zu", atomic_load(&keymap_global_stats.lookups));
	mlwrite("  HITS: %zu", atomic_load(&keymap_global_stats.hits));
	mlwrite("  MISSES: %zu", atomic_load(&keymap_global_stats.misses));
	mlwrite("  COLLISIONS: %zu", atomic_load(&keymap_global_stats.collisions));
	
	double hit_rate = 0.0;
	size_t lookups = atomic_load(&keymap_global_stats.lookups);
	if (lookups > 0) {
		hit_rate = (double)atomic_load(&keymap_global_stats.hits) / lookups * 100.0;
	}
	mlwrite("  HIT RATE: %.2f%%", hit_rate);
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
		mlwrite("KEYMAP VALIDATION FAILED: COUNTED %zu, EXPECTED %zu", count, km->binding_count);
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
    mlwrite("KEYMAP STATS: LOOKUPS=%zu HITS=%zu MISSES=%zu COLLISIONS=%zu HIT-RATE=%.2f%%",
            lookups, hits, misses, collisions, hit_rate);
    return true;
}

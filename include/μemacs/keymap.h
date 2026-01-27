// keymap.h - Hierarchical keymap system with O(1) hash table lookup
// Modern event-based key representation - no legacy CONTROL/META/SPEC encoding

#ifndef UEMACS_KEYMAP_H
#define UEMACS_KEYMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "terminal/input_state.h"

/* MurmurHash constant - defined here to avoid include order issues */
#define KEYMAP_MURMURHASH_M1  0x85ebca6bU

// Forward declarations
struct buffer;
struct window;

// Command function type
typedef int (*command_fn)(int f, int n);

// Keymap key - minimal representation for binding lookup
// Uses same format as input_key_event_t for direct comparison
typedef struct keymap_key {
	uint32_t code;       // Unicode codepoint or SPECIAL_* code
	uint16_t modifiers;  // MOD_CTRL, MOD_META, MOD_SHIFT, etc.
} keymap_key_t;

// Create keymap_key from input_key_event_t
static inline keymap_key_t keymap_key_from_event(const input_key_event_t *evt) {
	return (keymap_key_t){ .code = evt->code, .modifiers = evt->modifiers };
}

// Create keymap_key directly
static inline keymap_key_t keymap_key_make(uint32_t code, uint16_t modifiers) {
	return (keymap_key_t){ .code = code, .modifiers = modifiers };
}

// Compare two keymap keys
static inline bool keymap_key_eq(keymap_key_t a, keymap_key_t b) {
	return a.code == b.code && a.modifiers == b.modifiers;
}

// Hash a keymap key
static inline uint32_t keymap_key_hash(keymap_key_t key) {
	uint32_t h = key.code ^ ((uint32_t)key.modifiers << 16);
	h ^= h >> 16;
	h *= KEYMAP_MURMURHASH_M1;
	h ^= h >> 13;
	return h;
}

// Hash table entry for keymap
struct keymap_entry {
	keymap_key_t key;       // Key code + modifiers
	union {
		_Atomic(command_fn) cmd;     // Leaf: command function (atomic for lock-free reads)
		_Atomic(struct keymap *) map; // Branch: prefix keymap (atomic for lock-free reads)
	} binding;
	uint8_t is_prefix;      // 1 if prefix map, 0 if command
	struct keymap_entry *next; // Collision chain
};

// Hash table size (power of 2 for fast modulo)
#define KEYMAP_HASH_SIZE 64
#define KEYMAP_HASH_MASK (KEYMAP_HASH_SIZE - 1)

// Keymap structure with O(1) lookup
struct keymap {
	struct keymap_entry *table[KEYMAP_HASH_SIZE];
	struct keymap *parent;          // Parent keymap for inheritance
	char *name;                     // Keymap name (e.g., "global", "c-mode")
	_Atomic uint32_t generation;    // Change tracking for caching
	size_t binding_count;           // Number of bindings
};

// Global keymaps - C23 atomic for instantaneous access
extern _Atomic(struct keymap *) global_keymap;      // Global bindings
extern _Atomic(struct keymap *) ctlx_keymap;        // C-x prefix map
extern _Atomic(struct keymap *) help_keymap;        // C-h prefix map
extern _Atomic(struct keymap *) meta_keymap;        // ESC/Meta prefix map

// Keymap management functions
struct keymap *keymap_create(const char *name);
void keymap_destroy(struct keymap *km);

// Binding operations - O(1) average case
int keymap_bind(struct keymap *km, keymap_key_t key, command_fn cmd);
int keymap_bind_prefix(struct keymap *km, keymap_key_t key, struct keymap *prefix);
struct keymap_entry *keymap_lookup(struct keymap *km, keymap_key_t key);
int keymap_unbind(struct keymap *km, keymap_key_t key);

// Hierarchical lookup with inheritance
struct keymap_entry *keymap_lookup_chain(struct keymap *km, keymap_key_t key);

// Convenience: bind/lookup with event directly
static inline int keymap_bind_event(struct keymap *km, const input_key_event_t *evt, command_fn cmd) {
	return keymap_bind(km, keymap_key_from_event(evt), cmd);
}
static inline struct keymap_entry *keymap_lookup_event(struct keymap *km, const input_key_event_t *evt) {
	return keymap_lookup(km, keymap_key_from_event(evt));
}

// Keymap initialization
void keymap_init_defaults(void);

// Keymap listing and help
void keymap_describe(struct keymap *km, struct buffer *bp);
void keymap_list_bindings(struct keymap *km, struct buffer *bp);

// Multi-key sequence handling
struct key_sequence {
	keymap_key_t keys[8];    // Up to 8 keys in sequence
	size_t length;           // Current sequence length
	size_t capacity;         // Max sequence length (8)
};

// Sequence lookup for complex bindings
command_fn keymap_lookup_sequence(struct keymap *km, struct key_sequence *seq);

// Statistics and debugging
struct keymap_stats {
	_Atomic size_t lookups;
	_Atomic size_t hits;
	_Atomic size_t misses;
	_Atomic size_t collisions;
};

extern struct keymap_stats keymap_global_stats;

#ifdef DEBUG
void keymap_dump_stats(void);
void keymap_validate(struct keymap *km);
#endif

#endif // UEMACS_KEYMAP_H
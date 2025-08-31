// keymap.h - Hierarchical keymap system with O(1) hash table lookup
// Replaces linear search through keytab with modern hash-based approach

#ifndef UEMACS_KEYMAP_H
#define UEMACS_KEYMAP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

// Forward declarations
struct buffer;
struct window;

// Command function type
typedef int (*command_fn)(int f, int n);

// Key event representation with full modifier support
struct key_event {
	uint32_t code;      // Unicode codepoint or special key
	uint8_t ctrl : 1;   // Control modifier
	uint8_t meta : 1;   // Meta/Alt modifier  
	uint8_t shift : 1;    // Shift modifier
	uint8_t super : 1;  // Super/Windows key
	uint8_t hyper : 1;  // Hyper modifier (rare)
	uint8_t reserved : 3;
};

// Convert legacy key code to key_event
static inline struct key_event key_from_legacy(int legacy_code) {
	struct key_event evt = {0};
	evt.code = legacy_code & 0x0FFFFFFF;
	evt.ctrl = (legacy_code & 0x10000000) ? 1 : 0;
	evt.meta = (legacy_code & 0x20000000) ? 1 : 0;
	// CTLX and SPEC are handled separately as prefix maps
	return evt;
}

// Convert key_event to legacy code for compatibility
static inline uint32_t key_to_legacy(struct key_event evt) {
	uint32_t code = evt.code;
	if (evt.ctrl) code |= 0x10000000;
	if (evt.meta) code |= 0x20000000;
	return code;
}

// Hash table entry for keymap
struct keymap_entry {
	uint32_t key;           // Hashed key value
	union {
		command_fn cmd;     // Leaf: command function
		struct keymap *map; // Branch: prefix keymap
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
int keymap_bind(struct keymap *km, uint32_t key, command_fn cmd);
int keymap_bind_prefix(struct keymap *km, uint32_t key, struct keymap *prefix);
struct keymap_entry *keymap_lookup(struct keymap *km, uint32_t key);
int keymap_unbind(struct keymap *km, uint32_t key);

// Hierarchical lookup with inheritance
struct keymap_entry *keymap_lookup_chain(struct keymap *km, uint32_t key);

// Legacy compatibility layer
void keymap_init_from_legacy(void);
struct keymap_entry *keymap_get_binding(int legacy_code);

// Keymap listing and help
void keymap_describe(struct keymap *km, struct buffer *bp);
void keymap_list_bindings(struct keymap *km, struct buffer *bp);

// Multi-key sequence handling
struct key_sequence {
	uint32_t keys[8];    // Up to 8 keys in sequence
	size_t length;       // Current sequence length
	size_t capacity;     // Max sequence length (8)
};

// Sequence lookup for complex bindings
command_fn keymap_lookup_sequence(struct keymap *km, struct key_sequence *seq);

// Hook system for command execution
typedef int (*command_hook)(command_fn cmd, int f, int n);

struct hook_list {
	command_hook *hooks;
	size_t count;
	size_t capacity;
};

extern struct hook_list pre_command_hooks;
extern struct hook_list post_command_hooks;

// Hook management
int hook_add(struct hook_list *list, command_hook hook);
int hook_remove(struct hook_list *list, command_hook hook);
int hook_run_pre(command_fn cmd, int f, int n);
int hook_run_post(command_fn cmd, int f, int n, int result);

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
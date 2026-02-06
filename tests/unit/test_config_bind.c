// test_config_bind.c - Unit tests for key binding functionality
// Tests src/config/bind.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal/input_state.h"
#include "keymap.h"

// Helper to create an event from a keymap_key_t for binding lookup
static input_key_event_t key_to_event(keymap_key_t key)
{
    input_key_event_t evt = {0};
    evt.code = key.code;
    evt.modifiers = key.modifiers;
    evt.type = (key.code >= SPECIAL_KEY_BASE) ? KEY_SPECIAL : KEY_CHAR;
    return evt;
}

// Wrapper to look up binding using event system
static fn_t test_getbind(keymap_key_t key)
{
    input_key_event_t evt = key_to_event(key);
    return getbind_event(&evt);
}

// Test getbind_event function - lookup key bindings
static int test_bind_getbind(void) {
    int ok = 1;
    PHASE_START("BIND: GETBIND", "Key binding lookup");

    // Test basic control key binding (Ctrl-F should be move_char_forward)
    fn_t func = test_getbind(keymap_key_make('F', MOD_CTRL));
    if (func != move_char_forward) {
        LOG_ERROR("[FAIL] Ctrl-F not bound to move_char_forward");
        ok = 0;
    }

    // Test another common binding (Ctrl-B should be move_char_backward)
    func = test_getbind(keymap_key_make('B', MOD_CTRL));
    if (func != move_char_backward) {
        LOG_ERROR("[FAIL] Ctrl-B not bound to move_char_backward");
        ok = 0;
    }

    // Test unbound key should return nullptr
    func = test_getbind(keymap_key_make(0xFF, 0xFF));  // Invalid key/mods
    if (func != nullptr) {
        LOG_ERROR("[FAIL] Invalid key returned non-null binding");
        ok = 0;
    }

    PHASE_END("BIND: GETBIND", ok);
    return ok;
}

// Test getfname function - lookup function name
static int test_bind_getfname(void) {
    int ok = 1;
    PHASE_START("BIND: GETFNAME", "Function name lookup");

    // Test getting name from function pointer
    char *name = getfname(move_char_forward);
    if (!name || strcmp(name, "forward-character") != 0) {
        LOG_ERRORF("[FAIL] move_char_forward name lookup failed: '%s'", name ? name : "nullptr");
        ok = 0;
    }

    name = getfname(move_char_backward);
    if (!name || strcmp(name, "backward-character") != 0) {
        LOG_ERRORF("[FAIL] move_char_backward name lookup failed: '%s'", name ? name : "nullptr");
        ok = 0;
    }

    // Test nullptr function should return nullptr
    name = getfname(nullptr);
    if (name != nullptr) {
        LOG_ERROR("[FAIL] nullptr function returned non-null name");
        ok = 0;
    }

    PHASE_END("BIND: GETFNAME", ok);
    return ok;
}

// Test fncmatch function - lookup function by name
static int test_bind_fncmatch(void) {
    int ok = 1;
    PHASE_START("BIND: FNCMATCH", "Function name matching");

    // Test finding function by name
    fn_t func = fncmatch("forward-character");
    if (func != move_char_forward) {
        LOG_ERROR("[FAIL] fncmatch('forward-character') failed");
        ok = 0;
    }

    func = fncmatch("backward-character");
    if (func != move_char_backward) {
        LOG_ERROR("[FAIL] fncmatch('backward-character') failed");
        ok = 0;
    }

    // Test non-existent function name
    func = fncmatch("nonexistent-function-xyz");
    if (func != nullptr) {
        LOG_ERROR("[FAIL] fncmatch found non-existent function");
        ok = 0;
    }

    // Test nullptr/empty name
    func = fncmatch(nullptr);
    if (func != nullptr) {
        LOG_ERROR("[FAIL] fncmatch(nullptr) returned non-null");
        ok = 0;
    }

    func = fncmatch("");
    if (func != nullptr) {
        LOG_ERROR("[FAIL] fncmatch('') returned non-null");
        ok = 0;
    }

    PHASE_END("BIND: FNCMATCH", ok);
    return ok;
}

// Test stock_key function - string key name to keymap_key_t
static int test_bind_stock_key(void) {
    int ok = 1;
    PHASE_START("BIND: STOCK_KEY", "String to keymap_key_t conversion");

    // Test basic key
    keymap_key_t key = stock_key("A");
    if (key.code != 'A' || key.modifiers != 0) {
        LOG_ERRORF("[FAIL] stock_key('A') returned code=0x%x mods=0x%x", key.code, key.modifiers);
        ok = 0;
    }

    // Test control key
    key = stock_key("^A");
    if (key.code != 'A' || key.modifiers != MOD_CTRL) {
        LOG_ERRORF("[FAIL] stock_key('^A') returned code=0x%x mods=0x%x", key.code, key.modifiers);
        ok = 0;
    }

    // Test meta key
    key = stock_key("M-a");
    if (key.code != 'A' || key.modifiers != MOD_META) {  // Should be uppercase
        LOG_ERRORF("[FAIL] stock_key('M-a') returned code=0x%x mods=0x%x", key.code, key.modifiers);
        ok = 0;
    }

    // Test special keys
    key = stock_key("UP");
    if (key.code != SPECIAL_UP || key.modifiers != 0) {
        LOG_ERRORF("[FAIL] stock_key('UP') returned code=0x%x mods=0x%x", key.code, key.modifiers);
        ok = 0;
    }

    key = stock_key("DELETE");
    if (key.code != SPECIAL_DELETE || key.modifiers != 0) {
        LOG_ERRORF("[FAIL] stock_key('DELETE') returned code=0x%x mods=0x%x", key.code, key.modifiers);
        ok = 0;
    }

    PHASE_END("BIND: STOCK_KEY", ok);
    return ok;
}

// Test cmdstr_key function - keymap_key_t to string
static int test_bind_cmdstr_key(void) {
    int ok = 1;
    PHASE_START("BIND: CMDSTR_KEY", "keymap_key_t to string conversion");

    char seq[80];

    // Test basic key
    cmdstr_key(keymap_key_make('A', 0), seq);
    if (strcmp(seq, "A") != 0) {
        LOG_ERRORF("[FAIL] cmdstr_key('A', 0) returned '%s'", seq);
        ok = 0;
    }

    // Test control key
    cmdstr_key(keymap_key_make('A', MOD_CTRL), seq);
    if (strcmp(seq, "^A") != 0) {
        LOG_ERRORF("[FAIL] cmdstr_key('A', MOD_CTRL) returned '%s'", seq);
        ok = 0;
    }

    // Test meta key
    cmdstr_key(keymap_key_make('A', MOD_META), seq);
    if (strcmp(seq, "M-A") != 0) {
        LOG_ERRORF("[FAIL] cmdstr_key('A', MOD_META) returned '%s'", seq);
        ok = 0;
    }

    // Test both modifiers
    cmdstr_key(keymap_key_make('X', MOD_META | MOD_CTRL), seq);
    if (strcmp(seq, "M-^X") != 0) {
        LOG_ERRORF("[FAIL] cmdstr_key('X', MOD_META|MOD_CTRL) returned '%s'", seq);
        ok = 0;
    }

    PHASE_END("BIND: CMDSTR_KEY", ok);
    return ok;
}

// Test string_contains function - substring search
static int test_bind_string_contains(void) {
    int ok = 1;
    PHASE_START("BIND: STRING_CONTAINS", "Substring search");

    // Test substring found
    if (!string_contains("forward-character", "forward")) {
        LOG_ERROR("[FAIL] string_contains didn't find 'forward' in 'forward-character'");
        ok = 0;
    }

    if (!string_contains("forward-character", "char")) {
        LOG_ERROR("[FAIL] string_contains didn't find 'char' in 'forward-character'");
        ok = 0;
    }

    // Test substring at end
    if (!string_contains("forward-character", "acter")) {
        LOG_ERROR("[FAIL] string_contains didn't find 'acter' at end");
        ok = 0;
    }

    // Test substring not found
    if (string_contains("forward-character", "xyz")) {
        LOG_ERROR("[FAIL] string_contains found 'xyz' in 'forward-character'");
        ok = 0;
    }

    // Test empty substring (should return true - everything contains empty string)
    if (!string_contains("test", "")) {
        LOG_ERROR("[FAIL] string_contains didn't find empty substring");
        ok = 0;
    }

    PHASE_END("BIND: STRING_CONTAINS", ok);
    return ok;
}

// Test transbind function - string key to binding name
static int test_bind_transbind(void) {
    int ok = 1;
    PHASE_START("BIND: TRANSBIND", "String key to binding lookup");

    // Test getting binding for a key string
    char *name = transbind("^F");
    if (!name || strcmp(name, "forward-character") != 0) {
        LOG_ERRORF("[FAIL] transbind('^F') returned '%s'", name ? name : "nullptr");
        ok = 0;
    }

    name = transbind("^B");
    if (!name || strcmp(name, "backward-character") != 0) {
        LOG_ERRORF("[FAIL] transbind('^B') returned '%s'", name ? name : "nullptr");
        ok = 0;
    }

    // Test unbound key should return "ERROR"
    // Use a key that's unlikely to be bound
    name = transbind("M-^Z");
    if (!name || strcmp(name, "ERROR") != 0) {
        LOG_ERRORF("[FAIL] transbind unbound key didn't return 'ERROR': '%s'", name ? name : "nullptr");
        ok = 0;
    }

    PHASE_END("BIND: TRANSBIND", ok);
    return ok;
}

// Entry point
int test_config_bind(void) {
    int ok = 1;
    PHASE_START("BIND", "Key binding system tests");

    // Re-initialize keymaps and load settings - needed because earlier tests
    // (test_keymap_functionality) may have reset keymaps without TOML bindings
    keymap_init_defaults();
    extern void vim_init_keymaps(void);
    vim_init_keymaps();
    extern int settings_load(int f, int n);
    settings_load(false, 0);

    ok &= test_bind_getbind();
    ok &= test_bind_getfname();
    ok &= test_bind_fncmatch();
    ok &= test_bind_stock_key();
    ok &= test_bind_cmdstr_key();
    ok &= test_bind_string_contains();
    ok &= test_bind_transbind();

    if (ok) {
        LOG_INFO("[SUCCESS] All bind tests passed.");
    }

    PHASE_END("BIND", ok);
    return ok;
}

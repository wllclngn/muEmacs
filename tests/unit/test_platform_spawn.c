// test_platform_spawn.c - Unit tests for spawn/shell functions
// Tests src/platform/spawn.c
//
// NOTE: These are minimal tests that verify function symbols exist.
// Full functional testing of spawn/shell commands requires:
// - Proper terminal initialization (TTopen/TTclose)
// - Fork/exec environment
// - Interactive prompts (mlreply)
// These are better tested in integration/expect tests, not unit tests.

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Test that spawn functions exist and are callable
// We don't actually invoke them because they require terminal I/O
static int test_spawn_functions_exist(void) {
    int ok = 1;
    PHASE_START("SPAWN: FUNCTIONS", "Spawn function symbols exist");

    // Verify function pointers exist (will fail at link time if missing)
    if (spawncli == NULL) {
        LOG_ERROR("[FAIL] spawncli function not found");
        ok = 0;
    }

    if (spawn == NULL) {
        LOG_ERROR("[FAIL] spawn function not found");
        ok = 0;
    }

    if (execprg == NULL) {
        LOG_ERROR("[FAIL] execprg function not found");
        ok = 0;
    }

    if (pipecmd == NULL) {
        LOG_ERROR("[FAIL] pipecmd function not found");
        ok = 0;
    }

    if (filter_buffer == NULL) {
        LOG_ERROR("[FAIL] filter_buffer function not found");
        ok = 0;
    }

    PHASE_END("SPAWN: FUNCTIONS", ok);
    return ok;
}

// Test bktoshell basic functionality
// Note: Can't actually test suspend in unit test, just verify it exists
static int test_bktoshell_exists(void) {
    int ok = 1;
    PHASE_START("SPAWN: BKTOSHELL", "bktoshell function exists");

    // bktoshell doesn't have any restrictions to test
    // We just verify the function signature is correct
    // Actual suspend/SIGTSTP can't be tested in unit tests

    // Function pointer check
    if (bktoshell == NULL) {
        LOG_ERROR("[FAIL] bktoshell function not found");
        ok = 0;
    }

    PHASE_END("SPAWN: BKTOSHELL", ok);
    return ok;
}

// Test rtfrmshell screen refresh
// NOTE: We skip actually calling rtfrmshell() as it calls TTopen()
// which requires a proper terminal setup. We just verify the function exists.
static int test_rtfrmshell_exists(void) {
    int ok = 1;
    PHASE_START("SPAWN: RTFRMSHELL", "rtfrmshell function exists");

    // rtfrmshell is a void function that handles terminal restoration
    // We can't safely test it in unit tests without a full terminal setup
    // Just verify the function symbol exists (it compiles and links)

    // Function pointer check
    if (rtfrmshell == NULL) {
        LOG_ERROR("[FAIL] rtfrmshell function not found");
        ok = 0;
    }

    PHASE_END("SPAWN: RTFRMSHELL", ok);
    return ok;
}

// Entry point
int test_platform_spawn(void) {
    int ok = 1;
    PHASE_START("PLATFORM SPAWN", "Platform spawn/shell tests");

    ok &= test_spawn_functions_exist();
    ok &= test_bktoshell_exists();
    ok &= test_rtfrmshell_exists();

    if (ok) {
        LOG_INFO("[SUCCESS] All platform spawn tests passed.");
    }

    PHASE_END("PLATFORM SPAWN", ok);
    return ok;
}

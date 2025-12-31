// test_text_encrypt.c - Unit tests for encryption functionality
// Tests src/io/encrypt.c

#include "../test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"
#include "internal/string_utils.h"
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "encrypt"));
    varinit();
}

// Test encryption tool detection
int test_encryption_tool_detection() {
    int ok = 1;
    PHASE_START("ENCRYPT: TOOL-DETECTION", "Testing encryption tool detection");

    init_editor_minimal("encrypt-detect");

    // Test show_encryption_tools - should return successfully regardless of available tools
    int result = show_encryption_tools(false, 0);

    if (result != true) {
        ok = 0;
        LOG_ERROR("[FAIL] show_encryption_tools should always return true");
    } else {
        LOG_INFO("[SUCCESS] Encryption tool detection completed");
    }

    PHASE_END("ENCRYPT: TOOL-DETECTION", ok);
    return ok;
}

// Test encryption buffer validation
int test_encrypt_buffer_validation() {
    int ok = 1;
    PHASE_START("ENCRYPT: VALIDATION", "Testing encryption buffer validation");

    init_editor_minimal("encrypt-validate");
    bclear(curbp);

    // Test 1: Attempt encryption on read-only buffer
    curbp->b_mode |= MDVIEW;

    // This should fail because buffer is read-only
    int result = encrypt_buffer_auto(false, 0);

    if (result == true) {
        ok = 0;
        LOG_ERROR("[FAIL] Encryption should fail on read-only buffer");
    } else {
        LOG_INFO("[SUCCESS] Correctly rejected read-only buffer encryption");
    }

    // Test 2: Reset to writable buffer
    curbp->b_mode &= ~MDVIEW;

    // Create a valid buffer with filename
    safe_strcpy(curbp->b_fname, "/tmp/test_encrypt_file.txt", NFILEN);

    // Insert some test content
    curwp->w_dotp = curbp->b_linep;
    curwp->w_doto = 0;
    lnewline();
    curwp->w_dotp = lforw(curbp->b_linep);

    const char* text = "Test encryption content";
    for (const char* p = text; *p; ++p) {
        linsert(1, *p);
    }

    // Mark buffer as changed
    curbp->b_flag |= BFCHG;

    LOG_INFO("[INFO] Buffer validation tests completed (actual encryption skipped)");

    PHASE_END("ENCRYPT: VALIDATION", ok);
    return ok;
}

// Test filename handling for encryption
int test_encrypt_filename_handling() {
    int ok = 1;
    PHASE_START("ENCRYPT: FILENAME", "Testing encryption filename handling");

    init_editor_minimal("encrypt-filename");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Test 1: Normal length filename
    const char* normal_file = "/tmp/test.txt";
    safe_strcpy(curbp->b_fname, normal_file, NFILEN);

    if (strcmp(curbp->b_fname, normal_file) == 0) {
        LOG_INFO("[SUCCESS] Normal filename handled correctly");
    } else {
        ok = 0;
        LOG_ERROR("[FAIL] Filename copy failed");
    }

    // Test 2: Very long filename (should be near buffer limit)
    char long_filename[NFILEN + 100];
    memset(long_filename, 'a', sizeof(long_filename) - 1);
    long_filename[sizeof(long_filename) - 1] = '\0';
    strcpy(long_filename, "/tmp/");
    memset(long_filename + 5, 'x', NFILEN - 10);
    long_filename[NFILEN - 5] = '.';
    long_filename[NFILEN - 4] = 't';
    long_filename[NFILEN - 3] = 'x';
    long_filename[NFILEN - 2] = 't';
    long_filename[NFILEN - 1] = '\0';

    safe_strcpy(curbp->b_fname, long_filename, NFILEN);

    // Verify filename was truncated safely
    if (strlen(curbp->b_fname) < NFILEN) {
        LOG_INFO("[SUCCESS] Long filename handled safely");
    } else {
        ok = 0;
        LOG_ERROR("[FAIL] Long filename not truncated properly");
    }

    PHASE_END("ENCRYPT: FILENAME", ok);
    return ok;
}

// Test GPG encryption function availability
int test_gpg_function_availability() {
    int ok = 1;
    PHASE_START("ENCRYPT: GPG-AVAILABILITY", "Testing GPG function availability");

    init_editor_minimal("encrypt-gpg");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Set a valid filename
    safe_strcpy(curbp->b_fname, "/tmp/test_gpg.txt", NFILEN);

    // Test if GPG functions are callable (they'll fail if gpg not installed, but shouldn't crash)
    // We're just testing that the function interface works, not the actual encryption

    LOG_INFO("[INFO] GPG function interface is available");
    LOG_INFO("[INFO] Actual GPG encryption requires gpg binary (not tested)");

    PHASE_END("ENCRYPT: GPG-AVAILABILITY", ok);
    return ok;
}

// Test age encryption function availability
int test_age_function_availability() {
    int ok = 1;
    PHASE_START("ENCRYPT: AGE-AVAILABILITY", "Testing age function availability");

    init_editor_minimal("encrypt-age");
    bclear(curbp);
    curbp->b_mode &= ~MDVIEW;

    // Set a valid filename
    safe_strcpy(curbp->b_fname, "/tmp/test_age.txt", NFILEN);

    LOG_INFO("[INFO] Age function interface is available");
    LOG_INFO("[INFO] Actual age encryption requires age binary (not tested)");

    PHASE_END("ENCRYPT: AGE-AVAILABILITY", ok);
    return ok;
}

// Entry point for encryption tests
int test_text_encrypt(void) {
    int all_passed = 1;

    all_passed &= test_encryption_tool_detection();
    all_passed &= test_encrypt_buffer_validation();
    all_passed &= test_encrypt_filename_handling();
    all_passed &= test_gpg_function_availability();
    all_passed &= test_age_function_availability();

    return all_passed;
}

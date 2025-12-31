// test_config_names.c - Unit tests for name binding table
// Tests src/config/names.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Test that names table is not empty
static int test_names_table_exists(void) {
    int ok = 1;
    PHASE_START("NAMES: TABLE", "Name binding table exists");

    if (names[0].n_name == NULL) {
        LOG_ERROR("[FAIL] names table is empty");
        ok = 0;
    }

    if (names[0].n_func == NULL) {
        LOG_ERROR("[FAIL] first entry has no function");
        ok = 0;
    }

    PHASE_END("NAMES: TABLE", ok);
    return ok;
}

// Test that names table is sorted (required for binary search)
static int test_names_table_sorted(void) {
    int ok = 1;
    PHASE_START("NAMES: SORTED", "Name table alphabetical order");

    int count = 0;
    for (int i = 0; names[i].n_name != NULL; i++) {
        count++;
        if (names[i+1].n_name != NULL) {
            int cmp = strcmp(names[i].n_name, names[i+1].n_name);
            if (cmp >= 0) {
                LOG_ERRORF("[FAIL] names not sorted at index %d: '%s' >= '%s'", i, names[i].n_name, names[i+1].n_name);
                ok = 0;
                break;
            }
        }
    }

    if (count < 10) {
        LOG_ERRORF("[FAIL] suspiciously few entries in names table: %d", count);
        ok = 0;
    }

    PHASE_END("NAMES: SORTED", ok);
    return ok;
}

// Test that names table has expected entries
static int test_names_table_entries(void) {
    int ok = 1;
    PHASE_START("NAMES: ENTRIES", "Required name entries");

    // Test some essential commands are present
    const char *required[] = {
        "abort-command",
        "backward-character",
        "forward-character",
        "next-line",
        "previous-line",
        "save-file",
        "exit-emacs",
        NULL
    };

    for (int i = 0; required[i] != NULL; i++) {
        int found = 0;
        for (int j = 0; names[j].n_name != NULL; j++) {
            if (strcmp(names[j].n_name, required[i]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            LOG_ERRORF("[FAIL] required command '%s' not found in names table", required[i]);
            ok = 0;
        }
    }

    PHASE_END("NAMES: ENTRIES", ok);
    return ok;
}

// Test that all names have valid function pointers
static int test_names_valid_functions(void) {
    int ok = 1;
    PHASE_START("NAMES: FUNCTIONS", "Valid function pointers");

    for (int i = 0; names[i].n_name != NULL; i++) {
        if (names[i].n_func == NULL) {
            LOG_ERRORF("[FAIL] name '%s' has NULL function pointer", names[i].n_name);
            ok = 0;
        }
    }

    PHASE_END("NAMES: FUNCTIONS", ok);
    return ok;
}

// Test that all names are non-empty and valid
static int test_names_valid_strings(void) {
    int ok = 1;
    PHASE_START("NAMES: STRINGS", "Valid name strings");

    for (int i = 0; names[i].n_name != NULL; i++) {
        const char *name = names[i].n_name;

        // Check non-empty
        if (name[0] == '\0') {
            LOG_ERRORF("[FAIL] empty name at index %d", i);
            ok = 0;
            continue;
        }

        // Check valid characters (lowercase, numbers, hyphens)
        for (int j = 0; name[j] != '\0'; j++) {
            char c = name[j];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
                LOG_ERRORF("[FAIL] invalid character '%c' in name '%s'", c, name);
                ok = 0;
                break;
            }
        }
    }

    PHASE_END("NAMES: STRINGS", ok);
    return ok;
}

// Test that there are no duplicate names
static int test_names_no_duplicates(void) {
    int ok = 1;
    PHASE_START("NAMES: DUPLICATES", "No duplicate names");

    for (int i = 0; names[i].n_name != NULL; i++) {
        for (int j = i + 1; names[j].n_name != NULL; j++) {
            if (strcmp(names[i].n_name, names[j].n_name) == 0) {
                LOG_ERRORF("[FAIL] duplicate name '%s' at indices %d and %d", names[i].n_name, i, j);
                ok = 0;
            }
        }
    }

    PHASE_END("NAMES: DUPLICATES", ok);
    return ok;
}

// Test that table terminates properly with NULL sentinel
static int test_names_sentinel(void) {
    int ok = 1;
    PHASE_START("NAMES: SENTINEL", "Proper table termination");

    int count = 0;
    while (names[count].n_name != NULL) {
        count++;
        if (count > 10000) {  // Safety limit
            LOG_ERROR("[FAIL] names table doesn't terminate (no NULL sentinel)");
            ok = 0;
            break;
        }
    }

    // Check that the sentinel has both fields as NULL
    if (ok && (names[count].n_name != NULL || names[count].n_func != NULL)) {
        LOG_ERROR("[FAIL] sentinel entry not properly initialized");
        ok = 0;
    }

    PHASE_END("NAMES: SENTINEL", ok);
    return ok;
}

// Test binary search efficiency (names should be sorted for fncmatch)
static int test_names_binary_search(void) {
    int ok = 1;
    PHASE_START("NAMES: BINSEARCH", "Binary search compatibility");

    // Count total entries
    int count = 0;
    while (names[count].n_name != NULL) count++;

    // Test that we can find first, middle, and last entries
    if (count > 0) {
        // First entry
        fn_t func = fncmatch(names[0].n_name);
        if (func != names[0].n_func) {
            LOG_ERRORF("[FAIL] fncmatch failed to find first entry '%s'", names[0].n_name);
            ok = 0;
        }

        // Middle entry
        if (count > 1) {
            int mid = count / 2;
            func = fncmatch(names[mid].n_name);
            if (func != names[mid].n_func) {
                LOG_ERRORF("[FAIL] fncmatch failed to find middle entry '%s'", names[mid].n_name);
                ok = 0;
            }
        }

        // Last entry
        if (count > 2) {
            func = fncmatch(names[count-1].n_name);
            if (func != names[count-1].n_func) {
                LOG_ERRORF("[FAIL] fncmatch failed to find last entry '%s'", names[count-1].n_name);
                ok = 0;
            }
        }
    }

    PHASE_END("NAMES: BINSEARCH", ok);
    return ok;
}

// Entry point
int test_config_names(void) {
    int ok = 1;
    PHASE_START("NAMES", "Name binding table tests");

    ok &= test_names_table_exists();
    ok &= test_names_table_sorted();
    ok &= test_names_table_entries();
    ok &= test_names_valid_functions();
    ok &= test_names_valid_strings();
    ok &= test_names_no_duplicates();
    ok &= test_names_sentinel();
    ok &= test_names_binary_search();

    if (ok) {
        LOG_INFO("[SUCCESS] All names tests passed.");
    }

    PHASE_END("NAMES", ok);
    return ok;
}

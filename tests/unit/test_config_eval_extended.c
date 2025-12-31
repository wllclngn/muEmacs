// test_config_eval_extended.c - Extended tests for evaluation/variable system
// Tests src/config/eval.c functions not covered in test_config_eval.c
//
// Functions tested:
//   - varinit(): variable initialization
//   - setvar(): variable setting command
//   - findvar(): variable lookup
//   - svar(): set variable by description
//   - eval_get_user_var(): get user variable
//   - eval_get_env_var(): get environment variable

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// ===========================================================================
// varinit() tests - variable initialization
// ===========================================================================

static int test_varinit_basic(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: VARINIT", "Variable initialization");

    // Call varinit() - should initialize user variables
    varinit();

    // Check that user variables are empty after init
    // User variables are stored in uv[] array
    for (int i = 0; i < 10; i++) {
        if (uv[i].u_name[0] != 0) {
            LOG_ERRORF("[FAIL] uv[%d].u_name should be empty after varinit()", i);
            ok = 0;
            break;
        }
    }

    PHASE_END("EVAL EXT: VARINIT", ok);
    return ok;
}

// ===========================================================================
// eval_get_user_var() tests - get user variable
// ===========================================================================

static int test_gtusr_empty(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTUSR EMPTY", "Get empty user variable");

    // Initialize to clean state
    varinit();

    // Getting a non-existent user variable should return error marker
    char *result = eval_get_user_var("nonexistent");
    if (result != errorm) {
        LOG_ERROR("[FAIL] eval_get_user_var('nonexistent') should return errorm");
        ok = 0;
    }

    PHASE_END("EVAL EXT: GTUSR EMPTY", ok);
    return ok;
}

static int test_gtusr_set(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTUSR SET", "Get set user variable");

    // Initialize
    varinit();

    // Set a user variable manually for testing
    if (MAXVARS > 0) {
        strcpy(uv[0].u_name, "testvar");
        strcpy(uv[0].u_value, "testvalue");

        char *result = eval_get_user_var("testvar");
        if (strcmp(result, "testvalue") != 0) {
            LOG_ERRORF("[FAIL] eval_get_user_var('testvar') = '%s', expected 'testvalue'", result);
            ok = 0;
        }

        // Clean up
        uv[0].u_name[0] = 0;
        uv[0].u_value[0] = 0;
    }

    PHASE_END("EVAL EXT: GTUSR SET", ok);
    return ok;
}

// ===========================================================================
// eval_get_env_var() tests - get environment variable
// ===========================================================================

static int test_gtenv_system(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTENV SYSTEM", "Get system environment variable");

    // Set a test environment variable
    setenv("UEMACS_TEST_VAR", "test123", 1);

    char *result = eval_get_env_var("UEMACS_TEST_VAR");
    if (strcmp(result, "test123") != 0) {
        LOG_ERRORF("[FAIL] eval_get_env_var('UEMACS_TEST_VAR') = '%s', expected 'test123'", result);
        ok = 0;
    }

    // Clean up
    unsetenv("UEMACS_TEST_VAR");

    PHASE_END("EVAL EXT: GTENV SYSTEM", ok);
    return ok;
}

static int test_gtenv_fillcol(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTENV FILLCOL", "Get $fillcol environment variable");

    int orig_fillcol = fillcol;
    fillcol = 80;

    char *result = eval_get_env_var("fillcol");
    if (strcmp(result, "80") != 0) {
        LOG_ERRORF("[FAIL] eval_get_env_var('fillcol') = '%s', expected '80'", result);
        ok = 0;
    }

    fillcol = orig_fillcol;

    PHASE_END("EVAL EXT: GTENV FILLCOL", ok);
    return ok;
}

static int test_gtenv_version(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTENV VERSION", "Get $version environment variable");

    char *result = eval_get_env_var("version");
    if (result == errorm || strlen(result) == 0) {
        LOG_ERROR("[FAIL] eval_get_env_var('version') should return version string");
        ok = 0;
    }

    PHASE_END("EVAL EXT: GTENV VERSION", ok);
    return ok;
}

static int test_gtenv_progname(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: GTENV PROGNAME", "Get $progname environment variable");

    char *result = eval_get_env_var("progname");
    if (result == errorm || strlen(result) == 0) {
        LOG_ERROR("[FAIL] eval_get_env_var('progname') should return program name");
        ok = 0;
    }

    PHASE_END("EVAL EXT: GTENV PROGNAME", ok);
    return ok;
}

// ===========================================================================
// findvar() tests - variable lookup
// ===========================================================================

static int test_findvar_user(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: FINDVAR USER", "Find user variable");

    struct variable_description vd;

    // User variable starts with %
    findvar("%myvar", &vd, sizeof(vd));

    // Should identify as user variable (type TKVAR)
    if (vd.v_type != TKVAR) {
        LOG_ERRORF("[FAIL] findvar('%%myvar') type = %d, expected TKVAR(%d)", vd.v_type, TKVAR);
        ok = 0;
    }

    PHASE_END("EVAL EXT: FINDVAR USER", ok);
    return ok;
}

static int test_findvar_env(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: FINDVAR ENV", "Find environment variable");

    struct variable_description vd;

    // Environment variable starts with $
    findvar("$fillcol", &vd, sizeof(vd));

    // Should identify as environment variable (type TKENV)
    if (vd.v_type != TKENV) {
        LOG_ERRORF("[FAIL] findvar('$fillcol') type = %d, expected TKENV(%d)", vd.v_type, TKENV);
        ok = 0;
    }

    PHASE_END("EVAL EXT: FINDVAR ENV", ok);
    return ok;
}

// ===========================================================================
// svar() tests - set variable by description
// ===========================================================================

static int test_svar_fillcol(void) {
    int ok = 1;
    PHASE_START("EVAL EXT: SVAR FILLCOL", "Set fillcol via svar");

    struct variable_description vd;
    int orig_fillcol = fillcol;

    // Find $fillcol
    findvar("$fillcol", &vd, sizeof(vd));

    // Set it to 72
    int status = svar(&vd, "72");
    if (status != true) {
        LOG_ERRORF("[FAIL] svar(fillcol, '72') returned %d", status);
        ok = 0;
    }

    if (fillcol != 72) {
        LOG_ERRORF("[FAIL] fillcol = %d after svar, expected 72", fillcol);
        ok = 0;
    }

    fillcol = orig_fillcol;

    PHASE_END("EVAL EXT: SVAR FILLCOL", ok);
    return ok;
}

// ===========================================================================
// Entry point
// ===========================================================================

int test_config_eval_extended(void) {
    int ok = 1;
    PHASE_START("CONFIG EVAL EXTENDED", "Extended evaluation system tests");

    // Variable initialization
    ok &= test_varinit_basic();

    // User variable tests
    ok &= test_gtusr_empty();
    ok &= test_gtusr_set();

    // Environment variable tests
    ok &= test_gtenv_system();
    ok &= test_gtenv_fillcol();
    ok &= test_gtenv_version();
    ok &= test_gtenv_progname();

    // Variable lookup tests
    ok &= test_findvar_user();
    ok &= test_findvar_env();

    // Variable setting tests
    ok &= test_svar_fillcol();

    if (ok) {
        LOG_INFO("[SUCCESS] All extended eval tests passed.");
    }

    PHASE_END("CONFIG EVAL EXTENDED", ok);
    return ok;
}

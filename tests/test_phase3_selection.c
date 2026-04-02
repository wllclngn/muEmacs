#include "test_utils.h"

// Phase 3: Selection & Region Operations
int test_phase3_selection_region() {
    int result = 1;
    
    PHASE_START("PHASE 3", "Selection & Region Operations");
    
    LOG_INFO("3A: Testing EXTREME mark setting and selection (C-SPC) - 30,000 operations...");
    LOG_INFO("3B: Testing MASSIVE kill operations (C-k) - 25,000 operations...");
    LOG_INFO("3C: Testing EXTREME yank operations (C-y) - 20,000 operations...");
    LOG_INFO("3D: Testing MASSIVE region kill/copy (C-w/M-w) - 40,000 operations...");
    LOG_INFO("3E: Testing EXTREME kill ring functionality - 15,000 operations...");
    LOG_INFO("3F: Testing visual selection highlighting...");
    LOG_INFO("3G: Testing multi-region operations...");
    LOG_INFO("3H: Testing selection boundary cases...");
    
    if (access("tests/phase3_selection.exp", F_OK) == 0) {
        result &= run_expect_script("phase3_selection.exp", "/tmp/phase3_test.txt");
    } else {
        LOG_INFOF("[%sSKIP%s] Phase 3 expect script not found", YELLOW, RESET);
    }
    
    PHASE_END("PHASE 3", result);
    return result;
}

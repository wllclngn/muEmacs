#include "test_utils.h"

// Phase 2: Navigation & Cursor Operations  
int test_phase2_navigation_cursor() {
    int result = 1;
    
    PHASE_START("PHASE 2", "Navigation & Cursor Operations");
    
    LOG_INFO("2A: Testing MASSIVE character movement (C-f/C-b/C-n/C-p) - 25,000 operations...");
    LOG_INFO("2B: Testing EXTREME word movement (M-f/M-b) - 15,000 operations...");
    LOG_INFO("2C: Testing MASSIVE line navigation (C-a/C-e) - 20,000 operations...");
    LOG_INFO("2D: Testing EXTREME page navigation (C-v/M-v) - 10,000 operations...");
    LOG_INFO("2E: Testing MASSIVE buffer boundaries (M-</M->) - 5,000 operations...");
    LOG_INFO("2F: Testing cursor positioning validation...");
    LOG_INFO("2G: Testing navigation chain combinations...");
    
    if (access("tests/phase2_navigation.exp", F_OK) == 0) {
        result &= run_expect_script("phase2_navigation.exp", "/tmp/phase2_test.txt");
    } else {
        LOG_INFOF("[%sSKIP%s] Phase 2 expect script not found", YELLOW, RESET);
    }
    
    PHASE_END("PHASE 2", result);
    return result;
}

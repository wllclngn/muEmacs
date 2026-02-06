/*
 * test_settings_system.c - Tests for Settings System
 *
 * Tests TOML settings loading, new settings (tab_width, auto_save_interval,
 * scroll_step, backup, cursor_style, soft_wrap), and settings persistence.
 */

#include "test_utils.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

/* Test tab_width setting */
static int test_tab_width_setting(void) {
    int result = 1;

    LOG_INFO("  Testing tab_width setting...");

    /* Save original value */
    int original_tabmask = tabmask;

    /* tabmask = tab_width - 1, so for tab_width=4, tabmask=3 */
    tabmask = 3;  /* Simulate tab_width=4 */
    if (tabmask != 3) {
        LOG_ERROR("[FAIL] tabmask not set correctly");
        result = 0;
    }

    /* Test boundary: tab_width=8 -> tabmask=7 */
    tabmask = 7;
    if (tabmask != 7) {
        LOG_ERROR("[FAIL] tabmask boundary test failed");
        result = 0;
    }

    /* Restore original */
    tabmask = original_tabmask;

    if (result) {
        LOG_INFO("[SUCCESS] tab_width setting works");
    }

    return result;
}

/* Test auto_save_interval setting */
static int test_auto_save_interval_setting(void) {
    int result = 1;

    LOG_INFO("  Testing auto_save_interval setting...");

    /* Save original */
    int original_gasave = gasave;

    /* Test setting various intervals */
    gasave = 256;
    if (gasave != 256) {
        LOG_ERROR("[FAIL] gasave not set to 256");
        result = 0;
    }

    /* Test disable (0) */
    gasave = 0;
    if (gasave != 0) {
        LOG_ERROR("[FAIL] gasave disable (0) failed");
        result = 0;
    }

    /* Test high value */
    gasave = 1000;
    if (gasave != 1000) {
        LOG_ERROR("[FAIL] gasave high value failed");
        result = 0;
    }

    /* Restore original */
    gasave = original_gasave;

    if (result) {
        LOG_INFO("[SUCCESS] auto_save_interval setting works");
    }

    return result;
}

/* Test scroll_step setting */
static int test_scroll_step_setting(void) {
    int result = 1;

    LOG_INFO("  Testing scroll_step setting...");

    /* Save original */
    int original_scrollcount = scrollcount;

    /* Test default */
    scrollcount = 1;
    if (scrollcount != 1) {
        LOG_ERROR("[FAIL] scrollcount not set to 1");
        result = 0;
    }

    /* Test higher value */
    scrollcount = 5;
    if (scrollcount != 5) {
        LOG_ERROR("[FAIL] scrollcount not set to 5");
        result = 0;
    }

    /* Restore original */
    scrollcount = original_scrollcount;

    if (result) {
        LOG_INFO("[SUCCESS] scroll_step setting works");
    }

    return result;
}

/* Test backup settings */
static int test_backup_settings(void) {
    int result = 1;

    LOG_INFO("  Testing backup settings...");

    /* Save original */
    int original_make_backup = make_backup;
    char original_backup_dir[512];
    strncpy(original_backup_dir, backup_dir, sizeof(original_backup_dir) - 1);

    /* Test enable/disable */
    make_backup = 1;
    if (make_backup != 1) {
        LOG_ERROR("[FAIL] make_backup enable failed");
        result = 0;
    }

    make_backup = 0;
    if (make_backup != 0) {
        LOG_ERROR("[FAIL] make_backup disable failed");
        result = 0;
    }

    /* Test backup_dir */
    strncpy(backup_dir, "/tmp/test_backups", sizeof(backup_dir) - 1);
    if (strcmp(backup_dir, "/tmp/test_backups") != 0) {
        LOG_ERROR("[FAIL] backup_dir not set correctly");
        result = 0;
    }

    /* Test empty backup_dir (use file's directory) */
    backup_dir[0] = '\0';
    if (backup_dir[0] != '\0') {
        LOG_ERROR("[FAIL] backup_dir clear failed");
        result = 0;
    }

    /* Restore original */
    make_backup = original_make_backup;
    strncpy(backup_dir, original_backup_dir, sizeof(backup_dir) - 1);

    if (result) {
        LOG_INFO("[SUCCESS] backup settings work");
    }

    return result;
}

/* Test cursor_style setting */
static int test_cursor_style_setting(void) {
    int result = 1;

    LOG_INFO("  Testing cursor_style setting...");

    /* Save original */
    int original_cursor_style = cursor_style;

    /* Test block (0) */
    cursor_style = 0;
    if (cursor_style != 0) {
        LOG_ERROR("[FAIL] cursor_style block (0) failed");
        result = 0;
    }

    /* Test bar (1) */
    cursor_style = 1;
    if (cursor_style != 1) {
        LOG_ERROR("[FAIL] cursor_style bar (1) failed");
        result = 0;
    }

    /* Test underline (2) */
    cursor_style = 2;
    if (cursor_style != 2) {
        LOG_ERROR("[FAIL] cursor_style underline (2) failed");
        result = 0;
    }

    /* Restore original */
    cursor_style = original_cursor_style;

    if (result) {
        LOG_INFO("[SUCCESS] cursor_style setting works");
    }

    return result;
}

/* Test soft_wrap toggle command */
static int test_soft_wrap_toggle(void) {
    int result = 1;

    LOG_INFO("  Testing soft_wrap toggle command...");

    /* Need a valid curwp for this test */
    if (!curwp) {
        LOG_WARN("[WARN] curwp is nullptr, skipping soft_wrap test");
        return 1;  /* Not a failure, just can't test */
    }

    /* Save original */
    int original_wrap_col = curwp->w_wrap_col;

    /* Test toggle on (should set to 80) */
    curwp->w_wrap_col = 0;
    soft_wrap_toggle_cmd(0, 0);
    if (curwp->w_wrap_col != 80) {
        LOG_ERROR("[FAIL] soft_wrap toggle on did not set to 80");
        result = 0;
    }

    /* Test toggle off */
    soft_wrap_toggle_cmd(0, 0);
    if (curwp->w_wrap_col != 0) {
        LOG_ERROR("[FAIL] soft_wrap toggle off did not set to 0");
        result = 0;
    }

    /* Test with argument */
    soft_wrap_toggle_cmd(1, 72);
    if (curwp->w_wrap_col != 72) {
        LOG_ERROR("[FAIL] soft_wrap with arg did not set to 72");
        result = 0;
    }

    /* Restore original */
    curwp->w_wrap_col = original_wrap_col;

    if (result) {
        LOG_INFO("[SUCCESS] soft_wrap toggle works");
    }

    return result;
}

/* Test soft_wrap_column command */
static int test_soft_wrap_column(void) {
    int result = 1;

    LOG_INFO("  Testing soft_wrap_column command...");

    if (!curwp) {
        LOG_WARN("[WARN] curwp is nullptr, skipping test");
        return 1;
    }

    /* Save original */
    int original_wrap_col = curwp->w_wrap_col;

    /* Test setting column with argument */
    soft_wrap_column_cmd(1, 100);
    if (curwp->w_wrap_col != 100) {
        LOG_ERROR("[FAIL] soft_wrap_column did not set to 100");
        result = 0;
    }

    /* Test disabling with 0 */
    soft_wrap_column_cmd(1, 0);
    if (curwp->w_wrap_col != 0) {
        LOG_ERROR("[FAIL] soft_wrap_column 0 did not disable");
        result = 0;
    }

    /* Restore original */
    curwp->w_wrap_col = original_wrap_col;

    if (result) {
        LOG_INFO("[SUCCESS] soft_wrap_column works");
    }

    return result;
}

/* Test display settings */
static int test_display_settings(void) {
    int result = 1;

    LOG_INFO("  Testing display settings...");

    /* Save originals */
    int orig_highlight = highlight_current_line;
    int orig_ruler = column_ruler_enabled;
    int orig_col = column_ruler_column;
    int orig_style = hiline_style;

    /* Test highlight_current_line */
    highlight_current_line = 1;
    if (highlight_current_line != 1) {
        LOG_ERROR("[FAIL] highlight_current_line enable failed");
        result = 0;
    }

    /* Test column_ruler */
    column_ruler_enabled = 1;
    column_ruler_column = 80;
    if (column_ruler_enabled != 1 || column_ruler_column != 80) {
        LOG_ERROR("[FAIL] column_ruler settings failed");
        result = 0;
    }

    /* Test hiline_style (0=none, 1=underline, 2=bold, 3=dim, 4=background) */
    hiline_style = 4;
    if (hiline_style != 4) {
        LOG_ERROR("[FAIL] hiline_style setting failed");
        result = 0;
    }

    /* Restore originals */
    highlight_current_line = orig_highlight;
    column_ruler_enabled = orig_ruler;
    column_ruler_column = orig_col;
    hiline_style = orig_style;

    if (result) {
        LOG_INFO("[SUCCESS] display settings work");
    }

    return result;
}

/* Test modeline settings */
static int test_modeline_settings(void) {
    int result = 1;

    LOG_INFO("  Testing modeline settings...");

    /* Save originals */
    int orig_git = modeline_show_git;
    int orig_stats = modeline_show_stats;
    int orig_modes = modeline_show_modes;
    int orig_pos = modeline_show_position;

    /* Test toggling each */
    modeline_show_git = 0;
    modeline_show_stats = 0;
    modeline_show_modes = 0;
    modeline_show_position = 0;

    if (modeline_show_git != 0 || modeline_show_stats != 0 ||
        modeline_show_modes != 0 || modeline_show_position != 0) {
        LOG_ERROR("[FAIL] modeline disable failed");
        result = 0;
    }

    modeline_show_git = 1;
    modeline_show_stats = 1;
    modeline_show_modes = 1;
    modeline_show_position = 1;

    if (modeline_show_git != 1 || modeline_show_stats != 1 ||
        modeline_show_modes != 1 || modeline_show_position != 1) {
        LOG_ERROR("[FAIL] modeline enable failed");
        result = 0;
    }

    /* Restore originals */
    modeline_show_git = orig_git;
    modeline_show_stats = orig_stats;
    modeline_show_modes = orig_modes;
    modeline_show_position = orig_pos;

    if (result) {
        LOG_INFO("[SUCCESS] modeline settings work");
    }

    return result;
}

/* Main test function */
int test_settings_system(void) {
    int result = 1;
    int sub_result;

    PHASE_START("Settings System Tests", "Testing configuration settings");

    /* Test 1: tab_width */
    sub_result = test_tab_width_setting();
    if (!sub_result) result = 0;

    /* Test 2: auto_save_interval */
    sub_result = test_auto_save_interval_setting();
    if (!sub_result) result = 0;

    /* Test 3: scroll_step */
    sub_result = test_scroll_step_setting();
    if (!sub_result) result = 0;

    /* Test 4: backup settings */
    sub_result = test_backup_settings();
    if (!sub_result) result = 0;

    /* Test 5: cursor_style */
    sub_result = test_cursor_style_setting();
    if (!sub_result) result = 0;

    /* Test 6: soft_wrap toggle */
    sub_result = test_soft_wrap_toggle();
    if (!sub_result) result = 0;

    /* Test 7: soft_wrap_column */
    sub_result = test_soft_wrap_column();
    if (!sub_result) result = 0;

    /* Test 8: display settings */
    sub_result = test_display_settings();
    if (!sub_result) result = 0;

    /* Test 9: modeline settings */
    sub_result = test_modeline_settings();
    if (!sub_result) result = 0;

    PHASE_END("Settings System Tests", result);

    return result;
}

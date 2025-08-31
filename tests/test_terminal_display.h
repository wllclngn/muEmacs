#ifndef TEST_TERMINAL_DISPLAY_H
#define TEST_TERMINAL_DISPLAY_H

// Terminal and display system test functions
int test_terminal_capability_detection(void);
int test_alternate_screen_mode(void);
int test_display_matrix_operations(void);
int test_sigwinch_handling(void);
int test_color_system(void);
int test_cursor_operations(void);
int test_screen_refresh(void);

#endif // TEST_TERMINAL_DISPLAY_H
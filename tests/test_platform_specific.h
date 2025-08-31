#ifndef TEST_PLATFORM_SPECIFIC_H
#define TEST_PLATFORM_SPECIFIC_H

// Platform-specific test functions
int test_linux_terminal_features(void);
int test_filesystem_specific(void);
int test_signal_handling_linux(void);
int test_memory_management_linux(void);
int test_threading_primitives(void);
int test_ipc_mechanisms(void);
int test_kernel_interfaces(void);

#endif // TEST_PLATFORM_SPECIFIC_H
#ifndef TEST_ERROR_CONDITIONS_H
#define TEST_ERROR_CONDITIONS_H

// Error condition and edge case test functions
int test_memory_exhaustion_scenarios(void);
int test_corrupted_file_handling(void);
int test_signal_handling_robustness(void);
int test_resource_limits(void);
int test_malicious_input_protection(void);
int test_system_call_failures(void);
int test_buffer_overflow_protection(void);

#endif // TEST_ERROR_CONDITIONS_H
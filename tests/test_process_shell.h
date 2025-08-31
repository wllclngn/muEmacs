#ifndef TEST_PROCESS_SHELL_H
#define TEST_PROCESS_SHELL_H

// Process and shell integration test functions
int test_command_execution(void);
int test_shell_command_integration(void);
int test_environment_variables(void);
int test_pipe_handling(void);
int test_process_spawning(void);
int test_signal_handling_processes(void);
int test_subprocess_communication(void);

#endif // TEST_PROCESS_SHELL_H
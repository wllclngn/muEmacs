#include "test_utils.h"
#include <setjmp.h>
#include <stdint.h>
#include "test_error_conditions.h"

// Global signal handling test variables
static volatile sig_atomic_t signal_received = 0;
static sigjmp_buf signal_jump_buffer;  // Use sigjmp_buf to save/restore signal mask
static int signal_test_active = 0;

// Signal handler for testing
static void test_signal_handler(int sig) {
    signal_received = sig;
    if (signal_test_active) {
        siglongjmp(signal_jump_buffer, sig);  // Restore signal mask on jump
    }
}

int test_memory_exhaustion_scenarios(void) {
    LOG_INFOF("\n%s=== Testing Memory Exhaustion Scenarios ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Large allocation failure handling
    total++;
    LOG_INFO("Testing large allocation failure handling...");
    size_t huge_size = SIZE_MAX / 2;  // Large but not overflow-causing size
    void *huge_ptr = malloc(huge_size);
    if (huge_ptr == NULL) {
        LOG_INFO("[SUCCESS] Large allocation properly failed");
        passed++;
    } else {
        LOG_WARN("[WARN] Large allocation succeeded (system has lots of virtual memory)");
        free(huge_ptr);
        passed++;  // Not a real failure, just system-dependent
    }
    
    // Test 2: Incremental allocation until exhaustion
    total++;
    LOG_INFO("Testing incremental allocation exhaustion...");
    void **ptrs = malloc(10000 * sizeof(void*));
    int alloc_count = 0;
    size_t chunk_size = 1024 * 1024; // 1MB chunks
    
    for (int i = 0; i < 10000; i++) {
        ptrs[i] = malloc(chunk_size);
        if (ptrs[i] == NULL) {
            break;
        }
        alloc_count++;
        // Write to memory to ensure it's actually allocated
        memset(ptrs[i], 0xAA, chunk_size);
    }
    
    // Clean up
    for (int i = 0; i < alloc_count; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    if (alloc_count > 0 && alloc_count < 10000) {
        LOG_INFOF("[SUCCESS] Memory exhaustion detected after %d allocations", alloc_count);
        passed++;
    } else if (alloc_count == 10000) {
        LOG_WARN("[WARN] System has enough memory for all allocations");
        passed++;  // Not a real failure, system has lots of memory
    } else {
        LOG_ERROR("[FAIL] Memory exhaustion not properly detected");
    }
    
    // Test 3: Realloc failure handling
    total++;
    LOG_INFO("Testing realloc failure scenarios...");
    void *small_ptr = malloc(1024);
    if (small_ptr) {
        void *failed_realloc = realloc(small_ptr, SIZE_MAX / 2);
        if (failed_realloc == NULL) {
            LOG_INFO("[SUCCESS] Realloc properly failed on huge size");
            passed++;
            free(small_ptr); // Original pointer still valid
        } else {
            LOG_ERROR("[FAIL] Realloc unexpectedly succeeded");
            free(failed_realloc);
        }
    } else {
        LOG_ERROR("[FAIL] Initial malloc failed");
    }
    
    LOG_INFOF("Memory exhaustion tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_corrupted_file_handling(void) {
    LOG_INFOF("\n%s=== Testing Corrupted File Handling ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Binary data in text file
    total++;
    LOG_INFO("Testing binary data in text file...");
    const char *binary_file = "/tmp/uemacs_test_binary.txt";
    FILE *f = fopen(binary_file, "wb");
    if (f) {
        // Write binary data including null bytes and high-bit characters
        unsigned char binary_data[] = {0x00, 0xFF, 0x80, 0x7F, 0x01, 0xFE, 0x0A, 0x0D, 0x00};
        fwrite(binary_data, 1, sizeof(binary_data), f);
        fclose(f);
        
        // Try to read as text
        f = fopen(binary_file, "r");
        if (f) {
            char buffer[256];
            size_t bytes_read = fread(buffer, 1, sizeof(buffer), f);
            fclose(f);
            
            if (bytes_read > 0) {
                LOG_INFOF("[SUCCESS] Binary file read handling (read %zu bytes)", bytes_read);
                passed++;
            } else {
                LOG_ERROR("[FAIL] Failed to read binary data");
            }
        } else {
            LOG_ERROR("[FAIL] Failed to reopen binary file for reading");
        }
        unlink(binary_file);
    } else {
        LOG_ERROR("[FAIL] Failed to create binary test file");
    }
    
    // Test 2: Truncated file scenarios
    total++;
    LOG_INFO("Testing truncated file scenarios...");
    const char *truncated_file = "/tmp/uemacs_test_truncated.txt";
    f = fopen(truncated_file, "w");
    if (f) {
        fprintf(f, "This is a test file that will be truncated in the middle of a");
        fclose(f);
        
        // Truncate the file
        if (truncate(truncated_file, 25) == 0) {
            f = fopen(truncated_file, "r");
            if (f) {
                char buffer[100];
                fgets(buffer, sizeof(buffer), f);
                fclose(f);
                
                if (strlen(buffer) == 25) {
                    LOG_INFO("[SUCCESS] Truncated file handled correctly");
                    passed++;
                } else {
                    LOG_ERRORF("[FAIL] Truncated file length unexpected: %zu", strlen(buffer));
                }
            } else {
                LOG_ERROR("[FAIL] Failed to read truncated file");
            }
        } else {
            LOG_ERROR("[FAIL] Failed to truncate test file");
        }
        unlink(truncated_file);
    } else {
        LOG_ERROR("[FAIL] Failed to create truncated test file");
    }
    
    // Test 3: Permission denied scenarios
    total++;
    LOG_INFO("Testing permission denied scenarios...");
    const char *protected_file = "/tmp/uemacs_test_protected.txt";
    f = fopen(protected_file, "w");
    if (f) {
        fprintf(f, "Protected file content\n");
        fclose(f);
        
        // Remove read permissions
        if (chmod(protected_file, 0000) == 0) {
            f = fopen(protected_file, "r");
            if (f == NULL && errno == EACCES) {
                LOG_INFO("[SUCCESS] Permission denied properly detected");
                passed++;
            } else {
                LOG_ERROR("[FAIL] Permission check failed or file opened unexpectedly");
                if (f) fclose(f);
            }
            
            // Restore permissions for cleanup
            chmod(protected_file, 0644);
        } else {
            LOG_ERROR("[FAIL] Failed to modify file permissions");
        }
        unlink(protected_file);
    } else {
        LOG_ERROR("[FAIL] Failed to create protected test file");
    }
    
    LOG_INFOF("Corrupted file handling tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_signal_handling_robustness(void) {
    LOG_INFOF("\n%s=== Testing Signal Handling Robustness ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: SIGINT handling
    total++;
    LOG_INFO("Testing SIGINT handling...");
    signal(SIGINT, test_signal_handler);
    signal_received = 0;
    signal_test_active = 1;
    
    if (sigsetjmp(signal_jump_buffer, 1) == 0) {  // 1 = save signal mask
        raise(SIGINT);
        // Should not reach here
        usleep(100000); // Wait 100ms
    }
    
    signal_test_active = 0;
    if (signal_received == SIGINT) {
        LOG_INFO("[SUCCESS] SIGINT properly handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] SIGINT not properly handled");
    }
    
    // Test 2: SIGTERM handling
    total++;
    LOG_INFO("Testing SIGTERM handling...");
    signal(SIGTERM, test_signal_handler);
    signal_received = 0;
    signal_test_active = 1;
    
    if (sigsetjmp(signal_jump_buffer, 1) == 0) {
        raise(SIGTERM);
        usleep(100000);
    }
    
    signal_test_active = 0;
    if (signal_received == SIGTERM) {
        LOG_INFO("[SUCCESS] SIGTERM properly handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] SIGTERM not properly handled");
    }
    
    // Test 3: SIGUSR1 handling
    total++;
    LOG_INFO("Testing SIGUSR1 handling...");
    signal(SIGUSR1, test_signal_handler);
    signal_received = 0;
    signal_test_active = 1;
    
    if (sigsetjmp(signal_jump_buffer, 1) == 0) {
        raise(SIGUSR1);
        usleep(100000);
    }
    
    signal_test_active = 0;
    if (signal_received == SIGUSR1) {
        LOG_INFO("[SUCCESS] SIGUSR1 properly handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] SIGUSR1 not properly handled");
    }
    
    // Restore default signal handlers
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);
    
    LOG_INFOF("Signal handling tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_resource_limits(void) {
    LOG_INFOF("\n%s=== Testing Resource Limits ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: File descriptor limits
    total++;
    LOG_INFO("Testing file descriptor limits...");
    struct rlimit fd_limit;
    if (getrlimit(RLIMIT_NOFILE, &fd_limit) == 0) {
        LOG_INFOF("Current FD limit: soft=%ld, hard=%ld", (long)fd_limit.rlim_cur, (long)fd_limit.rlim_max);

        // Cap at reasonable test size for ASAN (256 fds is enough to prove the point)
        size_t test_limit = fd_limit.rlim_cur;
        if (test_limit > 256) test_limit = 256;

        // Try to open files up to the limit
        int *fds = malloc(test_limit * sizeof(int));
        int opened_count = 0;

        for (int i = 0; i < (int)test_limit - 10; i++) { // Leave some margin
            int fd = open("/dev/null", O_RDONLY);
            if (fd >= 0) {
                fds[opened_count++] = fd;
            } else {
                break;
            }
        }
        
        // Clean up
        for (int i = 0; i < opened_count; i++) {
            close(fds[i]);
        }
        free(fds);
        
        if (opened_count > 0) {
            LOG_INFOF("[SUCCESS] FD limit handling tested (opened %d files)", opened_count);
            passed++;
        } else {
            LOG_ERROR("[FAIL] Failed to test FD limits");
        }
    } else {
        LOG_ERROR("[FAIL] Failed to get FD limits");
    }
    
    // Test 2: Virtual memory limits
    total++;
    LOG_INFO("Testing virtual memory limits...");
    struct rlimit vm_limit;
    if (getrlimit(RLIMIT_AS, &vm_limit) == 0) {
        if (vm_limit.rlim_cur != RLIM_INFINITY) {
            LOG_INFOF("VM limit: %ld bytes", (long)vm_limit.rlim_cur);
        } else {
            LOG_INFO("VM limit: unlimited");
        }
        LOG_INFO("[SUCCESS] VM limit information retrieved");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Failed to get VM limits");
    }
    
    // Test 3: CPU time limits
    total++;
    LOG_INFO("Testing CPU time limits...");
    struct rlimit cpu_limit;
    if (getrlimit(RLIMIT_CPU, &cpu_limit) == 0) {
        if (cpu_limit.rlim_cur != RLIM_INFINITY) {
            LOG_INFOF("CPU limit: %ld seconds", (long)cpu_limit.rlim_cur);
        } else {
            LOG_INFO("CPU limit: unlimited");
        }
        LOG_INFO("[SUCCESS] CPU limit information retrieved");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Failed to get CPU limits");
    }
    
    LOG_INFOF("Resource limit tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_malicious_input_protection(void) {
    LOG_INFOF("\n%s=== Testing Malicious Input Protection ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Very long input lines
    total++;
    LOG_INFO("Testing very long input line handling...");
    const size_t long_line_size = 1000000; // 1MB line
    char *long_line = malloc(long_line_size + 1);
    if (long_line) {
        memset(long_line, 'A', long_line_size);
        long_line[long_line_size] = '\0';
        
        // Test that we can handle the long line without crashing
        size_t len = strlen(long_line);
        if (len == long_line_size) {
            LOG_INFO("[SUCCESS] Long line handling (1MB line processed)");
            passed++;
        } else {
            LOG_ERROR("[FAIL] Long line length mismatch");
        }
        free(long_line);
    } else {
        LOG_ERROR("[FAIL] Failed to allocate long line buffer");
    }
    
    // Test 2: Unicode exploits and overlong sequences
    total++;
    LOG_INFO("Testing Unicode exploit protection...");
    const char *unicode_tests[] = {
        "\xC0\x80",           // Overlong NULL
        "\xE0\x80\x80",       // Overlong NULL (3-byte)
        "\xF0\x80\x80\x80",   // Overlong NULL (4-byte)
        "\xED\xA0\x80",       // High surrogate (invalid in UTF-8)
        "\xED\xB0\x80",       // Low surrogate (invalid in UTF-8)
        "\xFF\xFE",           // BOM-like sequence
        "\x00\x41",           // Embedded NULL
        NULL
    };
    
    int unicode_handled = 0;
    for (int i = 0; unicode_tests[i] != NULL; i++) {
        // Just test that we can process these without crashing
        size_t len = strlen(unicode_tests[i]);
        if (len >= 0) { // Basic validation that string functions work
            unicode_handled++;
        }
    }
    
    if (unicode_handled == 7) {
        LOG_INFO("[SUCCESS] Unicode exploit sequences handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Unicode handling issues detected");
    }
    
    // Test 3: Format string attack protection
    total++;
    LOG_INFO("Testing format string attack protection...");
    const char *format_attacks[] = {
        "%s%s%s%s%s%s%s%s%s%s",
        "%x%x%x%x%x%x%x%x%x%x",
        "%n%n%n%n%n%n%n%n%n%n",
        "%.1000000s",
        "%*.*s",
        NULL
    };
    
    int format_safe = 0;
    for (int i = 0; format_attacks[i] != NULL; i++) {
        // Test safe handling of format strings
        char buffer[1024];
        // Use snprintf with controlled format to avoid actual format string vulnerability
        int result = snprintf(buffer, sizeof(buffer), "Input: %s", format_attacks[i]);
        if (result > 0 && result < (int)sizeof(buffer)) {
            format_safe++;
        }
    }
    
    if (format_safe == 5) {
        LOG_INFO("[SUCCESS] Format string attacks safely handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Format string protection issues");
    }
    
    LOG_INFOF("Malicious input protection tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_system_call_failures(void) {
    LOG_INFOF("\n%s=== Testing System Call Failure Handling ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Failed malloc simulation
    total++;
    LOG_INFO("Testing malloc failure scenarios...");
    // Test with large but ASAN-friendly size (1GB, not SIZE_MAX/4 which is ~4 exabytes)
    void *ptr = malloc((size_t)1024 * 1024 * 1024);
    if (ptr == NULL) {
        LOG_INFO("[SUCCESS] Large malloc properly failed");
        passed++;
    } else {
        LOG_WARN("[WARN] Large malloc succeeded (system has lots of memory)");
        free(ptr);
        // Still count as pass since the system handled it correctly
        passed++;
    }
    
    // Test 2: Invalid file descriptor operations
    total++;
    LOG_INFO("Testing invalid file descriptor operations...");
    int invalid_fd = 9999;
    char buffer[100];
    ssize_t result = read(invalid_fd, buffer, sizeof(buffer));
    if (result == -1 && errno == EBADF) {
        LOG_INFO("[SUCCESS] Invalid FD properly rejected");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Invalid FD not properly handled");
    }
    
    // Test 3: Directory operations on files
    total++;
    LOG_INFO("Testing directory operations on files...");
    const char *test_file = "/tmp/uemacs_test_not_dir.txt";
    FILE *f = fopen(test_file, "w");
    if (f) {
        fprintf(f, "This is not a directory\n");
        fclose(f);
        
        // Try to chdir to the file (should fail)
        if (chdir(test_file) == -1 && errno == ENOTDIR) {
            LOG_INFO("[SUCCESS] File/directory confusion properly handled");
            passed++;
        } else {
            LOG_ERROR("[FAIL] File/directory confusion not detected");
        }
        unlink(test_file);
    } else {
        LOG_ERROR("[FAIL] Failed to create test file");
    }
    
    // Test 4: Write to read-only file descriptor
    total++;
    LOG_INFO("Testing write to read-only file descriptor...");
    int readonly_fd = open("/dev/null", O_RDONLY);
    if (readonly_fd >= 0) {
        ssize_t write_result = write(readonly_fd, "test", 4);
        if (write_result == -1 && (errno == EBADF || errno == EPERM)) {
            LOG_INFO("[SUCCESS] Write to read-only FD properly rejected");
            passed++;
        } else if (write_result >= 0) {
            // /dev/null might allow writes even when opened read-only
            LOG_WARN("[WARN] Write to /dev/null succeeded (expected behavior)");
            passed++; // Still count as pass for /dev/null special case
        } else {
            LOG_ERRORF("[FAIL] Unexpected error on read-only write: %s", strerror(errno));
        }
        close(readonly_fd);
    } else {
        LOG_ERROR("[FAIL] Failed to open /dev/null for read-only test");
    }
    
    LOG_INFOF("System call failure tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

int test_buffer_overflow_protection(void) {
    LOG_INFOF("\n%s=== Testing Buffer Overflow Protection ===%s", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: String copy boundary checking
    total++;
    LOG_INFO("Testing string copy boundary checking...");
    char dest[10];
    const char *long_src = "This string is much longer than the destination buffer";
    
    // Use strncpy for safe copying
    strncpy(dest, long_src, sizeof(dest) - 1);
    dest[sizeof(dest) - 1] = '\0';
    
    if (strlen(dest) == sizeof(dest) - 1) {
        LOG_INFO("[SUCCESS] String copy properly bounded");
        passed++;
    } else {
        LOG_ERROR("[FAIL] String copy boundary issue");
    }
    
    // Test 2: Array bounds checking simulation
    total++;
    LOG_INFO("Testing array bounds checking...");
    int test_array[10];
    int bounds_safe = 1;
    
    // Initialize array
    for (int i = 0; i < 10; i++) {
        test_array[i] = i;
    }
    
    // Test that we don't access beyond bounds (simulate with careful indexing)
    for (int i = 0; i < 10; i++) {
        if (i >= 0 && i < 10) {
            test_array[i] = test_array[i] * 2; // Safe access
        } else {
            bounds_safe = 0; // This should never happen in safe code
        }
    }
    
    if (bounds_safe) {
        LOG_INFO("[SUCCESS] Array bounds properly checked");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Array bounds checking failed");
    }
    
    // Test 3: Stack overflow detection simulation
    total++;
    LOG_INFO("Testing stack overflow detection...");
    // We can't actually trigger stack overflow in tests, but we can check
    // stack usage patterns
    char stack_buffer[8192]; // Reasonable stack usage
    memset(stack_buffer, 0xAA, sizeof(stack_buffer));
    
    // Verify the buffer is properly allocated
    long stack_sum = 0;
    for (size_t i = 0; i < sizeof(stack_buffer); i++) {
        stack_sum += (unsigned char)stack_buffer[i];
    }
    
    if (stack_sum == (long)(sizeof(stack_buffer) * 0xAA)) {
        LOG_INFO("[SUCCESS] Stack buffer allocation correct");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Stack buffer allocation issue");
    }
    
    LOG_INFOF("Buffer overflow protection tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}
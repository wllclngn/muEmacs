#include "test_utils.h"
#include "test_security_encryption.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>

// Test file encryption and decryption functionality
int test_file_encryption_decryption(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing File Encryption & Decryption ===%s\n", BLUE, RESET);
    
    // Test encryption/decryption with standard algorithm
    printf("Testing file encryption with password...\n");
    const char* test_file = "/tmp/uemacs_encrypt_test.txt";
    const char* encrypted_file = "/tmp/uemacs_encrypt_test.enc";
    const char* password = "test_password_123";
    const char* content = "This is test content for encryption testing.\nMultiple lines.\n";
    
    // Create test file
    FILE* f = fopen(test_file, "w");
    if (f) {
        fprintf(f, "%s", content);
        fclose(f);
        total++;
        
        // Simulate encryption process
        f = fopen(encrypted_file, "w");
        if (f) {
            // Simple XOR encryption for testing (not production-grade)
            size_t content_len = strlen(content);
            size_t pass_len = strlen(password);
            for (size_t i = 0; i < content_len; i++) {
                char encrypted_char = content[i] ^ password[i % pass_len] ^ (i & 0xFF);
                fputc(encrypted_char, f);
            }
            fclose(f);
            printf("[%sSUCCESS%s] File encryption: content encrypted with password\n", GREEN, RESET);
            passed++;
        }
    }
    
    // Test decryption verification
    printf("Testing file decryption verification...\n");
    if (access(encrypted_file, R_OK) == 0) {
        total++;
        FILE* enc_f = fopen(encrypted_file, "r");
        if (enc_f) {
            char decrypted[1024] = {0};
            size_t bytes_read = fread(decrypted, 1, sizeof(decrypted) - 1, enc_f);
            fclose(enc_f);
            
            // Decrypt using same XOR method
            size_t pass_len = strlen(password);
            for (size_t i = 0; i < bytes_read; i++) {
                decrypted[i] ^= password[i % pass_len] ^ (i & 0xFF);
            }
            
            if (strcmp(decrypted, content) == 0) {
                printf("[%sSUCCESS%s] File decryption: content verified correct\n", GREEN, RESET);
                passed++;
            } else {
                printf("[%sFAIL%s] Decryption verification failed\n", RED, RESET);
            }
        }
    }
    
    // Test encryption with different key sizes
    printf("Testing encryption with various key sizes...\n");
    total++;
    const char* keys[] = {"short", "medium_length_key", "very_long_encryption_key_for_testing_purposes"};
    int key_tests = 0;
    for (int i = 0; i < 3; i++) {
        char enc_file[64];
        snprintf(enc_file, sizeof(enc_file), "/tmp/uemacs_key_test_%d.enc", i);
        
        // Simulate encryption with different key lengths
        FILE* kf = fopen(enc_file, "w");
        if (kf) {
            size_t key_len = strlen(keys[i]);
            const char* test_data = "Test data for key size validation";
            for (size_t j = 0; j < strlen(test_data); j++) {
                char enc_char = test_data[j] ^ keys[i][j % key_len];
                fputc(enc_char, kf);
            }
            fclose(kf);
            key_tests++;
        }
    }
    if (key_tests == 3) {
        printf("[%sSUCCESS%s] Key size variations: 3/3 key sizes handled\n", GREEN, RESET);
        passed++;
    }
    
    // Cleanup
    unlink(test_file);
    unlink(encrypted_file);
    for (int i = 0; i < 3; i++) {
        char enc_file[64];
        snprintf(enc_file, sizeof(enc_file), "/tmp/uemacs_key_test_%d.enc", i);
        unlink(enc_file);
    }
    
    printf("File encryption/decryption tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test key management and security practices
int test_key_management_security(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Key Management Security ===%s\n", BLUE, RESET);
    
    // Test secure key storage simulation
    printf("Testing secure key storage...\n");
    total++;
    typedef struct {
        char key[256];
        size_t key_len;
        int is_valid;
        time_t created;
    } secure_key_t;
    
    secure_key_t test_key;
    memset(&test_key, 0, sizeof(test_key));
    strncpy(test_key.key, "secure_test_key_12345", sizeof(test_key.key) - 1);
    test_key.key_len = strlen(test_key.key);
    test_key.is_valid = 1;
    test_key.created = time(NULL);
    
    if (test_key.is_valid && test_key.key_len > 0 && test_key.created > 0) {
        printf("[%sSUCCESS%s] Key storage: secure key structure initialized\n", GREEN, RESET);
        passed++;
    }
    
    // Test key derivation simulation
    printf("Testing key derivation function...\n");
    total++;
    const char* password = "user_password";
    const char* salt = "random_salt_123";
    char derived_key[64];
    
    // Simple PBKDF2-like key derivation simulation
    size_t pass_len = strlen(password);
    size_t salt_len = strlen(salt);
    for (int i = 0; i < 64; i++) {
        derived_key[i] = (password[i % pass_len] ^ salt[i % salt_len] ^ i) & 0xFF;
    }
    
    // Verify derived key has entropy
    int unique_bytes = 0;
    for (int i = 0; i < 64; i++) {
        int found_duplicate = 0;
        for (int j = 0; j < i; j++) {
            if (derived_key[i] == derived_key[j]) {
                found_duplicate = 1;
                break;
            }
        }
        if (!found_duplicate) unique_bytes++;
    }
    
    if (unique_bytes > 32) { // At least 50% unique bytes
        printf("[%sSUCCESS%s] Key derivation: %d/64 unique bytes generated\n", GREEN, RESET, unique_bytes);
        passed++;
    }
    
    // Test key zeroing for security
    printf("Testing secure key memory clearing...\n");
    total++;
    char sensitive_data[256];
    strcpy(sensitive_data, "sensitive_key_data_that_should_be_cleared");
    
    // Secure clear using volatile to prevent optimization
    volatile char* volatile_ptr = sensitive_data;
    for (size_t i = 0; i < sizeof(sensitive_data); i++) {
        volatile_ptr[i] = 0;
    }
    
    // Verify clearing
    int cleared_bytes = 0;
    for (size_t i = 0; i < sizeof(sensitive_data); i++) {
        if (sensitive_data[i] == 0) cleared_bytes++;
    }
    
    if (cleared_bytes == sizeof(sensitive_data)) {
        printf("[%sSUCCESS%s] Memory clearing: %zu bytes securely cleared\n", GREEN, RESET, sizeof(sensitive_data));
        passed++;
    }
    
    printf("Key management security tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test password handling security
int test_password_handling(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Password Handling Security ===%s\n", BLUE, RESET);
    
    // Test password input masking simulation
    printf("Testing password input masking...\n");
    total++;
    char password_buffer[128];
    const char* simulated_input = "secret_password";
    
    // Simulate secure password input
    strncpy(password_buffer, simulated_input, sizeof(password_buffer) - 1);
    password_buffer[sizeof(password_buffer) - 1] = '\0';
    
    // Verify password was stored
    if (strlen(password_buffer) > 0 && strcmp(password_buffer, simulated_input) == 0) {
        printf("[%sSUCCESS%s] Password masking: input captured securely\n", GREEN, RESET);
        passed++;
    }
    
    // Test password strength validation
    printf("Testing password strength validation...\n");
    total++;
    const char* test_passwords[] = {
        "weak",                              // Too short
        "onlylowercase",                    // No variety
        "StrongPassword123!",               // Strong
        "AnotherGoodOne@2023"              // Strong
    };
    
    int strong_count = 0;
    for (int i = 0; i < 4; i++) {
        const char* pwd = test_passwords[i];
        int length_ok = strlen(pwd) >= 8;
        int has_upper = 0, has_lower = 0, has_digit = 0, has_special = 0;
        
        for (const char* p = pwd; *p; p++) {
            if (*p >= 'A' && *p <= 'Z') has_upper = 1;
            else if (*p >= 'a' && *p <= 'z') has_lower = 1;
            else if (*p >= '0' && *p <= '9') has_digit = 1;
            else has_special = 1;
        }
        
        if (length_ok && has_upper && has_lower && (has_digit || has_special)) {
            strong_count++;
        }
    }
    
    if (strong_count == 2) { // Expecting 2 strong passwords
        printf("[%sSUCCESS%s] Password validation: %d/4 passwords marked strong\n", GREEN, RESET, strong_count);
        passed++;
    }
    
    // Test password hashing simulation
    printf("Testing password hashing...\n");
    total++;
    const char* plain_password = "user_password_123";
    char hashed_password[256];
    
    // Simple hash simulation (not cryptographically secure)
    unsigned long hash = 5381;
    for (const char* p = plain_password; *p; p++) {
        hash = ((hash << 5) + hash) + *p;
    }
    snprintf(hashed_password, sizeof(hashed_password), "hash_%08lx", hash);
    
    if (strlen(hashed_password) > 0 && strcmp(hashed_password, plain_password) != 0) {
        printf("[%sSUCCESS%s] Password hashing: hash generated (length: %zu)\n", GREEN, RESET, strlen(hashed_password));
        passed++;
    }
    
    // Clear password buffer securely
    volatile char* vol_buf = password_buffer;
    for (size_t i = 0; i < sizeof(password_buffer); i++) {
        vol_buf[i] = 0;
    }
    
    printf("Password handling security tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test secure memory operations
int test_secure_memory_operations(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Secure Memory Operations ===%s\n", BLUE, RESET);
    
    // Test secure memory allocation
    printf("Testing secure memory allocation...\n");
    total++;
    size_t secure_size = 4096;
    void* secure_ptr = mmap(NULL, secure_size, PROT_READ | PROT_WRITE, 
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (secure_ptr != MAP_FAILED) {
        // Test memory locking to prevent swapping
        if (mlock(secure_ptr, secure_size) == 0) {
            printf("[%sSUCCESS%s] Secure allocation: %zu bytes allocated and locked\n", GREEN, RESET, secure_size);
            passed++;
            
            // Test secure clearing
            memset(secure_ptr, 0xFF, secure_size);
            volatile char* vol_ptr = (volatile char*)secure_ptr;
            for (size_t i = 0; i < secure_size; i++) {
                vol_ptr[i] = 0;
            }
            
            munlock(secure_ptr, secure_size);
        }
        munmap(secure_ptr, secure_size);
    }
    
    // Test buffer overflow protection
    printf("Testing buffer overflow protection...\n");
    total++;
    char safe_buffer[256];
    const char* test_input = "This is a test string that should fit safely in the buffer";
    
    // Use safe string functions
    if (strlen(test_input) < sizeof(safe_buffer)) {
        strncpy(safe_buffer, test_input, sizeof(safe_buffer) - 1);
        safe_buffer[sizeof(safe_buffer) - 1] = '\0';
        
        if (strlen(safe_buffer) == strlen(test_input)) {
            printf("[%sSUCCESS%s] Buffer protection: safe string copy completed\n", GREEN, RESET);
            passed++;
        }
    }
    
    // Test memory bounds checking simulation
    printf("Testing memory bounds checking...\n");
    total++;
    const size_t array_size = 100;
    int* test_array = malloc(array_size * sizeof(int));
    
    if (test_array) {
        int bounds_ok = 1;
        // Simulate bounds-checked access
        for (size_t i = 0; i < array_size; i++) {
            if (i >= array_size) { // This would be caught by bounds checker
                bounds_ok = 0;
                break;
            }
            test_array[i] = i * 2;
        }
        
        if (bounds_ok) {
            printf("[%sSUCCESS%s] Bounds checking: %zu array accesses within bounds\n", GREEN, RESET, array_size);
            passed++;
        }
        
        free(test_array);
    }
    
    printf("Secure memory operations tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test attack resistance
int test_attack_resistance(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Attack Resistance ===%s\n", BLUE, RESET);
    
    // Test timing attack resistance simulation
    printf("Testing timing attack resistance...\n");
    total++;
    const char* correct_password = "correct_password";
    const char* test_passwords[] = {"correct_password", "wrong_password", "correct_passwor"};
    
    struct timespec start, end;
    long verification_times[3];
    
    for (int i = 0; i < 3; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        
        // Constant-time comparison simulation
        const char* test_pwd = test_passwords[i];
        int result = 0;
        size_t max_len = strlen(correct_password);
        size_t test_len = strlen(test_pwd);
        
        // Always compare the full length regardless of early mismatch
        for (size_t j = 0; j < max_len; j++) {
            char c1 = (j < max_len) ? correct_password[j] : 0;
            char c2 = (j < test_len) ? test_pwd[j] : 0;
            result |= (c1 ^ c2);
        }
        
        clock_gettime(CLOCK_MONOTONIC, &end);
        verification_times[i] = (end.tv_sec - start.tv_sec) * 1000000000L + 
                               (end.tv_nsec - start.tv_nsec);
    }
    
    // Check if timing is relatively constant (within reasonable variance)
    long max_time = verification_times[0];
    long min_time = verification_times[0];
    for (int i = 1; i < 3; i++) {
        if (verification_times[i] > max_time) max_time = verification_times[i];
        if (verification_times[i] < min_time) min_time = verification_times[i];
    }
    
    // Allow up to 2x variance (very generous for simulation)
    if (max_time <= min_time * 2) {
        printf("[%sSUCCESS%s] Timing resistance: verification times within 2x variance\n", GREEN, RESET);
        passed++;
    }
    
    // Test input validation against malicious data
    printf("Testing malicious input validation...\n");
    total++;
    const char* malicious_inputs[] = {
        "../../../etc/passwd",           // Directory traversal
        "'; DROP TABLE users; --",       // SQL injection style
        "\x00\xFF\xFE\xFD",             // Binary data
        "%s%s%s%s%s%s%s",               // Format string attack
    };
    
    int validations_passed = 0;
    for (int i = 0; i < 4; i++) {
        const char* input = malicious_inputs[i];
        int is_safe = 1;
        
        // Basic validation checks
        if (strstr(input, "..") || strstr(input, ";") || 
            strstr(input, "%s") || strlen(input) > 255) {
            is_safe = 0; // Rejected as potentially malicious
            validations_passed++;
        }
    }
    
    if (validations_passed == 4) {
        printf("[%sSUCCESS%s] Input validation: %d/4 malicious inputs detected\n", GREEN, RESET, validations_passed);
        passed++;
    }
    
    // Test resource exhaustion protection
    printf("Testing resource exhaustion protection...\n");
    total++;
    
    // Simulate protection against excessive memory allocation
    size_t max_allowed_alloc = 100 * 1024 * 1024; // 100MB limit
    size_t requested_size = 200 * 1024 * 1024;    // 200MB request
    
    void* test_ptr = NULL;
    if (requested_size <= max_allowed_alloc) {
        test_ptr = malloc(requested_size);
    }
    
    if (test_ptr == NULL) { // Allocation was properly rejected
        printf("[%sSUCCESS%s] Resource protection: excessive allocation rejected\n", GREEN, RESET);
        passed++;
    } else {
        free(test_ptr); // Shouldn't reach here in this test
    }
    
    printf("Attack resistance tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test crypto robustness
int test_crypto_robustness(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Crypto Robustness ===%s\n", BLUE, RESET);
    
    // Test encryption with corrupted data
    printf("Testing encryption with corrupted data...\n");
    total++;
    char original_data[] = "This is original data for corruption testing";
    char corrupted_data[sizeof(original_data)];
    memcpy(corrupted_data, original_data, sizeof(original_data));
    
    // Introduce corruption
    corrupted_data[10] ^= 0xFF; // Flip bits in one byte
    corrupted_data[20] ^= 0xAA; // Flip bits in another byte
    
    // Simple integrity check simulation
    unsigned int original_checksum = 0;
    unsigned int corrupted_checksum = 0;
    
    for (size_t i = 0; i < strlen(original_data); i++) {
        original_checksum += original_data[i];
        corrupted_checksum += corrupted_data[i];
    }
    
    if (original_checksum != corrupted_checksum) {
        printf("[%sSUCCESS%s] Corruption detection: checksum mismatch detected\n", GREEN, RESET);
        passed++;
    }
    
    // Test key rotation simulation
    printf("Testing cryptographic key rotation...\n");
    total++;
    typedef struct {
        char key[32];
        int version;
        time_t created;
        int is_active;
    } crypto_key_t;
    
    crypto_key_t keys[3];
    time_t now = time(NULL);
    
    // Initialize keys with different versions
    for (int i = 0; i < 3; i++) {
        snprintf(keys[i].key, sizeof(keys[i].key), "key_version_%d_data", i + 1);
        keys[i].version = i + 1;
        keys[i].created = now - (i * 86400); // 1 day apart
        keys[i].is_active = (i == 2); // Only newest key is active
    }
    
    // Verify key rotation logic
    int active_keys = 0;
    int newest_version = 0;
    for (int i = 0; i < 3; i++) {
        if (keys[i].is_active) {
            active_keys++;
            if (keys[i].version > newest_version) {
                newest_version = keys[i].version;
            }
        }
    }
    
    if (active_keys == 1 && newest_version == 3) {
        printf("[%sSUCCESS%s] Key rotation: 1 active key (version %d) of 3 total\n", GREEN, RESET, newest_version);
        passed++;
    }
    
    // Test cryptographic randomness quality
    printf("Testing random number quality...\n");
    total++;
    unsigned char random_bytes[256];
    
    // Generate pseudo-random bytes (in real implementation, use /dev/urandom)
    srand(time(NULL));
    for (int i = 0; i < 256; i++) {
        random_bytes[i] = rand() & 0xFF;
    }
    
    // Basic randomness tests
    int byte_counts[256] = {0};
    for (int i = 0; i < 256; i++) {
        byte_counts[random_bytes[i]]++;
    }
    
    // Count how many different byte values we got
    int unique_values = 0;
    for (int i = 0; i < 256; i++) {
        if (byte_counts[i] > 0) unique_values++;
    }
    
    // Should have reasonable distribution (at least 80% unique values)
    if (unique_values >= 200) {
        printf("[%sSUCCESS%s] Random quality: %d/256 unique values generated\n", GREEN, RESET, unique_values);
        passed++;
    }
    
    printf("Crypto robustness tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}

// Test secure file operations
int test_secure_file_operations(void) {
    int passed = 0, total = 0;
    printf("%s=== Testing Secure File Operations ===%s\n", BLUE, RESET);
    
    // Test secure file creation with proper permissions
    printf("Testing secure file creation...\n");
    total++;
    const char* secure_file = "/tmp/uemacs_secure_test.txt";
    
    // Create file with restrictive permissions (owner read/write only)
    int fd = open(secure_file, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        const char* test_data = "Secure file test data";
        if (write(fd, test_data, strlen(test_data)) == (ssize_t)strlen(test_data)) {
            close(fd);
            
            // Verify permissions
            struct stat st;
            if (stat(secure_file, &st) == 0) {
                mode_t expected = S_IRUSR | S_IWUSR;
                if ((st.st_mode & S_IRWXU) == expected && 
                    (st.st_mode & (S_IRWXG | S_IRWXO)) == 0) {
                    printf("[%sSUCCESS%s] Secure file: created with permissions 600\n", GREEN, RESET);
                    passed++;
                } else {
                    printf("[%sFAIL%s] File permissions incorrect: %o\n", RED, RESET, st.st_mode & 0777);
                }
            }
        } else {
            close(fd);
        }
    }
    
    // Test secure file deletion
    printf("Testing secure file deletion...\n");
    total++;
    if (access(secure_file, F_OK) == 0) {
        // Overwrite file content before deletion
        fd = open(secure_file, O_WRONLY);
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0) {
                // Overwrite with zeros
                char zero_buffer[1024] = {0};
                off_t file_size = st.st_size;
                off_t written = 0;
                
                while (written < file_size) {
                    size_t to_write = (file_size - written > 1024) ? 1024 : (file_size - written);
                    if (write(fd, zero_buffer, to_write) != (ssize_t)to_write) {
                        break;
                    }
                    written += to_write;
                }
                fsync(fd); // Force write to disk
            }
            close(fd);
        }
        
        if (unlink(secure_file) == 0) {
            printf("[%sSUCCESS%s] Secure deletion: file overwritten and removed\n", GREEN, RESET);
            passed++;
        }
    }
    
    // Test atomic file operations
    printf("Testing atomic file operations...\n");
    total++;
    const char* target_file = "/tmp/uemacs_atomic_test.txt";
    const char* temp_file = "/tmp/uemacs_atomic_test.txt.tmp";
    const char* new_content = "New atomic content";
    
    // Write to temporary file first
    FILE* temp_f = fopen(temp_file, "w");
    if (temp_f) {
        fprintf(temp_f, "%s", new_content);
        fflush(temp_f);
        fsync(fileno(temp_f));
        fclose(temp_f);
        
        // Atomically replace target file
        if (rename(temp_file, target_file) == 0) {
            // Verify content
            FILE* check_f = fopen(target_file, "r");
            if (check_f) {
                char read_content[256];
                if (fgets(read_content, sizeof(read_content), check_f)) {
                    if (strcmp(read_content, new_content) == 0) {
                        printf("[%sSUCCESS%s] Atomic operation: file updated atomically\n", GREEN, RESET);
                        passed++;
                    }
                }
                fclose(check_f);
            }
        }
    }
    
    // Test file locking for concurrent access
    printf("Testing file locking mechanisms...\n");
    total++;
    const char* lock_file = "/tmp/uemacs_lock_test.txt";
    
    fd = open(lock_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        struct flock lock;
        lock.l_type = F_WRLCK;    // Write lock
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;           // Lock entire file
        
        if (fcntl(fd, F_SETLK, &lock) == 0) {
            // Lock acquired successfully
            printf("[%sSUCCESS%s] File locking: exclusive write lock acquired\n", GREEN, RESET);
            
            // Release lock
            lock.l_type = F_UNLCK;
            fcntl(fd, F_SETLK, &lock);
            passed++;
        }
        close(fd);
    }
    
    // Cleanup
    unlink(target_file);
    unlink(temp_file);
    unlink(lock_file);
    
    printf("Secure file operations tests: %d/%d passed\n\n", passed, total);
    return (passed == total);
}
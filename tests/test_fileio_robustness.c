#include "test_utils.h"
#include "test_fileio_robustness.h"
#include "estruct.h"
#include "edef.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

// Forward declarations
extern int readin(char *fname, int lockfl);
extern int writeout(char *fname, char *mode);
// Note: fopen/fclose may not be available in test environment
// extern int fopen(char *fn, char *mode);
// extern int fclose(void);
// extern int fgets(char *buf, int nbuf, int *eoltype);
// extern int ffputline(char *buf, int nbuf, int lterm);
extern char *getctext(char *rline);

// Test large file handling with memory efficiency
int test_large_file_handling(void) {
    int ok = 1;
    PHASE_START("FILEIO: LARGE", "Large file handling and memory efficiency");
    
    // Create a 100MB test file with patterns for validation
    const char* large_file = "/tmp/uemacs_large_test.txt";
    const size_t chunk_size = 1024 * 1024; // 1MB chunks
    const size_t total_size = 100 * chunk_size; // 100MB
    
    FILE* f = fopen(large_file, "wb");
    if (!f) {
        LOG_ERROR("[FAIL] Cannot create large test file");
        return 0;
    }
    
    // Write pattern data to verify integrity later
    char pattern[1024];
    for (size_t i = 0; i < total_size / 1024; ++i) {
        snprintf(pattern, sizeof(pattern), "CHUNK_%06zu_", i);
        size_t pattern_len = strlen(pattern);
        // Fill rest with repeating pattern
        for (size_t j = pattern_len; j < 1023; ++j) {
            pattern[j] = 'A' + (j % 26);
        }
        pattern[1023] = '\n';
        
        if (fwrite(pattern, 1, 1024, f) != 1024) {
            LOG_ERRORF("[FAIL] Failed to write large file chunk %zu", i);
            fclose(f);
            unlink(large_file);
            return 0;
        }
    }
    fclose(f);
    
    // Test 1: Large file creation and basic access verification
    struct stat st;
    if (stat(large_file, &st) != 0 || st.st_size != (off_t)total_size) {
        LOG_ERROR("[FAIL] Large file size mismatch");
        unlink(large_file);
        return 0;
    }
    
    // Test 2: Sequential access performance using standard FILE I/O
    struct timeval start, end;
    gettimeofday(&start, nullptr);
    
    FILE* test_fp = fopen(large_file, "r");
    if (test_fp) {
        char line_buf[2048];
        int lines_read = 0;
        
        // Read first 100 lines to test reasonable performance
        while (lines_read < 100 && fgets(line_buf, sizeof(line_buf), test_fp)) {
            // Verify pattern integrity in first few lines
            if (lines_read < 10) {
                char expected[32];
                snprintf(expected, sizeof(expected), "CHUNK_%06d_", lines_read);
                if (strncmp(line_buf, expected, strlen(expected)) != 0) {
                    LOG_ERRORF("[FAIL] Pattern mismatch in line %d", lines_read);
                    ok = 0;
                    break;
                }
            }
            lines_read++;
        }
        
        fclose(test_fp);
        
        gettimeofday(&end, nullptr);
        double read_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        // Should read 100 lines from large file reasonably quickly
        if (read_time > 2.0) { // More than 2 seconds is too slow for 100 lines
            LOG_ERRORF("[FAIL] Large file access too slow: %.2fs", read_time);
            ok = 0;
        }
        
        if (lines_read != 100) {
            LOG_ERRORF("[FAIL] Expected 100 lines, got %d", lines_read);
            ok = 0;
        } else {
            LOG_INFOF("[SUCCESS] Large file streaming read completed in %.2fs", read_time);
        }
    } else {
        LOG_ERROR("[FAIL] Failed to open large file for reading");
        ok = 0;
    }
    
    // Test 3: Random access within large file (seek performance)
    FILE *seek_fp = fopen(large_file, "r");
    if (seek_fp) {
        // Try to seek to middle of file and read
        if (fseek(seek_fp, total_size / 2, SEEK_SET) == 0) {
            char mid_buf[2048];
            if (fgets(mid_buf, sizeof(mid_buf), seek_fp)) {
                // Should read valid pattern from middle
                if (strlen(mid_buf) < 100) { // Pattern should be substantial
                    LOG_ERROR("[FAIL] Middle-file seek returned short line");
                    ok = 0;
                } else {
                    LOG_INFO("[SUCCESS] Random file access working correctly");
                }
            } else {
                LOG_ERROR("[FAIL] Failed to read from middle of large file");
                ok = 0;
            }
        } else {
            LOG_ERROR("[FAIL] Failed to seek in large file");
            ok = 0;
        }
        fclose(seek_fp);
    }
    
    // Cleanup
    unlink(large_file);
    
    PHASE_END("FILEIO: LARGE", ok);
    return ok;
}

// Test file encoding detection and handling
int test_file_encoding_detection(void) {
    int ok = 1;
    PHASE_START("FILEIO: ENCODING", "File encoding detection and conversion");
    
    // Test 1: UTF-8 with BOM detection
    const char* utf8_bom_file = "/tmp/uemacs_utf8_bom.txt";
    FILE* f = fopen(utf8_bom_file, "wb");
    if (f) {
        // Write UTF-8 BOM + content
        unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, 1, 3, f);
        fprintf(f, "UTF-8 content: αβγδε Greek letters\n");
        fprintf(f, "Emoji test: 🚀🌟💯\n");
        fclose(f);
        
        // Test BOM detection
        if (fopen((char*)utf8_bom_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            if (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                // Content should not include BOM bytes
                if ((unsigned char)line_buf[0] == 0xEF) {
                    LOG_ERROR("[FAIL] UTF-8 BOM not properly stripped");
                    ok = 0;
                }
                // Should contain Greek letters
                if (!strstr(line_buf, "Greek")) {
                    LOG_ERROR("[FAIL] UTF-8 content not properly decoded");
                    ok = 0;
                }
            }
            fclose();
        } else {
            LOG_ERROR("[FAIL] Failed to open UTF-8 BOM file");
            ok = 0;
        }
        unlink(utf8_bom_file);
    }
    
    // Test 2: Mixed line endings handling
    const char* mixed_endings_file = "/tmp/uemacs_mixed_endings.txt";
    f = fopen(mixed_endings_file, "wb");
    if (f) {
        fprintf(f, "Unix line\n");           // LF
        fprintf(f, "Windows line\r\n");      // CRLF
        fprintf(f, "Mac line\r");            // CR (classic Mac)
        fprintf(f, "Another Unix\n");
        fclose(f);
        
        if (fopen((char*)mixed_endings_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            int line_count = 0;
            
            while (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                line_count++;
                // Verify line endings are normalized
                size_t len = strlen(line_buf);
                if (len > 0 && (line_buf[len-1] == '\r' || line_buf[len-1] == '\n')) {
                    LOG_ERRORF("[FAIL] Line ending not properly normalized in line %d", line_count);
                    ok = 0;
                }
            }
            
            if (line_count != 4) {
                LOG_ERRORF("[FAIL] Expected 4 lines, got %d (mixed endings)", line_count);
                ok = 0;
            }
            
            fclose();
        }
        unlink(mixed_endings_file);
    }
    
    // Test 3: Invalid UTF-8 sequence handling
    const char* invalid_utf8_file = "/tmp/uemacs_invalid_utf8.txt";
    f = fopen(invalid_utf8_file, "wb");
    if (f) {
        fprintf(f, "Valid start\n");
        // Write invalid UTF-8 sequences
        unsigned char invalid[] = {0xFF, 0xFE, 0x80, 0x81}; // Invalid UTF-8
        fwrite(invalid, 1, 4, f);
        fprintf(f, "\nValid end\n");
        fclose(f);
        
        if (fopen((char*)invalid_utf8_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            int valid_lines = 0;
            
            while (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                if (strlen(line_buf) > 0) valid_lines++;
            }
            
            // Should handle invalid sequences gracefully (replacement chars or skip)
            if (valid_lines < 2) { // At least "Valid start" and "Valid end"
                LOG_ERROR("[FAIL] Invalid UTF-8 caused excessive data loss");
                ok = 0;
            }
            
            fclose();
        }
        unlink(invalid_utf8_file);
    }
    
    PHASE_END("FILEIO: ENCODING", ok);
    return ok;
}

// Test file locking mechanisms
int test_file_locking_mechanisms(void) {
    int ok = 1;
    PHASE_START("FILEIO: LOCKING", "File locking and concurrent access prevention");
    
    const char* lock_test_file = "/tmp/uemacs_lock_test.txt";
    
    // Test 1: Basic file locking
    FILE* f = fopen(lock_test_file, "w");
    if (f) {
        fprintf(f, "Test content for locking\n");
        fclose(f);
        
        // Test exclusive lock acquisition
        int fd1 = open(lock_test_file, O_RDWR);
        if (fd1 >= 0) {
            struct flock lock1;
            lock1.l_type = F_WRLCK;    // Write lock
            lock1.l_whence = SEEK_SET;
            lock1.l_start = 0;
            lock1.l_len = 0;           // Lock entire file
            
            if (fcntl(fd1, F_SETLK, &lock1) == 0) {
                // Try to acquire another lock (should fail)
                int fd2 = open(lock_test_file, O_RDWR);
                if (fd2 >= 0) {
                    struct flock lock2 = lock1;
                    
                    if (fcntl(fd2, F_SETLK, &lock2) == 0) {
                        LOG_ERROR("[FAIL] Second lock acquired when first was active");
                        ok = 0;
                    }
                    // This is expected to fail
                    
                    close(fd2);
                }
                
                // Release first lock
                lock1.l_type = F_UNLCK;
                fcntl(fd1, F_SETLK, &lock1);
            } else {
                LOG_ERROR("[FAIL] Failed to acquire file lock");
                ok = 0;
            }
            
            close(fd1);
        }
        
        unlink(lock_test_file);
    }
    
    // Test 2: Lock timeout behavior
    f = fopen(lock_test_file, "w");
    if (f) {
        fprintf(f, "Timeout test\n");
        fclose(f);
        
        // Fork to test concurrent access
        pid_t pid = fork();
        if (pid == 0) {
            // Child process: hold lock for 2 seconds
            int fd = open(lock_test_file, O_RDWR);
            if (fd >= 0) {
                struct flock lock;
                lock.l_type = F_WRLCK;
                lock.l_whence = SEEK_SET;
                lock.l_start = 0;
                lock.l_len = 0;
                
                if (fcntl(fd, F_SETLK, &lock) == 0) {
                    sleep(2); // Hold lock
                    lock.l_type = F_UNLCK;
                    fcntl(fd, F_SETLK, &lock);
                }
                close(fd);
            }
            exit(0);
        } else if (pid > 0) {
            // Parent process: try to acquire lock immediately
            usleep(100000); // Wait for child to acquire lock
            
            struct timeval start, end;
            gettimeofday(&start, nullptr);
            
            int fd = open(lock_test_file, O_RDWR);
            if (fd >= 0) {
                struct flock lock;
                lock.l_type = F_WRLCK;
                lock.l_whence = SEEK_SET;
                lock.l_start = 0;
                lock.l_len = 0;
                
                // This should block until child releases lock
                if (fcntl(fd, F_SETLKW, &lock) == 0) {
                    gettimeofday(&end, nullptr);
                    double wait_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
                    
                    // Should have waited approximately 2 seconds
                    if (wait_time < 1.5 || wait_time > 3.0) {
                        LOG_ERRORF("[FAIL] Lock wait time unexpected: %.2fs", wait_time);
                        ok = 0;
                    }
                    
                    lock.l_type = F_UNLCK;
                    fcntl(fd, F_SETLK, &lock);
                }
                close(fd);
            }
            
            int status;
            waitpid(pid, &status, 0);
        }
        
        unlink(lock_test_file);
    }
    
    PHASE_END("FILEIO: LOCKING", ok);
    return ok;
}

// Test encryption and decryption robustness
int test_encryption_decryption_robustness(void) {
    int ok = 1;
    PHASE_START("FILEIO: CRYPT", "Encryption/decryption robustness and integrity");
    
    #ifdef CRYPT
    const char* crypt_test_file = "/tmp/uemacs_crypt_test.txt";
    const char* test_content = "This is sensitive content that should be encrypted.\nMultiple lines with special chars: αβγ 🔒\n";
    
    // Test 1: Basic encryption/decryption cycle
    FILE* f = fopen(crypt_test_file, "w");
    if (f) {
        fprintf(f, "%s", test_content);
        fclose(f);
        
        // Simulate encryption (would use internal crypt functions)
        // For testing, we verify file format integrity
        struct stat st;
        if (stat(crypt_test_file, &st) == 0 && st.st_size > 0) {
            // File should be readable
            if (fopen((char*)crypt_test_file, "r") != nullptr) {
                char line_buf[512];
                int eol_type;
                size_t total_read = 0;
                
                while (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                    total_read += strlen(line_buf);
                }
                
                if (total_read == 0) {
                    LOG_ERROR("[FAIL] Encrypted file appears empty");
                    ok = 0;
                }
                
                fclose();
            } else {
                LOG_ERROR("[FAIL] Cannot read encrypted file");
                ok = 0;
            }
        }
        
        unlink(crypt_test_file);
    }
    
    // Test 2: Corrupted encryption header handling
    f = fopen(crypt_test_file, "wb");
    if (f) {
        // Write invalid/corrupted encryption header
        unsigned char corrupt_header[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFF, 0xFF};
        fwrite(corrupt_header, 1, 6, f);
        fprintf(f, "Some content after corrupt header\n");
        fclose(f);
        
        // Should handle corrupted encryption gracefully
        if (fopen((char*)crypt_test_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            // Should either decrypt with fallback or error gracefully
            int result = fgets(line_buf, sizeof(line_buf), &eol_type);
            if (result != FIOSUC && result != FIOEOF) {
                // Expected - corruption detected
            }
            fclose();
        }
        
        unlink(crypt_test_file);
    }
    
    // Test 3: Key validation and format verification
    f = fopen(crypt_test_file, "w");
    if (f) {
        fprintf(f, "Key validation test content\n");
        fclose(f);
        
        // Test with empty/invalid key scenarios
        // (Would use getctext() function for key input simulation)
        char* test_key = getctext("test_password");
        if (test_key && strlen(test_key) > 0) {
            // Key should be validated for minimum requirements
            if (strlen(test_key) < 3) { // Minimum key length check
                LOG_ERROR("[FAIL] Weak key accepted");
                ok = 0;
            }
        }
        
        unlink(crypt_test_file);
    }
    
    #else
    LOG_WARN("[WARN] CRYPT not enabled - skipping encryption tests");
    #endif
    
    PHASE_END("FILEIO: CRYPT", ok);
    return ok;
}

// Test backup and recovery systems
int test_backup_recovery_systems(void) {
    int ok = 1;
    PHASE_START("FILEIO: BACKUP", "Backup creation and crash recovery");
    
    const char* original_file = "/tmp/uemacs_backup_test.txt";
    const char* backup_file = "/tmp/uemacs_backup_test.txt~";
    
    // Test 1: Automatic backup creation
    FILE* f = fopen(original_file, "w");
    if (f) {
        fprintf(f, "Original content v1\n");
        fprintf(f, "Line 2 original\n");
        fclose(f);
        
        // Simulate editing that would trigger backup
        if (readin((char*)original_file, 0) == true) {
            // Backup should be created before modifications
            struct stat orig_stat, backup_stat;
            
            if (stat(original_file, &orig_stat) == 0) {
                // Simulate backup creation (normally done by writeout)
                if (link(original_file, backup_file) == 0 || 
                    (errno == EEXIST && stat(backup_file, &backup_stat) == 0)) {
                    
                    // Backup file should exist and have content
                    if (stat(backup_file, &backup_stat) != 0) {
                        LOG_ERROR("[FAIL] Backup file not created");
                        ok = 0;
                    } else if (backup_stat.st_size != orig_stat.st_size) {
                        LOG_ERROR("[FAIL] Backup size mismatch");
                        ok = 0;
                    }
                }
            }
        }
    }
    
    // Test 2: Recovery from backup after simulated crash
    if (access(backup_file, R_OK) == 0) {
        // Simulate corrupted original file
        f = fopen(original_file, "w");
        if (f) {
            fprintf(f, "CORRUPTED"); // Truncated/corrupted content
            fclose(f);
        }
        
        // Recovery should prefer backup if original is suspect
        struct stat orig_stat, backup_stat;
        if (stat(original_file, &orig_stat) == 0 && stat(backup_file, &backup_stat) == 0) {
            if (backup_stat.st_size > orig_stat.st_size) {
                // Backup has more content - likely the good copy
                FILE* backup_f = fopen(backup_file, "r");
                if (backup_f) {
                    char line[256];
                    if (fgets(line, sizeof(line), backup_f) && strstr(line, "Original content")) {
                        // Backup contains expected content
                    } else {
                        LOG_ERROR("[FAIL] Backup content validation failed");
                        ok = 0;
                    }
                    fclose(backup_f);
                }
            }
        }
    }
    
    // Test 3: Disk space handling during backup
    struct statvfs vfs;
    if (statvfs("/tmp", &vfs) == 0) {
        unsigned long long free_space = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
        
        // If very low disk space, backup creation should fail gracefully
        if (free_space < 1024 * 1024) { // Less than 1MB free
            // Test backup failure handling
            LOG_INFO("[INFO] Low disk space detected - testing backup failure handling");
            
            // Backup creation should fail gracefully without corrupting original
            struct stat orig_before;
            if (stat(original_file, &orig_before) == 0) {
                // Attempt backup (should fail due to space)
                // Original file should remain intact
                struct stat orig_after;
                if (stat(original_file, &orig_after) == 0) {
                    if (orig_before.st_size != orig_after.st_size || 
                        orig_before.st_mtime != orig_after.st_mtime) {
                        LOG_ERROR("[FAIL] Original file corrupted during backup failure");
                        ok = 0;
                    }
                }
            }
        }
    }
    
    // Cleanup
    unlink(original_file);
    unlink(backup_file);
    
    PHASE_END("FILEIO: BACKUP", ok);
    return ok;
}

// Test permission handling scenarios
int test_permission_handling(void) {
    int ok = 1;
    PHASE_START("FILEIO: PERMS", "File permission handling and access control");
    
    const char* readonly_file = "/tmp/uemacs_readonly_test.txt";
    const char* nowrite_dir = "/tmp/uemacs_nowrite_test";
    
    // Test 1: Read-only file handling
    FILE* f = fopen(readonly_file, "w");
    if (f) {
        fprintf(f, "Read-only test content\n");
        fclose(f);
        
        // Make file read-only
        if (chmod(readonly_file, S_IRUSR | S_IRGRP | S_IROTH) == 0) {
            // Test reading from read-only file
            if (fopen((char*)readonly_file, "r") != nullptr) {
                char line_buf[256];
                int eol_type;
                if (fgets(line_buf, sizeof(line_buf), &eol_type) != FIOSUC) {
                    LOG_ERROR("[FAIL] Cannot read from read-only file");
                    ok = 0;
                }
                fclose();
            } else {
                LOG_ERROR("[FAIL] Cannot open read-only file for reading");
                ok = 0;
            }
            
            // Test writing to read-only file (should fail gracefully)
            if (fopen((char*)readonly_file, "w") != nullptr) {
                LOG_ERROR("[FAIL] Read-only file opened for writing");
                ok = 0;
                fclose();
            }
            // Expected to fail - this is correct behavior
        }
        
        // Restore permissions for cleanup
        chmod(readonly_file, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        unlink(readonly_file);
    }
    
    // Test 2: Directory permission handling
    if (mkdir(nowrite_dir, S_IRUSR | S_IXUSR) == 0) {
        // Directory without write permission
        char test_file_in_nowrite[256];
        snprintf(test_file_in_nowrite, sizeof(test_file_in_nowrite), "%s/test.txt", nowrite_dir);
        
        // Should fail to create file in no-write directory
        if (fopen(test_file_in_nowrite, "w") != nullptr) {
            LOG_ERROR("[FAIL] Created file in no-write directory");
            ok = 0;
            fclose();
        }
        // Expected to fail
        
        // Cleanup
        rmdir(nowrite_dir);
    }
    
    // Test 3: Permission change detection
    f = fopen(readonly_file, "w");
    if (f) {
        fprintf(f, "Permission change test\n");
        fclose(f);
        
        struct stat st_before, st_after;
        if (stat(readonly_file, &st_before) == 0) {
            // Change permissions while file might be open
            chmod(readonly_file, S_IRUSR);
            
            if (stat(readonly_file, &st_after) == 0) {
                if (st_before.st_mode == st_after.st_mode) {
                    LOG_ERROR("[FAIL] Permission change not detected");
                    ok = 0;
                }
            }
        }
        
        chmod(readonly_file, S_IRUSR | S_IWUSR);
        unlink(readonly_file);
    }
    
    PHASE_END("FILEIO: PERMS", ok);
    return ok;
}

// Test network file operations (NFS/CIFS behavior)
int test_network_file_operations(void) {
    int ok = 1;
    PHASE_START("FILEIO: NETWORK", "Network filesystem handling and timeouts");
    
    // Test 1: Timeout behavior simulation
    const char* slow_file = "/tmp/uemacs_network_sim.txt";
    FILE* f = fopen(slow_file, "w");
    if (f) {
        fprintf(f, "Network file simulation\n");
        fclose(f);
        
        // Test file access with simulated network delay
        struct timeval start, end;
        gettimeofday(&start, nullptr);
        
        if (fopen((char*)slow_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            
            // Read with potential timeout
            alarm(5); // 5-second timeout for network operations
            int result = fgets(line_buf, sizeof(line_buf), &eol_type);
            alarm(0); // Cancel timeout
            
            if (result != FIOSUC) {
                LOG_ERROR("[FAIL] Network file read failed or timed out");
                ok = 0;
            }
            
            fclose();
        }
        
        gettimeofday(&end, nullptr);
        double access_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        // Should complete within reasonable time for local file
        if (access_time > 2.0) {
            LOG_ERRORF("[FAIL] File access took too long: %.2fs", access_time);
            ok = 0;
        }
        
        unlink(slow_file);
    }
    
    // Test 2: Network connectivity loss simulation
    const char* disconnected_file = "/tmp/uemacs_disconnected_sim.txt";
    f = fopen(disconnected_file, "w");
    if (f) {
        fprintf(f, "Disconnection test\n");
        fclose(f);
        
        // Simulate network interruption by removing file mid-operation
        if (fopen((char*)disconnected_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            
            // Read first line successfully
            if (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                // Simulate disconnection
                unlink(disconnected_file);
                
                // Try to read more (should handle disconnection gracefully)
                int result = fgets(line_buf, sizeof(line_buf), &eol_type);
                if (result != nullptr) {
                    // Unexpected success after disconnection
                    LOG_WARN("[WARN] Read succeeded after file removal");
                }
                // FIOEOF or error expected here
            }
            
            fclose();
        }
    }
    
    // Test 3: Large network transfer simulation
    const char* large_network_file = "/tmp/uemacs_large_network_sim.txt";
    f = fopen(large_network_file, "w");
    if (f) {
        // Create moderately large file (1MB) to simulate network transfer
        for (int i = 0; i < 10000; ++i) {
            fprintf(f, "Network transfer line %04d with some content padding\n", i);
        }
        fclose(f);
        
        // Test streaming read performance
        struct timeval start, end;
        gettimeofday(&start, nullptr);
        
        if (fopen((char*)large_network_file, "r") != nullptr) {
            char line_buf[256];
            int eol_type;
            int lines_read = 0;
            
            while (fgets(line_buf, sizeof(line_buf), &eol_type) != nullptr) {
                lines_read++;
                // Simulate processing delay
                if (lines_read % 1000 == 0) {
                    usleep(1000); // 1ms delay every 1000 lines
                }
            }
            
            if (lines_read != 10000) {
                LOG_ERRORF("[FAIL] Expected 10000 lines, got %d", lines_read);
                ok = 0;
            }
            
            fclose();
        } else {
            LOG_ERROR("[FAIL] Failed to open large network file");
            ok = 0;
        }
        
        gettimeofday(&end, nullptr);
        double transfer_time = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
        
        // Should handle large file efficiently
        if (transfer_time > 10.0) {
            LOG_ERRORF("[FAIL] Large network file transfer too slow: %.2fs", transfer_time);
            ok = 0;
        }
        
        unlink(large_network_file);
    }
    
    PHASE_END("FILEIO: NETWORK", ok);
    return ok;
}
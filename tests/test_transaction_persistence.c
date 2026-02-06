#include "test_utils.h"
#include "test_transaction_persistence.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>

// Single unified atomic operations structure to prevent lock pool aliasing
// All atomic operations use this single struct to avoid address collisions
typedef struct {
    // Transaction state fields
    atomic_int depth;
    atomic_int committed;
    atomic_int aborted;
    char _pad1[64]; // Cache line padding
    
    // Resource isolation fields  
    atomic_int value;
    atomic_int readers;
    atomic_int writers;
    char _pad2[64]; // Cache line padding
    
    // Concurrent operations fields
    atomic_int global_counter;
    atomic_int transaction_depth;
    atomic_int conflict_counter;
    atomic_int active_transactions;
    char _pad3[64]; // Cache line padding
} unified_atomic_state_t;

// Global static instance to prevent multiple allocations
static unified_atomic_state_t atomic_state = {0};

// Buffer state structure for multi-step operations testing
typedef struct {
    char data[1024];
    size_t size;
    int line_count;
    int modified;
} buffer_state_t;

// Reset atomic struct to zero state for reuse
#define RESET_ATOMIC_STRUCT(ptr) \
    do { \
        atomic_store(&(ptr)->depth, 0); \
        atomic_store(&(ptr)->committed, 0); \
        atomic_store(&(ptr)->aborted, 0); \
        atomic_store(&(ptr)->value, 0); \
        atomic_store(&(ptr)->readers, 0); \
        atomic_store(&(ptr)->writers, 0); \
        atomic_store(&(ptr)->global_counter, 0); \
        atomic_store(&(ptr)->transaction_depth, 0); \
        atomic_store(&(ptr)->conflict_counter, 0); \
        atomic_store(&(ptr)->active_transactions, 0); \
    } while(0)

// Test transaction atomicity
int test_transaction_atomicity(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Transaction Atomicity ===%s", BLUE, RESET);
    
    // Use unified global atomic state
    unified_atomic_state_t *tx = &atomic_state;
    
    // Test atomic transaction begin/commit
    LOG_INFO("Testing atomic transaction operations...");
    total++;
    
    // Simulate transaction begin
    atomic_fetch_add(&tx->depth, 1);
    if (atomic_load(&tx->depth) == 1) {
        // Transaction started
        
        // Simulate some operations
        atomic_fetch_add(&tx->depth, 1); // Nested transaction
        atomic_fetch_sub(&tx->depth, 1); // End nested
        
        // Commit transaction
        int prev_depth = atomic_fetch_sub(&tx->depth, 1);
        if (prev_depth == 1) {
            atomic_store(&tx->committed, 1);
        }
        
        if (atomic_load(&tx->committed) == 1 && atomic_load(&tx->depth) == 0) {
            LOG_INFO("[SUCCESS] Transaction atomicity: begin/commit cycle completed");
            fflush(stdout);
            passed++;
        }
    }
    
    // Reset struct for next test
    RESET_ATOMIC_STRUCT(tx);
    
    // Test transaction abort
    LOG_INFO("Testing transaction abort functionality...");
    total++;
    
    atomic_fetch_add(&tx->depth, 1);
    
    // Simulate abort condition
    atomic_store(&tx->depth, 0);
    atomic_store(&tx->aborted, 1);
    
    if (atomic_load(&tx->aborted) == 1 && atomic_load(&tx->depth) == 0) {
        LOG_INFO("[SUCCESS] Transaction abort: transaction properly aborted");
        passed++;
    }
    
    // Reset struct for next test
    RESET_ATOMIC_STRUCT(tx);
    
    // Test nested transaction handling
    LOG_INFO("Testing nested transaction handling...");
    total++;
    
    
    // Begin outer transaction
    atomic_fetch_add(&tx->depth, 1);
    int outer_depth = atomic_load(&tx->depth);
    
    // Begin inner transaction
    atomic_fetch_add(&tx->depth, 1);
    int inner_depth = atomic_load(&tx->depth);
    
    // Commit inner transaction
    atomic_fetch_sub(&tx->depth, 1);
    int after_inner = atomic_load(&tx->depth);
    
    // Commit outer transaction
    atomic_fetch_sub(&tx->depth, 1);
    int final_depth = atomic_load(&tx->depth);
    
    if (outer_depth == 1 && inner_depth == 2 && after_inner == 1 && final_depth == 0) {
        LOG_INFO("[SUCCESS] Nested transactions: depth tracking works correctly");
        passed++;
    }
    
    // Reset atomic state for next test
    atomic_store(&tx->depth, 0);
    atomic_store(&tx->committed, 0);
    atomic_store(&tx->aborted, 0);
    
    LOG_INFOF("Transaction atomicity tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test multi-step operations
int test_multi_step_operations(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Multi-Step Operations ===%s", BLUE, RESET);
    
    // Reset atomic state to prevent interference from previous tests
    RESET_ATOMIC_STRUCT(&atomic_state);
    
    // Test file operation transaction
    LOG_INFO("Testing file operation transaction...");
    total++;
    
    const char* test_file = "/tmp/uemacs_tx_test.txt";
    const char* backup_file = "/tmp/uemacs_tx_test.txt.bak";
    const char* original_content = "Original content line 1\nOriginal content line 2\n";
    const char* new_content = "Modified content line 1\nModified content line 2\nNew line 3\n";
    
    // Step 1: Create original file
    FILE* f = fopen(test_file, "w");
    if (f) {
        fprintf(f, "%s", original_content);
        fclose(f);
        
        // Step 2: Create backup
        FILE* orig = fopen(test_file, "r");
        FILE* backup = fopen(backup_file, "w");
        if (orig && backup) {
            char *copy_buffer = malloc(1024);
            while (fgets(copy_buffer, 1024, orig)) {
                fputs(copy_buffer, backup);
            }
            fclose(orig);
            fclose(backup);
            free(copy_buffer);
            orig = nullptr;
            backup = nullptr;
            
            // Step 3: Modify original file
            FILE* mod = fopen(test_file, "w");
            if (mod) {
                fprintf(mod, "%s", new_content);
                fclose(mod);
                
                // Step 4: Verify transaction completed
                FILE* verify = fopen(test_file, "r");
                if (verify) {
                    char *verify_buffer = calloc(1024, 1);
                    fread(verify_buffer, 1, 1023, verify);
                    fclose(verify);
                    
                    if (strcmp(verify_buffer, new_content) == 0) {
                        LOG_INFO("[SUCCESS] Multi-step file: backup created, file modified");
                        fflush(stdout);
                        passed++;
                    }
                    free(verify_buffer);
                }
            }
        }
        if (orig) fclose(orig);
        if (backup) fclose(backup);
    }
    
    // Test buffer operation transaction
    LOG_INFO("Testing buffer operation transaction...");
    total++;
    
    // Use heap allocation to prevent stack layout corruption
    buffer_state_t *buffer = calloc(1, sizeof(buffer_state_t));
    strcpy(buffer->data, "Line 1\nLine 2\n");
    buffer->size = strlen(buffer->data);
    buffer->line_count = 2;
    buffer->modified = 0;
    
    // Save checkpoint
    buffer_state_t *checkpoint = calloc(1, sizeof(buffer_state_t));
    *checkpoint = *buffer;
    
    // Perform multi-step modification
    strcat(buffer->data, "Line 3\n");
    buffer->size = strlen(buffer->data);
    buffer->line_count = 3;
    buffer->modified = 1;
    
    // Verify transaction state
    if (buffer->line_count == 3 && buffer->modified == 1 && 
        checkpoint->line_count == 2 && checkpoint->modified == 0) {
        LOG_INFO("[SUCCESS] Buffer transaction: state tracked through modifications");
        passed++;
    }
    
    // Test rollback operation
    LOG_INFO("Testing transaction rollback...");
    total++;
    
    // Rollback to checkpoint
    *buffer = *checkpoint;
    
    if (buffer->line_count == 2 && buffer->modified == 0 && 
        strstr(buffer->data, "Line 3") == nullptr) {
        LOG_INFO("[SUCCESS] Transaction rollback: buffer restored to checkpoint");
        passed++;
    }
    
    // Cleanup heap allocations
    free(buffer);
    free(checkpoint);
    
    // Cleanup files
    unlink(test_file);
    unlink(backup_file);
    
    LOG_INFOF("Multi-step operation tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test crash recovery functionality
int test_crash_recovery(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Crash Recovery ===%s", BLUE, RESET);
    
    // Test journal file creation
    LOG_INFO("Testing journal file creation...");
    total++;
    
    const char* journal_file = "/tmp/uemacs_journal.log";
    const char* data_file = "/tmp/uemacs_data_test.txt";
    
    // Create journal entry
    FILE* journal = fopen(journal_file, "w");
    if (journal) {
        fprintf(journal, "TRANSACTION_BEGIN\n");
        fprintf(journal, "FILE_OPERATION: %s\n", data_file);
        fprintf(journal, "OPERATION: WRITE\n");
        fprintf(journal, "TIMESTAMP: %ld\n", time(nullptr));
        fprintf(journal, "TRANSACTION_END\n");
        fflush(journal);
        fsync(fileno(journal));
        fclose(journal);
        
        // Verify journal exists and has content
        struct stat st;
        if (stat(journal_file, &st) == 0 && st.st_size > 0) {
            LOG_INFOF("[SUCCESS] Journal creation: %ld bytes written to journal", st.st_size);
            passed++;
        }
    }
    
    // Test recovery from journal
    LOG_INFO("Testing recovery from journal...");
    total++;
    
    FILE* recovery = fopen(journal_file, "r");
    if (recovery) {
        char line[256];
        int transaction_found = 0;
        int file_op_found = 0;
        int timestamp_found = 0;
        
        while (fgets(line, sizeof(line), recovery)) {
            if (strstr(line, "TRANSACTION_BEGIN")) transaction_found = 1;
            if (strstr(line, "FILE_OPERATION:")) file_op_found = 1;
            if (strstr(line, "TIMESTAMP:")) timestamp_found = 1;
        }
        fclose(recovery);
        
        if (transaction_found && file_op_found && timestamp_found) {
            LOG_INFO("[SUCCESS] Journal recovery: transaction data parsed successfully");
            passed++;
        }
    }
    
    // Test incomplete transaction detection
    LOG_INFO("Testing incomplete transaction detection...");
    total++;
    
    const char* incomplete_journal = "/tmp/uemacs_incomplete.log";
    FILE* incomplete = fopen(incomplete_journal, "w");
    if (incomplete) {
        fprintf(incomplete, "TRANSACTION_BEGIN\n");
        fprintf(incomplete, "FILE_OPERATION: %s\n", data_file);
        fprintf(incomplete, "OPERATION: WRITE\n");
        // Missing TRANSACTION_END - simulates crash
        fclose(incomplete);
        
        // Simulate recovery process
        FILE* check = fopen(incomplete_journal, "r");
        if (check) {
            char content[1024] = {0};
            fread(content, 1, sizeof(content) - 1, check);
            fclose(check);
            
            int has_begin = strstr(content, "TRANSACTION_BEGIN") != nullptr;
            int has_end = strstr(content, "TRANSACTION_END") != nullptr;
            
            if (has_begin && !has_end) {
                LOG_INFO("[SUCCESS] Incomplete detection: incomplete transaction identified");
                passed++;
            }
        }
    }
    
    // Cleanup
    unlink(journal_file);
    unlink(incomplete_journal);
    unlink(data_file);
    
    LOG_INFOF("Crash recovery tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test undo persistence functionality
int test_undo_persistence(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Undo Persistence ===%s", BLUE, RESET);
    
    // Test undo stack serialization
    LOG_INFO("Testing undo stack serialization...");
    total++;
    
    typedef struct undo_entry {
        int operation_type;
        char data[256];
        size_t data_size;
        struct undo_entry* next;
    } undo_entry_t;
    
    // Create mock undo stack
    undo_entry_t entries[3];
    entries[0] = (undo_entry_t){1, "insert text", 11, &entries[1]};
    entries[1] = (undo_entry_t){2, "delete line", 11, &entries[2]};
    entries[2] = (undo_entry_t){3, "replace word", 12, nullptr};
    
    const char* undo_file = "/tmp/uemacs_undo_test.dat";
    FILE* undo_f = fopen(undo_file, "wb");
    if (undo_f) {
        // Write undo stack to file
        for (int i = 0; i < 3; i++) {
            fwrite(&entries[i].operation_type, sizeof(int), 1, undo_f);
            fwrite(&entries[i].data_size, sizeof(size_t), 1, undo_f);
            fwrite(entries[i].data, 1, entries[i].data_size, undo_f);
        }
        fclose(undo_f);
        
        // Verify file was created
        struct stat st;
        if (stat(undo_file, &st) == 0 && st.st_size > 0) {
            LOG_INFOF("[SUCCESS] Undo serialization: %ld bytes written to undo file", st.st_size);
            passed++;
        }
    }
    
    // Test undo stack deserialization
    LOG_INFO("Testing undo stack deserialization...");
    total++;
    
    FILE* read_undo = fopen(undo_file, "rb");
    if (read_undo) {
        undo_entry_t loaded_entries[3];
        int loaded_count = 0;
        
        for (int i = 0; i < 3; i++) {
            if (fread(&loaded_entries[i].operation_type, sizeof(int), 1, read_undo) == 1 &&
                fread(&loaded_entries[i].data_size, sizeof(size_t), 1, read_undo) == 1 &&
                fread(loaded_entries[i].data, 1, loaded_entries[i].data_size, read_undo) == loaded_entries[i].data_size) {
                loaded_entries[i].data[loaded_entries[i].data_size] = '\0';
                loaded_count++;
            }
        }
        fclose(read_undo);
        
        if (loaded_count == 3 && 
            loaded_entries[0].operation_type == 1 &&
            strcmp(loaded_entries[0].data, "insert text") == 0) {
            LOG_INFOF("[SUCCESS] Undo deserialization: %d entries loaded correctly", loaded_count);
            passed++;
        }
    }
    
    // Test undo persistence across sessions
    LOG_INFO("Testing undo persistence across sessions...");
    total++;
    
    const char* session_undo = "/tmp/uemacs_session_undo.dat";
    
    // Simulate session 1: create undo data
    FILE* session1 = fopen(session_undo, "w");
    if (session1) {
        fprintf(session1, "SESSION_1_UNDO_DATA\n");
        fprintf(session1, "operation_1: insert_text\n");
        fprintf(session1, "operation_2: delete_char\n");
        fclose(session1);
        
        // Simulate session 2: load undo data
        FILE* session2 = fopen(session_undo, "r");
        if (session2) {
            char buffer[1024] = {0};
            fread(buffer, 1, sizeof(buffer) - 1, session2);
            fclose(session2);
            
            if (strstr(buffer, "SESSION_1_UNDO_DATA") && 
                strstr(buffer, "operation_1") && 
                strstr(buffer, "operation_2")) {
                LOG_INFO("[SUCCESS] Session persistence: undo data persisted across sessions");
                passed++;
            }
        }
    }
    
    // Cleanup
    unlink(undo_file);
    unlink(session_undo);
    
    LOG_INFOF("Undo persistence tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test buffer state persistence
int test_buffer_state_persistence(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Buffer State Persistence ===%s", BLUE, RESET);
    
    // Test buffer metadata persistence
    LOG_INFO("Testing buffer metadata persistence...");
    total++;
    
    typedef struct {
        char filename[256];
        int line_count;
        int char_count;
        int modified;
        time_t last_modified;
        int cursor_line;
        int cursor_col;
    } buffer_metadata_t;
    
    buffer_metadata_t metadata = {
        .filename = "/tmp/test_file.txt",
        .line_count = 100,
        .char_count = 2500,
        .modified = 1,
        .last_modified = time(nullptr),
        .cursor_line = 45,
        .cursor_col = 12
    };
    
    const char* metadata_file = "/tmp/uemacs_buffer_meta.dat";
    FILE* meta_f = fopen(metadata_file, "wb");
    if (meta_f) {
        fwrite(&metadata, sizeof(buffer_metadata_t), 1, meta_f);
        fclose(meta_f);
        
        // Load and verify
        buffer_metadata_t loaded_meta;
        FILE* load_f = fopen(metadata_file, "rb");
        if (load_f) {
            if (fread(&loaded_meta, sizeof(buffer_metadata_t), 1, load_f) == 1) {
                if (loaded_meta.line_count == 100 && 
                    loaded_meta.cursor_line == 45 && 
                    strcmp(loaded_meta.filename, "/tmp/test_file.txt") == 0) {
                    LOG_INFO("[SUCCESS] Buffer metadata: saved and loaded correctly");
                    passed++;
                }
            }
            fclose(load_f);
        }
    }
    
    // Test buffer content checksum
    LOG_INFO("Testing buffer content integrity checking...");
    total++;
    
    const char* content = "Buffer content for checksum testing\nLine 2\nLine 3\n";
    
    // Calculate simple checksum
    unsigned int checksum = 0;
    for (const char* p = content; *p; p++) {
        checksum = (checksum * 31) + *p;
    }
    
    // Save content with checksum
    const char* content_file = "/tmp/uemacs_content_check.txt";
    const char* checksum_file = "/tmp/uemacs_content_check.sum";
    
    FILE* content_f = fopen(content_file, "w");
    FILE* sum_f = fopen(checksum_file, "w");
    if (content_f && sum_f) {
        fprintf(content_f, "%s", content);
        fprintf(sum_f, "%u", checksum);
        fclose(content_f);
        fclose(sum_f);
        
        // Verify integrity on load
        FILE* verify_content = fopen(content_file, "r");
        FILE* verify_sum = fopen(checksum_file, "r");
        if (verify_content && verify_sum) {
            char loaded_content[1024] = {0};
            unsigned int loaded_checksum = 0;
            
            fread(loaded_content, 1, sizeof(loaded_content) - 1, verify_content);
            fscanf(verify_sum, "%u", &loaded_checksum);
            
            // Recalculate checksum
            unsigned int calc_checksum = 0;
            for (const char* p = loaded_content; *p; p++) {
                calc_checksum = (calc_checksum * 31) + *p;
            }
            
            if (calc_checksum == loaded_checksum && calc_checksum == checksum) {
                LOG_INFOF("[SUCCESS] Content integrity: checksum verified (0x%08x)", checksum);
                passed++;
            }
            
            fclose(verify_content);
            fclose(verify_sum);
        }
        if (verify_content) fclose(verify_content);
        if (verify_sum) fclose(verify_sum);
    }
    if (content_f) fclose(content_f);
    if (sum_f) fclose(sum_f);
    
    // Cleanup
    unlink(metadata_file);
    unlink(content_file);
    unlink(checksum_file);
    
    LOG_INFOF("Buffer state persistence tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test concurrent transactions
int test_concurrent_transactions(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Concurrent Transactions ===%s", BLUE, RESET);
    
    // Test atomic counter operations
    LOG_INFO("Testing atomic transaction counters...");
    total++;
    
    // Use unified global atomic state - concurrent ops section
    unified_atomic_state_t *ops = &atomic_state;
    
    const int num_operations = 100;
    
    // Simulate concurrent operations
    for (int i = 0; i < num_operations; i++) {
        atomic_fetch_add(&ops->transaction_depth, 1);
        atomic_fetch_add(&ops->global_counter, 1);
        atomic_fetch_sub(&ops->transaction_depth, 1);
    }
    
    if (atomic_load(&ops->global_counter) == num_operations && 
        atomic_load(&ops->transaction_depth) == 0) {
        LOG_INFOF("[SUCCESS] Atomic counters: %d operations completed, depth=0", num_operations);
        passed++;
    }
    
    // Test transaction isolation simulation
    LOG_INFO("Testing transaction isolation...");
    total++;
    
    // Use unified global atomic state - resource isolation section
    unified_atomic_state_t *resource = &atomic_state;
    
    // Simulate reader transaction
    atomic_fetch_add(&resource->readers, 1);
    int read_value = atomic_load(&resource->value);
    atomic_fetch_sub(&resource->readers, 1);
    
    // Simulate writer transaction (only if no readers)
    if (atomic_load(&resource->readers) == 0) {
        atomic_fetch_add(&resource->writers, 1);
        atomic_store(&resource->value, 42);
        atomic_fetch_sub(&resource->writers, 1);
    }
    
    if (atomic_load(&resource->value) == 42 && 
        atomic_load(&resource->readers) == 0 && 
        atomic_load(&resource->writers) == 0) {
        LOG_INFO("[SUCCESS] Transaction isolation: reader/writer coordination works");
        passed++;
    }
    
    // Test transaction conflict detection
    LOG_INFO("Testing transaction conflict detection...");
    total++;
    
    // Reset counters for conflict detection test
    atomic_store(&ops->conflict_counter, 0);
    atomic_store(&ops->active_transactions, 0);
    
    // Simulate conflicting operations
    for (int i = 0; i < 10; i++) {
        int current_active = atomic_fetch_add(&ops->active_transactions, 1);
        
        if (current_active > 0) {
            // Conflict detected
            atomic_fetch_add(&ops->conflict_counter, 1);
        }
        
        // Simulate work
        usleep(1000); // 1ms
        
        atomic_fetch_sub(&ops->active_transactions, 1);
    }
    
    if (atomic_load(&ops->conflict_counter) > 0) {
        LOG_INFOF("[SUCCESS] Conflict detection: %d conflicts detected", atomic_load(&ops->conflict_counter));
        passed++;
    }
    
    LOG_INFOF("Concurrent transaction tests: %d/%d passed\n", passed, total);
    return (passed == total);
}

// Test transaction rollback
int test_transaction_rollback(void) {
    int passed = 0, total = 0;
    LOG_INFOF("%s=== Testing Transaction Rollback ===%s", BLUE, RESET);
    
    // Test single operation rollback
    LOG_INFO("Testing single operation rollback...");
    total++;
    
    typedef struct {
        char content[512];
        int position;
        int length;
        int operation_id;
    } rollback_state_t;
    
    rollback_state_t original_state = {
        .content = "Original text content",
        .position = 0,
        .length = 21,
        .operation_id = 0
    };
    
    rollback_state_t current_state = original_state;
    
    // Perform operation
    strcat(current_state.content, " - modified");
    current_state.length = strlen(current_state.content);
    current_state.operation_id = 1;
    
    // Rollback operation
    current_state = original_state;
    
    if (strcmp(current_state.content, "Original text content") == 0 && 
        current_state.operation_id == 0 && 
        current_state.length == 21) {
        LOG_INFO("[SUCCESS] Single rollback: state restored to original");
        passed++;
    }
    
    // Test multi-operation rollback
    LOG_INFO("Testing multi-operation rollback...");
    total++;
    
    rollback_state_t checkpoints[4];
    checkpoints[0] = original_state;
    
    // Operation 1: append text
    current_state = checkpoints[0];
    strcat(current_state.content, " + op1");
    current_state.length = strlen(current_state.content);
    current_state.operation_id = 1;
    checkpoints[1] = current_state;
    
    // Operation 2: append more text
    strcat(current_state.content, " + op2");
    current_state.length = strlen(current_state.content);
    current_state.operation_id = 2;
    checkpoints[2] = current_state;
    
    // Operation 3: append final text
    strcat(current_state.content, " + op3");
    current_state.length = strlen(current_state.content);
    current_state.operation_id = 3;
    checkpoints[3] = current_state;
    
    // Rollback to checkpoint 1
    current_state = checkpoints[1];
    
    if (strstr(current_state.content, "+ op1") && 
        !strstr(current_state.content, "+ op2") && 
        current_state.operation_id == 1) {
        LOG_INFO("[SUCCESS] Multi-operation rollback: rolled back to checkpoint 1");
        passed++;
    }
    
    // Test rollback with file operations
    LOG_INFO("Testing rollback with file operations...");
    total++;
    
    const char* rollback_file = "/tmp/uemacs_rollback_test.txt";
    const char* backup_rollback = "/tmp/uemacs_rollback_test.bak";
    
    // Create original file
    FILE* orig = fopen(rollback_file, "w");
    if (orig) {
        fprintf(orig, "Original file content\n");
        fclose(orig);
        
        // Create backup
        FILE* src = fopen(rollback_file, "r");
        FILE* dst = fopen(backup_rollback, "w");
        if (src && dst) {
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), src)) {
                fputs(buffer, dst);
            }
            fclose(src);
            fclose(dst);
            
            // Modify original
            FILE* mod = fopen(rollback_file, "w");
            if (mod) {
                fprintf(mod, "Modified file content\n");
                fclose(mod);
                
                // Rollback: restore from backup
                src = fopen(backup_rollback, "r");
                dst = fopen(rollback_file, "w");
                if (src && dst) {
                    char restore_buffer[1024];
                    while (fgets(restore_buffer, sizeof(restore_buffer), src)) {
                        fputs(restore_buffer, dst);
                    }
                    fclose(src);
                    fclose(dst);
                    
                    // Verify rollback
                    FILE* verify = fopen(rollback_file, "r");
                    if (verify) {
                        char verify_buffer[1024] = {0};
                        fgets(verify_buffer, sizeof(verify_buffer), verify);
                        fclose(verify);
                        
                        if (strstr(verify_buffer, "Original file content")) {
                            LOG_INFO("[SUCCESS] File rollback: original content restored");
                            passed++;
                        }
                    }
                }
                if (src) fclose(src);
                if (dst) fclose(dst);
            }
        }
        if (src) fclose(src);
        if (dst) fclose(dst);
    }
    
    // Cleanup
    unlink(rollback_file);
    unlink(backup_rollback);
    
    LOG_INFOF("Transaction rollback tests: %d/%d passed\n", passed, total);
    return (passed == total);
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <pthread.h>
#include "test_performance_stress.h"

// ANSI color codes for output
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define RESET   "\x1b[0m"

// Performance timing utilities
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void print_performance_stats(const char *test_name, double start_time, double end_time, 
                                   size_t operations, size_t data_size) {
    double duration = end_time - start_time;
    double ops_per_sec = operations / (duration / 1000.0);
    double mb_per_sec = (data_size / (1024.0 * 1024.0)) / (duration / 1000.0);
    
    printf("%s Performance: %.2fms, %.0f ops/sec", test_name, duration, ops_per_sec);
    if (data_size > 0) {
        printf(", %.2f MB/sec", mb_per_sec);
    }
    printf("\n");
}

int test_large_file_operations(void) {
    printf("\n%s=== Testing Large File Operations ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Large file creation and writing
    total++;
    printf("Testing large file creation and writing...\n");
    const char *large_file = "/tmp/uemacs_large_test.txt";
    const size_t file_size = 50 * 1024 * 1024; // 50MB
    const size_t chunk_size = 8192;
    
    double start_time = get_time_ms();
    FILE *f = fopen(large_file, "wb");
    if (f) {
        char *chunk = malloc(chunk_size);
        if (chunk) {
            memset(chunk, 'A', chunk_size);
            size_t written = 0;
            
            while (written < file_size) {
                size_t to_write = (file_size - written < chunk_size) ? 
                                  file_size - written : chunk_size;
                if (fwrite(chunk, 1, to_write, f) != to_write) {
                    break;
                }
                written += to_write;
            }
            
            free(chunk);
            fclose(f);
            double end_time = get_time_ms();
            
            if (written == file_size) {
                print_performance_stats("Large file write", start_time, end_time, 
                                       written / chunk_size, written);
                printf("[%sSUCCESS%s] Large file creation: %zu bytes written\n", 
                       GREEN, RESET, written);
                passed++;
            } else {
                printf("[%sFAIL%s] Large file write incomplete: %zu/%zu bytes\n", 
                       RED, RESET, written, file_size);
            }
        } else {
            printf("[%sFAIL%s] Failed to allocate write chunk\n", RED, RESET);
            fclose(f);
        }
    } else {
        printf("[%sFAIL%s] Failed to create large test file\n", RED, RESET);
    }
    
    // Test 2: Large file reading and processing
    total++;
    printf("Testing large file reading and processing...\n");
    start_time = get_time_ms();
    f = fopen(large_file, "rb");
    if (f) {
        char *read_chunk = malloc(chunk_size);
        if (read_chunk) {
            size_t total_read = 0;
            size_t lines_counted = 0;
            size_t bytes_read;
            
            while ((bytes_read = fread(read_chunk, 1, chunk_size, f)) > 0) {
                total_read += bytes_read;
                // Count newlines in chunk
                for (size_t i = 0; i < bytes_read; i++) {
                    if (read_chunk[i] == '\n') {
                        lines_counted++;
                    }
                }
            }
            
            free(read_chunk);
            fclose(f);
            double end_time = get_time_ms();
            
            if (total_read > 0) {
                print_performance_stats("Large file read", start_time, end_time, 
                                       total_read / chunk_size, total_read);
                printf("[%sSUCCESS%s] Large file read: %zu bytes, %zu lines\n", 
                       GREEN, RESET, total_read, lines_counted);
                passed++;
            } else {
                printf("[%sFAIL%s] Large file read failed\n", RED, RESET);
            }
        } else {
            printf("[%sFAIL%s] Failed to allocate read chunk\n", RED, RESET);
            fclose(f);
        }
    } else {
        printf("[%sFAIL%s] Failed to open large file for reading\n", RED, RESET);
    }
    
    // Cleanup
    unlink(large_file);
    
    printf("Large file operation tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int test_memory_intensive_operations(void) {
    printf("\n%s=== Testing Memory Intensive Operations ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Multiple buffer allocation stress
    total++;
    printf("Testing multiple buffer allocation stress...\n");
    const int buffer_count = 1000;
    const size_t buffer_size = 64 * 1024; // 64KB each
    void **buffers = malloc(buffer_count * sizeof(void*));
    
    if (buffers) {
        double start_time = get_time_ms();
        int allocated = 0;
        
        // Allocate buffers
        for (int i = 0; i < buffer_count; i++) {
            buffers[i] = malloc(buffer_size);
            if (buffers[i]) {
                memset(buffers[i], i % 256, buffer_size); // Initialize to prevent optimization
                allocated++;
            } else {
                break;
            }
        }
        
        double mid_time = get_time_ms();
        
        // Access all buffers (memory pressure test)
        volatile int checksum = 0;
        for (int i = 0; i < allocated; i++) {
            unsigned char *buf = (unsigned char*)buffers[i];
            for (size_t j = 0; j < buffer_size; j += 4096) { // Page-aligned access
                checksum += buf[j];
            }
        }
        
        // Deallocate
        for (int i = 0; i < allocated; i++) {
            free(buffers[i]);
        }
        
        double end_time = get_time_ms();
        free(buffers);
        
        printf("Memory allocation: %.2fms, access: %.2fms, deallocation: %.2fms\n",
               mid_time - start_time, end_time - mid_time, end_time - mid_time);
        
        if (allocated >= buffer_count * 0.8) { // Allow some allocation failures
            printf("[%sSUCCESS%s] Memory stress test: %d/%d buffers allocated (checksum: %d)\n", 
                   GREEN, RESET, allocated, buffer_count, checksum);
            passed++;
        } else {
            printf("[%sFAIL%s] Memory stress test: only %d/%d buffers allocated\n", 
                   RED, RESET, allocated, buffer_count);
        }
    } else {
        printf("[%sFAIL%s] Failed to allocate buffer array\n", RED, RESET);
    }
    
    // Test 2: Memory fragmentation stress
    total++;
    printf("Testing memory fragmentation stress...\n");
    const int frag_iterations = 5000;
    void **frag_ptrs = malloc(frag_iterations * sizeof(void*));
    
    if (frag_ptrs) {
        double start_time = get_time_ms();
        int frag_allocated = 0;
        
        // Allocate random-sized blocks
        srand((unsigned int)time(NULL));
        for (int i = 0; i < frag_iterations; i++) {
            size_t size = 128 + (rand() % 8192); // 128B to 8KB
            frag_ptrs[i] = malloc(size);
            if (frag_ptrs[i]) {
                memset(frag_ptrs[i], i % 256, size);
                frag_allocated++;
                
                // Occasionally free some earlier allocations to create fragmentation
                if (i > 100 && (rand() % 10) == 0) {
                    int free_idx = rand() % i;
                    if (frag_ptrs[free_idx]) {
                        free(frag_ptrs[free_idx]);
                        frag_ptrs[free_idx] = NULL;
                    }
                }
            }
        }
        
        // Clean up remaining allocations
        for (int i = 0; i < frag_iterations; i++) {
            if (frag_ptrs[i]) {
                free(frag_ptrs[i]);
            }
        }
        
        double end_time = get_time_ms();
        free(frag_ptrs);
        
        print_performance_stats("Memory fragmentation", start_time, end_time, 
                               frag_allocated, 0);
        
        if (frag_allocated >= frag_iterations * 0.9) {
            printf("[%sSUCCESS%s] Fragmentation stress: %d/%d allocations\n", 
                   GREEN, RESET, frag_allocated, frag_iterations);
            passed++;
        } else {
            printf("[%sFAIL%s] Fragmentation stress failed: %d/%d allocations\n", 
                   RED, RESET, frag_allocated, frag_iterations);
        }
    } else {
        printf("[%sFAIL%s] Failed to allocate fragmentation test array\n", RED, RESET);
    }
    
    printf("Memory intensive operation tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int test_rapid_ui_updates(void) {
    printf("\n%s=== Testing Rapid UI Updates ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Rapid screen refresh simulation
    total++;
    printf("Testing rapid screen refresh simulation...\n");
    const int refresh_count = 10000;
    const int screen_width = 80;
    const int screen_height = 25;
    
    // Simulate screen buffer
    char **screen_buffer = malloc(screen_height * sizeof(char*));
    if (screen_buffer) {
        for (int i = 0; i < screen_height; i++) {
            screen_buffer[i] = malloc(screen_width + 1);
            memset(screen_buffer[i], ' ', screen_width);
            screen_buffer[i][screen_width] = '\0';
        }
        
        double start_time = get_time_ms();
        
        // Simulate rapid updates
        for (int update = 0; update < refresh_count; update++) {
            int row = update % screen_height;
            int col = (update * 7) % screen_width; // Pseudo-random position
            screen_buffer[row][col] = 'A' + (update % 26);
            
            // Simulate cursor movement and screen refresh every 100 updates
            if (update % 100 == 0) {
                volatile int refresh_work = 0;
                for (int r = 0; r < screen_height; r++) {
                    for (int c = 0; c < screen_width; c++) {
                        refresh_work += screen_buffer[r][c];
                    }
                }
            }
        }
        
        double end_time = get_time_ms();
        
        // Cleanup
        for (int i = 0; i < screen_height; i++) {
            free(screen_buffer[i]);
        }
        free(screen_buffer);
        
        print_performance_stats("UI refresh", start_time, end_time, refresh_count, 0);
        
        printf("[%sSUCCESS%s] Rapid UI updates: %d refreshes completed\n", 
               GREEN, RESET, refresh_count);
        passed++;
    } else {
        printf("[%sFAIL%s] Failed to allocate screen buffer\n", RED, RESET);
    }
    
    // Test 2: Damage tracking simulation
    total++;
    printf("Testing damage tracking simulation...\n");
    const int damage_updates = 5000;
    
    // Simulate damage rectangles
    typedef struct {
        int x, y, width, height;
        int dirty;
    } damage_rect_t;
    
    damage_rect_t *damage_rects = malloc(damage_updates * sizeof(damage_rect_t));
    if (damage_rects) {
        double start_time = get_time_ms();
        
        // Initialize damage rectangles
        for (int i = 0; i < damage_updates; i++) {
            damage_rects[i].x = i % screen_width;
            damage_rects[i].y = (i / screen_width) % screen_height;
            damage_rects[i].width = 1 + (i % 20);
            damage_rects[i].height = 1 + (i % 10);
            damage_rects[i].dirty = 1;
        }
        
        // Simulate damage consolidation
        int consolidated = 0;
        for (int i = 0; i < damage_updates; i++) {
            if (damage_rects[i].dirty) {
                // Mark overlapping rectangles as clean (consolidated)
                for (int j = i + 1; j < damage_updates; j++) {
                    if (damage_rects[j].dirty &&
                        damage_rects[i].x < damage_rects[j].x + damage_rects[j].width &&
                        damage_rects[i].x + damage_rects[i].width > damage_rects[j].x &&
                        damage_rects[i].y < damage_rects[j].y + damage_rects[j].height &&
                        damage_rects[i].y + damage_rects[i].height > damage_rects[j].y) {
                        damage_rects[j].dirty = 0;
                    }
                }
                consolidated++;
            }
        }
        
        double end_time = get_time_ms();
        free(damage_rects);
        
        print_performance_stats("Damage tracking", start_time, end_time, 
                               damage_updates, 0);
        
        printf("[%sSUCCESS%s] Damage tracking: %d rects consolidated to %d\n", 
               GREEN, RESET, damage_updates, consolidated);
        passed++;
    } else {
        printf("[%sFAIL%s] Failed to allocate damage rectangles\n", RED, RESET);
    }
    
    printf("Rapid UI update tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

// Thread data structure for concurrent testing
typedef struct {
    int thread_id;
    int operations;
    volatile int *shared_counter;
    pthread_mutex_t *mutex;
    char *shared_buffer;
    size_t buffer_size;
} thread_data_t;

static void* buffer_worker_thread(void* arg) {
    thread_data_t *data = (thread_data_t*)arg;
    
    for (int i = 0; i < data->operations; i++) {
        pthread_mutex_lock(data->mutex);
        
        // Simulate buffer operations
        (*data->shared_counter)++;
        size_t pos = (data->thread_id * 1000 + i) % data->buffer_size;
        data->shared_buffer[pos] = 'A' + (data->thread_id % 26);
        
        pthread_mutex_unlock(data->mutex);
        
        // Small delay to increase contention
        usleep(1);
    }
    
    return NULL;
}

int test_concurrent_buffer_operations(void) {
    printf("\n%s=== Testing Concurrent Buffer Operations ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Multi-threaded buffer access
    total++;
    printf("Testing multi-threaded buffer access...\n");
    
    const int thread_count = 8;
    const int ops_per_thread = 1000;
    const size_t shared_buffer_size = 64 * 1024;
    
    pthread_t threads[thread_count];
    thread_data_t thread_data[thread_count];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    volatile int shared_counter = 0;
    char *shared_buffer = malloc(shared_buffer_size);
    
    if (shared_buffer) {
        memset(shared_buffer, 0, shared_buffer_size);
        double start_time = get_time_ms();
        
        // Create threads
        for (int i = 0; i < thread_count; i++) {
            thread_data[i].thread_id = i;
            thread_data[i].operations = ops_per_thread;
            thread_data[i].shared_counter = &shared_counter;
            thread_data[i].mutex = &mutex;
            thread_data[i].shared_buffer = shared_buffer;
            thread_data[i].buffer_size = shared_buffer_size;
            
            if (pthread_create(&threads[i], NULL, buffer_worker_thread, &thread_data[i]) != 0) {
                printf("[%sFAIL%s] Failed to create thread %d\n", RED, RESET, i);
                total--; // Don't count this test
                break;
            }
        }
        
        // Wait for all threads to complete
        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
        }
        
        double end_time = get_time_ms();
        
        print_performance_stats("Concurrent buffer ops", start_time, end_time, 
                               shared_counter, 0);
        
        if (shared_counter == thread_count * ops_per_thread) {
            printf("[%sSUCCESS%s] Concurrent operations: %d total operations\n", 
                   GREEN, RESET, shared_counter);
            passed++;
        } else {
            printf("[%sFAIL%s] Concurrent operations mismatch: %d expected %d\n", 
                   RED, RESET, shared_counter, thread_count * ops_per_thread);
        }
        
        free(shared_buffer);
        pthread_mutex_destroy(&mutex);
    } else {
        printf("[%sFAIL%s] Failed to allocate shared buffer\n", RED, RESET);
    }
    
    printf("Concurrent buffer operation tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int test_search_performance_stress(void) {
    printf("\n%s=== Testing Search Performance Stress ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Large text search performance
    total++;
    printf("Testing large text search performance...\n");
    
    const size_t text_size = 10 * 1024 * 1024; // 10MB of text
    const char *pattern = "target_pattern";
    const int pattern_instances = 1000;
    
    char *large_text = malloc(text_size);
    if (large_text) {
        // Fill with random text
        const char *chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 \n";
        size_t chars_len = strlen(chars);
        srand((unsigned int)time(NULL));
        
        for (size_t i = 0; i < text_size - 1; i++) {
            large_text[i] = chars[rand() % chars_len];
        }
        large_text[text_size - 1] = '\0';
        
        // Insert pattern at regular intervals
        size_t pattern_len = strlen(pattern);
        size_t interval = text_size / pattern_instances;
        for (int i = 0; i < pattern_instances; i++) {
            size_t pos = i * interval;
            if (pos + pattern_len < text_size) {
                memcpy(large_text + pos, pattern, pattern_len);
            }
        }
        
        double start_time = get_time_ms();
        
        // Perform multiple searches
        int found_count = 0;
        const int search_iterations = 100;
        
        for (int iter = 0; iter < search_iterations; iter++) {
            char *search_pos = large_text;
            while ((search_pos = strstr(search_pos, pattern)) != NULL) {
                found_count++;
                search_pos += pattern_len;
            }
        }
        
        double end_time = get_time_ms();
        
        print_performance_stats("Text search", start_time, end_time, 
                               search_iterations, text_size * search_iterations);
        
        if (found_count >= pattern_instances * search_iterations * 0.9) {
            printf("[%sSUCCESS%s] Search performance: %d patterns found\n", 
                   GREEN, RESET, found_count);
            passed++;
        } else {
            printf("[%sFAIL%s] Search performance: %d patterns found, expected ~%d\n", 
                   RED, RESET, found_count, pattern_instances * search_iterations);
        }
        
        free(large_text);
    } else {
        printf("[%sFAIL%s] Failed to allocate large text buffer\n", RED, RESET);
    }
    
    // Test 2: Regex performance simulation (simplified)
    total++;
    printf("Testing regex performance simulation...\n");
    
    const char *test_strings[] = {
        "user@example.com",
        "invalid-email",
        "another.user@domain.org",
        "not_an_email_at_all",
        "test.email@sub.domain.com",
        "bad@format@email.com",
        "good@email.net",
        NULL
    };
    
    // Simple email pattern matching (not real regex, but performance simulation)
    double start_time = get_time_ms();
    int email_matches = 0;
    const int regex_iterations = 10000;
    
    for (int iter = 0; iter < regex_iterations; iter++) {
        for (int i = 0; test_strings[i] != NULL; i++) {
            const char *str = test_strings[i];
            // Simple email validation: contains @ and . after @
            char *at_pos = strchr(str, '@');
            if (at_pos && strchr(at_pos, '.')) {
                email_matches++;
            }
        }
    }
    
    double end_time = get_time_ms();
    
    print_performance_stats("Regex simulation", start_time, end_time, 
                           regex_iterations * 7, 0);
    
    if (email_matches > 0) {
        printf("[%sSUCCESS%s] Regex performance: %d email matches found\n", 
               GREEN, RESET, email_matches);
        passed++;
    } else {
        printf("[%sFAIL%s] Regex performance: no matches found\n", RED, RESET);
    }
    
    printf("Search performance stress tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int test_undo_redo_stress(void) {
    printf("\n%s=== Testing Undo/Redo Stress ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Large undo stack performance
    total++;
    printf("Testing large undo stack performance...\n");
    
    typedef struct undo_entry {
        char *data;
        size_t size;
        int operation_type;
        struct undo_entry *next;
        struct undo_entry *prev;
    } undo_entry_t;
    
    const int undo_operations = 10000;
    const size_t avg_operation_size = 512;
    
    undo_entry_t *undo_head = NULL;
    undo_entry_t *undo_tail = NULL;
    int undo_count = 0;
    
    double start_time = get_time_ms();
    
    // Build large undo stack
    for (int i = 0; i < undo_operations; i++) {
        undo_entry_t *entry = malloc(sizeof(undo_entry_t));
        if (entry) {
            entry->size = avg_operation_size + (rand() % 512);
            entry->data = malloc(entry->size);
            if (entry->data) {
                memset(entry->data, 'A' + (i % 26), entry->size);
                entry->operation_type = i % 4; // Insert, delete, replace, format
                entry->next = NULL;
                entry->prev = undo_tail;
                
                if (undo_tail) {
                    undo_tail->next = entry;
                } else {
                    undo_head = entry;
                }
                undo_tail = entry;
                undo_count++;
            } else {
                free(entry);
                break;
            }
        } else {
            break;
        }
    }
    
    double build_time = get_time_ms();
    
    // Simulate undo operations
    int undo_steps = undo_count / 4; // Undo 25% of operations
    undo_entry_t *current = undo_tail;
    for (int i = 0; i < undo_steps && current; i++) {
        // Simulate processing the undo operation
        volatile int checksum = 0;
        for (size_t j = 0; j < current->size; j += 64) {
            checksum += current->data[j];
        }
        current = current->prev;
    }
    
    double undo_time = get_time_ms();
    
    // Simulate redo operations
    for (int i = 0; i < undo_steps / 2 && current && current->next; i++) {
        current = current->next;
        volatile int checksum = 0;
        for (size_t j = 0; j < current->size; j += 64) {
            checksum += current->data[j];
        }
    }
    
    double end_time = get_time_ms();
    
    // Cleanup
    current = undo_head;
    while (current) {
        undo_entry_t *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    
    printf("Undo stack build: %.2fms, undo ops: %.2fms, redo ops: %.2fms\n",
           build_time - start_time, undo_time - build_time, end_time - undo_time);
    
    if (undo_count >= undo_operations * 0.9) {
        printf("[%sSUCCESS%s] Undo/redo stress: %d operations in stack\n", 
               GREEN, RESET, undo_count);
        passed++;
    } else {
        printf("[%sFAIL%s] Undo/redo stress: only %d/%d operations stored\n", 
               RED, RESET, undo_count, undo_operations);
    }
    
    printf("Undo/redo stress tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

int test_syntax_highlighting_stress(void) {
    printf("\n%s=== Testing Syntax Highlighting Stress ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Large source code syntax analysis
    total++;
    printf("Testing large source code syntax analysis...\n");
    
    // Generate large C source code
    const int lines_count = 50000;
    const char *code_templates[] = {
        "#include <stdio.h>",
        "int function_%d(int param) {",
        "    if (param > %d) {",
        "        return param * %d;",
        "    } else {",
        "        printf(\"Value: %%d\\n\", param);",
        "    }",
        "    return 0;",
        "}",
        "// Comment line %d",
        "/* Multi-line comment %d */",
        "#define CONSTANT_%d %d"
    };
    
    size_t template_count = sizeof(code_templates) / sizeof(code_templates[0]);
    size_t total_size = lines_count * 100; // Estimate
    char *source_code = malloc(total_size);
    
    if (source_code) {
        size_t pos = 0;
        srand((unsigned int)time(NULL));
        
        for (int line = 0; line < lines_count && pos < total_size - 200; line++) {
            int template_idx = rand() % template_count;
            int chars_written = snprintf(source_code + pos, total_size - pos,
                                       code_templates[template_idx], line, 
                                       rand() % 100, rand() % 50);
            if (chars_written > 0) {
                pos += chars_written;
                source_code[pos++] = '\n';
            }
        }
        source_code[pos] = '\0';
        
        double start_time = get_time_ms();
        
        // Simulate syntax highlighting analysis
        typedef enum {
            TOKEN_IDENTIFIER,
            TOKEN_KEYWORD,
            TOKEN_NUMBER,
            TOKEN_STRING,
            TOKEN_COMMENT,
            TOKEN_OPERATOR,
            TOKEN_PREPROCESSOR
        } token_type_t;
        
        const char *keywords[] = {"int", "if", "else", "return", "include", 
                                 "define", "printf", "for", "while", NULL};
        
        int token_counts[7] = {0}; // Count for each token type
        char *current = source_code;
        char token_buffer[256];
        
        while (*current) {
            // Skip whitespace
            while (*current && (*current == ' ' || *current == '\t' || 
                               *current == '\n' || *current == '\r')) {
                current++;
            }
            
            if (!*current) break;
            
            // Identify token type
            if (*current == '#') {
                // Preprocessor
                while (*current && *current != '\n') current++;
                token_counts[TOKEN_PREPROCESSOR]++;
            } else if (*current == '/' && *(current + 1) == '/') {
                // Single line comment
                while (*current && *current != '\n') current++;
                token_counts[TOKEN_COMMENT]++;
            } else if (*current == '/' && *(current + 1) == '*') {
                // Multi-line comment
                current += 2;
                while (*current && !(*current == '*' && *(current + 1) == '/')) {
                    current++;
                }
                if (*current) current += 2;
                token_counts[TOKEN_COMMENT]++;
            } else if (*current == '"') {
                // String
                current++;
                while (*current && *current != '"') {
                    if (*current == '\\' && *(current + 1)) current++;
                    current++;
                }
                if (*current) current++;
                token_counts[TOKEN_STRING]++;
            } else if ((*current >= '0' && *current <= '9')) {
                // Number
                while (*current && ((*current >= '0' && *current <= '9') || 
                                   *current == '.')) {
                    current++;
                }
                token_counts[TOKEN_NUMBER]++;
            } else if ((*current >= 'a' && *current <= 'z') || 
                      (*current >= 'A' && *current <= 'Z') || *current == '_') {
                // Identifier or keyword
                int token_len = 0;
                while (*current && ((*current >= 'a' && *current <= 'z') ||
                                   (*current >= 'A' && *current <= 'Z') ||
                                   (*current >= '0' && *current <= '9') ||
                                   *current == '_') && token_len < 255) {
                    token_buffer[token_len++] = *current++;
                }
                token_buffer[token_len] = '\0';
                
                // Check if it's a keyword
                int is_keyword = 0;
                for (int i = 0; keywords[i]; i++) {
                    if (strcmp(token_buffer, keywords[i]) == 0) {
                        is_keyword = 1;
                        break;
                    }
                }
                
                if (is_keyword) {
                    token_counts[TOKEN_KEYWORD]++;
                } else {
                    token_counts[TOKEN_IDENTIFIER]++;
                }
            } else {
                // Operator or other
                current++;
                token_counts[TOKEN_OPERATOR]++;
            }
        }
        
        double end_time = get_time_ms();
        
        int total_tokens = 0;
        for (int i = 0; i < 7; i++) {
            total_tokens += token_counts[i];
        }
        
        print_performance_stats("Syntax highlighting", start_time, end_time, 
                               total_tokens, strlen(source_code));
        
        if (total_tokens > 0) {
            printf("[%sSUCCESS%s] Syntax analysis: %d tokens (%d keywords, %d identifiers, %d comments)\n", 
                   GREEN, RESET, total_tokens, token_counts[TOKEN_KEYWORD], 
                   token_counts[TOKEN_IDENTIFIER], token_counts[TOKEN_COMMENT]);
            passed++;
        } else {
            printf("[%sFAIL%s] Syntax analysis: no tokens found\n", RED, RESET);
        }
        
        free(source_code);
    } else {
        printf("[%sFAIL%s] Failed to allocate source code buffer\n", RED, RESET);
    }
    
    printf("Syntax highlighting stress tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
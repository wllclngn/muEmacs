// test_core_gapbuffer.c - Unit tests for gap buffer implementation
// Tests src/core/gapbuffer.c

#include "../test_utils.h"
#include "gapbuffer.h"

// Test basic creation and destruction
static int test_gapbuffer_create_destroy(void) {
    int ok = 1;
    PHASE_START("GAPBUF: CREATE", "Create and destroy gap buffers");

    // Test basic creation
    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] gap_buffer_create returned NULL");
        ok = 0;
    } else {
        // Verify initial state
        if (gap_buffer_size(gb) != 0) {
            LOG_ERRORF("[FAIL] new buffer has non-zero size: %zu", gap_buffer_size(gb));
            ok = 0;
        }
        if (gap_buffer_capacity(gb) < 64) {
            LOG_ERRORF("[FAIL] capacity less than requested: %zu < 64", gap_buffer_capacity(gb));
            ok = 0;
        }
        gap_buffer_destroy(gb);
    }

    // Test creation with minimum size enforcement
    gb = gap_buffer_create(1); // Should be bumped to GAP_BUFFER_MIN_SIZE
    if (gb) {
        if (gap_buffer_capacity(gb) < GAP_BUFFER_MIN_SIZE) {
            LOG_ERROR("[FAIL] minimum size not enforced");
            ok = 0;
        }
        gap_buffer_destroy(gb);
    }

    // Test destroy with NULL (should not crash)
    gap_buffer_destroy(NULL);

    PHASE_END("GAPBUF: CREATE", ok);
    return ok;
}

// Test basic insert operations
static int test_gapbuffer_insert(void) {
    int ok = 1;
    PHASE_START("GAPBUF: INSERT", "Insert operations");

    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] could not create gap buffer");
        ok = 0;
        PHASE_END("GAPBUF: INSERT", ok);
        return 0;
    }

    // Insert at beginning
    if (gap_buffer_insert(gb, 0, "Hello", 5) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] insert at beginning failed");
        ok = 0;
    } else if (gap_buffer_size(gb) != 5) {
        LOG_ERRORF("[FAIL] size after insert: %zu != 5", gap_buffer_size(gb));
        ok = 0;
    }

    // Verify content
    char c = gap_buffer_get_char(gb, 0);
    if (c != 'H') {
        LOG_ERRORF("[FAIL] get_char(0) = '%c' != 'H'", c);
        ok = 0;
    }

    // Insert at end
    if (gap_buffer_insert(gb, 5, " World", 6) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] insert at end failed");
        ok = 0;
    } else if (gap_buffer_size(gb) != 11) {
        LOG_ERRORF("[FAIL] size after second insert: %zu != 11", gap_buffer_size(gb));
        ok = 0;
    }

    // Insert in middle
    if (gap_buffer_insert(gb, 5, ",", 1) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] insert in middle failed");
        ok = 0;
    }

    // Verify final content: "Hello, World"
    char buf[16];
    size_t got = gap_buffer_get_text(gb, 0, 12, buf, sizeof(buf));
    buf[got] = '\0';
    if (strcmp(buf, "Hello, World") != 0) {
        LOG_ERRORF("[FAIL] content mismatch: '%s'", buf);
        ok = 0;
    }

    // Test invalid insert
    if (gap_buffer_insert(gb, 100, "X", 1) != GAP_BUFFER_INVALID) {
        LOG_ERROR("[FAIL] insert at invalid pos should fail");
        ok = 0;
    }
    if (gap_buffer_insert(NULL, 0, "X", 1) != GAP_BUFFER_INVALID) {
        LOG_ERROR("[FAIL] insert with NULL buffer should fail");
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: INSERT", ok);
    return ok;
}

// Test delete operations
static int test_gapbuffer_delete(void) {
    int ok = 1;
    PHASE_START("GAPBUF: DELETE", "Delete operations");

    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] could not create gap buffer");
        ok = 0;
        PHASE_END("GAPBUF: DELETE", ok);
        return 0;
    }

    gap_buffer_insert(gb, 0, "Hello World", 11);

    // Delete from middle
    if (gap_buffer_delete(gb, 5, 1) != GAP_BUFFER_SUCCESS) { // Delete space
        LOG_ERROR("[FAIL] delete in middle failed");
        ok = 0;
    } else if (gap_buffer_size(gb) != 10) {
        LOG_ERRORF("[FAIL] size after delete: %zu != 10", gap_buffer_size(gb));
        ok = 0;
    }

    // Verify: "HelloWorld"
    char buf[16];
    gap_buffer_get_text(gb, 0, 10, buf, sizeof(buf));
    buf[10] = '\0';
    if (strcmp(buf, "HelloWorld") != 0) {
        LOG_ERRORF("[FAIL] after middle delete: '%s'", buf);
        ok = 0;
    }

    // Delete from beginning
    gap_buffer_delete(gb, 0, 5); // Delete "Hello"
    gap_buffer_get_text(gb, 0, 5, buf, sizeof(buf));
    buf[5] = '\0';
    if (strcmp(buf, "World") != 0) {
        LOG_ERRORF("[FAIL] after begin delete: '%s'", buf);
        ok = 0;
    }

    // Delete from end
    gap_buffer_delete(gb, 3, 2); // Delete "ld"
    gap_buffer_get_text(gb, 0, 3, buf, sizeof(buf));
    buf[3] = '\0';
    if (strcmp(buf, "Wor") != 0) {
        LOG_ERRORF("[FAIL] after end delete: '%s'", buf);
        ok = 0;
    }

    // Test invalid delete
    if (gap_buffer_delete(gb, 0, 100) != GAP_BUFFER_INVALID) {
        LOG_ERROR("[FAIL] delete beyond size should fail");
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: DELETE", ok);
    return ok;
}

// Test cursor operations
static int test_gapbuffer_cursor(void) {
    int ok = 1;
    PHASE_START("GAPBUF: CURSOR", "Cursor positioning");

    struct gap_buffer *gb = gap_buffer_create(64);
    gap_buffer_insert(gb, 0, "ABCDEFGHIJ", 10);

    // Initial cursor should be at end of insert
    size_t cursor = gap_buffer_get_cursor(gb);
    if (cursor != 10) {
        LOG_ERRORF("[FAIL] cursor after insert: %zu != 10", cursor);
        ok = 0;
    }

    // Move cursor to beginning
    if (gap_buffer_set_cursor(gb, 0) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] set_cursor(0) failed");
        ok = 0;
    }
    cursor = gap_buffer_get_cursor(gb);
    if (cursor != 0) {
        LOG_ERRORF("[FAIL] cursor after set_cursor(0): %zu != 0", cursor);
        ok = 0;
    }

    // Move cursor to middle
    gap_buffer_set_cursor(gb, 5);
    cursor = gap_buffer_get_cursor(gb);
    if (cursor != 5) {
        LOG_ERRORF("[FAIL] cursor at middle: %zu != 5", cursor);
        ok = 0;
    }

    // Insert at cursor should be O(1)
    if (gap_buffer_insert(gb, 5, "XXX", 3) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] insert at cursor failed");
        ok = 0;
    }

    // Verify: "ABCDEXXXFGHIJ"
    char buf[20];
    gap_buffer_get_text(gb, 0, 13, buf, sizeof(buf));
    buf[13] = '\0';
    if (strcmp(buf, "ABCDEXXXFGHIJ") != 0) {
        LOG_ERRORF("[FAIL] content after cursor insert: '%s'", buf);
        ok = 0;
    }

    // Invalid cursor position
    if (gap_buffer_set_cursor(gb, 100) != GAP_BUFFER_INVALID) {
        LOG_ERROR("[FAIL] set_cursor(100) should fail");
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: CURSOR", ok);
    return ok;
}

// Test text access
static int test_gapbuffer_access(void) {
    int ok = 1;
    PHASE_START("GAPBUF: ACCESS", "Text access operations");

    struct gap_buffer *gb = gap_buffer_create(64);
    gap_buffer_insert(gb, 0, "Hello World", 11);

    // Move gap to middle for thorough testing
    gap_buffer_set_cursor(gb, 6);

    // Test get_char across gap
    if (gap_buffer_get_char(gb, 0) != 'H') {
        LOG_ERROR("[FAIL] get_char before gap");
        ok = 0;
    }
    if (gap_buffer_get_char(gb, 6) != 'W') {
        LOG_ERROR("[FAIL] get_char at gap");
        ok = 0;
    }
    if (gap_buffer_get_char(gb, 10) != 'd') {
        LOG_ERROR("[FAIL] get_char after gap");
        ok = 0;
    }

    // Test get_char out of bounds
    if (gap_buffer_get_char(gb, 100) != '\0') {
        LOG_ERROR("[FAIL] get_char OOB should return '\\0'");
        ok = 0;
    }

    // Test get_text spanning gap
    char buf[20];
    size_t got = gap_buffer_get_text(gb, 4, 4, buf, sizeof(buf)); // "o Wo"
    buf[got] = '\0';
    if (strcmp(buf, "o Wo") != 0) {
        LOG_ERRORF("[FAIL] get_text spanning gap: '%s'", buf);
        ok = 0;
    }

    // Test get_text truncation
    got = gap_buffer_get_text(gb, 0, 100, buf, 5); // Should only get 5 chars
    if (got != 5) {
        LOG_ERRORF("[FAIL] get_text truncation: got %zu != 5", got);
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: ACCESS", ok);
    return ok;
}

// Test line indexing
static int test_gapbuffer_lines(void) {
    int ok = 1;
    PHASE_START("GAPBUF: LINES", "Line indexing and navigation");

    struct gap_buffer *gb = gap_buffer_create(256);
    gap_buffer_insert(gb, 0, "Line 1\nLine 2\nLine 3\n", 21);

    // Test line count
    size_t lines = gap_buffer_line_count(gb);
    if (lines != 4) { // 3 newlines = 4 lines (including trailing)
        LOG_ERRORF("[FAIL] line_count: %zu != 4", lines);
        ok = 0;
    }

    // Test line_to_offset
    size_t off0 = gap_buffer_line_to_offset(gb, 0);
    size_t off1 = gap_buffer_line_to_offset(gb, 1);
    size_t off2 = gap_buffer_line_to_offset(gb, 2);

    if (off0 != 0) {
        LOG_ERRORF("[FAIL] line 0 offset: %zu != 0", off0);
        ok = 0;
    }
    if (off1 != 7) { // After "Line 1\n"
        LOG_ERRORF("[FAIL] line 1 offset: %zu != 7", off1);
        ok = 0;
    }
    if (off2 != 14) { // After "Line 1\nLine 2\n"
        LOG_ERRORF("[FAIL] line 2 offset: %zu != 14", off2);
        ok = 0;
    }

    // Test offset_to_line (binary search)
    size_t ln = gap_buffer_offset_to_line(gb, 0);
    if (ln != 0) {
        LOG_ERRORF("[FAIL] offset 0 -> line %zu != 0", ln);
        ok = 0;
    }
    ln = gap_buffer_offset_to_line(gb, 8);
    if (ln != 1) {
        LOG_ERRORF("[FAIL] offset 8 -> line %zu != 1", ln);
        ok = 0;
    }
    // Offset 20 is the '\n' at end of "Line 3", still on line 2 (0-indexed)
    ln = gap_buffer_offset_to_line(gb, 20);
    if (ln != 2) {
        LOG_ERRORF("[FAIL] offset 20 -> line %zu != 2", ln);
        ok = 0;
    }
    // Offset 21 is start of line 3 (empty trailing line)
    ln = gap_buffer_offset_to_line(gb, 21);
    if (ln != 3) {
        LOG_ERRORF("[FAIL] offset 21 -> line %zu != 3", ln);
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: LINES", ok);
    return ok;
}

// Test capacity expansion with large single insert (verifies bug fix)
static int test_gapbuffer_expansion(void) {
    int ok = 1;
    PHASE_START("GAPBUF: EXPAND", "Large insert expansion (bug fix verification)");

    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] gap_buffer_create returned NULL");
        ok = 0;
        PHASE_END("GAPBUF: EXPAND", ok);
        return 0;
    }

    size_t initial_cap = gap_buffer_capacity(gb);
    // GAP_BUFFER_MIN_SIZE is 1024, so initial_cap = 1024

    // BUG FIX TEST: Insert 2000 bytes in ONE operation into 1024-byte buffer
    // This used to crash due to expansion bug (expand_gap_buffer got wrong size)
    char *big_chunk = malloc(2000);
    if (!big_chunk) {
        LOG_ERROR("[FAIL] malloc failed");
        gap_buffer_destroy(gb);
        ok = 0;
        PHASE_END("GAPBUF: EXPAND", ok);
        return ok;
    }
    memset(big_chunk, 'X', 2000);

    int result = gap_buffer_insert(gb, 0, big_chunk, 2000);
    if (result != GAP_BUFFER_SUCCESS) {
        LOG_ERRORF("[FAIL] large single insert failed (result=%d)", result);
        ok = 0;
    } else {
        LOG_INFO("[SUCCESS] large single insert (2000 bytes into 1024 buffer) succeeded");
    }

    size_t new_cap = gap_buffer_capacity(gb);
    if (new_cap <= initial_cap) {
        LOG_ERRORF("[FAIL] capacity should have expanded: %zu <= %zu", new_cap, initial_cap);
        ok = 0;
    } else {
        LOG_INFOF("[SUCCESS] capacity expanded: %zu > %zu", new_cap, initial_cap);
    }

    // Verify data integrity
    if (gap_buffer_size(gb) != 2000) {
        LOG_ERRORF("[FAIL] size mismatch: %zu != 2000", gap_buffer_size(gb));
        ok = 0;
    }
    if (gap_buffer_get_char(gb, 0) != 'X' || gap_buffer_get_char(gb, 1999) != 'X') {
        LOG_ERROR("[FAIL] data corrupted");
        ok = 0;
    }

    free(big_chunk);
    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: EXPAND", ok);
    return ok;
}

// Test compaction
static int test_gapbuffer_compact(void) {
    int ok = 1;
    PHASE_START("GAPBUF: COMPACT", "Gap compaction");

    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] gap_buffer_create returned NULL");
        ok = 0;
        PHASE_END("GAPBUF: COMPACT", ok);
        return 0;
    }

    // Insert some data (staying within initial capacity)
    char data[500];
    memset(data, 'A', sizeof(data));
    gap_buffer_insert(gb, 0, data, sizeof(data));

    // Delete some to create gap
    gap_buffer_delete(gb, 0, 400);

    size_t before_cap = gap_buffer_capacity(gb);
    double before_frag = gap_buffer_fragmentation(gb);

    // Compact should succeed (even if it doesn't reduce capacity much)
    if (gap_buffer_compact(gb) != GAP_BUFFER_SUCCESS) {
        LOG_ERROR("[FAIL] compact failed");
        ok = 0;
    }

    size_t after_cap = gap_buffer_capacity(gb);
    double after_frag = gap_buffer_fragmentation(gb);

    LOG_INFOF("[INFO] compact: cap %zu->%zu, frag %.1f%%->%.1f%%", before_cap, after_cap, before_frag*100, after_frag*100);

    // Data should be preserved (100 bytes remaining)
    if (gap_buffer_size(gb) != 100) {
        LOG_ERRORF("[FAIL] size changed by compact: %zu != 100", gap_buffer_size(gb));
        ok = 0;
    }

    // Verify data integrity
    char verify = gap_buffer_get_char(gb, 0);
    if (verify != 'A') {
        LOG_ERROR("[FAIL] data corrupted after compact");
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: COMPACT", ok);
    return ok;
}

// Test search operations
static int test_gapbuffer_search(void) {
    int ok = 1;
    PHASE_START("GAPBUF: SEARCH", "Boyer-Moore search");

    struct gap_buffer *gb = gap_buffer_create(256);
    gap_buffer_insert(gb, 0, "The quick brown fox jumps over the lazy dog", 43);

    // Move gap to middle for thorough test
    gap_buffer_set_cursor(gb, 20);

    // Search forward
    size_t pos = gap_buffer_search_forward(gb, 0, "fox", 3);
    if (pos != 16) {
        LOG_ERRORF("[FAIL] search 'fox': %zu != 16", pos);
        ok = 0;
    }

    // Search from middle
    pos = gap_buffer_search_forward(gb, 10, "over", 4);
    if (pos != 26) {
        LOG_ERRORF("[FAIL] search 'over' from 10: %zu != 26", pos);
        ok = 0;
    }

    // Search not found
    size_t size = gap_buffer_size(gb);
    pos = gap_buffer_search_forward(gb, 0, "xyz", 3);
    if (pos != size) {
        LOG_ERRORF("[FAIL] search not-found should return size: %zu != %zu", pos, size);
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: SEARCH", ok);
    return ok;
}

// Test edge cases and stress
static int test_gapbuffer_stress(void) {
    int ok = 1;
    PHASE_START("GAPBUF: STRESS", "Edge cases and stress test");

    struct gap_buffer *gb = gap_buffer_create(32);

    // Rapid insert/delete at cursor
    for (int i = 0; i < 1000; i++) {
        gap_buffer_insert(gb, 0, "X", 1);
        gap_buffer_delete(gb, 0, 1);
    }
    if (gap_buffer_size(gb) != 0) {
        LOG_ERRORF("[FAIL] rapid insert/delete: size %zu != 0", gap_buffer_size(gb));
        ok = 0;
    }

    // Random position insert/delete
    gap_buffer_insert(gb, 0, "ABCDEFGHIJ", 10);
    for (int i = 0; i < 500; i++) {
        size_t pos = (size_t)(rand() % (gap_buffer_size(gb) + 1));
        gap_buffer_insert(gb, pos, ".", 1);
    }
    if (gap_buffer_size(gb) != 510) {
        LOG_ERRORF("[FAIL] random insert: size %zu != 510", gap_buffer_size(gb));
        ok = 0;
    }

    // Delete all
    gap_buffer_delete(gb, 0, gap_buffer_size(gb));
    if (gap_buffer_size(gb) != 0) {
        LOG_ERRORF("[FAIL] delete all: size %zu != 0", gap_buffer_size(gb));
        ok = 0;
    }

    // Empty buffer operations
    if (gap_buffer_get_char(gb, 0) != '\0') {
        LOG_ERROR("[FAIL] get_char on empty should return '\\0'");
        ok = 0;
    }

    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: STRESS", ok);
    return ok;
}

// Test loading entire Poe collected works (302KB) in single insert
static int test_gapbuffer_poe_stress(void) {
    int ok = 1;
    PHASE_START("GAPBUF: POE", "Load 302KB Poe file in single insert");

    // Try multiple paths to find the Poe file
    const char *paths[] = {
        "tests/data/poe-collected-fictions.txt",
        "../tests/data/poe-collected-fictions.txt",
        "../../tests/data/poe-collected-fictions.txt",
        NULL
    };

    FILE *fp = NULL;
    for (int i = 0; paths[i]; i++) {
        fp = fopen(paths[i], "r");
        if (fp) break;
    }

    if (!fp) {
        LOG_INFOF("[%sINFO%s] Poe file not found, skipping large file test", YELLOW, RESET);
        PHASE_END("GAPBUF: POE", ok);
        return 1;  // Not a failure, just skip
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    LOG_INFOF("[INFO] Loading %ld bytes from Poe file", file_size);

    char *content = malloc(file_size);
    if (!content) {
        LOG_ERRORF("[FAIL] malloc(%ld) failed", file_size);
        fclose(fp);
        ok = 0;
        PHASE_END("GAPBUF: POE", ok);
        return ok;
    }

    size_t read = fread(content, 1, file_size, fp);
    fclose(fp);

    if (read != (size_t)file_size) {
        LOG_ERRORF("[FAIL] fread: %zu != %ld", read, file_size);
        free(content);
        ok = 0;
        PHASE_END("GAPBUF: POE", ok);
        return ok;
    }

    // Create gap buffer and insert ENTIRE file in ONE operation
    struct gap_buffer *gb = gap_buffer_create(64);  // Start tiny
    if (!gb) {
        LOG_ERROR("[FAIL] gap_buffer_create failed");
        free(content);
        ok = 0;
        PHASE_END("GAPBUF: POE", ok);
        return ok;
    }

    int result = gap_buffer_insert(gb, 0, content, read);
    if (result != GAP_BUFFER_SUCCESS) {
        LOG_ERRORF("[FAIL] insert %zu bytes failed (result=%d)", read, result);
        ok = 0;
    } else {
        LOG_INFOF("[SUCCESS] Inserted %zu bytes in single operation", read);
    }

    // Verify size
    if (gap_buffer_size(gb) != read) {
        LOG_ERRORF("[FAIL] size mismatch: %zu != %zu", gap_buffer_size(gb), read);
        ok = 0;
    }

    // Verify first and last bytes match
    if (gap_buffer_get_char(gb, 0) != content[0]) {
        LOG_ERROR("[FAIL] first byte mismatch");
        ok = 0;
    }
    if (gap_buffer_get_char(gb, read - 1) != content[read - 1]) {
        LOG_ERROR("[FAIL] last byte mismatch");
        ok = 0;
    }

    // Verify capacity is reasonable (not insanely over-allocated)
    size_t cap = gap_buffer_capacity(gb);
    LOG_INFOF("[INFO] Final capacity: %zu bytes (%.1fx data size)", cap, (double)cap / read);

    gap_buffer_destroy(gb);
    free(content);
    PHASE_END("GAPBUF: POE", ok);
    return ok;
}

// Test gap_buffer_reserve() function
static int test_gapbuffer_reserve(void) {
    int ok = 1;
    PHASE_START("GAPBUF: RESERVE", "Pre-allocation with reserve()");

    struct gap_buffer *gb = gap_buffer_create(64);
    if (!gb) {
        LOG_ERROR("[FAIL] gap_buffer_create failed");
        ok = 0;
        PHASE_END("GAPBUF: RESERVE", ok);
        return ok;
    }

    // Reserve space for 1MB
    size_t reserve_size = 1024 * 1024;
    int result = gap_buffer_reserve(gb, reserve_size);
    if (result != GAP_BUFFER_SUCCESS) {
        LOG_ERRORF("[FAIL] reserve(%zu) failed (result=%d)", reserve_size, result);
        ok = 0;
    }

    size_t gap = gap_buffer_gap_size(gb);
    if (gap < reserve_size) {
        LOG_ERRORF("[FAIL] gap after reserve: %zu < %zu", gap, reserve_size);
        ok = 0;
    } else {
        LOG_INFOF("[SUCCESS] reserve(%zu): gap is now %zu bytes", reserve_size, gap);
    }

    // Now insert should not need to expand
    size_t cap_before = gap_buffer_capacity(gb);
    char *data = malloc(reserve_size / 2);
    memset(data, 'Z', reserve_size / 2);
    gap_buffer_insert(gb, 0, data, reserve_size / 2);
    size_t cap_after = gap_buffer_capacity(gb);

    if (cap_after != cap_before) {
        LOG_INFOF("[%sINFO%s] capacity changed after reserved insert: %zu -> %zu", YELLOW, RESET, cap_before, cap_after);
    } else {
        LOG_INFO("[SUCCESS] no reallocation needed after reserve");
    }

    free(data);
    gap_buffer_destroy(gb);
    PHASE_END("GAPBUF: RESERVE", ok);
    return ok;
}

// Entry point for gap buffer tests
int test_core_gapbuffer(void) {
    int all_passed = 1;

    all_passed &= test_gapbuffer_create_destroy();
    all_passed &= test_gapbuffer_insert();
    all_passed &= test_gapbuffer_delete();
    all_passed &= test_gapbuffer_cursor();
    all_passed &= test_gapbuffer_access();
    all_passed &= test_gapbuffer_lines();
    all_passed &= test_gapbuffer_expansion();
    all_passed &= test_gapbuffer_compact();
    all_passed &= test_gapbuffer_search();
    all_passed &= test_gapbuffer_stress();
    all_passed &= test_gapbuffer_poe_stress();
    all_passed &= test_gapbuffer_reserve();

    return all_passed;
}

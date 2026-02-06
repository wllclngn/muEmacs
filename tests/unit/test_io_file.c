// test_io_file.c - Unit tests for file I/O operations
// Tests src/io/file.c and src/io/fileio.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

// Test file name handling
static int test_file_names(void) {
    int ok = 1;
    PHASE_START("FILE: NAMES", "File name handling");

    // Create buffer with filename
    struct buffer *bp = bfind("test-file", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("FILE: NAMES", ok);
        return ok;
    }

    // Set filename
    strncpy(bp->b_fname, "/tmp/test-uemacs.txt", sizeof(bp->b_fname) - 1);
    bp->b_fname[sizeof(bp->b_fname) - 1] = '\0';

    if (strcmp(bp->b_fname, "/tmp/test-uemacs.txt") != 0) {
        LOG_ERROR("[FAIL] filename not set correctly");
        ok = 0;
    }

    // Cleanup
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);

    PHASE_END("FILE: NAMES", ok);
    return ok;
}

// Test file write
static int test_file_write(void) {
    int ok = 1;
    PHASE_START("FILE: WRITE", "File write operations");

    const char *test_path = "/tmp/uemacs-test-write.txt";

    // Remove if exists
    unlink(test_path);

    // Create buffer with content
    struct buffer *bp = bfind("test-write", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("FILE: WRITE", ok);
        return ok;
    }

    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Add content
    linstr("Test line 1");
    lnewline();
    linstr("Test line 2");

    // Set filename
    strncpy(bp->b_fname, test_path, sizeof(bp->b_fname) - 1);

    // Write file
    if (filesave(false, 1) != true) {
        LOG_ERROR("[FAIL] filesave failed");
        ok = 0;
    }

    // Verify file exists
    struct stat st;
    if (stat(test_path, &st) != 0) {
        LOG_ERROR("[FAIL] file not created");
        ok = 0;
    } else if (st.st_size == 0) {
        LOG_ERROR("[FAIL] file is empty");
        ok = 0;
    }

    // Cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);
    unlink(test_path);

    PHASE_END("FILE: WRITE", ok);
    return ok;
}

// Test file read
static int test_file_read(void) {
    int ok = 1;
    PHASE_START("FILE: READ", "File read operations");

    const char *test_path = "/tmp/uemacs-test-read.txt";

    // Create test file
    FILE *fp = fopen(test_path, "w");
    if (!fp) {
        LOG_ERROR("[FAIL] could not create test file");
        ok = 0;
        PHASE_END("FILE: READ", ok);
        return ok;
    }
    fprintf(fp, "Line one\nLine two\nLine three\n");
    fclose(fp);

    // Create buffer
    struct buffer *bp = bfind("test-read", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        unlink(test_path);
        PHASE_END("FILE: READ", ok);
        return ok;
    }

    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;

    // Set filename and read
    strncpy(bp->b_fname, test_path, sizeof(bp->b_fname) - 1);

    if (readin(test_path, true) != true) {
        LOG_ERROR("[FAIL] readin failed");
        ok = 0;
    }

    // Count lines
    int line_count = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        line_count++;
        lp = lforw(lp);
    }

    if (line_count < 3) {
        LOG_ERRORF("[FAIL] wrong line count: %d", line_count);
        ok = 0;
    }

    // Cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);
    unlink(test_path);

    PHASE_END("FILE: READ", ok);
    return ok;
}

// Test modified flag on save
static int test_file_modified(void) {
    int ok = 1;
    PHASE_START("FILE: MODIFIED", "Modified flag handling");

    const char *test_path = "/tmp/uemacs-test-mod.txt";
    unlink(test_path);

    struct buffer *bp = bfind("test-modified", true, 0);
    if (!bp) {
        LOG_ERROR("[FAIL] could not create test buffer");
        ok = 0;
        PHASE_END("FILE: MODIFIED", ok);
        return ok;
    }

    struct buffer *oldbp = curbp;
    curbp = bp;
    curwp->w_bufp = bp;
    curwp->w_dotp = lforw(bp->b_linep);
    curwp->w_doto = 0;

    // Add content (marks buffer as modified)
    linstr("Modified content");

    if (!(bp->b_flag & BFCHG)) {
        LOG_ERROR("[FAIL] buffer not marked as modified");
        ok = 0;
    }

    // Set filename and save
    strncpy(bp->b_fname, test_path, sizeof(bp->b_fname) - 1);
    (void)filesave(false, 1);

    // After save, should not be modified
    if (bp->b_flag & BFCHG) {
        LOG_ERROR("[FAIL] buffer still modified after save");
        ok = 0;
    }

    // Cleanup
    curbp = oldbp;
    curwp->w_bufp = oldbp;
    bp->b_flag &= ~BFCHG;
    (void)zotbuf(bp);
    unlink(test_path);

    PHASE_END("FILE: MODIFIED", ok);
    return ok;
}

// Entry point
int test_io_file(void) {
    int ok = 1;

    // Initialize editor (sets up curwp, curbp, etc.)
    test_init_editor("file");

    PHASE_START("FILE", "File I/O tests");

    ok &= test_file_names();
    ok &= test_file_write();
    ok &= test_file_read();
    ok &= test_file_modified();

    if (ok) {
        LOG_INFO("[SUCCESS] All file I/O tests passed.");
    }

    PHASE_END("FILE", ok);
    return ok;
}

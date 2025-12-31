// test_io_fileio.c - Unit tests for low-level file I/O operations
// Tests src/io/fileio.c

#include "../test_utils.h"
#include "../test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

// Test ffropen - open file for reading
static int test_ffropen(void) {
    int ok = 1;
    PHASE_START("FILEIO: FFROPEN", "Open file for reading");

    // Create a temp file
    char tmpfile[] = "/tmp/uemacs_test_read_XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) {
        LOG_ERROR("[FAIL] Could not create temp file");
        ok = 0; PHASE_END("FILEIO: FFROPEN", ok);
        return 0;
    }
    write(fd, "test content\n", 13);
    close(fd);

    // Test successful open
    int result = ffropen(tmpfile);
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffropen should succeed, got %d", result);
        ok = 0;
    }

    // Close it
    ffclose();

    // Test opening non-existent file
    result = ffropen("/nonexistent/file/path/test.txt");
    if (result != FIOFNF) {
        LOG_ERRORF("[FAIL] ffropen should return FIOFNF for missing file, got %d", result);
        ok = 0;
    }

    // Cleanup
    unlink(tmpfile);

    PHASE_END("FILEIO: FFROPEN", ok);
    return ok;
}

// Test ffwopen - open file for writing
static int test_ffwopen(void) {
    int ok = 1;
    PHASE_START("FILEIO: FFWOPEN", "Open file for writing");

    // Test writing to /tmp (should succeed)
    char tmpfile[] = "/tmp/uemacs_test_write_XXXXXX";
    int fd = mkstemp(tmpfile);
    close(fd);
    unlink(tmpfile);  // Remove it so we can test creation

    int result = ffwopen(tmpfile);
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffwopen should succeed, got %d", result);
        ok = 0;
    } else {
        ffclose();
        // Verify file was created
        if (access(tmpfile, F_OK) != 0) {
            LOG_ERROR("[FAIL] File was not created");
            ok = 0;
        }
        unlink(tmpfile);
    }

    // Test writing to read-only directory (should fail)
    // Note: This test may not work if running as root
    if (getuid() != 0) {
        result = ffwopen("/nonexistent_dir/test.txt");
        if (result != FIOERR) {
            LOG_ERRORF("[FAIL] ffwopen should fail for invalid path, got %d", result);
            ok = 0;
        }
    }

    PHASE_END("FILEIO: FFWOPEN", ok);
    return ok;
}

// Test ffclose - close file
static int test_ffclose(void) {
    int ok = 1;
    PHASE_START("FILEIO: FFCLOSE", "Close file");

    char tmpfile[] = "/tmp/uemacs_test_close_XXXXXX";
    int fd = mkstemp(tmpfile);
    write(fd, "test\n", 5);
    close(fd);

    // Open, then close
    if (ffropen(tmpfile) == FIOSUC) {
        int result = ffclose();
        if (result != FIOSUC) {
            LOG_ERRORF("[FAIL] ffclose should succeed, got %d", result);
            ok = 0;
        }
    } else {
        LOG_ERROR("[FAIL] ffropen failed");
        ok = 0;
    }

    // Test double close (should handle gracefully)
    int result = ffclose();
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffclose on already closed file should succeed, got %d", result);
        ok = 0;
    }

    unlink(tmpfile);

    PHASE_END("FILEIO: FFCLOSE", ok);
    return ok;
}

// Test ffputline - write line to file
static int test_ffputline(void) {
    int ok = 1;
    PHASE_START("FILEIO: FFPUTLINE", "Write line to file");

    char tmpfile[] = "/tmp/uemacs_test_putline_XXXXXX";
    int fd = mkstemp(tmpfile);
    close(fd);

    if (ffwopen(tmpfile) != FIOSUC) {
        LOG_ERROR("[FAIL] ffwopen failed");
        unlink(tmpfile);
        ok = 0; PHASE_END("FILEIO: FFPUTLINE", ok);
        return 0;
    }

    // Write a line
    char line[] = "Hello, World!";
    int result = ffputline(line, strlen(line));
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffputline should succeed, got %d", result);
        ok = 0;
    }

    // Write empty line
    result = ffputline("", 0);
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffputline with empty string should succeed, got %d", result);
        ok = 0;
    }

    // Write another line
    char line2[] = "Second line";
    result = ffputline(line2, strlen(line2));
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffputline second line should succeed, got %d", result);
        ok = 0;
    }

    ffclose();

    // Verify file contents
    FILE *fp = fopen(tmpfile, "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            buf[strcspn(buf, "\n")] = 0;
            if (strcmp(buf, "Hello, World!") != 0) {
                LOG_ERRORF("[FAIL] First line mismatch: '%s'", buf);
                ok = 0;
            }
        } else {
            LOG_ERROR("[FAIL] Could not read first line");
            ok = 0;
        }
        fclose(fp);
    }

    unlink(tmpfile);

    PHASE_END("FILEIO: FFPUTLINE", ok);
    return ok;
}

// Test ffgetline - read line from file
static int test_ffgetline(void) {
    int ok = 1;
    PHASE_START("FILEIO: FFGETLINE", "Read line from file");

    char tmpfile[] = "/tmp/uemacs_test_getline_XXXXXX";
    int fd = mkstemp(tmpfile);
    const char *content = "Line 1\nLine 2\nLine 3\n";
    write(fd, content, strlen(content));
    close(fd);

    if (ffropen(tmpfile) != FIOSUC) {
        LOG_ERROR("[FAIL] ffropen failed");
        unlink(tmpfile);
        ok = 0; PHASE_END("FILEIO: FFGETLINE", ok);
        return 0;
    }

    // Read first line
    int result = ffgetline();
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffgetline should succeed, got %d", result);
        ok = 0;
    } else {
        if (strcmp(fline, "Line 1") != 0) {
            LOG_ERRORF("[FAIL] First line mismatch: '%s'", fline);
            ok = 0;
        }
    }

    // Read second line
    result = ffgetline();
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffgetline second line should succeed, got %d", result);
        ok = 0;
    } else {
        if (strcmp(fline, "Line 2") != 0) {
            LOG_ERRORF("[FAIL] Second line mismatch: '%s'", fline);
            ok = 0;
        }
    }

    // Read third line
    result = ffgetline();
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffgetline third line should succeed, got %d", result);
        ok = 0;
    }

    // Read past EOF
    result = ffgetline();
    if (result != FIOEOF) {
        LOG_ERRORF("[FAIL] ffgetline should return FIOEOF, got %d", result);
        ok = 0;
    }

    ffclose();
    unlink(tmpfile);

    PHASE_END("FILEIO: FFGETLINE", ok);
    return ok;
}

// Test fexist - check file existence
static int test_fexist(void) {
    int ok = 1;
    PHASE_START("FILEIO: FEXIST", "Check file existence");

    char tmpfile[] = "/tmp/uemacs_test_exist_XXXXXX";
    int fd = mkstemp(tmpfile);
    close(fd);

    // Test existing file
    if (!fexist(tmpfile)) {
        LOG_ERROR("[FAIL] fexist should return true for existing file");
        ok = 0;
    }

    unlink(tmpfile);

    // Test non-existent file
    if (fexist(tmpfile)) {
        LOG_ERROR("[FAIL] fexist should return false for non-existent file");
        ok = 0;
    }

    // Test non-existent path
    if (fexist("/nonexistent/directory/file.txt")) {
        LOG_ERROR("[FAIL] fexist should return false for invalid path");
        ok = 0;
    }

    PHASE_END("FILEIO: FEXIST", ok);
    return ok;
}

// Test long lines (buffer expansion)
static int test_long_lines(void) {
    int ok = 1;
    PHASE_START("FILEIO: LONGLINES", "Long line handling");

    char tmpfile[] = "/tmp/uemacs_test_longline_XXXXXX";
    int fd = mkstemp(tmpfile);

    // Create a very long line (larger than NSTRING)
    char longline[4096];
    memset(longline, 'A', sizeof(longline) - 2);
    longline[sizeof(longline) - 2] = '\n';
    longline[sizeof(longline) - 1] = '\0';

    write(fd, longline, strlen(longline));
    close(fd);

    if (ffropen(tmpfile) != FIOSUC) {
        LOG_ERROR("[FAIL] ffropen failed");
        unlink(tmpfile);
        ok = 0; PHASE_END("FILEIO: LONGLINES", ok);
        return 0;
    }

    int result = ffgetline();
    if (result != FIOSUC) {
        LOG_ERRORF("[FAIL] ffgetline should handle long line, got %d", result);
        ok = 0;
    } else {
        if (strlen(fline) != sizeof(longline) - 2) {
            LOG_ERRORF("[FAIL] Long line length mismatch: %zu vs %zu", strlen(fline), sizeof(longline) - 2);
            ok = 0;
        }
    }

    ffclose();
    unlink(tmpfile);

    PHASE_END("FILEIO: LONGLINES", ok);
    return ok;
}

// Test empty file
static int test_empty_file(void) {
    int ok = 1;
    PHASE_START("FILEIO: EMPTYFILE", "Empty file handling");

    char tmpfile[] = "/tmp/uemacs_test_empty_XXXXXX";
    int fd = mkstemp(tmpfile);
    close(fd);

    if (ffropen(tmpfile) != FIOSUC) {
        LOG_ERROR("[FAIL] ffropen failed");
        unlink(tmpfile);
        ok = 0; PHASE_END("FILEIO: EMPTYFILE", ok);
        return 0;
    }

    int result = ffgetline();
    if (result != FIOEOF) {
        LOG_ERRORF("[FAIL] ffgetline on empty file should return FIOEOF, got %d", result);
        ok = 0;
    }

    ffclose();
    unlink(tmpfile);

    PHASE_END("FILEIO: EMPTYFILE", ok);
    return ok;
}

// Entry point
int test_io_fileio(void) {
    int ok = 1;
    PHASE_START("FILEIO", "File I/O tests");

    ok &= test_ffropen();
    ok &= test_ffwopen();
    ok &= test_ffclose();
    ok &= test_ffputline();
    ok &= test_ffgetline();
    ok &= test_fexist();
    ok &= test_long_lines();
    ok &= test_empty_file();

    if (ok) {
        LOG_INFO("[SUCCESS] All file I/O tests passed.");
    }

    PHASE_END("FILEIO", ok);
    return ok;
}

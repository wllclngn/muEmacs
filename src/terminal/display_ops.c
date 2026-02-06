// display_ops.c - Consolidated display operations for μEmacs
// Optimizes terminal I/O through batched operations and efficient string handling

/*
 * =============================================================================
 * Modern Raw ANSI Terminal Driver
 * =============================================================================
 *
 * This file uses the raw ANSI terminal driver (ansi.c) for all output.
 * Pure POSIX with direct ANSI escape sequences.
 *
 * Benefits:
 * - Zero external dependencies
 * - Inherits user's terminal theme automatically
 * - Works with any modern terminal (kitty, alacritty, xterm, etc.)
 * - Full UTF-8 and truecolor support
 *
 * All output goes through the terminal driver API:
 * - (*term.t_move)(row, col) for cursor positioning
 * - (*term.t_rev)(state) for reverse video (theme-aware)
 * - (*term.t_eeol)() for erase to end of line
 * - (*term.t_flush)() for buffer flush
 * - ttputc() / ttflush() for direct output
 *
 * =============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "string_utils.h"
#include "c23_compat.h"
#include "terminal_capability.h"
#include "terminal_ops.h"
#include "git_status.h"
#include "util/logger.h"

/* Buffer stats API for O(1) modeline updates */
extern void buffer_get_stats_fast(struct buffer *bp, int *line_count, long *byte_count, int *word_count);
extern int get_line_number_cached(struct window *wp);

// Forward declarations for internal buffer functions
static void buffer_append(const char* str);
static void buffer_append_char(char c);

// UTF-8 symbols as named constants
static const char* UTF8_BULLET = "\xe2\x97\x8f";     // ●
static const char* UTF8_DOT = "\xe2\x80\xa2";        // •
static const char* UTF8_ARROW = "\xe2\x86\x92";      // →
static const char* UTF8_CHECK = "\xe2\x9c\x93";      // ✓
static const char* UTF8_CROSS = "\xe2\x9c\x97";      // ✗

/*
 * Unicode box-drawing characters with ASCII fallbacks
 * Like montauk's Renderer.cpp - detects UTF-8 support and falls back gracefully
 */
typedef struct {
    const char* top_left;
    const char* top_right;
    const char* bottom_left;
    const char* bottom_right;
    const char* horizontal;
    const char* vertical;
    const char* cross;
    const char* t_down;
    const char* t_up;
    const char* t_right;
    const char* t_left;
} box_chars_t;

/* Unicode rounded corners (like montauk) */
static const box_chars_t box_unicode = {
    .top_left     = "╭",
    .top_right    = "╮",
    .bottom_left  = "╰",
    .bottom_right = "╯",
    .horizontal   = "─",
    .vertical     = "│",
    .cross        = "┼",
    .t_down       = "┬",
    .t_up         = "┴",
    .t_right      = "├",
    .t_left       = "┤"
};

/* ASCII fallback */
static const box_chars_t box_ascii = {
    .top_left     = "+",
    .top_right    = "+",
    .bottom_left  = "+",
    .bottom_right = "+",
    .horizontal   = "-",
    .vertical     = "|",
    .cross        = "+",
    .t_down       = "+",
    .t_up         = "+",
    .t_right      = "+",
    .t_left       = "+"
};

/* Detect if terminal supports Unicode */
static bool terminal_supports_unicode(void) {
    terminal_caps_t caps = get_terminal_capabilities();
    return caps.utf8_capable;
}

/* Get appropriate box character set */
const box_chars_t* get_box_chars(void) {
    static const box_chars_t* cached = nullptr;
    static bool initialized = false;

    if (!initialized) {
        cached = terminal_supports_unicode() ? &box_unicode : &box_ascii;
        initialized = true;
    }
    return cached;
}

/* Draw a horizontal line using box characters */
void display_hline(int width) {
    const box_chars_t* box = get_box_chars();
    for (int i = 0; i < width; i++) {
        buffer_append(box->horizontal);
    }
}

/* Draw a box border (top) */
void display_box_top(int width) {
    const box_chars_t* box = get_box_chars();
    buffer_append(box->top_left);
    display_hline(width - 2);
    buffer_append(box->top_right);
}

/* Draw a box border (bottom) */
void display_box_bottom(int width) {
    const box_chars_t* box = get_box_chars();
    buffer_append(box->bottom_left);
    display_hline(width - 2);
    buffer_append(box->bottom_right);
}

/* Draw a vertical bar */
void display_vbar(void) {
    const box_chars_t* box = get_box_chars();
    buffer_append(box->vertical);
}

// Display buffer for batched output
#define DISPLAY_BUFFER_SIZE 4096
static char display_buffer[DISPLAY_BUFFER_SIZE];
static int buffer_pos = 0;

// Flush display buffer through terminal driver - bulk write
void display_flush(void) {
    if (buffer_pos > 0) {
        ttwrite(display_buffer, buffer_pos);  /* Bulk write - no char-by-char loop */
        ttflush();
        buffer_pos = 0;
    }
}

// Add string to display buffer
static void buffer_append(const char* str) {
    int len = (int)strlen(str);
    if (buffer_pos + len >= DISPLAY_BUFFER_SIZE - 1) {
        display_flush();
    }
    if (len < DISPLAY_BUFFER_SIZE - 1) {
        memcpy(&display_buffer[buffer_pos], str, (size_t)len);
        buffer_pos += len;
    }
}

// Add character to display buffer
static void buffer_append_char(char c) {
    if (buffer_pos >= DISPLAY_BUFFER_SIZE - 1) {
        display_flush();
    }
    display_buffer[buffer_pos++] = c;
}

// Formatted append to display buffer
static void buffer_appendf(const char* fmt, ...) {
    char temp[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(temp, sizeof(temp), fmt, args);
    va_end(args);

    if (len > 0) {
        buffer_append(temp);
    }
}

// Display UTF-8 symbol
void display_utf8_symbol(const char* symbol) {
    buffer_append(symbol);
}

// Display aligned text with padding
void display_aligned_text(const char* text, int width, char pad) {
    int len = (int)strlen(text);
    buffer_append(text);

    while (len < width) {
        buffer_append_char(pad);
        len++;
    }
}

// Display status section with separator
void display_status_section(const char* text, const char* separator) {
    if (separator) {
        buffer_append(separator);
        buffer_append(" ");
    }
    buffer_append(text);
    buffer_append(" ");
}

// Display complete status line (optimized version)
// Uses terminal driver for styling - inherits terminal theme
void display_status_line(struct buffer* bp, struct window* wp, int row) {
    char temp[256];
    terminal_caps_t caps = get_terminal_capabilities();
    char gitbuf[128]; gitbuf[0] = '\0';
    git_status_request_async(nullptr);
    (void)git_status_get_cached(gitbuf, sizeof(gitbuf));

    // Flush any pending buffer content first
    display_flush();

    // Move to status line position via safe terminal wrapper
    TTmove(row, 0);

    // Enable reverse video (inherits terminal theme colors)
    TTrev(true);

    // Buffer status indicator
    buffer_append(UTF8_BULLET);
    buffer_append(" - ");
    buffer_append(bp->b_bname);

    // File type
    display_status_section("", UTF8_DOT);
    const char* ext = strrchr(bp->b_fname, '.');
    if (ext) {
        if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) {
            buffer_append("C");
        } else if (strcmp(ext, ".py") == 0) {
            buffer_append("Py");
        } else if (strcmp(ext, ".go") == 0) {
            buffer_append("Go");
        } else if (strcmp(ext, ".js") == 0) {
            buffer_append("JS");
        } else if (strcmp(ext, ".rs") == 0) {
            buffer_append("Rust");
        } else {
            buffer_append("Txt");
        }
    } else {
        buffer_append("?");
    }

    // Encoding
    display_status_section("UTF-8", UTF8_DOT);

    // Git branch/status (async/throttled; only if available)
    if (gitbuf[0] != '\0') {
        display_status_section(gitbuf, UTF8_DOT);
    }

    // File size and line count (O(1) via cached stats)
    int total_lines = 0;
    long file_size = 0;
    buffer_get_stats_fast(bp, &total_lines, &file_size, nullptr);

    if (file_size < 1024) {
        snprintf(temp, sizeof(temp), "%ldB", file_size);
    } else if (file_size < 1048576) {
        snprintf(temp, sizeof(temp), "%.2fKB", (double)file_size / 1024.0);
    } else if (file_size < 1073741824) {
        snprintf(temp, sizeof(temp), "%.2fMB", (double)file_size / 1048576.0);
    } else if (file_size < 1099511627776LL) {
        snprintf(temp, sizeof(temp), "%.2fGB", (double)file_size / 1073741824.0);
    } else {
        snprintf(temp, sizeof(temp), "%.2fTB", (double)file_size / 1099511627776.0);
    }
    display_status_section(temp, UTF8_DOT);

    // Line position (O(1) via cached line number)
    int current_line = get_line_number_cached(wp);
    snprintf(temp, sizeof(temp), "L%d/%d", current_line, total_lines);
    display_status_section(temp, UTF8_DOT);

    // Column position
    snprintf(temp, sizeof(temp), "C%d", getccol(false));
    buffer_append(temp);

    // Flush text content before padding
    display_flush();

    // Pad to full width (erase to end of line)
    TTeeol();

    // Turn off reverse video
    TTrev(false);

    // Final flush
    ttflush();
}

// Display progress bar
void display_progress_bar(int percent, int width) {
    if (width < 3) return;

    int filled = (percent * (width - 2)) / 100;
    int empty = (width - 2) - filled;

    buffer_append_char('[');
    for (int i = 0; i < filled; i++) {
        buffer_append_char('=');
    }
    for (int i = 0; i < empty; i++) {
        buffer_append_char('-');
    }
    buffer_append_char(']');
}

// Optimized screen update
void display_update_screen(void) {
    // Clear screen via bulk ANSI escape
    ttputs("\033[2J");
    // Move to home position via safe terminal wrapper
    TTmove(0, 0);
    ttflush();
}

// Initialize display optimization
void display_init_optimization(void) {
    LOG_DEBUG("Display: Initializing display optimization");
    terminal_caps_t caps = detect_terminal_capabilities();
    optimize_for_terminal(&caps);
    git_status_init();
    buffer_pos = 0;
    LOG_DEBUG("Display: Optimization initialized");
}

// Cleanup display optimization
void display_cleanup_optimization(void) {
    LOG_DEBUG("Display: Cleaning up display optimization");
    display_flush();
    cleanup_terminal_optimizations();
}

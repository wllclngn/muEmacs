// display_ops.c - Consolidated display operations for μEmacs
// Optimizes terminal I/O through batched operations and efficient string handling

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "string_utils.h"
#include "c23_compat.h"
#include "terminal_capability.h"
#include "git_status.h"

// UTF-8 symbols as named constants
static const char* UTF8_BULLET = "\xe2\x97\x8f";     // ●
static const char* UTF8_DOT = "\xe2\x80\xa2";        // •
static const char* UTF8_ARROW = "\xe2\x86\x92";      // →
static const char* UTF8_CHECK = "\xe2\x9c\x93";      // ✓
static const char* UTF8_CROSS = "\xe2\x9c\x97";      // ✗

// Display buffer for batched output
#define DISPLAY_BUFFER_SIZE 4096
static char display_buffer[DISPLAY_BUFFER_SIZE];
static int buffer_pos = 0;

// Flush display buffer via vt layer to avoid stdout interleaving
void display_flush(void) {
    if (buffer_pos > 0) {
        display_buffer[buffer_pos] = '\0';
        vtputs(display_buffer);
        TTflush();
        buffer_pos = 0;
    }
}

// Add string to display buffer
static void buffer_append(const char* str) {
    int len = strlen(str);
    if (buffer_pos + len >= DISPLAY_BUFFER_SIZE - 1) {
        display_flush();
    }
    if (len < DISPLAY_BUFFER_SIZE - 1) {
        memcpy(&display_buffer[buffer_pos], str, len);
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
    int len = strlen(text);
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
void display_status_line(struct buffer* bp, struct window* wp, int row) {
    char temp[256];
    terminal_caps_t caps = get_terminal_capabilities();
    char gitbuf[128]; gitbuf[0] = '\0';
    git_status_request_async(NULL);
    (void)git_status_get_cached(gitbuf, sizeof(gitbuf));
    
    // Move to status line position
    buffer_appendf("\x1b[%d;1H", row + 1);
    
    // Style status line; gate truecolor by capability
    if (caps.truecolor) {
        // Dimmed dark background with light foreground (e.g., bg #222222, fg #DDDDDD)
        buffer_append("\x1b[48;2;34;34;34m\x1b[38;2;221;221;221m");
    } else {
        // Fallback: reverse video
        buffer_append("\x1b[7m");
    }
    
    // Buffer status indicator
    display_utf8_symbol(UTF8_BULLET);
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
    
    // File size (efficient calculation)
    long file_size = 0;
    struct line* lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        file_size += llength(lp) + 1;
        lp = lforw(lp);
    }
    
    if (file_size < 1024) {
        snprintf(temp, sizeof(temp), "%ldB", file_size);
    } else if (file_size < 1048576) {
        snprintf(temp, sizeof(temp), "%.2fKB", file_size / 1024.0);
    } else if (file_size < 1073741824) {
        snprintf(temp, sizeof(temp), "%.2fMB", file_size / 1048576.0);
    } else if (file_size < 1099511627776LL) {
        snprintf(temp, sizeof(temp), "%.2fGB", file_size / 1073741824.0);
    } else {
        snprintf(temp, sizeof(temp), "%.2fTB", file_size / 1099511627776.0);
    }
    display_status_section(temp, UTF8_DOT);
    
    // Line position
    int current_line = 0, total_lines = 0;
    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        total_lines++;
        if (lp == wp->w_dotp) {
            current_line = total_lines;
        }
        lp = lforw(lp);
    }
    
    snprintf(temp, sizeof(temp), "L%d/%d", current_line, total_lines);
    display_status_section(temp, UTF8_DOT);
    
    // Column position
    snprintf(temp, sizeof(temp), "C%d", getccol(FALSE));
    buffer_append(temp);
    
    // Pad to full width
    int current_pos = strlen(bp->b_bname) + 50; // Approximate
    while (current_pos < caps.width) {
        buffer_append_char(' ');
        current_pos++;
    }
    
    // Reset attributes
    buffer_append("\x1b[0m");
    
    // Flush to terminal
    display_flush();
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
    terminal_caps_t caps = get_terminal_capabilities();
    
    // Use alternate screen if available
    if (caps.alt_screen) {
        buffer_append("\x1b[?1049h");
    }
    
    // Clear screen efficiently
    buffer_append("\x1b[2J\x1b[H");
    
    // Update content (delegated to existing update logic)
    // This would integrate with existing vtputc/vtputs calls
    
    display_flush();
}

// Initialize display optimization
void display_init_optimization(void) {
    terminal_caps_t caps = detect_terminal_capabilities();
    optimize_for_terminal(&caps);
    git_status_init();
    buffer_pos = 0;
}

// Cleanup display optimization
void display_cleanup_optimization(void) {
    display_flush();
    cleanup_terminal_optimizations();
}

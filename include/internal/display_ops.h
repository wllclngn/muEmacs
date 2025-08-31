// display_ops.h - Display operations interface

#ifndef DISPLAY_OPS_H
#define DISPLAY_OPS_H

#include <stdbool.h>

// Forward declarations
struct buffer;
struct window;

// Core display operations
void display_flush(void);
void display_utf8_symbol(const char* symbol);
void display_aligned_text(const char* text, int width, char pad);
void display_status_section(const char* text, const char* separator);
void display_status_line(struct buffer* bp, struct window* wp, int row);
void display_progress_bar(int percent, int width);

// Screen update optimization
void display_update_screen(void);
void display_init_optimization(void);
void display_cleanup_optimization(void);

// UTF-8 symbol constants are internal to display_ops.c

#endif // DISPLAY_OPS_H

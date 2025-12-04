/*
 * terminal_capability.h - Terminal capability detection interface
 */

#ifndef TERMINAL_CAPABILITY_H
#define TERMINAL_CAPABILITY_H

#include <stdbool.h>
#include <stdint.h>

/* Terminal capability structure */
typedef struct terminal_caps {
    bool truecolor;         /* 24-bit color support */
    bool bracketed_paste;   /* Bracketed paste mode */
    bool focus_events;      /* Focus in/out events */
    bool sixel;             /* Sixel graphics */
    bool kitty_graphics;    /* Kitty graphics protocol */
    int max_colors;         /* Color count */
    int width;              /* Columns */
    int height;             /* Rows */
    bool utf8_capable;      /* UTF-8 support */
    bool alt_screen;        /* Alternate screen buffer */
    /* Default theme colors (if detectable via OSC 10/11). */
    bool theme_colors_known; /* Default fg/bg known */
    uint8_t fg_r, fg_g, fg_b; /* Default foreground */
    uint8_t bg_r, bg_g, bg_b; /* Default background */
} terminal_caps_t;

/* Core capability functions */
terminal_caps_t detect_terminal_capabilities(void);
terminal_caps_t get_terminal_capabilities(void);

/* Terminal optimization */
void optimize_for_terminal(const terminal_caps_t* caps);
void cleanup_terminal_optimizations(void);

/* Debug/info */
void print_terminal_capabilities(void);

/* Compute highlight background derived from terminal theme.
 * kind: 1=cursorline, 2=ruler, 3=intersection.
 * If truecolor is available, returns 24-bit RGB and sets *is_truecolor=true.
 * Otherwise, returns a best-effort ANSI index mapped into RGB approximation and sets *is_truecolor=false.
 */
void compute_theme_highlight_bg(int kind, uint8_t *r, uint8_t *g, uint8_t *b, bool *is_truecolor);

#endif /* TERMINAL_CAPABILITY_H */

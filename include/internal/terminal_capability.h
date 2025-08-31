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
    bool mouse;             /* Mouse tracking */
    bool bracketed_paste;   /* Bracketed paste mode */
    bool focus_events;      /* Focus in/out events */
    bool sixel;            /* Sixel graphics */
    bool kitty_graphics;   /* Kitty graphics protocol */
    int max_colors;        /* Color count */
    int width;             /* Columns */
    int height;            /* Rows */
    bool utf8_capable;     /* UTF-8 support */
    bool alt_screen;       /* Alternate screen buffer */
} terminal_caps_t;

/* Core capability functions */
terminal_caps_t detect_terminal_capabilities(void);
terminal_caps_t get_terminal_capabilities(void);

/* Terminal optimization */
void optimize_for_terminal(const terminal_caps_t* caps);
void cleanup_terminal_optimizations(void);

/* Debug/info */
void print_terminal_capabilities(void);

#endif /* TERMINAL_CAPABILITY_H */
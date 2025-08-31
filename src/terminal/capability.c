/*
 * capability.c - Terminal capability detection for μEmacs
 * 
 * Consolidates and modernizes terminal feature detection with proper error handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <errno.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "error.h"
#include "c23_compat.h"

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

static terminal_caps_t current_caps = {0};
static bool caps_initialized = false;

/* Get terminal size using modern ioctl */
static bool get_terminal_size(int* width, int* height) {
    struct winsize ws;
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
        return true;
    }
    
    /* Fallback to environment variables */
    const char* cols = getenv("COLUMNS");
    const char* lines = getenv("LINES");
    
    if (cols && lines) {
        *width = atoi(cols);
        *height = atoi(lines);
        return (*width > 0 && *height > 0);
    }
    
    /* Default fallback */
    *width = 80;
    *height = 24;
    return false;
}

/* Send capability query and read response (with timeout) */
static bool query_terminal_capability(const char* query, char* response, size_t response_size) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }
    
    /* Send query */
    if (write(STDOUT_FILENO, query, strlen(query)) < 0) {
        return false;
    }
    
    /* Set terminal to raw mode for response */
    struct termios old_termios, raw_termios;
    if (tcgetattr(STDIN_FILENO, &old_termios) != 0) {
        return false;
    }
    
    raw_termios = old_termios;
    raw_termios.c_lflag &= ~(ECHO | ICANON);
    raw_termios.c_cc[VMIN] = 0;
    raw_termios.c_cc[VTIME] = 1;  /* 0.1 second timeout */
    
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_termios) != 0) {
        return false;
    }
    
    /* Read response */
    ssize_t bytes_read = read(STDIN_FILENO, response, response_size - 1);
    response[SAFE_MAX(0, bytes_read)] = '\0';
    
    /* Restore terminal mode */
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    
    return bytes_read > 0;
}

/* Detect terminal capabilities */
terminal_caps_t detect_terminal_capabilities(void) {
    if (caps_initialized) {
        return current_caps;
    }
    
    terminal_caps_t caps = {0};
    char response[256];
    
    /* Get terminal size */
    if (!get_terminal_size(&caps.width, &caps.height)) {
        REPORT_ERROR(ERR_TERMINAL_INIT, "Failed to get terminal size");
    }
    
    /* Environment-based detection */
    const char* term = getenv("TERM");
    const char* colorterm = getenv("COLORTERM");
    const char* term_program = getenv("TERM_PROGRAM");
    
    /* UTF-8 support */
    const char* lang = getenv("LANG");
    const char* lc_all = getenv("LC_ALL");
    caps.utf8_capable = (lang && strstr(lang, "UTF-8")) || 
                        (lc_all && strstr(lc_all, "UTF-8"));
    
    /* True color support */
    if (colorterm) {
        caps.truecolor = (strcmp(colorterm, "truecolor") == 0 || 
                         strcmp(colorterm, "24bit") == 0);
        caps.max_colors = caps.truecolor ? 16777216 : 256;
    }
    
    /* Terminal-specific features */
    if (term) {
        if (strstr(term, "kitty")) {
            caps.truecolor = true;
            caps.kitty_graphics = true;
            caps.bracketed_paste = true;
            caps.focus_events = true;
            caps.alt_screen = true;
            caps.max_colors = 16777216;
        } else if (strstr(term, "alacritty")) {
            caps.truecolor = true;
            caps.bracketed_paste = true;
            caps.alt_screen = true;
            caps.max_colors = 16777216;
        } else if (strstr(term, "xterm")) {
            caps.mouse = true;
            caps.alt_screen = true;
            caps.max_colors = caps.truecolor ? 16777216 : 256;
        }
        
        /* Common terminal features */
        if (strstr(term, "256") || caps.truecolor) {
            caps.max_colors = SAFE_MAX(caps.max_colors, 256);
        }
    }
    
    /* Terminal program specific */
    if (term_program) {
        if (strcmp(term_program, "vscode") == 0) {
            caps.truecolor = true;
            caps.max_colors = 16777216;
        }
    }
    
    /* Query terminal for specific capabilities (if interactive) */
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        /* Query for DA1 (Device Attributes) */
        if (query_terminal_capability("\x1b[c", response, sizeof(response))) {
            /* Parse response for specific features */
            if (strstr(response, "64;")) {  /* Sixel support indicator */
                caps.sixel = true;
            }
        }
        
        /* Query for color support */
        if (!caps.truecolor && query_terminal_capability("\x1b[48;2;1;2;3m\x1b[38;2;1;2;3m", response, sizeof(response))) {
            /* If terminal doesn't reject true color, assume support */
            caps.truecolor = true;
            caps.max_colors = 16777216;
        }
    }
    
    /* Fallback color detection */
    if (caps.max_colors == 0) {
        caps.max_colors = 8;  /* Conservative fallback */
    }
    
    current_caps = caps;
    caps_initialized = true;
    
    return caps;
}

/* Get current terminal capabilities */
terminal_caps_t get_terminal_capabilities(void) {
    return detect_terminal_capabilities();
}

/* Optimize editor for detected terminal */
void optimize_for_terminal(const terminal_caps_t* caps) {
    if (!caps) return;
    
    /* Set global terminal variables */
    term.t_ncol = caps->width;
    term.t_nrow = caps->height - 1;  /* Reserve line for status */
    
    /* Configure color support (purely informational here) */
    if (caps->truecolor) {
        /* Truecolor capable terminals; no explicit mode toggle needed */
    } else if (caps->max_colors >= 256) {
        /* 256-color terminals */
    }

    /* Enable bracketed paste if supported */
    if (caps->bracketed_paste) {
        vtputs("\x1b[?2004h");
    }
    
    /* Enable mouse support if available */
    if (caps->mouse) {
        vtputs("\x1b[?1000h");  /* Basic mouse reporting */
        vtputs("\x1b[?1006h");  /* SGR mouse mode */
    }
    
    /* Enable focus events if supported */
    if (caps->focus_events) {
        vtputs("\x1b[?1004h");
    }
    
    /* Switch to alternate screen if supported (use 1049 for alt buffer) */
    if (caps->alt_screen) {
        vtputs("\x1b[?1049h");
    }
}

/* Cleanup terminal optimizations */
void cleanup_terminal_optimizations(void) {
    const terminal_caps_t* caps = &current_caps;
    
    if (caps->focus_events) {
        vtputs("\x1b[?1004l");
    }
    
    if (caps->mouse) {
        vtputs("\x1b[?1006l");
        vtputs("\x1b[?1000l");
    }
    
    if (caps->bracketed_paste) {
        vtputs("\x1b[?2004l");
    }
    
    if (caps->alt_screen) {
        vtputs("\x1b[?1049l");
    }
    
    /* Reset terminal */
    vtputs("\x1b[0m");
}

/* Print capability information for debugging */
void print_terminal_capabilities(void) {
    const terminal_caps_t* caps = &current_caps;
    
    if (!caps_initialized) {
        detect_terminal_capabilities();
    }
    
    mlwrite("Terminal Capabilities:");
    mlwrite("  Size: %dx%d", caps->width, caps->height);
    mlwrite("  Colors: %d%s", caps->max_colors, caps->truecolor ? " (true color)" : "");
    mlwrite("  UTF-8: %s", caps->utf8_capable ? "yes" : "no");
    mlwrite("  Mouse: %s", caps->mouse ? "yes" : "no");
    mlwrite("  Graphics: %s%s", caps->sixel ? "Sixel " : "", caps->kitty_graphics ? "Kitty" : "none");
    mlwrite("  Features: %s%s%s",
            caps->bracketed_paste ? "paste " : "",
            caps->focus_events ? "focus " : "",
            caps->alt_screen ? "altscreen" : "");
}

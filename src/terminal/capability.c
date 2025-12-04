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

// Terminal cleanup tracking
extern void terminal_set_alt_screen(bool active);
extern void terminal_set_cursor_visible(bool visible);

/* Terminal capability structure (matches header) */
typedef struct terminal_caps {
    bool truecolor;         /* 24-bit color support */
    bool bracketed_paste;   /* Bracketed paste mode */
    bool focus_events;      /* Focus in/out events */
    bool sixel;            /* Sixel graphics */
    bool kitty_graphics;   /* Kitty graphics protocol */
    int max_colors;        /* Color count */
    int width;             /* Columns */
    int height;            /* Rows */
    bool utf8_capable;     /* UTF-8 support */
    bool alt_screen;       /* Alternate screen buffer */
    bool theme_colors_known; /* Default fg/bg known */
    uint8_t fg_r, fg_g, fg_b; /* Default foreground */
    uint8_t bg_r, bg_g, bg_b; /* Default background */
} terminal_caps_t;

static terminal_caps_t current_caps = {0};
static bool caps_initialized = false;
/* Forward declarations for local helpers */
static void detect_theme_colors(terminal_caps_t* caps);
static bool parse_osc_color(const char* s, uint8_t* r, uint8_t* g, uint8_t* b);
/* Mouse features removed; editor is keyboard-only. */

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
    raw_termios.c_cc[VTIME] = 20;  /* 2 second timeout for slower terminals */
    
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
    
    /* Always try to detect theme colors via OSC 10/11 - works on most modern terminals */
    detect_theme_colors(&caps);
    
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
    
    /* Mouse interactions are intentionally unsupported (keyboard-only editor). */
    
    /* Enable focus events if supported */
    if (caps->focus_events) {
        vtputs("\x1b[?1004h");
    }
    
    /* Switch to alternate screen if supported (use 1049 for alt buffer) */
    if (caps->alt_screen) {
        vtputs("\x1b[?1049h");
        terminal_set_alt_screen(true);
    }
    
    // Hide cursor for editor operation
    vtputs("\x1b[?25l");
    terminal_set_cursor_visible(false);
}

/* Cleanup terminal optimizations */
void cleanup_terminal_optimizations(void) {
    const terminal_caps_t* caps = &current_caps;
    
    // Show cursor
    vtputs("\x1b[?25h");
    terminal_set_cursor_visible(true);
    
    if (caps->focus_events) {
        vtputs("\x1b[?1004l");
    }
    
    /* No-op for mouse (not supported) */
    
    if (caps->bracketed_paste) {
        vtputs("\x1b[?2004l");
    }
    
    if (caps->alt_screen) {
        vtputs("\x1b[?1049l");
        terminal_set_alt_screen(false);
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
    mlwrite("  Graphics: %s%s", caps->sixel ? "Sixel " : "", caps->kitty_graphics ? "Kitty" : "none");
    mlwrite("  Features: %s%s%s",
            caps->bracketed_paste ? "paste " : "",
            caps->focus_events ? "focus " : "",
            caps->alt_screen ? "altscreen" : "");
    if (caps->theme_colors_known) {
        mlwrite("  Theme FG: #%02x%02x%02x BG: #%02x%02x%02x",
                caps->fg_r, caps->fg_g, caps->fg_b,
                caps->bg_r, caps->bg_g, caps->bg_b);
    }
}

/* Simple sRGB luminance */
static inline float luminance(uint8_t r, uint8_t g, uint8_t b) {
    return (0.2126f * (r/255.0f)) + (0.7152f * (g/255.0f)) + (0.0722f * (b/255.0f));
}

/* Blend toward a target color by alpha [0..1] */
static inline void blend_rgb(uint8_t br, uint8_t bg, uint8_t bb,
                             uint8_t tr, uint8_t tg, uint8_t tb,
                             float a, uint8_t* outr, uint8_t* outg, uint8_t* outb)
{
    float r = (1.0f - a) * br + a * tr;
    float g = (1.0f - a) * bg + a * tg;
    float b = (1.0f - a) * bb + a * tb;
    *outr = (uint8_t)(r + 0.5f);
    *outg = (uint8_t)(g + 0.5f);
    *outb = (uint8_t)(b + 0.5f);
}

/* Public: compute a theme-derived highlight background */
void compute_theme_highlight_bg(int kind, uint8_t *r, uint8_t *g, uint8_t *b, bool *is_truecolor)
{
    if (is_truecolor) *is_truecolor = false;
    terminal_caps_t caps = get_terminal_capabilities();
    if (caps.theme_colors_known && caps.truecolor) {
        extern int highlight_intensity_pct;
        extern int ruler_intensity_pct;
        extern int intersection_intensity_pct;
        extern int highlight_strategy; // 0=blend,1=lighten,2=darken

        int pct = highlight_intensity_pct;
        if (kind == 2) pct = ruler_intensity_pct;
        else if (kind == 3) pct = intersection_intensity_pct;
        if (pct < 0) {
            pct = 0;
        }
        if (pct > 50) {
            pct = 50;
        }
        float a = pct / 100.0f;

        uint8_t rr = caps.bg_r, gg = caps.bg_g, bb = caps.bg_b;
        if (highlight_strategy == 0) {
            // Blend toward foreground
            blend_rgb(caps.bg_r, caps.bg_g, caps.bg_b, caps.fg_r, caps.fg_g, caps.fg_b, a, &rr, &gg, &bb);
        } else if (highlight_strategy == 1) {
            // Lighten bg toward white
            blend_rgb(caps.bg_r, caps.bg_g, caps.bg_b, 255, 255, 255, a, &rr, &gg, &bb);
        } else {
            // Darken bg toward black
            blend_rgb(caps.bg_r, caps.bg_g, caps.bg_b, 0, 0, 0, a, &rr, &gg, &bb);
        }

        if (r) {
            *r = rr;
        }
        if (g) {
            *g = gg;
        }
        if (b) {
            *b = bb;
        }
        if (is_truecolor) *is_truecolor = true;
        return;
    }
    // Fallback: theme detection failed - use pure black background with subtle highlight
    // RGB(30,30,30) is subtle but visible on black backgrounds
    if (r) *r = 30;
    if (g) *g = 30;
    if (b) *b = 30;
    if (is_truecolor) *is_truecolor = true;
}

/* Runtime commands to enable/disable mouse reporting */
/* Mouse commands removed: editor is keyboard-only */
/* Parse OSC color response: supports "rgb:rrrr/gggg/bbbb" and "#RRGGBB". */
static bool parse_osc_color(const char* s, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (!s || !r || !g || !b) return false;
    const char* p = s;
    // Skip any leading OSC framing
    while (*p && *p != 'r' && *p != '#') p++;
    if (*p == '#') {
        unsigned rv, gv, bv;
        if (sscanf(p, "#%02x%02x%02x", &rv, &gv, &bv) == 3) {
            *r = (uint8_t)rv; *g = (uint8_t)gv; *b = (uint8_t)bv; return true;
        }
    } else if (strncmp(p, "rgb:", 4) == 0) {
        p += 4;
        unsigned rv=0, gv=0, bv=0;
        unsigned rr=0, gg=0, bb=0;
        if (sscanf(p, "%x/%x/%x", &rr, &gg, &bb) == 3) {
            // Values could be 16-bit (rrrr). Normalize to 8-bit.
            rv = (rr > 0xFF) ? (rr >> 8) : rr;
            gv = (gg > 0xFF) ? (gg >> 8) : gg;
            bv = (bb > 0xFF) ? (bb >> 8) : bb;
            *r = (uint8_t)rv; *g = (uint8_t)gv; *b = (uint8_t)bv; return true;
        }
    }
    return false;
}

/* Query default fg/bg via OSC 10/11; many modern terminals (kitty, xterm, alacritty) reply. */
static void detect_theme_colors(terminal_caps_t* caps)
{
    if (!caps) return;
    char resp[256] = {0};
    bool fg_ok = false, bg_ok = false;
    // Default foreground
    if (query_terminal_capability("\x1b]10;?\x07", resp, sizeof(resp))) {
        uint8_t r=0,g=0,b=0;
        if (parse_osc_color(resp, &r, &g, &b)) {
            caps->fg_r = r; caps->fg_g = g; caps->fg_b = b; fg_ok = true;
            fprintf(stderr, "[DEBUG] Detected FG: #%02x%02x%02x\n", r, g, b);
        }
    }
    // Default background
    memset(resp, 0, sizeof(resp));
    if (query_terminal_capability("\x1b]11;?\x07", resp, sizeof(resp))) {
        uint8_t r=0,g=0,b=0;
        if (parse_osc_color(resp, &r, &g, &b)) {
            caps->bg_r = r; caps->bg_g = g; caps->bg_b = b; bg_ok = true;
            fprintf(stderr, "[DEBUG] Detected BG: #%02x%02x%02x\n", r, g, b);
        }
    }
    caps->theme_colors_known = (fg_ok && bg_ok);
    fprintf(stderr, "[DEBUG] Theme colors known: %s\n", caps->theme_colors_known ? "YES" : "NO");
}

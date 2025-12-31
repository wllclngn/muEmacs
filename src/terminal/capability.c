/*
 * capability.c - Terminal capability detection for μEmacs
 *
 * Environment-based detection only - NO terminal queries.
 *
 * Philosophy (from montauk):
 * - OSC queries are unreliable and can leave garbage on screen
 * - DA1 and other queries can timeout or pollute input
 * - Use environment variables: COLORTERM, TERM, COLORFGBG, TERM_PROGRAM
 * - Provide safe defaults for unknown terminals
 * - Allow user override via MUEMACS_* environment variables
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <strings.h>  /* For strcasecmp */
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "error.h"
#include "c23_compat.h"
#include "util/logger.h"

/* Terminal cleanup tracking */
extern void terminal_set_alt_screen(bool active);
extern void terminal_set_cursor_visible(bool visible);

/* Terminal capability structure (matches header) */
typedef struct terminal_caps {
    bool truecolor;         /* 24-bit color support */
    bool bracketed_paste;   /* Bracketed paste mode */
    bool focus_events;      /* Focus in/out events */
    bool sixel;            /* Sixel graphics */
    bool kitty_graphics;   /* Kitty graphics protocol */
    bool kitty_keyboard;   /* Kitty keyboard protocol (CSI u) */
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
        char *endptr;
        errno = 0;
        long w = strtol(cols, &endptr, 10);
        if (errno != 0 || endptr == cols) w = 0;
        errno = 0;
        long h = strtol(lines, &endptr, 10);
        if (errno != 0 || endptr == lines) h = 0;
        *width = (int)w;
        *height = (int)h;
        return (*width > 0 && *height > 0);
    }
    
    /* Default fallback */
    *width = 80;
    *height = 24;
    return false;
}

/*
 * NOTE: Terminal queries removed.
 *
 * OSC/DA1 queries were causing garbage on screen because:
 * - Response timing is unpredictable
 * - Some terminals don't respond at all
 * - Responses can arrive after we've moved on, polluting display
 *
 * All capability detection now uses environment variables only.
 */

/* Detect terminal capabilities */
terminal_caps_t detect_terminal_capabilities(void) {
    if (caps_initialized) {
        return current_caps;
    }

    terminal_caps_t caps = {0};
    
    /* Get terminal size */
    if (!get_terminal_size(&caps.width, &caps.height)) {
        REPORT_ERROR(ERR_TERMINAL_INIT, "FAILED TO GET TERMINAL SIZE");
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
            caps.kitty_keyboard = true;  /* Kitty keyboard protocol (CSI u) */
            caps.bracketed_paste = true;
            caps.focus_events = true;
            caps.alt_screen = true;
            caps.max_colors = 16777216;
        } else if (strstr(term, "foot")) {
            /* foot terminal - supports Kitty keyboard protocol */
            caps.truecolor = true;
            caps.kitty_keyboard = true;
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
        } else if (strcmp(term_program, "iTerm.app") == 0) {
            caps.truecolor = true;
            caps.max_colors = 16777216;
            caps.bracketed_paste = true;
        } else if (strcmp(term_program, "WezTerm") == 0) {
            caps.truecolor = true;
            caps.max_colors = 16777216;
            caps.sixel = true;
            caps.bracketed_paste = true;
            caps.kitty_keyboard = true;  /* WezTerm supports Kitty keyboard protocol */
        } else if (strcmp(term_program, "Ghostty") == 0) {
            caps.truecolor = true;
            caps.max_colors = 16777216;
            caps.bracketed_paste = true;
            caps.kitty_keyboard = true;  /* Ghostty supports Kitty keyboard protocol */
        }
    }

    /* User override for Kitty keyboard protocol */
    const char* muemacs_kitty_kbd = getenv("MUEMACS_KITTY_KEYBOARD");
    if (muemacs_kitty_kbd) {
        if (strcmp(muemacs_kitty_kbd, "1") == 0 || strcasecmp(muemacs_kitty_kbd, "true") == 0) {
            caps.kitty_keyboard = true;
        } else if (strcmp(muemacs_kitty_kbd, "0") == 0 || strcasecmp(muemacs_kitty_kbd, "false") == 0) {
            caps.kitty_keyboard = false;
        }
    }

    /* Sixel detection via TERM (safer than queries) */
    if (term) {
        if (strstr(term, "sixel") || strstr(term, "mlterm")) {
            caps.sixel = true;
        }
    }

    /* User override via environment */
    const char* muemacs_truecolor = getenv("MUEMACS_TRUECOLOR");
    if (muemacs_truecolor) {
        if (strcmp(muemacs_truecolor, "1") == 0 || strcasecmp(muemacs_truecolor, "true") == 0) {
            caps.truecolor = true;
            caps.max_colors = 16777216;
        } else if (strcmp(muemacs_truecolor, "0") == 0 || strcasecmp(muemacs_truecolor, "false") == 0) {
            caps.truecolor = false;
        }
    }

    /* Detect theme colors from environment (no OSC queries) */
    detect_theme_colors(&caps);
    
    /* Fallback color detection */
    if (caps.max_colors == 0) {
        caps.max_colors = 8;  /* Conservative fallback */
    }
    
    current_caps = caps;
    caps_initialized = true;

    LOG_INFOF("Terminal: %dx%d, Truecolor=%d, UTF8=%d, Colors=%d",
              caps.width, caps.height, caps.truecolor, caps.utf8_capable, caps.max_colors);

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

    /* Enable Kitty keyboard protocol if supported
     * Uses flag 1 (disambiguation only) for minimal disruption.
     * This fixes ESC key ambiguity and provides clean modifier reporting.
     * See: https://sw.kovidgoyal.net/kitty/keyboard-protocol/
     */
    if (caps->kitty_keyboard) {
        vtputs("\x1b[>1u");  /* Push enhancement level 1 (disambiguation) */
        LOG_INFO("Terminal: Kitty keyboard protocol enabled (level 1)");
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

    /* Disable Kitty keyboard protocol */
    if (caps->kitty_keyboard) {
        vtputs("\x1b[<u");  /* Pop enhancement stack */
        LOG_INFO("Terminal: Kitty keyboard protocol disabled");
    }

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
    
    mlwrite("TERMINAL CAPABILITIES:");
    mlwrite("  SIZE: %dX%d", caps->width, caps->height);
    mlwrite("  COLORS: %d%s", caps->max_colors, caps->truecolor ? " [TRUE COLOR]" : "");
    mlwrite("  UTF-8: %s", caps->utf8_capable ? "YES" : "NO");
    mlwrite("  GRAPHICS: %s%s", caps->sixel ? "SIXEL " : "", caps->kitty_graphics ? "KITTY" : "NONE");
    mlwrite("  FEATURES: %s%s%s%s",
            caps->bracketed_paste ? "paste " : "",
            caps->focus_events ? "focus " : "",
            caps->kitty_keyboard ? "kitty-kbd " : "",
            caps->alt_screen ? "altscreen" : "");
    if (caps->theme_colors_known) {
        mlwrite("  THEME FG: #%02x%02x%02x BG: #%02x%02x%02x",
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
    LOG_DEBUGF("TUI: compute_theme_highlight_bg(kind=%d) theme_known=%d truecolor=%d",
               kind, caps.theme_colors_known, caps.truecolor);
    LOG_DEBUGF("TUI: Terminal colors: fg=(%d,%d,%d) bg=(%d,%d,%d)",
               caps.fg_r, caps.fg_g, caps.fg_b, caps.bg_r, caps.bg_g, caps.bg_b);
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
        LOG_DEBUGF("TUI: compute_theme_highlight_bg() -> computed RGB(%d,%d,%d)", rr, gg, bb);
        if (is_truecolor) *is_truecolor = true;
        return;
    }
    // Fallback: theme detection failed - use a VISIBLE gray
    // RGB(70,70,70) is clearly visible on black backgrounds
    LOG_DEBUG("TUI: compute_theme_highlight_bg() -> fallback RGB(70,70,70)");
    if (r) *r = 70;
    if (g) *g = 70;
    if (b) *b = 70;
    if (is_truecolor) *is_truecolor = true;
}

/*
 * Environment-based theme color detection.
 *
 * Sources (in priority order):
 * 1. MUEMACS_THEME - explicit "light" or "dark"
 * 2. MUEMACS_BG_COLOR - explicit hex color "#RRGGBB"
 * 3. COLORFGBG - rxvt-style "fg;bg" format
 * 4. Default: assume dark theme (most common)
 */
static bool parse_hex_color(const char* s, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (!s || !r || !g || !b) return false;

    /* Skip leading '#' if present */
    if (*s == '#') s++;

    unsigned rv, gv, bv;
    if (sscanf(s, "%02x%02x%02x", &rv, &gv, &bv) == 3) {
        *r = (uint8_t)rv;
        *g = (uint8_t)gv;
        *b = (uint8_t)bv;
        return true;
    }
    return false;
}

static void detect_theme_colors(terminal_caps_t* caps)
{
    if (!caps) return;

    /* Default: assume dark theme (white on black) - most modern terminals */
    caps->fg_r = 255; caps->fg_g = 255; caps->fg_b = 255;  /* White foreground */
    caps->bg_r = 0;   caps->bg_g = 0;   caps->bg_b = 0;    /* Black background */

    /* 1. Check for explicit MUEMACS_THEME */
    const char* theme = getenv("MUEMACS_THEME");
    if (theme) {
        if (strcasecmp(theme, "light") == 0) {
            caps->fg_r = 0;   caps->fg_g = 0;   caps->fg_b = 0;    /* Black foreground */
            caps->bg_r = 255; caps->bg_g = 255; caps->bg_b = 255;  /* White background */
        }
        /* "dark" is already the default */
    }

    /* 2. Check for explicit MUEMACS_BG_COLOR (overrides theme) */
    const char* bg_color = getenv("MUEMACS_BG_COLOR");
    if (bg_color) {
        uint8_t r, g, b;
        if (parse_hex_color(bg_color, &r, &g, &b)) {
            caps->bg_r = r; caps->bg_g = g; caps->bg_b = b;
            /* Infer foreground from background luminance */
            float lum = (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
            if (lum > 0.5f) {
                /* Light background -> dark foreground */
                caps->fg_r = 0; caps->fg_g = 0; caps->fg_b = 0;
            } else {
                /* Dark background -> light foreground */
                caps->fg_r = 255; caps->fg_g = 255; caps->fg_b = 255;
            }
        }
    }

    /* 3. Check COLORFGBG (rxvt, urxvt style) */
    const char* colorfgbg = getenv("COLORFGBG");
    if (colorfgbg && !theme && !bg_color) {
        /* Format: "fg;bg" or "fg;bg;..." */
        const char* semi = strchr(colorfgbg, ';');
        if (semi) {
            char *endptr;
            errno = 0;
            long bg_val = strtol(semi + 1, &endptr, 10);
            int bg_idx = (errno == 0 && endptr != semi + 1) ? (int)bg_val : -1;
            /* ANSI colors: 0=black, 7=white, 15=bright white */
            if (bg_idx == 7 || bg_idx == 15) {
                /* Light background */
                caps->fg_r = 0;   caps->fg_g = 0;   caps->fg_b = 0;
                caps->bg_r = 255; caps->bg_g = 255; caps->bg_b = 255;
            }
            /* Dark backgrounds (0, 8, etc.) are already the default */
        }
    }

    /* Mark as known - we always have valid colors now */
    caps->theme_colors_known = true;

    LOG_INFOF("Theme detection (env-based): fg=#%02x%02x%02x bg=#%02x%02x%02x",
              caps->fg_r, caps->fg_g, caps->fg_b,
              caps->bg_r, caps->bg_g, caps->bg_b);
}

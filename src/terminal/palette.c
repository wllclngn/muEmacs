/*
 * palette.c - Terminal color palette system implementation
 *
 * Provides semantic color indices that map to the 256-color terminal palette.
 * This allows muEmacs to inherit colors from the user's terminal theme.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "terminal/palette.h"

/*
 * Global palette instance with default values.
 *
 * Default indices use -2 (truecolor) with RGB values that work well on
 * dark backgrounds. These can be overridden via settings.toml.
 */
ui_palette_t g_palette = {
    /* Background colors - use truecolor by default */
    .modeline_bg = -2,
    .highlight_bg = -2,
    .ruler_bg = -2,
    .selection_bg = -2,
    .search_bg = -2,
    .message_bg = -2,

    /* Foreground colors */
    .modeline_fg = -1,      /* Terminal default foreground */
    .accent = 14,           /* Bright cyan (terminal-defined) */
    .search_fg = -1,        /* Inherit from text by default */

    /* Truecolor RGB defaults */
    .modeline_rgb = {0x2E, 0x2E, 0x2E},     /* #2E2E2E - dark gray */
    .modeline_fg_rgb = {0xFF, 0xFF, 0xFF},  /* #FFFFFF - white foreground */
    .highlight_rgb = {0x38, 0x38, 0x38},    /* #383838 - cursor line */
    .ruler_rgb = {0x2E, 0x2E, 0x2E},        /* #2E2E2E - subtle ruler */
    .selection_rgb = {0x4A, 0x4A, 0x6A},    /* #4A4A6A - purple-tinted selection */
    .search_bg_rgb = {0x5F, 0x5F, 0x00},    /* #5F5F00 - yellow-green search */
    .search_fg_rgb = {0x00, 0x00, 0x00},    /* #000000 - black (if enabled) */
    .message_bg_rgb = {0x2E, 0x2E, 0x2E},   /* #2E2E2E - same as modeline */

    /* Vim mode indicator colors (foreground) */
    .mode_normal_rgb = {0x6A, 0x64, 0xFF},  /* #6A64FF - purple/blue */
    .mode_insert_rgb = {0x00, 0xCD, 0xF0},  /* #00CDF0 - cyan */
    .mode_visual_rgb = {0xFF, 0xA5, 0x61},  /* #FFA561 - orange */
    .mode_replace_rgb = {0xEA, 0x17, 0x17}, /* #EA1717 - red */
    .mode_evil_rgb = {0xEA, 0x17, 0x17},    /* #EA1717 - red (EVIL splash) */

    /* Message type colors (foreground) */
    .accent_rgb = {0x03, 0xFE, 0xAF},       /* #03FEAF - mint green */
    .error_rgb = {0xEA, 0x17, 0x17},        /* #EA1717 - red */
    .warning_rgb = {0xFF, 0xA5, 0x61},      /* #FFA561 - orange */
    .success_rgb = {0x00, 0xFF, 0xA6},      /* #00FFA6 - bright green */

    /* Flags */
    .search_fg_set = false
};

/* Cached color capability */
static color_capability_t s_color_cap = (color_capability_t)-1;

/*
 * Detect terminal color capability from environment.
 */
color_capability_t palette_detect_capability(void) {
    if (s_color_cap != (color_capability_t)-1) {
        return s_color_cap;
    }

    /* Check for truecolor first */
    const char* colorterm = getenv("COLORTERM");
    if (colorterm) {
        /* Case-insensitive check for "truecolor" or "24bit" */
        char lower[64];
        size_t i;
        for (i = 0; i < sizeof(lower) - 1 && colorterm[i]; i++) {
            lower[i] = (char)tolower((unsigned char)colorterm[i]);
        }
        lower[i] = '\0';

        if (strstr(lower, "truecolor") || strstr(lower, "24bit")) {
            s_color_cap = COLOR_CAP_TRUECOLOR;
            return s_color_cap;
        }
    }

    /* Check TERM for 256-color support */
    const char* term = getenv("TERM");
    if (term) {
        if (strstr(term, "256color") || strstr(term, "256-color")) {
            s_color_cap = COLOR_CAP_256;
            return s_color_cap;
        }

        /* Common terminals that support at least 16 colors */
        if (strstr(term, "xterm") || strstr(term, "screen") ||
            strstr(term, "tmux") || strstr(term, "rxvt") ||
            strstr(term, "linux") || strstr(term, "vt100")) {
            s_color_cap = COLOR_CAP_16;
            return s_color_cap;
        }
    }

    /* Default: assume no color support */
    s_color_cap = COLOR_CAP_NONE;
    return s_color_cap;
}

/*
 * Check if terminal supports truecolor.
 */
bool palette_truecolor_capable(void) {
    return palette_detect_capability() >= COLOR_CAP_TRUECOLOR;
}

/*
 * Initialize palette from environment variables.
 * Called after settings.c loads TOML config.
 */
void palette_init(void) {
    const char* env;

    /* Environment variables override TOML settings */
    if ((env = getenv("MUEMACS_MODELINE_BG")) != nullptr) {
        char *endptr;
        errno = 0;
        long val = strtol(env, &endptr, 10);
        if (errno == 0 && endptr != env && *endptr == '\0')
            g_palette.modeline_bg = (int)val;
    }
    if ((env = getenv("MUEMACS_HIGHLIGHT_BG")) != nullptr) {
        char *endptr;
        errno = 0;
        long val = strtol(env, &endptr, 10);
        if (errno == 0 && endptr != env && *endptr == '\0')
            g_palette.highlight_bg = (int)val;
    }
    if ((env = getenv("MUEMACS_RULER_BG")) != nullptr) {
        char *endptr;
        errno = 0;
        long val = strtol(env, &endptr, 10);
        if (errno == 0 && endptr != env && *endptr == '\0')
            g_palette.ruler_bg = (int)val;
    }
    if ((env = getenv("MUEMACS_ACCENT")) != nullptr) {
        char *endptr;
        errno = 0;
        long val = strtol(env, &endptr, 10);
        if (errno == 0 && endptr != env && *endptr == '\0')
            g_palette.accent = (int)val;
    }

    /* Truecolor hex overrides (only if terminal supports it) */
    int r, g, b;
    if ((env = getenv("MUEMACS_MODELINE_HEX")) != nullptr && palette_truecolor_capable()) {
        if (palette_parse_hex(env, &r, &g, &b)) {
            g_palette.modeline_bg = -2;
            g_palette.modeline_rgb[0] = (unsigned char)r;
            g_palette.modeline_rgb[1] = (unsigned char)g;
            g_palette.modeline_rgb[2] = (unsigned char)b;
        }
    }
    if ((env = getenv("MUEMACS_HIGHLIGHT_HEX")) != nullptr && palette_truecolor_capable()) {
        if (palette_parse_hex(env, &r, &g, &b)) {
            g_palette.highlight_bg = -2;
            g_palette.highlight_rgb[0] = (unsigned char)r;
            g_palette.highlight_rgb[1] = (unsigned char)g;
            g_palette.highlight_rgb[2] = (unsigned char)b;
        }
    }
    if ((env = getenv("MUEMACS_RULER_HEX")) != nullptr && palette_truecolor_capable()) {
        if (palette_parse_hex(env, &r, &g, &b)) {
            g_palette.ruler_bg = -2;
            g_palette.ruler_rgb[0] = (unsigned char)r;
            g_palette.ruler_rgb[1] = (unsigned char)g;
            g_palette.ruler_rgb[2] = (unsigned char)b;
        }
    }
}

/*
 * Generate SGR foreground escape sequence for palette index.
 */
const char* sgr_palette_fg(int idx) {
    static _Thread_local char buf[32];  /* Thread-safe static buffer */

    if (idx == -1) {
        return "";  /* No color change - use terminal default */
    }

    if (idx == -2) {
        /* Truecolor not supported for foreground in this simple API */
        return "";
    }

    /* Clamp to terminal capability */
    idx = palette_clamp(idx);
    if (idx < 0) {
        return "";
    }

    /* Always use 256-color escape for consistent palette mapping.
     * This ensures terminal theme colors are used correctly. */
    snprintf(buf, sizeof(buf), "\033[38;5;%dm", idx);

    return buf;
}

/*
 * Generate SGR background escape sequence for palette index.
 */
const char* sgr_palette_bg(int idx) {
    static _Thread_local char buf[32];  /* Thread-safe static buffer */

    if (idx == -1) {
        return "";  /* No color change - use terminal default */
    }

    if (idx == -2) {
        /* Handled by caller using sgr_truecolor_bg() */
        return "";
    }

    /* Clamp to terminal capability */
    idx = palette_clamp(idx);
    if (idx < 0) {
        return "";
    }

    /* Always use 256-color escape for consistent palette mapping.
     * Legacy SGR 40-47/100-107 can differ from 48;5;N on modern terminals.
     * This ensures the user's terminal theme colors are used correctly. */
    snprintf(buf, sizeof(buf), "\033[48;5;%dm", idx);

    return buf;
}

/*
 * Generate SGR truecolor background escape sequence.
 */
const char* sgr_truecolor_bg(int r, int g, int b) {
    static _Thread_local char buf[32];  /* Thread-safe static buffer */

    /* Clamp values */
    if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } else if (b > 255) { b = 255; }

    snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm", r, g, b);
    return buf;
}

/*
 * Clamp palette index to terminal capability.
 */
int palette_clamp(int idx) {
    color_capability_t cap = palette_detect_capability();

    if (idx < 0) {
        return idx;  /* Special values pass through */
    }

    switch (cap) {
        case COLOR_CAP_NONE:
            return -1;  /* No colors */

        case COLOR_CAP_16:
            if (idx > 15) {
                /* Map 256-color grayscale to basic colors */
                if (idx >= 232) {
                    /* Grayscale ramp: map to black (0) or white (7) */
                    int gray_level = idx - 232;  /* 0-23 */
                    return (gray_level < 12) ? 0 : 7;
                }
                /* Color cube: crude mapping to basic 8 */
                return idx % 8;
            }
            return idx;

        case COLOR_CAP_256:
        case COLOR_CAP_TRUECOLOR:
        default:
            return idx;  /* Full palette available */
    }
}

/*
 * Parse hex color string to RGB.
 */
bool palette_parse_hex(const char* hex, int* r, int* g, int* b) {
    if (!hex || !r || !g || !b) {
        return false;
    }

    /* Skip leading # if present */
    if (hex[0] == '#') {
        hex++;
    }

    /* Must be exactly 6 hex digits */
    if (strlen(hex) != 6) {
        return false;
    }

    /* Parse hex */
    unsigned int rgb;
    if (sscanf(hex, "%6x", &rgb) != 1) {
        return false;
    }

    *r = (rgb >> 16) & 0xFF;
    *g = (rgb >> 8) & 0xFF;
    *b = rgb & 0xFF;

    return true;
}

/*
 * Generate SGR escape sequence for truecolor foreground.
 */
const char* sgr_truecolor_fg(int r, int g, int b) {
    static _Thread_local char buf[32];

    /* Clamp values */
    if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } else if (b > 255) { b = 255; }

    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", r, g, b);
    return buf;
}

/*
 * Generate SGR for vim mode indicator color.
 * Returns foreground color for the specified mode.
 */
const char* sgr_mode_color(int mode) {
    static _Thread_local char buf[32];
    const unsigned char *rgb;

    switch (mode) {
        case 0:  /* NORMAL */
            rgb = g_palette.mode_normal_rgb;
            break;
        case 1:  /* INSERT */
            rgb = g_palette.mode_insert_rgb;
            break;
        case 2:  /* VISUAL */
        case 3:  /* V-LINE */
        case 4:  /* V-BLOCK */
            rgb = g_palette.mode_visual_rgb;
            break;
        case 5:  /* REPLACE */
            rgb = g_palette.mode_replace_rgb;
            break;
        default:
            rgb = g_palette.mode_normal_rgb;
            break;
    }

    /* Always use truecolor */
    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
    return buf;
}

/*
 * Generate SGR for selection background.
 */
const char* sgr_selection_bg(void) {
    static _Thread_local char buf[32];
    /* Always use truecolor */
    snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm",
             g_palette.selection_rgb[0],
             g_palette.selection_rgb[1],
             g_palette.selection_rgb[2]);
    return buf;
}

/*
 * Generate SGR for search match background.
 */
const char* sgr_search_bg(void) {
    static _Thread_local char buf[32];
    /* Always use truecolor */
    snprintf(buf, sizeof(buf), "\033[48;2;%d;%d;%dm",
             g_palette.search_bg_rgb[0],
             g_palette.search_bg_rgb[1],
             g_palette.search_bg_rgb[2]);
    return buf;
}

/*
 * Generate SGR for search match foreground (if set).
 */
const char* sgr_search_fg(void) {
    static _Thread_local char buf[32];

    if (!g_palette.search_fg_set) {
        return "";
    }

    /* Always use truecolor */
    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm",
             g_palette.search_fg_rgb[0],
             g_palette.search_fg_rgb[1],
             g_palette.search_fg_rgb[2]);
    return buf;
}

/*
 * Generate SGR for message type color.
 * type: 0=error, 1=warning, 2=success
 */
const char* sgr_message_color(int type) {
    static _Thread_local char buf[32];
    const unsigned char *rgb;

    switch (type) {
        case 0:  /* ERROR */
            rgb = g_palette.error_rgb;
            break;
        case 1:  /* WARNING */
            rgb = g_palette.warning_rgb;
            break;
        case 2:  /* SUCCESS */
            rgb = g_palette.success_rgb;
            break;
        default:
            rgb = g_palette.accent_rgb;
            break;
    }

    /* Always use truecolor */
    snprintf(buf, sizeof(buf), "\033[38;2;%d;%d;%dm", rgb[0], rgb[1], rgb[2]);
    return buf;
}

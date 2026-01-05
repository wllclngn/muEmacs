/*
 * palette.h - Terminal color palette system
 *
 * Provides semantic color indices that map to the 256-color terminal palette.
 * This allows muEmacs to inherit colors from the user's terminal theme rather
 * than hardcoding truecolor RGB values.
 *
 * Configuration priority (highest to lowest):
 *   1. Environment variables (runtime override)
 *   2. User settings.toml
 *   3. Default settings.toml
 *   4. Hardcoded defaults
 *
 * C23 compliant
 */

#ifndef TERMINAL_PALETTE_H
#define TERMINAL_PALETTE_H

#include <stdbool.h>

/*
 * Terminal color capability levels
 */
typedef enum {
    COLOR_CAP_NONE = 0,      /* No color support */
    COLOR_CAP_16 = 1,        /* 16-color (basic ANSI 30-37, 90-97) */
    COLOR_CAP_256 = 2,       /* 256-color (CSI 38;5;N / 48;5;N) */
    COLOR_CAP_TRUECOLOR = 3  /* 24-bit RGB (CSI 38;2;R;G;B / 48;2;R;G;B) */
} color_capability_t;

/*
 * UI color palette - semantic color indices
 *
 * Index values:
 *   -1 = Use terminal default (no color code emitted)
 *   0-7 = Basic ANSI colors (black, red, green, yellow, blue, magenta, cyan, white)
 *   8-15 = Bright ANSI colors
 *   16-231 = 256-color cube
 *   232-255 = Grayscale ramp
 *   -2 = Use truecolor RGB (check *_rgb fields)
 */
typedef struct {
    /* Background colors (palette index or -2 for truecolor) */
    int modeline_bg;      /* Modeline background (default: 236 = dark gray) */
    int highlight_bg;     /* Cursor line background (default: 237 = slightly lighter) */
    int ruler_bg;         /* Column ruler background (default: 235 = subtle) */
    int selection_bg;     /* Visual selection background (default: -2, uses truecolor) */
    int search_bg;        /* Search match background (default: -2, uses truecolor) */
    int message_bg;       /* Message line background (default: -2, uses truecolor) */

    /* Foreground colors */
    int modeline_fg;      /* Modeline foreground (default: -1 = terminal default) */
    int accent;           /* Accent color for titles (default: 14 = bright cyan) */
    int search_fg;        /* Search match foreground (-1 = inherit, -2 = truecolor) */

    /* Truecolor RGB for backgrounds and modeline foreground */
    unsigned char modeline_rgb[3];
    unsigned char modeline_fg_rgb[3];
    unsigned char highlight_rgb[3];
    unsigned char ruler_rgb[3];
    unsigned char selection_rgb[3];
    unsigned char search_bg_rgb[3];
    unsigned char search_fg_rgb[3];
    unsigned char message_bg_rgb[3];

    /* Vim mode indicator colors (truecolor RGB) */
    unsigned char mode_normal_rgb[3];
    unsigned char mode_insert_rgb[3];
    unsigned char mode_visual_rgb[3];
    unsigned char mode_replace_rgb[3];
    unsigned char mode_evil_rgb[3];     /* EVIL splash color */

    /* Message type colors (truecolor RGB) */
    unsigned char accent_rgb[3];
    unsigned char error_rgb[3];
    unsigned char warning_rgb[3];
    unsigned char success_rgb[3];

    /* Syntax highlighting colors (truecolor RGB) */
    unsigned char syntax_keyword_rgb[3];
    unsigned char syntax_string_rgb[3];
    unsigned char syntax_comment_rgb[3];
    unsigned char syntax_number_rgb[3];
    unsigned char syntax_type_rgb[3];
    unsigned char syntax_function_rgb[3];
    unsigned char syntax_operator_rgb[3];
    unsigned char syntax_preproc_rgb[3];
    unsigned char syntax_constant_rgb[3];
    unsigned char syntax_variable_rgb[3];
    unsigned char syntax_attribute_rgb[3];
    unsigned char syntax_escape_rgb[3];
    unsigned char syntax_regex_rgb[3];
    unsigned char syntax_special_rgb[3];

    /* Syntax font styles (bold, italic, underline flags) */
    unsigned char syntax_keyword_style;
    unsigned char syntax_comment_style;
    unsigned char syntax_string_style;
    unsigned char syntax_type_style;
    unsigned char syntax_function_style;

    /* Flags */
    bool search_fg_set;   /* true if user specified search foreground */
    bool syntax_enabled;  /* syntax highlighting enabled */
    bool syntax_prefer_builtin;  /* true = built-in lexers first, false = extensions first */
} ui_palette_t;

/* Global palette instance */
extern ui_palette_t g_palette;

/*
 * Initialize palette from environment variables and config.
 * Should be called early in startup, after settings are loaded.
 */
void palette_init(void);

/*
 * Detect terminal color capability.
 * Checks COLORTERM and TERM environment variables.
 */
color_capability_t palette_detect_capability(void);

/*
 * Check if terminal supports truecolor.
 */
bool palette_truecolor_capable(void);

/*
 * Generate SGR escape sequence for foreground color.
 *
 * @param idx  Palette index (0-255, -1 for default, -2 for truecolor)
 * @return     Static buffer with escape sequence (e.g., "\033[38;5;14m")
 *             Returns "" for idx == -1 (no color change)
 */
const char* sgr_palette_fg(int idx);

/*
 * Generate SGR escape sequence for background color.
 *
 * @param idx  Palette index (0-255, -1 for default, -2 for truecolor)
 * @return     Static buffer with escape sequence (e.g., "\033[48;5;236m")
 *             Returns "" for idx == -1 (no color change)
 */
const char* sgr_palette_bg(int idx);

/*
 * Generate SGR escape sequence for truecolor background.
 *
 * @param r, g, b  RGB values (0-255)
 * @return         Static buffer with escape sequence
 */
const char* sgr_truecolor_bg(int r, int g, int b);

/*
 * Generate SGR escape sequence for truecolor foreground.
 *
 * @param r, g, b  RGB values (0-255)
 * @return         Static buffer with escape sequence
 */
const char* sgr_truecolor_fg(int r, int g, int b);

/*
 * Generate SGR for vim mode indicator color.
 *
 * @param mode  Vim mode (0=NORMAL, 1=INSERT, 2=VISUAL, 3=REPLACE, etc.)
 * @return      Static buffer with foreground color escape sequence
 */
const char* sgr_mode_color(int mode);

/*
 * Generate SGR for selection background.
 * Uses selection_rgb from g_palette.
 *
 * @return  Static buffer with background color escape sequence
 */
const char* sgr_selection_bg(void);

/*
 * Generate SGR for search match background.
 * Uses search_bg_rgb from g_palette.
 *
 * @return  Static buffer with background color escape sequence
 */
const char* sgr_search_bg(void);

/*
 * Generate SGR for search match foreground (if set).
 * Returns empty string if search_fg_set is false.
 *
 * @return  Static buffer with foreground color escape sequence, or ""
 */
const char* sgr_search_fg(void);

/*
 * Generate SGR for message type (error, warning, success).
 *
 * @param type  0=error, 1=warning, 2=success
 * @return      Static buffer with foreground color escape sequence
 */
const char* sgr_message_color(int type);

/*
 * Clamp palette index to terminal capability.
 * Downgrades 256-color to 16-color if needed.
 *
 * @param idx  Palette index
 * @return     Clamped index suitable for terminal
 */
int palette_clamp(int idx);

/*
 * Parse hex color string to RGB values.
 *
 * @param hex      Hex string (e.g., "#2d2d32" or "2d2d32")
 * @param r, g, b  Output RGB values
 * @return         true if parsed successfully
 */
bool palette_parse_hex(const char* hex, int* r, int* g, int* b);

/*
 * Generate SGR escape sequence for syntax face foreground color.
 *
 * @param face_id  Syntax face ID (from syntax_face_t enum)
 * @return         Static buffer with foreground color escape sequence
 */
const char* sgr_syntax_fg(int face_id);

/*
 * Generate SGR escape sequence for syntax face style (bold/italic/underline).
 *
 * @param face_id  Syntax face ID (from syntax_face_t enum)
 * @return         Static buffer with style escape sequence, or ""
 */
const char* sgr_syntax_style(int face_id);

/*
 * Reset syntax style (turn off bold/italic/underline).
 *
 * @return         Static buffer with reset sequence
 */
const char* sgr_syntax_style_reset(void);

#endif /* TERMINAL_PALETTE_H */

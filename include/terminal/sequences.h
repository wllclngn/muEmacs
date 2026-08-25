/*
 * sequences.h - Terminal Escape Sequence Constants
 *
 * Centralized definitions for ANSI/VT escape sequences.
 * Improves maintainability by keeping magic strings in one place.
 *
 * References:
 * - https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - https://sw.kovidgoyal.net/kitty/keyboard-protocol/
 * - https://terminalguide.namepad.de/
 */

#ifndef TERMINAL_SEQUENCES_H
#define TERMINAL_SEQUENCES_H

/* Screen Management */

/* Alternate screen buffer (xterm) */
#define ESC_ALT_SCREEN_ON   "\033[?1049h"
#define ESC_ALT_SCREEN_OFF  "\033[?1049l"

/* Screen clearing */
#define ESC_CLEAR_SCREEN    "\033[2J"       /* Clear entire screen */
#define ESC_CLEAR_ABOVE     "\033[1J"       /* Clear from cursor up */
#define ESC_CLEAR_BELOW     "\033[0J"       /* Clear from cursor down */
#define ESC_CLEAR_LINE      "\033[2K"       /* Clear entire line */
#define ESC_CLEAR_LINE_LEFT "\033[1K"       /* Clear from cursor left */
#define ESC_CLEAR_LINE_RIGHT "\033[0K"      /* Clear from cursor right */

/* Cursor Control */

/* Cursor positioning */
#define ESC_CURSOR_HOME     "\033[H"        /* Move to (1,1) */
#define ESC_CURSOR_SAVE     "\0337"         /* Save cursor (DEC) */
#define ESC_CURSOR_RESTORE  "\0338"         /* Restore cursor (DEC) */
#define ESC_CURSOR_SAVE_SCO "\033[s"        /* Save cursor (SCO) */
#define ESC_CURSOR_RESTORE_SCO "\033[u"     /* Restore cursor (SCO) */

/* Cursor visibility */
#define ESC_CURSOR_SHOW     "\033[?25h"     /* Show cursor */
#define ESC_CURSOR_HIDE     "\033[?25l"     /* Hide cursor */

/* Cursor shape (DECSCUSR) */
#define ESC_CURSOR_BLOCK        "\033[2 q"  /* Block cursor */
#define ESC_CURSOR_UNDERLINE    "\033[4 q"  /* Underline cursor */
#define ESC_CURSOR_BAR          "\033[6 q"  /* Bar cursor */
#define ESC_CURSOR_BLINK_BLOCK  "\033[1 q"  /* Blinking block */
#define ESC_CURSOR_BLINK_UNDER  "\033[3 q"  /* Blinking underline */
#define ESC_CURSOR_BLINK_BAR    "\033[5 q"  /* Blinking bar */

/* Attribute Control (SGR - Select Graphic Rendition) */

/* Reset all attributes */
#define ESC_RESET           "\033[0m"

/* Text styles */
#define ESC_BOLD            "\033[1m"
#define ESC_DIM             "\033[2m"
#define ESC_ITALIC          "\033[3m"
#define ESC_UNDERLINE       "\033[4m"
#define ESC_BLINK           "\033[5m"
#define ESC_REVERSE         "\033[7m"
#define ESC_HIDDEN          "\033[8m"
#define ESC_STRIKETHROUGH   "\033[9m"

/* Reset specific styles */
#define ESC_BOLD_OFF        "\033[22m"
#define ESC_ITALIC_OFF      "\033[23m"
#define ESC_UNDERLINE_OFF   "\033[24m"
#define ESC_BLINK_OFF       "\033[25m"
#define ESC_REVERSE_OFF     "\033[27m"

/* Default colors */
#define ESC_DEFAULT_FG      "\033[39m"
#define ESC_DEFAULT_BG      "\033[49m"

/* Color format strings (for snprintf) */
#define ESC_FG_256_FMT      "\033[38;5;%dm"     /* 256-color foreground */
#define ESC_BG_256_FMT      "\033[48;5;%dm"     /* 256-color background */
#define ESC_FG_RGB_FMT      "\033[38;2;%d;%d;%dm"  /* Truecolor foreground */
#define ESC_BG_RGB_FMT      "\033[48;2;%d;%d;%dm"  /* Truecolor background */

/* Terminal Modes */

/* Bracketed paste mode */
#define ESC_BRACKETED_PASTE_ON  "\033[?2004h"
#define ESC_BRACKETED_PASTE_OFF "\033[?2004l"
#define ESC_PASTE_START         "\033[200~"
#define ESC_PASTE_END           "\033[201~"

/* Focus reporting */
#define ESC_FOCUS_ON        "\033[?1004h"
#define ESC_FOCUS_OFF       "\033[?1004l"
#define ESC_FOCUS_IN        "\033[I"
#define ESC_FOCUS_OUT       "\033[O"

/* Mouse tracking */
#define ESC_MOUSE_ON        "\033[?1000h"   /* Basic mouse */
#define ESC_MOUSE_OFF       "\033[?1000l"
#define ESC_MOUSE_SGR_ON    "\033[?1006h"   /* SGR extended mouse */
#define ESC_MOUSE_SGR_OFF   "\033[?1006l"

/* Synchronized Updates (DEC private mode 2026)
 * Prevents flicker by batching updates */

#define ESC_SYNC_START      "\033[?2026h"   /* Begin synchronized update */
#define ESC_SYNC_END        "\033[?2026l"   /* End synchronized update */

/* Kitty Keyboard Protocol
 * See: https://sw.kovidgoyal.net/kitty/keyboard-protocol/ */

/* Query terminal for keyboard protocol support */
#define ESC_KITTY_QUERY     "\033[?u"

/* Push/pop keyboard enhancement flags */
#define ESC_KITTY_PUSH      "\033[>%du"     /* Push flags (format string) */
#define ESC_KITTY_PUSH_1    "\033[>1u"      /* Push with disambiguation only */
#define ESC_KITTY_PUSH_3    "\033[>3u"      /* Push with event types */
#define ESC_KITTY_PUSH_31   "\033[>31u"     /* Push with full features */
#define ESC_KITTY_POP       "\033[<u"       /* Pop enhancement stack */

/* Kitty keyboard enhancement flags */
#define KITTY_FLAG_DISAMBIGUATE     0x01    /* Disambiguate escape codes */
#define KITTY_FLAG_REPORT_EVENTS    0x02    /* Report key release events */
#define KITTY_FLAG_REPORT_ALTERNATE 0x04    /* Report alternate key codes */
#define KITTY_FLAG_REPORT_ALL       0x08    /* Report all keys as escapes */
#define KITTY_FLAG_REPORT_TEXT      0x10    /* Report associated text */

/* Terminal Queries */

/* Device attributes */
#define ESC_DEVICE_ATTRS    "\033[c"        /* Primary device attributes */
#define ESC_DEVICE_ATTRS_2  "\033[>c"       /* Secondary device attributes */

/* Terminal size (not all terminals support this) */
#define ESC_QUERY_SIZE      "\033[18t"      /* Request terminal size */

/* Cursor position */
#define ESC_QUERY_CURSOR    "\033[6n"       /* Request cursor position */

/* Arrow Keys and Special Keys (for PTY output) */

/* Arrow keys */
#define ESC_KEY_UP          "\033[A"
#define ESC_KEY_DOWN        "\033[B"
#define ESC_KEY_RIGHT       "\033[C"
#define ESC_KEY_LEFT        "\033[D"

/* Navigation keys */
#define ESC_KEY_HOME        "\033[H"
#define ESC_KEY_END         "\033[F"
#define ESC_KEY_INSERT      "\033[2~"
#define ESC_KEY_DELETE      "\033[3~"
#define ESC_KEY_PAGEUP      "\033[5~"
#define ESC_KEY_PAGEDOWN    "\033[6~"

/* Function keys (basic) */
#define ESC_KEY_F1          "\033OP"
#define ESC_KEY_F2          "\033OQ"
#define ESC_KEY_F3          "\033OR"
#define ESC_KEY_F4          "\033OS"
#define ESC_KEY_F5          "\033[15~"
#define ESC_KEY_F6          "\033[17~"
#define ESC_KEY_F7          "\033[18~"
#define ESC_KEY_F8          "\033[19~"
#define ESC_KEY_F9          "\033[20~"
#define ESC_KEY_F10         "\033[21~"
#define ESC_KEY_F11         "\033[23~"
#define ESC_KEY_F12         "\033[24~"

#endif /* TERMINAL_SEQUENCES_H */

/*      INPUT_STATE.H
 *
 *      Input state machine for terminal escape sequence parsing
 *      No timeouts - proper state machine with lookahead buffer
 *
 *      2024 - Complete rewrite for robust terminal handling
 */

#ifndef INPUT_STATE_H
#define INPUT_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

/* Buffer sizes - generous to handle all modern terminal sequences */
static constexpr int INPUT_CSI_BUF_SIZE      = 256;   /* CSI parameter buffer (was 64) */
static constexpr int INPUT_STRING_BUF_SIZE   = 8192;  /* OSC/APC/DCS string buffer */
static constexpr int INPUT_PENDING_SIZE      = 256;   /* Lookahead pushback buffer (was 64, increased for rapid input) */
static constexpr int INPUT_UTF8_BUF_SIZE     = 4;     /* UTF-8 continuation bytes */

static_assert(INPUT_CSI_BUF_SIZE >= 64, "CSI buffer must handle long sequences");
static_assert(INPUT_PENDING_SIZE >= 64, "Pending buffer must handle rapid input");
static_assert(INPUT_UTF8_BUF_SIZE >= 4, "UTF-8 buffer must hold max sequence length");

/* Parser states - follows VT500 state machine model */
typedef enum {
    INPUT_GROUND = 0,           /* Normal character processing */
    INPUT_ESCAPE,               /* Just saw ESC (0x1B) */
    INPUT_ESCAPE_INTERMEDIATE,  /* ESC followed by intermediate byte */
    INPUT_CSI_ENTRY,            /* Saw ESC [ */
    INPUT_CSI_PARAM,            /* Reading CSI parameters */
    INPUT_CSI_INTERMEDIATE,     /* Reading CSI intermediate bytes */
    INPUT_CSI_IGNORE,           /* Discarding malformed CSI */
    INPUT_OSC_STRING,           /* Reading OSC string (until ST or BEL) */
    INPUT_DCS_ENTRY,            /* Saw ESC P */
    INPUT_DCS_PARAM,            /* Reading DCS parameters */
    INPUT_DCS_INTERMEDIATE,     /* Reading DCS intermediate bytes */
    INPUT_DCS_PASSTHROUGH,      /* Passing DCS data (sixel, etc.) */
    INPUT_DCS_IGNORE,           /* Discarding malformed DCS */
    INPUT_APC_STRING,           /* Reading APC string (kitty graphics) */
    INPUT_PM_STRING,            /* Reading PM string */
    INPUT_SOS_STRING,           /* Reading SOS string */
    INPUT_SS2,                  /* Single shift 2 (ESC N) */
    INPUT_SS3,                  /* Single shift 3 (ESC O) */
    INPUT_PASTE_CONTENT,        /* Inside bracketed paste */
} input_state_t;

/* Key event types returned by parser */
typedef enum {
    KEY_NONE = 0,               /* No event (need more input) */
    KEY_CHAR,                   /* Normal Unicode character */
    KEY_SPECIAL,                /* Function key, arrow, etc. */
    KEY_PASTE_START,            /* Bracketed paste beginning */
    KEY_PASTE_END,              /* Bracketed paste ending */
    KEY_FOCUS_IN,               /* Focus gained */
    KEY_FOCUS_OUT,              /* Focus lost */
    KEY_MOUSE,                  /* Mouse event (if enabled) */
    KEY_CSI_UNKNOWN,            /* Unrecognized CSI sequence */
    KEY_OSC_RECEIVED,           /* OSC sequence received */
} input_key_event_type_t;

/* Modifier flags */
#define MOD_NONE        0x0000
#define MOD_SHIFT       0x0001
#define MOD_ALT         0x0002  /* Meta key */
#define MOD_CTRL        0x0004
#define MOD_SUPER       0x0008
#define MOD_HYPER       0x0010
#define MOD_META        0x0020  /* Explicit Meta (ESC prefix) */

/* Special key codes (beyond Unicode range) */
#define SPECIAL_KEY_BASE        0x110000
#define SPECIAL_UP              (SPECIAL_KEY_BASE + 0)
#define SPECIAL_DOWN            (SPECIAL_KEY_BASE + 1)
#define SPECIAL_RIGHT           (SPECIAL_KEY_BASE + 2)
#define SPECIAL_LEFT            (SPECIAL_KEY_BASE + 3)
#define SPECIAL_HOME            (SPECIAL_KEY_BASE + 4)
#define SPECIAL_END             (SPECIAL_KEY_BASE + 5)
#define SPECIAL_INSERT          (SPECIAL_KEY_BASE + 6)
#define SPECIAL_DELETE          (SPECIAL_KEY_BASE + 7)
#define SPECIAL_PAGEUP          (SPECIAL_KEY_BASE + 8)
#define SPECIAL_PAGEDOWN        (SPECIAL_KEY_BASE + 9)
#define SPECIAL_F1              (SPECIAL_KEY_BASE + 10)
#define SPECIAL_F2              (SPECIAL_KEY_BASE + 11)
#define SPECIAL_F3              (SPECIAL_KEY_BASE + 12)
#define SPECIAL_F4              (SPECIAL_KEY_BASE + 13)
#define SPECIAL_F5              (SPECIAL_KEY_BASE + 14)
#define SPECIAL_F6              (SPECIAL_KEY_BASE + 15)
#define SPECIAL_F7              (SPECIAL_KEY_BASE + 16)
#define SPECIAL_F8              (SPECIAL_KEY_BASE + 17)
#define SPECIAL_F9              (SPECIAL_KEY_BASE + 18)
#define SPECIAL_F10             (SPECIAL_KEY_BASE + 19)
#define SPECIAL_F11             (SPECIAL_KEY_BASE + 20)
#define SPECIAL_F12             (SPECIAL_KEY_BASE + 21)
#define SPECIAL_BACKTAB         (SPECIAL_KEY_BASE + 22)
#define SPECIAL_KEYPAD_BEGIN    (SPECIAL_KEY_BASE + 23)

/* Kitty keyboard protocol functional key codes
 * See: https://sw.kovidgoyal.net/kitty/keyboard-protocol/#functional-key-definitions
 * These are the unicode codepoints used by Kitty for special keys */
#define KITTY_KEY_ESCAPE        27
#define KITTY_KEY_ENTER         13
#define KITTY_KEY_TAB           9
#define KITTY_KEY_BACKSPACE     127
#define KITTY_KEY_INSERT        57348
#define KITTY_KEY_DELETE        57349
#define KITTY_KEY_LEFT          57350
#define KITTY_KEY_RIGHT         57351
#define KITTY_KEY_UP            57352
#define KITTY_KEY_DOWN          57353
#define KITTY_KEY_PAGEUP        57354
#define KITTY_KEY_PAGEDOWN      57355
#define KITTY_KEY_HOME          57356
#define KITTY_KEY_END           57357
#define KITTY_KEY_CAPS_LOCK     57358
#define KITTY_KEY_SCROLL_LOCK   57359
#define KITTY_KEY_NUM_LOCK      57360
#define KITTY_KEY_PRINT_SCREEN  57361
#define KITTY_KEY_PAUSE         57362
#define KITTY_KEY_MENU          57363
#define KITTY_KEY_F1            57364
#define KITTY_KEY_F2            57365
#define KITTY_KEY_F3            57366
#define KITTY_KEY_F4            57367
#define KITTY_KEY_F5            57368
#define KITTY_KEY_F6            57369
#define KITTY_KEY_F7            57370
#define KITTY_KEY_F8            57371
#define KITTY_KEY_F9            57372
#define KITTY_KEY_F10           57373
#define KITTY_KEY_F11           57374
#define KITTY_KEY_F12           57375

/* Kitty event types */
#define KITTY_EVENT_PRESS       1
#define KITTY_EVENT_REPEAT      2
#define KITTY_EVENT_RELEASE     3

/* Key event output from parser */
typedef struct input_key_event {
    uint32_t code;              /* Unicode codepoint or special key code */
    uint16_t modifiers;         /* Modifier flags (MOD_*) */
    input_key_event_type_t type;      /* Event type */

    /* For mouse events */
    uint8_t mouse_button;
    uint16_t mouse_x;
    uint16_t mouse_y;

    /* For OSC/CSI data passthrough */
    const uint8_t *data;
    size_t data_len;
} input_key_event_t;

/* Input parser context */
typedef struct input_parser {
    input_state_t state;

    /* CSI parameter accumulation */
    uint8_t csi_buf[INPUT_CSI_BUF_SIZE];
    size_t csi_len;
    uint8_t csi_intermediate[16];
    size_t csi_intermediate_len;
    bool csi_private;           /* CSI starts with ? or < or > or = */
    uint8_t csi_private_marker; /* The actual private marker */

    /* String accumulation (OSC, APC, DCS, etc.) */
    uint8_t string_buf[INPUT_STRING_BUF_SIZE];
    size_t string_len;
    uint8_t string_type;        /* Which string type we're parsing */

    /* Bracketed paste state */
    bool in_paste;
    uint8_t paste_match_buf[6]; /* For matching end sequence */
    size_t paste_match_idx;

    /* UTF-8 decoding */
    uint8_t utf8_buf[INPUT_UTF8_BUF_SIZE];
    size_t utf8_expected;
    size_t utf8_collected;

    /* Pending input queue (for lookahead pushback) */
    uint8_t pending[INPUT_PENDING_SIZE];
    size_t pending_head;
    size_t pending_tail;
    size_t pending_count;

    /* ESC key handling */
    bool esc_pending;           /* ESC received, waiting to see if sequence follows */

    /* Statistics for debugging */
    _Atomic uint64_t sequences_parsed;
    _Atomic uint64_t chars_emitted;
    _Atomic uint64_t parse_errors;
    _Atomic uint64_t bytes_processed;
} input_parser_t;

/* Parser lifecycle */
void input_parser_init(input_parser_t *p);
void input_parser_reset(input_parser_t *p);
void input_parser_destroy(input_parser_t *p);

/*
 * Feed one byte to the parser
 *
 * Returns: Number of events produced (0 or 1)
 *
 * This function is designed to be called in a loop:
 *   while (have_bytes) {
 *       int n = input_parser_feed(p, byte, &event);
 *       if (n > 0) handle_event(&event);
 *   }
 *
 * Key design properties:
 * - NO TIMEOUTS: The parser accumulates state without timeout checks
 * - NO RECURSION: Always returns after processing one byte
 * - NO BLOCKING: Never waits for more input
 * - LOOKAHEAD: Uses pending buffer for sequence disambiguation
 */
int input_parser_feed(input_parser_t *p, uint8_t byte, input_key_event_t *out);

/*
 * Check if there's an unresolved ESC pending
 *
 * Call this when the input buffer is exhausted to see if an ESC
 * should be emitted as a standalone key (user pressed ESC).
 * Returns true if flush_esc() should be called.
 */
bool input_parser_has_pending_esc(input_parser_t *p);

/*
 * Flush a pending ESC as a standalone key event
 *
 * Call when input is exhausted and has_pending_esc() returns true.
 * This emits ESC (0x1B) as a KEY_CHAR event.
 */
int input_parser_flush_esc(input_parser_t *p, input_key_event_t *out);

/*
 * Push bytes back into the parser's pending queue
 *
 * Used when bytes were read speculatively but turned out to not be
 * part of a complete sequence.
 */
void input_parser_pushback(input_parser_t *p, const uint8_t *data, size_t len);

/*
 * Check if parser has pending output from pushback
 */
bool input_parser_has_pending(input_parser_t *p);

/*
 * Get next pending byte (if any)
 * Returns -1 if no pending bytes
 */
int input_parser_get_pending(input_parser_t *p);

/* Debug: get parser statistics */
void input_parser_get_stats(input_parser_t *p,
                            uint64_t *sequences_parsed,
                            uint64_t *chars_emitted,
                            uint64_t *parse_errors,
                            uint64_t *bytes_processed);

/*
 * High-level input functions (defined in src/io/input.c)
 */

/* Get a key event from terminal using unified parser */
int get_key_event(input_key_event_t *out);

/*
 * Event comparison helpers - Modern replacement for legacy key comparisons
 */

/* Check if event is a plain character (no modifiers) */
static inline bool evt_is_char(const input_key_event_t *evt, int ch) {
    return evt->type == KEY_CHAR && evt->code == (uint32_t)ch && evt->modifiers == 0;
}

/* Check if event is Ctrl+char (case-insensitive for letters) */
static inline bool evt_is_ctrl(const input_key_event_t *evt, int ch) {
    if (evt->type != KEY_CHAR || (evt->modifiers & MOD_CTRL) == 0)
        return false;
    /* Case-insensitive comparison for A-Z/a-z */
    uint32_t code = evt->code;
    uint32_t target = (uint32_t)ch;
    if (code >= 'a' && code <= 'z') code -= 32;  /* to uppercase */
    if (target >= 'a' && target <= 'z') target -= 32;
    return code == target;
}

/* Check if event is Meta/Alt+char (case-insensitive for letters) */
static inline bool evt_is_meta(const input_key_event_t *evt, int ch) {
    if (evt->type != KEY_CHAR || (evt->modifiers & (MOD_META | MOD_ALT)) == 0)
        return false;
    /* Case-insensitive comparison for A-Z/a-z */
    uint32_t code = evt->code;
    uint32_t target = (uint32_t)ch;
    if (code >= 'a' && code <= 'z') code -= 32;
    if (target >= 'a' && target <= 'z') target -= 32;
    return code == target;
}

/* Check if event is a digit 0-9 (no modifiers) */
static inline bool evt_is_digit(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR && evt->code >= '0' && evt->code <= '9' &&
           evt->modifiers == 0;
}

/* Check if event is Meta+digit */
static inline bool evt_is_meta_digit(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR && evt->code >= '0' && evt->code <= '9' &&
           (evt->modifiers & (MOD_META | MOD_ALT)) != 0;
}

/* Get the base character code from event (for digit arithmetic, etc.) */
static inline int evt_char(const input_key_event_t *evt) {
    return (evt->type == KEY_CHAR) ? (int)evt->code : -1;
}

/* Check if event is ESC key (raw escape, not as modifier) */
static inline bool evt_is_esc(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR && evt->code == 0x1B && evt->modifiers == 0;
}

/* Check if event has Meta modifier set */
static inline bool evt_has_meta(const input_key_event_t *evt) {
    return (evt->modifiers & (MOD_META | MOD_ALT)) != 0;
}

/* Check if event has Ctrl modifier set */
static inline bool evt_has_ctrl(const input_key_event_t *evt) {
    return (evt->modifiers & MOD_CTRL) != 0;
}

/* Check for Enter/Return key (input parser converts 0x0D to Ctrl+M) */
static inline bool evt_is_enter(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR &&
           evt->code == 'M' &&
           evt->modifiers == MOD_CTRL;
}

/* Check for Backspace (0x7F passes through unchanged from input parser) */
static inline bool evt_is_backspace(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR &&
           evt->code == 0x7F &&
           evt->modifiers == 0;
}

/* Check for Tab key (input parser converts 0x09 to Ctrl+I) */
static inline bool evt_is_tab(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR &&
           evt->code == 'I' &&
           evt->modifiers == MOD_CTRL;
}

/* Check for specific special key (arrow, F-key, etc.) */
static inline bool evt_is_special(const input_key_event_t *evt, uint32_t special_code) {
    return evt->type == KEY_SPECIAL && evt->code == special_code;
}

/* Check if event is a printable ASCII character (no modifiers) */
static inline bool evt_is_printable(const input_key_event_t *evt) {
    return evt->type == KEY_CHAR &&
           evt->code >= 0x20 && evt->code < 0x7F &&
           evt->modifiers == 0;
}

/* Check if event matches Ctrl+char (case insensitive) */
static inline bool evt_is_ctrl_ci(const input_key_event_t *evt, int ch) {
    int upper = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
    int lower = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
    return evt->type == KEY_CHAR &&
           (evt->code == (uint32_t)upper || evt->code == (uint32_t)lower) &&
           (evt->modifiers & MOD_CTRL) != 0;
}

#endif /* INPUT_STATE_H */

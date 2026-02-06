/*      INPUT_STATE.C
 *
 *      Input state machine for terminal escape sequence parsing
 *      No timeouts - proper state machine with lookahead buffer
 *
 *      2024 - Complete rewrite for robust terminal handling
 *
 *      Design principles:
 *      - No timeouts: State machine accumulates until sequence completes
 *      - No recursion: Single-level processing per byte
 *      - Lookahead pushback: Disambiguate ESC vs ESC-sequence
 *      - VT500-compatible: Follows DEC terminal state machine model
 */

#include "terminal/input_state.h"
#include <string.h>

/* Static helpers */
static void emit_char(input_parser_t *p, input_key_event_t *out, uint32_t code, uint16_t mods);
static void emit_special(input_parser_t *p, input_key_event_t *out, uint32_t code, uint16_t mods);
static void emit_paste_start(input_parser_t *p, input_key_event_t *out);
static void emit_paste_end(input_parser_t *p, input_key_event_t *out);
static void emit_focus(input_parser_t *p, input_key_event_t *out, bool in);
static int handle_csi_final(input_parser_t *p, uint8_t final, input_key_event_t *out);
static int handle_ss3_final(input_parser_t *p, uint8_t final, input_key_event_t *out);
static int handle_kitty_key(input_parser_t *p, input_key_event_t *out);
static int parse_sgr_mouse(input_parser_t *p, uint8_t final, input_key_event_t *out);
static int parse_csi_params(input_parser_t *p, int *params, int max_params);
static uint16_t decode_modifiers(int modifier_param);
static bool is_csi_param_byte(uint8_t c);
static bool is_csi_intermediate_byte(uint8_t c);
static bool is_csi_final_byte(uint8_t c);
static bool is_c0_control(uint8_t c);

void input_parser_init(input_parser_t *p) {
    memset(p, 0, sizeof(*p));
    p->state = INPUT_GROUND;
    atomic_store(&p->sequences_parsed, 0);
    atomic_store(&p->chars_emitted, 0);
    atomic_store(&p->parse_errors, 0);
    atomic_store(&p->bytes_processed, 0);
}

void input_parser_reset(input_parser_t *p) {
    p->state = INPUT_GROUND;
    p->csi_len = 0;
    p->csi_intermediate_len = 0;
    p->csi_private = false;
    p->csi_private_marker = 0;
    p->string_len = 0;
    p->utf8_expected = 0;
    p->utf8_collected = 0;
    p->esc_pending = false;
    /* Don't reset pending queue or statistics */
}

void input_parser_destroy(input_parser_t *p) {
    /* Nothing to free - all storage is inline */
    (void)p;
}

int input_parser_feed(input_parser_t *p, uint8_t byte, input_key_event_t *out) {
    atomic_fetch_add(&p->bytes_processed, 1);

    /* Clear output */
    memset(out, 0, sizeof(*out));

    /* Handle UTF-8 continuation bytes */
    if (p->utf8_expected > 0) {
        if ((byte & 0xC0) == 0x80) {
            /* Valid continuation byte */
            p->utf8_buf[p->utf8_collected++] = byte;
            if (p->utf8_collected == p->utf8_expected) {
                /* Complete UTF-8 sequence */
                uint32_t cp = 0;
                switch (p->utf8_expected) {
                case 2:
                    cp = (uint32_t)((p->utf8_buf[0] & 0x1F) << 6) |
                         (uint32_t)(p->utf8_buf[1] & 0x3F);
                    break;
                case 3:
                    cp = (uint32_t)((p->utf8_buf[0] & 0x0F) << 12) |
                         (uint32_t)((p->utf8_buf[1] & 0x3F) << 6) |
                         (uint32_t)(p->utf8_buf[2] & 0x3F);
                    break;
                case 4:
                    cp = (uint32_t)((p->utf8_buf[0] & 0x07) << 18) |
                         (uint32_t)((p->utf8_buf[1] & 0x3F) << 12) |
                         (uint32_t)((p->utf8_buf[2] & 0x3F) << 6) |
                         (uint32_t)(p->utf8_buf[3] & 0x3F);
                    break;
                }
                p->utf8_expected = 0;
                p->utf8_collected = 0;
                emit_char(p, out, cp, MOD_NONE);
                return 1;
            }
            return 0; /* Need more continuation bytes */
        } else {
            /* Invalid continuation - reset and process as new byte */
            p->utf8_expected = 0;
            p->utf8_collected = 0;
            atomic_fetch_add(&p->parse_errors, 1);
            /* Fall through to process byte normally */
        }
    }

    /* State machine */
    switch (p->state) {
    case INPUT_GROUND:
        if (byte == 0x1B) {
            /* ESC - start escape sequence */
            p->state = INPUT_ESCAPE;
            p->esc_pending = true;
            return 0;
        } else if (byte == 0x7F) {
            /* DEL (127) - modern terminals send this for Backspace key
             * Pass through as-is so evt_is_backspace() works */
            emit_char(p, out, 0x7F, MOD_NONE);
            return 1;
        } else if (byte < 0x20) {
            /* C0 control characters: convert to letter + MOD_CTRL
             * e.g., 0x13 (Ctrl-S) -> 'S' (0x53) with MOD_CTRL
             * Keymaps store CONTROL|'S', not CONTROL|0x13 */
            emit_char(p, out, (uint32_t)(byte + '@'), MOD_CTRL);
            return 1;
        } else if (byte >= 0x80 && byte < 0xC0) {
            /* Invalid UTF-8 start byte or C1 control */
            if (byte >= 0x80 && byte <= 0x9F) {
                /* C1 control - treat as 7-bit equivalent */
                /* CSI = 0x9B, equivalent to ESC [ */
                if (byte == 0x9B) {
                    p->state = INPUT_CSI_ENTRY;
                    p->csi_len = 0;
                    p->csi_intermediate_len = 0;
                    p->csi_private = false;
                    return 0;
                }
                emit_char(p, out, byte, MOD_NONE);
                return 1;
            }
            /* Invalid byte */
            atomic_fetch_add(&p->parse_errors, 1);
            emit_char(p, out, byte, MOD_NONE);
            return 1;
        } else if ((byte & 0xE0) == 0xC0) {
            /* UTF-8 2-byte sequence start */
            p->utf8_buf[0] = byte;
            p->utf8_expected = 2;
            p->utf8_collected = 1;
            return 0;
        } else if ((byte & 0xF0) == 0xE0) {
            /* UTF-8 3-byte sequence start */
            p->utf8_buf[0] = byte;
            p->utf8_expected = 3;
            p->utf8_collected = 1;
            return 0;
        } else if ((byte & 0xF8) == 0xF0) {
            /* UTF-8 4-byte sequence start */
            p->utf8_buf[0] = byte;
            p->utf8_expected = 4;
            p->utf8_collected = 1;
            return 0;
        } else {
            /* Regular ASCII or invalid */
            emit_char(p, out, byte, MOD_NONE);
            return 1;
        }
        break;

    case INPUT_ESCAPE:
        p->esc_pending = false;
        if (byte == '[') {
            /* CSI introducer */
            p->state = INPUT_CSI_ENTRY;
            p->csi_len = 0;
            p->csi_intermediate_len = 0;
            p->csi_private = false;
            p->csi_private_marker = 0;
            return 0;
        } else if (byte == 'O') {
            /* SS3 - single shift 3 */
            p->state = INPUT_SS3;
            return 0;
        } else if (byte == 'N') {
            /* SS2 - single shift 2 */
            p->state = INPUT_SS2;
            return 0;
        } else if (byte == 'P') {
            /* DCS - device control string */
            p->state = INPUT_DCS_ENTRY;
            p->string_len = 0;
            return 0;
        } else if (byte == ']') {
            /* OSC - operating system command */
            p->state = INPUT_OSC_STRING;
            p->string_len = 0;
            return 0;
        } else if (byte == '_') {
            /* APC - application program command */
            p->state = INPUT_APC_STRING;
            p->string_len = 0;
            return 0;
        } else if (byte == '^') {
            /* PM - privacy message */
            p->state = INPUT_PM_STRING;
            p->string_len = 0;
            return 0;
        } else if (byte == 'X') {
            /* SOS - start of string */
            p->state = INPUT_SOS_STRING;
            p->string_len = 0;
            return 0;
        } else if (byte >= 0x20 && byte <= 0x2F) {
            /* Intermediate byte - ESC sequence with intermediate */
            p->state = INPUT_ESCAPE_INTERMEDIATE;
            p->csi_intermediate[0] = byte;
            p->csi_intermediate_len = 1;
            return 0;
        } else if (byte >= 0x30 && byte <= 0x7E) {
            /* Final byte of simple escape sequence */
            p->state = INPUT_GROUND;
            /* ESC followed by letter is typically Meta+letter */
            emit_char(p, out, byte, MOD_META);
            return 1;
        } else if (byte == 0x1B) {
            /* Double ESC - emit first ESC, stay in escape state */
            emit_char(p, out, 0x1B, MOD_NONE);
            p->esc_pending = true;
            return 1;
        } else {
            /* Invalid - emit ESC and this char */
            p->state = INPUT_GROUND;
            /* Push back current char and emit ESC */
            input_parser_pushback(p, &byte, 1);
            emit_char(p, out, 0x1B, MOD_NONE);
            return 1;
        }
        break;

    case INPUT_ESCAPE_INTERMEDIATE:
        if (byte >= 0x20 && byte <= 0x2F) {
            /* Another intermediate byte */
            if (p->csi_intermediate_len < sizeof(p->csi_intermediate)) {
                p->csi_intermediate[p->csi_intermediate_len++] = byte;
            }
            return 0;
        } else if (byte >= 0x30 && byte <= 0x7E) {
            /* Final byte */
            p->state = INPUT_GROUND;
            atomic_fetch_add(&p->sequences_parsed, 1);
            /* Most ESC intermediate sequences are ignored */
            return 0;
        } else {
            /* Invalid */
            p->state = INPUT_GROUND;
            atomic_fetch_add(&p->parse_errors, 1);
            return 0;
        }
        break;

    case INPUT_CSI_ENTRY:
        if (byte == '?' || byte == '<' || byte == '>' || byte == '=') {
            /* Private mode indicator */
            p->csi_private = true;
            p->csi_private_marker = byte;
            p->state = INPUT_CSI_PARAM;
            return 0;
        }
        /* Fall through to param handling */
        p->state = INPUT_CSI_PARAM;
        /* Process this byte as param */
        /* Fall through */

    case INPUT_CSI_PARAM:
        if (is_csi_param_byte(byte)) {
            /* Parameter byte (0-9, ;, :) */
            if (p->csi_len < INPUT_CSI_BUF_SIZE - 1) {
                p->csi_buf[p->csi_len++] = byte;
            }
            return 0;
        } else if (is_csi_intermediate_byte(byte)) {
            /* Intermediate byte */
            p->state = INPUT_CSI_INTERMEDIATE;
            if (p->csi_intermediate_len < sizeof(p->csi_intermediate)) {
                p->csi_intermediate[p->csi_intermediate_len++] = byte;
            }
            return 0;
        } else if (is_csi_final_byte(byte)) {
            /* Final byte - execute CSI sequence */
            p->state = INPUT_GROUND;
            p->csi_buf[p->csi_len] = '\0';
            return handle_csi_final(p, byte, out);
        } else if (is_c0_control(byte)) {
            /* Execute C0 control inline, stay in CSI - convert to letter */
            emit_char(p, out, (uint32_t)(byte + '@'), MOD_CTRL);
            return 1;
        } else {
            /* Invalid */
            p->state = INPUT_CSI_IGNORE;
            atomic_fetch_add(&p->parse_errors, 1);
            return 0;
        }
        break;

    case INPUT_CSI_INTERMEDIATE:
        if (is_csi_intermediate_byte(byte)) {
            if (p->csi_intermediate_len < sizeof(p->csi_intermediate)) {
                p->csi_intermediate[p->csi_intermediate_len++] = byte;
            }
            return 0;
        } else if (is_csi_final_byte(byte)) {
            /* Final byte */
            p->state = INPUT_GROUND;
            p->csi_buf[p->csi_len] = '\0';
            return handle_csi_final(p, byte, out);
        } else if (is_c0_control(byte)) {
            /* Execute C0 control inline - convert to letter */
            emit_char(p, out, (uint32_t)(byte + '@'), MOD_CTRL);
            return 1;
        } else {
            p->state = INPUT_CSI_IGNORE;
            atomic_fetch_add(&p->parse_errors, 1);
            return 0;
        }
        break;

    case INPUT_CSI_IGNORE:
        /* Discard until final byte */
        if (is_csi_final_byte(byte)) {
            p->state = INPUT_GROUND;
        }
        return 0;

    case INPUT_SS3:
        /* SS3 sequence - typically keypad/function keys */
        p->state = INPUT_GROUND;
        if (byte >= 0x40 && byte <= 0x7E) {
            return handle_ss3_final(p, byte, out);
        }
        atomic_fetch_add(&p->parse_errors, 1);
        return 0;

    case INPUT_SS2:
        /* SS2 sequence - rarely used */
        p->state = INPUT_GROUND;
        /* Emit as Meta + char */
        if (byte >= 0x20) {
            emit_char(p, out, byte, MOD_META);
            return 1;
        }
        return 0;

    case INPUT_OSC_STRING:
        /* Collect until ST (ESC \) or BEL */
        if (byte == 0x07) {
            /* BEL terminates OSC */
            p->state = INPUT_GROUND;
            out->type = KEY_OSC_RECEIVED;
            out->data = p->string_buf;
            out->data_len = p->string_len;
            atomic_fetch_add(&p->sequences_parsed, 1);
            return 1;
        } else if (byte == 0x1B) {
            /* Possible ST (ESC \) */
            /* We need to peek at next byte */
            /* For now, just end OSC on ESC */
            p->state = INPUT_ESCAPE;
            p->esc_pending = true;
            out->type = KEY_OSC_RECEIVED;
            out->data = p->string_buf;
            out->data_len = p->string_len;
            atomic_fetch_add(&p->sequences_parsed, 1);
            return 1;
        } else if (byte == 0x9C) {
            /* C1 ST */
            p->state = INPUT_GROUND;
            out->type = KEY_OSC_RECEIVED;
            out->data = p->string_buf;
            out->data_len = p->string_len;
            atomic_fetch_add(&p->sequences_parsed, 1);
            return 1;
        } else if (p->string_len < INPUT_STRING_BUF_SIZE - 1) {
            p->string_buf[p->string_len++] = byte;
        }
        return 0;

    case INPUT_DCS_ENTRY:
    case INPUT_DCS_PARAM:
    case INPUT_DCS_INTERMEDIATE:
    case INPUT_DCS_PASSTHROUGH:
        /* DCS handling - similar to OSC */
        if (byte == 0x9C || (byte == 0x1B)) {
            p->state = (byte == 0x1B) ? INPUT_ESCAPE : INPUT_GROUND;
            if (byte == 0x1B) p->esc_pending = true;
            atomic_fetch_add(&p->sequences_parsed, 1);
            return 0;
        }
        /* Accumulate but don't emit */
        if (p->string_len < INPUT_STRING_BUF_SIZE - 1) {
            p->string_buf[p->string_len++] = byte;
        }
        return 0;

    case INPUT_DCS_IGNORE:
        if (byte == 0x9C) p->state = INPUT_GROUND;
        return 0;

    case INPUT_APC_STRING:
    case INPUT_PM_STRING:
    case INPUT_SOS_STRING:
        /* String commands - collect until ST */
        if (byte == 0x9C || byte == 0x07) {
            p->state = INPUT_GROUND;
            atomic_fetch_add(&p->sequences_parsed, 1);
        } else if (byte == 0x1B) {
            p->state = INPUT_ESCAPE;
            p->esc_pending = true;
            atomic_fetch_add(&p->sequences_parsed, 1);
        } else if (p->string_len < INPUT_STRING_BUF_SIZE - 1) {
            p->string_buf[p->string_len++] = byte;
        }
        return 0;

    case INPUT_PASTE_CONTENT:
        /* Inside bracketed paste - check for end sequence */
        /* End sequence: ESC [ 2 0 1 ~ */
        if (byte == 0x1B && p->paste_match_idx == 0) {
            p->paste_match_buf[0] = byte;
            p->paste_match_idx = 1;
            return 0;
        } else if (p->paste_match_idx > 0) {
            static const uint8_t paste_end[] = { 0x1B, '[', '2', '0', '1', '~' };
            if (byte == paste_end[p->paste_match_idx]) {
                p->paste_match_buf[p->paste_match_idx++] = byte;
                if (p->paste_match_idx == 6) {
                    /* End of paste */
                    p->state = INPUT_GROUND;
                    p->in_paste = false;
                    p->paste_match_idx = 0;
                    emit_paste_end(p, out);
                    return 1;
                }
                return 0;
            } else {
                /* Not end sequence - emit buffered bytes and this one */
                /* Push everything to pending and emit first */
                for (size_t i = 0; i < p->paste_match_idx; i++) {
                    input_parser_pushback(p, &p->paste_match_buf[i], 1);
                }
                input_parser_pushback(p, &byte, 1);
                p->paste_match_idx = 0;
                /* Pop first and emit */
                int b = input_parser_get_pending(p);
                if (b >= 0) {
                    emit_char(p, out, (uint32_t)b, MOD_NONE);
                    return 1;
                }
                return 0;
            }
        } else {
            /* Regular paste content */
            emit_char(p, out, byte, MOD_NONE);
            return 1;
        }
        break;
    }

    return 0;
}

bool input_parser_has_pending_esc(input_parser_t *p) {
    return p->esc_pending && p->state == INPUT_ESCAPE;
}

int input_parser_flush_esc(input_parser_t *p, input_key_event_t *out) {
    if (p->esc_pending) {
        p->esc_pending = false;
        p->state = INPUT_GROUND;
        emit_char(p, out, 0x1B, MOD_NONE);
        return 1;
    }
    return 0;
}

void input_parser_pushback(input_parser_t *p, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len && p->pending_count < INPUT_PENDING_SIZE; i++) {
        size_t idx = (p->pending_head + p->pending_count) % INPUT_PENDING_SIZE;
        p->pending[idx] = data[i];
        p->pending_count++;
    }
}

bool input_parser_has_pending(input_parser_t *p) {
    return p->pending_count > 0;
}

int input_parser_get_pending(input_parser_t *p) {
    if (p->pending_count == 0) return -1;
    uint8_t c = p->pending[p->pending_head];
    p->pending_head = (p->pending_head + 1) % INPUT_PENDING_SIZE;
    p->pending_count--;
    return c;
}

void input_parser_get_stats(input_parser_t *p,
                            uint64_t *sequences_parsed,
                            uint64_t *chars_emitted,
                            uint64_t *parse_errors,
                            uint64_t *bytes_processed) {
    if (sequences_parsed) *sequences_parsed = atomic_load(&p->sequences_parsed);
    if (chars_emitted) *chars_emitted = atomic_load(&p->chars_emitted);
    if (parse_errors) *parse_errors = atomic_load(&p->parse_errors);
    if (bytes_processed) *bytes_processed = atomic_load(&p->bytes_processed);
}

/* ========== Static helpers ========== */

static void emit_char(input_parser_t *p, input_key_event_t *out, uint32_t code, uint16_t mods) {
    out->type = KEY_CHAR;
    out->code = code;
    out->modifiers = mods;
    atomic_fetch_add(&p->chars_emitted, 1);
}

static void emit_special(input_parser_t *p, input_key_event_t *out, uint32_t code, uint16_t mods) {
    out->type = KEY_SPECIAL;
    out->code = code;
    out->modifiers = mods;
    atomic_fetch_add(&p->chars_emitted, 1);
}

static void emit_paste_start(input_parser_t *p, input_key_event_t *out) {
    out->type = KEY_PASTE_START;
    out->code = 0;
    out->modifiers = 0;
    p->in_paste = true;
    p->state = INPUT_PASTE_CONTENT;
}

static void emit_paste_end(input_parser_t *p, input_key_event_t *out) {
    out->type = KEY_PASTE_END;
    out->code = 0;
    out->modifiers = 0;
    p->in_paste = false;
}

static void emit_focus(input_parser_t *p, input_key_event_t *out, bool in) {
    out->type = in ? KEY_FOCUS_IN : KEY_FOCUS_OUT;
    out->code = 0;
    out->modifiers = 0;
    (void)p;
}

static bool is_csi_param_byte(uint8_t c) {
    return (c >= '0' && c <= '9') || c == ';' || c == ':';
}

static bool is_csi_intermediate_byte(uint8_t c) {
    return c >= 0x20 && c <= 0x2F;
}

static bool is_csi_final_byte(uint8_t c) {
    return c >= 0x40 && c <= 0x7E;
}

static bool is_c0_control(uint8_t c) {
    return c < 0x20 && c != 0x1B;
}

static uint16_t decode_modifiers(int modifier_param) {
    /* xterm modifier encoding: param = 1 + modifiers
     * Shift=1, Alt=2, Ctrl=4, Meta=8 */
    if (modifier_param <= 1) return MOD_NONE;
    uint16_t mods = MOD_NONE;
    int m = modifier_param - 1;
    if (m & 1) mods |= MOD_SHIFT;
    if (m & 2) mods |= MOD_ALT;
    if (m & 4) mods |= MOD_CTRL;
    if (m & 8) mods |= MOD_META;
    return mods;
}

static int parse_csi_params(input_parser_t *p, int *params, int max_params) {
    int count = 0;
    int value = 0;
    bool has_value = false;

    for (size_t i = 0; i < p->csi_len && count < max_params; i++) {
        uint8_t c = p->csi_buf[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            has_value = true;
        } else if (c == ';' || c == ':') {
            params[count++] = has_value ? value : 0;
            value = 0;
            has_value = false;
        }
    }
    if (count < max_params && has_value) {
        params[count++] = value;
    }
    return count;
}

static int handle_csi_final(input_parser_t *p, uint8_t final, input_key_event_t *out) {
    int params[16] = {0};
    int param_count = parse_csi_params(p, params, 16);
    uint16_t mods = MOD_NONE;

    atomic_fetch_add(&p->sequences_parsed, 1);

    /* Handle modifiers in second parameter */
    if (param_count >= 2) {
        mods = decode_modifiers(params[1]);
    }

    /* Check for private mode sequences */
    if (p->csi_private) {
        /* SGR Mouse: CSI < Cb ; Cx ; Cy M/m */
        if (p->csi_private_marker == '<' && (final == 'M' || final == 'm')) {
            return parse_sgr_mouse(p, final, out);
        }
        /* Other DEC private sequences - ignored for input */
        return 0;
    }

    switch (final) {
    /* Arrow keys */
    case 'A': emit_special(p, out, SPECIAL_UP, mods); return 1;
    case 'B': emit_special(p, out, SPECIAL_DOWN, mods); return 1;
    case 'C': emit_special(p, out, SPECIAL_RIGHT, mods); return 1;
    case 'D': emit_special(p, out, SPECIAL_LEFT, mods); return 1;

    /* Home/End */
    case 'H': emit_special(p, out, SPECIAL_HOME, mods); return 1;
    case 'F': emit_special(p, out, SPECIAL_END, mods); return 1;

    /* Keypad center (numpad 5) */
    case 'E': emit_special(p, out, SPECIAL_KEYPAD_BEGIN, mods); return 1;

    /* Focus events */
    case 'I': emit_focus(p, out, true); return 1;
    case 'O': emit_focus(p, out, false); return 1;

    /* tilde sequences: CSI number ~ */
    case '~':
        if (param_count >= 1) {
            if (param_count >= 2) mods = decode_modifiers(params[1]);
            switch (params[0]) {
            case 1: emit_special(p, out, SPECIAL_HOME, mods); return 1;
            case 2: emit_special(p, out, SPECIAL_INSERT, mods); return 1;
            case 3: emit_special(p, out, SPECIAL_DELETE, mods); return 1;
            case 4: emit_special(p, out, SPECIAL_END, mods); return 1;
            case 5: emit_special(p, out, SPECIAL_PAGEUP, mods); return 1;
            case 6: emit_special(p, out, SPECIAL_PAGEDOWN, mods); return 1;
            case 7: emit_special(p, out, SPECIAL_HOME, mods); return 1;
            case 8: emit_special(p, out, SPECIAL_END, mods); return 1;

            /* Function keys */
            case 11: emit_special(p, out, SPECIAL_F1, mods); return 1;
            case 12: emit_special(p, out, SPECIAL_F2, mods); return 1;
            case 13: emit_special(p, out, SPECIAL_F3, mods); return 1;
            case 14: emit_special(p, out, SPECIAL_F4, mods); return 1;
            case 15: emit_special(p, out, SPECIAL_F5, mods); return 1;
            case 17: emit_special(p, out, SPECIAL_F6, mods); return 1;
            case 18: emit_special(p, out, SPECIAL_F7, mods); return 1;
            case 19: emit_special(p, out, SPECIAL_F8, mods); return 1;
            case 20: emit_special(p, out, SPECIAL_F9, mods); return 1;
            case 21: emit_special(p, out, SPECIAL_F10, mods); return 1;
            case 23: emit_special(p, out, SPECIAL_F11, mods); return 1;
            case 24: emit_special(p, out, SPECIAL_F12, mods); return 1;

            /* Bracketed paste */
            case 200:
                emit_paste_start(p, out);
                return 1;
            case 201:
                emit_paste_end(p, out);
                return 1;
            }
        }
        break;

    /* Backtab (Shift+Tab) */
    case 'Z':
        emit_special(p, out, SPECIAL_BACKTAB, mods);
        return 1;

    /* Kitty keyboard protocol: CSI key:shifted:base ; modifiers:event_type ; text u */
    case 'u':
        return handle_kitty_key(p, out);
    }

    /* Unknown CSI sequence */
    out->type = KEY_CSI_UNKNOWN;
    out->data = p->csi_buf;
    out->data_len = p->csi_len;
    return 1;
}

static int handle_ss3_final(input_parser_t *p, uint8_t final, input_key_event_t *out) {
    atomic_fetch_add(&p->sequences_parsed, 1);

    switch (final) {
    /* Arrow keys (application mode) */
    case 'A': emit_special(p, out, SPECIAL_UP, MOD_NONE); return 1;
    case 'B': emit_special(p, out, SPECIAL_DOWN, MOD_NONE); return 1;
    case 'C': emit_special(p, out, SPECIAL_RIGHT, MOD_NONE); return 1;
    case 'D': emit_special(p, out, SPECIAL_LEFT, MOD_NONE); return 1;

    /* Home/End (application mode) */
    case 'H': emit_special(p, out, SPECIAL_HOME, MOD_NONE); return 1;
    case 'F': emit_special(p, out, SPECIAL_END, MOD_NONE); return 1;

    /* Function keys F1-F4 (vt100 style) */
    case 'P': emit_special(p, out, SPECIAL_F1, MOD_NONE); return 1;
    case 'Q': emit_special(p, out, SPECIAL_F2, MOD_NONE); return 1;
    case 'R': emit_special(p, out, SPECIAL_F3, MOD_NONE); return 1;
    case 'S': emit_special(p, out, SPECIAL_F4, MOD_NONE); return 1;

    /* Keypad keys */
    case 'M': emit_char(p, out, '\r', MOD_NONE); return 1;  /* Enter */
    case 'j': emit_char(p, out, '*', MOD_NONE); return 1;
    case 'k': emit_char(p, out, '+', MOD_NONE); return 1;
    case 'm': emit_char(p, out, '-', MOD_NONE); return 1;
    case 'n': emit_char(p, out, '.', MOD_NONE); return 1;
    case 'o': emit_char(p, out, '/', MOD_NONE); return 1;
    case 'p': emit_char(p, out, '0', MOD_NONE); return 1;
    case 'q': emit_char(p, out, '1', MOD_NONE); return 1;
    case 'r': emit_char(p, out, '2', MOD_NONE); return 1;
    case 's': emit_char(p, out, '3', MOD_NONE); return 1;
    case 't': emit_char(p, out, '4', MOD_NONE); return 1;
    case 'u': emit_char(p, out, '5', MOD_NONE); return 1;
    case 'v': emit_char(p, out, '6', MOD_NONE); return 1;
    case 'w': emit_char(p, out, '7', MOD_NONE); return 1;
    case 'x': emit_char(p, out, '8', MOD_NONE); return 1;
    case 'y': emit_char(p, out, '9', MOD_NONE); return 1;
    }

    /* Unknown SS3 - emit as Meta + char */
    emit_char(p, out, final, MOD_META);
    return 1;
}

/*
 * Handle Kitty keyboard protocol sequence: CSI key ; modifiers u
 *
 * Full format: CSI key:shifted:base ; modifiers:event_type ; text u
 *
 * Where:
 * - key: Unicode codepoint or Kitty functional key code
 * - shifted: Shifted version of the key (optional, after colon)
 * - base: Base layout key (optional, after second colon)
 * - modifiers: 1 + modifier bits (Shift=1, Alt=2, Ctrl=4, Super=8)
 * - event_type: 1=press, 2=repeat, 3=release (optional, after colon)
 * - text: Associated text codepoint (optional third parameter)
 */
static int handle_kitty_key(input_parser_t *p, input_key_event_t *out) {
    /* Parse CSI buffer manually to handle colon-separated sub-parameters */
    uint32_t key_code = 0;
    uint32_t shifted_key = 0;
    uint32_t base_key = 0;
    int modifiers_raw = 0;
    int event_type = KITTY_EVENT_PRESS;  /* Default to press */
    uint32_t text_codepoint = 0;

    /* Parse state: 0=key, 1=modifiers, 2=text */
    int param_idx = 0;
    /* Sub-param state: 0=first, 1=second, 2=third */
    int subparam_idx = 0;
    uint32_t value = 0;
    bool has_value = false;

    for (size_t i = 0; i <= p->csi_len; i++) {
        uint8_t c = (i < p->csi_len) ? p->csi_buf[i] : ';';  /* Force final flush */

        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            has_value = true;
        } else if (c == ':' || c == ';' || i == p->csi_len) {
            /* Store accumulated value */
            if (has_value || c == ':' || c == ';') {
                uint32_t v = has_value ? value : 0;

                if (param_idx == 0) {
                    /* Key parameter (key:shifted:base) */
                    switch (subparam_idx) {
                    case 0: key_code = v; break;
                    case 1: shifted_key = v; break;
                    case 2: base_key = v; break;
                    }
                } else if (param_idx == 1) {
                    /* Modifiers parameter (modifiers:event_type) */
                    switch (subparam_idx) {
                    case 0: modifiers_raw = (int)v; break;
                    case 1: event_type = (int)v; break;
                    }
                } else if (param_idx == 2) {
                    /* Text parameter */
                    text_codepoint = v;
                }

                value = 0;
                has_value = false;
            }

            if (c == ';') {
                param_idx++;
                subparam_idx = 0;
            } else if (c == ':') {
                subparam_idx++;
            }
        }
    }

    /* Ignore release events - we only care about press/repeat */
    if (event_type == KITTY_EVENT_RELEASE) {
        return 0;
    }

    /* Decode modifiers (Kitty uses 1 + modifier bits) */
    uint16_t mods = decode_modifiers(modifiers_raw);

    /* Suppress shifted key and base key - only used for key identification */
    (void)shifted_key;
    (void)base_key;
    (void)text_codepoint;

    atomic_fetch_add(&p->sequences_parsed, 1);

    /* Map Kitty functional key codes to internal SPECIAL_* codes */
    switch (key_code) {
    /* Navigation keys */
    case KITTY_KEY_UP:       emit_special(p, out, SPECIAL_UP, mods); return 1;
    case KITTY_KEY_DOWN:     emit_special(p, out, SPECIAL_DOWN, mods); return 1;
    case KITTY_KEY_LEFT:     emit_special(p, out, SPECIAL_LEFT, mods); return 1;
    case KITTY_KEY_RIGHT:    emit_special(p, out, SPECIAL_RIGHT, mods); return 1;
    case KITTY_KEY_HOME:     emit_special(p, out, SPECIAL_HOME, mods); return 1;
    case KITTY_KEY_END:      emit_special(p, out, SPECIAL_END, mods); return 1;
    case KITTY_KEY_PAGEUP:   emit_special(p, out, SPECIAL_PAGEUP, mods); return 1;
    case KITTY_KEY_PAGEDOWN: emit_special(p, out, SPECIAL_PAGEDOWN, mods); return 1;
    case KITTY_KEY_INSERT:   emit_special(p, out, SPECIAL_INSERT, mods); return 1;
    case KITTY_KEY_DELETE:   emit_special(p, out, SPECIAL_DELETE, mods); return 1;

    /* Function keys */
    case KITTY_KEY_F1:  emit_special(p, out, SPECIAL_F1, mods); return 1;
    case KITTY_KEY_F2:  emit_special(p, out, SPECIAL_F2, mods); return 1;
    case KITTY_KEY_F3:  emit_special(p, out, SPECIAL_F3, mods); return 1;
    case KITTY_KEY_F4:  emit_special(p, out, SPECIAL_F4, mods); return 1;
    case KITTY_KEY_F5:  emit_special(p, out, SPECIAL_F5, mods); return 1;
    case KITTY_KEY_F6:  emit_special(p, out, SPECIAL_F6, mods); return 1;
    case KITTY_KEY_F7:  emit_special(p, out, SPECIAL_F7, mods); return 1;
    case KITTY_KEY_F8:  emit_special(p, out, SPECIAL_F8, mods); return 1;
    case KITTY_KEY_F9:  emit_special(p, out, SPECIAL_F9, mods); return 1;
    case KITTY_KEY_F10: emit_special(p, out, SPECIAL_F10, mods); return 1;
    case KITTY_KEY_F11: emit_special(p, out, SPECIAL_F11, mods); return 1;
    case KITTY_KEY_F12: emit_special(p, out, SPECIAL_F12, mods); return 1;

    /* Special character keys (emit as chars, let caller handle) */
    case KITTY_KEY_ESCAPE:
        emit_char(p, out, 0x1B, mods);
        return 1;
    case KITTY_KEY_ENTER:
        emit_char(p, out, '\r', mods);
        return 1;
    case KITTY_KEY_TAB:
        if (mods & MOD_SHIFT) {
            emit_special(p, out, SPECIAL_BACKTAB, mods & (uint16_t)~MOD_SHIFT);
        } else {
            emit_char(p, out, '\t', mods);
        }
        return 1;
    case KITTY_KEY_BACKSPACE:
        emit_char(p, out, 0x7F, mods);
        return 1;

    default:
        /* Regular Unicode character */
        if (key_code > 0 && key_code < 0x110000) {
            emit_char(p, out, key_code, mods);
            return 1;
        }
        break;
    }

    /* Unknown Kitty key code */
    out->type = KEY_CSI_UNKNOWN;
    out->data = p->csi_buf;
    out->data_len = p->csi_len;
    return 1;
}

/*
 * Parse SGR 1006 extended mouse sequences: CSI < Cb ; Cx ; Cy M/m
 *
 * SGR 1006 format:
 * - Cb: Button code with modifiers
 *   - Bits 0-1: Button (0=left, 1=middle, 2=right, 3=release)
 *   - Bit 2: Shift
 *   - Bit 3: Alt/Meta
 *   - Bit 4: Ctrl
 *   - Bit 5: Motion (drag)
 *   - Bits 6-7: Extended buttons (64=scroll up, 65=scroll down, etc.)
 * - Cx, Cy: 1-indexed coordinates
 * - M: Press/drag, m: Release
 *
 * Examples:
 *   CSI < 0 ; 10 ; 5 M   - Left button press at (10,5)
 *   CSI < 0 ; 10 ; 5 m   - Left button release at (10,5)
 *   CSI < 32 ; 11 ; 5 M  - Left button drag to (11,5)
 *   CSI < 64 ; 10 ; 5 M  - Scroll up at (10,5)
 *   CSI < 65 ; 10 ; 5 M  - Scroll down at (10,5)
 */
static int parse_sgr_mouse(input_parser_t *p, uint8_t final, input_key_event_t *out) {
    int params[3] = {0};
    int count = parse_csi_params(p, params, 3);

    if (count < 3) {
        /* Need at least button, x, y */
        atomic_fetch_add(&p->parse_errors, 1);
        return 0;
    }

    int cb = params[0];
    int x = params[1] - 1;  /* Convert from 1-indexed to 0-indexed */
    int y = params[2] - 1;

    /* Clamp coordinates to non-negative */
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    /* Extract button (bits 0-1 + bits 6-7 for extended) */
    uint8_t button = (uint8_t)((cb & 0x03) | ((cb & 0xC0) >> 4));

    /* Extract modifiers from button code (bits 2-4) */
    uint16_t mods = MOD_NONE;
    if (cb & 0x04) mods |= MOD_SHIFT;
    if (cb & 0x08) mods |= MOD_ALT;
    if (cb & 0x10) mods |= MOD_CTRL;

    /* Determine action:
     * - 'm' final = release
     * - 'M' final with bit 5 set = drag/motion
     * - 'M' final without bit 5 = press
     */
    uint8_t action;
    if (final == 'm') {
        action = 1;  /* MOUSE_RELEASE */
    } else if (cb & 0x20) {
        action = 2;  /* MOUSE_DRAG */
    } else {
        action = 0;  /* MOUSE_PRESS */
    }

    /* Fill output event */
    out->type = KEY_MOUSE;
    out->mouse_button = button;
    out->mouse_x = (uint16_t)x;
    out->mouse_y = (uint16_t)y;
    out->modifiers = mods;
    out->code = action;  /* Store action in code field */

    atomic_fetch_add(&p->sequences_parsed, 1);
    return 1;
}

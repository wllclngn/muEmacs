/*
 * vim_find_char.c - Vim f/F/t/T Character Find Commands
 *
 * Implements character search within line:
 * - f{char} - Find char forward
 * - F{char} - Find char backward
 * - t{char} - Till char forward (stop before)
 * - T{char} - Till char backward (stop after)
 * - ; - Repeat last f/F/t/T
 * - , - Repeat last f/F/t/T in opposite direction
 *
 * C23 compliant
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "editor_mode.h"
#include "util/logger.h"

/*
 * Core find char implementation
 */
static int vim_find_char_impl(char cmd, char ch, int count) {
    if (ch == 0) return false;

    int direction = (cmd == 'f' || cmd == 't') ? 1 : -1;
    int till_offset = (cmd == 't' || cmd == 'T') ? 1 : 0;

    /* Store for repeat with ; and , */
    g_vim_state.last_find_char = ch;
    g_vim_state.last_find_cmd = cmd;

    struct line *lp = curwp->w_dotp;
    int line_len = llength(lp);
    int start_offset = curwp->w_doto;
    int found_pos = -1;

    for (int i = 0; i < count; i++) {
        found_pos = -1;

        if (direction > 0) {
            /* Search forward */
            for (int pos = start_offset + 1; pos < line_len; pos++) {
                if (lgetc(lp, pos) == ch) {
                    found_pos = pos;
                    break;
                }
            }
        } else {
            /* Search backward */
            for (int pos = start_offset - 1; pos >= 0; pos--) {
                if (lgetc(lp, pos) == ch) {
                    found_pos = pos;
                    break;
                }
            }
        }

        if (found_pos < 0) {
            /* Character not found */
            return false;
        }

        start_offset = found_pos;
    }

    /* Apply till offset */
    if (till_offset) {
        found_pos -= direction;
        if (found_pos < 0 || found_pos >= line_len) {
            return false;
        }
    }

    /* Move cursor */
    curwp->w_doto = found_pos;
    curwp->w_flag |= WFMOVE;
    return true;
}

/*
 * Execute operator on find char result (mark already set at start)
 */
static int vim_find_char_operator(char op, bool inclusive) {
    if (inclusive) {
        /* Include the character under cursor (for f, not t) */
        move_char_forward(false, 1);
    }

    /* Perform the operation */
    if (op == 'd' || op == 'c') {
        region_kill(false, 1);
        if (op == 'c') {
            vim_enter_insert_mode(false, 1);
        }
    } else if (op == 'y') {
        region_copy(false, 1);
        swapmark(false, 0);  /* Return to start */
        mlwrite("[Yanked]");
    }
    return true;
}

/*
 * f - find char forward
 */
int vim_find_char_forward(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    char pending = g_vim_state.pending_op;

    LOG_DEBUGF("VIM_FIND_CHAR_FORWARD: entering, count=%d pending=%d", count, pending);
    mlwrite("[f-]");
    int ch = vim_getkey();
    LOG_DEBUGF("VIM_FIND_CHAR_FORWARD: got ch=0x%02x ('%c')", ch, (ch >= 32 && ch < 127) ? ch : '?');

    if (pending) {
        /* Operator pending - set mark at start, find char, execute op */
        setmark(false, 0);
        int result = vim_find_char_impl('f', ch, count);
        vim_clear_pending_state();
        if (result) {
            return vim_find_char_operator(pending, true);  /* inclusive */
        }
        return false;
    }

    int result = vim_find_char_impl('f', ch, count);
    LOG_DEBUGF("VIM_FIND_CHAR_FORWARD: impl returned %d, cursor now at doto=%d", result, curwp->w_doto);
    vim_clear_pending_state();
    return result;
}

/*
 * F - find char backward
 */
int vim_find_char_backward(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    char pending = g_vim_state.pending_op;

    mlwrite("[F-]");
    int ch = vim_getkey();

    if (pending) {
        setmark(false, 0);
        int result = vim_find_char_impl('F', ch, count);
        vim_clear_pending_state();
        if (result) {
            /* Backward motion: swap mark and point so region is correct */
            swapmark(false, 0);
            return vim_find_char_operator(pending, true);  /* inclusive */
        }
        return false;
    }

    int result = vim_find_char_impl('F', ch, count);
    vim_clear_pending_state();
    return result;
}

/*
 * t - till char forward (stop before char)
 */
int vim_till_char_forward(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    char pending = g_vim_state.pending_op;

    mlwrite("[t-]");
    int ch = vim_getkey();

    if (pending) {
        setmark(false, 0);
        int result = vim_find_char_impl('t', ch, count);
        vim_clear_pending_state();
        if (result) {
            /* Till motion: include up to but not including target */
            move_char_forward(false, 1);  /* Move past the stopping point */
            return vim_find_char_operator(pending, false);  /* not inclusive */
        }
        return false;
    }

    int result = vim_find_char_impl('t', ch, count);
    vim_clear_pending_state();
    return result;
}

/*
 * T - till char backward (stop after char)
 */
int vim_till_char_backward(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    char pending = g_vim_state.pending_op;

    mlwrite("[T-]");
    int ch = vim_getkey();

    if (pending) {
        setmark(false, 0);
        int result = vim_find_char_impl('T', ch, count);
        vim_clear_pending_state();
        if (result) {
            swapmark(false, 0);
            return vim_find_char_operator(pending, false);
        }
        return false;
    }

    int result = vim_find_char_impl('T', ch, count);
    vim_clear_pending_state();
    return result;
}

/*
 * ; - repeat last f/F/t/T
 */
int vim_repeat_find(int f, int n) {
    (void)f; (void)n;

    if (g_vim_state.last_find_char == 0) {
        mlwrite("[No previous find]");
        return false;
    }

    int count = vim_get_effective_count();
    int result = vim_find_char_impl(g_vim_state.last_find_cmd,
                                     g_vim_state.last_find_char, count);
    vim_clear_pending_state();
    return result;
}

/*
 * , - repeat last f/F/t/T in opposite direction
 */
int vim_repeat_find_reverse(int f, int n) {
    (void)f; (void)n;

    if (g_vim_state.last_find_char == 0) {
        mlwrite("[No previous find]");
        return false;
    }

    /* Reverse the command */
    char reversed_cmd;
    switch (g_vim_state.last_find_cmd) {
        case 'f': reversed_cmd = 'F'; break;
        case 'F': reversed_cmd = 'f'; break;
        case 't': reversed_cmd = 'T'; break;
        case 'T': reversed_cmd = 't'; break;
        default: return false;
    }

    int count = vim_get_effective_count();
    int result = vim_find_char_impl(reversed_cmd,
                                     g_vim_state.last_find_char, count);
    vim_clear_pending_state();
    return result;
}

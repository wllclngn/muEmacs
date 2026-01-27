#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "keymap.h"
#include "editor_mode.h"
#include "line.h"
#include "utf8.h"
#include "terminal/terminal.h"
#include "terminal/input_state.h"
#include "util/logger.h"
#include "internal/memory.h"
#include <time.h>

/* Focus state for Alt+Tab cycling */
typedef enum {
    FOCUS_EDITOR,
    FOCUS_TERMINAL,
    FOCUS_MINIBUFFER
} focus_state_t;

static focus_state_t current_focus = FOCUS_EDITOR;

/* Forward declarations for internal (static) helper functions */
static int vim_motion_with_operator(int (*motion)(int, int));
static int vim_get_word_under_cursor(void);
static void vim_record_change(char op, char motion, int count, int linewise);
static void vim_record_replace_change(char ch, int count);
static int vim_block_delete(int f, int n);
static int vim_block_yank(int f, int n);

/* Helper: get next key character using unified parser
 * Non-static - shared with vim_find_char.c */
int vim_getkey(void) {
    input_key_event_t evt;
    LOG_DEBUGF("VIM_GETKEY: waiting for input...");
    int ret = input_read_event(&evt);
    LOG_DEBUGF("VIM_GETKEY: input_read_event returned %d", ret);
    if (ret < 0) return -1;
    int ch = evt_char(&evt);
    LOG_DEBUGF("VIM_GETKEY: evt_char returned 0x%02x ('%c')", ch, (ch >= 32 && ch < 127) ? ch : '?');
    return ch;
}

/*
 * cmd_focus_cycle - Cycle focus between editor and terminal
 *
 * Alt+Tab cycles: editor -> terminal -> editor
 * (minibuffer cycling can be added later if needed)
 */
int cmd_focus_cycle(int f, int n) {
    (void)f; (void)n;
    mlwrite("[Focus cycling disabled - Terminal removed]");
    return true;
}

/* Reset focus to editor (called when terminal closes) */
void focus_reset_to_editor(void) {
    current_focus = FOCUS_EDITOR;
}

/* Vim Navigation Wrappers - with count and operator support */

int vim_move_left(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(move_char_backward);
}

int vim_move_down(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(cursor_down);
}

int vim_move_up(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(cursor_up);
}

int vim_move_right(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(move_char_forward);
}

int vim_word_forward(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(move_word_forward);
}

int vim_word_backward(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(move_word_backward);
}

int vim_word_end(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(vim_end_of_word);
}

int vim_line_start(int f, int n) {
    (void)f; (void)n;
    LOG_DEBUG("vim_line_start: called (H or 0)");
    return vim_motion_with_operator(goto_line_start);
}

int vim_line_end(int f, int n) {
    (void)f; (void)n;
    LOG_DEBUG("vim_line_end: called (L or $)");
    return vim_motion_with_operator(goto_line_end);
}

int vim_first_nonblank_wrapped(int f, int n) {
    (void)f; (void)n;
    return vim_motion_with_operator(vim_first_nonblank);
}

int vim_enter_insert_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Entering Insert Mode");
    atomic_store(&g_vim_state.current_mode, MODE_INSERT);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    mlwrite("[EVIL: INSERT]");
    return true;
}

/* a: Append after cursor */
int vim_append(int f, int n) {
    (void)f; (void)n;
    /* Move right one char (unless at end of line), then enter insert */
    if (curwp->w_doto < llength(curwp->w_dotp))
        curwp->w_doto++;
    return vim_enter_insert_mode(false, 1);
}

/* A: Append at end of line */
int vim_append_eol(int f, int n) {
    (void)f; (void)n;
    goto_line_end(false, 1);
    return vim_enter_insert_mode(false, 1);
}

/* I: Insert at first non-blank */
int vim_insert_bol(int f, int n) {
    (void)f; (void)n;
    vim_first_nonblank(false, 1);
    return vim_enter_insert_mode(false, 1);
}

int vim_enter_normal_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Entering Normal Mode");
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    mlwrite("[EVIL: NORMAL]");
    return true;
}

/* External entry point for ESC handling from bind.c */
int vim_enter_normal_mode_external(int f, int n) {
    return vim_enter_normal_mode(f, n);
}

/* Vim Motion Commands */

/* Move to end of word (Vim 'e') */
int vim_end_of_word(int f, int n) {
    (void)f;
    if (n < 1) n = 1;

    for (int i = 0; i < n; i++) {
        /* Skip any whitespace first */
        while (!curwp->w_doto && curwp->w_dotp != curbp->b_linep) {
            /* At line start, just use move_word_forward */
            break;
        }

        /* Move forward one char to get off current word boundary */
        if (curwp->w_doto < llength(curwp->w_dotp)) {
            move_char_forward(false, 1);
        }

        /* Skip whitespace/non-word chars */
        while (curwp->w_dotp != curbp->b_linep) {
            if (curwp->w_doto >= llength(curwp->w_dotp)) {
                /* End of line, move to next */
                cursor_down(false, 1);
                goto_line_start(false, 0);
                continue;
            }
            int c = lgetc(curwp->w_dotp, curwp->w_doto);
            if (c != ' ' && c != '\t') break;
            move_char_forward(false, 1);
        }

        /* Now move to end of word (until whitespace or end of line) */
        while (curwp->w_dotp != curbp->b_linep) {
            if (curwp->w_doto >= llength(curwp->w_dotp)) break;
            int c = lgetc(curwp->w_dotp, curwp->w_doto);
            if (c == ' ' || c == '\t') {
                move_char_backward(false, 1);  /* Back up to last word char */
                break;
            }
            if (curwp->w_doto + 1 >= llength(curwp->w_dotp)) break;
            move_char_forward(false, 1);
        }
    }
    return true;
}

/* Move to first non-blank character (Vim '^') */
int vim_first_nonblank(int f, int n) {
    (void)f; (void)n;
    goto_line_start(false, 0);

    while (curwp->w_doto < llength(curwp->w_dotp)) {
        int c = lgetc(curwp->w_dotp, curwp->w_doto);
        if (c != ' ' && c != '\t') break;
        move_char_forward(false, 1);
    }
    return true;
}

/* Open line below, stay in normal mode (user .vimrc: o → o<Esc>) */
int vim_open_line_below(int f, int n) {
    (void)f; (void)n;
    goto_line_end(false, 0);
    openline(false, 1);
    cursor_down(false, 1);
    return true;
}

/* Open line above, stay in normal mode (user .vimrc: O → O<Esc>) */
int vim_open_line_above(int f, int n) {
    (void)f; (void)n;
    goto_line_start(false, 0);
    openline(false, 1);
    return true;
}

/* Paste and move right (user .vimrc: p → Pl) */
static int vim_paste(int f, int n) {
    (void)f; (void)n;
    yank(false, 1);
    move_char_forward(false, 1);
    return true;
}

/* Delete character under cursor (Vim 'x') - respects count */
int vim_delete_char(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();
    vim_record_change('x', 0, count, 0);
    return delete_char_forward(true, count);
}

/*
 * Count Prefix and Operator-Pending State Machine
 *
 * Implements Vim's grammar:
 *   [count]motion      - repeat motion count times (5j, 3w)
 *   [count]operator[count]motion - operator over motion (2d3w)
 *   operator operator  - operate on whole line (dd, cc, yy)
 */

/* Clear pending state after operation completes
 * Non-static - shared with vim_find_char.c */
void vim_clear_pending_state(void) {
    g_vim_state.count = 0;
    g_vim_state.count_given = 0;
    g_vim_state.pending_op = 0;
    g_vim_state.op_count = 0;
}

/* Get effective count: multiply op_count and count, default to 1
 * Non-static - shared with vim_find_char.c */
int vim_get_effective_count(void) {
    int op_count = g_vim_state.op_count > 0 ? g_vim_state.op_count : 1;
    int count = g_vim_state.count > 0 ? g_vim_state.count : 1;
    return op_count * count;
}

/* Digit key handler: accumulate count prefix */
static int vim_digit(int f, int n) {
    (void)f; (void)n;
    /* This is called for digits 1-9 (and 0 after first digit) */
    /* The actual digit is passed via n */
    int digit = n;

    if (g_vim_state.count == 0 && digit == 0) {
        /* '0' at start is goto_line_start, not count */
        vim_clear_pending_state();
        return goto_line_start(false, 1);
    }

    g_vim_state.count = g_vim_state.count * 10 + digit;
    g_vim_state.count_given = 1;

    /* Show count in minibuffer */
    if (g_vim_state.pending_op) {
        mlwrite("[%c%d-]", g_vim_state.pending_op, g_vim_state.count);
    } else {
        mlwrite("[%d-]", g_vim_state.count);
    }
    return true;
}

/* Individual digit commands (bound to keys) */
int vim_digit_1(int f, int n) { (void)f; (void)n; return vim_digit(true, 1); }
int vim_digit_2(int f, int n) { (void)f; (void)n; return vim_digit(true, 2); }
int vim_digit_3(int f, int n) { (void)f; (void)n; return vim_digit(true, 3); }
int vim_digit_4(int f, int n) { (void)f; (void)n; return vim_digit(true, 4); }
int vim_digit_5(int f, int n) { (void)f; (void)n; return vim_digit(true, 5); }
int vim_digit_6(int f, int n) { (void)f; (void)n; return vim_digit(true, 6); }
int vim_digit_7(int f, int n) { (void)f; (void)n; return vim_digit(true, 7); }
int vim_digit_8(int f, int n) { (void)f; (void)n; return vim_digit(true, 8); }
int vim_digit_9(int f, int n) { (void)f; (void)n; return vim_digit(true, 9); }
int vim_digit_0(int f, int n) { (void)f; (void)n; return vim_digit(true, 0); }

/* Execute operation on region between current position and motion result */
static int vim_execute_operator_with_motion(char op, int (*motion)(int, int), int count) {
    LOG_DEBUGF("Vim: execute_operator op='%c' count=%d", op, count);

    /* Set mark at current position */
    setmark(false, 0);

    /* Execute motion count times */
    for (int i = 0; i < count; i++) {
        motion(false, 1);
    }

    /* Perform operation on region */
    enum vim_operator vim_op = OP_NONE;
    if (op == 'd') vim_op = OP_DELETE;
    else if (op == 'c') vim_op = OP_CHANGE;
    else if (op == 'y') vim_op = OP_YANK;

    if (vim_op == OP_DELETE || vim_op == OP_CHANGE) {
        region_kill(false, 1);
        vim_store_to_register(1, 0);  /* Delete, character-wise */
        if (vim_op == OP_CHANGE) {
            vim_enter_insert_mode(false, 1);
        }
    } else if (vim_op == OP_YANK) {
        region_copy(false, 1);
        vim_store_to_register(0, 0);  /* Yank, character-wise */
        /* Restore cursor to original position (start of region) */
        swapmark(false, 0);
        mlwrite("[Yanked]");
    }

    vim_clear_pending_state();
    return true;
}

/* Execute line operation (dd, cc, yy) */
static int vim_execute_line_operation(char op, int count) {
    LOG_DEBUGF("Vim: line operation op='%c' count=%d", op, count);

    /* Record for dot repeat */
    vim_record_change(op, 0, count, 1);

    /* Go to beginning of current line */
    goto_line_start(false, 0);
    setmark(false, 0);

    /* Move down count lines */
    cursor_down(false, count);

    enum vim_operator vim_op = OP_NONE;
    if (op == 'd') vim_op = OP_DELETE;
    else if (op == 'c') vim_op = OP_CHANGE;
    else if (op == 'y') vim_op = OP_YANK;

    if (vim_op == OP_DELETE || vim_op == OP_CHANGE) {
        region_kill(false, 1);
        vim_store_to_register(1, 1);  /* Delete, linewise */
        if (vim_op == OP_CHANGE) {
            vim_open_line_above(false, 1);
            vim_enter_insert_mode(false, 1);
        }
    } else if (vim_op == OP_YANK) {
        region_copy(false, 1);
        vim_store_to_register(0, 1);  /* Yank, linewise */
        swapmark(false, 0);
        mlwrite("[%d lines yanked]", count);
    }

    vim_clear_pending_state();
    return true;
}

/* Set operator-pending state */
static int vim_set_operator_pending(char op) {
    if (g_vim_state.pending_op == op) {
        /* Double operator (dd, cc, yy) - operate on lines */
        int count = vim_get_effective_count();
        return vim_execute_line_operation(op, count);
    }

    /* Store count before operator as op_count */
    if (g_vim_state.count_given) {
        g_vim_state.op_count = g_vim_state.count;
        g_vim_state.count = 0;
        g_vim_state.count_given = 0;
    }

    g_vim_state.pending_op = op;
    mlwrite("[%c-]", op);
    return true;
}

/* Motion wrapper that handles operator-pending state */
static int vim_motion_with_operator(int (*motion)(int, int)) {
    int count = vim_get_effective_count();

    LOG_DEBUGF("vim_motion_with_operator: count=%d, pending_op=%c",
               count, g_vim_state.pending_op ? g_vim_state.pending_op : '0');

    if (g_vim_state.pending_op) {
        /* Execute operator with this motion */
        char op = g_vim_state.pending_op;
        return vim_execute_operator_with_motion(op, motion, count);
    }

    /* No operator pending - just execute motion count times */
    int result = true;
    for (int i = 0; i < count && result; i++) {
        LOG_DEBUGF("vim_motion_with_operator: calling motion i=%d", i);
        result = motion(false, 1);
        LOG_DEBUGF("vim_motion_with_operator: motion returned %d", result);
    }
    vim_clear_pending_state();
    return result;
}

/* Delete operator (d{motion} or dd) */
int vim_delete_operator(int f, int n) {
    (void)f; (void)n;
    return vim_set_operator_pending('d');
}

/* Change operator (c{motion} or cc) */
int vim_change_operator(int f, int n) {
    (void)f; (void)n;
    return vim_set_operator_pending('c');
}

/* Yank operator (y{motion} or yy) */
int vim_yank_operator(int f, int n) {
    (void)f; (void)n;
    return vim_set_operator_pending('y');
}

/*
 * Visual Mode
 */

/* Enter visual mode (character-wise) */
int vim_enter_visual_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Entering Visual Mode");
    atomic_store(&g_vim_state.current_mode, MODE_VISUAL);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    setmark(false, 0);  /* Set mark at current position (anchor) */
    mlwrite("[EVIL: VISUAL]");
    return true;
}

/* Enter visual line mode */
int vim_enter_visual_line_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Entering Visual Line Mode");
    atomic_store(&g_vim_state.current_mode, MODE_VISUAL_LINE);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    goto_line_start(false, 0);  /* Start from beginning of line */
    setmark(false, 0);
    mlwrite("[EVIL: VISUAL LINE]");
    return true;
}

/* Exit visual mode back to normal */
int vim_exit_visual_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Exiting Visual Mode");
    atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    /* Clear mark to stop highlighting */
    curwp->w_markp = nullptr;
    curwp->w_marko = 0;
    curwp->w_flag |= WFHARD;
    mlwrite("[EVIL: NORMAL]");
    return true;
}

/* Helper: Normalize mark/cursor to cover full lines for linewise operations.
 * Saves both endpoints before manipulation, determines line order, then
 * sets up cursor at start of first line and mark at start of line after last.
 * Returns true if region was set up correctly. */
static bool vim_setup_linewise_region(void)
{
    if (curwp->w_markp == nullptr) return false;

    struct line *mark_line = curwp->w_markp;
    struct line *dot_line = curwp->w_dotp;

    /* Same line case - just expand to full line */
    if (mark_line == dot_line) {
        curwp->w_doto = 0;
        struct line *next = lforw(dot_line);
        if (next == curbp->b_linep) {
            /* Last line in buffer - mark at end + 1 for newline */
            curwp->w_markp = dot_line;
            curwp->w_marko = llength(dot_line);
        } else {
            curwp->w_markp = next;
            curwp->w_marko = 0;
        }
        return true;
    }

    /* Multi-line: determine which line comes first by scanning buffer */
    struct line *start_line, *end_line;
    bool mark_first = false;

    for (struct line *scan = lforw(curbp->b_linep);
         scan != curbp->b_linep; scan = lforw(scan)) {
        if (scan == mark_line) { mark_first = true; break; }
        if (scan == dot_line) { mark_first = false; break; }
    }

    if (mark_first) {
        start_line = mark_line;
        end_line = dot_line;
    } else {
        start_line = dot_line;
        end_line = mark_line;
    }

    /* Set cursor to beginning of first line */
    curwp->w_dotp = start_line;
    curwp->w_doto = 0;

    /* Set mark to beginning of line AFTER last line */
    struct line *after_end = lforw(end_line);
    if (after_end == curbp->b_linep) {
        /* end_line is last line in buffer - mark at end of it */
        curwp->w_markp = end_line;
        curwp->w_marko = llength(end_line);
    } else {
        curwp->w_markp = after_end;
        curwp->w_marko = 0;
    }
    return true;
}

/* Delete selection in visual mode */
int vim_visual_delete(int f, int n) {
    (void)f; (void)n;
    enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
    int linewise = (mode == MODE_VISUAL_LINE);

    LOG_DEBUGF("VIM_VISUAL_DELETE: mode=%d linewise=%d", mode, linewise);
    LOG_DEBUGF("VIM_VISUAL_DELETE: mark=%p marko=%d dot=%p doto=%d",
               (void*)curwp->w_markp, curwp->w_marko,
               (void*)curwp->w_dotp, curwp->w_doto);

    if (linewise) {
        LOG_DEBUG("VIM_VISUAL_DELETE: calling vim_setup_linewise_region");
        vim_setup_linewise_region();
    } else if (mode == MODE_VISUAL_BLOCK) {
        LOG_DEBUG("VIM_VISUAL_DELETE: calling vim_block_delete");
        return vim_block_delete(f, n);
    } else {
        /* MODE_VISUAL (char-wise): Vim visual is inclusive on both ends,
         * but emacs region_kill is exclusive at cursor. Move cursor +1
         * to include the character under cursor in the deletion. */
        if (curwp->w_doto < llength(curwp->w_dotp)) {
            curwp->w_doto++;
        }
    }

    LOG_DEBUG("VIM_VISUAL_DELETE: calling region_kill");
    region_kill(false, 1);
    LOG_DEBUG("VIM_VISUAL_DELETE: calling vim_store_to_register");
    vim_store_to_register(1, linewise);  /* Delete */
    LOG_DEBUG("VIM_VISUAL_DELETE: calling vim_exit_visual_mode");
    vim_exit_visual_mode(false, 1);
    LOG_DEBUG("VIM_VISUAL_DELETE: complete");
    return true;
}

/* Yank selection in visual mode */
int vim_visual_yank(int f, int n) {
    (void)f; (void)n;
    enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
    int linewise = (mode == MODE_VISUAL_LINE);

    if (linewise) {
        vim_setup_linewise_region();
    } else if (mode == MODE_VISUAL_BLOCK) {
        return vim_block_yank(f, n);
    } else {
        /* MODE_VISUAL (char-wise): Vim visual is inclusive on both ends */
        if (curwp->w_doto < llength(curwp->w_dotp)) {
            curwp->w_doto++;
        }
    }

    region_copy(false, 1);
    vim_store_to_register(0, linewise);  /* Yank */
    vim_exit_visual_mode(false, 1);
    return true;
}

/* Change selection in visual mode */
int vim_visual_change(int f, int n) {
    (void)f; (void)n;
    vim_visual_delete(false, 1);
    vim_enter_insert_mode(false, 1);
    return true;
}

/*
 * Visual Block Mode (Ctrl-V)
 */

/* Enter visual block mode */
int vim_enter_visual_block_mode(int f, int n) {
    (void)f; (void)n;
    LOG_INFO("Vim: Entering Visual Block Mode");
    atomic_store(&g_vim_state.current_mode, MODE_VISUAL_BLOCK);
    g_vim_state.block_start_col = getccol(false);  /* Save starting virtual column */
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    setmark(false, 0);  /* Set mark at current position (anchor) */
    mlwrite("[EVIL: VISUAL BLOCK]");
    return true;
}

/* Helper: Get byte offset for a virtual column on a line */
static int col_to_offset(struct line *lp, int vcol)
{
    int len = llength(lp);
    int offset = 0;
    int col = 0;

    while (offset < len && col < vcol) {
        int ch = lgetc(lp, offset);
        if (ch == '\t') {
            col = (col / 8 + 1) * 8;  /* Tab stops at multiples of 8 */
        } else {
            col++;
        }
        offset++;
    }
    return offset;
}

/* Delete block selection - removes rectangular region from each line */
static int vim_block_delete(int f, int n) {
    (void)f; (void)n;

    if (curwp->w_markp == nullptr) {
        mlwrite("NO MARK SET");
        return false;
    }

    struct line *mark_line = curwp->w_markp;
    struct line *dot_line = curwp->w_dotp;

    /* Determine line range */
    struct line *start_line, *end_line;
    bool mark_first = false;

    if (mark_line == dot_line) {
        start_line = end_line = mark_line;
    } else {
        for (struct line *scan = lforw(curbp->b_linep);
             scan != curbp->b_linep; scan = lforw(scan)) {
            if (scan == mark_line) { mark_first = true; break; }
            if (scan == dot_line) { mark_first = false; break; }
        }
        if (mark_first) {
            start_line = mark_line;
            end_line = dot_line;
        } else {
            start_line = dot_line;
            end_line = mark_line;
        }
    }

    /* Get column range */
    int cur_col = getccol(false);
    int left_col = (g_vim_state.block_start_col < cur_col) ?
                   g_vim_state.block_start_col : cur_col;
    int right_col = (g_vim_state.block_start_col > cur_col) ?
                    g_vim_state.block_start_col : cur_col;

    /* Delete block from each line */
    for (struct line *lp = start_line; ; lp = lforw(lp)) {
        int len = llength(lp);
        int start_byte = col_to_offset(lp, left_col);
        int end_byte = col_to_offset(lp, right_col + 1);

        if (start_byte < len && end_byte > start_byte) {
            /* Position cursor on this line and delete the range */
            curwp->w_dotp = lp;
            curwp->w_doto = start_byte;
            int delete_count = (end_byte <= len) ? (end_byte - start_byte) :
                                                   (len - start_byte);
            if (delete_count > 0) {
                ldelete((long)delete_count, false);
            }
        }

        if (lp == end_line) break;
    }

    /* Position cursor at top-left of deleted block */
    curwp->w_dotp = start_line;
    curwp->w_doto = col_to_offset(start_line, left_col);

    lchange(WFHARD);
    vim_exit_visual_mode(false, 1);
    return true;
}

/* Yank block selection - copies rectangular region to kill buffer */
static int vim_block_yank(int f, int n) {
    (void)f; (void)n;

    if (curwp->w_markp == nullptr) {
        mlwrite("NO MARK SET");
        return false;
    }

    struct line *mark_line = curwp->w_markp;
    struct line *dot_line = curwp->w_dotp;

    /* Determine line range */
    struct line *start_line, *end_line;
    bool mark_first = false;

    if (mark_line == dot_line) {
        start_line = end_line = mark_line;
    } else {
        for (struct line *scan = lforw(curbp->b_linep);
             scan != curbp->b_linep; scan = lforw(scan)) {
            if (scan == mark_line) { mark_first = true; break; }
            if (scan == dot_line) { mark_first = false; break; }
        }
        if (mark_first) {
            start_line = mark_line;
            end_line = dot_line;
        } else {
            start_line = dot_line;
            end_line = mark_line;
        }
    }

    /* Get column range */
    int cur_col = getccol(false);
    int left_col = (g_vim_state.block_start_col < cur_col) ?
                   g_vim_state.block_start_col : cur_col;
    int right_col = (g_vim_state.block_start_col > cur_col) ?
                    g_vim_state.block_start_col : cur_col;

    /* Clear kill buffer and copy block */
    kdelete();

    for (struct line *lp = start_line; ; lp = lforw(lp)) {
        int len = llength(lp);
        int start_byte = col_to_offset(lp, left_col);
        int end_byte = col_to_offset(lp, right_col + 1);

        /* Copy characters from this line */
        for (int i = start_byte; i < end_byte && i < len; i++) {
            kinsert(lgetc(lp, i));
        }
        /* Pad with spaces if line is shorter than selection */
        for (int i = len; i < end_byte; i++) {
            kinsert(' ');
        }

        if (lp == end_line) break;
        kinsert('\n');  /* Newline between block rows */
    }

    mlwrite("[BLOCK YANKED]");
    vim_exit_visual_mode(false, 1);
    return true;
}

/* Note: f/F/t/T character motions moved to vim_find_char.c */

/*
 * Miscellaneous Commands: r, R, J, ~
 */

/* r{char}: Replace character under cursor */
int vim_replace_char(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    mlwrite("[r-]");
    int ch = vim_getkey();

    if (ch < 32 && ch != '\t' && ch != '\n') {
        /* Ignore control chars except tab and newline */
        return false;
    }

    /* Record for dot repeat */
    vim_record_replace_change(ch, count);

    /* Replace count characters */
    for (int i = 0; i < count; i++) {
        if (curwp->w_doto >= llength(curwp->w_dotp)) {
            break;  /* Don't go past end of line */
        }

        /* Get UTF-8 byte length of character under cursor */
        int byte_len = 1;
        int remaining = llength(curwp->w_dotp) - curwp->w_doto;
        if (remaining > 0) {
            /* Read bytes into buffer to determine UTF-8 length */
            char buf[6];  /* Max UTF-8 sequence is 4 bytes, extra for safety */
            int to_read = (remaining > 6) ? 6 : remaining;
            for (int j = 0; j < to_read; j++) {
                buf[j] = (char)lgetc(curwp->w_dotp, curwp->w_doto + j);
            }
            unicode_t uc;
            byte_len = utf8_to_unicode(buf, 0, to_read, &uc);
            if (byte_len <= 0) byte_len = 1;  /* Fallback for invalid UTF-8 */
        }

        /* Delete the full UTF-8 character */
        ldelchar(byte_len, false);
        if (ch == '\n') {
            openline(false, 1);
        } else {
            linsert(1, ch);
            move_char_backward(false, 1);  /* Stay on replaced char */
        }

        if (i < count - 1) {
            move_char_forward(false, 1);
        }
    }

    curwp->w_flag |= WFHARD;
    return true;
}

/* R: Enter Replace (overtype) mode */
int vim_enter_replace_mode(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();
    atomic_store(&g_vim_state.current_mode, MODE_REPLACE);
    curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
    mlwrite("[EVIL: REPLACE]");
    return true;
}

/* J: Join current line with next line */
static int vim_join_lines(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    /* Record for dot repeat */
    vim_record_change('J', 0, count, 0);

    /* Join count lines (or 2 if no count given) */
    int lines_to_join = count > 1 ? count : 2;

    for (int i = 1; i < lines_to_join; i++) {
        /* Go to end of current line */
        goto_line_end(false, 1);

        /* If not at end of buffer, delete the newline */
        if (curwp->w_dotp != curbp->b_linep) {
            /* Delete the newline character */
            delete_char_forward(false, 1);

            /* Add a space if next line doesn't start with whitespace */
            if (curwp->w_doto < llength(curwp->w_dotp)) {
                int c = lgetc(curwp->w_dotp, curwp->w_doto);
                if (c != ' ' && c != '\t') {
                    linsert(1, ' ');
                    move_char_backward(false, 1);
                }
            }
        }
    }

    curwp->w_flag |= WFHARD;
    return true;
}

/* ~: Toggle case of character under cursor */
int vim_toggle_case(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    /* Record for dot repeat */
    vim_record_change('~', 0, count, 0);

    for (int i = 0; i < count; i++) {
        if (curwp->w_doto >= llength(curwp->w_dotp)) {
            break;  /* Don't go past end of line */
        }

        int c = lgetc(curwp->w_dotp, curwp->w_doto);
        int newc = c;

        if (c >= 'a' && c <= 'z') {
            newc = c - 32;  /* lowercase to uppercase */
        } else if (c >= 'A' && c <= 'Z') {
            newc = c + 32;  /* uppercase to lowercase */
        }

        if (newc != c) {
            /* Replace the character */
            delete_char_forward(false, 1);
            linsert(1, newc);
        } else {
            move_char_forward(false, 1);  /* Just move forward */
        }
    }

    curwp->w_flag |= WFHARD;
    return true;
}

/*
 * Named Marks: m{a-z}, '{a-z}, `{a-z}
 */

/* m{a-z}: Set named mark at current position */
int vim_set_mark(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();

    mlwrite("[m-]");
    int ch = vim_getkey();

    if (ch >= 'a' && ch <= 'z') {
        int idx = ch - 'a';
        g_vim_state.marks[idx].line = curwp->w_dotp;
        g_vim_state.marks[idx].offset = curwp->w_doto;
        mlwrite("[Mark '%c' set]", ch);
        return true;
    }

    mlwrite("[Invalid mark]");
    return false;
}

/* Helper: Jump to a mark */
static int vim_jump_to_mark(char mark_char, bool exact_position) {
    if (mark_char < 'a' || mark_char > 'z') {
        mlwrite("[Invalid mark]");
        return false;
    }

    int idx = mark_char - 'a';
    struct line *target_line = g_vim_state.marks[idx].line;

    if (target_line == NULL) {
        mlwrite("[Mark '%c' not set]", mark_char);
        return false;
    }

    /* Save current position for '' and `` */
    g_vim_state.last_jump_line = curwp->w_dotp;
    g_vim_state.last_jump_offset = curwp->w_doto;

    /* Find the line in the buffer */
    struct line *lp;
    for (lp = lforw(curbp->b_linep); lp != curbp->b_linep; lp = lforw(lp)) {
        if (lp == target_line) {
            curwp->w_dotp = lp;
            if (exact_position) {
                /* `: go to exact column */
                int offset = g_vim_state.marks[idx].offset;
                if (offset > llength(lp)) {
                    offset = llength(lp);
                }
                curwp->w_doto = offset;
            } else {
                /* ': go to first non-blank on line */
                curwp->w_doto = 0;
                vim_first_nonblank(false, 1);
            }
            curwp->w_flag |= WFMOVE;
            return true;
        }
    }

    mlwrite("[Mark '%c' invalid (line deleted?)]", mark_char);
    g_vim_state.marks[idx].line = NULL;  /* Clear invalid mark */
    return false;
}

/* '{a-z}: Go to mark line (first non-blank) */
int vim_goto_mark_line(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();

    mlwrite("['-]");
    int ch = vim_getkey();

    if (ch == '\'') {
        /* '': Jump to last jump position */
        if (g_vim_state.last_jump_line == NULL) {
            mlwrite("[No previous position]");
            return false;
        }

        /* Swap current and last position */
        struct line *save_line = curwp->w_dotp;
        int save_offset = curwp->w_doto;

        curwp->w_dotp = g_vim_state.last_jump_line;
        curwp->w_doto = 0;
        vim_first_nonblank(false, 1);

        g_vim_state.last_jump_line = save_line;
        g_vim_state.last_jump_offset = save_offset;

        curwp->w_flag |= WFMOVE;
        return true;
    }

    return vim_jump_to_mark(ch, false);
}

/* `{a-z}: Go to mark exact position */
int vim_goto_mark_exact(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();

    mlwrite("[`-]");
    int ch = vim_getkey();

    if (ch == '`') {
        /* ``: Jump to last jump position (exact) */
        if (g_vim_state.last_jump_line == NULL) {
            mlwrite("[No previous position]");
            return false;
        }

        /* Swap current and last position */
        struct line *save_line = curwp->w_dotp;
        int save_offset = curwp->w_doto;

        curwp->w_dotp = g_vim_state.last_jump_line;
        curwp->w_doto = g_vim_state.last_jump_offset;
        if (curwp->w_doto > llength(curwp->w_dotp)) {
            curwp->w_doto = llength(curwp->w_dotp);
        }

        g_vim_state.last_jump_line = save_line;
        g_vim_state.last_jump_offset = save_offset;

        curwp->w_flag |= WFMOVE;
        return true;
    }

    return vim_jump_to_mark(ch, true);
}

/* =========== Search Commands =========== */

/* /: Forward search */
int vim_search_forward(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();
    g_vim_state.last_search_dir = 1;  /* Forward */
    return forwsearch(false, 1);
}

/* ?: Backward search */
int vim_search_backward(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();
    g_vim_state.last_search_dir = -1;  /* Backward */
    return backsearch(false, 1);
}

/* n: Repeat search in same direction */
int vim_search_next(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    if (g_vim_state.last_search_dir == 0) {
        mlwrite("[No previous search]");
        return false;
    }

    int status = true;
    for (int i = 0; i < count && status; i++) {
        if (g_vim_state.last_search_dir > 0) {
            status = forwhunt(false, 1);
        } else {
            status = backhunt(false, 1);
        }
    }
    return status;
}

/* N: Repeat search in opposite direction */
int vim_search_prev(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    if (g_vim_state.last_search_dir == 0) {
        mlwrite("[No previous search]");
        return false;
    }

    int status = true;
    for (int i = 0; i < count && status; i++) {
        if (g_vim_state.last_search_dir > 0) {
            status = backhunt(false, 1);  /* Opposite of forward */
        } else {
            status = forwhunt(false, 1);  /* Opposite of backward */
        }
    }
    return status;
}

/* Helper: Extract word under cursor into pattern buffers */
static int vim_get_word_under_cursor(void) {
    /* Get word under cursor */
    int orig_doto = curwp->w_doto;

    /* Move to start of word if in middle */
    while (curwp->w_doto > 0) {
        move_char_backward(false, 1);
        if (!inword()) {
            move_char_forward(false, 1);
            break;
        }
    }

    int start = curwp->w_doto;

    /* Find end of word */
    while (curwp->w_doto < llength(curwp->w_dotp) && inword()) {
        move_char_forward(false, 1);
    }
    int end = curwp->w_doto;

    /* Restore position */
    curwp->w_doto = orig_doto;

    if (end == start) {
        return 0;  /* No word */
    }

    /* Copy word to search pattern */
    extern char pat[];
    extern char tap[];
    int len = end - start;
    if (len >= NPAT) len = NPAT - 1;
    for (int i = 0; i < len; i++) {
        pat[i] = lgetc(curwp->w_dotp, start + i);
    }
    pat[len] = '\0';

    /* Also set reversed pattern for backhunt */
    rvstrcpy(tap, pat, NPAT);

    return len;
}

/* *: Search forward for word under cursor */
int vim_search_word_forward(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();

    if (vim_get_word_under_cursor() == 0) {
        mlwrite("[No word under cursor]");
        return false;
    }

    /* Move to end of current word first so we find NEXT occurrence */
    while (curwp->w_doto < llength(curwp->w_dotp) && inword()) {
        move_char_forward(false, 1);
    }

    g_vim_state.last_search_dir = 1;
    return forwhunt(false, 1);
}

/* #: Search backward for word under cursor */
int vim_search_word_backward(int f, int n) {
    (void)f; (void)n;
    vim_clear_pending_state();

    if (vim_get_word_under_cursor() == 0) {
        mlwrite("[No word under cursor]");
        return false;
    }

    /* Move to start of current word first so we find PREVIOUS occurrence */
    while (curwp->w_doto > 0 && inword()) {
        move_char_backward(false, 1);
    }
    if (curwp->w_doto > 0 || !inword()) {
        /* Move back one more if we went past the word */
        if (!inword() && curwp->w_doto > 0) {
            move_char_backward(false, 1);
        }
    }

    g_vim_state.last_search_dir = -1;
    return backhunt(false, 1);
}

/* Note: Text objects moved to vim_text_objects.c */

/* =========== Register System =========== */

/* Register index mapping:
 * 0-9: numbered registers (0 = yank, 1-9 = delete stack)
 * 10-35: named registers (a-z)
 * Special: '"' = unnamed (default), '+' = clipboard, '_' = black hole
 */
static int vim_reg_index(char reg) {
    if (reg >= '0' && reg <= '9') return reg - '0';
    if (reg >= 'a' && reg <= 'z') return 10 + (reg - 'a');
    if (reg >= 'A' && reg <= 'Z') return 10 + (reg - 'A');  /* Append mode */
    return 0;  /* Default to register 0 */
}

/* Store text in a register */
static void vim_reg_store(char reg, const char *text, int len, int linewise) {
    if (reg == '_') return;  /* Black hole register - discard */

    int idx = vim_reg_index(reg);
    int append = (reg >= 'A' && reg <= 'Z');

    if (append && g_vim_state.registers[idx].text) {
        /* Append to existing content */
        int old_len = g_vim_state.registers[idx].len;
        int new_len = old_len + len;
        char *new_text = SAFE_ALLOC_SIZED(char, new_len + 1, "vim register append");
        if (new_text) {
            memcpy(new_text, g_vim_state.registers[idx].text, old_len);
            memcpy(new_text + old_len, text, len);
            new_text[new_len] = '\0';
            SAFE_FREE(g_vim_state.registers[idx].text);
            g_vim_state.registers[idx].text = new_text;
            g_vim_state.registers[idx].len = new_len;
            /* Keep linewise flag from original if either is linewise */
            g_vim_state.registers[idx].linewise |= linewise;
        }
    } else {
        /* Replace register content */
        if (g_vim_state.registers[idx].text) {
            SAFE_FREE(g_vim_state.registers[idx].text);
        }
        g_vim_state.registers[idx].text = SAFE_ALLOC_SIZED(char, len + 1, "vim register store");
        if (g_vim_state.registers[idx].text) {
            memcpy(g_vim_state.registers[idx].text, text, len);
            g_vim_state.registers[idx].text[len] = '\0';
            g_vim_state.registers[idx].len = len;
            g_vim_state.registers[idx].linewise = linewise;
        }
    }
}

/* Get register contents */
static char *vim_reg_get(char reg, int *len, int *linewise) {
    int idx = vim_reg_index(reg);
    if (len) *len = g_vim_state.registers[idx].len;
    if (linewise) *linewise = g_vim_state.registers[idx].linewise;
    return g_vim_state.registers[idx].text;
}

/* Store killed/yanked text into appropriate register
 * Non-static - shared with vim_text_objects.c */
void vim_store_to_register(int is_delete, int linewise) {
    /* Get text from temp kill buffer (where kinsert puts it) */
    extern char temp_kill_buf[];
    extern size_t temp_kill_len;

    if (temp_kill_len == 0) return;

    size_t len = temp_kill_len;

    char reg = g_vim_state.pending_register;
    if (reg == 0) reg = '"';  /* Use unnamed register */

    /* Store to selected register */
    vim_reg_store(reg, temp_kill_buf, len, linewise);

    /* Also store to yank register (0) for yanks, or shift delete stack for deletes */
    if (!is_delete) {
        /* Yank: also store to register 0 */
        if (reg != '0' && reg != '"') {
            vim_reg_store('0', temp_kill_buf, len, linewise);
        }
    } else {
        /* Delete: shift registers 1-9 down, put new text in 1 */
        /* (Only for significant deletes, not single chars) */
        if (len > 1 || linewise) {
            /* Free only the evicted register (9) - don't free during shift! */
            SAFE_FREE(g_vim_state.registers[9].text);
            /* Shift registers 8->9, 7->8, ..., 1->2 (no freeing) */
            for (int i = 8; i >= 1; i--) {
                g_vim_state.registers[i + 1] = g_vim_state.registers[i];
            }
            /* Clear register 1 and store new content */
            g_vim_state.registers[1].text = NULL;
            g_vim_state.registers[1].len = 0;
            g_vim_state.registers[1].linewise = 0;
            vim_reg_store('1', temp_kill_buf, len, linewise);
        }
    }

    g_vim_state.pending_register = 0;  /* Clear pending register */
}

/* "{register} prefix handler */
int vim_register_prefix(int f, int n) {
    (void)f; (void)n;

    mlwrite("[\"-]");
    int ch = vim_getkey();

    /* Valid register characters: a-zA-Z, 0-9, ", +, *, _, - */
    if ((ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '"' || ch == '+' || ch == '*' || ch == '_' || ch == '-') {

        g_vim_state.pending_register = ch;
        mlwrite("[\"%c]", ch);
        return true;
    }

    mlwrite("[Invalid register: %c]", ch);
    return false;
}

/* p: Put after cursor */
int vim_put_after(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    char reg = g_vim_state.pending_register;
    if (reg == 0) reg = '"';

    int len, linewise;
    char *text = vim_reg_get(reg, &len, &linewise);

    if (!text || len == 0) {
        mlwrite("[Register %c empty]", reg);
        g_vim_state.pending_register = 0;
        return false;
    }

    /* For linewise paste, move to next line first */
    if (linewise) {
        goto_line_end(false, 0);
        openline(false, 1);
        cursor_down(false, 1);
    } else {
        /* Character-wise: move past current char */
        if (curwp->w_doto < llength(curwp->w_dotp)) {
            move_char_forward(false, 1);
        }
    }

    /* Insert text count times */
    for (int c = 0; c < count; c++) {
        for (int i = 0; i < len; i++) {
            if (text[i] == '\n') {
                openline(false, 1);
                cursor_down(false, 1);
            } else {
                linsert(1, text[i]);
            }
        }
    }

    /* Move back one char for character-wise paste */
    if (!linewise) {
        move_char_backward(false, 1);
    }

    curwp->w_flag |= WFHARD;
    g_vim_state.pending_register = 0;
    mlwrite("[%d chars pasted from \"%c]", len * count, reg);
    return true;
}

/* P: Put before cursor */
int vim_put_before(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    char reg = g_vim_state.pending_register;
    if (reg == 0) reg = '"';

    int len, linewise;
    char *text = vim_reg_get(reg, &len, &linewise);

    if (!text || len == 0) {
        mlwrite("[Register %c empty]", reg);
        g_vim_state.pending_register = 0;
        return false;
    }

    /* For linewise paste, open line above */
    if (linewise) {
        goto_line_start(false, 0);
        openline(false, 1);
    }

    /* Insert text count times */
    for (int c = 0; c < count; c++) {
        for (int i = 0; i < len; i++) {
            if (text[i] == '\n') {
                openline(false, 1);
                cursor_down(false, 1);
            } else {
                linsert(1, text[i]);
            }
        }
    }

    curwp->w_flag |= WFHARD;
    g_vim_state.pending_register = 0;
    mlwrite("[%d chars pasted from \"%c]", len * count, reg);
    return true;
}

/* =========== Dot Repeat System =========== */

/* Last change info for dot repeat */
static struct {
    char op;           /* Operator: 'd', 'c', 'y', 'x', 'r', etc. */
    char motion;       /* Motion char or 0 for line-wise */
    char motion2;      /* Second motion char (for text objects: 'iw', 'a"', etc.) */
    int count;         /* Count for the operation */
    int linewise;      /* Was it a linewise operation? */
    char replace_char; /* For 'r' command */
    char insert_text[256];  /* Text inserted during change */
    int insert_len;    /* Length of inserted text */
    int valid;         /* Is this a valid recorded change? */
} last_change = {0};

/* Record a change for dot repeat */
static void vim_record_change(char op, char motion, int count, int linewise) {
    last_change.op = op;
    last_change.motion = motion;
    last_change.motion2 = 0;
    last_change.count = count > 0 ? count : 1;
    last_change.linewise = linewise;
    last_change.replace_char = 0;
    last_change.insert_len = 0;
    last_change.valid = 1;
}

/* Record text object change
 * Non-static - shared with vim_text_objects.c */
void vim_record_text_object_change(char op, char prefix, char object, int count) {
    last_change.op = op;
    last_change.motion = prefix;    /* 'i' or 'a' */
    last_change.motion2 = object;   /* 'w', '"', '(', etc. */
    last_change.count = count > 0 ? count : 1;
    last_change.linewise = 0;
    last_change.replace_char = 0;
    last_change.insert_len = 0;
    last_change.valid = 1;
}

/* Record replace char change */
static void vim_record_replace_change(char ch, int count) {
    last_change.op = 'r';
    last_change.motion = 0;
    last_change.motion2 = 0;
    last_change.count = count > 0 ? count : 1;
    last_change.linewise = 0;
    last_change.replace_char = ch;
    last_change.insert_len = 0;
    last_change.valid = 1;
}

/* Dot repeat command */
int vim_dot_repeat(int f, int n) {
    (void)f; (void)n;
    int count = vim_get_effective_count();
    vim_clear_pending_state();

    if (!last_change.valid) {
        mlwrite("[No change to repeat]");
        return false;
    }

    /* Use provided count or last count */
    int repeat_count = (count > 1) ? count : last_change.count;

    /* Handle different operation types */
    switch (last_change.op) {
        case 'd':
        case 'c':
        case 'y':
            if (last_change.linewise) {
                /* Linewise operation (dd, cc, yy) */
                return vim_execute_line_operation(last_change.op, repeat_count);
            } else if (last_change.motion2) {
                /* Text object (diw, ci", etc.) */
                g_vim_state.pending_op = last_change.op;
                /* Simulate text object execution */
                int result = false;
                bool inner = (last_change.motion == 'i');
                switch (last_change.motion2) {
                    case 'w': case 'W':
                        result = vim_select_word_object(inner);
                        break;
                    case '"':
                        result = vim_select_quote_object('"', inner);
                        break;
                    case '\'':
                        result = vim_select_quote_object('\'', inner);
                        break;
                    case '`':
                        result = vim_select_quote_object('`', inner);
                        break;
                    case '(': case ')': case 'b':
                        result = vim_select_bracket_object('(', ')', inner);
                        break;
                    case '{': case '}': case 'B':
                        result = vim_select_bracket_object('{', '}', inner);
                        break;
                    case '[': case ']':
                        result = vim_select_bracket_object('[', ']', inner);
                        break;
                    case '<': case '>':
                        result = vim_select_bracket_object('<', '>', inner);
                        break;
                    default:
                        return false;
                }
                if (!result) return false;
                /* Execute operator */
                if (last_change.op == 'd' || last_change.op == 'c') {
                    region_kill(false, 1);
                    vim_store_to_register(1, 0);
                    if (last_change.op == 'c') {
                        vim_enter_insert_mode(false, 1);
                    }
                } else if (last_change.op == 'y') {
                    region_copy(false, 1);
                    vim_store_to_register(0, 0);
                    swapmark(false, 0);
                }
                return true;
            } else {
                /* Motion-based operation */
                /* For now, simplified - just do linewise */
                return vim_execute_line_operation(last_change.op, repeat_count);
            }
            break;

        case 'x':
            /* Delete char */
            return delete_char_forward(true, repeat_count);

        case 'r':
            /* Replace char */
            for (int i = 0; i < repeat_count; i++) {
                if (curwp->w_doto >= llength(curwp->w_dotp)) break;
                delete_char_forward(false, 1);
                if (last_change.replace_char == '\n') {
                    openline(false, 1);
                } else {
                    linsert(1, last_change.replace_char);
                    move_char_backward(false, 1);
                }
                if (i < repeat_count - 1) move_char_forward(false, 1);
            }
            curwp->w_flag |= WFHARD;
            return true;

        case 'J':
            /* Join lines */
            return vim_join_lines(false, repeat_count);

        case '~':
            /* Toggle case */
            return vim_toggle_case(false, repeat_count);

        default:
            mlwrite("[Cannot repeat last change]");
            return false;
    }
}

/* G: Go to line N or end of file if no count */
int vim_goto_line_or_end(int f, int n) {
    (void)f; (void)n;
    if (g_vim_state.count_given) {
        int line = g_vim_state.count;
        vim_clear_pending_state();
        return gotoline(true, line);
    }
    vim_clear_pending_state();
    return goto_buffer_end(false, 1);  /* Go to end of buffer */
}

/* gg: Go to top of file (or line N with count) */
int vim_goto_top(int f, int n) {
    (void)f; (void)n;
    if (g_vim_state.count_given) {
        int line = g_vim_state.count;
        vim_clear_pending_state();
        return gotoline(true, line);
    }
    vim_clear_pending_state();
    return goto_buffer_start(false, 1);  /* Go to beginning of buffer */
}

/* g prefix handler - waits for second key */
int vim_goto_prefix(int f, int n) {
    (void)f; (void)n;
    mlwrite("[g-]");
    int key = vim_getkey();

    if (key == 'g') {
        return vim_goto_top(false, 1);
    }
    /* TODO: Add more g-prefix commands (gU, gu, etc.) */
    mlwrite("[Unknown: g%c]", key);
    vim_clear_pending_state();
    return false;
}

/*
 * Toggle Vim/Evil Mode
 */
int evil_mode(int f, int n) {
    (void)f; (void)n;
    int current = atomic_load(&vim_mode_active);
    atomic_store(&vim_mode_active, !current);

    if (atomic_load(&vim_mode_active)) {
        LOG_INFO("Vim: Evil Mode ENABLED");
        evil_mode_start_time = (long)time(NULL);  /* Start 3-second flash timer */
        atomic_store(&g_vim_state.current_mode, MODE_NORMAL);
        curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
        mlwrite("[EVIL: ENABLED]");
    } else {
        LOG_INFO("Vim: Evil Mode DISABLED");
        evil_mode_start_time = 0;
        atomic_store(&g_vim_state.current_mode, MODE_INSERT); // Reset state
        curwp->w_flag |= WFMODE;  /* Trigger modeline refresh */
        mlwrite("[EVIL: DISABLED]");
    }
    return true;
}

/* Initialize Vim Keymaps - NO command bindings (those come from TOML)
 *
 * This function only creates the keymap infrastructure.
 * All vim command bindings are defined in settings.toml under:
 *   [keybindings.normal]  - Vim normal mode
 *   [keybindings.visual]  - Vim visual mode
 *
 * The only binding kept here is the 'g' prefix, which is structural routing
 * (similar to C-x and ESC prefixes in the global keymap).
 */
void vim_init_keymaps(void) {
    LOG_INFO("Vim: Initializing Keymaps (infrastructure only - bindings from TOML)");

    /* Create Normal Mode Keymap */
    struct keymap *nkm = keymap_create("vim_normal");
    if (!nkm) return;

    /* ONLY prefix binding stays in code - 'g' is structural routing for gg/G commands */
    keymap_bind(nkm, keymap_key_make('g', 0), vim_goto_prefix);

    /* Ctrl-V for visual block mode - bind here since TOML parser has issues with ^V in normal section */
    keymap_bind(nkm, keymap_key_make('V', MOD_CTRL), vim_enter_visual_block_mode);

    atomic_store(&vim_normal_keymap, nkm);

    /* Create Visual Mode Keymap */
    struct keymap *vkm = keymap_create("vim_visual");
    if (vkm) {
        atomic_store(&vim_visual_keymap, vkm);
    }
}

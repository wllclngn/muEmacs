/*
 * vim_text_objects.c - Vim Text Object Commands
 *
 * Implements text object selection for operators:
 * - iw/aw - inner/around word
 * - i"/a" - inner/around quoted string
 * - i(/a( - inner/around parentheses
 * - i{/a{ - inner/around braces
 * - i[/a[ - inner/around brackets
 * - i</a< - inner/around angle brackets
 *
 * C23 compliant
 */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "editor_mode.h"

/*
 * Helper: Find matching bracket/paren/brace
 */
static int vim_find_matching_bracket(char open, char close, int direction) {
    int depth = 1;
    struct line *lp = curwp->w_dotp;
    int off = curwp->w_doto;

    while (true) {
        if (direction > 0) {
            off++;
            if (off >= llength(lp)) {
                lp = lforw(lp);
                if (lp == curbp->b_linep) return false;
                off = 0;
                continue;
            }
        } else {
            off--;
            if (off < 0) {
                lp = lback(lp);
                if (lp == curbp->b_linep) return false;
                off = llength(lp) - 1;
                if (off < 0) off = 0;
                continue;
            }
        }

        char c = lgetc(lp, off);
        if (c == close) depth--;
        else if (c == open) depth++;

        if (depth == 0) {
            curwp->w_dotp = lp;
            curwp->w_doto = off;
            curwp->w_flag |= WFMOVE;
            return true;
        }
    }
}

/*
 * Select word text object
 */
int vim_select_word_object(bool inner) {
    /* Save start */
    int orig = curwp->w_doto;

    /* Move to start of word */
    while (curwp->w_doto > 0) {
        move_char_backward(false, 1);
        if (!inword()) {
            move_char_forward(false, 1);
            break;
        }
    }

    /* Set mark at start of word */
    int word_start = curwp->w_doto;
    (void)word_start;  /* Used for clarity, compiler may optimize */

    /* If inner and we were on whitespace, restore */
    if (!inword() && !inner) {
        curwp->w_doto = orig;
    }

    setmark(false, 0);

    /* Move to end of word */
    curwp->w_doto = orig;
    while (curwp->w_doto < llength(curwp->w_dotp) && inword()) {
        move_char_forward(false, 1);
    }

    /* If around, include trailing whitespace */
    if (!inner) {
        while (curwp->w_doto < llength(curwp->w_dotp) &&
               lgetc(curwp->w_dotp, curwp->w_doto) == ' ') {
            move_char_forward(false, 1);
        }
    }

    return true;
}

/*
 * Select quoted string text object
 */
int vim_select_quote_object(char quote, bool inner) {
    struct line *lp = curwp->w_dotp;
    int len = llength(lp);
    int pos = curwp->w_doto;

    /* Find opening quote (search backward and forward) */
    int start = -1, end = -1;

    /* Search backward for opening quote */
    for (int i = pos; i >= 0; i--) {
        if (lgetc(lp, i) == quote) {
            start = i;
            break;
        }
    }

    /* Search forward for closing quote */
    for (int i = (start >= 0 ? start + 1 : pos); i < len; i++) {
        if (lgetc(lp, i) == quote) {
            end = i;
            break;
        }
    }

    if (start < 0 || end < 0 || start >= end) {
        mlwrite("[No quoted string found]");
        return false;
    }

    /* Adjust for inner (exclude quotes) vs around (include quotes) */
    if (inner) {
        start++;
        end--;
        if (start > end) {
            /* Empty quoted string */
            start = end;
        }
    }

    /* Set mark at start, move to end */
    curwp->w_doto = start;
    setmark(false, 0);
    curwp->w_doto = end + 1;
    curwp->w_flag |= WFMOVE;

    return true;
}

/*
 * Select bracket text object
 */
int vim_select_bracket_object(char open, char close, bool inner) {
    struct line *lp = curwp->w_dotp;
    int off = curwp->w_doto;

    /* Find opening bracket */
    struct line *open_line = NULL;
    int open_off = -1;
    int depth = 0;

    /* Check if we're on the bracket */
    char c = lgetc(lp, off);
    if (c == close) {
        depth = 1;
    } else if (c == open) {
        open_line = lp;
        open_off = off;
    } else {
        /* Search backward for opening bracket */
        struct line *search_lp = lp;
        int search_off = off;
        while (true) {
            search_off--;
            if (search_off < 0) {
                search_lp = lback(search_lp);
                if (search_lp == curbp->b_linep) break;
                search_off = llength(search_lp) - 1;
                if (search_off < 0) search_off = 0;
                continue;
            }
            c = lgetc(search_lp, search_off);
            if (c == close) depth++;
            else if (c == open) {
                if (depth == 0) {
                    open_line = search_lp;
                    open_off = search_off;
                    break;
                }
                depth--;
            }
        }
    }

    if (open_line == NULL) {
        mlwrite("[No opening %c found]", open);
        return false;
    }

    /* Find closing bracket from open position */
    curwp->w_dotp = open_line;
    curwp->w_doto = open_off;
    if (!vim_find_matching_bracket(open, close, 1)) {
        mlwrite("[No closing %c found]", close);
        return false;
    }

    struct line *close_line = curwp->w_dotp;
    int close_off = curwp->w_doto;

    /* Set region */
    if (inner) {
        /* Inner: exclude brackets */
        curwp->w_dotp = open_line;
        curwp->w_doto = open_off + 1;
        setmark(false, 0);
        curwp->w_dotp = close_line;
        curwp->w_doto = close_off;
    } else {
        /* Around: include brackets */
        curwp->w_dotp = open_line;
        curwp->w_doto = open_off;
        setmark(false, 0);
        curwp->w_dotp = close_line;
        curwp->w_doto = close_off + 1;
    }

    curwp->w_flag |= WFMOVE;
    return true;
}

/*
 * Text object handler for 'i' (inner) prefix
 */
int vim_text_object_inner(int f, int n) {
    (void)f; (void)n;

    enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
    bool in_visual = (mode == MODE_VISUAL || mode == MODE_VISUAL_LINE || mode == MODE_VISUAL_BLOCK);

    if (g_vim_state.pending_op == 0 && !in_visual) {
        /* No operator pending and not in visual mode - 'i' goes to insert mode */
        return vim_enter_insert_mode(false, 1);
    }

    char pending = g_vim_state.pending_op;
    if (!in_visual) {
        vim_clear_pending_state();
    }

    mlwrite("[i-]");
    int ch = vim_getkey();

    int result = false;
    switch (ch) {
        case 'w': result = vim_select_word_object(true); break;
        case 'W': result = vim_select_word_object(true); break;  /* Same as w for now */
        case '"': result = vim_select_quote_object('"', true); break;
        case '\'': result = vim_select_quote_object('\'', true); break;
        case '`': result = vim_select_quote_object('`', true); break;
        case '(': case ')': case 'b':
            result = vim_select_bracket_object('(', ')', true); break;
        case '{': case '}': case 'B':
            result = vim_select_bracket_object('{', '}', true); break;
        case '[': case ']':
            result = vim_select_bracket_object('[', ']', true); break;
        case '<': case '>':
            result = vim_select_bracket_object('<', '>', true); break;
        default:
            mlwrite("[Unknown text object: i%c]", ch);
            return false;
    }

    if (!result) return false;

    /* In visual mode, just update the selection - don't execute operator */
    if (in_visual) {
        return true;
    }

    /* Record for dot repeat */
    vim_record_text_object_change(pending, 'i', ch, 1);

    /* Execute operator */
    if (pending == 'd' || pending == 'c') {
        region_kill(false, 1);
        vim_store_to_register(1, 0);  /* Delete, character-wise */
        if (pending == 'c') {
            vim_enter_insert_mode(false, 1);
        }
    } else if (pending == 'y') {
        region_copy(false, 1);
        vim_store_to_register(0, 0);  /* Yank, character-wise */
        swapmark(false, 0);
        mlwrite("[Yanked]");
    }

    return true;
}

/*
 * Text object handler for 'a' (around) prefix
 */
int vim_text_object_around(int f, int n) {
    (void)f; (void)n;

    enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
    bool in_visual = (mode == MODE_VISUAL || mode == MODE_VISUAL_LINE || mode == MODE_VISUAL_BLOCK);

    if (g_vim_state.pending_op == 0 && !in_visual) {
        /* No operator pending and not in visual mode - 'a' appends after cursor */
        move_char_forward(false, 1);
        return vim_enter_insert_mode(false, 1);
    }

    char pending = g_vim_state.pending_op;
    if (!in_visual) {
        vim_clear_pending_state();
    }

    mlwrite("[a-]");
    int ch = vim_getkey();

    int result = false;
    switch (ch) {
        case 'w': result = vim_select_word_object(false); break;
        case 'W': result = vim_select_word_object(false); break;
        case '"': result = vim_select_quote_object('"', false); break;
        case '\'': result = vim_select_quote_object('\'', false); break;
        case '`': result = vim_select_quote_object('`', false); break;
        case '(': case ')': case 'b':
            result = vim_select_bracket_object('(', ')', false); break;
        case '{': case '}': case 'B':
            result = vim_select_bracket_object('{', '}', false); break;
        case '[': case ']':
            result = vim_select_bracket_object('[', ']', false); break;
        case '<': case '>':
            result = vim_select_bracket_object('<', '>', false); break;
        default:
            mlwrite("[Unknown text object: a%c]", ch);
            return false;
    }

    if (!result) return false;

    /* In visual mode, just update the selection - don't execute operator */
    if (in_visual) {
        return true;
    }

    /* Record for dot repeat */
    vim_record_text_object_change(pending, 'a', ch, 1);

    /* Execute operator */
    if (pending == 'd' || pending == 'c') {
        region_kill(false, 1);
        vim_store_to_register(1, 0);  /* Delete, character-wise */
        if (pending == 'c') {
            vim_enter_insert_mode(false, 1);
        }
    } else if (pending == 'y') {
        region_copy(false, 1);
        vim_store_to_register(0, 0);  /* Yank, character-wise */
        swapmark(false, 0);
        mlwrite("[Yanked]");
    }

    return true;
}

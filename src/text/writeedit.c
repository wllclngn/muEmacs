/*
 * writeedit.c - WriteEdit prose mode for μEmacs
 *
 * Provides Word-like document editing:
 * - Soft-wrap: Visual line wrapping without hard newlines
 * - Smart typography: Real-time transforms (-- → —, smart quotes)
 *
 * Usage: M-x write-edit or M-x WE
 */

#include <stdbool.h>
#include <string.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"

/* WriteEdit settings structure */
typedef struct {
    int soft_wrap_col;          /* Visual wrap width (0 = terminal width) */
    bool smart_typography;      /* Master toggle for all transforms */
    bool em_dash;               /* -- → — */
    bool smart_quotes;          /* " → "" */
    bool curly_apostrophe;      /* ' → ' */
} writeedit_settings_t;

/* Global defaults (loaded from TOML) */
writeedit_settings_t writeedit_defaults = {
    .soft_wrap_col = 80,
    .smart_typography = true,
    .em_dash = true,
    .smart_quotes = true,
    .curly_apostrophe = true,
};

/* Track previous character for context-aware transforms */
static int prev_char = 0;

/*
 * Reset WriteEdit state (for testing/re-initialization).
 */
void writeedit_reset(void)
{
    prev_char = 0;
}

/*
 * Check if character is a word boundary (for smart quote context).
 */
static bool is_word_boundary(int c)
{
    return c == 0 || c == ' ' || c == '\t' || c == '\n' ||
           c == '(' || c == '[' || c == '{' || c == '<';
}

/*
 * Unicode codepoints for smart typography.
 */
#define EMDASH          0x2014  /* — */
#define LEFT_DQUOTE     0x201C  /* " */
#define RIGHT_DQUOTE    0x201D  /* " */
#define RIGHT_SQUOTE    0x2019  /* ' (also apostrophe) */

/*
 * Transform a character according to smart typography rules.
 *
 * Returns:
 *   0 - No transform, use original character c
 *   1 - Use *out_codepoint instead of c
 *  -1 - Delete previous char, then insert *out_codepoint (em-dash case)
 *
 * Transforms:
 *   -- → — (em dash)
 *   " → "" (context-aware double quotes)
 *   ' → ' (curly apostrophe)
 */
int writeedit_transform_char(int c, int *out_codepoint)
{
    if (!curbp || !(curbp->b_mode & MDWRITEEDIT)) {
        prev_char = c;
        return 0;
    }

    /* Check master toggle */
    if (!writeedit_defaults.smart_typography) {
        prev_char = c;
        return 0;
    }

    /* Em dash: -- → — */
    if (writeedit_defaults.em_dash && c == '-' && prev_char == '-') {
        *out_codepoint = EMDASH;
        prev_char = 0;  /* Reset - em dash is complete */
        return -1;      /* Signal: delete prev char, insert em-dash */
    }

    /* Smart double quotes */
    if (writeedit_defaults.smart_quotes && c == '"') {
        if (is_word_boundary(prev_char)) {
            *out_codepoint = LEFT_DQUOTE;
        } else {
            *out_codepoint = RIGHT_DQUOTE;
        }
        prev_char = c;
        return 1;
    }

    /* Curly apostrophe: ' → ' */
    if (writeedit_defaults.curly_apostrophe && c == '\'') {
        *out_codepoint = RIGHT_SQUOTE;
        prev_char = c;
        return 1;
    }

    /* No transformation */
    prev_char = c;
    return 0;
}

/*
 * Toggle WriteEdit mode for current buffer.
 * M-x write-edit or M-x WE
 */
int write_edit_cmd(int f, int n)
{
    (void)f; (void)n;

    if (!curbp) return false;

    if (curbp->b_mode & MDWRITEEDIT) {
        /* Disable WriteEdit mode */
        curbp->b_mode &= ~MDWRITEEDIT;

        /* Disable soft wrap */
        if (curwp) {
            curwp->w_wrap_col = 0;
            curwp->w_flag |= WFHARD;
        }

        prev_char = 0;
        mlwrite("[WRITEEDIT DISABLED]");
    } else {
        /* Enable WriteEdit mode */
        curbp->b_mode |= MDWRITEEDIT;

        /* Enable soft wrap with configured column */
        if (curwp) {
            curwp->w_wrap_col = writeedit_defaults.soft_wrap_col > 0 ?
                                writeedit_defaults.soft_wrap_col : term.t_ncol;
            curwp->w_flag |= WFHARD;
        }

        prev_char = 0;
        mlwrite("[WRITEEDIT ENABLED - SOFT WRAP AT %d]",
                curwp ? curwp->w_wrap_col : writeedit_defaults.soft_wrap_col);
    }

    sgarbf = true;
    upmode();
    return true;
}

/*
 * Set WriteEdit defaults from TOML.
 * Called by settings.c during config load.
 */
void writeedit_set_default_wrap_col(int col)
{
    writeedit_defaults.soft_wrap_col = col > 0 ? col : 80;
}

void writeedit_set_default_smart_typography(bool enabled)
{
    writeedit_defaults.smart_typography = enabled;
}

void writeedit_set_default_em_dash(bool enabled)
{
    writeedit_defaults.em_dash = enabled;
}

void writeedit_set_default_smart_quotes(bool enabled)
{
    writeedit_defaults.smart_quotes = enabled;
}

void writeedit_set_default_curly_apostrophe(bool enabled)
{
    writeedit_defaults.curly_apostrophe = enabled;
}

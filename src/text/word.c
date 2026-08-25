/*	word.c
 *
 *      The routines in this file implement commands that work word or a
 *      paragraph at a time.  There are all sorts of word mode commands.  If I
 *      do any sentence mode commands, they are likely to be put in this file.
 *
 *	Modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "undo.h"
#include "memory.h"
#include "util/logger.h"

/* Helper to count display width of UTF-8 text (codepoints, not bytes) */
static int utf8_display_width(const char *text, int len) {
    int width = 0;
    for (int i = 0; i < len; i++) {
        /* Count only non-continuation bytes (not 10xxxxxx) */
        if ((text[i] & 0xC0) != 0x80) {
            width++;
        }
    }
    return width;
}

/* Word wrap on n-spaces. Back-over whatever precedes the point on the current
 * line and stop on the first word-break or the beginning of the line. If we
 * reach the beginning of the line, jump back to the end of the word and start
 * a new line.	Otherwise, break the line at the word-break, eat it, and jump
 * back to the end of the word.
 * Returns true on success, false on errors.
 *
 * @f: default flag.
 * @n: numeric argument.
 */
int wrapword(int f, int n)
{
    int cnt;	/* size of word wrapped to next line */
    int c;		/* charector temporary */

    /* backup from the <NL> 1 char */
    if (!move_char_backward(0, 1))
        return false;

    /* back up until we aren't in a word,
     make sure there is a break in the line */
    cnt = 0;
    while (((c = lgetc(curwp->w_dotp, curwp->w_doto)) != ' ')
       && (c != '\t')) {
        cnt++;
        if (!move_char_backward(0, 1))
            return false;
        /* if we make it to the beginning, start a new line */
        if (curwp->w_doto == 0) {
            goto_line_end(false, 0);
            return lnewline();
        }
    }

    /* delete the forward white space */
    if (!delete_char_forward(0, 1))
        return false;

    /* put in a end of line */
    if (!lnewline())
        return false;

    /* and past the first word */
    while (cnt-- > 0) {
        if (move_char_forward(false, 1) == false)
            return false;
    }
    return true;
}

/*
 * Move the cursor backward by "n" words. All of the details of motion are
 * performed by the "move_char_backward" and "move_char_forward" routines. Error if you try to
 * move beyond the buffers.
 */
int move_word_backward(int f, int n)
{
    if (n < 0)
        return move_word_forward(f, -n);
    if (move_char_backward(false, 1) == false)
        return false;
    while (n--) {
        while (inword() == false) {
            if (move_char_backward(false, 1) == false)
                return false;
        }
        while (inword() != false) {
            if (move_char_backward(false, 1) == false)
                return false;
        }
    }
    return move_char_forward(false, 1);
}

/*
 * Move the cursor forward by the specified number of words. All of the motion
 * is done by "move_char_forward". Error if you try and move beyond the buffer's end.
 */
int move_word_forward(int f, int n)
{
    if (n < 0)
        return move_word_backward(f, -n);
    while (n--) {
        while (inword() == true) {
            if (move_char_forward(false, 1) == false)
                return false;
        }

        while (inword() == false) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
    }
    return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move,
 * convert any characters to upper case. Error if you try and move beyond the
 * end of the buffer. Bound to "M-U".
 */
int upperword(int f, int n)
{
    int c;

    if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
        return rdonly();	/* we are in read only mode     */
    if (n < 0)
        return false;
    while (n--) {
        while (inword() == false) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
        while (inword() != false) {
            c = lgetc(curwp->w_dotp, curwp->w_doto);
            int upper = to_upper(c);
            if (upper != c) {
                lputc(curwp->w_dotp, curwp->w_doto, upper);
                lchange(WFHARD);
            }
            if (move_char_forward(false, 1) == false)
                return false;
        }
    }
    return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move
 * convert characters to lower case. Error if you try and move over the end of
 * the buffer. Bound to "M-L".
 */
int lowerword(int f, int n)
{
    int c;

    if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
        return rdonly();	/* we are in read only mode     */
    if (n < 0)
        return false;
    while (n--) {
        while (inword() == false) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
        while (inword() != false) {
            c = lgetc(curwp->w_dotp, curwp->w_doto);
            int lower = to_lower(c);
            if (lower != c) {
                lputc(curwp->w_dotp, curwp->w_doto, lower);
                lchange(WFHARD);
            }
            if (move_char_forward(false, 1) == false)
                return false;
        }
    }
    return true;
}

/*
 * Move the cursor forward by the specified number of words. As you move
 * convert the first character of the word to upper case, and subsequent
 * characters to lower case. Error if you try and move past the end of the
 * buffer. Bound to "M-C".
 */
int capword(int f, int n)
{
    int c;

    if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
        return rdonly();	/* we are in read only mode     */
    if (n < 0)
        return false;
    while (n--) {
        while (inword() == false) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
        if (inword() != false) {
            /* Capitalize first letter */
            c = lgetc(curwp->w_dotp, curwp->w_doto);
            int upper = to_upper(c);
            if (upper != c) {
                lputc(curwp->w_dotp, curwp->w_doto, upper);
                lchange(WFHARD);
            }
            if (move_char_forward(false, 1) == false)
                return false;
            /* Lowercase rest of word */
            while (inword() != false) {
                c = lgetc(curwp->w_dotp, curwp->w_doto);
                int lower = to_lower(c);
                if (lower != c) {
                    lputc(curwp->w_dotp, curwp->w_doto,
                       lower);
                    lchange(WFHARD);
                }
                if (move_char_forward(false, 1) == false)
                    return false;
            }
        }
    }
    return true;
}

/*
 * Kill forward by "n" words. Remember the location of dot. Move forward by
 * the right number of words. Put dot back where it was and issue the kill
 * command for the right number of characters. With a zero argument, just
 * kill one word and no whitespace. Bound to "M-D".
 */
int delete_word_forward(int f, int n)
{
    struct line *dotp;	/* original cursor line */
    int doto;	/*      and row */
    int c;		/* temp char */
    long size;		/* # of chars to delete */

    /* don't allow this command if we are in read only mode */
    if (curbp->b_mode & MDVIEW)
        return rdonly();

    /* ignore the command if there is a negative argument */
    if (n < 0)
        return false;

    /* Clear the kill buffer if last command wasn't a kill */
    if ((lastflag & CFKILL) == 0)
        kdelete();
    thisflag |= CFKILL;	/* this command is a kill */

    /* save the current cursor position */
    dotp = curwp->w_dotp;
    doto = curwp->w_doto;

    /* figure out how many characters to give the axe */
    size = 0;

    /* get us into a word.... */
    while (inword() == false) {
        if (move_char_forward(false, 1) == false)
            return false;
        ++size;
    }

    if (n == 0) {
        /* skip one word, no whitespace! */
        while (inword() == true) {
            if (move_char_forward(false, 1) == false)
                return false;
            ++size;
        }
    } else {
        /* skip n words.... */
        while (n--) {

            /* if we are at EOL; skip to the beginning of the next */
            while (curwp->w_doto == llength(curwp->w_dotp)) {
                if (move_char_forward(false, 1) == false)
                    return false;
                ++size;
            }

            /* move forward till we are at the end of the word */
            while (inword() == true) {
                if (move_char_forward(false, 1) == false)
                    return false;
                ++size;
            }

            /* if there are more words, skip the interword stuff */
            if (n != 0)
                while (inword() == false) {
                    if (move_char_forward(false, 1) == false)
                        return false;
                    ++size;
                }
        }

        /* skip whitespace and newlines */
        while ((curwp->w_doto == llength(curwp->w_dotp)) ||
           ((c = lgetc(curwp->w_dotp, curwp->w_doto)) == ' ')
           || (c == '\t')) {
            if (move_char_forward(false, 1) == false)
                break;
            ++size;
        }
    }

    /* restore the original position and delete the words */
    curwp->w_dotp = dotp;
    curwp->w_doto = doto;
    return ldelete(size, true);
}

/*
 * Kill backwards by "n" words. Move backwards by the desired number of words,
 * counting the characters. When dot is finally moved to its resting place,
 * fire off the kill command. Bound to "M-Rubout" and to "M-Backspace".
 */
int delete_word_backward(int f, int n)
{
    long size;

    /* don't allow this command if we are in read only mode */
    if (curbp->b_mode & MDVIEW)
        return rdonly();

    /* ignore the command if there is a nonpositive argument */
    if (n <= 0)
        return false;

    /* Clear the kill buffer if last command wasn't a kill */
    if ((lastflag & CFKILL) == 0)
        kdelete();
    thisflag |= CFKILL;	/* this command is a kill */

    if (move_char_backward(false, 1) == false)
        return false;
    size = 0;
    while (n--) {
        while (inword() == false) {
            if (move_char_backward(false, 1) == false)
                return false;
            ++size;
        }
        while (inword() != false) {
            ++size;
            if (move_char_backward(false, 1) == false)
                goto bckdel;
        }
    }
    if (move_char_forward(false, 1) == false)
        return false;
      bckdel:return ldelchar(size, true);
}

/*
 * Return true if the character at dot is a character that is considered to be
 * part of a word. The word character list is hard coded. Should be setable.
 */
int inword(void)
{
    int c;

    if (curwp->w_doto == llength(curwp->w_dotp))
        return false;
    c = lgetc(curwp->w_dotp, curwp->w_doto);
    if (is_letter(c))
        return true;
    if (c >= '0' && c <= '9')
        return true;
    return false;
}

/*
 * Check if we're at a subword boundary (camelCase or snake_case).
 * Returns true if the cursor is at a position where a subword begins or ends.
 * For example: camelCase has boundaries at 'c' and 'C'.
 *              snake_case has boundaries at 's', '_', and 'c'.
 */
int at_subword_boundary(void)
{
    int c, prevc, nextc;
    
    if (curwp->w_doto == 0)
        return true;  /* Beginning of line is always a boundary */
    
    if (curwp->w_doto >= llength(curwp->w_dotp))
        return true;  /* End of line is always a boundary */
    
    /* Get current, previous, and next characters */
    c = lgetc(curwp->w_dotp, curwp->w_doto);
    prevc = lgetc(curwp->w_dotp, curwp->w_doto - 1);
    
    /* Check for underscore boundaries (snake_case) */
    if (c == '_' || prevc == '_')
        return true;
    
    /* Check for camelCase boundaries */
    if (curwp->w_doto + 1 < llength(curwp->w_dotp)) {
        nextc = lgetc(curwp->w_dotp, curwp->w_doto + 1);
        
        /* Lowercase to uppercase transition (camelCase) */
        if ((prevc >= 'a' && prevc <= 'z') && (c >= 'A' && c <= 'Z'))
            return true;
        
        /* Uppercase to lowercase where next is lowercase (CAPSWord -> CAPS|Word) */
        if ((prevc >= 'A' && prevc <= 'Z') && (c >= 'A' && c <= 'Z') && 
          (nextc >= 'a' && nextc <= 'z'))
            return true;
    }
    
    /* Digit boundaries */
    if ((prevc >= '0' && prevc <= '9') != (c >= '0' && c <= '9'))
        return true;
    
    return false;
}

/*
 * Move forward by one subword. Stops at camelCase boundaries and underscores.
 * For example: "camelCaseWord" -> stops at 'C' and 'W'
 *              "snake_case_word" -> stops at each underscore and word start
 */
int forwsubword(int f, int n)
{
    if (n < 0)
        return backsubword(f, -n);
    
    while (n--) {
        /* Skip any non-word characters first */
        while (inword() == false) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
        
        /* Now skip to next subword boundary */
        if (move_char_forward(false, 1) == false)
            return false;
        
        while (inword() == true && !at_subword_boundary()) {
            if (move_char_forward(false, 1) == false)
                return false;
        }
    }
    return true;
}

/*
 * Move backward by one subword. Stops at camelCase boundaries and underscores.
 */
int backsubword(int f, int n)
{
    if (n < 0)
        return forwsubword(f, -n);
    
    while (n--) {
        /* Move back one character first */
        if (move_char_backward(false, 1) == false)
            return false;
        
        /* Skip any non-word characters */
        while (inword() == false) {
            if (move_char_backward(false, 1) == false)
                return false;
        }
        
        /* Now skip to previous subword boundary */
        while (inword() == true && !at_subword_boundary()) {
            if (move_char_backward(false, 1) == false)
                return false;
        }
    }
    return true;
}

/*
 * Fill the current paragraph according to the current
 * fill column
 *
 * f and n - deFault flag and Numeric argument
 */
/*
 * Optimized fillpara - O(lines) undo operations instead of O(chars)
 * Uses Emacs-style approach: join lines, then re-break.
 * Preserves first line to maintain external pointer validity.
 */
int fillpara(int f, int n)
{
    if (curbp->b_mode & MDVIEW)
        return rdonly();

    /* Hybrid soft/hard-wrap: when the current window has soft-wrap active
    * (w_wrap_col > 0, set by WE mode or the soft-wrap toggle), use that as
    * the fill target so M-q hard-wraps to the user's visible margin.
    * Otherwise fall back to the global fillcol. */
    int fill_target = (curwp && curwp->w_wrap_col > 0) ? curwp->w_wrap_col : fillcol;
    if (fill_target == 0) {
        mlwrite("NO FILL COLUMN SET");
        return false;
    }

    /* Find paragraph boundaries */
    goto_para_end(false, 1);
    struct line *eopline = lforw(curwp->w_dotp);
    goto_para_start(false, 1);
    struct line *bopline = curwp->w_dotp;
    curwp->w_doto = 0;  /* Always start from beginning of line */
    int start_offset = 0;

    /* Calculate total paragraph size */
    size_t total_size = 0;
    int line_count = 0;
    for (struct line *lp = bopline; lp != eopline && lp != curbp->b_linep; lp = lforw(lp)) {
        total_size += (size_t)llength(lp) + 1;
        line_count++;
    }
    if (total_size == 0 || line_count == 0) return true;

    /* Allocate buffer for paragraph text */
    char *para_buf = SAFE_ALLOC_SIZED(char, total_size + 1, "paragraph_buffer");
    if (!para_buf) return false;

    /* Copy paragraph to buffer, converting newlines to spaces */
    /* Skip any CR characters (handle CRLF line endings) */
    size_t pos = 0;
    for (struct line *lp = bopline; lp != eopline && lp != curbp->b_linep; lp = lforw(lp)) {
        int len = llength(lp);
        int start = (lp == bopline) ? start_offset : 0;
        for (int i = start; i < len; i++) {
            char c = (char)lgetc(lp, i);
            if (c != '\r') {  /* Skip CR characters */
                para_buf[pos++] = c;
            }
        }
        if (lforw(lp) != eopline && lforw(lp) != curbp->b_linep) {
            para_buf[pos++] = ' ';
        }
    }
    para_buf[pos] = '\0';

    LOG_DEBUGF("fillpara: read %zu bytes, line_count=%d, start_offset=%d, fill_target=%d",
         pos, line_count, start_offset, fill_target);

    /* Group undo for atomic operation */
    undo_group_begin(curbp);

    /* Step 1: Delete paragraph content line by line (O(lines) operations) */
    curwp->w_dotp = bopline;
    curwp->w_doto = start_offset;

    /* Calculate total bytes to delete */
    long total_bytes = 0;
    struct line *lp = bopline;
    for (int i = 0; i < line_count && lp != curbp->b_linep; i++) {
        int line_start = (i == 0) ? start_offset : 0;
        total_bytes += llength(lp) - line_start;
        if (i < line_count - 1) total_bytes++;  /* newline */
        lp = lforw(lp);
    }

    /* Delete all content in ONE operation for efficiency */
    LOG_DEBUGF("fillpara: deleting %ld bytes", total_bytes);
    if (total_bytes > 0) {
        ldelete(total_bytes, false);
    }

    /* Step 2: Build reformatted text in memory, then insert all at once */
    char *output = SAFE_ALLOC_SIZED(char, total_size * 2 + 1, "reformat_output");
    if (!output) {
        SAFE_FREE(para_buf);
        undo_group_end(curbp);
        return false;
    }

    size_t out_pos = 0;
    int clength = 0;
    int firstflag = 1;
    char *p = para_buf;

    while (*p) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Extract word */
        char *word_start = p;
        int dotflag = 0;
        while (*p && *p != ' ' && *p != '\t') {
            if (*p == '.') dotflag = 1;
            p++;
        }
        int wordlen = (int)(p - word_start);  /* Byte length for memcpy */
        if (wordlen == 0) continue;

        /* Calculate display width (UTF-8 aware) */
        int wordwidth = utf8_display_width(word_start, wordlen);

        /* Check if extra space after period will be added */
        int extraspace = (dotflag && (*p == ' ' || *p == '\t' || *p == '\0')) ? 1 : 0;

        /* Calculate new line length using display width (include post-period space) */
        int newlength = clength + (firstflag ? 0 : 1) + wordwidth + extraspace;

        if (newlength > fill_target && !firstflag) {
            /* Start new line */
            output[out_pos++] = '\n';
            clength = 0;
        } else if (!firstflag) {
            /* Add space before word */
            output[out_pos++] = ' ';
            clength++;
        }

        /* Add word */
        memcpy(output + out_pos, word_start, (size_t)wordlen);
        out_pos += (size_t)wordlen;
        clength += wordwidth;  /* Use display width, not byte length */
        firstflag = 0;

        /* Add extra space after period */
        if (dotflag && (*p == ' ' || *p == '\t' || *p == '\0')) {
            output[out_pos++] = ' ';
            clength++;
        }
    }

    /* Add final newline */
    output[out_pos++] = '\n';
    output[out_pos] = '\0';

    /* Insert all reformatted text (handles newlines internally) */
    LOG_DEBUGF("fillpara: output %zu bytes, inserting", out_pos);
    linstr(output);

    SAFE_FREE(output);
    SAFE_FREE(para_buf);
    undo_group_end(curbp);
    return true;
}

/* Fill the current paragraph according to the current
 * fill column and cursor position
 *
 * int f, n;		deFault flag and Numeric argument
 */
int justpara(int f, int n)
{
    unicode_t c;		/* current char durring scan    */
    unicode_t wbuf[NSTRING];/* buffer for current word      */
    int wordlen;	/* length of current word       */
    int clength;	/* position on line during fill */
    int i;		/* index during word copy       */
    int newlength;	/* tentative new line length    */
    int eopflag;	/* Are we at the End-Of-Paragraph? */
    int firstflag;	/* first word? (needs no space) */
    struct line *eopline;	/* pointer to line just past EOP */
    int leftmarg;		/* left marginal */

    if (curbp->b_mode & MDVIEW)	/* don't allow this command if      */
        return rdonly();	/* we are in read only mode     */
    if (fillcol == 0) {	/* no fill column set */
        mlwrite("NO FILL COLUMN SET");
        return false;
    }
    justflag = true;
    leftmarg = curwp->w_doto;
    if (leftmarg + 10 > fillcol) {
        leftmarg = 0;
        mlwrite("COLUMN TOO NARROW");
        return false;
    }

    /* Group all undo operations for atomic undo/redo */
    undo_group_begin(curbp);

    /* record the pointer to the line just past the EOP */
    goto_para_end(false, 1);
    eopline = lforw(curwp->w_dotp);

    /* and back top the beginning of the paragraph */
    goto_para_start(false, 1);

    /* initialize various info */
    if (leftmarg < llength(curwp->w_dotp))
        curwp->w_doto = leftmarg;

    clength = curwp->w_doto;
    if (clength && lgetc(curwp->w_dotp, 0) == TAB)
        clength = 8;

    wordlen = 0;

    /* scan through lines, filling words */
    firstflag = true;
    eopflag = false;
    while (!eopflag) {
        int bytes = 1;

        /* get the next character in the paragraph */
        if (curwp->w_doto == llength(curwp->w_dotp)) {
            c = ' ';
            if (lforw(curwp->w_dotp) == eopline)
                eopflag = true;
        } else
            bytes = lgetchar(&c);

        /* and then delete it */
        ldelete(bytes, false);

        /* if not a separator, just add it in */
        if (c != ' ' && c != '\t') {
            if (wordlen < NSTRING - 1)
                wbuf[wordlen++] = c;
        } else if (wordlen) {
            /* at a word break with a word waiting */
            /* calculate tentitive new length with word added */
            newlength = clength + 1 + wordlen;
            if (newlength <= fillcol) {
                /* add word to current line */
                if (!firstflag) {
                    linsert(1, ' ');	/* the space */
                    ++clength;
                }
                firstflag = false;
            } else {
                /* start a new line */
                lnewline();
                for (i = 0; i < leftmarg; i++)
                    linsert(1, ' ');
                clength = leftmarg;
            }

            /* Convert unicode word to UTF-8 and bulk insert */
            {
                char utf8_word[NSTRING * 4];
                int utf8_len = 0;
                for (i = 0; i < wordlen; i++) {
                    utf8_len += (int)unicode_to_utf8(wbuf[i], utf8_word + utf8_len);
                }
                utf8_word[utf8_len] = '\0';
                if (!linstr(utf8_word))
                    return false;
                clength += wordlen;
            }
            wordlen = 0;
        }
    }
    /* and add a last newline for the end of our new paragraph */
    lnewline();

    move_word_forward(false, 1);
    if (llength(curwp->w_dotp) > leftmarg)
        curwp->w_doto = leftmarg;
    else
        curwp->w_doto = llength(curwp->w_dotp);

    /* End undo grouping */
    undo_group_end(curbp);

    justflag = false;
    return true;
}

/*
 * delete n paragraphs starting with the current one
 *
 * int f	default flag
 * int n	# of paras to delete
 */
int killpara(int f, int n)
{
    int status;	/* returned status of functions */

    while (n--) {		/* for each paragraph to delete */

        /* mark out the end and beginning of the para to delete */
        goto_para_end(false, 1);

        /* set the mark here */
        curwp->w_markp = curwp->w_dotp;
        curwp->w_marko = curwp->w_doto;

        /* go to the beginning of the paragraph */
        goto_para_start(false, 1);
        curwp->w_doto = 0;	/* force us to the beginning of line */

        /* and delete it */
        if ((status = region_kill(false, 1)) != true)
            return status;

        /* and clean up the 2 extra lines */
        ldelete(2L, true);
    }
    return true;
}


/*
 *	wordcount:	count the # of words in the marked region,
 *			along with average word sizes, # of chars, etc,
 *			and report on them.
 *
 * int f, n;		ignored numeric arguments
 */
int wordcount(int f, int n)
{
    struct line *lp;	/* current line to scan */
    int offset;	/* current char to scan */
    long size;		/* size of region left to count */
    int ch;	/* current character to scan */
    int wordflag;	/* are we in a word now? */
    int lastword;	/* were we just in a word? */
    long nwords;		/* total # of words */
    long nchars;		/* total number of chars */
    int nlines;		/* total number of lines in region */
    int avgch;		/* average number of chars/word */
    int status;		/* status return code */
    struct region region;		/* region to look at */

    /* make sure we have a region to count */
    if ((status = getregion(&region)) != true)
        return status;
    lp = region.r_linep;
    offset = region.r_offset;
    size = region.r_size;

    /* count up things */
    lastword = false;
    nchars = 0L;
    nwords = 0L;
    nlines = 0;
    while (size--) {

        /* get the current character */
        if (offset == llength(lp)) {	/* end of line */
            ch = '\n';
            lp = lforw(lp);
            offset = 0;
            ++nlines;
        } else {
            ch = lgetc(lp, offset);
            ++offset;
        }

        /* and tabulate it */
        wordflag = (is_letter(ch) || (ch >= '0' && ch <= '9'));
        if (wordflag == true && lastword == false)
            ++nwords;
        lastword = wordflag;
        ++nchars;
    }

    /* and report on the info */
    if (nwords > 0L)
        avgch = (int) ((100L * nchars) / nwords);
    else
        avgch = 0;

    mlwrite("WORDS %D CHARS %D LINES %d AVG CHARS/WORD %f",
        nwords, nchars, nlines + 1, avgch);
    return true;
}

/*	search.c
 *
 * The functions in this file implement commands that search in the forward
 * and backward directions.  Refactored for C23 compliance and O(1) readiness.
 *
 * Original Authors: Dave G. Conroy, Steve Wilhite, George Jones
 * Modifications: John M. Gamble (Magic Mode), Petri Kutvonen
 * Modernization (2025): Refactored into UI/Logic separation.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "string_utils.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "memory.h"
#include <sublimation_text.h>

#include "util/logger.h"

/* Forward Declarations */
static int search_interactive(int f, int n, int direction, const char *restrict prompt);
static int search_execute(const char *restrict pattern, int direction, int n);
int scan_buffer(const char *restrict pattern, int direction, int beg_or_end);
int scan_buffer_forward(const char *restrict pattern, int beg_or_end);
int scan_buffer_backward(const char *restrict pattern, int beg_or_end);

static int readpattern(const char *restrict prompt, char *restrict apat, int srch);
static int replaces(int kind, int f, int n);
static int nextch(struct line **pcurline, int *pcuroff, int dir);
static void savematch(void);

/* Replacement-side metacharacter machinery. The forward-magic scanner
 * is gone (the sublimation regex face covers MDMAGIC search); what
 * survives is & expansion in replacement strings via delins. */
#if defined(MAGIC)
static int rmcstr(void);
void rmcclear(void);

static short int rmagical;
static struct magic_replacement rmcpat[NPAT]; /* The replacement magic array. */
#endif

/* Helper: Extract line text into buffer for contiguous access */
/* Made thread-safe by using caller-provided buffer - handles view mode lines */
static inline const char* get_line_text(struct line *lp, char *buffer, size_t buf_size) {
    int len = llength(lp);
    if ((size_t)len > buf_size - 1) len = (int)buf_size - 1;
    lget_text(lp, 0, (size_t)len, buffer, buf_size);
    buffer[len] = '\0';
    return buffer;
}

/*
 * sublimation-backed matcher. One cached compiled program; recompiled
 * only when the pattern text, case mode or magic mode changes. The
 * program is a value object (no heap). Faces: FIXED when magic is off,
 * regex (Glushkov) when magic is on; an invalid regex degrades to
 * literal, matching the old engine's fall-through behaviour. Matching
 * is per line (patterns containing newlines take the naive multi-line
 * walk below), so ^ and $ anchor to line boundaries exactly as before.
 */
static struct {
    sublimation_search prog;
    char pat[NPAT];
    unsigned flags;
    int k;
    bool valid;
} sub_cache;

/* Nonzero selects the fuzzy k-mismatch face for the duration of one
 * command (set by fuzzsearch around search_execute). */
static int fuzz_k = 0;

static sublimation_search *sub_engine(const char *pattern,
                                      bool case_sensitive, bool regex_face)
{
    size_t plen = strlen(pattern);
    unsigned flags = (regex_face ? 0u : SUBLIMATION_SEARCH_FIXED) |
                     (case_sensitive ? 0u : SUBLIMATION_SEARCH_ICASE);

    if (plen == 0 || plen >= NPAT) return nullptr;
    if (sub_cache.valid && sub_cache.flags == flags &&
        sub_cache.k == fuzz_k && strcmp(sub_cache.pat, pattern) == 0)
        return &sub_cache.prog;

    sub_cache.valid = false;
    sublimation_search_compile(&sub_cache.prog, pattern, plen, flags, fuzz_k);
    if (!sublimation_search_valid(&sub_cache.prog) && regex_face) {
        flags |= SUBLIMATION_SEARCH_FIXED;
        sublimation_search_compile(&sub_cache.prog, pattern, plen, flags, fuzz_k);
    }
    if (!sublimation_search_valid(&sub_cache.prog)) return nullptr;

    safe_strcpy(sub_cache.pat, pattern, NPAT);
    sub_cache.flags = flags;
    sub_cache.k = fuzz_k;
    sub_cache.valid = true;
    return &sub_cache.prog;
}

/*
 * UI ENTRY POINTS
 */

/*
 * forwsearch -- Search forward.  Get a search string from the user, and
 * 	search for the string.
 */
int forwsearch(int f, int n)
{
    if (n < 0) return backsearch(f, -n);
    return search_interactive(f, n, DIR_FORWARD, "Search");
}

/*
 * fuzzsearch -- Forward search within k mismatches (sublimation fuzzy
 * face). The numeric argument selects k (default 1, capped at 3).
 */
int fuzzsearch(int f, int n)
{
    int status;
    int k = (f && n > 0) ? n : 1;
    if (k > 3) k = 3;

    if ((status = readpattern("Fuzzy search", &pat[0], true)) != true)
        return status;

    fuzz_k = k;
    status = search_execute(pat, DIR_FORWARD, 1);
    fuzz_k = 0;

    if (status == true)
        savematch();
    else
        mlwrite("NOT FOUND");
    return status;
}

/*
 * forwhunt -- Search forward for a previously acquired search string.
 */
int forwhunt(int f, int n)
{
    if (n < 0) return backhunt(f, -n);
    
    if (pat[0] == '\0') {
        mlwrite("NO PATTERN SET");
        return false;
    }

    return search_execute(pat, DIR_FORWARD, n);
}

/*
 * backsearch -- Reverse search.
 */
int backsearch(int f, int n)
{
    if (n < 0) return forwsearch(f, -n);
    return search_interactive(f, n, DIR_REVERSE, "Reverse search");
}

/*
 * backhunt -- Reverse search for a previously acquired search string.
 */
int backhunt(int f, int n)
{
    if (n < 0) return forwhunt(f, -n);

    /* Check both patterns - need at least one */
    if (pat[0] == '\0' && tap[0] == '\0') {
        mlwrite("NO PATTERN SET");
        return false;
    }

    /* scan_buffer_backward expects the NORMAL pattern, not the reversed
     * one; the engine handles direction internally. */
    return search_execute(pat, DIR_REVERSE, n);
}

/*
 * SEARCH LOGIC
 */

/*
 * search_interactive -- Common UI logic for search prompts
 */
static int search_interactive(int f, int n, int direction, const char *restrict prompt)
{
    int status;
    
    if ((status = readpattern(prompt, &pat[0], true)) == true) {
        status = search_execute(pat, direction, n);
        
        if (status == true)
            savematch();
        else
            mlwrite("NOT FOUND");
    }
    return status;
}

/*
 * search_execute -- The main loop for repeating searches 'n' times
 */
static int search_execute(const char *restrict pattern, int direction, int n)
{
    int status = true;
    
    do {
        /* One engine for both faces: scan_buffer selects the regex face
         * under MDMAGIC, literal otherwise. */
        status = scan_buffer(pattern, direction,
                           (direction == DIR_FORWARD) ? POS_END : POS_BEGIN);
    } while ((--n > 0) && status);
    
    return status;
}

/*
 * scan_buffer -- Dispatches to forward or backward scanner
 */
int scan_buffer(const char *restrict pattern, int direction, int beg_or_end)
{
    if (direction == DIR_FORWARD)
        return scan_buffer_forward(pattern, beg_or_end);
    else
        return scan_buffer_backward(pattern, beg_or_end);
}

/*
 * scanner -- Public wrapper for scan_buffer (used by isearch.c)
 */
int scanner(const char *pattern, int direction, int beg_or_end)
{
    return scan_buffer(pattern, direction, beg_or_end);
}

/*
 * scan_buffer_forward -- Optimized forward search
 */
int scan_buffer_forward(const char *restrict pattern, int beg_or_end)
{
    struct line *curline = curwp->w_dotp;
    int curoff = curwp->w_doto;
    int patlen = (int)strlen(pattern);
    bool case_sensitive = ((curwp->w_bufp->b_mode & MDEXACT) != 0);
    bool regex_face = ((curwp->w_bufp->b_mode & MDMAGIC) != 0);
    bool pat_has_nl = (strchr(pattern, '\n') != nullptr) || (strchr(pattern, '\r') != nullptr);

    /* Thread-safe buffer for line text */
    char line_buf[NSTRING];

    /* sublimation strategy: per-line matching, both faces. */
    if (!pat_has_nl) {
        sublimation_search *prog = sub_engine(pattern, case_sensitive, regex_face);
        if (prog) {
            struct line *lp = curline;
            while (lp != curbp->b_linep) {
                int n = llength(lp);
                /* get_line_text truncates to the stack buffer; searching
                 * past the copy would read garbage (the legacy BMH paths
                 * did exactly that on lines over NSTRING-1 chars). */
                if (n > NSTRING - 1) n = NSTRING - 1;
                int start = (lp == curline) ? curoff : 0;
                if (start <= n) {
                    const char *text = get_line_text(lp, line_buf, NSTRING);
                    long end = 0;
                    long idx = sublimation_search_find_from(prog, text,
                                                            (size_t)n,
                                                            (size_t)start,
                                                            &end);
                    if (idx >= 0) {
                        matchline = lp;
                        matchoff = (int)idx;
                        matchlen = (unsigned int)(end - idx);
                        curwp->w_dotp = lp;
                        curwp->w_doto = (beg_or_end == POS_END) ? (int)end
                                                                : (int)idx;
                        curwp->w_flag |= WFMOVE;
                        return true;
                    }
                }
                lp = lforw(lp);
            }
            return false;
        }
    }

    /* Naive Strategy (multi-line patterns, or pattern the engine
     * cannot compile) */
    struct line *scanline;
    int scanoff;
    const char *patptr;
    int c;

    while (!boundry(curline, curoff, DIR_FORWARD)) {
        matchline = curline;
        matchoff = curoff;
        c = nextch(&curline, &curoff, DIR_FORWARD);

        if (eq((unsigned char)c, (unsigned char)pattern[0])) {
            scanline = curline;
            scanoff = curoff;
            patptr = &pattern[0];

            while (*++patptr != '\0') {
                c = nextch(&scanline, &scanoff, DIR_FORWARD);
                if (!eq((unsigned char)c, (unsigned char)*patptr)) goto fail;
            }

            /* Match found */
            curwp->w_dotp = (beg_or_end == POS_END) ? scanline : matchline;
            curwp->w_doto = (beg_or_end == POS_END) ? scanoff : matchoff;
            curwp->w_flag |= WFMOVE;
            return true;
        }
        fail:;
    }
    return false;
}

/*
 * scan_buffer_backward -- Optimized backward search
 */
int scan_buffer_backward(const char *restrict pattern, int beg_or_end)
{
    /* Toggle beg_or_end with direction to match Linus' original behavior.
     * Reverse search called with POS_BEGIN, toggled to POS_END here.
     */
    beg_or_end ^= DIR_REVERSE;

    struct line *curline = curwp->w_dotp;
    int curoff = curwp->w_doto;
    int patlen = (int)strlen(pattern);
    bool case_sensitive = ((curwp->w_bufp->b_mode & MDEXACT) != 0);
    bool regex_face = ((curwp->w_bufp->b_mode & MDMAGIC) != 0);
    bool pat_has_nl = (strchr(pattern, '\n') != nullptr) || (strchr(pattern, '\r') != nullptr);

    /* Thread-safe buffer for line text */
    char line_buf[NSTRING];

    /* sublimation strategy: per-line, walking back from the cursor. The
     * engine only finds forward, so within each line we take the LAST
     * match that starts at or before the limit. */
    if (!pat_has_nl) {
        sublimation_search *prog = sub_engine(pattern, case_sensitive, regex_face);
        if (prog) {
            struct line *lp = curline;
            while (lp != curbp->b_linep) {
                int n = llength(lp);
                if (n > NSTRING - 1) n = NSTRING - 1;
                int limit = (lp == curline) ? (curoff - 1) : (n - 1);
                if (limit >= 0) {
                    const char *text = get_line_text(lp, line_buf, NSTRING);
                    long best = -1, best_end = 0;
                    size_t from = 0;
                    for (;;) {
                        long end = 0;
                        long idx = sublimation_search_find_from(prog, text,
                                                                (size_t)n,
                                                                from, &end);
                        if (idx < 0 || idx > limit) break;
                        best = idx;
                        best_end = end;
                        from = (size_t)idx + 1;
                    }
                    if (best >= 0) {
                        matchline = lp;
                        matchoff = (int)best;
                        matchlen = (unsigned int)(best_end - best);
                        curwp->w_dotp = lp;
                        /* Post-toggle the meanings are mirrored (the
                         * reverse naive walk records its positions
                         * backward): POS_END lands at match START,
                         * POS_BEGIN at match end. The legacy BMH
                         * reverse path got this wrong and diverged
                         * from the naive path for patterns >=
                         * BMH_MIN_LEN; the naive convention is the
                         * tested one. */
                        curwp->w_doto = (beg_or_end == POS_END)
                                            ? (int)best : (int)best_end;
                        curwp->w_flag |= WFMOVE;
                        return true;
                    }
                }
                struct line *prev = lback(lp);
                if (prev == nullptr) break;
                lp = prev;
            }
            return false;
        }
    }

    /* Naive Strategy (multi-line patterns, or pattern the engine
     * cannot compile) */
    struct line *scanline;
    int scanoff;
    const char *patptr;
    int c;

    while (!boundry(curline, curoff, DIR_REVERSE)) {
        matchline = curline;
        matchoff = curoff;
        c = nextch(&curline, &curoff, DIR_REVERSE);

        if (eq((unsigned char)c, (unsigned char)pattern[patlen - 1])) {
            scanline = curline;
            scanoff = curoff;
            patptr = &pattern[patlen - 1];

            while (patptr > pattern) {
                c = nextch(&scanline, &scanoff, DIR_REVERSE);
                if (!eq((unsigned char)c, (unsigned char)*--patptr)) goto fail;
            }

            /* Match found */
            curwp->w_dotp = (beg_or_end == POS_END) ? scanline : matchline;
            curwp->w_doto = (beg_or_end == POS_END) ? scanoff : matchoff;
            curwp->w_flag |= WFMOVE;
            return true;
        }
        fail:;
    }
    return false;
}


/*
 * UTILITIES
 */

/*
 * eq -- Compare two characters.
 */
int eq(unsigned char bc, unsigned char pc)
{
    if ((curwp->w_bufp->b_mode & MDEXACT) == 0) {
        bc = (unsigned char)to_upper(bc);
        pc = (unsigned char)to_upper(pc);
    }
    return bc == pc;
}

/*
 * readpattern -- Read a pattern.
 */
static int readpattern(const char *restrict prompt, char *restrict apat, int srch)
{
    int status;
    char tpat[NPAT + 20];

    safe_strcpy(tpat, prompt, sizeof(tpat));
    safe_strcat(tpat, " (", sizeof(tpat));
    expandp(&apat[0], &tpat[strlen(tpat)], NPAT / 2);    /* add old pattern */
    safe_strcat(tpat, "): ", sizeof(tpat));

    if ((status = minibuf_read(tpat, tpat, NPAT)) == true) {
        safe_strcpy(apat, tpat, NPAT);
        if (srch) {
            rvstrcpy(tap, apat, NPAT);
            mlenold = matchlen = (unsigned int)strlen(apat);
        }
        /* Search-side compilation lives in the sublimation engine
        * (sub_engine cache); only replacement metas still compile
        * here. */
        if ((curwp->w_bufp->b_mode & MDMAGIC) == 0)
            rmcclear();
        else if (!srch)
            status = rmcstr();
    } else if (status == false && apat[0] != 0)    /* Old one */
        status = true;

    return status;
}

/*
 * savematch -- We found the pattern?  Let's save it away.
 */
static void savematch(void)
{
    char *ptr;
    unsigned int j;
    struct line *curline;
    int curoff;

    if (patmatch != nullptr)
        SAFE_FREE(patmatch);

    ptr = patmatch = (char*)safe_alloc(matchlen + 1, "pattern match buffer", __FILE__, __LINE__);

    if (ptr != nullptr) {
        curoff = matchoff;
        curline = matchline;

        for (j = 0; j < matchlen; j++)
            *ptr++ = (char)nextch(&curline, &curoff, DIR_FORWARD);

        *ptr = '\0';
    }
}

/*
 * rvstrcpy -- Reverse string copy with bounds checking.
 */
void rvstrcpy(char *rvstr, char *str, size_t maxlen)
{
    if (maxlen == 0) return;

    size_t len = strlen(str);
    if (len >= maxlen) len = maxlen - 1;

    str += len;
    for (size_t i = 0; i < len; i++)
        *rvstr++ = *--str;
    *rvstr = '\0';
}

/*
 * sreplace -- Search and replace.
 */
int sreplace(int f, int n)
{
    return replaces(false, f, n);
}

/*
 * qreplace -- search and replace with query.
 */
int qreplace(int f, int n)
{
    return replaces(true, f, n);
}

/*
 * replaces -- Search for a string and replace it with another.
 */
static int replaces(int kind, int f, int n)
{
    int status;
    int rlength;
    int numsub = 0;
    int nummatch = 0;
    int nlflag;
    int nlrepl;
    char c;
    char tpat[NPAT];
    struct line *origline;
    int origoff;
    struct line *lastline = nullptr;
    int lastoff = 0;

    if (curbp->b_mode & MDVIEW) return rdonly();
    if (f && n < 0) return false;

    if ((status = readpattern((kind == false ? "Replace" : "Query replace"), &pat[0], true)) != true)
        return status;
    if ((status = readpattern("with", &rpat[0], false)) == ABORT)
        return status;

    rlength = (int)strlen(&rpat[0]);
    nlflag = (pat[matchlen - 1] == '\n');
    nlrepl = false;

    if (kind) {
        safe_strcpy(tpat, "Replace '", sizeof(tpat));
        expandp(&pat[0], &tpat[strlen(tpat)], NPAT / 3);
        safe_strcat(tpat, " with '", sizeof(tpat));
        expandp(&rpat[0], &tpat[strlen(tpat)], NPAT / 3);
        safe_strcat(tpat, "'? ", sizeof(tpat));
    }

    origline = curwp->w_dotp;
    origoff = curwp->w_doto;

    while ((f == false || n > nummatch) && (nlflag == false || nlrepl == false)) {
        if (!scan_buffer(pat, DIR_FORWARD, POS_BEGIN)) break;

        ++nummatch;
        nlrepl = (lforw(curwp->w_dotp) == curwp->w_bufp->b_linep);

        if (kind) {
            int show_prompt = true;  /* true = show main prompt, false = just wait */
            int prompt_done = false;
            while (!prompt_done) {
                if (show_prompt) {
                    mlwrite(&tpat[0], &pat[0], &rpat[0]);
                }
                show_prompt = true;  /* Reset for next iteration */
                update(true);
                c = (char)input_read_byte();
                mlwrite("");

                switch (c) {
                case 'Y': case 'y': case ' ':
                    savematch();
                    prompt_done = true;
                    break;
                case 'N': case 'n':
                    move_char_forward(false, 1);
                    prompt_done = true;
                    continue;  /* Skip to next match */
                case '!':
                    kind = false;
                    prompt_done = true;
                    break;
                case 'U': case 'u':
                    if (lastline == nullptr) {
                        TTbeep();
                        continue;  /* show_prompt still true, loop again */
                    }
                    curwp->w_dotp = lastline;
                    curwp->w_doto = lastoff;
                    lastline = nullptr;
                    lastoff = 0;
                    move_char_backward(false, rlength);
                    matchline = curwp->w_dotp;
                    matchoff = curwp->w_doto;
                    status = delins(rlength, patmatch, false);
                    if (status != true) return status;
                    --numsub;
                    move_char_backward(false, (int)mlenold);
                    matchline = curwp->w_dotp;
                    matchoff = curwp->w_doto;
                    continue;  /* show_prompt still true, loop again */
                case '.':
                    curwp->w_dotp = origline;
                    curwp->w_doto = origoff;
                    curwp->w_flag |= WFMOVE;
                    [[fallthrough]];
                case BELL:
                    mlwrite("ABORTED!");
                    return false;
                default:
                    TTbeep();
                    [[fallthrough]];
                case '?':
                    mlwrite
                      ("[Y]es, [N]o, [!]Do rest, [U]ndo last, [^G]Abort, [.]Abort back, [?]Help: ");
                    show_prompt = false;  /* Just wait, don't show main prompt */
                    continue;
                }
            }
            if (c == 'N' || c == 'n') continue;  /* Skip rest of loop for 'N' */
        }

        status = delins((int)matchlen, &rpat[0], true);
        if (status != true) return status;

        if (kind) {
            lastline = curwp->w_dotp;
            lastoff = curwp->w_doto;
        }
        numsub++;
    }

    mlwrite("%d SUBSTITUTIONS", numsub);
    return true;
}

/*
 * delins -- Delete a specified length from the current point
 */
int delins(int dlength, char *instr, int use_meta)
{
    int status;
    struct magic_replacement *rmcptr;

    if ((status = ldelete((long) dlength, false)) != true)
        mlwrite("ERROR WHILE DELETING");
    else if ((rmagical && use_meta) && (curwp->w_bufp->b_mode & MDMAGIC) != 0) {
        rmcptr = &rmcpat[0];
        while (rmcptr->mc_type != MCNIL && status == true) {
            if (rmcptr->mc_type == LITCHAR)
                status = linstr(rmcptr->rstr);
            else
                status = linstr(patmatch);
            rmcptr++;
        }
    } else
        status = linstr(instr);

    return status;
}

/*
 * expandp -- Expand control key sequences for output.
 */
int expandp(char *srcstr, char *deststr, int maxlength)
{
    unsigned char c;
    while ((c = (unsigned char)*srcstr++) != 0) {
        if (c == '\n') {
            *deststr++ = '<'; *deststr++ = 'N'; *deststr++ = 'L'; *deststr++ = '>';
            maxlength -= 4;
        }
        else if ((c > 0 && c < 0x20) || c == DEL_KEY) {
            *deststr++ = '^';
            *deststr++ = (char)(c ^ 0x40);
            maxlength -= 2;
        }
        else if (c == '%') {
            *deststr++ = '%'; *deststr++ = '%';
            maxlength -= 2;
        }
        else {
            *deststr++ = (char)c;
            maxlength--;
        }
        if (maxlength < 4) {
            *deststr++ = '$'; *deststr = '\0';
            return false;
        }
    }
    *deststr = '\0';
    return true;
}

/*
 * boundary -- Return whether search may continue.
 */
int boundry(struct line *curline, int curoff, int dir)
{
    if (dir == DIR_FORWARD) {
        return (curoff == llength(curline)) && (lforw(curline) == curbp->b_linep);
    } else {
        return (curoff == 0) && (lback(curline) == curbp->b_linep);
    }
}

/*
 * nextch -- retrieve the next/previous character in the buffer
 */
static int nextch(struct line **pcurline, int *pcuroff, int dir)
{
    struct line *curline = *pcurline;
    int curoff = *pcuroff;
    int c;

    if (dir == DIR_FORWARD) {
        if (curoff == llength(curline)) {
            curline = lforw(curline);
            curoff = 0;
            c = '\n';
        } else
            c = lgetc(curline, curoff++);
    } else {
        if (curoff == 0) {
            curline = lback(curline);
            curoff = llength(curline);
            c = '\n';
        } else
            c = lgetc(curline, --curoff);
    }
    *pcurline = curline;
    *pcuroff = curoff;

    return c;
}

/*
 * LEGACY MAGIC MODE SUPPORT
 */

static int rmcstr(void)
{
    struct magic_replacement *rmcptr;
    char *patptr;
    int status = true;
    int mj = 0;

    patptr = &rpat[0];
    rmcptr = &rmcpat[0];
    rmagical = false;

    while (*patptr && status == true) {
        switch (*patptr) {
        case MC_DITTO:
            if (mj != 0) {
                rmcptr->mc_type = LITCHAR;
                if ((rmcptr->rstr = (char*)safe_alloc((size_t)mj + 1, "replace string", __FILE__, __LINE__)) == nullptr) {
                    mlwrite("OUT OF MEMORY");
                    status = false;
                    break;
                }
                if (mj > 0) memcpy(rmcptr->rstr, patptr - mj, (size_t)mj);
                rmcptr->rstr[mj] = '\0';
                rmcptr++; mj = 0;
            }
            rmcptr->mc_type = DITTO;
            rmcptr++; rmagical = true;
            break;
        case MC_ESC:
            rmcptr->mc_type = LITCHAR;
            if ((rmcptr->rstr = (char*)safe_alloc((size_t)mj + 2, "replace escape string", __FILE__, __LINE__)) == nullptr) {
                mlwrite("OUT OF MEMORY");
                status = false;
                break;
            }
            if (mj + 1 > 0) memcpy(rmcptr->rstr, patptr - mj, (size_t)(mj + 1));
            rmcptr->rstr[mj + 1] = '\0';
            if (*(patptr + 1) != '\0') *((rmcptr->rstr) + mj) = *++patptr;
            rmcptr++; mj = 0;
            rmagical = true;
            break;
        default:
            mj++;
        }
        patptr++;
    }

    if (rmagical && mj > 0) {
        rmcptr->mc_type = LITCHAR;
        if ((rmcptr->rstr = (char*)safe_alloc((size_t)mj + 1, "replace literal string", __FILE__, __LINE__)) == nullptr) {
            mlwrite("OUT OF MEMORY");
            status = false;
        }
        if (mj > 0) memcpy(rmcptr->rstr, patptr - mj, (size_t)mj);
        rmcptr->rstr[mj] = '\0';
        rmcptr++;
    }
    rmcptr->mc_type = MCNIL;
    return status;
}

void rmcclear(void)
{
    struct magic_replacement *rmcptr = &rmcpat[0];
    while (rmcptr->mc_type != MCNIL) {
        if (rmcptr->mc_type == LITCHAR) SAFE_FREE(rmcptr->rstr);
        rmcptr++;
    }
    rmcpat[0].mc_type = MCNIL;
}


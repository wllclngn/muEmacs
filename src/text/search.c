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
#include "boyer_moore.h"
#include "nfa.h"
#include "parallel_search.h"

/*
 * Search Strategy Enumeration
 */
typedef enum {
    SEARCH_STRATEGY_NAIVE,
    SEARCH_STRATEGY_BMH,      /* Boyer-Moore-Horspool */
    SEARCH_STRATEGY_TWOWAY,   /* Two-Way Algorithm */
    SEARCH_STRATEGY_NFA,      /* NFA Regex */
    SEARCH_STRATEGY_MAGIC     /* Legacy Magic Mode */
} search_strategy_t;

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

/* Legacy Magic Mode Forward Declarations */
#if defined(MAGIC)
static int mcstr(void);
static int rmcstr(void);
static int mcscanner(struct magic *mcpatrn, int direct, int beg_or_end);
static int amatch(struct magic *mcptr, int direct, struct line **pcwline, int *pcwoff);
static int cclmake(char **ppatptr, struct magic *mcptr);
static int biteq(int bc, char *cclmap);
static char *clearbits(void);
static void setbit(int bc, char *cclmap);
static int mceq(int bc, struct magic *mt);
void mcclear(void);
void rmcclear(void);

static short int magical;
static short int rmagical;
static struct magic mcpat[NPAT]; /* The magic pattern. */
static struct magic tapcm[NPAT]; /* The reversed magic pattern. */
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
 * UI ENTRY POINTS
 */

/*
 * forwsearch -- Search forward.  Get a search string from the user, and
 * 	search for the string.
 */
int forwsearch(int f, int n)
{
    if (n < 0) return backsearch(f, -n);
    
    // Try parallel search for simple forward searches
    // Only if not in magic mode (regex not supported in parallel yet)
    if ((curwp->w_bufp->b_mode & MDMAGIC) == 0) {
        // Ask user for pattern first, then dispatch
        int status;
        if ((status = readpattern("Search", &pat[0], true)) == true) {
            // Try parallel first
            if (parallel_search(pat, DIR_FORWARD)) {
                savematch();
                return true;
            }
            // Fallback to serial if parallel didn't find it or wasn't applicable
            status = search_execute(pat, DIR_FORWARD, n);
            if (status == true)
                savematch();
            else
                mlwrite("NOT FOUND");
            return status;
        }
        return status;
    }

    return search_interactive(f, n, DIR_FORWARD, "Search");
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

    if ((curwp->w_bufp->b_mode & MDMAGIC) != 0 && mcpat[0].mc_type == MCNIL) {
        if (!mcstr()) return false;
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
    if ((curwp->w_bufp->b_mode & MDMAGIC) != 0 && tapcm[0].mc_type == MCNIL) {
        if (!mcstr()) return false;
    }

    /* Note: The modernized scan_buffer_backward expects the NORMAL pattern,
     * not the reversed one. The Boyer-Moore search handles direction internally.
     * Use pat for the modernized scanner (non-magic mode). */
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
        if ((magical && curwp->w_bufp->b_mode & MDMAGIC) != 0) {
            /* Use legacy magic scanner */
            status = mcscanner((direction == DIR_FORWARD) ? &mcpat[0] : &tapcm[0],
                             direction,
                             (direction == DIR_FORWARD) ? POS_END : POS_BEGIN);
        } else {
            /* Use modernized scanner */
            status = scan_buffer(pattern, direction,
                               (direction == DIR_FORWARD) ? POS_END : POS_BEGIN);
        }
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
    int patlen = strlen(pattern);
    bool case_sensitive = ((curwp->w_bufp->b_mode & MDEXACT) != 0);
    bool pat_has_nl = (strchr(pattern, '\n') != nullptr) || (strchr(pattern, '\r') != nullptr);
    
    /* Thread-safe buffer for line text */
    char line_buf[NSTRING];

    /* NFA Strategy (Regex Lite) */
    const char* nfa_env = getenv("UEMACS_SEARCH_NFA");
    bool nfa_enabled = !(nfa_env && strcmp(nfa_env, "0") == 0);
    
    if (nfa_enabled && (curwp->w_bufp->b_mode & MDMAGIC) != 0) {
#ifdef ENABLE_SEARCH_NFA
        nfa_program_info nfa = {0};
        if (nfa_compile(pattern, case_sensitive, &nfa)) {
            struct line* mlp = nullptr; 
            int moff = 0;
            if (nfa_search_forward(&nfa, curline, curoff, beg_or_end, &mlp, &moff)) {
                matchline = mlp; matchoff = moff;
                curwp->w_dotp = mlp; curwp->w_doto = moff;
                curwp->w_flag |= WFMOVE;
                return true;
            }
        }
#endif
    }

    /* Boyer-Moore-Horspool Strategy (Single line, no newlines) */
    if (!pat_has_nl && patlen >= BMH_MIN_LEN) {
        struct boyer_moore_context bm = {0};
        if (bm_init(&bm, (const unsigned char*)pattern, patlen, case_sensitive) == 0) {
            struct line *lp = curline;
            int off = curoff;
            
            while (lp != curbp->b_linep) {
                int n = llength(lp);
                if (n > 0) {
                    int start = (lp == curline) ? off : 0;
                    const char *line_text = get_line_text(lp, line_buf, NSTRING);
                    /* Ensure start is within bounds */
                    int safe_start = (start < n) ? start : (n - 1);
                    if (safe_start >= 0) {
                        int idx = bm_search(&bm, (const unsigned char*)line_text, n, safe_start);
                        if (idx >= 0) {
                            matchline = lp; matchoff = idx;
                            curwp->w_dotp = lp;
                            curwp->w_doto = (beg_or_end == POS_END) ? (idx + patlen) : idx;
                            curwp->w_flag |= WFMOVE;
                            bm_free(&bm);
                            return true;
                        }
                    }
                }
                lp = lforw(lp);
            }
        }
        bm_free(&bm);
    }

    /* Naive Strategy (Fallthrough) */
    struct line *scanline;
    int scanoff;
    const char *patptr;
    int c;

    while (!boundry(curline, curoff, DIR_FORWARD)) {
        matchline = curline;
        matchoff = curoff;
        c = nextch(&curline, &curoff, DIR_FORWARD);

        if (eq(c, pattern[0])) {
            scanline = curline;
            scanoff = curoff;
            patptr = &pattern[0];

            while (*++patptr != '\0') {
                c = nextch(&scanline, &scanoff, DIR_FORWARD);
                if (!eq(c, *patptr)) goto fail;
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
    /* Reverse search beg_or_end toggle handled by caller logic usually,
     * but legacy scanner did `beg_or_end ^= direct`. 
     * Since direct=DIR_REVERSE=1, we toggle it here to match legacy behavior.
     */
    beg_or_end ^= DIR_REVERSE;

    struct line *curline = curwp->w_dotp;
    int curoff = curwp->w_doto;
    int patlen = strlen(pattern);
    bool case_sensitive = ((curwp->w_bufp->b_mode & MDEXACT) != 0);
    bool pat_has_nl = (strchr(pattern, '\n') != nullptr) || (strchr(pattern, '\r') != nullptr);
    
    /* Thread-safe buffer for line text */
    char line_buf[NSTRING];

    /* Boyer-Moore-Horspool Strategy (Single line, no newlines) */
    if (!pat_has_nl && patlen >= BMH_MIN_LEN) {
        struct boyer_moore_context bm = {0};
        if (bm_init(&bm, (const unsigned char*)pattern, patlen, case_sensitive) == 0) {
            struct line *lp = curline;
            int off = curoff;
            
            while (true) {
                if (lp == curbp->b_linep) break;
                int n = llength(lp);
                if (n > 0) {
                    int start = (lp == curline) ? (off - 1) : (n - 1);
                    if (start >= 0) {
                        const char *line_text = get_line_text(lp, line_buf, NSTRING);
                        int idx = bm_search_reverse(&bm, (const unsigned char*)line_text, n, start);
                        if (idx >= 0) {
                            matchline = lp;
                            matchoff = idx;
                            curwp->w_dotp = lp;
                            curwp->w_doto = (beg_or_end == POS_END) ? (idx + patlen) : idx;
                            curwp->w_flag |= WFMOVE;
                            bm_free(&bm);
                            return true;
                        }
                    }
                }
                /* Move to previous line */
                struct line *prev = lback(lp);
                if (prev == nullptr) break;
                lp = prev;
            }
        }
        bm_free(&bm);
    }

    /* Naive Strategy */
    struct line *scanline;
    int scanoff;
    const char *patptr;
    int c;

    while (!boundry(curline, curoff, DIR_REVERSE)) {
        matchline = curline;
        matchoff = curoff;
        c = nextch(&curline, &curoff, DIR_REVERSE);

        if (eq(c, pattern[patlen - 1])) {
            scanline = curline;
            scanoff = curoff;
            patptr = &pattern[patlen - 1];

            while (patptr > pattern) {
                c = nextch(&scanline, &scanoff, DIR_REVERSE);
                if (!eq(c, *--patptr)) goto fail;
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
		bc = to_upper(bc);
		pc = to_upper(pc);
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
			mlenold = matchlen = strlen(apat);
		}
		if ((curwp->w_bufp->b_mode & MDMAGIC) == 0) {
			mcclear();
			rmcclear();
		} else
			status = srch ? mcstr() : rmcstr();
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
			*ptr++ = nextch(&curline, &curoff, DIR_FORWARD);

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

	rlength = strlen(&rpat[0]);
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
		if ((magical && curwp->w_bufp->b_mode & MDMAGIC) != 0) {
			if (!mcscanner(&mcpat[0], DIR_FORWARD, POS_BEGIN)) break;
		} else if (!scan_buffer(pat, DIR_FORWARD, POS_BEGIN)) break;

		++nummatch;
		nlrepl = (lforw(curwp->w_dotp) == curwp->w_bufp->b_linep);

		if (kind) {
		      pprompt:mlwrite(&tpat[0], &pat[0], &rpat[0]);
		      qprompt:
			update(true);
			c = input_read_byte();
			mlwrite("");

			switch (c) {
			case 'Y': case 'y': case ' ':
				savematch();
				break;
			case 'N': case 'n':
				move_char_forward(false, 1);
				continue;
			case '!':
				kind = false;
				break;
			case 'U': case 'u':
				if (lastline == nullptr) {
					TTbeep();
					goto pprompt;
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
				move_char_backward(false, mlenold);
				matchline = curwp->w_dotp;
				matchoff = curwp->w_doto;
				goto pprompt;
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
				goto qprompt;
			}
		}

		status = delins(matchlen, &rpat[0], true);
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
	while ((c = *srcstr++) != 0) {
		if (c == '\n') {
			*deststr++ = '<'; *deststr++ = 'N'; *deststr++ = 'L'; *deststr++ = '>';
			maxlength -= 4;
		}
		else if ((c > 0 && c < 0x20) || c == DEL_KEY) {
			*deststr++ = '^';
			*deststr++ = c ^ 0x40;
			maxlength -= 2;
		}
		else if (c == '%') {
			*deststr++ = '%'; *deststr++ = '%';
			maxlength -= 2;
		}
		else {
			*deststr++ = c;
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

static int mcstr(void)
{
	struct magic *mcptr, *rtpcm;
	char *patptr;
	int mj = 0;
	int pchr;
	int status = true;
	int does_closure = false;

	if (magical) mcclear();
	magical = false;
	mcptr = &mcpat[0];
	patptr = &pat[0];

	while ((pchr = *patptr) && status) {
		switch (pchr) {
		case MC_CCL:
			status = cclmake(&patptr, mcptr);
			magical = true;
			does_closure = true;
			break;
		case MC_BOL:
			if (mj != 0) goto litcase;
			mcptr->mc_type = BOL;
			magical = true;
			does_closure = false;
			break;
		case MC_EOL:
			if (*(patptr + 1) != '\0') goto litcase;
			mcptr->mc_type = EOL;
			magical = true;
			does_closure = false;
			break;
		case MC_ANY:
			mcptr->mc_type = ANY;
			magical = true;
			does_closure = true;
			break;
		case MC_CLOSURE:
			if (!does_closure) goto litcase;
			mj--; mcptr--;
			mcptr->mc_type |= CLOSURE;
			magical = true;
			does_closure = false;
			break;
		case MC_ESC:
			if (*(patptr + 1) != '\0') {
				pchr = *++patptr;
				magical = true;
			}
			[[fallthrough]];
		default:
		      litcase:
            mcptr->mc_type = LITCHAR;
			mcptr->u.lchar = pchr;
			does_closure = (pchr != '\n');
			break;
		}
		mcptr++; patptr++; mj++;
	}
	mcptr->mc_type = MCNIL;
	if (status) {
		rtpcm = &tapcm[0];
		while (--mj >= 0) *rtpcm++ = *--mcptr;
		rtpcm->mc_type = MCNIL;
	} else {
		(--mcptr)->mc_type = MCNIL;
		mcclear();
	}
	return status;
}

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
				if ((rmcptr->rstr = (char*)safe_alloc(mj + 1, "replace string", __FILE__, __LINE__)) == nullptr) {
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
			if ((rmcptr->rstr = (char*)safe_alloc(mj + 2, "replace escape string", __FILE__, __LINE__)) == nullptr) {
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
		if ((rmcptr->rstr = (char*)safe_alloc(mj + 1, "replace literal string", __FILE__, __LINE__)) == nullptr) {
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

void mcclear(void)
{
	struct magic *mcptr = &mcpat[0];
	while (mcptr->mc_type != MCNIL) {
		if ((mcptr->mc_type & MASKCL) == CCL || (mcptr->mc_type & MASKCL) == NCCL)
			SAFE_FREE(mcptr->u.cclmap);
		mcptr++;
	}
	mcpat[0].mc_type = tapcm[0].mc_type = MCNIL;
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

static int mceq(int bc, struct magic *mt)
{
	int result;
	bc = bc & 0xFF;
	switch (mt->mc_type & MASKCL) {
	case LITCHAR:
		result = eq(bc, mt->u.lchar);
		break;
	case ANY:
		result = (bc != '\n');
		break;
	case CCL:
		if (!(result = biteq(bc, mt->u.cclmap))) {
			if ((curwp->w_bufp->b_mode & MDEXACT) == 0 && is_letter(bc))
				result = biteq(SAFE_CHCASE(bc), mt->u.cclmap);
		}
		break;
	case NCCL:
		result = !biteq(bc, mt->u.cclmap);
		if ((curwp->w_bufp->b_mode & MDEXACT) == 0 && is_letter(bc))
			result &= !biteq(SAFE_CHCASE(bc), mt->u.cclmap);
		break;
	default:
		result = false;
		break;
	}
	return result;
}

static int cclmake(char **ppatptr, struct magic *mcptr)
{
	char *bmap;
	char *patptr;
	int pchr, ochr;

	if ((bmap = clearbits()) == nullptr) {
		mlwrite("OUT OF MEMORY");
		return false;
	}
	mcptr->u.cclmap = bmap;
	patptr = *ppatptr;

	if (*++patptr == MC_NCCL) {
		patptr++; mcptr->mc_type = NCCL;
	} else mcptr->mc_type = CCL;

	if ((ochr = *patptr) == MC_ECCL) {
		mlwrite("NO CHARACTERS IN CHARACTER CLASS");
		return false;
	} else {
		if (ochr == MC_ESC) ochr = *++patptr;
		setbit(ochr, bmap);
		patptr++;
	}

	while (ochr != '\0' && (pchr = *patptr) != MC_ECCL) {
		switch (pchr) {
		case MC_RCCL:
			if (*(patptr + 1) == MC_ECCL) setbit(pchr, bmap);
			else {
				pchr = *++patptr;
				while (++ochr <= pchr) setbit(ochr, bmap);
			}
			break;
		case MC_ESC:
			pchr = *++patptr;
			[[fallthrough]];
		default:
			setbit(pchr, bmap);
			break;
		}
		patptr++; ochr = pchr;
	}
	*ppatptr = patptr;
	if (ochr == '\0') {
		mlwrite("CHARACTER CLASS NOT ENDED");
		SAFE_FREE(bmap);
		return false;
	}
	return true;
}

static int biteq(int bc, char *cclmap)
{
	bc = bc & 0xFF;
	if (bc >= HICHAR) return false;
	return (*(cclmap + (bc >> 3)) & (1U << (bc & 7))) ? true : false;
}

static char *clearbits(void)
{
	char *cclmap;
	if ((cclmap = (char *)safe_alloc(HIBYTE, "character class map", __FILE__, __LINE__)) != nullptr) {
		memset(cclmap, 0, HIBYTE);  /* O(1) vs O(n) manual loop */
	}
	return cclmap;
}

static void setbit(int bc, char *cclmap)
{
	bc = bc & 0xFF;
	if (bc < HICHAR) *(cclmap + (bc >> 3)) |= (1U << (bc & 7));
}

/*
 * mcscanner -- Search for a meta-pattern in either direction.
 */
static int mcscanner(struct magic *mcpatrn, int direct, int beg_or_end)
{
	struct line *curline;
	int curoff;

	beg_or_end ^= direct;
	mlenold = matchlen;
	curline = curwp->w_dotp;
	curoff = curwp->w_doto;

	while (!boundry(curline, curoff, direct)) {
		matchline = curline;
		matchoff = curoff;
		matchlen = 0;

		if (amatch(mcpatrn, direct, &curline, &curoff)) {
			if (beg_or_end == POS_END) {
				curwp->w_dotp = curline;
				curwp->w_doto = curoff;
			} else {
				curwp->w_dotp = matchline;
				curwp->w_doto = matchoff;
			}
			curwp->w_flag |= WFMOVE;
			return true;
		}
		nextch(&curline, &curoff, direct);
	}
	return false;
}

static int amatch(struct magic *mcptr, int direct, struct line **pcwline, int *pcwoff)
{
	int c;
	struct line *curline;
	int curoff;
	int nchars;

	curline = *pcwline;
	curoff = *pcwoff;

	if (mcptr->mc_type == BOL) {
		if (curoff != 0) return false;
		mcptr++;
	}
	if (mcptr->mc_type == EOL) {
		if (curoff != llength(curline)) return false;
		mcptr++;
	}

	while (mcptr->mc_type != MCNIL) {
		c = nextch(&curline, &curoff, direct);
		if (mcptr->mc_type & CLOSURE) {
			nchars = 0;
			while (c != '\n' && mceq(c, mcptr)) {
				c = nextch(&curline, &curoff, direct);
				nchars++;
			}
			mcptr++;
			for (;;) {
				c = nextch(&curline, &curoff, direct ^ DIR_REVERSE);
				if (amatch(mcptr, direct, &curline, &curoff)) {
					matchlen += nchars;
					goto success;
				}
				if (nchars-- == 0) return false;
			}
		} else {
			if (mcptr->mc_type == BOL) {
				if (curoff == llength(curline)) {
					c = nextch(&curline, &curoff, direct ^ DIR_REVERSE);
					goto success;
				} else return false;
			}
			if (mcptr->mc_type == EOL) {
				if (curoff == 0) {
					c = nextch(&curline, &curoff, direct ^ DIR_REVERSE);
					goto success;
				} else return false;
			}
			if (!mceq(c, mcptr)) return false;
		}
		matchlen++;
		mcptr++;
	}

      success:
	*pcwline = curline;
	*pcwoff = curoff;
	return true;
}
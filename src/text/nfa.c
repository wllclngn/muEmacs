/*
 * nfa.c - Thompson NFA (regex-lite) for MAGIC search
 * Features: literals, dot (.), anchors (^,$), closure (*) on single atoms.
 * Zero-heap runtime: fixed arenas and active sets.
 */

#include "nfa.h"
#include <string.h>
#include <ctype.h>
#include <stdatomic.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"

/* Fixed capacities */
#define NFA_MAX_STATES 2048
#define NFA_MAX_LIST   4096

typedef enum { ST_CHAR, ST_ANY, ST_CLASS, ST_SPLIT, ST_MATCH, ST_BOL, ST_EOL } stype_t;

typedef struct {
    stype_t type;
    unsigned char c; /* for ST_CHAR */
    unsigned char cls[32]; /* 256-bit char class for ST_CLASS */
    int out;
    int out1; /* for split */
} nfa_state;

static nfa_state arena[NFA_MAX_STATES];
static int arena_used = 0;

static inline unsigned char norm_byte(unsigned char b, bool cs) {
    return cs ? b : (unsigned char)tolower(b);
}

static int add_state(stype_t t, unsigned char c, int out, int out1) {
    if (arena_used >= NFA_MAX_STATES) return -1;
    nfa_state st = {0}; st.type=t; st.c=c; st.out=out; st.out1=out1;
    arena[arena_used] = st;
    return arena_used++;
}

/* Thompson fragments with patch lists are simplified here: we only support concatenation and '*' */
typedef struct { int start; int out; } frag;

static int patch(int s, int target) {
    if (s < 0) return -1;
    if (arena[s].type == ST_SPLIT) {
        if (arena[s].out1 < 0) arena[s].out1 = target;
        else arena[s].out = target;
    } else {
        arena[s].out = target;
    }
    return s;
}

static inline void cls_set(unsigned char *cls, int b) { cls[b >> 3] |= (1u << (b & 7)); }
static inline int cls_has(const unsigned char *cls, int b) { return (cls[b >> 3] & (1u << (b & 7))) != 0; }

/* Compile subset: ^? (atom\*)* $? ; atom: '.', literal, or char class [...] */
bool nfa_compile(const char* pattern, bool case_sensitive, nfa_program_info* out_info) {
    if (!pattern || !out_info) return false;
    if (strlen(pattern) == 0) return false;  // Empty patterns are not valid
    arena_used = 0;

    const char* p = pattern;
    bool cs = case_sensitive;
    int start_anchor = 0, end_anchor = 0;
    if (*p == '^') { start_anchor = 1; p++; }

    int start = -1;
    int last = -1;

    /* Optional sequence of (atom or atom*) */
    while (*p && *p != '$') {
        if (*p == '\\') { /* escape next as literal */
            p++; if (!*p) break;
            int s = add_state(ST_CHAR, norm_byte((unsigned char)*p, cs), -1, -1);
            if (s < 0) return false;
            if (start == -1) start = s; else patch(last, s);
            last = s;
            p++;
            continue;
        }
        if (*p == '.') {
            int s = add_state(ST_ANY, 0, -1, -1);
            if (s < 0) return false;
            if (start == -1) start = s; else patch(last, s);
            last = s;
            p++;
            if (*p == '*') {
                /* Closure: create split; loop back from atom to split */
                int split = add_state(ST_SPLIT, 0, -1, -1);
                if (split < 0) return false;
                arena[s].out = split;      /* atom -> split */
                arena[split].out = s;       /* split -> atom (loop) */
                if (start == s) start = split;
                else patch(last, split);
                last = split;
                p++;
            }
            continue;
        }
        if (*p == '[') {
            p++;
            int negate = 0;
            unsigned char cls[32] = {0};
            if (*p == '^') { negate = 1; p++; }
            if (*p == ']' || *p == '\0') return false;
            while (*p && *p != ']') {
                unsigned char a = (unsigned char)(case_sensitive ? *p : tolower(*p));
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    unsigned char b = (unsigned char)(case_sensitive ? p[2] : tolower(p[2]));
                    if (a > b) { unsigned char t=a; a=b; b=t; }
                    for (unsigned char x = a; x <= b; ++x) cls_set(cls, x);
                    p += 3;
                } else {
                    cls_set(cls, a); p++;
                }
            }
            if (*p != ']') {
                return false;
            }
            p++;
            if (negate) {
                for (int bi=0; bi<32; ++bi) cls[bi] = (unsigned char)~cls[bi];
                /* Disallow newline */
                cls[('\n') >> 3] &= (unsigned char)~(1u << (('\n') & 7));
            }
            int s = add_state(ST_CLASS, 0, -1, -1);
            if (s < 0) return false;
            memcpy(arena[s].cls, cls, sizeof(cls));
            if (start == -1) start = s; else patch(last, s);
            last = s;
            if (*p == '*') {
                int split = add_state(ST_SPLIT, 0, -1, -1);
                if (split < 0) return false;
                arena[s].out = split;
                arena[split].out = s;
                if (start == s) start = split; else patch(last, split);
                last = split;
                p++;
            }
            continue;
        }
        if (*p == '*') {
            /* malformed: stray *; reject */
            return false;
        }
        /* literal */
        int s = add_state(ST_CHAR, norm_byte((unsigned char)*p, cs), -1, -1);
        if (s < 0) return false;
        if (start == -1) start = s; else patch(last, s);
        last = s;
        p++;
        if (*p == '*') {
            int split = add_state(ST_SPLIT, 0, -1, -1);
            if (split < 0) return false;
            arena[s].out = split;
            arena[split].out = s;
            if (start == s) start = split; else patch(last, split);
            last = split;
            p++;
        }
    }
    if (*p == '$') { end_anchor = 1; p++; }
    if (*p != '\0') {
        /* unsupported constructs present */
        return false;
    }
    /* Anchors wrap the start with BOL and end with EOL */
    if (start_anchor) {
        int bol = add_state(ST_BOL, 0, start, -1);
        if (bol < 0) return false;
        start = bol;
    }
    int match = add_state(ST_MATCH, 0, -1, -1);
    if (match < 0) return false;
    if (last >= 0) patch(last, end_anchor ? add_state(ST_EOL, 0, match, -1) : match);
    else {
        /* empty pattern: match at start */
        if (end_anchor) {
            int eol = add_state(ST_EOL, 0, match, -1);
            if (eol < 0) return false;
            start = start_anchor ? add_state(ST_BOL, 0, eol, -1) : eol;
        } else {
            start = start_anchor ? add_state(ST_BOL, 0, match, -1) : match;
        }
    }

    out_info->start_state = start;
    out_info->state_count = arena_used;
    out_info->case_sensitive = cs;
    return true;
}

/* Active state set */
typedef struct { int idx[NFA_MAX_LIST]; int n; } slist;

static void list_clear(slist* l) { l->n = 0; }
static void list_add(slist* l, int s) { if (l->n < NFA_MAX_LIST) l->idx[l->n++] = s; }

/* Epsilon-closure add: add state and follow splits/BOL/EOL anchors that accept at this position */
static void add_epsilon(const nfa_program_info* prog, slist* l, int s, bool at_bol, bool at_eol) {
    for (;;) {
        if (s < 0) return;
        nfa_state* st = &arena[s];
        if (st->type == ST_SPLIT) {
            int a = st->out, b = st->out1;
            /* Default out1 = -1; if so, treat as pass-through */
            if (b >= 0) { add_epsilon(prog, l, b, at_bol, at_eol); }
            s = a; /* tail-recurse */
            continue;
        } else if (st->type == ST_BOL) {
            if (!at_bol) return; /* reject this path */
            s = st->out; continue;
        } else if (st->type == ST_EOL) {
            if (!at_eol) return;
            s = st->out; continue;
        } else {
            list_add(l, s);
            return;
        }
    }
}

static void step(const nfa_program_info* prog, const slist* cur, unsigned char byte, bool is_nl, slist* next) {
    list_clear(next);
    for (int i = 0; i < cur->n; i++) {
        int s = cur->idx[i];
        const nfa_state* st = &arena[s];
        switch (st->type) {
            case ST_CHAR:
                if (!is_nl && byte == st->c) add_epsilon(prog, next, st->out, false, false);
                break;
            case ST_ANY:
                if (!is_nl) add_epsilon(prog, next, st->out, false, false);
                break;
            case ST_CLASS:
                if (!is_nl && cls_has(st->cls, byte)) add_epsilon(prog, next, st->out, false, false);
                break;
            case ST_MATCH:
                /* Do nothing; match will be detected separately */
                break;
            default:
                break;
        }
    }
}

/* Iterate across buffer lines forward */
bool nfa_search_forward(const nfa_program_info* prog,
                        struct line* start_lp,
                        int start_off,
                        int beg_or_end,
                        struct line** match_lp,
                        int* match_off) {
    if (!prog || !start_lp || !match_lp || !match_off) return false;

    slist cur = {0}, next = {0};
    struct line* lp = start_lp;
    int off = start_off;
    struct line* run_start_lp = lp;
    int run_start_off = off;
    bool at_bol = (off == 0);
    bool have_match = false;
    struct line* m_lp = NULL; int m_off = 0; /* match boundary to report */

    /* seed epsilon-closure at start */
    list_clear(&cur);
    add_epsilon(prog, &cur, prog->start_state, at_bol, false);
    // Zero-length match check at start
    for (int i = 0; i < cur.n; i++) {
        if (arena[cur.idx[i]].type == ST_MATCH) {
            *match_lp = start_lp; *match_off = start_off; return true;
        }
    }

    for (;;) {
        if (lp == curbp->b_linep) break;
        int n = llength(lp);
        bool at_eol = (off == n);
        if (at_eol) {
            /* Process EOL as a special non-consuming check via epsilon closure */
            /* Advance to next line */
            lp = lforw(lp);
            off = 0;
            at_bol = true;
            list_clear(&next);
            /* Recompute closure at new position */
            list_clear(&cur);
            add_epsilon(prog, &cur, prog->start_state, at_bol, false);
            run_start_lp = lp; run_start_off = off;
            continue;
        }
        unsigned char b = (unsigned char)lp->l_text[off];
        bool is_nl = false;

        /* Step */
        step(prog, &cur, norm_byte(b, prog->case_sensitive), is_nl, &next);

        /* Epsilon-closure on next and check for match */
        slist closure = {0};
        for (int i = 0; i < next.n; i++) {
            add_epsilon(prog, &closure, next.idx[i], false, false);
        }
        for (int i = 0; i < closure.n; i++) {
            if (arena[closure.idx[i]].type == ST_MATCH) {
                have_match = true;
                if (beg_or_end == 1 /* PTEND */) { m_lp = lp; m_off = off + 1; }
                else { m_lp = run_start_lp; m_off = run_start_off; }
                break;
            }
        }
        /* Prepare for next byte */
        cur = closure; /* copy by value */
        list_clear(&next);
        off++;
        at_bol = false;
        if (have_match) break;
    }

    if (have_match) {
        *match_lp = m_lp; *match_off = m_off; return true;
    }
    return false;
}

/*
 * nfa.h - Thompson NFA (regex-lite) for MAGIC search
 * Supports: literals, dot (.), anchors (^,$), closure (*) on single atoms.
 * Zero-heap runtime via fixed arenas and state sets.
 */

#ifndef NFA_H_
#define NFA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward decls to avoid leaking editor internals */
struct line;

typedef struct {
    int start_state;
    int state_count;
    bool case_sensitive;
} nfa_program_info;

/* Compile pattern into internal fixed-size arena.
 * Returns true on success; false if pattern unsupported or exceeds capacity.
 */
bool nfa_compile(const char* pattern, bool case_sensitive, nfa_program_info* out_info);

/* Search forward from (start_lp,start_off) to end of buffer.
 * On match: sets *match_lp and *match_off to beginning or end depending on beg_or_end (PTBEG/PTEND semantics).
 * Returns true if found.
 */
bool nfa_search_forward(const nfa_program_info* prog,
                        struct line* start_lp,
                        int start_off,
                        int beg_or_end,
                        struct line** match_lp,
                        int* match_off);

#endif /* NFA_H_ */


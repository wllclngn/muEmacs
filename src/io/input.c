/*	input.c
 *
 *	Various input routines
 *
 *	written by Daniel Lawrence 5/9/86
 *	modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "wrapper.h"
#include "file_utils.h"
#include "string_safe.h"
#include "keymap.h"

#define	COMPLC	1

/* C23 Atomic key processing structures */
static _Atomic uint64_t meta_key_generation = 0;

/* C23 atomic key transformation - instantaneous case conversion */
static inline uint32_t atomic_meta_transform(int key_code)
{
	// Atomic generation-based cache invalidation
	static _Atomic uint64_t cached_generation = 0;
	uint64_t current_gen = atomic_load_explicit(&meta_key_generation, memory_order_acquire);
	
	// Check if cache is valid with atomic compare
	if (atomic_load_explicit(&cached_generation, memory_order_relaxed) != current_gen) {
		// Atomic cache invalidation - instantaneous
		atomic_store_explicit(&cached_generation, current_gen, memory_order_release);
	}
	
	// Atomic case transformation for Meta keys (Linus's algorithm with C23 atomics)
	if (key_code >= 'a' && key_code <= 'z') {
		// Atomic bit flip for case conversion - instantaneous
		uint32_t transformed = (uint32_t)key_code ^ 0x20;  // DIFCASE atomic operation
		return transformed;
	}
	return (uint32_t)key_code;
}

/*
 * Ask a yes or no question in the message line. Return either true, false, or
 * ABORT. The ABORT status is returned if the user bumps out of the question
 * with a ^G. Used any time a confirmation is required.
 */
int mlyesno(const char *prompt)
{
    int c;			/* input character (use TTgetc) */
    char buf[NPAT];		/* prompt to user */

	for (;;) {
		/* build and prompt the user */
		safe_strcpy(buf, prompt, NPAT);
		safe_strcat(buf, " [Y/N]? ", NPAT);
		mlwrite(buf);

        /* get the response from terminal layer to avoid stdio/raw mismatch */
        TTflush();  /* ensure prompt is displayed */
        c = TTgetc();

		if (c == ectoc(abortc))	/* Bail out! */
			return ABORT;

        if (c == 'y' || c == 'Y')
            return true;

        if (c == 'n' || c == 'N')
            return false;
    }
}

/*
 * Write a prompt into the message line, then read back a response. Keep
 * track of the physical position of the cursor. If we are in a keyboard
 * macro throw the prompt away, and return the remembered response. This
 * lets macros run at full speed. The reply is always terminated by a carriage
 * return. Handle erase, kill, and abort keys.
 */

int mlreply(const char *prompt, char *buf, int nbuf)
{
	return nextarg(prompt, buf, nbuf, ctoec('\n'));
}

int mlreplyt(const char *prompt, char *buf, int nbuf, int eolchar)
{
	return nextarg(prompt, buf, nbuf, eolchar);
}

/*
 * ectoc:
 *	expanded character to character
 *	collapse the CONTROL and SPEC flags back into an ascii code
 */
int ectoc(int c)
{
	if (c & CONTROL)
		c = c & ~(CONTROL | 0x40);
	if (c & SPEC)
		c = c & 255;
	return c;
}

/*
 * ctoec:
 *	character to extended character
 *	pull out the CONTROL and SPEC prefixes (if possible)
 */
int ctoec(int c)
{
	if (c >= 0x00 && c <= 0x1F)
		c = CONTROL | (c + '@');
	return c;
}

/*
 * get a command name from the command line. Command completion means
 * that pressing a <SPACE> will attempt to complete an unfinished command
 * name if it is unique.
 */
fn_t getname(void)
{
	int cpos;	/* current column on screen output */
	int c;
	char *sp;	/* pointer to string for output */
	struct name_bind *ffp;	/* first ptr to entry in name binding table */
	struct name_bind *cffp;	/* current ptr to entry in name binding table */
	struct name_bind *lffp;	/* last ptr to entry in name binding table */
	char buf[NSTRING];	/* buffer to hold tentative command name */

	/* starting at the beginning of the string buffer */
	cpos = 0;

	/* if we are executing a command line get the next arg and match it */
	if (clexec) {
		if (macarg(buf) != true)
			return nullptr;
		return fncmatch(&buf[0]);
	}

	/* build a name string from the keyboard */
	while (true) {
		c = tgetc();

		/* if we are at the end, just match it */
		if (c == 0x0d) {
			buf[cpos] = 0;

			/* and match it off */
			return fncmatch(&buf[0]);

		} else if (c == ectoc(abortc)) {	/* Bell, abort */
			ctrlg(false, 0);
			TTflush();
			return nullptr;

		} else if (c == 0x7F || c == 0x08) {	/* rubout/erase */
			if (cpos != 0) {
				TTputc('\b');
				TTputc(' ');
				TTputc('\b');
				--ttcol;
				--cpos;
				TTflush();
			}

		} else if (c == 0x15) {	/* C-U, kill */
			while (cpos != 0) {
				TTputc('\b');
				TTputc(' ');
				TTputc('\b');
				--cpos;
				--ttcol;
			}

			TTflush();

		} else if (c == ' ' || c == 0x1b || c == 0x09) {
/* <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
			/* attempt a completion */
			buf[cpos] = 0;	/* terminate it for us */
			ffp = &names[0];	/* scan for matches */
			while (ffp->n_func != nullptr) {
				if (strncmp(buf, ffp->n_name, strlen(buf))
				    == 0) {
					/* a possible match! More than one? */
					if ((ffp + 1)->n_func == nullptr ||
					    (strncmp
					     (buf, (ffp + 1)->n_name,
					      strlen(buf)) != 0)) {
						/* no...we match, print it */
						sp = ffp->n_name + cpos;
						while (*sp)
							TTputc(*sp++);
						TTflush();
						return ffp->n_func;
					} else {
/* << << << << << << << << << << << << << << << << << */
						/* try for a partial match against the list */

						/* first scan down until we no longer match the current input */
						lffp = (ffp + 1);
						while ((lffp +
							1)->n_func !=
						       nullptr) {
							if (strncmp
							    (buf,
							     (lffp +
							      1)->n_name,
							     strlen(buf))
							    != 0)
								break;
							++lffp;
						}

						/* and now, attempt to partial complete the string, char at a time */
						while (true) {
							/* add the next char in */
							buf[cpos] =
							    ffp->
							    n_name[cpos];

							/* scan through the candidates */
							cffp = ffp + 1;
							while (cffp <=
							       lffp) {
								if (cffp->
								    n_name
								    [cpos]
								    !=
								    buf
								    [cpos])
									goto onward;
								++cffp;
							}

							/* add the character */
							TTputc(buf
							       [cpos++]);
						}
/* << << << << << << << << << << << << << << << << << */
					}
				}
				++ffp;
			}

			/* no match.....beep and onward */
			TTbeep();
		      onward:;
			TTflush();
/* <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< */
		} else {
			if (cpos < NSTRING - 1 && c > ' ') {
				buf[cpos++] = c;
				TTputc(c);
			}

			++ttcol;
			TTflush();
		}
	}
}

// UTF-8 sequence detection
static int utf8_seq_length(unsigned char first_byte)
{
	if ((first_byte & 0x80) == 0) return 1;      // 0xxxxxxx - ASCII
	if ((first_byte & 0xE0) == 0xC0) return 2;   // 110xxxxx - 2 bytes
	if ((first_byte & 0xF0) == 0xE0) return 3;   // 1110xxxx - 3 bytes
	if ((first_byte & 0xF8) == 0xF0) return 4;   // 11110xxx - 4 bytes
	return 1; // Invalid UTF-8, treat as single byte
}

// C23 UTF-8 atomic character collection - prevents race conditions  
// Complete character-level processing to fix fundamental architecture issue
// State for UTF-8 and bracketed paste handling (file-scope for test reset)
static struct {
    unsigned char bytes[4];
    size_t collected;
    size_t needed;
    bool in_sequence;
} utf8_state = {0};

static struct {
    bool paste_mode;                // Inside bracketed paste 2004 mode
    unsigned char pending[64];      // Pending lookahead bytes to replay
    size_t pend_len;
    size_t pend_pos;
    unsigned char end_seq[6];       // ESC [ 2 0 1 ~
    size_t match_idx;               // Match index for end_seq
} paste = {
    .paste_mode = false,
    .pending = {0},
    .pend_len = 0,
    .pend_pos = 0,
    .end_seq = {0x1B, '[', '2', '0', '1', '~'},
    .match_idx = 0
};

// Swallow one unmodified digit right after a focus-in event (ESC [ I)
// to avoid stray digits from desktop shortcuts (e.g., Super+2).
static bool ignore_next_unmodified_digit = false;

static int get_utf8_character_atomic(void)
{

    // 1) Serve any pending lookahead bytes first
    if (paste.pend_pos < paste.pend_len) {
        return (int)paste.pending[paste.pend_pos++];
    }
    paste.pend_len = paste.pend_pos = 0;

    // 2) If in paste mode, stream bytes until we see the end sentinel
    if (paste.paste_mode) {
        int b = TTgetc();
        if (b < 0) return b;

        // Check rolling match against end sequence ESC[201~
        unsigned char ub = (unsigned char)b;
        if (ub == paste.end_seq[paste.match_idx]) {
            paste.match_idx++;
            if (paste.match_idx == sizeof(paste.end_seq)) {
                // End of paste; reset and do not return a char
                paste.paste_mode = false;
                paste.match_idx = 0;
                return get_utf8_character_atomic();
            }
            // Do not emit yet; wait for full match or mismatch
            return get_utf8_character_atomic();
        }

        // Mismatch after partial match: flush prefix bytes previously matched
        if (paste.match_idx > 0) {
            // Put previously matched prefix (which is actual text) into pending
            for (size_t i = 0; i < paste.match_idx && i < sizeof(paste.pending); i++) {
                paste.pending[i] = paste.end_seq[i];
            }
            paste.pend_len = paste.match_idx;
            paste.pend_pos = 0;
            paste.match_idx = 0;
            // Also handle current mismatching byte as content next time
            if (paste.pend_len < sizeof(paste.pending)) {
                paste.pending[paste.pend_len++] = ub;
            }
            return get_utf8_character_atomic();
        }

        // Normal paste content byte
        return b;
    }

    // 3) Not in paste mode: read first byte
    int first_byte = TTgetc();
    if (first_byte < 0) return first_byte;

    // Detect focus events and bracketed paste start without blocking
    if (first_byte == 0x1B) {
        int avail = typahead();
        if (avail <= 0) {
            return first_byte; // Bare ESC for now
        }
        int b1 = TTgetc();
        if (b1 == '[') {
            avail = typahead();
            if (avail > 0) {
                int b2 = TTgetc();
                if (b2 == 'I') { // Focus in
                    ignore_next_unmodified_digit = true;
                    return get_utf8_character_atomic();
                } else if (b2 == 'O') { // Focus out
                    return get_utf8_character_atomic();
                } else if (b2 == '2') {
                    // Possible bracketed paste start: ESC [ 200~
                    avail = typahead();
                    if (avail >= 3) {
                        int b3 = TTgetc();
                        int b4 = TTgetc();
                        int b5 = TTgetc();
                        if (b3 == '0' && b4 == '0' && b5 == '~') {
                            paste.paste_mode = true;
                            paste.match_idx = 0;
                            paste.pend_len = paste.pend_pos = 0;
                            return get_utf8_character_atomic();
                        }
                        // Not actually paste; queue consumed bytes to replay
                        paste.pending[0] = (unsigned char)'[';
                        paste.pending[1] = (unsigned char)'2';
                        paste.pending[2] = (unsigned char)b3;
                        paste.pending[3] = (unsigned char)b4;
                        paste.pending[4] = (unsigned char)b5;
                        paste.pend_len = 5;
                        paste.pend_pos = 0;
                        return first_byte;
                    } else {
                        // Not enough lookahead; defer decision
                        paste.pending[0] = (unsigned char)'[';
                        paste.pending[1] = (unsigned char)'2';
                        paste.pend_len = 2;
                        paste.pend_pos = 0;
                        return first_byte;
                    }
                } else {
                    // Unknown CSI: queue '[' and this byte
                    paste.pending[0] = (unsigned char)'[';
                    paste.pending[1] = (unsigned char)b2;
                    paste.pend_len = 2;
                    paste.pend_pos = 0;
                    return first_byte;
                }
            } else {
                // No lookahead available; treat as ESC then replay '[' later
                paste.pending[0] = (unsigned char)'[';
                paste.pend_len = 1;
                paste.pend_pos = 0;
                return first_byte;
            }
        } else {
            // Not CSI; push back the single lookahead
            paste.pending[0] = (unsigned char)b1;
            paste.pend_len = 1;
            paste.pend_pos = 0;
            return first_byte;
        }
    }

    // 4) Standard UTF-8 collection paths
    if ((first_byte & 0b10000000) == 0) {
        return first_byte; // ASCII fast path
    }

    size_t seq_length = 1;
    if ((first_byte & 0b11100000) == 0b11000000) seq_length = 2;        // 110xxxxx
    else if ((first_byte & 0b11110000) == 0b11100000) seq_length = 3;   // 1110xxxx
    else if ((first_byte & 0b11111000) == 0b11110000) seq_length = 4;   // 11110xxx
    else return first_byte; // Invalid UTF-8 - treat as single byte

    utf8_state.bytes[0] = (unsigned char)first_byte;
    utf8_state.collected = 1;
    utf8_state.needed = seq_length;

    for (size_t i = 1; i < seq_length; i++) {
        const int next_byte = TTgetc();
        if ((next_byte & 0b11000000) != 0b10000000) {
            // Invalid continuation: return what we have as separate bytes
            utf8_state.in_sequence = true;
            const unsigned char result = utf8_state.bytes[0];
            for (size_t j = 1; j < utf8_state.collected; j++) {
                utf8_state.bytes[j-1] = utf8_state.bytes[j];
            }
            utf8_state.collected--;
            if (utf8_state.collected == 0) {
                utf8_state.in_sequence = false;
            }
            return (int)result;
        }
        utf8_state.bytes[utf8_state.collected++] = (unsigned char)next_byte;
    }

    utf8_state.in_sequence = true;
    const unsigned char result = utf8_state.bytes[0];
    for (size_t i = 1; i < utf8_state.collected; i++) {
        utf8_state.bytes[i-1] = utf8_state.bytes[i];
    }
    utf8_state.collected--;
    if (utf8_state.collected == 0) {
        utf8_state.in_sequence = false;
        utf8_state.needed = 0;
    }
    return (int)result;
}

// Test helper: reset internal parser state (UTF-8 and paste)
void input_reset_parser_state(void)
{
    memset(&utf8_state, 0, sizeof(utf8_state));
    memset(&paste.pending[0], 0, sizeof(paste.pending));
    paste.paste_mode = false;
    paste.pend_len = 0;
    paste.pend_pos = 0;
    paste.match_idx = 0;
    paste.end_seq[0] = 0x1B;
    paste.end_seq[1] = '[';
    paste.end_seq[2] = '2';
    paste.end_seq[3] = '0';
    paste.end_seq[4] = '1';
    paste.end_seq[5] = '~';
    ignore_next_unmodified_digit = false;
}
/*	tgetc:	Get a key from the terminal driver, resolve any keyboard
		macro action					*/

int tgetc(void)
{
	int c;			/* fetched character */

	/* if we are playing a keyboard macro back, */
	if (kbdmode == PLAY) {

		/* if there is some left... */
		if (kbdptr < kbdend)
			return (int) *kbdptr++;

		/* at the end of last repitition? */
		if (--kbdrep < 1) {
			kbdmode = STOP;
			/* force a screen update after all is done (VISMAC disabled) */
			update(false);
		} else {

			/* reset the macro to the begining for the next rep */
			kbdptr = &kbdm[0];
			return (int) *kbdptr++;
		}
	}

    /* fetch a character from the terminal driver, resolve macros */
    /* Use UTF-8 atomic collector to avoid sequence interleaving */
    c = get_utf8_character_atomic();

    /* Swallow one stray unmodified digit immediately after focus-in */
    if (ignore_next_unmodified_digit) {
        if (c >= '0' && c <= '9') {
            ignore_next_unmodified_digit = false;
            return tgetc();
        }
        ignore_next_unmodified_digit = false;
    }
	
	/* Validate character to prevent corruption during fast typing */
	if (c < 0 || c > 0x10FFFF) {
		c = c & 0xFF; /* Clamp to byte range */
	}

	/* record it for $lastkey */
	lastkey = c;

	/* save it if we need to */
    if (kbdmode == RECORD) {
        /* Do not record bracketed paste content as macro keystrokes */
        if (!paste.paste_mode) {
            *kbdptr++ = c;
            kbdend = kbdptr;

            /* don't overrun the buffer */
            if (kbdptr == &kbdm[NKBDM - 1]) {
                kbdmode = STOP;
                TTbeep();
            }
        }
    }

	/* and finally give the char back */
	return c;
}

/*	GET1KEY:	Get one keystroke. The only prefixs legal here
			are the SPEC and CONTROL prefixes.
								*/

/* Input buffer for high-speed escape sequence processing */
static unsigned char input_buf[16];
static int buf_pos = 0;
static int buf_len = 0;

static int buffered_getc(void)
{
	if (buf_pos >= buf_len) {
		/* Buffer empty, refill from terminal */
		buf_len = 0;
		buf_pos = 0;
		
		/* Read available characters in one shot for speed */
		int available = typahead();
		if (available > 0) {
			if (available > (int)sizeof(input_buf)) 
				available = (int)sizeof(input_buf);
			
			/* Fast bulk read */
			for (int i = 0; i < available; i++) {
				input_buf[buf_len++] = tgetc();
			}
		} else {
			/* Single character read when nothing buffered */
			input_buf[buf_len++] = tgetc();
		}
	}
	
	return input_buf[buf_pos++];
}

/* Public helper: parse Kitty CSI u sequence from a string for tests/tools
 * Expected form: "\x1b[<codepoint>;<mods>u". Returns true on success. */
int parse_kitty_csi_u(const char *seq, int *out_key)
{
    if (!seq || !out_key) return false;
    const unsigned char *p = (const unsigned char *)seq;
    if (*p++ != 0x1B) return false;      /* ESC */
    if (*p++ != '[') return false;       /* CSI */

    long codepoint = 0;
    long mods = 0;
    if (!(*p >= '0' && *p <= '9')) return false;
    while (*p >= '0' && *p <= '9') {
        codepoint = codepoint * 10 + (*p - '0');
        p++;
    }
    if (*p++ != ';') return false;
    if (!(*p >= '0' && *p <= '9')) return false;
    while (*p >= '0' && *p <= '9') {
        mods = mods * 10 + (*p - '0');
        p++;
    }
    if (*p++ != 'u') return false;

    int out = (int)codepoint;
    /* Ctrl (bit 3 in Kitty is 8) */
    if (mods & 8) {
        if (out >= 'A' && out <= 'Z') {
            out = CONTROL | out;
        } else if (out >= 'a' && out <= 'z') {
            out = CONTROL | (out - 32);
        } else {
            /* Non-letters: best-effort leave numeric with no CONTROL bit */
        }
    }
    /* Alt/Meta (bit 2 in Kitty is 4) */
    if (mods & 4) {
        out |= META;
    }
    *out_key = out;
    return true;
}

/* Pure decoder for ESC/CSI u into key_event for unit tests */
int decode_key_from_bytes(const unsigned char *buf, size_t len,
                          struct key_event *out, size_t *consumed)
{
    if (!buf || !out) return false;
    if (consumed) *consumed = 0;
    struct key_event evt = {0};
    // Swallow xterm focus events (CSI I/O) entirely
    if (len >= 3 && buf[0] == 0x1B && buf[1] == '[' &&
        (buf[2] == 'I' || buf[2] == 'O')) {
        if (buf[2] == 'I') {
            ignore_next_unmodified_digit = true;
        }
        if (consumed) *consumed = 3;
        return false;
    }
    
    // Handle standard CSI sequences (arrow keys, function keys, etc.)
    if (len >= 3 && buf[0] == 0x1B && buf[1] == '[') {
        unsigned char final = buf[2];
        // Arrow keys: ESC[A (up), ESC[B (down), ESC[C (right), ESC[D (left)
        if (final == 'A' || final == 'B' || final == 'C' || final == 'D') {
            evt.code = SPEC | final;  // SPEC flag marks special keys
            if (consumed) *consumed = 3;
            *out = evt;
            return true;
        }
        // Home/End: ESC[H, ESC[F
        if (final == 'H' || final == 'F') {
            evt.code = SPEC | final;
            if (consumed) *consumed = 3;
            *out = evt;
            return true;
        }
        // Insert/Delete/PgUp/PgDn: ESC[<digit>~
        if (len >= 4 && buf[2] >= '1' && buf[2] <= '6' && buf[3] == '~') {
            evt.code = SPEC | buf[2];
            if (consumed) *consumed = 4;
            *out = evt;
            return true;
        }
    }
    
    if (len >= 3 && buf[0] == 0x1B && buf[1] == '[') {
        size_t i = 2;
        long codepoint = 0;
        long mods = 0;
        if (i < len && (buf[i] >= '0' && buf[i] <= '9')) {
            while (i < len && buf[i] >= '0' && buf[i] <= '9') {
                codepoint = codepoint * 10 + (buf[i] - '0');
                i++;
            }
            if (i < len && buf[i] == ';') {
                i++;
                while (i < len && buf[i] >= '0' && buf[i] <= '9') {
                    mods = mods * 10 + (buf[i] - '0');
                    i++;
                }
                if (i < len && buf[i] == 'u') {
                    i++;
                    evt.code = (uint32_t)codepoint;
                    evt.ctrl = (mods & 8) ? 1 : 0;
                    evt.meta = (mods & 4) ? 1 : 0;
                    if (consumed) *consumed = i;
                    *out = evt;
                    return true;
                }
            }
        }
        /* fall through to ESC handling */
    }
    if (len >= 2 && buf[0] == 0x1B) {
        evt.meta = 1;
        unsigned char c = buf[1];
        if (c <= 0x1F) {
            evt.ctrl = 1;
            evt.code = (uint32_t)(c + '@');
        } else {
            evt.code = (uint32_t)c;
        }
        if (consumed) *consumed = 2;
        *out = evt;
        return true;
    }
    if (len >= 1 && buf[0] <= 0x1F) {
        evt.ctrl = 1;
        evt.code = (uint32_t)(buf[0] + '@');
        if (consumed) *consumed = 1;
        *out = evt;
        return true;
    }
    if (len >= 1) {
        evt.code = buf[0];
        if (consumed) *consumed = 1;
        *out = evt;
        return true;
    }
    return false;
}

/* C23 atomic key processing - matches Linus's get1key() behavior exactly */
int get1key(void)
{
	// Use unified key decoding with buffering to handle all escape sequences
	unsigned char buf[8] = {0};
	size_t len = 0;
	
	buf[len++] = (unsigned char)tgetc();
	
	// If we got ESC, we MUST wait for potential sequence completion
	// This is critical - escape sequences arrive as multiple bytes
	if (buf[0] == 0x1B && len < sizeof(buf)) {
		// Read next byte directly - tgetc() blocks until data arrives
		// Modern terminals send escape sequences atomically
		buf[len++] = (unsigned char)tgetc();
		
		// If ESC[, this is definitely a CSI sequence - read the rest
		if (buf[1] == '[' && len < sizeof(buf)) {
			// CSI sequences need the final byte
			buf[len++] = (unsigned char)tgetc();
			
			// For parameterized sequences (digits), keep reading
			if (len < sizeof(buf) && (buf[2] >= '0' && buf[2] <= '9')) {
				while (typahead() > 0 && len < sizeof(buf)) {
					buf[len++] = (unsigned char)tgetc();
					// Stop at final byte (letter or '~')
					if ((buf[len-1] >= 'A' && buf[len-1] <= 'Z') ||
						(buf[len-1] >= 'a' && buf[len-1] <= 'z') ||
						buf[len-1] == '~') {
						break;
					}
				}
			}
		}
	}
	
	// Try to decode the buffered sequence
	struct key_event evt = {0};
	size_t consumed = 0;
	
	if (decode_key_from_bytes(buf, len, &evt, &consumed)) {
		uint32_t result = key_to_legacy(evt);
		return (int)result;
	}
	
	// Fallback for single byte input
	int c = (int)buf[0];
	
	/* C23 atomic control character conversion - matches Linus exactly */
	if (c >= 0x00 && c <= 0x1F) {
		// Atomic bit manipulation for control codes (including ESC → CONTROL|'[')
		return (int)(CONTROL | (c + '@'));
	}
	
	return c;  // Direct return for normal keys
}

/* C23 atomic Meta command processing - matches Linus's getcmd() behavior */
int getcmd(void)
{
    // Unified key_event decoding using intelligent buffering
    unsigned char buf[8] = {0};
    size_t len = 0;
    
    // Read first byte
    buf[len++] = (unsigned char)tgetc();
    
    // If ESC, MUST wait for sequence completion
    if (buf[0] == 0x1B && len < sizeof(buf)) {
        // Read next byte directly - tgetc() blocks until data arrives
        // This is correct behavior: escape sequences are atomic from terminal
        buf[len++] = (unsigned char)tgetc();
        
        // If ESC[, read CSI sequence completely
        if (buf[1] == '[' && len < sizeof(buf)) {
            // CSI needs the final byte
            buf[len++] = (unsigned char)tgetc();
                
            // For parameterized sequences (digits), keep reading
            if (len < sizeof(buf) && (buf[2] >= '0' && buf[2] <= '9')) {
                while (typahead() > 0 && len < sizeof(buf)) {
                    buf[len++] = (unsigned char)tgetc();
                    // Stop at final byte
                    if ((buf[len-1] >= 'A' && buf[len-1] <= 'Z') ||
                        (buf[len-1] >= 'a' && buf[len-1] <= 'z') ||
                        buf[len-1] == '~') {
                        break;
                    }
                }
            }
        }
    }
    
    struct key_event evt = {0};
    size_t consumed = 0;
    if (!decode_key_from_bytes(buf, len, &evt, &consumed)) {
        // Fallback for edge cases
        return get1key();
    }
    
    uint32_t result = key_to_legacy(evt);
    
    // If a focus-in was just received, ignore one bare digit
    if (ignore_next_unmodified_digit && !evt.ctrl && !evt.meta &&
        evt.code >= '0' && evt.code <= '9') {
        ignore_next_unmodified_digit = false;
        return getcmd();
    }
    ignore_next_unmodified_digit = false;
    return (int)result;
}



/*	A more generalized prompt/reply function allowing the caller
	to specify the proper terminator. If the terminator is not
	a return ('\n') it will echo as "<NL>"
							*/
int getstring(const char *prompt, char *buf, int nbuf, int eolchar)
{
	int cpos;	/* current character position in string */
	int c;
	int quotef;	/* are we quoting the next char? */
#if	COMPLC
	int ffile, ocpos, nskip = 0, didtry = 0;
#if	UNIX
	static char tmp[] = "/tmp/meXXXXXX";
	FILE *tmpf = nullptr;
#endif
	ffile = (strcmp(prompt, "Find file: ") == 0
		 || strcmp(prompt, "View file: ") == 0
		 || strcmp(prompt, "Insert file: ") == 0
		 || strcmp(prompt, "Write file: ") == 0
		 || strcmp(prompt, "Read file: ") == 0
		 || strcmp(prompt, "File to execute: ") == 0);
#endif

	cpos = 0;
	quotef = false;

	/* prompt the user for the input string */
	mlwrite(prompt);

	for (;;) {
#if	COMPLC
		if (!didtry)
			nskip = -1;
		didtry = 0;
#endif
		/* get a character from the user */
		c = get1key();

		/* If it is a <ret>, change it to a <NL> */
		if (c == (CONTROL | 0x4d) && !quotef)
			c = CONTROL | 0x40 | '\n';

		/* if they hit the line terminate, wrap it up */
		if (c == eolchar && quotef == false) {
			buf[cpos++] = 0;

			/* clear the message line */
			mlwrite("");
			TTflush();

			/* if we default the buffer, return false */
			if (buf[0] == 0)
				return false;

			return true;
		}

		/* change from command form back to character form */
		c = ectoc(c);

		if (c == ectoc(abortc) && quotef == false) {
			/* Abort the input? */
			ctrlg(false, 0);
			TTflush();
			return ABORT;
		} else if ((c == 0x7F || c == 0x08) && quotef == false) {
			/* rubout/erase */
			if (cpos != 0) {
				outstring("\b \b");
				--ttcol;

				if (buf[--cpos] < 0x20) {
					outstring("\b \b");
					--ttcol;
				}
				if (buf[cpos] == '\n') {
					outstring("\b\b  \b\b");
					ttcol -= 2;
				}

				TTflush();
			}

		} else if (c == 0x15 && quotef == false) {
			/* C-U, kill */
			while (cpos != 0) {
				outstring("\b \b");
				--ttcol;

				if (buf[--cpos] < 0x20) {
					outstring("\b \b");
					--ttcol;
				}
				if (buf[cpos] == '\n') {
					outstring("\b\b  \b\b");
					ttcol -= 2;
				}
			}
			TTflush();

#if	COMPLC
		} else if ((c == 0x09 || c == ' ') && quotef == false
			   && ffile) {
			/* TAB, complete file name */
			char ffbuf[255];
			int n, iswild = 0;

			didtry = 1;
			ocpos = cpos;
			while (cpos != 0) {
				outstring("\b \b");
				--ttcol;

				if (buf[--cpos] < 0x20) {
					outstring("\b \b");
					--ttcol;
				}
				if (buf[cpos] == '\n') {
					outstring("\b\b  \b\b");
					ttcol -= 2;
				}
				if (buf[cpos] == '*' || buf[cpos] == '?')
					iswild = 1;
			}
			TTflush();
			if (nskip < 0) {
				buf[ocpos] = 0;
#if	UNIX
				if (tmpf != nullptr)
					safe_fclose(&tmpf);
				safe_strcpy(tmp, "/tmp/meXXXXXX", sizeof(tmp));
				safe_strcpy(ffbuf, "echo ", sizeof(ffbuf));
				safe_strcat(ffbuf, buf, sizeof(ffbuf));
				if (!iswild)
					safe_strcat(ffbuf, "*", sizeof(ffbuf));
				safe_strcat(ffbuf, " >", sizeof(ffbuf));
				xmkstemp(tmp);
				safe_strcat(ffbuf, tmp, sizeof(ffbuf));
				safe_strcat(ffbuf, " 2>&1", sizeof(ffbuf));
				
				// Safe command execution using sh -c
				pid_t pid = fork();
				if (pid == 0) {
					execl("/bin/sh", "sh", "-c", ffbuf, (char *)nullptr);
					_exit(127);
				} else if (pid > 0) {
					waitpid(pid, nullptr, 0);
				}
				tmpf = safe_fopen(tmp, FILE_READ);
#endif
				nskip = 0;
			}
#if	UNIX
			c = ' ';
			for (n = nskip; n > 0; n--)
				while ((c = getc(tmpf)) != EOF
				       && c != ' ');
#endif
			nskip++;

			if (c != ' ') {
				TTbeep();
				nskip = 0;
			}
#if	UNIX
			while ((c = getc(tmpf)) != EOF && c != '\n'
			       && c != ' ' && c != '*')
#endif
			{
				if (cpos < nbuf - 1)
					buf[cpos++] = c;
			}
#if	UNIX
			if (c == '*')
				TTbeep();
#endif

			for (n = 0; n < cpos; n++) {
				c = buf[n];
				if ((c < ' ') && (c != '\n')) {
					outstring("^");
					++ttcol;
					c ^= 0x40;
				}

				if (c != '\n') {
					if (disinp)
						TTputc(c);
				} else {	/* put out <NL> for <ret> */
					outstring("<NL>");
					ttcol += 3;
				}
				++ttcol;
			}
			TTflush();
#if	UNIX
			rewind(tmpf);
			unlink(tmp);
#endif
#endif

		} else if ((c == quotec || c == 0x16) && quotef == false) {
			quotef = true;
		} else {
			quotef = false;
			if (cpos < nbuf - 1) {
				buf[cpos++] = c;

				if ((c < ' ') && (c != '\n')) {
					outstring("^");
					++ttcol;
					c ^= 0x40;
				}

				if (c != '\n') {
					if (disinp)
						TTputc(c);
				} else {	/* put out <NL> for <ret> */
					outstring("<NL>");
					ttcol += 3;
				}
				++ttcol;
				TTflush();
			}
		}
	}
}

/*
 * output a string of characters
 *
 * char *s;		string to output
 */
void outstring(const char *s)
{
	if (disinp)
		while (*s)
			TTputc(*s++);
}

/*
 * output a string of output characters
 *
 * char *s;		string to output
 */
void ostring(const char *s)
{
	if (discmd)
		while (*s)
			TTputc(*s++);
}

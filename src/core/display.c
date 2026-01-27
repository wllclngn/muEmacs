/*	display.c
 *
 *      The functions in this file handle redisplay. There are two halves, the
 *      ones that update the virtual display screen, and the ones that make the
 *      physical display screen the same as the virtual display screen. These
 *      functions use hints that are left in the windows by the commands.
 *
 *	Modified by Petri Kutvonen
 */

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#include <limits.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "profiler.h"
#include "line.h"
#include "version.h"
#include "wrapper.h"
#include "utf8.h"
#include "../util/display_width.h"
#include "string_utils.h"
#include "memory.h"
#include "error.h"
#include "display_ops.h"
#include "terminal_ops.h"
#include "util/logger.h"
#include "terminal/palette.h"
#include "terminal/sequences.h"
#include "display_internal.h"
#include "internal/syntax.h"
#include "μemacs/buffer_utils.h"

#include "../util/git_status.h"
#include "uep/extension_api.h"
#include "internal/event_bus.h"

/* C23 atomic snapshot helper for terminal dimensions
 * Captures all dimensions at once to prevent SIGWINCH corruption mid-loop */
typedef struct {
    int mrow;  /* max rows */
    int nrow;  /* usable rows (mrow - 1 for status) */
    int mcol;  /* max columns */
    int ncol;  /* usable columns */
} term_snapshot_t;

static inline term_snapshot_t term_snapshot(void) {
    term_snapshot_t snap;
    snap.mrow = atomic_load_explicit(&term.t_mrow, memory_order_acquire);
    snap.nrow = atomic_load_explicit(&term.t_nrow, memory_order_acquire);
    snap.mcol = atomic_load_explicit(&term.t_mcol, memory_order_acquire);
    snap.ncol = atomic_load_explicit(&term.t_ncol, memory_order_acquire);
    return snap;
}

/* Highlight state sync - defined in curses.c */
extern void highlight_force_reset(void);

/* Synchronized updates (DEC 2026) - defined in curses.c */
extern void sync_frame_start(void);
extern void sync_frame_end(void);

/*
 * Palette-based color helpers.
 * These emit the correct SGR sequence based on whether we're using
 * palette indices or truecolor hex overrides.
 */
static inline void emit_modeline_bg(void) {
    /* Always use truecolor - modern terminals all support it */
    ttputs(sgr_truecolor_bg(g_palette.modeline_rgb[0],
                             g_palette.modeline_rgb[1],
                             g_palette.modeline_rgb[2]));
}

static inline void emit_modeline_fg(void) {
    /* Always use truecolor for modeline foreground */
    ttputs(sgr_truecolor_fg(g_palette.modeline_fg_rgb[0],
                             g_palette.modeline_fg_rgb[1],
                             g_palette.modeline_fg_rgb[2]));
}

static inline void emit_ruler_bg(void) {
    /* Always use truecolor */
    ttputs(sgr_truecolor_bg(g_palette.ruler_rgb[0],
                             g_palette.ruler_rgb[1],
                             g_palette.ruler_rgb[2]));
}

/* Lightweight SGR attribute helpers (inherit terminal theme) */
static inline void sgr_underline_on(void)  { vtputs("\x1b[4m"); }
static inline void sgr_underline_off(void) { vtputs("\x1b[24m"); }
static inline void sgr_bold_on(void)       { vtputs("\x1b[1m"); }
static inline void sgr_dim_on(void)        { vtputs("\x1b[2m"); }
static inline void sgr_bold_dim_off(void)  { vtputs("\x1b[22m"); }
static inline void sgr_reset(void)         { vtputs("\x1b[0m"); }

static inline void style_on(int style)
{
    switch (style) {
    case 1: sgr_underline_on(); break;
    case 2: sgr_bold_on(); break;
    case 3: sgr_dim_on(); break;
    /* NO reverse video (case 7) - we use truecolor backgrounds instead */
    default: break;
    }
}
static inline void style_off(int style)
{
    switch (style) {
    case 1: sgr_underline_off(); break;
    case 2:
    case 3: sgr_bold_dim_off(); break;
    /* NO reverse video (case 7) - we use truecolor backgrounds instead */
    default: break;
    }
}

/* struct video and VFCHG/etc flags are in display_internal.h */

/* Virtual and physical screens */
struct video **vscreen;		/* Virtual screen (exported for overlay) */
static struct video **pscreen;		/* Physical screen (internal) */

static int displaying = true;

/* Track cursor row range for cursor-line highlighting
 * Under rapid input, cursor may move multiple rows between updates.
 * Track min/max to ensure all intermediate rows get highlight cleared.
 * C23 atomic for consistency with currow (also atomic).
 */
static _Atomic int prev_cursor_row = -1;
static _Atomic int cursor_row_min = -1;
static _Atomic int cursor_row_max = -1;

/* Fast checksum calculation for change detection optimization */
static uint32_t video_checksum(unicode_t *text, int len)
{
	uint32_t hash = 2166136261U; // FNV-1a hash
	for (int i = 0; i < len; i++) {
		hash ^= text[i];
		hash *= 16777619U;
	}
	return hash;
}

/*
 * Display dirty tracking (montauk/OUROBOROS pattern)
 *
 * Track buffer modifications via sequence counter to avoid
 * redundant update() calls when nothing has changed.
 */
static _Atomic uint64_t last_rendered_seq = 0;

void mark_buffer_dirty(struct buffer *bp)
{
	if (bp) {
		atomic_fetch_add(&bp->b_dirty_seq, 1);
	}
}

bool display_needs_update(void)
{
	/* Check if current buffer's dirty_seq differs from last rendered */
	if (!curbp) return true;  /* Always update if no buffer */

	/* Check dirty sequence (content changed) */
	uint64_t current_seq = atomic_load(&curbp->b_dirty_seq);
	if (current_seq != atomic_load(&last_rendered_seq))
		return true;

	/* Check screen garbage flag (full redraw) */
	if (sgarbf)
		return true;

	/* Check if any window has pending updates (cursor moves, mode changes, etc.) */
	struct window *wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_flag)
			return true;
		wp = wp->w_wndp;
	}

	return false;
}

void mark_display_clean(void)
{
	if (curbp) {
		atomic_store(&last_rendered_seq, atomic_load(&curbp->b_dirty_seq));
	}
}

/* Update video line checksum atomically */
static void video_update_checksum(struct video *vp)
{
	uint32_t checksum = video_checksum(vp->v_text, term.t_ncol);
	atomic_store(&vp->v_checksum, checksum);
}

/* Fast comparison using checksums - collision rate is negligible */
static bool video_lines_differ(struct video *vp1, struct video *vp2)
{
	return atomic_load(&vp1->v_checksum) != atomic_load(&vp2->v_checksum);
}
#ifdef SIGWINCH
#include <sys/ioctl.h>
/* for window size changes */
volatile sig_atomic_t chg_width, chg_height;
#endif

static int reframe(struct window *wp);
static void updone(struct window *wp);
static void updall(struct window *wp);
static void updext(void);
static int updateline(int row, struct video *vp1, struct video *vp2);
static int calculate_wordwrap_line_rows(struct line *lp, int wrap_col, int tab_width);
static int calculate_wordwrap_cursor_pos(struct line *lp, int byte_offset, int wrap_col,
                                          int tab_width, int *out_col);
/* Note: modern_modeline() and helpers moved to modeline.c */
static int newscreensize(int h, int w);


/*
 * Initialize the data structures used by the display code. The edge vectors
 * used to access the screens are set up. The operating system's terminal I/O
 * channel is set up. All the other things get initialized at compile time.
 * The original window has "WFCHG" set, so that it will get completely
 * redrawn on the first call to "update".
 */
/*
 * Test-friendly screen allocation without terminal I/O.
 * Called by vtinit_test() for headless testing.
 */
static void allocate_screens(void)
{
	int i;
	struct video *vp;

	if (vscreen != nullptr) {
		LOG_DEBUG("Display: allocate_screens() - already allocated");
		return;
	}

	/* Sanity check terminal dimensions */
	if (term.t_mrow <= 0 || term.t_mcol <= 0) {
		LOG_ERRORF("Display: Invalid terminal size %dx%d, using defaults",
		           term.t_mrow, term.t_mcol);
		if (term.t_mrow <= 0) {
			term.t_mrow = 24;
			term.t_nrow = 23;  /* t_nrow = t_mrow - 1 (status line) */
		}
		if (term.t_mcol <= 0) {
			term.t_mcol = 80;
			term.t_ncol = 80;
		}
	}

	vscreen = (struct video**)safe_alloc(term.t_mrow * sizeof(struct video *), "vscreen", __FILE__, __LINE__);
	if (!vscreen) {
		LOG_ERROR("Display: Failed to allocate virtual screen");
		return;
	}

	pscreen = (struct video**)safe_alloc(term.t_mrow * sizeof(struct video *), "pscreen", __FILE__, __LINE__);
	if (!pscreen) {
		LOG_ERROR("Display: Failed to allocate physical screen");
		return;
	}
	for (i = 0; i < term.t_mrow; ++i) {
		vp = (struct video*)safe_alloc(sizeof(struct video) + term.t_mcol*4, "video row", __FILE__, __LINE__);
		if (!vp) {
			LOG_ERRORF("Display: Failed to allocate video row %d", i);
			return;
		}
		vp->v_flag = 0;
		atomic_store(&vp->v_checksum, 0);
		vscreen[i] = vp;
		vp = (struct video*)safe_alloc(sizeof(struct video) + term.t_mcol*4, "physical video row", __FILE__, __LINE__);
		vp->v_flag = 0;
		/* Initialize pscreen v_text with impossible values to force first render.
		 * Without this, zeroed pscreen may match zeroed vscreen rows,
		 * causing those rows to be skipped and appear blank. */
		for (int j = 0; j < term.t_mcol; j++) {
			vp->v_text[j] = 0xFFFFFFFF;  /* Impossible unicode value */
		}
		atomic_store(&vp->v_checksum, 0xDEADBEEF);  /* Force checksum mismatch */
		pscreen[i] = vp;
	}
	LOG_DEBUGF("Display: allocate_screens() allocated %d rows x %d cols", term.t_mrow, term.t_mcol);
}

/*
 * Test-only initialization: allocate screens without terminal I/O.
 * Use this in headless test environments where TTopen() would fail.
 */
void vtinit_test(void)
{
	allocate_screens();
}

void vtinit(void)
{
    TTopen();		/* open the screen */
    TTkopen();		/* open the keyboard */
    TTrev(false);
    /* Initialize terminal optimizations/capabilities (truecolor, paste) after TTopen */
    display_init_optimization();
    /* Allocate screen buffers */
    allocate_screens();
}


/*
 * Clean up the virtual terminal system, in anticipation for a return to the
 * operating system. Move down to the last line and clear it out (the next
 * system prompt will be written in the line). Shut down the channel to the
 * terminal.
 */
void vttidy(void)
{
	/*
	 * Restore the primary screen buffer and other terminal modes first,
	 * so any final clears/cursor moves happen on the real shell screen.
	 */
	display_cleanup_optimization();
	TTflush();

	/* Close terminal I/O – do not write or move the cursor after
	 * restoring the primary screen to avoid altering shell history. */
	TTclose();
	TTkclose();

}

/*
 * Set the virtual cursor to the specified row and column on the virtual
 * screen. There is no checking for nonsense values; this might be a good
 * idea during the early stages.
 */
void vtmove(int row, int col)
{
	vtrow = row;
	vtcol = col;
}

/*
 * Write a character to the virtual screen. The virtual row and
 * column are updated. If we are not yet on left edge, don't print
 * it yet. If the line is too long put a "$" in the last column.
 *
 * This routine only puts printing characters into the virtual
 * terminal buffers. Only column overflow is checked.
 *
 * Non-static: shared with modeline.c
 */
void vtputc(int c)
{
	struct video *vp;	/* ptr to line being updated */


	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_text[term.t_ncol - 1] = '$';
		return;
	}

	if (c == '\t') {
		do {
			vtputc(' ');
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	if (c < 0x20) {
		vtputc('^');
		vtputc(c ^ 0x40);
		return;
	}

	if (c == 0x7f) {
		vtputc('^');
		vtputc('?');
		return;
	}

	/* UTF-8: Let high bytes (0x80+) pass through as-is.
	 * The terminal handles multi-byte UTF-8 rendering.
	 * Previously this escaped 0x80-0xA0 as hex, which broke UTF-8. */

	if (vtcol >= 0) {
		vp->v_text[vtcol] = c;
		// Mark line as changed - checksum will be updated later
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

/* Put character with highlighting (truecolor background) */
#define HIGHLIGHT_BIT 0x80000000U  /* High bit indicates cursor line/ruler highlight */
#define SELECTION_BIT 0x40000000U  /* Selection highlight (different from cursor line) */

/* Syntax highlighting face encoding (4 bits = 16 faces) */
#define SYNTAX_FACE_MASK  0x0F000000U  /* Bits 24-27: syntax face ID */
#define SYNTAX_FACE_SHIFT 24
#define SYNTAX_ENCODE_FACE(c, face) ((c) | (((unicode_t)(face) & 0x0F) << SYNTAX_FACE_SHIFT))
#define SYNTAX_DECODE_FACE(c) (((c) & SYNTAX_FACE_MASK) >> SYNTAX_FACE_SHIFT)
#define SYNTAX_STRIP_BITS(c) ((c) & ~(HIGHLIGHT_BIT | SELECTION_BIT | SYNTAX_FACE_MASK))

/*
 * vtputc_syntax:
 * 	put a character with syntax face encoding into virtual screen
 *
 * c:    character to display
 * face: syntax face ID (FACE_DEFAULT, FACE_KEYWORD, etc.)
 */
static void vtputc_syntax(int c, int face)
{
	struct video *vp;

	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_text[term.t_ncol - 1] = '$';
		return;
	}

	if (c == '\t') {
		do {
			vtputc_syntax(' ', face);
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	if (c < 0x20) {
		vtputc_syntax('^', face);
		vtputc_syntax(c ^ 0x40, face);
		return;
	}

	if (c == 0x7f) {
		vtputc_syntax('^', face);
		vtputc_syntax('?', face);
		return;
	}

	if (vtcol >= 0) {
		/* Encode face into the character */
		vp->v_text[vtcol] = SYNTAX_ENCODE_FACE(c, face);
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

static void vtputc_highlighted(int c)
{
	struct video *vp;

	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	/* CRITICAL: Log every highlighted character placement */
	LOG_DEBUGF("Display: vtputc_highlighted(0x%X '%c') at row=%d col=%d",
	           c, (c >= 0x20 && c < 0x7F) ? c : '.', vtrow, vtcol);

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_flag |= VFEXT;
		return;
	}

	/* Handle tabs */
	if (c == '\t') {
		do {
			vtputc_highlighted(' ');
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	/* Handle control characters */
	if (c < 0x20) {
		vtputc_highlighted('^');
		vtputc_highlighted(c ^ 0x40);
		return;
	}

	if (c == 0x7f) {
		vtputc_highlighted('^');
		vtputc_highlighted('?');
		return;
	}

	/* UTF-8: Let high bytes (0x80+) pass through as-is. */

	/* Store character with highlight bit set */
	if (vtcol >= 0) {
		vp->v_text[vtcol] = c | HIGHLIGHT_BIT;
		// Mark line as changed - checksum will be updated later
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

/* Put character with selection highlighting (uses SELECTION_BIT) */
static void vtputc_selected(int c)
{
	struct video *vp;

	/* In case somebody passes us a signed char.. */
	if (c < 0) {
		c += 256;
		if (c < 0)
			return;
	}

	vp = vscreen[vtrow];

	if (vtcol >= term.t_ncol) {
		++vtcol;
		vp->v_flag |= VFEXT;
		return;
	}

	/* Handle tabs */
	if (c == '\t') {
		do {
			vtputc_selected(' ');
		} while (((vtcol + taboff) & tabmask) != 0);
		return;
	}

	/* Handle control characters */
	if (c < 0x20) {
		vtputc_selected('^');
		vtputc_selected(c ^ 0x40);
		return;
	}

	if (c == 0x7f) {
		vtputc_selected('^');
		vtputc_selected('?');
		return;
	}

	/* UTF-8: Let high bytes (0x80+) pass through as-is. */

	/* Store character with selection bit set */
	if (vtcol >= 0) {
		vp->v_text[vtcol] = c | SELECTION_BIT;
		vp->v_flag |= VFCHG;
	}
	++vtcol;
}

/*
 * Erase from the end of the software cursor to the end of the line on which
 * the software cursor is located.
 */
static void vteeol(void)
{
	unicode_t *vcp = vscreen[vtrow]->v_text;

	while (vtcol < term.t_ncol)
		vcp[vtcol++] = ' ';
}

/*
 * upscreen:
 *	user routine to force a screen update
 *	always finishes complete update
 */
int upscreen(int f, int n)
{
	update(true);
	return true;
}

/*
 * Make sure that the display is right. This is a three part process. First,
 * scan through all of the windows looking for dirty ones. Check the framing,
 * and refresh the screen. Second, make sure that "currow" and "curcol" are
 * correct for the current window. Third, make the virtual and physical
 * screens the same.
 *
 * int force;		force update past type ahead?
 */
int update(int force)
{
	// --- PROFILING HOOK ---
	perf_start_timing("update");

	struct window *wp;
	// Defer updates during edit transactions unless explicitly forced
	if (!force && atomic_load(&edit_transaction_depth) > 0) {
		LOG_DEBUG("Display: update() deferred (in transaction)");
		perf_end_timing("update");
		return true;
	}

	/* Coalesce updates under heavy input load (montauk pattern)
	 * Skip update if there's significant typeahead - we'll catch up
	 * when input slows down. This prevents terminal output from
	 * overwhelming the display under rapid keystrokes.
	 *
	 * IMPORTANT: Even when deferring, we MUST track cursor movement
	 * so that when we finally render, we know ALL rows that need
	 * highlight cleanup (not just prev_cursor_row and currow).
	 *
	 * NOTE: Threshold raised from 5 to 30 to fix "display lag during typing" bug.
	 * Modern terminals handle output well; aggressive skipping hurts UX.
	 */
	extern int input_pending(void);
	if (!force && input_pending() > 30) {
		/* Calculate current cursor row even when deferring.
		 * This ensures highlight cleanup covers all visited rows.
		 * (currow may be stale from previous update cycle) */
		if (highlight_current_line && curwp != nullptr) {
			int current_row = curwp->w_toprow;
			struct line *lp = curwp->w_linep;
			while (lp != nullptr && lp != curwp->w_dotp) {
				++current_row;
				lp = lforw(lp);
			}
			/* Track cursor range for highlight cleanup */
			int rmin = atomic_load_explicit(&cursor_row_min, memory_order_acquire);
			int rmax = atomic_load_explicit(&cursor_row_max, memory_order_acquire);
			if (rmin < 0 || rmax < 0) {
				rmin = current_row;
				rmax = current_row;
			} else {
				if (current_row < rmin) rmin = current_row;
				if (current_row > rmax) rmax = current_row;
			}
			atomic_store_explicit(&cursor_row_min, rmin, memory_order_release);
			atomic_store_explicit(&cursor_row_max, rmax, memory_order_release);
		}
		LOG_DEBUGF("Display: update() deferred (input_pending=%d > 30)", input_pending());
		perf_end_timing("update");
		return true;
	}

	LOG_DEBUGF("Display: update(force=%d) starting", force);

	/* DEBUG: Dump all windows in linked list */
	{
		struct window *dbg_wp = wheadp;
		int win_count = 0;
		while (dbg_wp != nullptr) {
			LOG_DEBUGF("Display: WINDOW[%d] toprow=%d ntrows=%d buf=%s dotp=%p",
			           win_count, dbg_wp->w_toprow, dbg_wp->w_ntrows,
			           dbg_wp->w_bufp ? dbg_wp->w_bufp->b_bname : "(null)",
			           (void*)dbg_wp->w_dotp);
			win_count++;
			dbg_wp = dbg_wp->w_wndp;
		}
		LOG_DEBUGF("Display: Total windows: %d", win_count);
	}

	/*
	 * Signal masking removed (montauk pattern):
	 * - Passive resize detection via ioctl(TIOCGWINSZ) in ttgetc()
	 * - No SIGWINCH handler needed
	 * - Eliminates ~176,000 syscalls/second when idle
	 */

#if	VISMAC == 0
	if (force == false && kbdmode == MACRO_PLAY) {
		perf_end_timing("update");
		return true;
	}
#endif

	displaying = true;

	/* Hide cursor during render to prevent flicker (montauk pattern) */
	extern void terminal_set_cursor_visible(bool visible);
	terminal_set_cursor_visible(false);

	/* Force-sync highlight state at frame start to prevent bleed under rapid input */
	highlight_force_reset();

	/* Propagate mode line changes to all instances of a buffer - O(N²) but safe */
	/* When one window showing a buffer needs mode update, all windows showing
	 * that same buffer need mode update too (e.g., buffer name change) */
	wp = wheadp;
	while (wp != nullptr) {
		if ((wp->w_flag & WFMODE) && wp->w_bufp->b_nwnd > 1) {
			/* This buffer is shown in multiple windows - propagate WFMODE */
			struct window *wp2 = wheadp;
			while (wp2 != nullptr) {
				if (wp2->w_bufp == wp->w_bufp) {
					wp2->w_flag |= WFMODE;
				}
				wp2 = wp2->w_wndp;
			}
		}
		wp = wp->w_wndp;
	}

	/* update any windows that need refreshing */
	bool had_hard_refresh = false;  /* Track if any window had WFHARD */
	wp = wheadp;
	while (wp != nullptr) {
		if (wp->w_flag) {
			LOG_DEBUGF("Display: Window needs update, flags=0x%x, buf=%s",
			           wp->w_flag, wp->w_bufp ? wp->w_bufp->b_bname : "(null)");
			/* Track WFHARD for forcing updupd later */
			if (wp->w_flag & WFHARD)
				had_hard_refresh = true;
			/* if the window has changed, service it */
			reframe(wp);	/* check the framing */
			if ((wp->w_flag & ~WFMODE) == WFEDIT) {
				LOG_DEBUG("Display: updone() - single line edit");
				updone(wp);	/* update EDITed line */
			} else if (wp->w_flag & ~WFMOVE) {
				LOG_DEBUG("Display: updall() - full window refresh");
				updall(wp);	/* update all lines */
			}
			if (wp->w_flag & WFMODE) {
				LOG_DEBUG("Display: modern_modeline()");
				modern_modeline(wp);
			}
			wp->w_flag &= WFTERM;  /* Preserve terminal marker, clear dirty flags */
			wp->w_force = 0;
		}
		/* on to the next window */
		wp = wp->w_wndp;
	}

	/* recalc the current hardware cursor location */
	updpos();

	/* Cursor-line highlighting: detect row changes and mark for update.
	 * Uses currow (set by updpos above) - no separate calculation needed.
	 * This MUST run after updpos() to use the correct cursor position.
	 * NOTE: Do NOT update prev_cursor_row here - updupd() needs the old value
	 * to know which row was previously highlighted and needs clearing. */
	int cur_row = atomic_load_explicit(&currow, memory_order_acquire);
	int prev_row = atomic_load_explicit(&prev_cursor_row, memory_order_acquire);
	term_snapshot_t tsnap = term_snapshot();
	if (highlight_current_line && cur_row >= 0) {
		if (prev_row >= 0 && prev_row != cur_row) {
			/* Cursor row changed - mark both old and new rows for update */
			if (prev_row < tsnap.mrow && vscreen && vscreen[prev_row]) {
				vscreen[prev_row]->v_flag |= VFCHG;
				LOG_DEBUGF("HILINE: old row %d needs update", prev_row);
			}
			if (cur_row < tsnap.mrow && vscreen && vscreen[cur_row]) {
				vscreen[cur_row]->v_flag |= VFCHG;
				LOG_DEBUGF("HILINE: new row %d needs update", cur_row);
			}
		}
		/* prev_cursor_row is updated in updupd() AFTER rendering */
	}

	/* check for lines to de-extend */
	upddex();

	/* Begin synchronized update - terminal batches output until sync_end
	 * Prevents flicker on modern terminals (kitty, foot, iTerm2, etc.)
	 * Falls back gracefully on terminals without DEC 2026 support */
	sync_frame_start();

	/* if screen is garbage, re-plot it */
	if (atomic_load_explicit(&sgarbf, memory_order_acquire) != false)
		updgar();

	/* update the virtual screen to the physical screen
	 * Force update when WFHARD was set to bypass checksum optimization.
	 * This prevents stale terminal content from persisting after buffer switch. */
	updupd(force || had_hard_refresh);

	/* update the cursor and flush the buffers - use atomic loads */
	int final_currow = atomic_load_explicit(&currow, memory_order_acquire);
	int final_curcol = atomic_load_explicit(&curcol, memory_order_acquire);
	movecursor(final_currow, final_curcol - lbound);

	/* Ensure cursor is visible after display update */
	extern void terminal_set_cursor_visible(bool visible);
	terminal_set_cursor_visible(true);

	/* End synchronized update - terminal can now display changes */
	sync_frame_end();

	/* CRITICAL: Flush ALL output including cursor show command */
	TTflush();

	displaying = false;
#if SIGWINCH
	while (chg_width || chg_height)
		newscreensize(chg_height, chg_width);
#endif

	LOG_DEBUGF("Display: update() complete, cursor at row=%d col=%d", currow, curcol);

	perf_end_timing("update");
	return true;
}

/*
 * reframe:
 *	check to see if the cursor is on in the window
 *	and re-frame it if needed or wanted
 */
static int reframe(struct window *wp)
{
	struct line *lp, *lp0;
	int i = 0;

	/* if not a requested reframe, check for a needed one */
	if ((wp->w_flag & WFFORCE) == 0) {
		/* loop from one line above the window to one line after */
		lp = wp->w_linep;
		lp0 = lback(lp);
		if (lp0 == wp->w_bufp->b_linep)
			i = 0;
		else {
			i = -1;
			lp = lp0;
		}
		for (; i <= (int) (wp->w_ntrows); i++)
		{
			/* if the line is in the window, no reframe */
			if (lp == wp->w_dotp) {
				/* if not _quite_ in, we'll reframe gently */
				if (i < 0 || i == wp->w_ntrows) {
					/* if the terminal can't help, then
					   we're simply outside */
					if (term.t_scroll == nullptr)
						i = wp->w_force;
					break;
				}
				return true;
			}

			/* if we are at the end of the file, reframe */
			if (lp == wp->w_bufp->b_linep)
				break;

			/* on to the next line */
			lp = lforw(lp);
		}
	}
	if (i == -1) {		/* we're just above the window */
		i = scrollcount;	/* put dot at first line */
	} else if (i == wp->w_ntrows) {	/* we're just below the window */
		i = -scrollcount;	/* put dot at last line */
	} else			/* put dot where requested */
		i = wp->w_force;	/* (is 0, unless reposition() was called) */

	wp->w_flag |= WFMODE;

	/* how far back to reframe? */
	if (i > 0) {		/* only one screen worth of lines max */
		if (--i >= wp->w_ntrows)
			i = wp->w_ntrows - 1;
	} else if (i < 0) {	/* negative update???? */
		i += wp->w_ntrows;
		if (i < 0)
			i = 0;
	} else
		i = wp->w_ntrows / 2;

	/* backup to new line at top of window */
	lp = wp->w_dotp;

	/* If cursor is at sentinel, start from last real line instead */
	if (lp == wp->w_bufp->b_linep) {
		lp = lback(lp);
		if (lp == wp->w_bufp->b_linep) {
			/* Empty buffer - sentinel is only option */
			wp->w_linep = lp;
			wp->w_flag |= WFHARD;
			wp->w_flag &= ~WFFORCE;
			return true;
		}
	}

	while (i != 0 && lback(lp) != wp->w_bufp->b_linep) {
		--i;
		lp = lback(lp);
	}

	/* Safety: ensure w_linep is never the sentinel (except empty buffer) */
	if (lp == wp->w_bufp->b_linep && lback(lp) != wp->w_bufp->b_linep) {
		lp = lback(lp);
		LOG_DEBUG("Display: reframe() backed off sentinel to last real line");
	}

	/* and reset the current line at top of window */
	wp->w_linep = lp;
	wp->w_flag |= WFHARD;
	wp->w_flag &= ~WFFORCE;

	/* Note: WFHARD is sufficient for window update. Removed sgarbf=true
	 * which was causing excessive full-screen redraws and high CPU usage. */

	return true;
}

/* Check if a character position is within the marked region */
/* Get selection bounds for a line - O(n) buffer scan done ONCE per line, not per char */
static bool get_line_selection_bounds(struct line *lp, int *sel_start, int *sel_end)
{
	struct window *wp = curwp;
	struct line *markp = wp->w_markp;
	int marko = wp->w_marko;
	struct line *dotp = wp->w_dotp;
	int doto = wp->w_doto;

	*sel_start = -1;
	*sel_end = -1;

	/* No mark set - no selection */
	if (markp == nullptr)
		return false;

	/* Check current visual mode */
	enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
	bool linewise = (mode == MODE_VISUAL_LINE);
	bool blockwise = (mode == MODE_VISUAL_BLOCK);

	/* For Visual Line mode: entire lines from col 0 to EOL */
	if (linewise) {
		/* Same line case */
		if (markp == dotp) {
			if (lp != markp) return false;
			*sel_start = 0;
			*sel_end = llength(lp);
			return true;
		}

		/* Multi-line: determine which lines are in selection */
		struct line *start_line, *end_line;
		bool mark_before_cursor = false;

		struct line *scan = wp->w_bufp->b_linep;
		while ((scan = lforw(scan)) != wp->w_bufp->b_linep) {
			if (scan == markp) { mark_before_cursor = true; break; }
			else if (scan == dotp) { mark_before_cursor = false; break; }
		}

		if (mark_before_cursor) {
			start_line = markp;
			end_line = dotp;
		} else {
			start_line = dotp;
			end_line = markp;
		}

		/* Check if lp is the start or end line */
		if (lp == start_line || lp == end_line) {
			*sel_start = 0;
			*sel_end = llength(lp);
			return true;
		}

		/* Check if lp is between start and end */
		struct line *between_scan = start_line;
		while ((between_scan = lforw(between_scan)) != wp->w_bufp->b_linep &&
		       between_scan != end_line) {
			if (between_scan == lp) {
				*sel_start = 0;
				*sel_end = llength(lp);
				return true;
			}
		}
		return false;
	}

	/* For Visual Block mode: fixed column range on each line in range */
	if (blockwise) {
		/* Same line case */
		if (markp == dotp) {
			if (lp != markp) return false;
		} else {
			/* Multi-line: check if lp is in the line range */
			struct line *start_line, *end_line;
			bool mark_before_cursor = false;

			struct line *scan = wp->w_bufp->b_linep;
			while ((scan = lforw(scan)) != wp->w_bufp->b_linep) {
				if (scan == markp) { mark_before_cursor = true; break; }
				else if (scan == dotp) { mark_before_cursor = false; break; }
			}

			if (mark_before_cursor) {
				start_line = markp;
				end_line = dotp;
			} else {
				start_line = dotp;
				end_line = markp;
			}

			/* Check if lp is in range */
			bool in_range = false;
			if (lp == start_line || lp == end_line) {
				in_range = true;
			} else {
				struct line *between_scan = start_line;
				while ((between_scan = lforw(between_scan)) != wp->w_bufp->b_linep &&
				       between_scan != end_line) {
					if (between_scan == lp) {
						in_range = true;
						break;
					}
				}
			}
			if (!in_range) return false;
		}

		/* Line is in range - use block column bounds */
		int left_col = g_vim_state.block_start_col;
		int right_col = getccol(false);  /* Current cursor virtual column */
		if (left_col > right_col) {
			int tmp = left_col;
			left_col = right_col;
			right_col = tmp;
		}
		/* Clamp to line length */
		int len = llength(lp);
		*sel_start = (left_col < len) ? left_col : len;
		*sel_end = (right_col < len) ? right_col + 1 : len;
		return true;
	}

	/* Character-wise visual mode (MODE_VISUAL) or non-visual with mark set */

	/* Same position - no selection */
	if (markp == dotp && marko == doto)
		return false;

	/* For single-line selections, handle directly */
	if (markp == dotp) {
		if (lp != markp) return false;
		*sel_start = (marko < doto) ? marko : doto;
		*sel_end = (marko < doto) ? doto : marko;
		return true;
	}

	/* Multi-line selection - determine line order (O(n) but only once per line) */
	struct line *start_line, *end_line;
	int start_pos, end_pos;
	bool mark_before_cursor = false;

	struct line *scan = wp->w_bufp->b_linep;
	while ((scan = lforw(scan)) != wp->w_bufp->b_linep) {
		if (scan == markp) { mark_before_cursor = true; break; }
		else if (scan == dotp) { mark_before_cursor = false; break; }
	}

	if (mark_before_cursor) {
		start_line = markp; start_pos = marko;
		end_line = dotp; end_pos = doto;
	} else {
		start_line = dotp; start_pos = doto;
		end_line = markp; end_pos = marko;
	}

	/* Determine selection bounds for this line */
	if (lp == start_line) {
		*sel_start = start_pos;
		*sel_end = llength(lp);  /* To end of line */
		return true;
	} else if (lp == end_line) {
		*sel_start = 0;
		*sel_end = end_pos;
		return true;
	} else {
		/* Check if line is between start and end */
		struct line *between_scan = start_line;
		while ((between_scan = lforw(between_scan)) != wp->w_bufp->b_linep &&
		       between_scan != end_line) {
			if (between_scan == lp) {
				*sel_start = 0;
				*sel_end = llength(lp);  /* Entire line selected */
				return true;
			}
		}
		return false;
	}
}

/* Legacy per-char function - now uses cached bounds when possible */
static int in_region(struct line *lp, int pos)
{
	int sel_start, sel_end;
	if (!get_line_selection_bounds(lp, &sel_start, &sel_end))
		return false;
	return (pos >= sel_start && pos < sel_end);
}

/* Show a line with optional soft-wrap. Returns number of screen rows consumed.
 * wp parameter is the window being rendered (NOT necessarily curwp!)
 * line_num is 0-indexed line number for syntax lookup (-1 = unknown) */
static int show_line_wrapped(struct window *wp, struct line *lp, int max_rows, int line_num)
{
	/* CRITICAL: Null check before dereferencing lp */
	if (!lp) {
		LOG_ERROR("Display: show_line_wrapped() FATAL - lp is NULL!");
		return 1;
	}
	/* Note: lp->storage can be NULL for view mode lines (piece table backed) */

	int i = 0, len = llength(lp);
	int in_selection = false;
	int rows_used = 1;
	int char_col = 0;  /* Character column for syntax lookup */

	/* Get syntax state for this buffer (may be NULL if no highlighting) */
	buffer_syntax_t *syn = NULL;
	if (wp && wp->w_bufp && wp->w_bufp->b_syntax && line_num >= 0) {
		syn = wp->w_bufp->b_syntax;
	}
	/* Debug: log syntax state for first 3 lines */
	if (vtrow < 3) {
		LOG_DEBUGF("SYNTAX_LOOKUP: vtrow=%d line_num=%d syn=%p", vtrow, line_num, (void*)syn);
	}

	/* Get wrap column from the window being rendered (NOT curwp!)
	 * CRITICAL FIX: When soft-wrap is disabled (w_wrap_col == 0), use INT_MAX
	 * to completely disable wrapping. Using term.t_ncol as fallback was WRONG
	 * because it still triggered wrapping at terminal edge, corrupting vtrow. */
	int wrap_col;
	bool soft_wrap_enabled = (wp && wp->w_wrap_col > 0);
	if (soft_wrap_enabled) {
		wrap_col = wp->w_wrap_col;
	} else {
		wrap_col = INT_MAX;  /* Disable wrapping entirely - truncate at terminal edge */
	}

	/* COMPREHENSIVE DEBUG: Always log for first 10 rows to trace display issues */
	LOG_DEBUGF("DISPLAY: show_line_wrapped row=%d vtrow=%d vtcol=%d len=%d wrap_col=%d soft_wrap=%s t_ncol=%d t_nrow=%d",
	           max_rows, vtrow, vtcol, len, wrap_col,
	           soft_wrap_enabled ? "ON" : "OFF", term.t_ncol, term.t_nrow);

	/* Only apply selection highlighting to content lines in the current window */
	int apply_highlighting = (wp != nullptr && wp->w_markp != nullptr &&
	                         lp != nullptr && lp != wp->w_bufp->b_linep);

	/* Pre-compute selection bounds once per line (not per char!) */
	int sel_start = -1, sel_end = -1;
	if (apply_highlighting) {
		get_line_selection_bounds(lp, &sel_start, &sel_end);
		LOG_DEBUGF("Display: Selection bounds: sel_start=%d sel_end=%d",
		           sel_start, sel_end);
	}

	/* Get the line text into a buffer - handles both regular and view mode lines */
	char line_text[4096];
	int actual_len = (len < (int)sizeof(line_text) - 1) ? len : (int)sizeof(line_text) - 1;
	if (actual_len > 0) {
		lget_text(lp, 0, (size_t)actual_len, line_text, sizeof(line_text));
#if UEMACS_DEBUG_LOG
		/* COMPREHENSIVE DEBUG: Log buffer content being read */
		if (vtrow < 5) {  // First 5 rows only
			char preview[61];
			int plen = (len < 60) ? len : 60;
			for (int pi = 0; pi < plen; pi++) {
				unsigned char ch = (unsigned char)line_text[pi];
				preview[pi] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.';
			}
			preview[plen] = '\0';
			LOG_DEBUGF("BUFFER_READ: row=%d len=%d text=[%s]", vtrow, len, preview);
			/* Hex dump first 10 bytes to debug UTF-8 issues */
			if (vtrow == 0 && len >= 3) {
				LOG_DEBUGF("HEXDUMP: lp=%p storage=%p bytes[0..9] = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
				           (void*)lp, (void*)lp->storage,
				           (unsigned char)line_text[0], (unsigned char)line_text[1],
				           (unsigned char)line_text[2], len > 3 ? (unsigned char)line_text[3] : 0,
				           len > 4 ? (unsigned char)line_text[4] : 0, len > 5 ? (unsigned char)line_text[5] : 0,
				           len > 6 ? (unsigned char)line_text[6] : 0, len > 7 ? (unsigned char)line_text[7] : 0,
				           len > 8 ? (unsigned char)line_text[8] : 0, len > 9 ? (unsigned char)line_text[9] : 0);
			}
		}
#endif
	}

	/* Word-wrap state: track last space for intelligent line breaking */
	int last_space_i = -1;        /* Buffer position after last space */
	int last_space_vtcol = -1;    /* Display column after last space */

	while (i < actual_len) {
		/* Stop if we've used all available rows */
		if (rows_used > max_rows || vtrow >= term.t_nrow) {
			break;
		}

		unicode_t c;
		int bytes = utf8_to_unicode(line_text, i, actual_len, &c);

		/* Debug: Log UTF-8 decode for first 5 chars of row 0 */
		if (vtrow == 0 && i < 15) {
			LOG_DEBUGF("UTF8_DECODE: i=%d bytes=%d codepoint=0x%04X raw=[%02X %02X %02X]",
			           i, bytes, c,
			           (unsigned char)line_text[i],
			           i+1 < actual_len ? (unsigned char)line_text[i+1] : 0,
			           i+2 < actual_len ? (unsigned char)line_text[i+2] : 0);
		}

		/* Check for soft-wrap BEFORE rendering this character */
		if (vtcol >= wrap_col && rows_used < max_rows) {
			/* Word wrap: if we have a space to break at, use it */
			if (last_space_vtcol > 0 && c != ' ' && c != '\t') {
				/* Backtrack: clear from last space to current position */
				int clear_from = last_space_vtcol;
				for (int col = clear_from; col < vtcol && col < term.t_ncol; col++) {
					vscreen[vtrow]->v_text[col] = ' ';
				}
				vtcol = clear_from;
				i = last_space_i;  /* Restart rendering from after the space */
			}
			/* If current char is space, just skip it (don't start new line with space) */
			if (c == ' ' || c == '\t') {
				i += bytes;
				/* Reset wrap tracking for new line */
				last_space_i = -1;
				last_space_vtcol = -1;
				/* Fall through to do the wrap */
			}

			LOG_DEBUGF("SOFTWRAP: WRAP AT vtcol=%d, wrap_col=%d, moving to row %d",
			           vtcol, wrap_col, vtrow + 1);
			vteeol();  /* Fill rest of current line */
			vtrow++;
			vtcol = 0;
			rows_used++;
			/* Mark continuation row */
			if (vtrow < term.t_nrow) {
				vscreen[vtrow]->v_flag |= VFCHG;
				vscreen[vtrow]->v_linep = lp;  /* Same buffer line */
			}
			/* Reset wrap tracking for new line */
			last_space_i = -1;
			last_space_vtcol = -1;
			continue;  /* Re-check with updated state */
		}

		/* Track spaces for word-wrap */
		if (c == ' ' || c == '\t') {
			last_space_i = i + bytes;  /* Position AFTER the space */
			last_space_vtcol = vtcol + 1;  /* Column AFTER the space */
		}

		/* Use pre-computed bounds instead of O(n) in_region() call */
		int char_in_selection = (sel_start >= 0 && i >= sel_start && i < sel_end);

		/* Apply highlighting by modifying character representation */
		if (char_in_selection != in_selection) {
			in_selection = char_in_selection;
		}

		// Filter control characters that corrupt terminal display
		if (c == '\r') {
			// Skip carriage returns - they show as ^M and corrupt display
			i += bytes;
			continue;
		}

		/* Look up syntax face for this character column */
		int face = FACE_DEFAULT;
		if (syn && line_num >= 0) {
			face = syntax_get_face(syn, line_num, char_col);
			/* Debug: log face for first 10 chars of first 3 lines */
			if (vtrow < 3 && char_col < 10 && face != FACE_DEFAULT) {
				LOG_DEBUGF("FACE: row=%d col=%d face=%d char='%c'",
				           vtrow, char_col, face, c >= 32 && c < 127 ? c : '?');
			}
		}

		if (c < 32 && c != '\t') {
			// Display other control chars as printable to avoid corruption
			if (in_selection) {
				vtputc_selected('^');
				vtputc_selected('@' + c);
			} else {
				vtputc_syntax('^', face);
				vtputc_syntax('@' + c, face);
			}
		} else {
			if (in_selection) {
				vtputc_selected(c);
			} else {
				vtputc_syntax(c, face);
			}
		}
		i += bytes;
		char_col++;  /* Advance character column for syntax lookup */
	}

	/* Hot path - logging disabled for performance
	LOG_DEBUGF("Display: show_line_wrapped() consumed %d rows", rows_used); */
	return rows_used;
}

/* Legacy wrapper for non-wrapped callers - uses curwp
 * NOTE: Passes -1 for line_num, disabling syntax highlighting.
 * This is OK since show_line is only used for single-line updates
 * where we don't track line numbers. For full syntax, use updall(). */
static void show_line(struct line *lp)
{
	show_line_wrapped(curwp, lp, 1, -1);  /* Single row, no wrap, no syntax */
}

/*
 * updone:
 *	update the current line	to the virtual screen
 *
 * struct window *wp;		window to update current line in
 */
static void updone(struct window *wp)
{
	struct line *lp;	/* line to update */
	int sline;	/* physical screen line to update */

	/* CRITICAL FIX: When soft-wrap is enabled, a single buffer line may span
	 * multiple screen rows. updone() was designed for single-row updates and
	 * cannot correctly handle multi-row wrapped lines. Fall back to updall()
	 * which properly renders all continuation rows. */
	if (wp->w_wrap_col > 0) {
		int line_len = llength(wp->w_dotp);
		int display_width = calculate_display_column_cached(wp->w_dotp, line_len, 8);
		if (display_width >= wp->w_wrap_col) {
			LOG_DEBUGF("Display: updone() soft-wrap line exceeds wrap_col (%d >= %d), using updall()",
			           display_width, wp->w_wrap_col);
			updall(wp);
			return;
		}
	}

	/* search down the line we want, accounting for soft-wrap */
	lp = wp->w_linep;
	sline = wp->w_toprow;
	while (lp != wp->w_dotp) {
		/* With soft-wrap, each line may occupy multiple screen rows.
		 * CRITICAL: Use calculate_wordwrap_line_rows() to match updpos()
		 * calculation exactly. Simple division is WRONG because word-wrap
		 * breaks at spaces, not at exact column boundaries. */
		if (wp->w_wrap_col > 0 && lp != wp->w_bufp->b_linep) {
			sline += calculate_wordwrap_line_rows(lp, wp->w_wrap_col, 8);
		} else {
			++sline;
		}
		lp = lforw(lp);

		/* Safety check: if we exceed window bounds, something is wrong.
		 * Fall back to full update to recover. */
		if (sline >= wp->w_toprow + wp->w_ntrows || sline >= term.t_mrow) {
			LOG_ERRORF("Display: updone() failed to find dotp within window! sline=%d top=%d rows=%d",
			           sline, wp->w_toprow, wp->w_ntrows);
			wp->w_flag |= WFHARD; /* Force full hard refresh */
			updall(wp);
			return;
		}
	}

	/* With soft-wrap, cursor may be on a continuation row within the current line.
	 * CRITICAL: Use calculate_wordwrap_cursor_pos() to match updpos() exactly.
	 * Simple division is WRONG because word-wrap breaks at spaces. */
	if (wp->w_wrap_col > 0) {
		int dummy_col;
		int extra_rows = calculate_wordwrap_cursor_pos(wp->w_dotp, wp->w_doto,
		                                                wp->w_wrap_col, 8, &dummy_col);
		sline += extra_rows;
	}

	/* CRITICAL: Log cursor-line detection for highlight debugging */
	LOG_DEBUGF("Display: updone() sline=%d, v_linep=%p, w_dotp=%p, match=%d",
	           sline, (void*)vscreen[sline]->v_linep, (void*)wp->w_dotp,
	           vscreen[sline]->v_linep == wp->w_dotp);

	/* and update the virtual line */
	vscreen[sline]->v_flag |= VFCHG;
	vscreen[sline]->v_linep = lp;  /* Track which line is on this screen row (lp == wp->w_dotp here) */
	vscreen[sline]->v_flag &= ~VFREQ;
	vtmove(sline, 0);
	show_line(lp);
	vteeol();

	/* Apply cursor line highlighting if enabled (after vteeol to cover full width) */
	if (highlight_current_line && vscreen[sline]->v_linep == wp->w_dotp) {
		/* Mark entire line for highlighting */
		for (int i = 0; i < term.t_ncol; i++) {
			vscreen[sline]->v_text[i] |= HIGHLIGHT_BIT;
		}
		/* Verify ALL chars have HIGHLIGHT_BIT */
		int missing = 0;
		for (int i = 0; i < term.t_ncol; i++) {
			if (!(vscreen[sline]->v_text[i] & HIGHLIGHT_BIT)) missing++;
		}
		LOG_DEBUGF("Display: updone() APPLIED cursor-line highlight to row=%d, style=%d, missing=%d",
		           sline, hiline_style, missing);
	}

	/* Apply column ruler overlay if enabled and within screen */
	if (column_ruler_enabled) {
		int idx = column_ruler_column - 1;
		if (idx >= 0 && idx < term.t_ncol) {
			vscreen[sline]->v_text[idx] |= HIGHLIGHT_BIT;
		}
	}
}

/*
 * updall:
 *	update all the lines in a window on the virtual screen
 *
 * struct window *wp;		window to update lines in
 */
static void updall(struct window *wp)
{
	struct line *lp;	/* line to update */
	int sline;	/* physical screen line to update */
	int rows_remaining;
	int line_num;	/* 0-indexed line number for syntax highlighting */

	/* COMPREHENSIVE DEBUG: Full window state dump at start of refresh */
	LOG_DEBUGF("UPDALL: START window rows=%d-%d ntrows=%d buf=%s dotp=%p wrap_col=%d",
	           wp->w_toprow, wp->w_toprow + wp->w_ntrows, wp->w_ntrows,
	           wp->w_bufp ? wp->w_bufp->b_bname : "(null)",
	           (void*)wp->w_dotp, wp->w_wrap_col);

	/* search down the lines, updating them */
	lp = wp->w_linep;
	sline = wp->w_toprow;

	/* Compute starting line number for syntax highlighting.
	 * get_line_number() returns 1-indexed, we need 0-indexed. */
	line_num = get_line_number(wp->w_bufp, wp->w_linep);
	if (line_num > 0) {
		line_num--;  /* Convert to 0-indexed */
	} else {
		line_num = -1;  /* Unknown - disable syntax for this window */
	}

	/* CRITICAL: Validate pointers before loop to catch null dereferences */
	if (!vscreen) {
		LOG_ERROR("Display: updall() FATAL - vscreen is NULL!");
		return;
	}
	LOG_DEBUGF("UPDALL: entering loop linep=%p sline=%d t_mrow=%d t_nrow=%d",
	           (void*)lp, sline, term.t_mrow, term.t_nrow);

	while (sline < wp->w_toprow + wp->w_ntrows) {
		rows_remaining = (wp->w_toprow + wp->w_ntrows) - sline;

		/* Bounds check before vscreen access */
		if (sline < 0 || sline >= term.t_mrow) {
			LOG_ERRORF("Display: updall() BOUNDS ERROR sline=%d, t_mrow=%d", sline, term.t_mrow);
			break;
		}

		/* and update the virtual line */
		vscreen[sline]->v_flag |= VFCHG;
		vscreen[sline]->v_linep = lp;  /* Track which line is on this screen row */
		vscreen[sline]->v_flag &= ~VFREQ;
		vtmove(sline, 0);

		int rows_used = 1;
		bool has_content = (lp != wp->w_bufp->b_linep);  /* Track if row has actual content */
		struct line *current_lp = lp;  /* Save current line pointer BEFORE advancing */

		/* Hot path - logging disabled for performance
		LOG_DEBUGF("Display: updall() row=%d, lp=%p, b_linep=%p, has_content=%d",
		           sline, (void*)lp, (void*)(wp->w_bufp ? wp->w_bufp->b_linep : NULL), has_content); */

		if (has_content) {
			/* Check display event handlers for folding/narrowing/etc. */
			display_line_action_t line_action = DISPLAY_RENDER;
			const char *substitute_text = NULL;

			if (event_bus_has_handlers(EVT_DISPLAY_LINE)) {
				event_display_line_t evt = {
					.buffer = wp->w_bufp,
					.line = lp,
					.line_num = line_num,
					.screen_row = sline,
					.action = DISPLAY_RENDER,
					.substitute = NULL,
					.substitute_face = 0,
				};
				event_bus_emit(EVT_DISPLAY_LINE, &evt, sizeof(evt));
				line_action = evt.action;
				substitute_text = evt.substitute;
			}

			if (line_action == DISPLAY_SKIP) {
				/* Line is folded/hidden - skip it but advance through buffer */
				lp = lforw(lp);
				if (line_num >= 0) line_num++;
				rows_used = 0;  /* No screen rows consumed */
				continue;  /* Don't increment sline, try next buffer line */
			} else if (line_action == DISPLAY_SUBSTITUTE && substitute_text) {
				/* Show substitute text instead of actual line content */
				vtmove(sline, 0);
				vtputs(substitute_text);
				vteeol();
				vscreen[sline]->v_linep = lp;  /* Still track original line */
				lp = lforw(lp);
				if (line_num >= 0) line_num++;
				rows_used = 1;
			} else {
				/* Render actual buffer line - pass wp to get correct wrap_col */
				rows_used = show_line_wrapped(wp, lp, rows_remaining, line_num);
				LOG_DEBUGF("UPDALL: sline=%d rendered lp=%p rows_used=%d vtrow=%d vtcol=%d line_num=%d",
				           sline, (void*)lp, rows_used, vtrow, vtcol, line_num);
				lp = lforw(lp);
				if (line_num >= 0) {
					line_num++;  /* Advance to next line for syntax */
				}
			}
		} else {
			/* Beyond buffer content - show tilde and clear v_linep */
			vtmove(sline, 0);
			vtputc('~');
			vteeol();
			vscreen[sline]->v_linep = NULL;  /* No buffer line for this row */
		}

		/* Apply colors and fill remaining space for each row used */
		for (int r = 0; r < rows_used && (sline + r) < wp->w_toprow + wp->w_ntrows; r++) {
			int row = sline + r;

			/* CRITICAL: Set v_linep for ALL rows of a wrapped line.
			 * Without this, continuation rows have stale v_linep values
			 * which causes cursor highlight to appear on wrong lines. */
			if (has_content) {
				vscreen[row]->v_linep = current_lp;
			}

			/* vteeol for the last row of this line (only if we have content) */
			if (has_content && r == rows_used - 1) {
				vtmove(row, vtcol);  /* Restore position for vteeol */
				vteeol();
			}

			/* Apply cursor line highlighting ONLY if row has actual content
			 * and matches the cursor line. This prevents highlighting
			 * empty rows beyond the buffer content. */
			if (has_content && highlight_current_line && vscreen[row]->v_linep == wp->w_dotp) {
				for (int i = 0; i < term.t_ncol; i++) {
					vscreen[row]->v_text[i] |= HIGHLIGHT_BIT;
				}
				/* Hot path - logging disabled for performance
				LOG_DEBUGF("Display: updall() APPLIED cursor-line highlight to row=%d, v_linep=%p",
				           row, (void*)vscreen[row]->v_linep); */
			}

			/* Column ruler overlay - applies to ALL rows including tilde rows */
			if (column_ruler_enabled) {
				int idx = column_ruler_column - 1;
				if (idx >= 0 && idx < term.t_ncol) {
					vscreen[row]->v_text[idx] |= HIGHLIGHT_BIT;
				}
			}
		}

		LOG_DEBUGF("UPDALL: advancing sline %d -> %d (rows_used=%d)",
		           sline, sline + rows_used, rows_used);
		sline += rows_used;
	}
	LOG_DEBUGF("UPDALL: END final sline=%d (expected %d)", sline, wp->w_toprow + wp->w_ntrows);
}

/*
 * Calculate how many screen rows a line occupies with word-wrap.
 * This mirrors the wrap logic from show_line_wrapped().
 */
static int calculate_wordwrap_line_rows(struct line *lp, int wrap_col, int tab_width)
{
	if (!lp || wrap_col <= 0) return 1;

	int len = llength(lp);
	if (len == 0) return 1;

	/* Get line text - handles both regular and view mode lines */
	char line_text[4096];
	int actual_len = (len < (int)sizeof(line_text) - 1) ? len : (int)sizeof(line_text) - 1;
	lget_text(lp, 0, (size_t)actual_len, line_text, sizeof(line_text));

	int i = 0;
	int vtcol = 0;
	int rows = 1;
	int last_space_i = -1;
	int last_space_vtcol = -1;

	while (i < actual_len) {
		unicode_t c;
		int bytes = utf8_to_unicode(line_text, i, actual_len, &c);
		if (bytes <= 0) bytes = 1;

		/* Check for wrap BEFORE processing */
		if (vtcol >= wrap_col) {
			if (last_space_vtcol > 0 && c != ' ' && c != '\t') {
				vtcol = last_space_vtcol;
				i = last_space_i;
				bytes = utf8_to_unicode(line_text, i, actual_len, &c);
				if (bytes <= 0) bytes = 1;
			}
			if (c == ' ' || c == '\t') {
				i += bytes;
				last_space_i = -1;
				last_space_vtcol = -1;
			}
			vtcol = 0;
			rows++;
			last_space_i = -1;
			last_space_vtcol = -1;
			continue;
		}

		if (c == ' ' || c == '\t') {
			last_space_i = i + bytes;
			last_space_vtcol = vtcol + 1;
		}

		if (c == '\t') {
			vtcol = ((vtcol + tab_width) / tab_width) * tab_width;
		} else if (c < 32 || c == 127) {
			vtcol += 2;
		} else {
			vtcol++;
		}
		i += bytes;
	}

	return rows;
}

/*
 * Calculate cursor position within a line considering word-wrap.
 * This mirrors the word-wrap logic in show_line_wrapped() to ensure
 * cursor position matches what's displayed.
 *
 * Returns: extra_rows (number of wrap-induced row increments)
 * Sets: *out_col to the column within the final row
 */
static int calculate_wordwrap_cursor_pos(struct line *lp, int byte_offset, int wrap_col, int tab_width,
                                          int *out_col)
{
	if (!lp || byte_offset <= 0 || wrap_col <= 0) {
		*out_col = calculate_display_column_cached(lp, byte_offset, tab_width);
		return 0;
	}

	int len = llength(lp);
	if (len == 0) {
		*out_col = 0;
		return 0;
	}

	/* Get line text - handles both regular and view mode lines */
	char line_text[4096];
	int actual_len = (len < (int)sizeof(line_text) - 1) ? len : (int)sizeof(line_text) - 1;
	lget_text(lp, 0, (size_t)actual_len, line_text, sizeof(line_text));

	int i = 0;
	int vtcol = 0;
	int rows = 0;
	int last_space_i = -1;
	int last_space_vtcol = -1;

	while (i < actual_len && i < byte_offset) {
		unicode_t c;
		int bytes = utf8_to_unicode(line_text, i, actual_len, &c);
		if (bytes <= 0) bytes = 1;

		/* Check for wrap BEFORE processing this character */
		if (vtcol >= wrap_col) {
			/* Word wrap: backtrack to last space if available */
			if (last_space_vtcol > 0 && c != ' ' && c != '\t') {
				/* Backtrack to after the space */
				vtcol = last_space_vtcol;
				i = last_space_i;
				bytes = utf8_to_unicode(line_text, i, actual_len, &c);
				if (bytes <= 0) bytes = 1;
			}
			/* Skip leading space on new line */
			if (c == ' ' || c == '\t') {
				i += bytes;
				last_space_i = -1;
				last_space_vtcol = -1;
			}
			/* Wrap to next row */
			vtcol = 0;
			rows++;
			last_space_i = -1;
			last_space_vtcol = -1;
			continue;
		}

		/* Track spaces for word-wrap */
		if (c == ' ' || c == '\t') {
			last_space_i = i + bytes;
			last_space_vtcol = vtcol + 1;
		}

		/* Calculate display width */
		if (c == '\t') {
			vtcol = ((vtcol + tab_width) / tab_width) * tab_width;
		} else if (c < 32 || c == 127) {
			vtcol += 2;  /* Control char like ^A */
		} else {
			vtcol++;  /* Normal char */
		}
		i += bytes;
	}

	/* Post-loop check: if cursor is at/past wrap_col and there's more text,
	 * the next character would wrap, so cursor should be on the next row.
	 * If cursor is at end of buffer, it stays at end of current visual line. */
	if (vtcol >= wrap_col && byte_offset < actual_len) {
		rows++;
		vtcol = 0;
	}

	*out_col = vtcol;
	return rows;
}

/*
 * updpos:
 *	update the position of the hardware cursor and handle extended
 *	lines. This is the only update for simple moves.
 */
void updpos(void)
{
	struct line *lp;
	int new_currow, new_curcol;
	int old_currow = atomic_load_explicit(&currow, memory_order_acquire);
	int old_curcol = atomic_load_explicit(&curcol, memory_order_acquire);
	term_snapshot_t tsnap = term_snapshot();

	/* find the current row - count buffer lines from window top */
	lp = curwp->w_linep;
	new_currow = curwp->w_toprow;
	while (lp != curwp->w_dotp) {
		/* With word-wrap, each line may occupy multiple screen rows */
		if (curwp->w_wrap_col > 0 && lp != curwp->w_bufp->b_linep) {
			new_currow += calculate_wordwrap_line_rows(lp, curwp->w_wrap_col, 8);
		} else {
			++new_currow;
		}
		lp = lforw(lp);
	}

	/* find the current column - use word-wrap aware calculation if soft-wrap enabled */
	if (curwp->w_wrap_col > 0) {
		int extra_rows = calculate_wordwrap_cursor_pos(curwp->w_dotp, curwp->w_doto,
		                                                curwp->w_wrap_col, 8, &new_curcol);
		new_currow += extra_rows;
	} else {
		new_curcol = calculate_display_column_cached(curwp->w_dotp, curwp->w_doto, 8);
	}

	/* Clamp currow to window bounds to prevent display corruption.
	 * This guards against cursor at sentinel or beyond-window positions. */
	int max_row = curwp->w_toprow + curwp->w_ntrows - 1;
	if (new_currow > max_row) {
		LOG_DEBUGF("Display: updpos() CLAMPING currow %d to max %d", new_currow, max_row);
		new_currow = max_row;
	}
	if (new_currow < curwp->w_toprow) {
		LOG_DEBUGF("Display: updpos() CLAMPING currow %d to min %d", new_currow, curwp->w_toprow);
		new_currow = curwp->w_toprow;
	}

	/* Atomically update cursor position (C23 signal safety) */
	atomic_store_explicit(&currow, new_currow, memory_order_release);
	atomic_store_explicit(&curcol, new_curcol, memory_order_release);

	/* HIGH: Log cursor position tracking - include w_doto for column debugging */
	LOG_DEBUGF("Display: updpos() currow=%d->%d curcol=%d->%d lbound=%d w_doto=%d w_dotp=%p",
	           old_currow, new_currow, old_curcol, new_curcol, lbound, curwp->w_doto, (void*)curwp->w_dotp);

	/* if extended, flag so and update the virtual line image */
	if (new_curcol >= tsnap.ncol - 1) {
		if (new_currow >= 0 && new_currow < tsnap.mrow && vscreen && vscreen[new_currow]) {
			vscreen[new_currow]->v_flag |= (VFEXT | VFCHG);
		}
		updext();
	} else
		lbound = 0;
}

/*
 * Atomic line number cache for instant status line updates
 */
int get_line_number_cached(struct window *wp)
{
	if (!wp || !wp->w_dotp) return 1;
	
	// Check if cache is clean and current
	bool cache_dirty = atomic_load(&wp->w_line_cache_dirty);
	if (!cache_dirty) {
		int cached_line = atomic_load(&wp->w_line_cache);
		if (cached_line > 0) return cached_line;  // Ensure never return 0
	}
	
	// Cache miss - calculate from scratch
	int current_line = 1;
	struct line *lp = wp->w_bufp->b_linep->l_fp;
	while (lp != wp->w_dotp && lp != wp->w_bufp->b_linep) {
		current_line++;
		lp = lp->l_fp;
	}
	
	// Ensure we never cache or return 0
	if (current_line <= 0) current_line = 1;
	
	// Update cache atomically
	atomic_store(&wp->w_line_cache, current_line);
	atomic_store(&wp->w_line_cache_dirty, false);
	
	return current_line;
}

void invalidate_line_cache(struct window *wp)
{
	if (wp) {
		atomic_store(&wp->w_line_cache_dirty, true);
	}
}

/*
 * upddex:
 *	de-extend any line that derserves it
 */
void upddex(void)
{
	struct window *wp;
	struct line *lp;
	int i;

	wp = wheadp;

	while (wp != nullptr) {
		lp = wp->w_linep;
		i = wp->w_toprow;

		while (i < wp->w_toprow + wp->w_ntrows) {
			if (vscreen[i]->v_flag & VFEXT) {
				if ((wp != curwp) || (lp != wp->w_dotp) ||
				    (curcol < term.t_ncol - 1)) {
					vtmove(i, 0);
					show_line(lp);
					vteeol();

					/* this line no longer is extended */
					vscreen[i]->v_flag &= ~VFEXT;
					vscreen[i]->v_flag |= VFCHG;
				}
			}
			lp = lforw(lp);
			++i;
		}
		/* and onward to the next window */
		wp = wp->w_wndp;
	}
}

/*
 * updgar:
 *	if the screen is garbage, clear the physical screen and
 *	the virtual screen and force a full update
 */
void updgar(void)
{
	unicode_t *txt;
	int i, j;

	for (i = 0; i < term.t_nrow; ++i) {
		vscreen[i]->v_flag |= VFCHG;
		vscreen[i]->v_flag &= ~VFREV;
		txt = pscreen[i]->v_text;
		for (j = 0; j < term.t_ncol; ++j)
			txt[j] = ' ';
	}

	movecursor(0, 0);	/* Erase the screen. */
	TTeeop();		/* Use safe terminal wrapper */
	sgarbf = false;		/* Erase-page clears */
	mpresf = false;		/* the message area. */
	mlerase();		/* needs to be cleared if colored */
}

/*
 * updupd:
 *	update the physical screen from the virtual screen
 *
 * int force;		forced update flag
 */
int updupd(int force)
{
	struct video *vp1;
	int i;
	int rows_updated = 0;

	LOG_DEBUGF("Display: updupd(force=%d) starting, rows=%d", force, term.t_nrow);

	int cur = atomic_load_explicit(&currow, memory_order_acquire);
	int prev_cur = atomic_load_explicit(&prev_cursor_row, memory_order_acquire);
	int row_min = atomic_load_explicit(&cursor_row_min, memory_order_acquire);
	int row_max = atomic_load_explicit(&cursor_row_max, memory_order_acquire);

	LOG_DEBUGF("UPDUPD: start currow=%d prev_cursor_row=%d range=[%d,%d]",
	           cur, prev_cur, row_min, row_max);

	/* Update cursor row range to include current position */
	if (row_min < 0 || row_max < 0) {
		/* First update - initialize range to current row */
		row_min = cur;
		row_max = cur;
	} else {
		/* Expand range to include current cursor position */
		if (cur < row_min) row_min = cur;
		if (cur > row_max) row_max = cur;
	}
	atomic_store_explicit(&cursor_row_min, row_min, memory_order_release);
	atomic_store_explicit(&cursor_row_max, row_max, memory_order_release);

	for (i = 0; i < term.t_nrow; ++i) {
		vp1 = vscreen[i];

		/* Check if this row is in the cursor movement range */
		bool in_cursor_range = (i >= row_min && i <= row_max);
		bool is_cursor_row = (i == cur);
		/* CRITICAL: prev_cursor_row ALWAYS needs update when it differs from currow,
		 * even if it's outside the tracked cursor_row_min/max range. This handles
		 * the case where updone() ran and range was reset before we could clear
		 * the old cursor line's highlight. */
		bool was_prev_cursor = (highlight_current_line && hiline_style != 0 &&
		                        prev_cur >= 0 && i == prev_cur && i != cur);

		/* for each line that needs to be updated */
		if ((vp1->v_flag & VFCHG) != 0) {
			// Update checksum after content changes
			video_update_checksum(vp1);

			// Use checksum-optimized comparison before expensive update
			// NEVER skip rows in cursor range or prev cursor - they need highlight updates
			if (!force && !in_cursor_range && !was_prev_cursor && !video_lines_differ(vp1, pscreen[i])) {
				vp1->v_flag &= ~VFCHG; // Clear change flag - lines are identical
			} else {
				if (in_cursor_range || was_prev_cursor) {
					LOG_DEBUGF("UPDUPD: updating row %d (in_range=%d cursor=%d was_prev=%d)",
					           i, in_cursor_range, is_cursor_row, was_prev_cursor);
				}
				// When force is set (hard refresh), invalidate ENTIRE pscreen row
				// to ensure updateline rewrites every character. Otherwise, the
				// incremental update path may skip positions where vscreen matches
				// pscreen, even if the actual terminal content differs (e.g., from
				// direct terminal writes by extensions).
				if (force) {
					for (int j = 0; j < term.t_ncol; j++) {
						pscreen[i]->v_text[j] = 0xFFFFFFFF;  // Impossible unicode value
					}
				}
				updateline(i, vp1, pscreen[i]);
				rows_updated++;
			}
		} else if (in_cursor_range || was_prev_cursor) {
			/* All rows in cursor range OR prev cursor ALWAYS get updated for highlight cleanup */
			LOG_DEBUGF("UPDUPD: force-updating row %d (in_range=%d was_prev=%d)",
			           i, in_cursor_range, was_prev_cursor);
			updateline(i, vp1, pscreen[i]);
			rows_updated++;
		}
	}

	/* Reset cursor range for next frame - only current row */
	LOG_DEBUGF("UPDUPD: resetting cursor range, prev=%d -> cur=%d", prev_cur, cur);
	atomic_store_explicit(&prev_cursor_row, cur, memory_order_release);
	atomic_store_explicit(&cursor_row_min, cur, memory_order_release);
	atomic_store_explicit(&cursor_row_max, cur, memory_order_release);

	LOG_DEBUGF("Display: updupd() complete, %d rows updated", rows_updated);
	return true;
}

/*
 * updext:
 *	update the extended line which the cursor is currently
 *	on at a column greater than the terminal width. The line
 *	will be scrolled right or left to let the user see where
 *	the cursor is
 */
static void updext(void)
{
	int rcursor;	/* real cursor location */
	struct line *lp;	/* pointer to current line */

	/* calculate what column the real cursor will end up in */
	rcursor = ((curcol - term.t_ncol) % term.t_scrsiz) + term.t_margin;
	taboff = lbound = curcol - rcursor + 1;

	/* scan through the line outputing characters to the virtual screen */
	/* once we reach the left edge                                  */
	vtmove(currow, -lbound);	/* start scanning offscreen */
	lp = curwp->w_dotp;	/* line to output */
	show_line(lp);

	/* truncate the virtual line, restore tab offset */
	vteeol();
	taboff = 0;

	/* and put a '$' in column 1 */
	vscreen[currow]->v_text[0] = '$';
}

/*
 * Update a single line. This does not use insert/delete character sequences;
 * it overwrites the entire line. Updates physical row and column variables.
 * Exploits erase-to-end-of-line for efficiency.
 */

/*
 * updateline()
 *
 * int row;		row of screen to update
 * struct video *vp1;	virtual screen image
 * struct video *vp2;	physical screen image
 */
static int updateline(int row, struct video *vp1, struct video *vp2)
{
	unicode_t *cp1;
	unicode_t *cp2;
	unicode_t *cp3;
	unicode_t *cp4;
	unicode_t *cp5;
	int nbflag;	/* non-blanks to the right flag? */
	int rev;		/* status line rendered flag */
	int req;		/* status line request flag */

	/* Hot path - logging disabled for performance
	LOG_DEBUGF("Display: updateline(row=%d) vflag=0x%x", row, vp1->v_flag); */

	/* set up pointers to virtual and physical lines */
	cp1 = &vp1->v_text[0];
	cp2 = &vp2->v_text[0];

	/* Determine if this entire row should be highlighted (cursor line)
	 * Use currow directly - it's where the cursor actually IS.
	 * The v_linep comparison was unreliable due to timing issues. */
	bool row_is_cursor_line = (highlight_current_line && hiline_style != 0 && row == currow);
	/* Check if this row needs highlight cleared - ANY row in the cursor movement
	 * range that isn't the current cursor row. This handles rapid j/k bouncing
	 * where multiple rows may have stale highlight from deferred updates. */
	bool row_needs_highlight_clear = (highlight_current_line && hiline_style != 0 &&
	                                  row != currow &&
	                                  row >= cursor_row_min && row <= cursor_row_max);
	LOG_DEBUGF("UPDATELINE: row=%d currow=%d range=[%d,%d] is_cursor=%d needs_clear=%d",
	           row, currow, cursor_row_min, cursor_row_max, row_is_cursor_line, row_needs_highlight_clear);

	/* if status line state changed or colors changed, re-write entire line */
	rev = (vp1->v_flag & VFREV) == VFREV;
	req = (vp1->v_flag & VFREQ) == VFREQ;
	if ((rev != req)
	    || req  /* Status line ALWAYS needs full render for truecolor background */
	    || row_is_cursor_line  /* Cursor line ALWAYS needs full render for highlight */
	    || row_needs_highlight_clear  /* Any row in cursor range needs full render to CLEAR highlight */
	    ) {
		movecursor(row, 0);	/* Go to start of line. */

		/* Sync highlight state BEFORE reset (raw_rev tracks state internally) */
		(*term.t_rev)(false);

		/* Reset all attributes to ensure clean state. */
		ttputs("\033[0m");

		/* Status line (VFREQ) gets dark truecolor background and foreground */
		if (req) {
			emit_modeline_bg();  /* Palette-based modeline background */
			emit_modeline_fg();  /* Palette-based modeline foreground */
		}

		/* scan through the line and dump it to the screen and
		   the virtual screen array                             */
		cp3 = &vp1->v_text[term.t_ncol];

        /* For cursor line, turn on highlight for ENTIRE line (truecolor) */
        if (row_is_cursor_line) {
            (*term.t_rev)(true);
        }

        /* Detect "EVIL" text in modeline for special coloring */
        int evil_start = -1, evil_end = -1;
        if (req && term.t_ncol >= 7) {
            /* Check for "   EVIL" at start of modeline (3 spaces + EVIL) */
            if (vp1->v_text[0] == ' ' && vp1->v_text[1] == ' ' && vp1->v_text[2] == ' ' &&
                vp1->v_text[3] == 'E' && vp1->v_text[4] == 'V' &&
                vp1->v_text[5] == 'I' && vp1->v_text[6] == 'L') {
                evil_start = 3;  /* Start of "EVIL" */
                evil_end = 7;    /* End of "EVIL" */
            }
        }
        int col_pos = 0;  /* Track column position for EVIL detection */

#if UEMACS_DEBUG_LOG
        /* COMPREHENSIVE DEBUG: Dump first 60 chars of EVERY row */
        {
            char line_preview[61];
            int preview_len = (term.t_ncol < 60) ? term.t_ncol : 60;
            for (int pi = 0; pi < preview_len; pi++) {
                unicode_t ch = SYNTAX_STRIP_BITS(cp1[pi]);
                line_preview[pi] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '.';
            }
            line_preview[preview_len] = '\0';
            LOG_DEBUGF("RENDER_LINE: row=%d cursor=%d content=[%s] evil=[%d,%d]",
                       row, row_is_cursor_line, line_preview, evil_start, evil_end);
        }
#endif

        /* Track current syntax face to minimize SGR escape sequences */
        int current_face = FACE_DEFAULT;
        bool syntax_active = !req && !row_is_cursor_line && g_palette.syntax_enabled;

        /* Debug: log syntax_active state for first 3 rows */
        if (row < 3) {
            LOG_DEBUGF("UPDATELINE: row=%d syntax_active=%d (req=%d cursor=%d enabled=%d)",
                       row, syntax_active, req, row_is_cursor_line, g_palette.syntax_enabled);
        }

        while (cp1 < cp3) {
            unicode_t ch = *cp1;
            bool char_highlighted = (ch & HIGHLIGHT_BIT) != 0;
            bool char_selected = (ch & SELECTION_BIT) != 0;
            int face = SYNTAX_DECODE_FACE(ch);
            ch = SYNTAX_STRIP_BITS(ch);  /* Strip all style bits */

            /* EVIL text coloring in modeline */
            if (col_pos == evil_start) {
                /* Emit EVIL color from palette */
                char evil_sgr[32];
                snprintf(evil_sgr, sizeof(evil_sgr), "\033[38;2;%d;%d;%dm",
                         g_palette.mode_evil_rgb[0], g_palette.mode_evil_rgb[1],
                         g_palette.mode_evil_rgb[2]);
                ttputs(evil_sgr);
            } else if (col_pos == evil_end) {
                /* Reset to modeline foreground */
                emit_modeline_fg();
            }

            /* Selection highlighting takes priority over ruler highlighting.
             * CRITICAL: When row_needs_highlight_clear is true, we're clearing OLD cursor-line
             * highlight. But we must STILL render the column ruler. The column ruler is at
             * a SPECIFIC column, so check if this is that column. */
            bool is_ruler_column = (column_ruler_enabled && col_pos == column_ruler_column - 1);
            if (char_selected && !row_is_cursor_line && !req && !row_needs_highlight_clear) {
                ttputs(sgr_selection_bg());  /* Palette-based selection background */
            } else if (!row_is_cursor_line && !req) {
                /* Not cursor line: render column ruler if this is the ruler column */
                if (is_ruler_column) {
                    emit_ruler_bg();
                }
            }

            /* Apply syntax highlighting color if face changed */
            if (syntax_active && face != current_face) {
                /* Debug: log face change for first 3 rows */
                if (row < 3) {
                    LOG_DEBUGF("FACE_EMIT: row=%d col=%d face=%d->%d",
                               row, col_pos, current_face, face);
                }
                if (face != FACE_DEFAULT) {
                    /* Apply new face color and style */
                    const char *style = sgr_syntax_style(face);
                    if (style[0]) ttputs(style);
                    ttputs(sgr_syntax_fg(face));
                } else {
                    /* Reset to default */
                    ttputs(sgr_syntax_style_reset());
                    ttputs("\033[39m");  /* Default foreground */
                }
                current_face = face;
            }

            TTputc(ch);

            /* Reset after highlighted/selected character */
            if (char_selected && !row_is_cursor_line && !req && !row_needs_highlight_clear) {
                ttputs("\033[49m");
            } else if (is_ruler_column && !row_is_cursor_line && !req) {
                ttputs("\033[49m");  /* Reset after column ruler */
            }

            ++ttcol;
            ++col_pos;
            *cp2++ = *cp1++;
        }

        /* Reset syntax style at end of line if active */
        if (syntax_active && current_face != FACE_DEFAULT) {
            ttputs(sgr_syntax_style_reset());
            ttputs("\033[39m");
        }

        /* Turn off highlight/modeline background at end of line */
        if (row_is_cursor_line) {
            (*term.t_rev)(false);  /* Use proper state-tracked reset */
        } else if (req) {
            ttputs("\033[49m");  /* Status line doesn't use rev state */
        }

		/* update the needed flags */
		vp1->v_flag &= ~VFCHG;
		if (req)
			vp1->v_flag |= VFREV;
		else
			vp1->v_flag &= ~VFREV;

		/* Update pscreen checksum after full-line render.
		 * Without this, pscreen checksum remains stale and future
		 * comparisons may fail to detect changed content. */
		video_update_checksum(vp2);

		return true;
	}

	/* advance past any common chars at the left */
	while (cp1 != &vp1->v_text[term.t_ncol] && cp1[0] == cp2[0]) {
		++cp1;
		++cp2;
	}

/* This can still happen, even though we only call this routine on changed
 * lines. A hard update is always done when a line splits, a massive
 * change is done, or a buffer is displayed twice. This optimizes out most
 * of the excess updating. A lot of computes are used, but these tend to
 * be hard operations that do a lot of update, so I don't really care.
 */
	/* if both lines are the same, no update needs to be done */
	if (cp1 == &vp1->v_text[term.t_ncol]) {
		vp1->v_flag &= ~VFCHG;	/* flag this line is changed */
		return true;
	}

	/* find out if there is a match on the right */
	nbflag = false;
	cp3 = &vp1->v_text[term.t_ncol];
	cp4 = &vp2->v_text[term.t_ncol];

	while (cp3[-1] == cp4[-1]) {
		--cp3;
		--cp4;
		if (cp3[0] != ' ')	/* Note if any nonblank */
			nbflag = true;	/* in right match. */
	}

	cp5 = cp3;

	/* Erase to EOL? Modern terminals always support clear-to-EOL. */
	if (nbflag == false && (req != true)) {
		while (cp5 != cp1 && cp5[-1] == ' ')
			--cp5;

		if (cp3 - cp5 <= 3)	/* Use only if erase is */
			cp5 = cp3;	/* fewer characters. */
	}

	movecursor(row, cp1 - &vp1->v_text[0]);	/* Go to start of line. */

	/* Sync highlight state BEFORE reset (raw_rev tracks state internally) */
	(*term.t_rev)(false);

	/* Reset all attributes to ensure clean state. */
	ttputs("\033[0m");

	/* Status line (VFREQ/req) gets dark truecolor background and foreground */
	if (req) {
		emit_modeline_bg();  /* Palette-based modeline background */
		emit_modeline_fg();  /* Palette-based modeline foreground */
	}

	/* For cursor line, turn on highlight for ENTIRE rendering span (truecolor) */
	if (row_is_cursor_line) {
		(*term.t_rev)(true);
	}

	/* DEBUG: Log incremental update info - rows 0-4 and overlay rows (29+) */
	if (row < 5 || row >= 29) {
		int start_col = (int)(cp1 - &vp1->v_text[0]);
		int num_chars = (int)(cp5 - cp1);
		unicode_t first_ch = cp1[0] & ~(HIGHLIGHT_BIT | SELECTION_BIT);
		if (first_ch >= 0x80) {
			LOG_DEBUGF("Display: updateline() row=%d start_col=%d num_chars=%d first=U+%04X",
			           row, start_col, num_chars, first_ch);
		} else {
			LOG_DEBUGF("Display: updateline() row=%d start_col=%d num_chars=%d first=[%c] (0x%x)",
			           row, start_col, num_chars,
			           first_ch >= 32 ? (char)first_ch : '.', cp1[0]);
		}
	}

	/* Track current syntax face for incremental updates */
	int current_face = FACE_DEFAULT;
	bool syntax_active = !req && !row_is_cursor_line && g_palette.syntax_enabled;

	while (cp1 != cp5) {	/* Ordinary. */
		unicode_t ch = *cp1;
		bool char_highlighted = (ch & HIGHLIGHT_BIT) != 0;
		bool char_selected = (ch & SELECTION_BIT) != 0;
		int face = SYNTAX_DECODE_FACE(ch);
		ch = SYNTAX_STRIP_BITS(ch);  /* Strip ALL style bits */

		/* Selection highlighting takes priority over ruler highlighting */
		if (char_selected && !row_is_cursor_line && !req) {
			ttputs(sgr_selection_bg());  /* Palette-based selection background */
		} else if (char_highlighted && !row_is_cursor_line && !req) {
			emit_ruler_bg();  /* Palette-based ruler background */
		}

		/* Apply syntax highlighting color if face changed */
		if (syntax_active && face != current_face) {
			if (face != FACE_DEFAULT) {
				const char *style = sgr_syntax_style(face);
				if (style[0]) ttputs(style);
				ttputs(sgr_syntax_fg(face));
			} else {
				ttputs(sgr_syntax_style_reset());
				ttputs("\033[39m");
			}
			current_face = face;
		}

		TTputc(ch);

		/* Reset after highlighted/selected character */
		if ((char_selected || char_highlighted) && !row_is_cursor_line && !req) {
			ttputs("\033[49m");
		}

		++ttcol;
		*cp2++ = *cp1++;
	}

	/* Reset syntax style at end of incremental update */
	if (syntax_active && current_face != FACE_DEFAULT) {
		ttputs(sgr_syntax_style_reset());
		ttputs("\033[39m");
	}

	if (cp5 != cp3) {	/* Erase. */
		/* Cursor line highlight is already on, TTeeol will use it */
		TTeeol();
		while (cp1 != cp3)
			*cp2++ = *cp1++;
	}

	/* Turn off highlight/modeline background at end */
	if (row_is_cursor_line) {
		(*term.t_rev)(false);  /* Use proper state-tracked reset */
	} else if (req) {
		ttputs("\033[49m");  /* Status line doesn't use rev state */
	}

	vp1->v_flag &= ~VFCHG;	/* flag this line as updated */

	// Update physical screen checksum after copying data
	video_update_checksum(vp2);

	/* Copy line pointer to physical screen for next frame */
	vp2->v_linep = vp1->v_linep;

	return true;
}

void upmode(void)
{				/* update all the mode lines */
	struct window *wp;

	wp = wheadp;
	while (wp != nullptr) {
		wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}
}

/*
 * Send a command to the terminal to move the hardware cursor to row "row"
 * and column "col". The row and column arguments are origin 0. Optimize out
 * random calls. Update "ttrow" and "ttcol".
 */
void movecursor(int row, int col)
{
	/* Hot path - logging disabled for performance
	LOG_DEBUGF("Display: movecursor(%d,%d) from ttrow=%d ttcol=%d",
	           row, col, ttrow, ttcol); */

	/* Only mask SIGWINCH when actually moving - reduces syscall overhead */
	if (row != ttrow || col != ttcol) {
		sigset_t oldmask, mask;
		sigemptyset(&mask);
#ifdef SIGWINCH
		sigaddset(&mask, SIGWINCH);
#endif
		pthread_sigmask(SIG_BLOCK, &mask, &oldmask);

		ttrow = row;
		ttcol = col;
		TTmove(row, col);

		pthread_sigmask(SIG_SETMASK, &oldmask, nullptr);
	}
}

/* Note: Message line functions moved to message_line.c */

/* Get terminal size from system.
   Store number of lines into *heightp and width into *widthp.
   If zero or a negative number is stored, the value is not valid.  */

void getscreensize(int *widthp, int *heightp)
{
#ifdef TIOCGWINSZ
	struct winsize size;
	*widthp = 0;
	*heightp = 0;
	if (ioctl(0, TIOCGWINSZ, &size) < 0)
		return;
	*widthp = size.ws_col;
	*heightp = size.ws_row;
#else
	*widthp = 0;
	*heightp = 0;
#endif
}

#ifdef SIGWINCH
// Check and handle pending resize - called from main loop
// Uses modern signal_handler.c infrastructure (sigaction with SA_RESTART)
void check_pending_resize(void)
{
    // sig_winch_pending is set by handle_winch() in signal_handler.c
    // which uses proper sigaction() - no need for re-registration
    extern volatile sig_atomic_t sig_winch_pending;

    if (sig_winch_pending) {
        // Defer resize while an edit transaction is open
        if (atomic_load(&edit_transaction_depth) > 0) {
            return;
        }
        sig_winch_pending = 0;
        int w, h;
        getscreensize(&w, &h);
        if (h && w && (h - 1 != term.t_nrow || w != term.t_ncol))
            newscreensize(h, w);
    }
}
#endif

/*
 * newscreensize - Handle terminal resize
 * IMPORTANT: Never call from signal context. Use check_pending_resize() instead.
 */
static int newscreensize(int h, int w)
{
	/* Defer resize if displaying or in unsafe context */
	extern volatile sig_atomic_t sig_winch_pending;
	if (displaying || sig_winch_pending) {
		chg_width = w;
		chg_height = h;
		return false;
	}

	chg_width = chg_height = 0;
	if (h - 1 < term.t_mrow)
		newsize(true, h);
	if (w < term.t_mcol)
		newwidth(true, w);

	update(true);
	return true;
}

/* Note: modern_modeline() and helpers moved to modeline.c */
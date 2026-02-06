/*	edef.h
 *
 *	Global variable definitions
 *
 *	written by Dave G. Conroy
 *	modified by Steve Wilhite, George Jones
 *	greatly modified by Daniel Lawrence
 *	modified by Petri Kutvonen
 */
#ifndef EDEF_H_
#define EDEF_H_

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>  // C23: Modern boolean types
#include <stdatomic.h> // C23: Atomics for thread/signal safety
#include "editor_mode.h" // Vim Mode State
#include "terminal/input_state.h" // For input_key_event_t in macro system

// C23: Modern function pointer with explicit parameter names and attributes
typedef int (*fn_t)([[maybe_unused]] int prefix_arg, [[maybe_unused]] int count);

/* Window iteration macro - eliminates duplicate while loops */
#define FOR_EACH_WINDOW(wp) \
	for (struct window *wp = wheadp; wp != nullptr; wp = wp->w_wndp)

/* Initialized global external declarations. */

extern _Atomic int vim_mode_active;      /* Global toggle for Vim/Evil mode - C23 atomic */
extern long evil_mode_start_time;        /* Timestamp for "EVIL" splash (flash for 3 seconds) */
extern struct vim_state g_vim_state; /* Global Vim Mode state */
extern _Atomic(struct keymap *) vim_normal_keymap; /* Vim Normal Mode Keymap */
extern _Atomic(struct keymap *) vim_visual_keymap; /* Vim Visual Mode Keymap */

extern _Atomic int fillcol;		/* Fill column - C23 atomic */
/* Modern keyboard macro system - stores full input events */
typedef struct keyboard_macro {
	input_key_event_t events[NKBDM];  /* Event storage */
	size_t count;                      /* Number of recorded events */
	size_t playback_pos;               /* Current playback position */
} keyboard_macro_t;

extern keyboard_macro_t g_macro;       /* Global macro storage */
extern char pat[];		/* Search pattern               */
extern char rpat[];		/* Replacement pattern          */
extern _Atomic(const char *) execstr;		/* pointer to string to execute - C23 atomic */
extern char golabel[];		/* current line to go to        */
extern int execlevel;		/* execution IF level           */
/* Legacy terminal capability flags removed - modern terminals always support:
 * - clear-to-EOL (eolexist was always true)
 * - truecolor (revexist was always false - no reverse video)
 * - buffered I/O (flickcode was always false)
 */
extern char *modename[];	/* text names of modes          */
extern char *mode2name[];	/* text names of modes          */
extern char modecode[];		/* letters to represent modes   */
extern struct key_tab keytab[];	/* key bind to functions table  */
extern struct name_bind names[];/* name to function table */
extern int gmode;		/* global editor mode           */
extern int gflags;		/* global control flag          */
extern int gfcolor;		/* global forgrnd color (white) */
extern int gbcolor;		/* global backgrnd color (black) */
extern int gasave;		/* global ASAVE size            */
extern int gacount;		/* count until next ASAVE       */
extern _Atomic int sgarbf;		/* State of screen unknown - C23 atomic */
extern _Atomic int mpresf;		/* Stuff in message line - C23 atomic */
extern _Atomic int clexec;		/* command line execution flag - C23 atomic */
extern _Atomic int mstore;		/* storing text to macro flag - C23 atomic */
extern _Atomic int discmd;		/* display command flag - C23 atomic */
extern _Atomic int disinp;		/* display input characters - C23 atomic */
extern _Atomic(struct buffer *) bstore;	/* buffer to store macro text to - C23 atomic */
extern int vtrow;		/* Row location of SW cursor */
extern int vtcol;		/* Column location of SW cursor */
extern int ttrow;		/* Row location of HW cursor */
extern int ttcol;		/* Column location of HW cursor */
extern int lbound;		/* leftmost column of current line
				   being displayed */
extern int taboff;		/* tab offset for display       */
extern int tabmask;
extern char *cname[];		/* names of colors              */
extern struct kill_ring g_kill_ring;	/* C23 atomic kill ring */
extern int yanked_size;		/* Size of last yank for yankpop */
extern char temp_kill_buf[];	/* Temporary kill buffer */
extern size_t temp_kill_len;	/* Temporary kill buffer length */
extern struct window *swindow;	/* saved window pointer         */
extern struct window *terminal_window;	/* active terminal window (nullptr if closed) */
extern _Atomic int kbdmode;	/* current keyboard macro mode (atomic for thread safety) */
extern _Atomic(struct keymap *) current_keymap;	/* current keymap - C23 atomic */
extern int kbdrep;		/* number of repetitions        */
extern int restflag;		/* restricted use?              */
extern int lastkey;		/* last keystoke                */
extern int macbug;		/* macro debuging flag          */
extern char errorm[];		/* error literal                */
extern char truem[];		/* true literal                 */
extern char falsem[];		/* false litereal               */
extern int cmdstatus;		/* last command status          */
extern char palstr[];		/* palette string               */
extern int saveflag;		/* Flags, saved with the $target var */
extern char *fline;		/* dynamic return line */
extern int flen;		/* current length of fline */
extern int rval;		/* return value of a subprocess */
extern _Atomic int nullflag;		/* accept null characters - C23 atomic */
extern _Atomic int justflag;		/* justify, don't fill - C23 atomic */
extern _Atomic int overlap;		/* line overlap in forw/back page - C23 atomic */
extern _Atomic int scrollcount;		/* number of lines to scroll - C23 atomic */
extern _Atomic int edit_transaction_depth; /* transactional editing depth */
/* Display preferences */
extern int highlight_current_line;
extern int column_ruler_enabled;
extern int column_ruler_column;
extern int hiline_style;
extern int ruler_style;
/* Highlight tuning (percentages and strategy) */
extern int highlight_intensity_pct;      /* 0..50 (default 12) */
extern int ruler_intensity_pct;          /* 0..50 (default 15) */
extern int intersection_intensity_pct;   /* 0..50 (default 18) */
/* 0=blend toward fg, 1=lighten bg, 2=darken bg */
extern int highlight_strategy;
/* Modeline toggles */
extern int modeline_show_git;
extern int modeline_show_stats;
extern int modeline_show_modes;
extern int modeline_show_position;
extern int modeline_ext_position;  /* 0=left, 1=right, 2=auto */
extern int modeline_ext_max_width; /* Max chars for extension segments */
/* Backup settings */
extern int make_backup;
extern char backup_dir[512];
/* Cursor style: 0=block, 1=bar, 2=underline */
extern int cursor_style;
/* Save-time processing options */
extern int trim_trailing_whitespace;
extern int insert_final_newline;
/* Terminal integration settings */
extern int terminal_enabled;
extern int terminal_height_percent;
extern char terminal_shell[512];
extern int terminal_scrollback;
extern int terminal_close_on_exit;

/* Performance tuning */
extern int perf_frame_interval;
extern int perf_escape_timeout;
extern int perf_parallel_threshold;
extern int perf_max_threads;

/* Window system - overlay support */
extern int effective_nrow;	/* Usable rows for windows (shrinks when overlay active) */

/* Uninitialized global external declarations. */

extern _Atomic int currow;		/* Cursor row - C23 atomic for signal safety */
extern _Atomic int curcol;		/* Cursor column - C23 atomic for signal safety */
extern int thisflag;		/* Flags, this command          */
extern int lastflag;		/* Flags, last command          */
extern int curgoal;		/* Goal for C-P, C-N            */
extern struct window *curwp;		/* Current window               */
extern struct buffer *curbp;		/* Current buffer               */
extern struct window *wheadp;                /* Head of list of windows      */
extern struct buffer *bheadp;		/* Head of list of buffers      */
extern struct buffer *blistp;		/* Buffer for C-X C-B           */

extern char sres[NBUFN];	        /* Current screen resolution.   */
extern char pat[];		        /* Search pattern.              */
extern char tap[];		        /* Reversed pattern array.      */
extern char rpat[];		        /* Replacement pattern.         */

extern unsigned int matchlen;
extern unsigned int mlenold;
extern char *patmatch;
extern struct line *matchline;
extern int matchoff;

extern char *dname[];		/* Directive name table.        */

#if	DEBUGM
/* Vars needed for macro debugging output. */
extern char outline[];		/* Global string to hold debug line text. */
#endif

/* Terminal table defined only in term.c */
extern struct terminal term;

#endif  /* EDEF_H_ */

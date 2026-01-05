#include "estruct.h"
#include "edef.h"
#include <stdbool.h>  // C23: Modern boolean types

/* initialized global definitions */

/* Global Vim State - Default to Normal Mode for now (will be configurable) */
_Atomic int vim_mode_active = 0; /* 0 = Standard μEmacs, 1 = Vim/Evil Mode Active - C23 atomic */
long evil_mode_start_time = 0; /* Timestamp when evil-mode was enabled (for startup splash) */
struct vim_state g_vim_state = {
    .current_mode = MODE_INSERT, /* Default to Insert (Standard Emacs) for compatibility */
    .operator_pending = OP_NONE,
    .visual_anchor_line = 0,
    .visual_anchor_col = 0
};

_Atomic(struct keymap *) vim_normal_keymap = nullptr;
_Atomic(struct keymap *) vim_visual_keymap = nullptr;

int fillcol = 72;		/* Current fill column          */

/* Modern keyboard macro system - stores full input events */
keyboard_macro_t g_macro = {
	.events = {{0}},
	.count = 0,
	.playback_pos = 0
};
_Atomic(const char *) execstr = nullptr;		/* pointer to string to execute - C23 atomic */
char golabel[NPAT] = "";	/* current line to go to        */
int execlevel = 0;		/* execution IF level           */
/* Legacy terminal capability flags removed:
 * - eolexist: All modern terminals support clear-to-EOL
 * - revexist: Truecolor only, no reverse video
 * - flickcode: No flicker suppression needed with modern buffered I/O
 */
char *modename[] = {		/* name of modes                */
	"WRAP", "CMODE", "SPELL", "EXACT", "VIEW", "OVER",
	"MAGIC", "CRYPT", "ASAVE", "TERM"
};
char *mode2name[] = {		/* name of modes                */
	"Wrap", "Cmode", "Spell", "Exact", "View", "Over",
	"Magic", "Crypt", "Asave", "Term"
};
char modecode[] = "WCSEVOMCAT";	/* letters to represent modes   */
int gmode = 0;			/* global editor mode           */
int gflags = GFREAD;		/* global control flag          */

/* Buffer hash table for O(1) lookup by name */
struct buffer_hash_entry *buffer_hash_table[BUFFER_HASH_SIZE] = {0};
int gfcolor = 7;		/* global forgrnd color (white) */
int gbcolor = 0;		/* global backgrnd color (black) */
int gasave = 256;		/* global ASAVE size            */
int gacount = 256;		/* count until next ASAVE       */
_Atomic int sgarbf = true;		/* true if screen is garbage - C23 atomic */
int mpresf = false;		/* true if message in last line */
_Atomic int clexec = false;		/* command line execution flag - C23 atomic */
_Atomic int mstore = false;		/* storing text to macro flag - C23 atomic */
int discmd = true;		/* display command flag         */
int disinp = true;		/* display input characters     */
int skip_settings = false;	/* skip loading settings.toml   */
_Atomic(struct buffer *) bstore = nullptr;	/* buffer to store macro text to - C23 atomic */
int vtrow = 0;			/* Row location of SW cursor */
int vtcol = 0;			/* Column location of SW cursor */
int ttrow = HUGE;		/* Row location of HW cursor */
int ttcol = HUGE;		/* Column location of HW cursor */
int lbound = 0;			/* leftmost column of current line
				   being displayed */
int taboff = 0;			/* tab offset for display       */
int tabmask = 0x07;		/* tabulator mask */
char *cname[] = {		/* names of colors              */
	"BLACK", "RED", "GREEN", "YELLOW", "BLUE",
	"MAGENTA", "CYAN", "WHITE"
};

/* C23 Modern Kill Ring - global instance */
struct kill_ring g_kill_ring = {
	.head = 0,
	.tail = 0, 
	.yank_index = 0,
	.count = 0,
	.entries = {{0}}  /* Zero-initialized entries */
};
int yanked_size = 0;		/* Size of last yank for yankpop */

/* Temporary kill buffer for unified kill system */
char temp_kill_buf[8192];
size_t temp_kill_len = 0;
struct window *swindow = nullptr;	/* saved window pointer                 */
struct window *terminal_window = nullptr;	/* active terminal window (NULL if closed) */
_Atomic int kbdmode = MACRO_STOP;	/* current keyboard macro mode (atomic) */
_Atomic(struct keymap *) current_keymap = nullptr;	/* current keymap - C23 atomic */
int kbdrep = 0;			/* number of repetitions        */
int restflag = false;		/* restricted use?              */
int lastkey = 0;		/* last keystoke                */
int macbug = false;		/* macro debuging flag          */
char errorm[] = "ERROR";	/* error literal                */
char truem[] = "true";		/* true literal                 */
char falsem[] = "false";	/* false litereal               */
int cmdstatus = true;		/* last command status          */
char palstr[49] = "";		/* palette string               */
int saveflag = 0;		/* Flags, saved with the $target var */
char *fline = nullptr;		/* dynamic return line */
int flen = 0;			/* current length of fline */
int rval = 0;			/* return value of a subprocess */
int nullflag = false;		/* accept null characters */
int justflag = false;		/* justify, don't fill */
int overlap = 0;		/* line overlap in forw/back page */
int scrollcount = 1;		/* number of lines to scroll */
_Atomic int edit_transaction_depth = 0; /* transactional editing depth */

/* Display preferences (user configurable) */
int highlight_current_line = 1;     /* Highlight cursor row (DISABLED - causing memory issues) */
int column_ruler_enabled = 1;       /* Enable vertical column ruler */
int column_ruler_column = 80;       /* Ruler target column (1-based) */
int hiline_style = 4;               /* 0 none, 1 underline, 2 bold, 3 dim, 4 background */
int ruler_style = 1;                /* 0 none, 1 underline, 2 bold, 3 dim */
/* Highlight tuning defaults */
int highlight_intensity_pct = 15;      /* Cursorline blend percent (VISIBLE) */
int ruler_intensity_pct = 15;          /* Ruler blend percent */
int intersection_intensity_pct = 18;   /* Intersection blend percent */
int highlight_strategy = 0;            /* 0=blend, 1=lighten, 2=darken */

/* Modeline visibility toggles (user configurable via TOML) */
int modeline_show_git = 0;  /* Off by default - spawns git processes */
int modeline_show_stats = 1;
int modeline_show_modes = 1;
int modeline_show_position = 1;
int modeline_ext_position = 2;  /* 0=left, 1=right, 2=auto (default) */
int modeline_ext_max_width = 20; /* Max chars for extension segments */
int linus_mode = 0;  /* Classic Linus-style modeline + B&W colors */

/* Backup settings */
int make_backup = 1;                   /* Create backup files when saving */
char backup_dir[512] = "";             /* Backup directory (empty = same as file) */

/* Cursor style: 0=block, 1=bar, 2=underline */
int cursor_style = 0;

/* Save-time processing options */
int trim_trailing_whitespace = 0;  /* Strip trailing spaces/tabs on save */
int insert_final_newline = 1;      /* Ensure file ends with newline (POSIX) */

/* Terminal integration settings */
int terminal_enabled = 1;          /* Allow terminal feature */
int terminal_height_percent = 45;  /* Window height as % of screen (10-80) */
char terminal_shell[512] = "";     /* Shell to use ("" = $SHELL or /bin/sh) */
int terminal_scrollback = 1000;    /* Lines of scrollback history */
int terminal_close_on_exit = 1;    /* Close terminal window when shell exits */

/* Performance tuning (configurable via [performance] TOML section) */
int perf_frame_interval = 100;     /* Terminal poll interval in ms (50-200) */
int perf_escape_timeout = 25;      /* Escape sequence timeout in ms (10-100) - reduced for rapid input */
int perf_parallel_threshold = 1000; /* Min lines for parallel search */
int perf_max_threads = 8;          /* Max parallel search threads (1-32) */

/* Window system - overlay support */
int effective_nrow = 0;            /* Usable rows for windows (init in edinit) */

/* uninitialized global definitions */

_Atomic int currow;			/* Cursor row - C23 atomic */
_Atomic int curcol;			/* Cursor column - C23 atomic */
int thisflag;			/* Flags, this command          */
int lastflag;			/* Flags, last command          */
int curgoal;			/* Goal for C-P, C-N            */
struct window *curwp;		/* Current window               */
struct buffer *curbp;			/* Current buffer               */
struct window *wheadp;		/* Head of list of windows      */
struct buffer *bheadp;			/* Head of list of buffers      */
struct buffer *blistp;			/* Buffer for C-X C-B           */

char sres[NBUFN];		/* current screen resolution    */
char pat[NPAT];			/* Search pattern               */
char tap[NPAT];			/* Reversed pattern array.      */
char rpat[NPAT];		/* replacement pattern          */

/* The variable matchlen holds the length of the matched
 * string - used by the replace functions.
 * The variable patmatch holds the string that satisfies
 * the search command.
 * The variables matchline and matchoff hold the line and
 * offset position of the *start* of match.
 */
unsigned int matchlen = 0;
unsigned int mlenold = 0;
char *patmatch = nullptr;
struct line *matchline = nullptr;
int matchoff = 0;

/* directive name table:
	This holds the names of all the directives....	*/

char *dname[] = {
	"if", "else", "endif",
	"goto", "return", "endm",
	"while", "endwhile", "break",
	"force"
};

#if	DEBUGM
/*	vars needed for macro debugging output	*/
char outline[NSTRING];		/* global string to hold debug line text */
#endif

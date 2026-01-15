/*      ESTRUCT.H
 *
 *      Structure and preprocessor defines
 *
 *	written by Dave G. Conroy
 *	modified by Steve Wilhite, George Jones
 *      substantially modified by Daniel Lawrence
 *	modified by Petri Kutvonen
 *	modernized for Linux-only build
 */

#ifndef ESTRUCT_H
#define ESTRUCT_H

/* Include CMake-generated configuration */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* System includes for Linux */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
#include <sys/types.h>  /* For pid_t */

/* Forward declarations */
struct edit_stack;
struct keymap;
struct buffer_syntax;
struct text_storage;

/* Configuration options not in config.h */
#define CVMVAS  1  /* arguments to page forward/back in pages      */

/* Global flags */
#define GFREAD  1  /* global read flag */

#define	CLRMSG	0  /* space clears the message line with no insert */
#define	CFENCE	1  /* fence matching in CMODE                      */
#define	TYPEAH	1  /* type ahead causes update to be skipped       */
#define DEBUGM	1  /* $debug triggers macro debugging              */
#define	NBRACE	1  /* new style brace matching command             */
#define	REVSTA	1  /* Status line appears in reverse video         */

/* Terminal capabilities from config.h are used */

#include "constants.h"

#define	NLOCKS	100		/* max # of file locks active   */
#define	NCOLORS	8		/* number of supported colors   */

// C23 compile-time validation of critical constants
static_assert(NBINDS > 0 && NBINDS <= 512, "NBINDS must be between 1 and 512");
static_assert(NFILEN >= 64, "NFILEN must be at least 64 bytes for modern paths");
static_assert(NSTRING >= 1024, "NSTRING must be at least 1KB for string operations");
static_assert((NCOLORS & (NCOLORS - 1)) == 0, "NCOLORS must be power of 2 for efficient lookup");

/* Legacy key encoding macros - REMOVED 2024-12
 * The old bit-flag system (CONTROL|META|CTLX|SPEC) has been replaced by
 * the modern input_key_event_t structure with separate code and modifiers.
 * See include/terminal/input_state.h for MOD_CTRL, MOD_META, etc.
 */

/* Boolean values */
#ifdef	false
#undef	false
#endif
#ifdef	true
#undef	true
#endif

// #define FALSE   0		/* False, no, bad, etc.         */  /* Removed: Using <stdbool.h> instead */
// #define TRUE    1		/* True, yes, good, etc.        */  /* Removed: Using <stdbool.h> instead */
#define ABORT   2		/* Death, ^G, abort, etc.       */
#define	FAILED	3		/* not-quite fatal false return */

/* Macro states - C23 enum for type safety */
enum macro_state {
	MACRO_STOP   = 0,	/* keyboard macro not in use    */
	MACRO_PLAY   = 1,	/*                playing       */
	MACRO_RECORD = 2	/*                recording     */
};

/* Directive definitions - C23 enum for type safety */
enum directive {
	DIR_IF        = 0,
	DIR_ELSE      = 1,
	DIR_ENDIF     = 2,
	DIR_GOTO      = 3,
	DIR_RETURN    = 4,
	DIR_ENDM      = 5,
	DIR_WHILE     = 6,
	DIR_ENDWHILE  = 7,
	DIR_BREAK     = 8,
	DIR_FORCE     = 9,
	DIR_COUNT     = 10	/* Number of directives */
};

/* Search position and direction - C23 enums for type safety */
enum search_position {
	POS_BEGIN = 0,		/* Leave the point at the beginning on search  */
	POS_END   = 1		/* Leave the point at the end on search         */
};

enum search_direction {
	DIR_FORWARD = 0,	/* forward direction            */
	DIR_REVERSE = 1		/* backwards direction          */
};

/* File I/O status codes - Standard enum */
enum FileStatus {
    FIOSUC  = 0,        /* File I/O, success.           */
    FIOFNF  = 1,        /* File I/O, file not found.    */
    FIOEOF  = 2,        /* File I/O, end of file.       */
    FIOERR  = 3,        /* File I/O, error.             */
    FIOMEM  = 4,        /* File I/O, out of memory      */
    FIOFUN  = 5         /* File I/O, eod of file/bad line */
};

// C23: Command flags with binary literals 
#define CFCPCN  0b0000000000000001U	/* Last command was C-P, C-N    */
#define CFKILL  0b0000000000000010U	/* Last command was a kill      */
#define CFYANK  0b0000000000000100U	/* Last command was a yank      */

// C23: Control characters with clearer representation
#define	BELL	0b00000111U		/* a bell character             */
#define	TAB	0b00001001U		/* a tab character              */

#define	PATHCHR	':'		/* PATH separator for Linux     */

#define	INTWIDTH	sizeof(int) * 3

/* Macro argument token types - C23 enum for type safety */
enum token_type {
	TOKEN_NUL  = 0,		/* end-of-string                */
	TOKEN_ARG  = 1,		/* interactive argument         */
	TOKEN_BUF  = 2,		/* buffer argument              */
	TOKEN_VAR  = 3,		/* user variables               */
	TOKEN_ENV  = 4,		/* environment variables        */
	TOKEN_FUN  = 5,		/* function....                 */
	TOKEN_DIR  = 6,		/* directive                    */
	TOKEN_LBL  = 7,		/* line label                   */
	TOKEN_LIT  = 8,		/* numeric literal              */
	TOKEN_STR  = 9,		/* quoted string literal        */
	TOKEN_CMD  = 10		/* command name                 */
};
/* Legacy aliases for backward compatibility during transition */
#define TKNUL TOKEN_NUL
#define TKARG TOKEN_ARG
#define TKBUF TOKEN_BUF
#define TKVAR TOKEN_VAR
#define TKENV TOKEN_ENV
#define TKFUN TOKEN_FUN
#define TKDIR TOKEN_DIR
#define TKLBL TOKEN_LBL
#define TKLIT TOKEN_LIT
#define TKSTR TOKEN_STR
#define TKCMD TOKEN_CMD

/*	Internal defined functions					*/

#define	nextab(a)	(a & ~tabmask) + (tabmask+1)
#ifdef	abs
#undef	abs
#endif

/* DIFCASE represents the integer difference between upper
   and lower case letters.  It is an xor-able value, which is
   fortunate, since the relative positions of upper to lower
   case letters is the opposite of ascii in ebcdic.
*/

#ifdef	islower
#undef	islower
#endif

/* PKCODE always enabled */
#ifdef	isupper
#undef	isupper
#endif

/*
 * Branchless character classification for μEmacs.
 * Handles ASCII + Latin-1 supplement (ISO-8859-1).
 *
 * Design goals (Linus-approved):
 *   - Zero branches: uses arithmetic range checks
 *   - Zero call overhead: always_inline + const
 *   - No ctype.h conflicts: clean namespacing
 *   - Single-cycle on modern CPUs
 *
 * Note: Latin-1 ranges 192-220/224-252 include × (215) and ÷ (247)
 * which are not letters. This matches original uemacs behavior.
 */

/* Branchless unsigned range check: true if lo <= x <= hi */
#define UEMACS_IN_RANGE(x, lo, hi) \
	((unsigned)((x) - (lo)) <= (unsigned)((hi) - (lo)))

/*
 * Character classification - branchless, always inlined.
 * [[gnu::const]] tells the compiler these are pure functions
 * with no side effects, enabling aggressive optimization.
 */

[[gnu::always_inline, gnu::const]]
static inline int is_lower(int c) {
	unsigned u = (unsigned char)c;
	return UEMACS_IN_RANGE(u, 'a', 'z');  /* ASCII only - Latin-1 conflicts with UTF-8 */
}

[[gnu::always_inline, gnu::const]]
static inline int is_upper(int c) {
	unsigned u = (unsigned char)c;
	return UEMACS_IN_RANGE(u, 'A', 'Z');  /* ASCII only - Latin-1 conflicts with UTF-8 */
}

[[gnu::always_inline, gnu::const]]
static inline int is_letter(int c) {
	unsigned u = (unsigned char)c;
	return UEMACS_IN_RANGE(u, 'a', 'z') | UEMACS_IN_RANGE(u, 'A', 'Z');  /* ASCII only */
}

/*
 * Case conversion - branchless XOR with 0x20.
 * ASCII only - Latin-1 ranges (192-220, 224-252) were removed because they
 * conflict with UTF-8 multi-byte sequences (lead bytes C0-FF). UTF-8 is the
 * standard encoding now; non-ASCII case conversion requires Unicode tables.
 */
#define DIFCASE 0x20

[[gnu::always_inline, gnu::const]]
static inline int to_lower(int c) {
	unsigned u = (unsigned char)c;
	int is_up = UEMACS_IN_RANGE(u, 'A', 'Z');  /* ASCII only */
	return c ^ (is_up * DIFCASE);
}

[[gnu::always_inline, gnu::const]]
static inline int to_upper(int c) {
	unsigned u = (unsigned char)c;
	int is_lo = UEMACS_IN_RANGE(u, 'a', 'z');  /* ASCII only */
	return c ^ (is_lo * DIFCASE);
}

/*
 * Case-insensitive character comparison - single branchless operation.
 * Folds both chars to lowercase before comparing.
 */
[[gnu::always_inline, gnu::const]]
static inline int eq_nocase(int c1, int c2) {
	return to_lower(c1) == to_lower(c2);
}

/*
 * There is a window structure allocated for every active display window. The
 * windows are kept in a big list, in top to bottom screen order, with the
 * listhead at "wheadp". Each window contains its own values of dot and mark.
 * The flag field contains some bits that are set by commands to guide
 * redisplay. Although this is a bit of a compromise in terms of decoupling,
 * the full blown redisplay is just too expensive to run for every input
 * character.
 */
struct window {
	struct window *w_wndp;	/* Next window                  */
	struct buffer *w_bufp;	/* Buffer displayed in window   */
	struct line *w_linep;	/* Top line in the window       */
	struct line *w_dotp;	/* Line containing "."          */
	struct line *w_markp;	/* Line containing "mark"       */
	int w_doto;		/* Byte offset for "."          */
	int w_marko;		/* Byte offset for "mark"       */
	int w_toprow;		/* Origin 0 top row of window   */
	int w_ntrows;		/* # of rows of text in window  */
	int w_wrap_col;		/* Soft wrap column (0 = disabled) */
	char w_force;		/* If NZ, forcing row.          */
	short w_flag;		/* Flags (expanded for WFTERM)  */

	// Atomic cursor position cache for instant status updates
	_Atomic int w_line_cache;	/* Cached line number for w_dotp */
	_Atomic bool w_line_cache_dirty; /* Line cache needs recalculation */
};

/* Window flags - Standard enum */
enum WindowFlags {
	WFFORCE = 0x01,		/* Window needs forced reframe  */
	WFMOVE  = 0x02,		/* Movement from line to line   */
	WFEDIT  = 0x04,		/* Editing within a line        */
	WFHARD  = 0x08,		/* Better to a full display     */
	WFMODE  = 0x10,		/* Update mode line.            */
	WFTERM  = 0x20		/* Terminal emulator window     */
};


/*
 * Text is kept in buffers. A buffer header, described below, exists for every
 * buffer in the system. The buffers are kept in a big list, so that commands
 * that search for a buffer by name can find the buffer header. There is a
 * safe store for the dot and mark in the header, but this is only valid if
 * the buffer is not being displayed (that is, if "b_nwnd" is 0). The text for
 * the buffer is kept in a circularly linked list of lines, with a pointer to
 * the header line in "b_linep".
 * 	Buffers may be "Inactive" which means the files associated with them
 * have not been read in yet. These get read in at "use buffer" time.
 */
/* 
 * C23 modernized buffer structure with improved memory layout and type safety
 */
struct buffer {
	struct buffer *b_bufp;	/* Link to next struct buffer   */
	struct line *b_dotp;	/* Link to "." struct line structure   */
	struct line *b_markp;	/* The same as the above two,   */
	struct line *b_linep;	/* Link to the header struct line      */
	int b_doto;		/* Offset of "." in above struct line  */
	int b_marko;		/* but for the "mark"           */
	uint32_t b_mode;	/* editor mode of this buffer (was int) */
	uint8_t b_active;	/* window activated flag (was char) */
	_Atomic uint8_t b_nwnd;		/* Count of windows on buffer - C23 atomic */
	_Atomic uint8_t b_flag;		/* Buffer flags - C23 atomic for RMW safety */
	uint8_t _reserved;	/* Padding for 32-bit alignment */
	
	// Cached status line statistics for instant updates
	_Atomic int b_line_count;	/* Total lines in buffer - cached */
	_Atomic long b_byte_count;	/* Total bytes in buffer - cached */
	_Atomic int b_word_count;	/* Total words in buffer - cached */
	_Atomic bool b_stats_dirty;	/* Statistics need recalculation */

	// Display dirty tracking (montauk/OUROBOROS pattern)
	_Atomic uint64_t b_dirty_seq;	/* Incremented on any modification */
	
	// O(1) Line Access Index (Phase 2)
	struct line **b_line_index;     /* Dynamic array of line pointers */
	size_t b_line_capacity;         /* Capacity of the index array */

	// Atomic undo/redo system (VSCode-inspired)
	struct atomic_undo_stack *b_undo_stack;	/* Edit history for this buffer */
	_Atomic uint64_t b_saved_version_id;   /* Version id of last saved/clean state */
	
	char b_fname[NFILEN];	/* File name                    */
	char b_bname[NBUFN];	/* Buffer name                  */
	/* Legacy b_key removed - use external GPG/age via encrypt.c */

	void *b_term_data;	/* Modern terminal state (terminal.h) */
	struct keymap *b_local_keymap;	/* Buffer-local keybindings (NULL = use global) */

	/* Syntax highlighting state (NULL = no highlighting) */
	struct buffer_syntax *b_syntax;
	int b_lang_id;	/* Language ID for syntax highlighting (-1 = unknown) */

	/* Large file support: buffer-level piece table storage
	 * When b_text is non-NULL, lines are "views" into this storage
	 * rather than owning their own gap buffers. This enables O(1)
	 * file loading via mmap for files >= 10MB.
	 */
	struct text_storage *b_text;	/* Buffer-level storage (NULL = per-line mode) */
};

/* Buffer flags - Standard enum */
enum BufferFlags {
	BFINVS  = 0x01,		/* Internal invisible buffer    */
	BFCHG   = 0x02,		/* Changed since last write     */
	BFTRUNC = 0x04		/* buffer was truncated when read */
};

/* Hash table for O(1) buffer lookup by name */
#define BUFFER_HASH_SIZE 256  /* Power of 2 for fast modulo */
struct buffer_hash_entry {
	struct buffer *buffer;         /* Pointer to buffer */
	struct buffer_hash_entry *next; /* Collision chain */
};

/* Global buffer hash table for instant buffer lookup */
extern struct buffer_hash_entry *buffer_hash_table[BUFFER_HASH_SIZE];

/*	mode flags	*/
#define	NUMMODES	11	/* # of defined modes           */

#define	MDWRAP	0x0001		/* word wrap                    */
#define	MDCMOD	0x0002		/* C indentation and fence match */
/* 0x0004 (SPELL) - reserved for modename[] array compatibility */
#define	MDEXACT	0x0008		/* Exact matching for searches  */
#define	MDVIEW	0x0010		/* read-only buffer             */
#define MDOVER	0x0020		/* overwrite mode               */
#define MDMAGIC	0x0040		/* regular expresions in search */
/* 0x0080 (CRYPT) - reserved for modename[] array compatibility */
/* 0x0100 (ASAVE) - reserved for modename[] array compatibility */
#define	MDTBUFFER 0x0200	/* terminal emulator buffer     */

/*
 * The starting position of a region, and the size of the region in
 * characters, is kept in a region structure.  Used by the region commands.
 */
struct region {
	struct line *r_linep;	/* Origin struct line address.         */
	int r_offset;		/* Origin struct line offset.          */
	long r_size;		/* Length in characters.        */
};

/*
 * The editor communicates with the display using a high level interface. A
 * "TERM" structure holds useful variables, and indirect pointers to routines
 * that do useful operations. The low level get and put routines are here too.
 * This lets a terminal, in addition to having non standard commands, have
 * funny get and put character code too. The calls might get changed to
 * "termp->t_field" style in the future, to make it possible to run more than
 * one terminal type.
 */
struct terminal {
	_Atomic short t_mrow;		/* max number of rows - C23 atomic for signal safety */
	_Atomic short t_nrow;		/* current rows used - C23 atomic for signal safety */
	_Atomic short t_mcol;		/* max columns - C23 atomic for signal safety */
	_Atomic short t_ncol;		/* current columns - C23 atomic for signal safety */
	short t_margin;		/* min margin for extended lines */
	short t_scrsiz;		/* size of scroll region "      */
	int t_pause;		/* # times thru update to pause */
	void (*t_open)(void);	/* Open terminal at the start.  */
	void (*t_close)(void);	/* Close terminal at end.       */
	void (*t_kopen)(void);	/* Open keyboard                */
	void (*t_kclose)(void);	/* close keyboard               */
	int (*t_getchar)(void);	/* Get character from keyboard. */
	int (*t_putchar)(int);	/* Put character to display.    */
	void (*t_flush) (void);	/* Flush output buffers.        */
	void (*t_move)(int, int);/* Move the cursor, origin 0.   */
	void (*t_eeol)(void);	/* Erase to end of line.        */
	void (*t_eeop)(void);	/* Erase to end of page.        */
	void (*t_beep)(void);	/* Beep.                        */
	void (*t_rev)(int);	/* set reverse video state      */
	int (*t_rez)(const char *);	/* change screen resolution     */
	void (*t_scroll)(int, int,int);	/* scroll a region of the screen */
};

/*	TEMPORARY macros for terminal I/O  (to be placed in a machine
					    dependant place later)	*/

// LEGACY TERMINAL MACROS REPLACED - See include/μemacs/terminal_ops.h
// 18 unsafe function pointer macros eliminated in Phase 2
#include "../μemacs/terminal_ops.h"
#include "c23_compat.h"

/* Structure for the table of initial key bindings. */
struct key_tab {
	int k_code;		 /* Key code */
	int (*k_fp)(int, int);	 /* Routine to handle it */
};

/* Structure for the name binding table. */
struct name_bind {
	char *n_name;		 /* name of function key */
	int (*n_func)(int, int); /* function name is bound to */
};


/* C23 Kill Ring Implementation
 * Modern atomic circular buffer for kill/yank operations.
 * Provides O(1) performance for all operations and thread safety.
 */
#define KILL_RING_MAX 32        /* Max entries in kill ring (power of 2) */
#define KILL_ENTRY_MAX 8192     /* Max bytes per kill ring entry */

static_assert((KILL_RING_MAX & (KILL_RING_MAX - 1)) == 0, 
              "KILL_RING_MAX must be power of 2 for efficient wraparound");
static_assert(KILL_ENTRY_MAX >= 250, "KILL_ENTRY_MAX must be >= 250 for kill buffer compatibility");

struct kill_ring_entry {
	_Atomic size_t length;              /* Length of text in entry */
	_Atomic bool valid;                 /* Entry contains valid data */
	ALIGN_TO(64) char text[KILL_ENTRY_MAX];  /* Cache-aligned text storage */
};

struct kill_ring {
	_Atomic size_t head;                /* Next slot to write (producer) */
	_Atomic size_t tail;                /* Oldest valid entry (for GC) */
	_Atomic size_t yank_index;          /* Current yank position for yankpop */
	_Atomic size_t count;               /* Number of valid entries */
	ALIGN_TO(64) struct kill_ring_entry entries[KILL_RING_MAX];
};

/* When emacs' command interpetor needs to get a variable's name,
 * rather than it's value, it is passed back as a variable description
 * structure. The v_num field is a index into the appropriate variable table.
 */
struct variable_description {
	int v_type;  /* Type of variable. */
	int v_num;   /* Ordinal pointer to variable in list. */
};

/* The !WHILE directive in the execution language needs to
 * stack references to pending whiles. These are stored linked
 * to each currently open procedure via a linked list of
 * the following structure.
*/
struct while_block {
	struct line *w_begin;        /* ptr to !while statement */
	struct line *w_end;          /* ptr to the !endwhile statement */
	int w_type;		     /* block type */
	struct while_block *w_next;  /* next while */
};

/* While block types - C23 enum for type safety */
enum block_type {
	BLOCK_WHILE = 1,
	BLOCK_BREAK = 2
};
/* Legacy aliases */
#define BTWHILE BLOCK_WHILE
#define BTBREAK BLOCK_BREAK

/*
 * Incremental search buffer size defined in constants.h (CMDBUFLEN)
 * Key handling now uses input_key_event_t with evt_is_* helpers
 */

/* Regex metacharacter types - C23 enum for type safety */
enum metachar_type {
	MC_NIL     = 0,		/* Like the '\0' for strings    */
	MC_LITCHAR = 1,		/* Literal character, or string */
	MC_ANYCHAR = 2,		/* Any character                */
	MC_CHARCLASS = 3,	/* Character class              */
	MC_NEGCLASS  = 4,	/* Negated character class      */
	MC_BEGLINE   = 5,	/* Beginning of line            */
	MC_ENDLINE   = 6,	/* End of line                  */
	MC_DITTO_REP = 7	/* Replacement ditto            */
};
/* Legacy aliases */
#define MCNIL   MC_NIL
#define LITCHAR MC_LITCHAR
#define ANY     MC_ANYCHAR
#define CCL     MC_CHARCLASS
#define NCCL    MC_NEGCLASS
#define BOL     MC_BEGLINE
#define EOL     MC_ENDLINE
#define DITTO   MC_DITTO_REP
/* CLOSURE and MASKCL are in constants.h */

#define	MC_ANY		'.'	/* 'Any' character (except newline). */
#define	MC_CCL		'['	/* Character class. */
#define	MC_NCCL		'^'	/* Negate character class. */
#define	MC_RCCL		'-'	/* Range in character class. */
#define	MC_ECCL		']'	/* End of character class. */
#define	MC_BOL		'^'	/* Beginning of line. */
#define	MC_EOL		'$'	/* End of line. */
#define	MC_CLOSURE	'*'	/* Closure - does not extend past newline. */
#define	MC_DITTO	'&'	/* Use matched string in replacement. */
#define	MC_ESC		'\\'	/* Escape - suppress meta-meaning. */

/* LEGACY macros removed. Use BIT_SET/BIT_TEST and SAFE_CHCASE instead. */

/* HICHAR and HIBYTE are in constants.h */

/* Typedefs that define the meta-character structure for MAGIC mode searching
 * (struct magic), and the meta-character structure for MAGIC mode replacement
 * (struct magic_replacement).
 */
struct magic {
	short int mc_type;
	union {
		int lchar;
		char *cclmap;
	} u;
};

struct magic_replacement {
	short int mc_type;
	char *rstr;
};

#endif  /* ESTRUCT_H */

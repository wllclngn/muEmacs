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

/* Forward declarations */
struct edit_stack;

/* Configuration options not in config.h */
#define CVMVAS  1  /* arguments to page forward/back in pages      */

/* Global flags */
#define GFREAD  1  /* global read flag */

#define	CLRMSG	0  /* space clears the message line with no insert */
#define	CFENCE	1  /* fence matching in CMODE                      */
#define	TYPEAH	1  /* type ahead causes update to be skipped       */
#define DEBUGM	1  /* $debug triggers macro debugging              */
#define	VISMAC	0  /* update display during keyboard macros        */
// #define	CTRLZ	0  // Disabled - no MSDOS support
#define ADDCR	0  /* ajout d'un CR en fin de chaque ligne (ST520) */
#define	NBRACE	1  /* new style brace matching command             */
#define	REVSTA	1  /* Status line appears in reverse video         */

/* Memory tracking */
#define	RAMSIZE	0		/* dynamic RAM memory usage tracking */
#define	RAMSHOW	0		/* auto dynamic RAM reporting */

/* Terminal capabilities from config.h are used */

/* Internal constants with C23 compile-time validation */
#define	NBINDS	256		/* max # of bound keys          */
#define NFILEN  256		/* # of bytes, file name        */
#define NBUFN   16		/* # of bytes, buffer name      */
#define NLINE   256		/* # of bytes, input line       */
#define	NSTRING	8192		/* # of bytes, string buffers   */
#define NKBDM   256		/* # of strokes, keyboard macro */
#define NPAT    128		/* # of bytes, pattern          */
#define HUGE    1000		/* Huge number                  */
#define	NLOCKS	100		/* max # of file locks active   */
#define	NCOLORS	8		/* number of supported colors   */

// C23 compile-time validation of critical constants
static_assert(NBINDS > 0 && NBINDS <= 512, "NBINDS must be between 1 and 512");
static_assert(NFILEN >= 64, "NFILEN must be at least 64 bytes for modern paths");
static_assert(NSTRING >= 1024, "NSTRING must be at least 1KB for string operations");
static_assert((NCOLORS & (NCOLORS - 1)) == 0, "NCOLORS must be power of 2 for efficient lookup");

/* Portable key flag bitmasks (avoid C23-only binary literals) */
#define CONTROL (1u << 28)    /* Control flag, or'ed in       */
#define META    (1u << 29)    /* Meta flag, or'ed in          */
#define CTLX    (1u << 30)    /* ^X flag, or'ed in            */
#define SPEC    (1u << 31)    /* special key (function keys)  */

// C23 compile-time validation of key flag isolation
static_assert((CONTROL & META) == 0, "CONTROL and META flags must not overlap");
static_assert((CONTROL & CTLX) == 0, "CONTROL and CTLX flags must not overlap");
static_assert((CONTROL & SPEC) == 0, "CONTROL and SPEC flags must not overlap");
static_assert((META & CTLX) == 0, "META and CTLX flags must not overlap");
static_assert((META & SPEC) == 0, "META and SPEC flags must not overlap");
static_assert((CTLX & SPEC) == 0, "CTLX and SPEC flags must not overlap");

/* Boolean values */
#ifdef	FALSE
#undef	FALSE
#endif
#ifdef	TRUE
#undef	TRUE
#endif

#define FALSE   0		/* False, no, bad, etc.         */
#define TRUE    1		/* True, yes, good, etc.        */
#define ABORT   2		/* Death, ^G, abort, etc.       */
#define	FAILED	3		/* not-quite fatal false return */

/* Macro states */
#define	STOP	0		/* keyboard macro not in use    */
#define	PLAY	1		/*                playing       */
#define	RECORD	2		/*                recording     */

/*	Directive definitions	*/
#define	DIF		0
#define DELSE		1
#define DENDIF		2
#define DGOTO		3
#define DRETURN		4
#define DENDM		5
#define DWHILE		6
#define	DENDWHILE	7
#define	DBREAK		8
#define DFORCE		9

#define NUMDIRS		10

/*
 * PTBEG, PTEND, FORWARD, and REVERSE are all toggle-able values for
 * the scan routines.
 */
#define	PTBEG	0		/* Leave the point at the beginning on search   */
#define	PTEND	1		/* Leave the point at the end on search         */
#define	FORWARD	0		/* forward direction            */
#define REVERSE	1		/* backwards direction          */

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

/*	Macro argument token types					*/

#define	TKNUL	0		/* end-of-string                */
#define	TKARG	1		/* interactive argument         */
#define	TKBUF	2		/* buffer argument              */
#define	TKVAR	3		/* user variables               */
#define	TKENV	4		/* environment variables        */
#define	TKFUN	5		/* function....                 */
#define	TKDIR	6		/* directive                    */
#define	TKLBL	7		/* line label                   */
#define	TKLIT	8		/* numeric literal              */
#define	TKSTR	9		/* quoted string literal        */
#define	TKCMD	10		/* command name                 */

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

#if	PKCODE
#ifdef	isupper
#undef	isupper
#endif
#endif

#define	DIFCASE		0x20

#define LASTUL 'Z'
#define LASTLL 'z'

#define isletter(c)	isxletter((0xFF & (c)))
#define islower(c)	isxlower((0xFF & (c)))
#define isupper(c)	isxupper((0xFF & (c)))

#define isxletter(c)	(('a' <= c && LASTLL >= c) || ('A' <= c && LASTUL >= c) || (192<=c && c<=255))
#define isxlower(c)	(('a' <= c && LASTLL >= c) || (224 <= c && 252 >= c))
#define isxupper(c)	(('A' <= c && LASTUL >= c) || (192 <= c && 220 >= c))

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
	char w_force;		/* If NZ, forcing row.          */
	char w_flag;		/* Flags.                       */
#if	COLOR
	char w_fcolor;		/* current forground color      */
	char w_bcolor;		/* current background color     */
#endif
	
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
	WFCOLR  = 0x20,		/* Needs a color change         */
#if SCROLLCODE
	WFKILLS = 0x40,		/* something was deleted        */
	WFINS   = 0x80		/* something was inserted       */
#endif
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
	uint8_t b_nwnd;		/* Count of windows on buffer (was char) */
	uint8_t b_flag;		/* Buffer flags - see BufferFlags enum */
	uint8_t _reserved;	/* Padding for 32-bit alignment */
	
	// Cached status line statistics for instant updates
	_Atomic int b_line_count;	/* Total lines in buffer - cached */
	_Atomic long b_byte_count;	/* Total bytes in buffer - cached */
	_Atomic int b_word_count;	/* Total words in buffer - cached */
	_Atomic bool b_stats_dirty;	/* Statistics need recalculation */
	
	// Atomic undo/redo system (VSCode-inspired)
	struct atomic_undo_stack *b_undo_stack;	/* Edit history for this buffer */
	_Atomic uint64_t b_saved_version_id;   /* Version id of last saved/clean state */
	
	char b_fname[NFILEN];	/* File name                    */
	char b_bname[NBUFN];	/* Buffer name                  */
#if	CRYPT
	char b_key[NPAT];	/* current encrypted key        */
#endif
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
#define	NUMMODES	10	/* # of defined modes           */

#define	MDWRAP	0x0001		/* word wrap                    */
#define	MDCMOD	0x0002		/* C indentation and fence match */
#define	MDSPELL	0x0004		/* spell error parcing          */
#define	MDEXACT	0x0008		/* Exact matching for searches  */
#define	MDVIEW	0x0010		/* read-only buffer             */
#define MDOVER	0x0020		/* overwrite mode               */
#define MDMAGIC	0x0040		/* regular expresions in search */
#define	MDCRYPT	0x0080		/* encrytion mode active        */
#define	MDASAVE	0x0100		/* auto-save mode               */

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
	short t_mrow;		/* max number of rows allowable */
	short t_nrow;		/* current number of rows used  */
	short t_mcol;		/* max Number of columns.       */
	short t_ncol;		/* current Number of columns.   */
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
#if	COLOR
	int (*t_setfor) (int);	/* set forground color          */
	int (*t_setback) (int);	/* set background color         */
#endif
#if     SCROLLCODE
	void (*t_scroll)(int, int,int);	/* scroll a region of the screen */
#endif
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

#define	BTWHILE		1
#define	BTBREAK		2

/*
 * Incremental search defines.
 */
#if	ISRCH

#define	CMDBUFLEN	256	/* Length of our command buffer */

#define	IS_ABORT	0x07	/* Abort the isearch */
#define IS_BACKSP	0x08	/* Delete previous char */
#define	IS_TAB		0x09	/* Tab character (allowed search char) */
#define IS_NEWLINE	0x0D	/* New line from keyboard (Carriage return) */
#define	IS_QUOTE	0x11	/* Quote next character */
#define IS_REVERSE	0x12	/* Search backward */
#define	IS_FORWARD	0x13	/* Search forward */
#define	IS_QUIT		0x1B	/* Exit the search */
#define	IS_RUBOUT	0x7F	/* Delete previous character */

/* IS_QUIT is no longer used, the variable metac is used instead */

#endif

#if defined(MAGIC)
/*
 * Defines for the metacharacters in the regular expression
 * search routines.
 */
#define	MCNIL		0	/* Like the '\0' for strings. */
#define	LITCHAR		1	/* Literal character, or string. */
#define	ANY		2
#define	CCL		3
#define	NCCL		4
#define	BOL		5
#define	EOL		6
#define	DITTO		7
#define	CLOSURE		256	/* An or-able value. */
#define	MASKCL		(CLOSURE - 1)

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

/* HICHAR - 1 is the largest character we will deal with.
 * HIBYTE represents the number of bytes in the bitmap.
 */
#define	HICHAR		256
#define	HIBYTE		(HICHAR >> 3)

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

#endif  /* MAGIC */

#endif  /* ESTRUCT_H */

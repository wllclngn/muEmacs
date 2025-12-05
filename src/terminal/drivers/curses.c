/* curses.c - ncurses driver for μEmacs
 *
 * Replaces legacy termcap/posix drivers with modern ncurses.
 * 
 * C23 Compliant.
 */

#define termdef 1

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <curses.h>
#include <term.h>
#include <signal.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "terminal_ops.h"

#define MARGIN 8
#define SCRSIZ 64
#define NPAUSE 10

static int curr_fg = COLOR_WHITE;
static int curr_bg = COLOR_BLACK;

/* Forward declarations */
static void curses_open(void);
static void curses_close(void);
static void curses_kopen(void);
static void curses_kclose(void);
static int curses_getchar(void);
static int curses_putchar(int c);
static void curses_flush(void);
static void curses_move(int row, int col);
static void curses_eeol(void);
static void curses_eeop(void);
static void curses_beep(void);
static void curses_rev(int state);
static int curses_rez(const char *res);

#if COLOR
static int curses_fcol(int color);
static int curses_bcol(int color);
static void apply_colors(void);
#endif

/* The terminal structure definition */
struct terminal ue_term = {
    0, 0, 0, 0,         /* size vars - set in open() */
    MARGIN,
    SCRSIZ,
    NPAUSE,
    curses_open,
    curses_close,
    curses_kopen,
    curses_kclose,
    curses_getchar,
    curses_putchar,
    curses_flush,
    curses_move,
    curses_eeol,
    curses_eeop,
    curses_beep,
    curses_rev,
    curses_rez
#if COLOR
    , curses_fcol,
    curses_bcol
#endif
#if SCROLLCODE
    , nullptr /* scrolling not implemented yet */
#endif
};

static void curses_open(void)
{
    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    nonl(); /* Detect return key */
    intrflush(stdscr, false);
    keypad(stdscr, true);
    
    /* Capabilities */
    revexist = true;
    eolexist = true;
    
    /* Set terminal size in struct */
    ue_term.t_mrow = LINES;
    ue_term.t_nrow = LINES - 1;
    ue_term.t_mcol = COLS;
    ue_term.t_ncol = COLS;

#if COLOR
    if (has_colors()) {
        start_color();
        use_default_colors();
        
        /* Map standard colors if needed, or assume standard ANSI mapping */
        /* 0:black, 1:red, 2:green, 3:yellow, 4:blue, 5:magenta, 6:cyan, 7:white */
    }
#endif
}

static void curses_close(void)
{
    endwin();
}

static void curses_kopen(void)
{
    /* handled in open */
}

static void curses_kclose(void)
{
    /* handled in close */
}

static int curses_getchar(void)
{
    int c = getch();
    
    /* Handle resize event */
    if (c == KEY_RESIZE) {
        ue_term.t_mrow = LINES;
        ue_term.t_nrow = LINES - 1;
        ue_term.t_mcol = COLS;
        ue_term.t_ncol = COLS;
        sgarbf = true; /* Force redraw */
        return 0; /* Ignore this keypress to core */
    }
    
    /* Map ncurses keys to μEmacs internal codes */
    switch (c) {
        case KEY_UP:     return SPEC | 'A';
        case KEY_DOWN:   return SPEC | 'B';
        case KEY_RIGHT:  return SPEC | 'C';
        case KEY_LEFT:   return SPEC | 'D';
        case KEY_HOME:   return CONTROL | 'A'; /* or META | '<' for buffer start? */
        case KEY_END:    return CONTROL | 'E';
        case KEY_PPAGE:  return META | 'V';
        case KEY_NPAGE:  return CONTROL | 'V';
        case KEY_DC:     return CONTROL | 'D';
        case KEY_BACKSPACE: return 0x7F; /* DEL */
        case 127:        return 0x7F; /* DEL */
        /* F-keys can be mapped here too */
    }
    
    return c;
}

static int curses_putchar(int c)
{
    addch(c);
    return c;
}

static void curses_flush(void)
{
    refresh();
}

static void curses_move(int row, int col)
{
    move(row, col);
}

static void curses_eeol(void)
{
    clrtoeol();
}

static void curses_eeop(void)
{
    /* Clear to end of page (screen) */
    clrtobot();
}

static void curses_beep(void)
{
    beep();
}

static void curses_rev(int state)
{
    if (state)
        attron(A_REVERSE);
    else
        attroff(A_REVERSE);
}

static int curses_rez(const char *res)
{
    (void)res;
    return true;
}

#if COLOR

/* Map μEmacs color codes to ncurses colors 
   μEmacs uses:
   0=black, 1=red, 2=green, 3=yellow, 4=blue, 5=magenta, 6=cyan, 7=white
   ncurses:
   COLOR_BLACK, COLOR_RED, ... matches this order usually.
*/

static void apply_colors(void)
{
    /* Simple pairing strategy:
       Pair index = (bg * 8) + fg + 1
       This creates pairs 1..64
    */
    int pair = (curr_bg * 8) + curr_fg + 1;
    
    if (pair > 64) pair = 1; /* Safety */
    
    init_pair(pair, curr_fg, curr_bg);
    attron(COLOR_PAIR(pair));
}

static int curses_fcol(int color)
{
    curr_fg = color & 0x07; /* Mask to 0-7 */
    apply_colors();
    return true;
}

static int curses_bcol(int color)
{
    curr_bg = color & 0x07;
    apply_colors();
    return true;
}
#endif

/* Check for pending input */
int typahead(void)
{
    int c;
    nodelay(stdscr, true);
    c = getch();
    nodelay(stdscr, false);
    if (c != ERR) {
        ungetch(c);
        return true;
    }
    return false;
}
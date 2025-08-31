/*
 * μEmacs - C23 type-safe terminal operations
 * Modern replacement for 18 terminal macros
 */

#ifndef UEMACS_TERMINAL_OPS_H
#define UEMACS_TERMINAL_OPS_H

#include <stdbool.h>
#include <stdint.h>
#include "core.h"

// Forward declarations for error checking
struct terminal;
extern struct terminal term;

// C23 type-safe inline terminal operations - replaces dangerous macros
static inline void tt_open(void)
{
    if (term.t_open) term.t_open();
}

static inline void tt_close(void) 
{
    if (term.t_close) term.t_close();
}

static inline void tt_kopen(void)
{
    if (term.t_kopen) term.t_kopen();
}

static inline void tt_kclose(void)
{
    if (term.t_kclose) term.t_kclose();
}

static inline int tt_getc(void)
{
    return term.t_getchar ? term.t_getchar() : -1;
}

static inline int tt_putc(int c)
{
    return term.t_putchar ? term.t_putchar(c) : -1;
}

static inline void tt_flush(void)
{
    if (term.t_flush) term.t_flush();
}

static inline void tt_move(int row, int col)
{
    if (term.t_move) term.t_move(row, col);
}

static inline void tt_eeol(void)
{
    if (term.t_eeol) term.t_eeol();
}

static inline void tt_eeop(void)
{
    if (term.t_eeop) term.t_eeop();
}

static inline void tt_beep(void)
{
    if (term.t_beep) term.t_beep();
}

static inline void tt_rev(int state)
{
    if (term.t_rev) term.t_rev(state);
}

static inline int tt_rez(const char* resolution)
{
    return term.t_rez ? term.t_rez(resolution) : TRUE;
}

#if COLOR
static inline void tt_setfor(int color)
{
    if (term.t_setfor) term.t_setfor(color);
}

static inline void tt_setback(int color)
{
    if (term.t_setback) term.t_setback(color);
}
#endif

// Legacy macro compatibility - can be removed in Phase 6
#define TTopen       tt_open
#define TTclose      tt_close  
#define TTkopen      tt_kopen
#define TTkclose     tt_kclose
#define TTgetc       tt_getc
#define TTputc       tt_putc
#define TTflush      tt_flush
#define TTmove       tt_move
#define TTeeol       tt_eeol
#define TTeeop       tt_eeop
#define TTbeep       tt_beep
#define TTrev        tt_rev
#define TTrez        tt_rez
#if COLOR
#define TTforg       tt_setfor
#define TTbacg       tt_setback
#endif

#endif // UEMACS_TERMINAL_OPS_H
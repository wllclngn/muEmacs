/*
 * message_line.c - Message Line Output for μEmacs
 *
 * Provides printf-like output to the message line (bottom row).
 * The message line is not part of the virtual screen and writes
 * directly to the terminal.
 *
 * C23 compliant
 */

#include <stdarg.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

/*
 * Write out an integer, in the specified radix. Update the physical cursor
 * position.
 */
static void mlputi(int i, int r)
{
    int q;
    static char hexdigits[] = "0123456789ABCDEF";

    if (i < 0) {
        i = -i;
        TTputc('-');
    }

    q = i / r;

    if (q != 0)
        mlputi(q, r);

    TTputc(hexdigits[i % r]);
    ++ttcol;
}

/*
 * do the same except as a long integer.
 */
static void mlputli(long l, int r)
{
    long q;

    if (l < 0) {
        l = -l;
        TTputc('-');
    }

    q = l / r;

    if (q != 0)
        mlputli(q, r);

    TTputc((int) (l % r) + '0');
    ++ttcol;
}

/*
 * write out a scaled integer with two decimal places
 *
 * int s;		scaled integer to output
 */
static void mlputf(int s)
{
    int i;			/* integer portion of number */
    int f;			/* fractional portion of number */

    /* break it up */
    i = s / 100;
    f = s % 100;

    /* send out the integer portion */
    mlputi(i, 10);
    TTputc('.');
    TTputc((f / 10) + '0');
    TTputc((f % 10) + '0');
    ttcol += 3;
}

/*
 * Erase the message line. This is a special routine because the message line
 * is not considered to be part of the virtual screen. It always works
 * immediately; the terminal buffer is flushed via a call to the flusher.
 */
void mlerase(void)
{
    movecursor(term.t_nrow, 0);
    if (discmd == false)
        return;

    ttputs("\033[0m");  /* Reset to terminal defaults */
    TTeeol();  /* Modern terminals always support clear-to-EOL */
    TTflush();
    mpresf = false;
}

/*
 * Write a message into the message line. Keep track of the physical cursor
 * position. A small class of printf like format items is handled. Assumes the
 * stack grows down; this assumption is made by the "++" in the argument scan
 * loop. Set the "message line" flag true.
 *
 * char *fmt;		format string for output
 * char *arg;		pointer to first argument to print
 */
void mlwrite(const char *restrict fmt, ...)
{
    int c;		/* current char in format string */
    va_list ap;

    /* if we are not currently echoing on the command line, abort this */
    if (discmd == false) {
        movecursor(term.t_nrow, 0);
        return;
    }

    movecursor(term.t_nrow, 0);
    ttputs("\033[0m");  /* Reset to terminal defaults */
    va_start(ap, fmt);
    while ((c = *fmt++) != 0) {
        if (c != '%') {
            TTputc(c);
            ++ttcol;
        } else {
            c = *fmt++;
            switch (c) {
            case 'd':
                mlputi(va_arg(ap, int), 10);
                break;

            case 'o':
                mlputi(va_arg(ap, int), 8);
                break;

            case 'x':
                mlputi(va_arg(ap, int), 16);
                break;

            case 'D':
                mlputli(va_arg(ap, long), 10);
                break;

            case 's':
                mlputs(va_arg(ap, char *));
                break;

            case 'f':
                mlputf(va_arg(ap, int));
                break;

            default:
                TTputc(c);
                ++ttcol;
            }
        }
    }
    va_end(ap);

    TTeeol();  /* Clear to end of line */
    TTflush();
    mpresf = true;
}

/*
 * Force a string out to the message line regardless of the
 * current $discmd setting. This is needed when $debug is true
 * and for the write-message and clear-message-line commands
 *
 * char *s;		string to force out
 */
void mlforce(const char *restrict s)
{
    int oldcmd;	/* original command display flag */

    oldcmd = discmd;	/* save the discmd value */
    discmd = true;		/* and turn display on */
    mlwrite(s);		/* write the string out */
    discmd = oldcmd;	/* and restore the original setting */
}

/*
 * Write out a string. Update the physical cursor position. This assumes that
 * the characters in the string all have width "1"; if this is not the case
 * things will get screwed up a little.
 */
void mlputs(const char *restrict s)
{
    int c;

    while ((c = *s++) != 0) {
        TTputc(c);
        ++ttcol;
    }
}

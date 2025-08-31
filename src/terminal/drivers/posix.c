/*	posix.c
 *
 *      The functions in this file negotiate with the operating system for
 *      characters, and write characters in a barely buffered fashion on the
 *      display. All operating systems.
 *
 *	modified by Petri Kutvonen
 *
 *	based on termio.c, with all the old cruft removed, and
 *	fixed for termios rather than the old termio.. Linus Torvalds
 */

#ifdef POSIX

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <sys/select.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/select.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "utf8.h"


static int kbdflgs;			/* saved keyboard fd flags      */
static int kbdpoll;			/* in O_NDELAY mode             */

static struct termios otermios;		/* original terminal characteristics */
static struct termios ntermios;		/* charactoristics to use inside */

#define TBUFSIZ 128
static char tobuf[TBUFSIZ];		/* terminal output buffer */

/*
 * Signal-masked UTF-8 input with race condition prevention
 * Simple approach focusing on the core issue
 */
static pthread_mutex_t input_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * This function is called once to set up the terminal device streams.
 * On VMS, it translates TT until it finds the terminal, then assigns
 * a channel to it and sets it raw. On CPM it is a no-op.
 */
void ttopen(void)
{
	tcgetattr(0, &otermios);	/* save old settings */

	/*
	 * base new settings on old ones - don't change things
	 * we don't know about
	 */
	ntermios = otermios;

	/* Optimized input handling for UTF-8 and rapid typing */
	ntermios.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK
			      | INPCK | INLCR | IGNCR | ICRNL | ISTRIP);
	/* Enable UTF-8 input processing */
	ntermios.c_iflag |= IGNPAR; /* Ignore parity errors for UTF-8 */

	/* raw CR/NR etc output handling */
	ntermios.c_oflag &=
	    ~(OPOST | ONLCR | OLCUC | OCRNL | ONOCR | ONLRET);

	/* No signal handling, no echo etc */
	ntermios.c_lflag &= ~(ISIG | ICANON | XCASE | ECHO | ECHOE | ECHOK
			      | ECHONL | NOFLSH | TOSTOP | ECHOCTL |
			      ECHOPRT | ECHOKE | FLUSHO | PENDIN | IEXTEN);

	/* Optimized for rapid input with minimal latency */
	ntermios.c_cc[VMIN] = 1;  /* At least 1 character */
	ntermios.c_cc[VTIME] = 0; /* No timeout - immediate response */
	tcsetattr(0, TCSADRAIN, &ntermios);	/* and activate them */

	/*
	 * provide a smaller terminal output buffer so that
	 * the type ahead detection works better (more often)
	 */
	setbuffer(stdout, &tobuf[0], TBUFSIZ);

	kbdflgs = fcntl(0, F_GETFL, 0);
	kbdpoll = FALSE;


	/* on all screens we are not sure of the initial position
	   of the cursor                                        */
	ttrow = 999;
	ttcol = 999;
}

/*
 * This function gets called just before we go back home to the command
 * interpreter. On VMS it puts the terminal back in a reasonable state.
 * Another no-operation on CPM.
 */
void ttclose(void)
{
	tcsetattr(0, TCSADRAIN, &otermios);	/* restore terminal settings */
}

/*
 * Write a character to the display. On VMS, terminal output is buffered, and
 * we just put the characters in the big array, after checking for overflow.
 * On CPM terminal I/O unbuffered, so we just write the byte out. Ditto on
 * MS-DOS (use the very very raw console output routine).
 */
int ttputc(int c)
{
	char utf8[8];
	int bytes;
	
	// Validate Unicode value to prevent BOM insertion
	if (c < 0 || c > 0x10FFFF || (c >= 0xFEFF && c <= 0xFFFF)) {
		// Invalid or BOM range - output as single byte
		putchar(c & 0xFF);
		return 0;
	}
	
	bytes = unicode_to_utf8(c, utf8);
	fwrite(utf8, 1, bytes, stdout);
	return 0;
}

/*
 * Flush terminal buffer. Does real work where the terminal output is buffered
 * up. A no-operation on systems where byte at a time terminal I/O is done.
 */
void ttflush(void)
{
/*
 * Add some terminal output success checking, sometimes an orphaned
 * process may be left looping on SunOS 4.1.
 *
 * How to recover here, or is it best just to exit and lose
 * everything?
 *
 * jph, 8-Oct-1993
 * Jani Jaakkola suggested using select after EAGAIN but let's just wait a bit
 *
 */
	int status;
	
    // Block only SIGWINCH during flush to avoid resize tearing
    sigset_t oldmask, mask;
    sigemptyset(&mask);
#ifdef SIGWINCH
    sigaddset(&mask, SIGWINCH);
#endif
    pthread_sigmask(SIG_BLOCK, &mask, &oldmask);

	status = fflush(stdout);
	while (status < 0 && errno == EAGAIN) {
		usleep(10000);  // 10ms instead of 1000ms - 100x faster
		status = fflush(stdout);
	}
	
	pthread_sigmask(SIG_SETMASK, &oldmask, NULL);
	
	if (status < 0)
		exit(15);
}



int ttgetc(void) {
    unsigned char byte;
    sigset_t oldmask, mask;

    // Block only SIGWINCH during read to avoid resize races
    sigemptyset(&mask);
#ifdef SIGWINCH
    sigaddset(&mask, SIGWINCH);
#endif
    pthread_sigmask(SIG_BLOCK, &mask, &oldmask);
    pthread_mutex_lock(&input_mutex);

    int n = read(0, &byte, 1);

    pthread_mutex_unlock(&input_mutex);
    pthread_sigmask(SIG_SETMASK, &oldmask, NULL);

    if (n <= 0) {
        // Return EOF indicator (Ctrl-D traditionally) or error
        return (n == 0) ? 0x04 : -1;
    }

    return (int)byte;
}

/* typahead:	Check to see if any characters are already in the
		keyboard buffer
*/

int typahead(void)
{
	int x = 0;
	
#ifdef FIONREAD
	if (ioctl(0, FIONREAD, &x) < 0)
		x = 0;
#endif
	
	return x;
}

#endif				/* POSIX */

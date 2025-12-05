/* posix.c
 *
 * Linux terminal I/O using termios. Handles keyboard input and minimally
 * buffered screen output. Derived from the classic termio path, modernized
 * for termios and UTF‑8.
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
#include "terminal_capability.h"
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

/* Initialize terminal device streams and enter raw mode. */
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
	kbdpoll = false;

	/* Setup terminal capabilities and optimizations */
	terminal_caps_t caps = detect_terminal_capabilities();
	optimize_for_terminal(&caps);

	/* on all screens we are not sure of the initial position
	   of the cursor                                        */
	ttrow = 999;
	ttcol = 999;
}

/* Restore terminal to the original settings. */
void ttclose(void)
{
	cleanup_terminal_optimizations();	/* restore terminal capabilities */
	tcsetattr(0, TCSADRAIN, &otermios);	/* restore terminal settings */
}

/* Write a character (UTF‑8 aware) to the display. */
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

/* Flush terminal buffer. */
void ttflush(void)
{
/*
 * Add simple terminal output success checking to avoid tight loops.
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
	
	pthread_sigmask(SIG_SETMASK, &oldmask, nullptr);
	
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
    pthread_sigmask(SIG_SETMASK, &oldmask, nullptr);

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

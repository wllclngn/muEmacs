/* config.h.in - Configuration header for modern Linux μEmacs */

#ifndef ΜEMACS_CONFIG_H
#define ΜEMACS_CONFIG_H

/* Package information */
#define PACKAGE_NAME "μEmacs"
#define PACKAGE_VERSION "0.0.23"
#define PACKAGE_STRING "μEmacs 0.0.23"
#define PACKAGE_DESCRIPTION "Modern Linux terminal text editor"

/* Version Information */
#define ΜEMACS_VERSION_MAJOR 0
#define ΜEMACS_VERSION_MINOR 0
#define ΜEMACS_VERSION_PATCH 23
#define ΜEMACS_VERSION_STRING "0.0.23"

/* System headers - Linux only */
#define HAVE_TERMIOS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_INOTIFY_H 1
#define HAVE_SYS_SIGNALFD_H 1
#define HAVE_SYS_EVENTFD_H 1
#define HAVE_SYS_TIMERFD_H 1
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_SIGNAL_H */
/* #undef HAVE_LOCALE_H */

/* System functions */
/* #undef HAVE_GETENV */
/* #undef HAVE_MKSTEMP */
/* #undef HAVE_STRDUP */
/* #undef HAVE_SIGWINCH */

/* Platform - Linux only now */
#define LINUX 1
#define UNIX 1
#define POSIX 1
#define AUTOCONF 1

/* Remove all non-Linux platforms */
#define WIN32 0
#define MSDOS 0
#define VMS 0
#define BSD 0
#define USG 1
#define V7 0

/* Features always enabled for modern Linux */
#define UTF8 1
#define COLOR 1
#define CRYPT 1
#define FILOCK 1
#define MODERN 1
#define SCROLLCODE 1
#define ISRCH 1
#define WORDPRO 1
#define APROP 1
#define MAGIC 1
#define AEDIT 1
#define PROC 1
#define PKCODE 1

/* Terminal configuration - Modern Linux */
#define TERMCAP 1
#define ANSI 0
#define VT220 1
#define VT100 0
#define VT52 0
#define IBMPC 0
#define VMSVT 0

/* Modern Linux features */
#define TRUECOLOR 1
#define BRACKETED_PASTE 1
#define MOUSE_TRACKING 1
#define FILE_WATCHING 1
#define SYSTEM_CLIPBOARD 1
#define GIT_INTEGRATION 1

/* Limits */
#define MAXCOL 500
#define MAXROW 500
#define MAXWATCH 32

/* Program identification */
#define PROGNAME "μEmacs"

/* Character set */
#define ASCII 1
#define EBCDIC 0
#define IBMCHR 0

/* Other settings */
#define XONXOFF 0
#define NATIONL 0
#define CLEAN 0

#endif /* ΜEMACS_CONFIG_H */

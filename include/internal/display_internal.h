/*
 * display_internal.h - Internal display structures for overlay rendering
 *
 * Exposes vscreen and struct video for terminal overlay to write directly.
 * C23 compliant.
 */

#ifndef DISPLAY_INTERNAL_H
#define DISPLAY_INTERNAL_H

#include <stdatomic.h>
#include "utf8.h"  /* For unicode_t */

/* Forward declaration */
struct line;

/*
 * Video structure - represents one line of the virtual screen
 */
struct video {
    _Atomic int v_flag;         /* Flags (atomic for thread safety under rapid input) */
    struct line *v_linep;       /* Buffer line pointer (for highlighting) */
    _Atomic uint32_t v_checksum; /* Cached line checksum */
    unicode_t v_text[1];        /* Flexible array for screen data */
};

/* Video flags */
#define VFCHG   0x0001          /* Changed flag */
#define VFEXT   0x0002          /* Extended (beyond screen) */
#define VFREV   0x0004          /* Reverse video */
#define VFREQ   0x0008          /* Redraw required (mode line) */

/* Virtual screen array - allocated in vtinit() */
extern struct video **vscreen;

#endif /* DISPLAY_INTERNAL_H */

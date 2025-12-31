#ifndef CONSTANTS_H
#define CONSTANTS_H

/* Common ASCII Control Codes */
#define CTRL_G          0x07    /* Bell / Abort */
#define CTRL_H          0x08    /* Backspace */
#define CTRL_R          0x12    /* Redo (Vim) */
#define TAB_CHAR        0x09    /* Tab */
#define LINE_FEED       0x0A    /* Line Feed */
#define CARRIAGE_RETURN 0x0D    /* Carriage Return */
#define ESC_KEY         0x1B    /* Escape */
#define DEL_KEY         0x7F    /* Delete / Rubout */

/* Terminal Defaults */
#define DEFAULT_TERM_WIDTH  80
#define MAX_TERM_WIDTH      256
#define MAX_TERM_HEIGHT     256

/* Buffer Sizes */
#define NBINDS          256     /* max # of bound keys */
#define NFILEN          256     /* File name length */
#define NBUFN           16      /* Buffer name length */
#define NLINE           256     /* Input line length */
#define NSTRING         8192    /* String buffer size */
#define NKBDM           4096    /* Keyboard macro size (increased for complex macros) */
#define NPAT            128     /* Pattern buffer size */
#define HUGE            1000    /* Huge number */
#define CMDBUFLEN       256     /* Command buffer length */

/* Search & Replace */
#define SEARCH_BUFFER_SIZE  256

/* Magic Mode Constants */
#define HICHAR          256
#define HIBYTE          (HICHAR >> 3)
#define CLOSURE         256
#define MASKCL          (CLOSURE - 1)

#endif /* CONSTANTS_H */

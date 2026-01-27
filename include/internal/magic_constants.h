/*
 * magic_constants.h - Named constants for μEmacs
 *
 * C23 compliant
 *
 * Centralizes magic numbers (hashes, masks, protocol constants) used throughout.
 * Note: Buffer sizes and limits are in include/constants.h
 */

#ifndef UEMACS_MAGIC_CONSTANTS_H
#define UEMACS_MAGIC_CONSTANTS_H

/* MurmurHash constants (used in window_hash.c, keymap.h) */
#define MURMURHASH_M1  0x85ebca6bU
#define MURMURHASH_M2  0xc2b2ae35U

/* Control character handling */
#define CTRL_XOR_MASK  0x40    /* XOR mask for control char display (^A = 'A' ^ 0x40) */
#define BYTE_MASK      0xFF    /* Mask for extracting low byte */
#define ASCII_SPACE    0x20    /* First printable ASCII character */

/* Tab width masks for tabmask variable */
#define TABMASK_8      0x07    /* 8-space tabs: (col & 0x07) for alignment */
#define TABMASK_4      0x03    /* 4-space tabs: (col & 0x03) for alignment */

/* VT500/ANSI state machine byte ranges */
#define VT500_PARAM_START        0x30  /* Parameter bytes start: 0-9:;<=>? */
#define VT500_PARAM_END          0x3F  /* Parameter bytes end */
#define VT500_INTERMEDIATE_START 0x20  /* Intermediate bytes start: space through / */
#define VT500_INTERMEDIATE_END   0x2F  /* Intermediate bytes end */
#define VT500_FINAL_START        0x40  /* Final bytes start: @A-Z[\]^_`a-z{|}~ */
#define VT500_FINAL_END          0x7E  /* Final bytes end */

/* Mouse event bit masks (SGR/X10 mouse protocol) */
#define MOUSE_BUTTON_MASK       0x03  /* Low 2 bits: button number */
#define MOUSE_BUTTON_HIGH_MASK  0xC0  /* High 2 bits: extended button info */
#define MOUSE_MOD_SHIFT         0x04  /* Shift modifier bit */
#define MOUSE_MOD_ALT           0x08  /* Alt/Meta modifier bit */
#define MOUSE_MOD_CTRL          0x10  /* Ctrl modifier bit */
#define MOUSE_MOTION_BIT        0x20  /* Motion event flag */

#endif /* UEMACS_MAGIC_CONSTANTS_H */

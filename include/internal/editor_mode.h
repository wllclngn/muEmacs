#ifndef EDITOR_MODE_H
#define EDITOR_MODE_H

#include <stdatomic.h>

/* Editor Modes for Modal Editing (Vim-style) */
enum editor_mode {
    MODE_NORMAL = 0,       /* Vim Normal Mode (Navigation/Command) */
    MODE_INSERT = 1,       /* Emacs Standard Mode (Editing) */
    MODE_VISUAL = 2,       /* Character-wise selection */
    MODE_VISUAL_LINE = 3,  /* Line-wise selection */
    MODE_VISUAL_BLOCK = 4, /* Block/column selection (Ctrl-V) */
    MODE_REPLACE = 5       /* Overwrite mode */
};

/* Vim State Machine */
struct vim_state {
    _Atomic enum editor_mode current_mode;
    _Atomic int operator_pending;    /* e.g., 'd' waiting for movement */
    int visual_anchor_line;          /* Start line of visual selection */
    int visual_anchor_col;           /* Start col of visual selection */
    int block_start_col;             /* Virtual column where block selection started (Ctrl-V) */

    /* Count prefix system (5j, 3dw, etc.) - C23 atomic for signal safety */
    _Atomic int count;                       /* Accumulated count (0 = no count given) */
    _Atomic int count_given;                 /* Non-zero if user explicitly typed count */

    /* Operator-pending state (d{motion}, c{motion}, y{motion}) - C23 atomic */
    _Atomic int pending_op;                 /* 'd', 'c', 'y', or 0 (int for atomic) */
    _Atomic int op_count;                    /* Count before operator (2d3w = op_count=2, count=3) */

    /* f/F/t/T repeat state */
    char last_find_char;             /* Last character searched for */
    char last_find_cmd;              /* 'f', 'F', 't', or 'T' */

    /* Dot repeat recording */
    char last_change[256];           /* Buffer for last change sequence */
    int last_change_len;             /* Length of last change */
    int recording_change;            /* Non-zero if recording a change */

    /* Named marks (a-z) - position stored as line/column pairs */
    struct {
        struct line *line;           /* Line pointer (NULL if not set) */
        int offset;                  /* Column offset */
    } marks[26];                     /* 26 marks: a-z */

    /* Jump list for '' and `` */
    struct line *last_jump_line;
    int last_jump_offset;

    /* Search direction for n/N repeat */
    int last_search_dir;             /* 1 = forward, -1 = backward, 0 = none */

    /* Register system */
    char pending_register;           /* Selected register (0 = unnamed) */
    struct {
        char *text;                  /* Register content (malloc'd) */
        int len;                     /* Length of content */
        int linewise;                /* Non-zero if linewise (yy, dd, etc.) */
    } registers[36];                 /* 0-9 (numbered) + a-z (named) */
    int yank_register;               /* Which register gets next yank (0 or specific) */
};

/* Operator Pending Codes */
enum vim_operator {
    OP_NONE = 0,
    OP_DELETE = 1,  /* 'd' */
    OP_CHANGE = 2,  /* 'c' */
    OP_YANK = 3     /* 'y' */
};

#endif /* EDITOR_MODE_H */

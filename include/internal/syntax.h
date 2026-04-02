/*
 * syntax.h - Syntax highlighting infrastructure
 *
 * μEmacs v1.0 - Hybrid C23 Core + UEP Extension Architecture
 *
 * Token-based syntax highlighting with:
 * - Per-line token storage (VSCode-inspired)
 * - Binary search for O(log n) lookup
 * - Incremental re-lexing on edit
 * - Extension API for additional languages
 */

#ifndef UEMACS_SYNTAX_H
#define UEMACS_SYNTAX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "c23_compat.h"

/* ============================================================================
 * Face IDs (4 bits = 16 faces)
 * ============================================================================ */

typedef enum syntax_face {
    FACE_DEFAULT = 0,
    FACE_KEYWORD,       /* if, while, return, fn, def */
    FACE_STRING,        /* "hello", 'c', `template` */
    FACE_COMMENT,       /* line and block comments */
    FACE_NUMBER,        /* 42, 3.14, 0xFF */
    FACE_TYPE,          /* int, String, MyClass */
    FACE_FUNCTION,      /* foo(), bar() */
    FACE_OPERATOR,      /* +, -, &&, |> */
    FACE_PREPROCESSOR,  /* #include, #define */
    FACE_CONSTANT,      /* nullptr, true, false */
    FACE_VARIABLE,      /* (heuristic or semantic) */
    FACE_ATTRIBUTE,     /* @decorator, #[attr] */
    FACE_ESCAPE,        /* \n, \x00 */
    FACE_REGEX,         /* /pattern/ */
    FACE_SPECIAL,       /* reserved for extensions */
    FACE_MAX
} syntax_face_t;

static_assert(FACE_MAX <= 16, "Face IDs must fit in 4 bits (SYNTAX_FACE_SHIFT packing)");

/* Font style flags (combine with face via OR) */
#define STYLE_NONE      0x00
#define STYLE_BOLD      0x10
#define STYLE_ITALIC    0x20
#define STYLE_UNDERLINE 0x40
#define STYLE_DIM       0x80

/* Extract face and style from combined value */
#define FACE_ID(x)      ((x) & 0x0F)
#define FACE_STYLE(x)   ((x) & 0xF0)

/* ============================================================================
 * Per-Line Token Storage
 * ============================================================================ */

/*
 * Tokens stored as pairs: [end_col, face_id]
 * Binary search to find face at any column.
 *
 * Example for: "int foo = 42;"
 *   tokens = [3, FACE_TYPE, 7, FACE_FUNCTION, 9, FACE_OPERATOR, 11, FACE_NUMBER, 12, FACE_DEFAULT]
 *   Meaning: cols 0-2 = TYPE, cols 3-6 = FUNCTION, col 7-8 = OPERATOR, etc.
 */
typedef struct line_tokens {
    uint16_t *data;     /* [end_col, face_id, end_col, face_id, ...] */
    uint16_t count;     /* number of token pairs */
    uint16_t capacity;  /* allocated pairs */
} line_tokens_t;

/* ============================================================================
 * Lexer State (for incremental re-lexing)
 * ============================================================================ */

typedef enum lexer_mode {
    LEX_NORMAL = 0,
    LEX_STRING,
    LEX_STRING_ESCAPE,
    LEX_CHAR,
    LEX_LINE_COMMENT,
    LEX_BLOCK_COMMENT,
    LEX_RAW_STRING,
    LEX_HEREDOC,
    LEX_TEMPLATE_LITERAL,
    LEX_TRIPLE_STRING,
} lexer_mode_t;

typedef struct lexer_state {
    lexer_mode_t mode;
    int nest_depth;         /* for nested comments/strings */
    char string_delim;      /* which quote started string */
    uint32_t state_hash;    /* for detecting state stabilization */
} lexer_state_t;

/* Initial state for beginning of file */
#define LEXER_STATE_INIT ((lexer_state_t){LEX_NORMAL, 0, 0, 0})

/* Compare two states for equality (used in incremental re-lex) */
UNSEQUENCED static inline bool lexer_state_eq(lexer_state_t a, lexer_state_t b) {
    return a.mode == b.mode &&
           a.nest_depth == b.nest_depth &&
           a.string_delim == b.string_delim;
}

/* ============================================================================
 * Buffer Syntax State
 * ============================================================================ */

typedef struct buffer_syntax {
    line_tokens_t *lines;   /* array, one per buffer line */
    lexer_state_t *states;  /* end-of-line state per line (for incremental) */
    int line_count;         /* number of lines allocated */
    int language_id;        /* which built-in lexer to use */
    bool enabled;           /* syntax highlighting on/off for this buffer */
    bool dirty;             /* needs full re-lex */
    int first_invalid_line; /* -1 = all valid, else first line needing re-lex */
} buffer_syntax_t;

/* ============================================================================
 * Language Definition
 * ============================================================================ */

/* Forward declaration for lexer function type */
struct syntax_language;
struct buffer;

/* Custom lexer function signature
 *
 * buffer and line_num are provided for AST-based parsers (tree-sitter, LSP)
 * that need to query a cached parse tree. Built-in state-machine lexers
 * can ignore these parameters.
 */
typedef lexer_state_t (*syntax_lex_fn)(
    const struct syntax_language *lang,
    struct buffer *buffer,      /* Buffer being lexed (may be nullptr for tests) */
    int line_num,               /* 0-indexed line number */
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
);

typedef struct syntax_language {
    const char *name;               /* "c", "python", "rust" */
    const char **extensions;        /* {".c", ".h", nullptr} */
    const char **keywords;          /* {"if", "else", "while", nullptr} */
    const char **types;             /* {"int", "char", "void", nullptr} */
    const char **constants;         /* {"nullptr", "true", "false", nullptr} */
    const char **builtins;          /* built-in functions */

    /* Comment styles */
    const char *line_comment;       /* "//" or "#" or "--" */
    const char *block_start;        /* slash-star or "{-" */
    const char *block_end;          /* star-slash or "-}" */
    bool nested_comments;           /* Rust/Haskell style nested */

    /* String styles */
    bool has_single_quote_str;      /* 'string' */
    bool has_double_quote_str;      /* "string" */
    bool has_backtick_str;          /* `template` */
    bool has_raw_strings;           /* r"..." or R"..." */
    bool has_triple_quote;          /* """...""" or '''...''' */
    char raw_string_prefix;         /* 'r' or 'R' or '\0' */

    /* Preprocessor */
    const char *preprocessor;       /* "#" for C, nullptr for most */

    /* Flags */
    bool case_sensitive;            /* keyword matching */

    /* Optional custom lexer (nullptr = use generic) */
    syntax_lex_fn custom_lexer;
} syntax_language_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/* Initialize syntax highlighting subsystem */
int syntax_init(void);

/* Shutdown and free all resources */
void syntax_shutdown(void);

/* Create syntax state for a buffer */
buffer_syntax_t *syntax_create(int initial_lines);

/* Free syntax state */
void syntax_free(buffer_syntax_t *syn);

/* Resize syntax state when buffer grows/shrinks */
int syntax_resize(buffer_syntax_t *syn, int new_line_count);

/* Detect language from filename, returns language_id or -1 */
int syntax_detect_language(const char *filename);

/* Get language by ID */
const syntax_language_t *syntax_get_language(int lang_id);

/* Full lex of entire buffer */
int syntax_lex_buffer(buffer_syntax_t *syn, int lang_id,
                      struct buffer *bp,
                      const char **lines, int line_count);

/* Incremental re-lex starting from line (after edit) */
int syntax_relex_from(buffer_syntax_t *syn, int lang_id,
                      struct buffer *bp,
                      const char **lines, int line_count,
                      int start_line);

/* Get face at position (binary search) */
int syntax_get_face(const buffer_syntax_t *syn, int line, int col);

/* Invalidate syntax from given line onwards (call after edit) */
void syntax_invalidate_from_line(buffer_syntax_t *syn, int line);

/* Re-lex invalid lines if needed (call before display) */
void syntax_update_if_needed(buffer_syntax_t *syn, int lang_id,
                              struct buffer *bp,
                              const char **lines, int line_count);

/* ============================================================================
 * Line Token Helpers
 * ============================================================================ */

/* Allocate tokens array */
line_tokens_t *line_tokens_alloc(int capacity);

/* Free tokens array */
void line_tokens_free(line_tokens_t *lt);

/* Clear tokens (reuse allocation) */
void line_tokens_clear(line_tokens_t *lt);

/* Add a token (end_col, face_id) */
int line_tokens_add(line_tokens_t *lt, uint16_t end_col, uint16_t face);

/* Binary search for face at column */
int line_tokens_get_face(const line_tokens_t *lt, int col);

/* ============================================================================
 * Extension Registration (for UEP)
 * ============================================================================ */

/* Register a custom language lexer from extension */
int syntax_register_language(
    const char *name,
    const char **patterns,      /* {"*.adb", "*.ads", nullptr} */
    syntax_lex_fn lex_fn,
    void *user_data
);

/* Unregister a custom language */
int syntax_unregister_language(const char *name);

#endif /* UEMACS_SYNTAX_H */

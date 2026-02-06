/*
 * lexer.c - Generic state machine lexer
 *
 * μEmacs v1.0
 *
 * Implements a table-driven lexer that handles most C-like languages.
 * Languages can override with custom_lexer for special cases.
 */

#include "internal/syntax.h"
#include "internal/memory.h"
#include "util/logger.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Keyword Lookup
 * ============================================================================ */

static bool is_keyword(const char **keywords, const char *word, int len, bool case_sensitive) {
    if (!keywords || !word || len <= 0) return false;

    /* Make null-terminated copy */
    char buf[64];
    if (len >= (int)sizeof(buf)) return false;
    memcpy(buf, word, (size_t)len);
    buf[len] = '\0';

    /* Linear search (keywords arrays are small) */
    for (int i = 0; keywords[i]; i++) {
        int cmp = case_sensitive ?
            strcmp(buf, keywords[i]) :
            strcasecmp(buf, keywords[i]);
        if (cmp == 0) return true;
    }
    return false;
}

/* ============================================================================
 * Character Classification
 * ============================================================================ */

static inline bool is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static inline bool is_ident_char(int c) {
    return isalnum(c) || c == '_';
}

static inline bool is_hex_digit(int c) {
    return isxdigit(c);
}

static inline bool is_bin_digit(int c) {
    return c == '0' || c == '1';
}

static inline bool is_oct_digit(int c) {
    return c >= '0' && c <= '7';
}

static inline bool starts_with(const char *s, int len, const char *prefix) {
    if (!prefix) return false;
    int plen = (int)strlen(prefix);
    return len >= plen && memcmp(s, prefix, (size_t)plen) == 0;
}

/* ============================================================================
 * Generic Lexer
 * ============================================================================ */

/*
 * Lex a single line using the language definition.
 * Produces token spans in 'out'.
 */
lexer_state_t syntax_lex_line(
    const syntax_language_t *lang,
    const char *line,
    int len,
    lexer_state_t prev_state,
    line_tokens_t *out
) {
    if (!lang || !line || !out) return prev_state;

    /* Clear output tokens */
    out->count = 0;

    lexer_state_t state = prev_state;
    int i = 0;
    int token_start = 0;
    syntax_face_t current_face = FACE_DEFAULT;

    /* Helper to emit token */
    #define EMIT_TOKEN(end_col, face) do { \
        if ((end_col) > token_start) { \
            line_tokens_add(out, (uint16_t)(end_col), (uint16_t)(face)); \
        } \
        token_start = (end_col); \
    } while (0)

    /* Continue previous state */
    if (state.mode == LEX_BLOCK_COMMENT) {
        current_face = FACE_COMMENT;
    } else if (state.mode == LEX_STRING || state.mode == LEX_TRIPLE_STRING) {
        current_face = FACE_STRING;
    }

    while (i < len) {
        int c = (unsigned char)line[i];

        switch (state.mode) {
        case LEX_NORMAL:
            /* Check for line comment */
            if (lang->line_comment && starts_with(line + i, len - i, lang->line_comment)) {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_COMMENT;
                /* Rest of line is comment */
                i = len;
                EMIT_TOKEN(i, FACE_COMMENT);
                continue;
            }

            /* Check for block comment start */
            if (lang->block_start && starts_with(line + i, len - i, lang->block_start)) {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_COMMENT;
                state.mode = LEX_BLOCK_COMMENT;
                state.nest_depth = 1;
                i += (int)strlen(lang->block_start);
                continue;
            }

            /* Check for preprocessor */
            if (lang->preprocessor && i == 0 && c == lang->preprocessor[0]) {
                /* Skip leading whitespace check - # at start means preprocessor */
                EMIT_TOKEN(i, current_face);
                current_face = FACE_PREPROCESSOR;
                i = len; /* Whole line is preprocessor */
                EMIT_TOKEN(i, FACE_PREPROCESSOR);
                continue;
            }

            /* Check for triple-quoted string */
            if (lang->has_triple_quote &&
                i + 2 < len &&
                ((c == '"' && line[i+1] == '"' && line[i+2] == '"') ||
                 (c == '\'' && line[i+1] == '\'' && line[i+2] == '\''))) {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_STRING;
                state.mode = LEX_TRIPLE_STRING;
                state.string_delim = (char)c;
                i += 3;
                continue;
            }

            /* Check for raw string */
            if (lang->has_raw_strings && lang->raw_string_prefix &&
                (c == lang->raw_string_prefix || c == toupper(lang->raw_string_prefix)) &&
                i + 1 < len && (line[i+1] == '"' || line[i+1] == '\'')) {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_STRING;
                state.mode = LEX_RAW_STRING;
                state.string_delim = line[i+1];
                i += 2;
                continue;
            }

            /* Check for double-quoted string */
            if (lang->has_double_quote_str && c == '"') {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_STRING;
                state.mode = LEX_STRING;
                state.string_delim = '"';
                i++;
                continue;
            }

            /* Check for single-quoted string/char */
            if (lang->has_single_quote_str && c == '\'') {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_STRING;
                state.mode = LEX_STRING;
                state.string_delim = '\'';
                i++;
                continue;
            }

            /* Check for backtick template literal */
            if (lang->has_backtick_str && c == '`') {
                EMIT_TOKEN(i, current_face);
                current_face = FACE_STRING;
                state.mode = LEX_TEMPLATE_LITERAL;
                state.string_delim = '`';
                i++;
                continue;
            }

            /* Check for identifier/keyword */
            if (is_ident_start(c)) {
                EMIT_TOKEN(i, current_face);
                int start = i;
                while (i < len && is_ident_char((unsigned char)line[i])) {
                    i++;
                }
                int word_len = i - start;

                /* Determine face for this identifier */
                syntax_face_t id_face = FACE_DEFAULT;

                if (is_keyword(lang->keywords, line + start, word_len, lang->case_sensitive)) {
                    id_face = FACE_KEYWORD;
                } else if (is_keyword(lang->types, line + start, word_len, lang->case_sensitive)) {
                    id_face = FACE_TYPE;
                } else if (is_keyword(lang->constants, line + start, word_len, lang->case_sensitive)) {
                    id_face = FACE_CONSTANT;
                } else if (is_keyword(lang->builtins, line + start, word_len, lang->case_sensitive)) {
                    id_face = FACE_FUNCTION;
                } else if (i < len && line[i] == '(') {
                    /* Followed by ( = function call */
                    id_face = FACE_FUNCTION;
                }

                EMIT_TOKEN(i, id_face);
                current_face = FACE_DEFAULT;
                continue;
            }

            /* Check for number */
            if (isdigit(c) || (c == '.' && i + 1 < len && isdigit((unsigned char)line[i+1]))) {
                EMIT_TOKEN(i, current_face);
                int start = i;

                /* Hex: 0x... */
                if (c == '0' && i + 1 < len && (line[i+1] == 'x' || line[i+1] == 'X')) {
                    i += 2;
                    while (i < len && is_hex_digit((unsigned char)line[i])) i++;
                }
                /* Binary: 0b... */
                else if (c == '0' && i + 1 < len && (line[i+1] == 'b' || line[i+1] == 'B')) {
                    i += 2;
                    while (i < len && is_bin_digit((unsigned char)line[i])) i++;
                }
                /* Octal: 0o... or just 0... */
                else if (c == '0' && i + 1 < len && (line[i+1] == 'o' || line[i+1] == 'O')) {
                    i += 2;
                    while (i < len && is_oct_digit((unsigned char)line[i])) i++;
                }
                /* Decimal/float */
                else {
                    while (i < len && isdigit((unsigned char)line[i])) i++;
                    /* Decimal point */
                    if (i < len && line[i] == '.') {
                        i++;
                        while (i < len && isdigit((unsigned char)line[i])) i++;
                    }
                    /* Exponent */
                    if (i < len && (line[i] == 'e' || line[i] == 'E')) {
                        i++;
                        if (i < len && (line[i] == '+' || line[i] == '-')) i++;
                        while (i < len && isdigit((unsigned char)line[i])) i++;
                    }
                }

                /* Type suffix (u, l, f, etc.) */
                while (i < len && isalpha((unsigned char)line[i])) i++;

                EMIT_TOKEN(i, FACE_NUMBER);
                current_face = FACE_DEFAULT;
                continue;
            }

            /* Operators - just mark as operator and continue */
            if (strchr("+-*/%=<>!&|^~?:;,.()[]{}", c)) {
                EMIT_TOKEN(i, current_face);
                /* Multi-char operators */
                int op_start = i;
                i++;
                /* Handle common multi-char operators */
                if (i < len) {
                    char next = line[i];
                    if ((c == '=' && next == '=') ||
                        (c == '!' && next == '=') ||
                        (c == '<' && next == '=') ||
                        (c == '>' && next == '=') ||
                        (c == '&' && next == '&') ||
                        (c == '|' && next == '|') ||
                        (c == '+' && next == '+') ||
                        (c == '-' && next == '-') ||
                        (c == '<' && next == '<') ||
                        (c == '>' && next == '>') ||
                        (c == '-' && next == '>') ||
                        (c == '=' && next == '>')) {
                        i++;
                    }
                }
                EMIT_TOKEN(i, FACE_OPERATOR);
                current_face = FACE_DEFAULT;
                continue;
            }

            /* Whitespace or other - just advance */
            i++;
            break;

        case LEX_STRING:
            if (c == '\\' && i + 1 < len) {
                /* Escape sequence */
                EMIT_TOKEN(i, current_face);
                i += 2; /* Skip backslash and escaped char */
                EMIT_TOKEN(i, FACE_ESCAPE);
                continue;
            }
            if (c == state.string_delim) {
                i++;
                EMIT_TOKEN(i, FACE_STRING);
                state.mode = LEX_NORMAL;
                current_face = FACE_DEFAULT;
                continue;
            }
            i++;
            break;

        case LEX_RAW_STRING:
            if (c == state.string_delim) {
                i++;
                EMIT_TOKEN(i, FACE_STRING);
                state.mode = LEX_NORMAL;
                current_face = FACE_DEFAULT;
                continue;
            }
            i++;
            break;

        case LEX_TRIPLE_STRING:
            if (c == state.string_delim &&
                i + 2 < len &&
                line[i+1] == state.string_delim &&
                line[i+2] == state.string_delim) {
                i += 3;
                EMIT_TOKEN(i, FACE_STRING);
                state.mode = LEX_NORMAL;
                current_face = FACE_DEFAULT;
                continue;
            }
            i++;
            break;

        case LEX_TEMPLATE_LITERAL:
            if (c == '\\' && i + 1 < len) {
                EMIT_TOKEN(i, current_face);
                i += 2;
                EMIT_TOKEN(i, FACE_ESCAPE);
                continue;
            }
            if (c == '`') {
                i++;
                EMIT_TOKEN(i, FACE_STRING);
                state.mode = LEX_NORMAL;
                current_face = FACE_DEFAULT;
                continue;
            }
            /* TODO: Handle ${...} interpolation */
            i++;
            break;

        case LEX_BLOCK_COMMENT:
            /* Check for nested comment start (if supported) */
            if (lang->nested_comments && lang->block_start &&
                starts_with(line + i, len - i, lang->block_start)) {
                state.nest_depth++;
                i += (int)strlen(lang->block_start);
                continue;
            }
            /* Check for block comment end */
            if (lang->block_end && starts_with(line + i, len - i, lang->block_end)) {
                state.nest_depth--;
                i += (int)strlen(lang->block_end);
                if (state.nest_depth <= 0) {
                    EMIT_TOKEN(i, FACE_COMMENT);
                    state.mode = LEX_NORMAL;
                    state.nest_depth = 0;
                    current_face = FACE_DEFAULT;
                }
                continue;
            }
            i++;
            break;

        default:
            i++;
            break;
        }
    }

    /* Emit final token for remainder of line */
    if (i > token_start) {
        syntax_face_t final_face = current_face;
        if (state.mode == LEX_BLOCK_COMMENT) {
            final_face = FACE_COMMENT;
        } else if (state.mode == LEX_STRING ||
                   state.mode == LEX_TRIPLE_STRING ||
                   state.mode == LEX_RAW_STRING ||
                   state.mode == LEX_TEMPLATE_LITERAL) {
            final_face = FACE_STRING;
        }
        EMIT_TOKEN(i, final_face);
    }

    #undef EMIT_TOKEN

    /* Compute state hash for incremental re-lex detection */
    state.state_hash = (uint32_t)state.mode |
                       ((uint32_t)state.nest_depth << 8) |
                       ((uint32_t)state.string_delim << 16);

    return state;
}

/* ============================================================================
 * Buffer Lexing
 * ============================================================================ */

int syntax_lex_buffer(
    buffer_syntax_t *syn,
    int lang_id,
    struct buffer *bp,
    const char **lines,
    int line_count
) {
    if (!syn || !lines || line_count <= 0) return -1;

    const syntax_language_t *lang = syntax_get_language(lang_id);
    if (!lang) return -1;

    /* Resize if needed */
    if (syntax_resize(syn, line_count) < 0) return -1;

    syn->language_id = lang_id;
    lexer_state_t state = LEXER_STATE_INIT;

    for (int i = 0; i < line_count; i++) {
        const char *line = lines[i];
        int len = line ? (int)strlen(line) : 0;

        /* Allocate line tokens if needed */
        if (!syn->lines[i].data) {
            syn->lines[i].data = safe_alloc(16 * 2 * sizeof(uint16_t), "syntax line tokens", __FILE__, __LINE__);
            syn->lines[i].capacity = 16;
        }
        syn->lines[i].count = 0;

        /* Lex the line */
        if (lang->custom_lexer) {
            state = lang->custom_lexer(lang, bp, i, line, len, state, &syn->lines[i]);
        } else {
            state = syntax_lex_line(lang, line, len, state, &syn->lines[i]);
        }

        /* Store end-of-line state */
        syn->states[i] = state;
    }

    syn->dirty = false;
    return 0;
}

int syntax_relex_from(
    buffer_syntax_t *syn,
    int lang_id,
    struct buffer *bp,
    const char **lines,
    int line_count,
    int start_line
) {
    if (!syn || !lines || line_count <= 0) return -1;
    if (start_line < 0) start_line = 0;
    if (start_line >= line_count) return 0; /* Nothing to do */

    const syntax_language_t *lang = syntax_get_language(lang_id);
    if (!lang) return -1;

    /* Resize if needed */
    if (syntax_resize(syn, line_count) < 0) return -1;

    /* Get starting state from previous line (or init if first line) */
    lexer_state_t state = (start_line > 0) ?
        syn->states[start_line - 1] :
        LEXER_STATE_INIT;

    for (int i = start_line; i < line_count; i++) {
        const char *line = lines[i];
        int len = line ? (int)strlen(line) : 0;

        /* Store previous state for comparison */
        lexer_state_t old_state = syn->states[i];

        /* Allocate line tokens if needed */
        if (!syn->lines[i].data) {
            syn->lines[i].data = safe_alloc(16 * 2 * sizeof(uint16_t), "syntax line tokens", __FILE__, __LINE__);
            syn->lines[i].capacity = 16;
        }
        syn->lines[i].count = 0;

        /* Lex the line */
        if (lang->custom_lexer) {
            state = lang->custom_lexer(lang, bp, i, line, len, state, &syn->lines[i]);
        } else {
            state = syntax_lex_line(lang, line, len, state, &syn->lines[i]);
        }

        /* Store new end-of-line state */
        syn->states[i] = state;

        /* Check if state stabilized (matches old state) */
        if (i > start_line && lexer_state_eq(state, old_state)) {
            /* State stabilized - no need to continue */
            break;
        }
    }

    return 0;
}

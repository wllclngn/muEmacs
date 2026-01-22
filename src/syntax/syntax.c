/*
 * syntax.c - Core syntax highlighting engine
 *
 * μEmacs v1.0
 *
 * Implements:
 * - Token storage and binary search lookup
 * - Buffer syntax state management
 * - Language detection
 * - Incremental re-lexing
 */

#include "internal/syntax.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/memory.h"
#include "terminal/palette.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Built-in Language Definitions (forward declarations)
 * ============================================================================ */

/* Defined in languages.c */
extern const syntax_language_t lang_c;
extern const syntax_language_t lang_cpp;
extern const syntax_language_t lang_zig;
extern const syntax_language_t lang_go;
extern const syntax_language_t lang_rust;
extern const syntax_language_t lang_javascript;
extern const syntax_language_t lang_typescript;
extern const syntax_language_t lang_java;
extern const syntax_language_t lang_python;
extern const syntax_language_t lang_bash;
extern const syntax_language_t lang_lua;
extern const syntax_language_t lang_sql;
extern const syntax_language_t lang_markdown;
extern const syntax_language_t lang_toml;
extern const syntax_language_t lang_yaml;
extern const syntax_language_t lang_json;

/* Language registry */
static const syntax_language_t *builtin_languages[] = {
    &lang_c,
    &lang_cpp,
    &lang_zig,
    &lang_go,
    &lang_rust,
    &lang_javascript,
    &lang_typescript,
    &lang_java,
    &lang_python,
    &lang_bash,
    &lang_lua,
    &lang_sql,
    &lang_markdown,
    &lang_toml,
    &lang_yaml,
    &lang_json,
    NULL
};

#define BUILTIN_LANG_COUNT (sizeof(builtin_languages) / sizeof(builtin_languages[0]) - 1)

/* Extension-registered languages */
#define MAX_EXT_LANGUAGES 32

typedef struct ext_language_entry {
    syntax_language_t lang;
    char *patterns[16];
    void *user_data;
    bool in_use;
} ext_language_entry_t;

static ext_language_entry_t ext_languages[MAX_EXT_LANGUAGES];
static int ext_language_count = 0;

/* ============================================================================
 * Initialization
 * ============================================================================ */

int syntax_init(void) {
    memset(ext_languages, 0, sizeof(ext_languages));
    ext_language_count = 0;
    return 0;
}

void syntax_shutdown(void) {
    /* Free extension language patterns */
    for (int i = 0; i < MAX_EXT_LANGUAGES; i++) {
        if (ext_languages[i].in_use) {
            for (int j = 0; ext_languages[i].patterns[j]; j++) {
                free(ext_languages[i].patterns[j]);
            }
            ext_languages[i].in_use = false;
        }
    }
    ext_language_count = 0;
}

/* ============================================================================
 * Line Token Management
 * ============================================================================ */

line_tokens_t *line_tokens_alloc(int capacity) {
    line_tokens_t *lt = SAFE_ALLOC(line_tokens_t, "line tokens");
    if (!lt) return NULL;

    lt->data = SAFE_ARRAY(uint16_t, capacity * 2, "line tokens data");
    if (!lt->data) {
        SAFE_FREE(lt);
        return NULL;
    }

    lt->count = 0;
    lt->capacity = capacity;
    return lt;
}

void line_tokens_free(line_tokens_t *lt) {
    if (lt) {
        SAFE_FREE(lt->data);
        SAFE_FREE(lt);
    }
}

void line_tokens_clear(line_tokens_t *lt) {
    if (lt) {
        lt->count = 0;
    }
}

int line_tokens_add(line_tokens_t *lt, uint16_t end_col, uint16_t face) {
    if (!lt) return -1;

    /* Grow if needed */
    if (lt->count >= lt->capacity) {
        int new_cap = lt->capacity * 2;
        if (new_cap < 16) new_cap = 16;
        uint16_t *new_data = SAFE_REALLOC(lt->data, new_cap * 2 * sizeof(uint16_t), "line tokens grow");
        if (!new_data) return -1;
        lt->data = new_data;
        lt->capacity = new_cap;
    }

    /* Add token pair */
    lt->data[lt->count * 2] = end_col;
    lt->data[lt->count * 2 + 1] = face;
    lt->count++;
    return 0;
}

/*
 * Binary search for face at column.
 * Tokens are stored as [end_col, face] pairs.
 * We find the first token where end_col > col.
 */
int line_tokens_get_face(const line_tokens_t *lt, int col) {
    if (!lt || lt->count == 0) return FACE_DEFAULT;

    int lo = 0, hi = lt->count - 1;

    /* Binary search for first token where end_col > col */
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (lt->data[mid * 2] <= (uint16_t)col) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    /* Check if we found a valid token */
    if (lt->data[lo * 2] > (uint16_t)col) {
        return lt->data[lo * 2 + 1];
    }

    return FACE_DEFAULT;
}

/* ============================================================================
 * Buffer Syntax State
 * ============================================================================ */

buffer_syntax_t *syntax_create(int initial_lines) {
    buffer_syntax_t *syn = SAFE_ALLOC(buffer_syntax_t, "buffer syntax");
    if (!syn) return NULL;

    if (initial_lines > 0) {
        syn->lines = SAFE_ARRAY(line_tokens_t, initial_lines, "syntax lines");
        syn->states = SAFE_ARRAY(lexer_state_t, initial_lines, "syntax states");
        if (!syn->lines || !syn->states) {
            SAFE_FREE(syn->lines);
            SAFE_FREE(syn->states);
            SAFE_FREE(syn);
            return NULL;
        }
        syn->line_count = initial_lines;
    }

    syn->language_id = -1;
    syn->enabled = true;
    syn->dirty = true;
    syn->first_invalid_line = -1;  /* All valid initially (will be set on first lex) */
    return syn;
}

void syntax_free(buffer_syntax_t *syn) {
    if (!syn) return;

    if (syn->lines) {
        for (int i = 0; i < syn->line_count; i++) {
            SAFE_FREE(syn->lines[i].data);
        }
        SAFE_FREE(syn->lines);
    }
    SAFE_FREE(syn->states);
    SAFE_FREE(syn);
}

int syntax_resize(buffer_syntax_t *syn, int new_count) {
    if (!syn) return -1;

    if (new_count <= syn->line_count) {
        /* Shrinking: free extra lines */
        for (int i = new_count; i < syn->line_count; i++) {
            SAFE_FREE(syn->lines[i].data);
            syn->lines[i].count = 0;
            syn->lines[i].capacity = 0;
        }
        syn->line_count = new_count;
        return 0;
    }

    /* Growing */
    line_tokens_t *new_lines = SAFE_REALLOC(syn->lines, new_count * sizeof(line_tokens_t), "syntax lines grow");
    lexer_state_t *new_states = SAFE_REALLOC(syn->states, new_count * sizeof(lexer_state_t), "syntax states grow");

    if (!new_lines || !new_states) {
        /* Partial failure - try to recover */
        if (new_lines) syn->lines = new_lines;
        if (new_states) syn->states = new_states;
        return -1;
    }

    syn->lines = new_lines;
    syn->states = new_states;

    /* Zero new entries */
    for (int i = syn->line_count; i < new_count; i++) {
        syn->lines[i].data = NULL;
        syn->lines[i].count = 0;
        syn->lines[i].capacity = 0;
        syn->states[i] = LEXER_STATE_INIT;
    }

    syn->line_count = new_count;
    return 0;
}

/* ============================================================================
 * Language Detection
 * ============================================================================ */

/* Check if filename matches pattern (glob: *.ext or .ext) */
static bool pattern_match(const char *pattern, const char *filename) {
    if (!pattern || !filename) return false;

    /* Handle *.ext pattern */
    if (pattern[0] == '*' && pattern[1] == '.') {
        const char *ext = strrchr(filename, '.');
        if (ext) {
            return strcasecmp(ext, pattern + 1) == 0;
        }
        return false;
    }

    /* Handle .ext pattern (extension only, no wildcard) */
    if (pattern[0] == '.') {
        const char *ext = strrchr(filename, '.');
        if (ext) {
            return strcasecmp(ext, pattern) == 0;
        }
        return false;
    }

    /* Exact match */
    return strcmp(pattern, filename) == 0;
}

int syntax_detect_language(const char *filename) {
    if (!filename) return -1;

    /* Get basename */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    /* Priority controlled by g_palette.syntax_prefer_builtin (TOML: prefer_builtin)
     * true  = Built-in lexers first (immediate highlighting, extensions for new types)
     * false = Extension lexers first (LSP semantic tokens can override) */

    if (g_palette.syntax_prefer_builtin) {
        /* Check built-in languages first */
        for (int i = 0; builtin_languages[i]; i++) {
            const syntax_language_t *lang = builtin_languages[i];
            if (!lang->extensions) continue;

            for (int j = 0; lang->extensions[j]; j++) {
                if (pattern_match(lang->extensions[j], base)) {
                    return i;
                }
            }
        }

        /* Then check extension-registered languages */
        for (int i = 0; i < MAX_EXT_LANGUAGES; i++) {
            if (!ext_languages[i].in_use) continue;
            for (int j = 0; ext_languages[i].patterns[j]; j++) {
                if (pattern_match(ext_languages[i].patterns[j], base)) {
                    return BUILTIN_LANG_COUNT + i;
                }
            }
        }
    } else {
        /* Check extension-registered languages first (higher priority) */
        for (int i = 0; i < MAX_EXT_LANGUAGES; i++) {
            if (!ext_languages[i].in_use) continue;
            for (int j = 0; ext_languages[i].patterns[j]; j++) {
                if (pattern_match(ext_languages[i].patterns[j], base)) {
                    return BUILTIN_LANG_COUNT + i;
                }
            }
        }

        /* Then check built-in languages */
        for (int i = 0; builtin_languages[i]; i++) {
            const syntax_language_t *lang = builtin_languages[i];
            if (!lang->extensions) continue;

            for (int j = 0; lang->extensions[j]; j++) {
                if (pattern_match(lang->extensions[j], base)) {
                    return i;
                }
            }
        }
    }

    return -1; /* Unknown */
}

const syntax_language_t *syntax_get_language(int lang_id) {
    if (lang_id < 0) return NULL;

    if (lang_id < (int)BUILTIN_LANG_COUNT) {
        return builtin_languages[lang_id];
    }

    int ext_id = lang_id - BUILTIN_LANG_COUNT;
    if (ext_id < MAX_EXT_LANGUAGES && ext_languages[ext_id].in_use) {
        return &ext_languages[ext_id].lang;
    }

    return NULL;
}

/* ============================================================================
 * Face Lookup
 * ============================================================================ */

int syntax_get_face(const buffer_syntax_t *syn, int line, int col) {
    if (!syn || !syn->enabled || !syn->lines) return FACE_DEFAULT;
    if (line < 0 || line >= syn->line_count) return FACE_DEFAULT;

    return line_tokens_get_face(&syn->lines[line], col);
}

/* ============================================================================
 * Extension Registration
 * ============================================================================ */

int syntax_register_language(
    const char *name,
    const char **patterns,
    syntax_lex_fn lex_fn,
    void *user_data
) {
    if (!name || !patterns || !lex_fn) return -1;

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_EXT_LANGUAGES; i++) {
        if (!ext_languages[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1; /* No room */

    /* Copy patterns */
    int pat_count = 0;
    for (int i = 0; patterns[i] && i < 15; i++) {
        ext_languages[slot].patterns[i] = SAFE_STRDUP(patterns[i], "syntax pattern");
        if (!ext_languages[slot].patterns[i]) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                SAFE_FREE(ext_languages[slot].patterns[j]);
            }
            return -1;
        }
        pat_count++;
    }
    ext_languages[slot].patterns[pat_count] = NULL;

    /* Set up language */
    ext_languages[slot].lang.name = name;
    ext_languages[slot].lang.extensions = (const char **)ext_languages[slot].patterns;
    ext_languages[slot].lang.custom_lexer = lex_fn;
    ext_languages[slot].user_data = user_data;
    ext_languages[slot].in_use = true;
    ext_language_count++;

    return BUILTIN_LANG_COUNT + slot;
}

int syntax_unregister_language(const char *name) {
    if (!name) return -1;

    for (int i = 0; i < MAX_EXT_LANGUAGES; i++) {
        if (ext_languages[i].in_use &&
            strcmp(ext_languages[i].lang.name, name) == 0) {
            /* Free patterns */
            for (int j = 0; ext_languages[i].patterns[j]; j++) {
                free(ext_languages[i].patterns[j]);
                ext_languages[i].patterns[j] = NULL;
            }
            ext_languages[i].in_use = false;
            ext_language_count--;
            return 0;
        }
    }

    return -1; /* Not found */
}

/* ============================================================================
 * Incremental Re-lexing
 * ============================================================================ */

void syntax_invalidate_from_line(buffer_syntax_t *syn, int line) {
    if (!syn) return;
    if (line < 0) line = 0;

    /* Track earliest invalid line */
    if (syn->first_invalid_line < 0 || line < syn->first_invalid_line) {
        syn->first_invalid_line = line;
    }
}

void syntax_update_if_needed(buffer_syntax_t *syn, int lang_id,
                              struct buffer *bp,
                              const char **lines, int line_count) {
    if (!syn || !syn->enabled) return;
    if (syn->first_invalid_line < 0) return;  /* All valid */
    if (lang_id < 0) return;

    /* Re-lex from first invalid line */
    syntax_relex_from(syn, lang_id, bp, lines, line_count, syn->first_invalid_line);

    /* Mark all as valid */
    syn->first_invalid_line = -1;
}

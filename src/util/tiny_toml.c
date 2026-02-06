#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tiny_toml.h"
#include "internal/string_utils.h"  /* safe_strcpy */

/* Skip whitespace */
static const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

/* Trim trailing whitespace/newlines from length */
static size_t trim_end(const char* start, size_t len) {
    while (len > 0 && (isspace(start[len-1]) || start[len-1] == '\r' || start[len-1] == '\n')) {
        len--;
    }
    return len;
}

void toml_parse(const char *src, toml_callback cb, void *user_data) {
    char section[128] = "";
    const char *line_start = src;
    
    while (*line_start) {
        // Find end of line
        const char *line_end = strchr(line_start, '\n');
        if (!line_end) line_end = line_start + strlen(line_start);
        
        // Parse line
        const char *p = line_start;
        p = skip_ws(p);
        
        if (*p && *p != '#') { // Not empty, not comment
            if (*p == '[') {
                // Section header: [section]
                p++;
                const char *end = strchr(p, ']');
                if (end && end < line_end) {
                    size_t len = (size_t)(end - p);
                    if (len >= sizeof(section)) len = sizeof(section) - 1;
                    memcpy(section, p, len);
                    section[len] = '\0';
                }
            } else {
                // Key-Value pair: key = value
                // Handle quoted keys (e.g., "=" = "value")
                const char *eq;
                if (*p == '"' || *p == '\'') {
                    char quote = *p;
                    const char *closing = memchr(p + 1, quote, (size_t)(line_end - p - 1));
                    eq = closing ? strchr(closing + 1, '=') : nullptr;
                } else {
                    eq = strchr(p, '=');
                }
                if (eq && eq < line_end) {
                    // Extract Key
                    const char *key_end = eq - 1;
                    while (key_end > p && isspace(*key_end)) key_end--;
                    key_end++; // Points to char after last key char
                    
                    char key[128];
                    size_t klen = (size_t)(key_end - p);
                    if (klen >= sizeof(key)) klen = sizeof(key) - 1;
                    memcpy(key, p, klen);
                    key[klen] = '\0';
                    
                    // Extract Value
                    const char *val_start = skip_ws(eq + 1);
                    const char *val_end = line_end;

                    // For quoted strings, find the closing quote first, then look for comments
                    // This prevents '#' inside hex colors like "#1C1C1C" from being treated as comments
                    if (*val_start == '"' || *val_start == '\'') {
                        char quote_char = *val_start;
                        const char *closing_quote = memchr(val_start + 1, quote_char, (size_t)(val_end - val_start - 1));
                        if (closing_quote) {
                            // Look for comments only AFTER the closing quote
                            const char *comment = memchr(closing_quote + 1, '#', (size_t)(val_end - closing_quote - 1));
                            if (comment) val_end = comment;
                        }
                    } else {
                        // For non-quoted values, trim comments normally
                        const char *comment = memchr(val_start, '#', (size_t)(val_end - val_start));
                        if (comment) val_end = comment;
                    }

                    size_t vlen = trim_end(val_start, (size_t)(val_end - val_start));
                    
                    if (vlen > 0) {
                        char val_str[256];
                        if (vlen >= sizeof(val_str)) vlen = sizeof(val_str) - 1;
                        memcpy(val_str, val_start, vlen);
                        val_str[vlen] = '\0';
                        
                        enum toml_type type = TOML_STRING;
                        char clean_val[256];
                        
                        if (val_str[0] == '"' || val_str[0] == '\'') {
                            // String: remove quotes
                            type = TOML_STRING;
                            size_t inner_len = vlen >= 2 ? vlen - 2 : 0;
                            if (inner_len >= sizeof(clean_val)) inner_len = sizeof(clean_val) - 1;
                            memcpy(clean_val, val_str + 1, inner_len);
                            clean_val[inner_len] = '\0';
                        } else if (strcmp(val_str, "true") == 0 || strcmp(val_str, "false") == 0) {
                            type = TOML_BOOL;
                            safe_strcpy(clean_val, val_str, sizeof(clean_val));
                        } else {
                            // Assume Int for digits (simplified)
                            type = TOML_INT;
                            safe_strcpy(clean_val, val_str, sizeof(clean_val));
                        }
                        
                        cb(section, key, clean_val, type, user_data);
                    }
                }
            }
        }
        
        if (*line_end == '\0') break;
        line_start = line_end + 1;
    }
}

void toml_write_section(FILE *fp, const char *section) {
    fprintf(fp, "\n[%s]\n", section);
}

void toml_write_string(FILE *fp, const char *key, const char *val) {
    fprintf(fp, "%s = \"%s\"\n", key, val ? val : "");
}

void toml_write_int(FILE *fp, const char *key, int val) {
    fprintf(fp, "%s = %d\n", key, val);
}

void toml_write_bool(FILE *fp, const char *key, int val) {
    fprintf(fp, "%s = %s\n", key, val ? "true" : "false");
}

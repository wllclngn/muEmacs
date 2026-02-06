/*
 * tiny_json.c - Minimal JSON parser for UEP protocol
 *
 * Zero-allocation, callback-based JSON parsing.
 * Patterns after tiny_toml.c.
 *
 * C23 compliant
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "uep/tiny_json.h"
#include "util/logger.h"

#define MAX_VALUE_LEN 4096
#define MAX_DEPTH 32

/* Error codes */
#define JSON_OK          0
#define JSON_ERR_EOF    -1
#define JSON_ERR_SYNTAX -2
#define JSON_ERR_TOOLONG -3
#define JSON_ERR_DEPTH  -4
#define JSON_ERR_ABORT  -5

/* Parser state */
typedef struct {
    const char *p;          /* Current position */
    json_callback cb;       /* User callback */
    void *user_data;        /* Callback context */
    int depth;              /* Current nesting level */
    char value_buf[MAX_VALUE_LEN]; /* Temp buffer for values */
} json_parser_t;

/* Forward declarations */
static int parse_value(json_parser_t *jp, const char *key);

/* Skip whitespace */
static void skip_ws(json_parser_t *jp) {
    while (*jp->p && (*jp->p == ' ' || *jp->p == '\t' ||
                      *jp->p == '\n' || *jp->p == '\r')) {
        jp->p++;
    }
}

/* Parse a string, writing to value_buf */
static int parse_string(json_parser_t *jp) {
    if (*jp->p != '"') return JSON_ERR_SYNTAX;
    jp->p++; /* Skip opening quote */

    size_t len = 0;
    while (*jp->p && *jp->p != '"') {
        if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;

        if (*jp->p == '\\' && jp->p[1]) {
            jp->p++;
            switch (*jp->p) {
                case 'n':  jp->value_buf[len++] = '\n'; break;
                case 'r':  jp->value_buf[len++] = '\r'; break;
                case 't':  jp->value_buf[len++] = '\t'; break;
                case '\\': jp->value_buf[len++] = '\\'; break;
                case '"':  jp->value_buf[len++] = '"';  break;
                case '/':  jp->value_buf[len++] = '/';  break;
                case 'u':
                    /* Unicode escape - simplified: just copy literally */
                    jp->value_buf[len++] = '\\';
                    jp->value_buf[len++] = 'u';
                    break;
                default:
                    jp->value_buf[len++] = *jp->p;
                    break;
            }
        } else {
            jp->value_buf[len++] = *jp->p;
        }
        jp->p++;
    }

    if (*jp->p != '"') return JSON_ERR_EOF;
    jp->p++; /* Skip closing quote */

    jp->value_buf[len] = '\0';
    return JSON_OK;
}

/* Parse a number */
static int parse_number(json_parser_t *jp) {
    size_t len = 0;

    /* Optional minus */
    if (*jp->p == '-') {
        if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
        jp->value_buf[len++] = *jp->p++;
    }

    /* Integer part */
    if (!isdigit((unsigned char)*jp->p)) return JSON_ERR_SYNTAX;
    while (isdigit((unsigned char)*jp->p)) {
        if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
        jp->value_buf[len++] = *jp->p++;
    }

    /* Fractional part */
    if (*jp->p == '.') {
        if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
        jp->value_buf[len++] = *jp->p++;
        while (isdigit((unsigned char)*jp->p)) {
            if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
            jp->value_buf[len++] = *jp->p++;
        }
    }

    /* Exponent */
    if (*jp->p == 'e' || *jp->p == 'E') {
        if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
        jp->value_buf[len++] = *jp->p++;
        if (*jp->p == '+' || *jp->p == '-') {
            if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
            jp->value_buf[len++] = *jp->p++;
        }
        while (isdigit((unsigned char)*jp->p)) {
            if (len >= MAX_VALUE_LEN - 1) return JSON_ERR_TOOLONG;
            jp->value_buf[len++] = *jp->p++;
        }
    }

    jp->value_buf[len] = '\0';
    return JSON_OK;
}

/* Parse an object */
static int parse_object(json_parser_t *jp, const char *parent_key) {
    if (*jp->p != '{') return JSON_ERR_SYNTAX;
    jp->p++; /* Skip opening brace */

    if (jp->depth >= MAX_DEPTH) return JSON_ERR_DEPTH;
    jp->depth++;

    /* Notify callback of object start */
    if (jp->cb && !jp->cb(parent_key, nullptr, JSON_OBJECT, jp->depth - 1, jp->user_data)) {
        return JSON_ERR_ABORT;
    }

    skip_ws(jp);

    /* Empty object */
    if (*jp->p == '}') {
        jp->p++;
        jp->depth--;
        return JSON_OK;
    }

    for (;;) {
        skip_ws(jp);

        /* Parse key */
        int err = parse_string(jp);
        if (err != JSON_OK) return err;

        char key[256];
        size_t klen = strlen(jp->value_buf);
        if (klen >= sizeof(key)) klen = sizeof(key) - 1;
        memcpy(key, jp->value_buf, klen);
        key[klen] = '\0';

        skip_ws(jp);

        /* Expect colon */
        if (*jp->p != ':') return JSON_ERR_SYNTAX;
        jp->p++;

        /* Parse value */
        err = parse_value(jp, key);
        if (err != JSON_OK) return err;

        skip_ws(jp);

        if (*jp->p == '}') {
            jp->p++;
            jp->depth--;
            return JSON_OK;
        }

        if (*jp->p != ',') return JSON_ERR_SYNTAX;
        jp->p++;
    }
}

/* Parse an array */
static int parse_array(json_parser_t *jp, const char *parent_key) {
    if (*jp->p != '[') return JSON_ERR_SYNTAX;
    jp->p++; /* Skip opening bracket */

    if (jp->depth >= MAX_DEPTH) return JSON_ERR_DEPTH;
    jp->depth++;

    /* Notify callback of array start */
    if (jp->cb && !jp->cb(parent_key, nullptr, JSON_ARRAY, jp->depth - 1, jp->user_data)) {
        return JSON_ERR_ABORT;
    }

    skip_ws(jp);

    /* Empty array */
    if (*jp->p == ']') {
        jp->p++;
        jp->depth--;
        return JSON_OK;
    }

    for (;;) {
        /* Parse value (no key for array elements) */
        int err = parse_value(jp, nullptr);
        if (err != JSON_OK) return err;

        skip_ws(jp);

        if (*jp->p == ']') {
            jp->p++;
            jp->depth--;
            return JSON_OK;
        }

        if (*jp->p != ',') return JSON_ERR_SYNTAX;
        jp->p++;
    }
}

/* Parse any JSON value */
static int parse_value(json_parser_t *jp, const char *key) {
    skip_ws(jp);

    if (!*jp->p) return JSON_ERR_EOF;

    int err;
    json_type_t type;

    switch (*jp->p) {
        case '{':
            return parse_object(jp, key);

        case '[':
            return parse_array(jp, key);

        case '"':
            err = parse_string(jp);
            if (err != JSON_OK) return err;
            type = JSON_STRING;
            break;

        case 't':
            if (strncmp(jp->p, "true", 4) == 0) {
                jp->p += 4;
                memcpy(jp->value_buf, "true", 5);  /* includes null terminator */
                type = JSON_BOOL;
            } else {
                return JSON_ERR_SYNTAX;
            }
            break;

        case 'f':
            if (strncmp(jp->p, "false", 5) == 0) {
                jp->p += 5;
                memcpy(jp->value_buf, "false", 6);  /* includes null terminator */
                type = JSON_BOOL;
            } else {
                return JSON_ERR_SYNTAX;
            }
            break;

        case 'n':
            if (strncmp(jp->p, "null", 4) == 0) {
                jp->p += 4;
                jp->value_buf[0] = '\0';
                type = JSON_NULL;
            } else {
                return JSON_ERR_SYNTAX;
            }
            break;

        default:
            if (*jp->p == '-' || isdigit((unsigned char)*jp->p)) {
                err = parse_number(jp);
                if (err != JSON_OK) return err;
                type = JSON_NUMBER;
            } else {
                return JSON_ERR_SYNTAX;
            }
            break;
    }

    /* Notify callback of primitive value */
    if (jp->cb && !jp->cb(key, jp->value_buf, type, jp->depth, jp->user_data)) {
        return JSON_ERR_ABORT;
    }

    return JSON_OK;
}

int json_parse(const char *src, json_callback cb, void *user_data) {
    if (!src) return JSON_ERR_EOF;

    json_parser_t jp = {
        .p = src,
        .cb = cb,
        .user_data = user_data,
        .depth = 0
    };

    LOG_DEBUGF("UEP: JSON parse starting, input length=%zu", strlen(src));

    int err = parse_value(&jp, nullptr);

    if (err != JSON_OK) {
        LOG_ERRORF("UEP: JSON parse error %d at offset %zu", err, (size_t)(jp.p - src));
    }

    return err;
}

const char *json_error_str(int code) {
    switch (code) {
        case JSON_OK:        return "Success";
        case JSON_ERR_EOF:   return "Unexpected end of input";
        case JSON_ERR_SYNTAX: return "Invalid syntax";
        case JSON_ERR_TOOLONG: return "Value too long";
        case JSON_ERR_DEPTH: return "Nesting too deep";
        case JSON_ERR_ABORT: return "Parsing aborted";
        default:             return "Unknown error";
    }
}

/* JSON building helpers */

int json_build_start(char *restrict buf, size_t bufsize) {
    if (bufsize < 2) return -1;
    buf[0] = '{';
    buf[1] = '\0';
    return 0;
}

/* Escape a string for JSON */
static size_t json_escape_string(char *dst, size_t dstsize, const char *src) {
    size_t di = 0;

    for (const char *s = src; *s && di < dstsize - 1; s++) {
        switch (*s) {
            case '"':
                if (di + 2 >= dstsize) goto done;
                dst[di++] = '\\';
                dst[di++] = '"';
                break;
            case '\\':
                if (di + 2 >= dstsize) goto done;
                dst[di++] = '\\';
                dst[di++] = '\\';
                break;
            case '\n':
                if (di + 2 >= dstsize) goto done;
                dst[di++] = '\\';
                dst[di++] = 'n';
                break;
            case '\r':
                if (di + 2 >= dstsize) goto done;
                dst[di++] = '\\';
                dst[di++] = 'r';
                break;
            case '\t':
                if (di + 2 >= dstsize) goto done;
                dst[di++] = '\\';
                dst[di++] = 't';
                break;
            default:
                if ((unsigned char)*s < 0x20) {
                    /* Control character - escape as \u00XX */
                    if (di + 6 >= dstsize) goto done;
                    di += (size_t)snprintf(dst + di, dstsize - di, "\\u%04x", (unsigned char)*s);
                } else {
                    dst[di++] = *s;
                }
                break;
        }
    }

done:
    dst[di] = '\0';
    return di;
}

int json_build_string(char *restrict buf, size_t bufsize, const char *restrict key, const char *restrict value) {
    size_t len = strlen(buf);
    bool needs_comma = (len > 1 && buf[len - 1] != '{');

    /* Escape key and value */
    char escaped_key[256];
    char escaped_value[MAX_VALUE_LEN];
    json_escape_string(escaped_key, sizeof(escaped_key), key);
    json_escape_string(escaped_value, sizeof(escaped_value), value ? value : "");

    int written = snprintf(buf + len, bufsize - len,
                          "%s\"%s\":\"%s\"",
                          needs_comma ? "," : "",
                          escaped_key,
                          escaped_value);

    if (written < 0 || (size_t)written >= bufsize - len) return -1;
    return 0;
}

int json_build_int(char *restrict buf, size_t bufsize, const char *restrict key, int value) {
    size_t len = strlen(buf);
    bool needs_comma = (len > 1 && buf[len - 1] != '{');

    char escaped_key[256];
    json_escape_string(escaped_key, sizeof(escaped_key), key);

    int written = snprintf(buf + len, bufsize - len,
                          "%s\"%s\":%d",
                          needs_comma ? "," : "",
                          escaped_key,
                          value);

    if (written < 0 || (size_t)written >= bufsize - len) return -1;
    return 0;
}

int json_build_bool(char *restrict buf, size_t bufsize, const char *restrict key, bool value) {
    size_t len = strlen(buf);
    bool needs_comma = (len > 1 && buf[len - 1] != '{');

    char escaped_key[256];
    json_escape_string(escaped_key, sizeof(escaped_key), key);

    int written = snprintf(buf + len, bufsize - len,
                          "%s\"%s\":%s",
                          needs_comma ? "," : "",
                          escaped_key,
                          value ? "true" : "false");

    if (written < 0 || (size_t)written >= bufsize - len) return -1;
    return 0;
}

int json_build_end(char *restrict buf, size_t bufsize) {
    size_t len = strlen(buf);
    if (len + 2 > bufsize) return -1;
    buf[len] = '}';
    buf[len + 1] = '\0';
    return 0;
}

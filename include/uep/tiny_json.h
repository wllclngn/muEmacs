/*
 * tiny_json.h - Minimal JSON parser for UEP protocol
 *
 * Zero-allocation, callback-based JSON parsing.
 * Designed for parsing UEP provider responses.
 *
 * C23 compliant
 */

#ifndef TINY_JSON_H
#define TINY_JSON_H

#include <stdbool.h>
#include <stddef.h>

/* JSON Value Types */
typedef enum {
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_NULL,
    JSON_OBJECT,
    JSON_ARRAY
} json_type_t;

/*
 * Callback function signature
 *
 * key: key name (may be nullptr for array elements)
 * value: string representation of value (for primitives)
 *        For objects/arrays, value is nullptr
 * type: detected type
 * depth: nesting level (0 = root)
 * user_data: passed through from json_parse()
 *
 * Return true to continue parsing, false to abort
 */
typedef bool (*json_callback)(const char *key, const char *value,
                              json_type_t type, int depth, void *user_data);

/*
 * Parse a JSON string and trigger callbacks for each value.
 *
 * Returns 0 on success, negative on error:
 *   -1: Unexpected end of input
 *   -2: Invalid syntax
 *   -3: String too long (> 4KB values)
 *   -4: Nesting too deep (> 32 levels)
 *   -5: Callback requested abort
 */
int json_parse(const char *src, json_callback cb, void *user_data);

/*
 * Helper: Get error description for json_parse return code
 */
const char *json_error_str(int code);

/*
 * JSON building helpers for requests
 */

/* Start building a JSON object into buffer */
int json_build_start(char *buf, size_t bufsize);

/* Add a string field: "key": "value" */
int json_build_string(char *buf, size_t bufsize, const char *key, const char *value);

/* Add an integer field: "key": 123 */
int json_build_int(char *buf, size_t bufsize, const char *key, int value);

/* Add a boolean field: "key": true/false */
int json_build_bool(char *buf, size_t bufsize, const char *key, bool value);

/* Close the JSON object */
int json_build_end(char *buf, size_t bufsize);

#endif /* TINY_JSON_H */

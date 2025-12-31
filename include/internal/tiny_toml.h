#ifndef TINY_TOML_H
#define TINY_TOML_H

#include <stdbool.h>

/* TOML Value Types */
enum toml_type {
    TOML_STRING,
    TOML_INT,
    TOML_BOOL
};

/* Callback function signature 
 * section: current [section] name (or empty string for root)
 * key: key name
 * value: string representation of value
 * type: detected type
 * user_data: passed through
 */
typedef void (*toml_callback)(const char *section, const char *key, const char *value, enum toml_type type, void *user_data);

/* Parse a TOML string and trigger callbacks for each key-value pair */
void toml_parse(const char *src, toml_callback cb, void *user_data);

/* Helper to write a string value safely (escaping quotes) to a file */
void toml_write_string(FILE *fp, const char *key, const char *val);
void toml_write_int(FILE *fp, const char *key, int val);
void toml_write_bool(FILE *fp, const char *key, int val);
void toml_write_section(FILE *fp, const char *section);

#endif /* TINY_TOML_H */

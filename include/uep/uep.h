/*
 * uep.h - uEmacs Extension Protocol
 *
 * JSON-based protocol for external tool integration.
 * Providers receive JSON on stdin, return JSON on stdout.
 *
 * C23 compliant
 */

#ifndef UEP_H
#define UEP_H

#include <stdbool.h>
#include <stddef.h>

/* Forward declarations */
struct buffer;

/* Protocol version */
#define UEP_VERSION 1

/* Request structure - sent to provider */
typedef struct {
    int version;              /* Protocol version (always UEP_VERSION) */
    const char *command;      /* Command: "format", "lint", "complete", etc. */
    const char *buffer;       /* Full buffer contents */
    size_t buffer_len;        /* Length of buffer */
    const char *filename;     /* File path (may be empty) */
    const char *filetype;     /* Detected filetype: "c", "python", etc. */
    int cursor_line;          /* 1-based line number */
    int cursor_col;           /* 1-based column number */
} uep_request_t;

/* Response structure - received from provider */
typedef struct {
    bool ok;                  /* true if operation succeeded */
    char *error;              /* Error message (if !ok) */
    char *buffer;             /* Result buffer (for format: new content) */
    size_t buffer_len;        /* Length of result buffer */
    /* Future: diagnostics array, completions, etc. */
} uep_response_t;

/*
 * Build a JSON request string from a request structure.
 *
 * Returns: Allocated string (caller must free), or nullptr on error.
 */
char *uep_build_request(const uep_request_t *req);

/*
 * Call a UEP provider synchronously.
 *
 * Spawns the provider command, writes request to stdin, reads response.
 *
 * provider_cmd: Shell command to execute (e.g., "black -q -")
 * req: Request to send
 *
 * Returns: Allocated response (caller must free with uep_response_free),
 *          or nullptr on catastrophic failure.
 */
uep_response_t *uep_call(const char *provider_cmd, const uep_request_t *req);

/*
 * Free a UEP response.
 */
void uep_response_free(uep_response_t *resp);

/*
 * Get buffer contents as a contiguous string.
 *
 * Returns: Allocated string (caller must free), or nullptr on error.
 *          Sets *len to the length of the string.
 */
char *uep_get_buffer_contents(struct buffer *bp, size_t *len);

/*
 * Replace buffer contents with new text.
 *
 * Returns: true on success, false on error.
 */
bool uep_replace_buffer_contents(struct buffer *bp, const char *text, size_t len);

/*
 * Initialize UEP subsystem.
 * Called once at startup.
 */
void uep_init(void);

/*
 * Cleanup UEP subsystem.
 * Called at shutdown.
 */
void uep_cleanup(void);

#endif /* UEP_H */

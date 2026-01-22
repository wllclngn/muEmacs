/*
 * uep.c - uEmacs Extension Protocol Core Implementation
 *
 * Handles provider communication via pipe I/O.
 * Pattern after spawn.c:filter_direct().
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <poll.h>

#include "estruct.h"
#include "edef.h"
#include "uep/uep.h"
#include "uep/uep_providers.h"
#include "uep/tiny_json.h"
#include "internal/memory.h"
#include "internal/line.h"
#include "util/logger.h"

/* Maximum request/response size */
#define UEP_MAX_REQUEST (16 * 1024 * 1024)   /* 16 MB */
#define UEP_MAX_RESPONSE (16 * 1024 * 1024)  /* 16 MB */

/* Build a JSON request string */
char *uep_build_request(const uep_request_t *req) {
    if (!req) return nullptr;

    /* Calculate approximate size needed */
    size_t est_size = 256 + req->buffer_len * 2 +
                      strlen(req->filename ? req->filename : "") +
                      strlen(req->filetype ? req->filetype : "") +
                      strlen(req->command ? req->command : "");

    if (est_size > UEP_MAX_REQUEST) {
        LOG_ERRORF("UEP: Request too large (%zu bytes)", est_size);
        return nullptr;
    }

    char *buf = SAFE_ALLOC_SIZED(char, est_size, "uep request buffer");
    if (!buf) return nullptr;

    /* Build JSON request */
    if (json_build_start(buf, est_size) < 0) goto fail;
    if (json_build_int(buf, est_size, "version", req->version) < 0) goto fail;
    if (json_build_string(buf, est_size, "command", req->command ? req->command : "") < 0) goto fail;
    if (json_build_string(buf, est_size, "buffer", req->buffer ? req->buffer : "") < 0) goto fail;
    if (json_build_string(buf, est_size, "filename", req->filename ? req->filename : "") < 0) goto fail;
    if (json_build_string(buf, est_size, "filetype", req->filetype ? req->filetype : "") < 0) goto fail;
    if (json_build_int(buf, est_size, "cursor_line", req->cursor_line) < 0) goto fail;
    if (json_build_int(buf, est_size, "cursor_col", req->cursor_col) < 0) goto fail;
    if (json_build_end(buf, est_size) < 0) goto fail;

    LOG_DEBUGF("UEP: Built request, %zu bytes", strlen(buf));
    return buf;

fail:
    SAFE_FREE(buf);
    LOG_ERROR("UEP: Failed to build JSON request");
    return nullptr;
}

/* Response parser context */
typedef struct {
    uep_response_t *resp;
    int in_root;
    char current_key[128];
} uep_parse_ctx_t;

/* JSON parse callback for response */
static bool uep_response_callback(const char *key, const char *value,
                                  json_type_t type, int depth,
                                  void *user_data) {
    uep_parse_ctx_t *ctx = user_data;

    /* Only process root-level keys */
    if (depth != 1) return true;

    /* Track current key for object start */
    if (key) {
        strncpy(ctx->current_key, key, sizeof(ctx->current_key) - 1);
        ctx->current_key[sizeof(ctx->current_key) - 1] = '\0';
    }

    /* Parse based on key */
    if (key && value) {
        if (strcmp(key, "ok") == 0 && type == JSON_BOOL) {
            ctx->resp->ok = (strcmp(value, "true") == 0);
        } else if (strcmp(key, "error") == 0 && type == JSON_STRING) {
            ctx->resp->error = SAFE_STRDUP(value, "uep response error");
        } else if (strcmp(key, "buffer") == 0 && type == JSON_STRING) {
            ctx->resp->buffer = SAFE_STRDUP(value, "uep response buffer");
            if (ctx->resp->buffer) {
                ctx->resp->buffer_len = strlen(ctx->resp->buffer);
            }
        }
    }

    return true;
}

/* Call provider command with pipe I/O (pattern from spawn.c:filter_direct) */
static int uep_pipe_call(const char *cmd, const char *input, size_t input_len,
                         char **output, size_t *output_len) {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    pid_t pid;
    int status = -1;

    *output = nullptr;
    *output_len = 0;

    LOG_DEBUGF("UEP: Calling provider: %s", cmd);

    /* Create pipes */
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        LOG_ERROR("UEP: Failed to create pipes");
        goto cleanup;
    }

    /* Set close-on-exec for pipe ends */
    fcntl(stdin_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdin_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[1], F_SETFD, FD_CLOEXEC);

    pid = fork();
    if (pid < 0) {
        LOG_ERROR("UEP: Fork failed");
        goto cleanup;
    }

    if (pid == 0) {
        /* Child process */

        /* Redirect stdin from pipe */
        dup2(stdin_pipe[0], STDIN_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);

        /* Redirect stdout to pipe */
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        /* Get shell */
        const char *shell = getenv("SHELL");
        if (!shell || !*shell) shell = "/bin/sh";

        /* Execute command */
        execlp(shell, shell, "-c", cmd, (char *)nullptr);
        _exit(127);
    }

    /* Parent process */

    /* Close unused pipe ends */
    close(stdin_pipe[0]);
    stdin_pipe[0] = -1;
    close(stdout_pipe[1]);
    stdout_pipe[1] = -1;

    /* Write input to child's stdin */
    size_t written = 0;
    while (written < input_len) {
        ssize_t n = write(stdin_pipe[1], input + written, input_len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_WARNF("UEP: Write to provider failed at %zu/%zu bytes", written, input_len);
            break;  /* Write error - child may have closed stdin */
        }
        written += n;
    }
    close(stdin_pipe[1]);
    stdin_pipe[1] = -1;

    LOG_DEBUGF("UEP: Wrote %zu bytes to provider", written);

    /* Read output from child's stdout with timeout */
    int timeout_ms = uep_get_timeout();
    size_t out_capacity = 4096;
    size_t out_len = 0;
    char *out_buf = SAFE_ALLOC_SIZED(char, out_capacity, "uep output buffer");
    if (!out_buf) {
        waitpid(pid, &status, 0);
        goto cleanup;
    }

    struct pollfd pfd = { .fd = stdout_pipe[0], .events = POLLIN };

    for (;;) {
        /* Check for data with timeout */
        int poll_ret = poll(&pfd, 1, timeout_ms);
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("UEP: Poll error");
            break;
        }
        if (poll_ret == 0) {
            LOG_WARN("UEP: Provider timeout");
            break;  /* Timeout */
        }

        /* Grow buffer if needed */
        if (out_len + 4096 > out_capacity) {
            if (out_capacity * 2 > UEP_MAX_RESPONSE) {
                LOG_ERROR("UEP: Response too large");
                break;
            }
            out_capacity *= 2;
            char *new_buf = SAFE_REALLOC(out_buf, out_capacity, "uep output buffer grow");
            if (!new_buf) break;
            out_buf = new_buf;
        }

        ssize_t n = read(stdout_pipe[0], out_buf + out_len, 4096);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("UEP: Read error from provider");
            break;
        }
        if (n == 0) break;  /* EOF */
        out_len += n;
    }

    close(stdout_pipe[0]);
    stdout_pipe[0] = -1;

    /* Wait for child */
    waitpid(pid, &status, 0);

    LOG_DEBUGF("UEP: Provider returned %zu bytes, exit status %d",
               out_len, WIFEXITED(status) ? WEXITSTATUS(status) : -1);

    /* Null-terminate for convenience */
    if (out_len + 1 > out_capacity) {
        char *new_buf = SAFE_REALLOC(out_buf, out_len + 1, "uep output terminate");
        if (new_buf) out_buf = new_buf;
    }
    out_buf[out_len] = '\0';

    *output = out_buf;
    *output_len = out_len;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;

cleanup:
    if (stdin_pipe[0] >= 0) close(stdin_pipe[0]);
    if (stdin_pipe[1] >= 0) close(stdin_pipe[1]);
    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    return -1;
}

/* Call a UEP provider */
uep_response_t *uep_call(const char *provider_cmd, const uep_request_t *req) {
    if (!provider_cmd || !req) return nullptr;

    /* Allocate response */
    uep_response_t *resp = SAFE_ALLOC(uep_response_t, "uep response");
    if (!resp) return nullptr;

    /* Build request JSON */
    char *request_json = uep_build_request(req);
    if (!request_json) {
        resp->ok = false;
        resp->error = SAFE_STRDUP("Failed to build request", "uep error");
        return resp;
    }

    /* Call provider */
    char *output = nullptr;
    size_t output_len = 0;
    int exit_code = uep_pipe_call(provider_cmd, request_json, strlen(request_json),
                                  &output, &output_len);
    SAFE_FREE(request_json);

    if (exit_code < 0 || !output) {
        resp->ok = false;
        resp->error = SAFE_STRDUP("Provider call failed", "uep error");
        SAFE_FREE(output);
        return resp;
    }

    /* For simple format providers that just output text (not JSON),
     * treat any output as the formatted buffer */
    if (output_len > 0 && output[0] != '{') {
        /* Not JSON - treat as raw formatted output */
        resp->ok = (exit_code == 0);
        if (resp->ok) {
            resp->buffer = output;
            resp->buffer_len = output_len;
        } else {
            resp->error = SAFE_STRDUP(output, "uep error from output");
            SAFE_FREE(output);
        }
        return resp;
    }

    /* Parse JSON response */
    uep_parse_ctx_t ctx = { .resp = resp, .in_root = 0 };
    int parse_result = json_parse(output, uep_response_callback, &ctx);

    if (parse_result != 0) {
        LOG_ERRORF("UEP: Failed to parse response: %s", json_error_str(parse_result));
        /* If JSON parse fails but we got output, use it as error */
        if (!resp->error) {
            resp->error = SAFE_STRDUP(output_len > 0 ? output : "Invalid response", "uep parse error");
        }
        resp->ok = false;
    }

    SAFE_FREE(output);
    return resp;
}

/* Free a UEP response */
void uep_response_free(uep_response_t *resp) {
    if (!resp) return;
    SAFE_FREE(resp->error);
    SAFE_FREE(resp->buffer);
    SAFE_FREE(resp);
}

/* Get buffer contents as contiguous string */
char *uep_get_buffer_contents(struct buffer *bp, size_t *len) {
    if (!bp || !len) return nullptr;

    /* Calculate total size */
    size_t total = 0;
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        total += llength(lp) + 1;  /* +1 for newline */
        lp = lforw(lp);
    }

    if (total == 0) {
        *len = 0;
        return SAFE_STRDUP("", "uep empty buffer");
    }

    /* Allocate buffer */
    char *buf = SAFE_ALLOC_SIZED(char, total + 1, "uep buffer contents");
    if (!buf) return nullptr;

    /* Copy lines */
    size_t pos = 0;
    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        int line_len = llength(lp);
        for (int i = 0; i < line_len; i++) {
            buf[pos++] = lgetc(lp, i);
        }
        buf[pos++] = '\n';
        lp = lforw(lp);
    }

    /* Remove trailing newline if buffer didn't have one */
    if (pos > 0 && buf[pos - 1] == '\n') {
        pos--;
    }

    buf[pos] = '\0';
    *len = pos;
    return buf;
}

/* Replace buffer contents with new text */
bool uep_replace_buffer_contents(struct buffer *bp, const char *text, size_t len) {
    if (!bp || !text) return false;

    LOG_DEBUGF("UEP: Replacing buffer contents, %zu bytes", len);

    /* Save current position for later restoration attempt */
    struct line *saved_dotp = bp->b_dotp;
    int saved_doto = bp->b_doto;
    long saved_line = getlinenum(bp, saved_dotp);

    /* Delete all buffer contents */
    struct line *lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        struct line *next = lforw(lp);
        lfree(lp);
        lp = next;
    }

    /* Reset buffer pointers */
    bp->b_linep->l_fp = bp->b_linep;
    bp->b_linep->l_bp = bp->b_linep;
    bp->b_dotp = bp->b_linep;
    bp->b_doto = 0;
    bp->b_markp = nullptr;
    bp->b_marko = 0;

    /* Insert new text */
    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        /* Find end of line */
        const char *eol = memchr(p, '\n', end - p);
        if (!eol) eol = end;

        /* Allocate new line */
        int line_len = (int)(eol - p);
        struct line *newlp = lalloc(line_len);
        if (!newlp) {
            LOG_ERROR("UEP: Failed to allocate line");
            return false;
        }

        /* Copy text to line */
        for (int i = 0; i < line_len; i++) {
            lputc(newlp, i, p[i]);
        }

        /* Link line before header (at end of buffer) */
        struct line *prev = lback(bp->b_linep);
        newlp->l_fp = bp->b_linep;
        newlp->l_bp = prev;
        prev->l_fp = newlp;
        bp->b_linep->l_bp = newlp;

        p = (eol < end) ? eol + 1 : end;
    }

    /* Set dot to first line */
    bp->b_dotp = lforw(bp->b_linep);
    bp->b_doto = 0;

    /* Try to restore approximate position */
    if (saved_line > 0) {
        lp = lforw(bp->b_linep);
        for (long i = 1; i < saved_line && lp != bp->b_linep; i++) {
            lp = lforw(lp);
        }
        if (lp != bp->b_linep) {
            bp->b_dotp = lp;
            bp->b_doto = (saved_doto < llength(lp)) ? saved_doto : llength(lp);
        }
    }

    /* Mark buffer as modified */
    bp->b_flag |= BFCHG;
    bp->b_stats_dirty = true;

    LOG_INFO("UEP: Buffer contents replaced");
    return true;
}

/* Initialize UEP subsystem */
void uep_init(void) {
    LOG_INFO("UEP: Subsystem initialized");
    uep_set_timeout(5000);  /* Default 5 second timeout */
}

/* Cleanup UEP subsystem */
void uep_cleanup(void) {
    uep_clear_providers();
    LOG_INFO("UEP: Subsystem cleaned up");
}

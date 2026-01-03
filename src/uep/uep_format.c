/*
 * uep_format.c - UEP Format Command
 *
 * M-x uep-format-buffer command implementation.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <string.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/uep.h"
#include "uep/uep_providers.h"
#include "internal/line.h"
#include "util/logger.h"

/*
 * uep-format-buffer command
 *
 * Format the current buffer using the configured format provider
 * for the buffer's filetype.
 */
int uep_format_buffer([[maybe_unused]] int f, [[maybe_unused]] int n) {
    /* Detect filetype from filename */
    const char *filetype = uep_detect_filetype(curbp->b_fname);

    LOG_INFOF("UEP: Format requested for filetype '%s' (%s)", filetype, curbp->b_fname);

    /* Get provider command */
    const char *provider = uep_get_provider(filetype, "format");
    if (!provider || !*provider) {
        mlwrite("[No format provider for %s]", filetype);
        LOG_WARNF("UEP: No format provider for %s", filetype);
        return false;
    }

    /* Get buffer contents */
    size_t buf_len;
    char *buf_contents = uep_get_buffer_contents(curbp, &buf_len);
    if (!buf_contents) {
        mlwrite("[Failed to get buffer contents]");
        LOG_ERROR("UEP: Failed to get buffer contents");
        return false;
    }

    /* Show working message */
    mlwrite("[Formatting with %s...]", provider);

    /* Get cursor position for context */
    long cursor_line = getlinenum(curbp, curwp->w_dotp);
    int cursor_col = curwp->w_doto + 1;  /* 1-based */

    /* Build request */
    uep_request_t req = {
        .version = UEP_VERSION,
        .command = "format",
        .buffer = buf_contents,
        .buffer_len = buf_len,
        .filename = curbp->b_fname,
        .filetype = filetype,
        .cursor_line = (int)cursor_line,
        .cursor_col = cursor_col
    };

    /* Call provider */
    uep_response_t *resp = uep_call(provider, &req);
    free(buf_contents);

    if (!resp) {
        mlwrite("[Format failed: no response]");
        LOG_ERROR("UEP: Format call returned null");
        return false;
    }

    if (!resp->ok) {
        mlwrite("[Format failed: %s]", resp->error ? resp->error : "unknown error");
        LOG_ERRORF("UEP: Format failed: %s", resp->error ? resp->error : "unknown");
        uep_response_free(resp);
        return false;
    }

    if (!resp->buffer || resp->buffer_len == 0) {
        mlwrite("[Format returned empty result]");
        LOG_WARN("UEP: Format returned empty buffer");
        uep_response_free(resp);
        return false;
    }

    /* Replace buffer contents */
    if (!uep_replace_buffer_contents(curbp, resp->buffer, resp->buffer_len)) {
        mlwrite("[Failed to update buffer]");
        LOG_ERROR("UEP: Failed to replace buffer contents");
        uep_response_free(resp);
        return false;
    }

    mlwrite("[Formatted with %s]", provider);
    LOG_INFO("UEP: Format complete");

    /* Mark window for redisplay */
    curwp->w_flag |= WFHARD;
    sgarbf = true;

    uep_response_free(resp);
    return true;
}

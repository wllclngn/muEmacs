/* clipboard.c - Unified clipboard abstraction for μEmacs
 *
 * Multiple provider support with auto-detection and TOML configuration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <spawn.h>

#include "estruct.h"
#include "edef.h"
#include "clipboard.h"
#include "string_utils.h"
#include "memory.h"
#include "util/logger.h"

/*
 * run_shell_cmd - Safe replacement for system() using posix_spawn()
 *
 * Spawns /bin/sh -c "command" and waits for completion.
 * Returns 0 on success, non-zero on failure.
 */
static int run_shell_cmd(const char *cmd)
{
    pid_t pid;
    int status;
    char *argv[] = {"/bin/sh", "-c", (char *)cmd, nullptr};

    if (posix_spawn(&pid, "/bin/sh", nullptr, nullptr, argv, environ) != 0) {
        return -1;
    }

    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Global clipboard configuration */
clipboard_config_t g_clipboard_config = {
    .provider = CLIP_AUTO,
    .detected = CLIP_NONE,
    .sync_kills = false,
    .osc52_enabled = true,
    .copy_cmd = {0},
    .paste_cmd = {0}
};

/* Base64 encoding table */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64 encode text into output buffer
 * Returns: length of encoded string, or -1 on error
 */
static int base64_encode(const char *input, size_t input_len,
                         char *output, size_t output_size) {
    size_t needed = ((input_len + 2) / 3) * 4 + 1;
    if (output_size < needed) return -1;

    size_t out_pos = 0;
    for (size_t i = 0; i < input_len; i += 3) {
        unsigned int val = 0;

        for (size_t j = i; j < i + 3 && j < input_len; j++) {
            val = (val << 8) | (unsigned char)input[j];
        }

        /* Pad remaining bits */
        int remain = (int)(input_len - i);
        if (remain == 1) val <<= 16;
        else if (remain == 2) val <<= 8;

        /* Encode 4 characters */
        output[out_pos++] = b64_table[(val >> 18) & 0x3F];
        output[out_pos++] = b64_table[(val >> 12) & 0x3F];
        output[out_pos++] = (remain > 1) ? b64_table[(val >> 6) & 0x3F] : '=';
        output[out_pos++] = (remain > 2) ? b64_table[val & 0x3F] : '=';
    }

    output[out_pos] = '\0';
    return (int)out_pos;
}

/* Check if a command exists in PATH */
static bool command_exists(const char *cmd) {
    char path[512];
    snprintf(path, sizeof(path), "command -v %s >/dev/null 2>&1", cmd);
    return run_shell_cmd(path) == 0;
}

/* OSC 52 clipboard set - works over SSH, tmux, etc.
 * Format: ESC ] 52 ; c ; <base64-data> BEL
 *
 * The terminal intercepts this and sets the system clipboard.
 * Supported by: kitty, iTerm2, alacritty, foot, Windows Terminal
 */
static int osc52_set(const char *text, size_t len) {
    /* Calculate base64 size needed */
    size_t b64_len = ((len + 2) / 3) * 4 + 1;
    char *b64 = SAFE_ALLOC_SIZED(char, b64_len, "osc52_base64");
    if (!b64) return -1;

    if (base64_encode(text, len, b64, b64_len) < 0) {
        SAFE_FREE(b64);
        return -1;
    }

    /* Write OSC 52 sequence directly to terminal
     * Format: \033]52;c;<base64>\007
     *
     * Note: We bypass the terminal abstraction here since OSC 52
     * must go directly to the terminal, not through our display buffer.
     */
    char header[] = "\033]52;c;";
    char trailer[] = "\007";

    /* Write atomically if possible */
    size_t total = sizeof(header) - 1 + strlen(b64) + sizeof(trailer) - 1;
    char *buf = SAFE_ALLOC_SIZED(char, total + 1, "osc52_buffer");
    if (!buf) {
        SAFE_FREE(b64);
        return -1;
    }

    snprintf(buf, total + 1, "%s%s%s", header, b64, trailer);
    ssize_t written = write(STDOUT_FILENO, buf, total);

    SAFE_FREE(buf);
    SAFE_FREE(b64);

    LOG_DEBUGF("CLIPBOARD: OSC 52 set %zu bytes", len);
    return (written == (ssize_t)total) ? 0 : -1;
}

/* OSC 52 clipboard get - less reliable than set
 * Some terminals support responding with clipboard contents
 * Format: ESC ] 52 ; c ; ? BEL
 * Response: ESC ] 52 ; c ; <base64-data> BEL
 */
static int osc52_get(char *buf, size_t maxlen) {
    /* OSC 52 get is unreliable - many terminals don't support it
     * Fall back to external tools for paste operations
     */
    (void)buf;
    (void)maxlen;
    LOG_DEBUG("CLIPBOARD: OSC 52 get not implemented (unreliable)");
    return -1;
}

/* Run external command with pipe for clipboard operations */
static int run_clipboard_cmd_set(const char *cmd, const char *text, size_t len) {
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) return -1;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    /* Redirect stdin from pipe */
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    /* Suppress stderr */
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    char *argv[] = {"/bin/sh", "-c", (char *)cmd, nullptr};

    if (posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[0]);

    /* Write text to clipboard process */
    ssize_t written = write(pipefd[1], text, len);
    close(pipefd[1]);

    int status;
    waitpid(pid, &status, 0);

    return (written == (ssize_t)len && WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static int run_clipboard_cmd_get(const char *cmd, char *buf, size_t maxlen) {
    int pipefd[2];
    pid_t pid;

    if (pipe(pipefd) == -1) return -1;

    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    /* Redirect stdout to pipe */
    posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    /* Suppress stderr */
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    char *argv[] = {"/bin/sh", "-c", (char *)cmd, nullptr};

    if (posix_spawn(&pid, "/bin/sh", &actions, nullptr, argv, environ) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    posix_spawn_file_actions_destroy(&actions);
    close(pipefd[1]);

    /* Read clipboard content */
    ssize_t total = 0;
    ssize_t n;
    while (total < (ssize_t)maxlen - 1 &&
           (n = read(pipefd[0], buf + total, maxlen - 1 - (size_t)total)) > 0) {
        total += n;
    }
    buf[total] = '\0';

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    /* Remove trailing newline if present */
    if (total > 0 && buf[total - 1] == '\n') {
        buf[--total] = '\0';
    }

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? (int)total : -1;
}

/* Wayland clipboard operations */
static int wl_copy_set(const char *text, size_t len) {
    return run_clipboard_cmd_set("wl-copy", text, len);
}

static int wl_paste_get(char *buf, size_t maxlen) {
    return run_clipboard_cmd_get("wl-paste -n 2>/dev/null", buf, maxlen);
}

/* X11 xclip operations */
static int xclip_set(const char *text, size_t len) {
    return run_clipboard_cmd_set("xclip -selection clipboard", text, len);
}

static int xclip_get(char *buf, size_t maxlen) {
    return run_clipboard_cmd_get("xclip -selection clipboard -o 2>/dev/null", buf, maxlen);
}

/* X11 xsel operations */
static int xsel_set(const char *text, size_t len) {
    return run_clipboard_cmd_set("xsel --clipboard --input", text, len);
}

static int xsel_get(char *buf, size_t maxlen) {
    return run_clipboard_cmd_get("xsel --clipboard --output 2>/dev/null", buf, maxlen);
}

/* Detect terminal OSC 52 support (best-effort heuristic) */
bool clipboard_osc52_supported(void) {
    const char *term_env = getenv("TERM");
    if (!term_env) return false;

    /* Terminals known to support OSC 52 */
    if (strstr(term_env, "kitty") ||
        strstr(term_env, "alacritty") ||
        strstr(term_env, "foot") ||
        strstr(term_env, "xterm") ||
        strstr(term_env, "tmux") ||
        strstr(term_env, "screen")) {
        return true;
    }

    /* Check for explicit support indicator */
    const char *colorterm = getenv("COLORTERM");
    if (colorterm && strcmp(colorterm, "truecolor") == 0) {
        /* Modern truecolor terminals usually support OSC 52 */
        return true;
    }

    /* Check for kitty/alacritty-specific env vars */
    if (getenv("KITTY_WINDOW_ID") || getenv("ALACRITTY_LOG")) {
        return true;
    }

    return false;
}

/* Auto-detect best available provider */
static clipboard_provider_t detect_provider(void) {
    /* Priority order:
     * 1. OSC 52 if enabled and terminal supports it
     * 2. wl-copy if $WAYLAND_DISPLAY set
     * 3. xclip if available
     * 4. xsel if available
     * 5. none
     */

    if (g_clipboard_config.osc52_enabled && clipboard_osc52_supported()) {
        LOG_DEBUG("CLIPBOARD: Auto-detected OSC 52");
        return CLIP_OSC52;
    }

    if (getenv("WAYLAND_DISPLAY") && command_exists("wl-copy")) {
        LOG_DEBUG("CLIPBOARD: Auto-detected wl-copy (Wayland)");
        return CLIP_WL_COPY;
    }

    if (getenv("DISPLAY")) {
        if (command_exists("xclip")) {
            LOG_DEBUG("CLIPBOARD: Auto-detected xclip (X11)");
            return CLIP_XCLIP;
        }
        if (command_exists("xsel")) {
            LOG_DEBUG("CLIPBOARD: Auto-detected xsel (X11)");
            return CLIP_XSEL;
        }
    }

    /* Fallback: try OSC 52 even if we're not sure the terminal supports it */
    if (g_clipboard_config.osc52_enabled) {
        LOG_DEBUG("CLIPBOARD: Fallback to OSC 52 (may not work)");
        return CLIP_OSC52;
    }

    LOG_DEBUG("CLIPBOARD: No provider available");
    return CLIP_NONE;
}

/* Initialize clipboard subsystem */
int clipboard_init(void) {
    LOG_DEBUG("CLIPBOARD: Initializing clipboard subsystem");

    if (g_clipboard_config.provider == CLIP_AUTO) {
        g_clipboard_config.detected = detect_provider();
    } else {
        g_clipboard_config.detected = g_clipboard_config.provider;
    }

    LOG_DEBUGF("CLIPBOARD: Using provider %s", clipboard_provider_name());
    return 0;
}

/* Shutdown clipboard subsystem */
void clipboard_cleanup(void) {
    /* Nothing to cleanup currently */
}

/* Copy text to system clipboard */
int clipboard_set(const char *text, size_t len) {
    if (!text || len == 0) return 0;

    clipboard_provider_t provider =
        (g_clipboard_config.provider == CLIP_AUTO)
            ? g_clipboard_config.detected
            : g_clipboard_config.provider;

    switch (provider) {
        case CLIP_OSC52:
            return osc52_set(text, len);

        case CLIP_WL_COPY:
            /* For wl-copy, fall back to xclip/OSC52 if it fails */
            if (wl_copy_set(text, len) == 0) return 0;
            /* Fallthrough to try X11 tools (hybrid sessions) */
            if (command_exists("xclip")) return xclip_set(text, len);
            if (command_exists("xsel")) return xsel_set(text, len);
            return -1;

        case CLIP_XCLIP:
            return xclip_set(text, len);

        case CLIP_XSEL:
            return xsel_set(text, len);

        case CLIP_CUSTOM:
            if (g_clipboard_config.copy_cmd[0]) {
                return run_clipboard_cmd_set(g_clipboard_config.copy_cmd, text, len);
            }
            return -1;

        case CLIP_NONE:
        case CLIP_AUTO:
        default:
            return -1;
    }
}

/* Paste text from system clipboard */
int clipboard_get(char *buf, size_t maxlen) {
    if (!buf || maxlen == 0) return -1;

    clipboard_provider_t provider =
        (g_clipboard_config.provider == CLIP_AUTO)
            ? g_clipboard_config.detected
            : g_clipboard_config.provider;

    switch (provider) {
        case CLIP_OSC52:
            /* OSC 52 get is unreliable, try external tools first */
            if (getenv("WAYLAND_DISPLAY") && command_exists("wl-paste")) {
                int ret = wl_paste_get(buf, maxlen);
                if (ret >= 0) return ret;
            }
            if (getenv("DISPLAY")) {
                if (command_exists("xclip")) {
                    int ret = xclip_get(buf, maxlen);
                    if (ret >= 0) return ret;
                }
                if (command_exists("xsel")) {
                    int ret = xsel_get(buf, maxlen);
                    if (ret >= 0) return ret;
                }
            }
            return osc52_get(buf, maxlen);

        case CLIP_WL_COPY:
            return wl_paste_get(buf, maxlen);

        case CLIP_XCLIP:
            return xclip_get(buf, maxlen);

        case CLIP_XSEL:
            return xsel_get(buf, maxlen);

        case CLIP_CUSTOM:
            if (g_clipboard_config.paste_cmd[0]) {
                return run_clipboard_cmd_get(g_clipboard_config.paste_cmd, buf, maxlen);
            }
            return -1;

        case CLIP_NONE:
        case CLIP_AUTO:
        default:
            return -1;
    }
}

/* Get name of current provider */
const char *clipboard_provider_name(void) {
    clipboard_provider_t provider =
        (g_clipboard_config.provider == CLIP_AUTO)
            ? g_clipboard_config.detected
            : g_clipboard_config.provider;

    switch (provider) {
        case CLIP_OSC52:   return "osc52";
        case CLIP_WL_COPY: return "wl-copy";
        case CLIP_XCLIP:   return "xclip";
        case CLIP_XSEL:    return "xsel";
        case CLIP_CUSTOM:  return "custom";
        case CLIP_NONE:    return "none";
        case CLIP_AUTO:    return "auto";
    }
    unreachable();
}

/* Check if clipboard is available */
bool clipboard_available(void) {
    clipboard_provider_t provider =
        (g_clipboard_config.provider == CLIP_AUTO)
            ? g_clipboard_config.detected
            : g_clipboard_config.provider;

    return provider != CLIP_NONE;
}

/* Configuration helpers */
void clipboard_set_provider(const char *name) {
    if (!name) return;

    if (strcmp(name, "auto") == 0)
        g_clipboard_config.provider = CLIP_AUTO;
    else if (strcmp(name, "osc52") == 0)
        g_clipboard_config.provider = CLIP_OSC52;
    else if (strcmp(name, "wl-copy") == 0 || strcmp(name, "wayland") == 0)
        g_clipboard_config.provider = CLIP_WL_COPY;
    else if (strcmp(name, "xclip") == 0)
        g_clipboard_config.provider = CLIP_XCLIP;
    else if (strcmp(name, "xsel") == 0)
        g_clipboard_config.provider = CLIP_XSEL;
    else if (strcmp(name, "custom") == 0)
        g_clipboard_config.provider = CLIP_CUSTOM;
    else if (strcmp(name, "none") == 0)
        g_clipboard_config.provider = CLIP_NONE;

    /* Re-detect if auto */
    if (g_clipboard_config.provider == CLIP_AUTO) {
        g_clipboard_config.detected = detect_provider();
    }

    LOG_DEBUGF("CLIPBOARD: Provider set to %s", name);
}

void clipboard_set_sync_kills(bool sync) {
    g_clipboard_config.sync_kills = sync;
    LOG_DEBUGF("CLIPBOARD: sync_kills = %s", sync ? "true" : "false");
}

void clipboard_set_osc52_enabled(bool enabled) {
    g_clipboard_config.osc52_enabled = enabled;

    /* Re-detect provider if changed */
    if (g_clipboard_config.provider == CLIP_AUTO) {
        g_clipboard_config.detected = detect_provider();
    }

    LOG_DEBUGF("CLIPBOARD: osc52_enabled = %s", enabled ? "true" : "false");
}

void clipboard_set_custom_copy(const char *cmd) {
    if (cmd) {
        safe_strcpy(g_clipboard_config.copy_cmd, cmd,
                    sizeof(g_clipboard_config.copy_cmd));
        LOG_DEBUGF("CLIPBOARD: custom copy_cmd = %s", cmd);
    }
}

void clipboard_set_custom_paste(const char *cmd) {
    if (cmd) {
        safe_strcpy(g_clipboard_config.paste_cmd, cmd,
                    sizeof(g_clipboard_config.paste_cmd));
        LOG_DEBUGF("CLIPBOARD: custom paste_cmd = %s", cmd);
    }
}

/* clipboard.h - Unified clipboard abstraction for μEmacs
 *
 * Supports multiple clipboard providers:
 *   - OSC 52 (terminal escape sequence, works over SSH/tmux)
 *   - wl-copy/wl-paste (Wayland)
 *   - xclip (X11)
 *   - xsel (X11 fallback)
 *   - Custom commands
 *
 * Configuration via TOML [clipboard] section.
 */

#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>

/* Clipboard provider types */
typedef enum {
    CLIP_NONE = 0,      /* No clipboard support */
    CLIP_AUTO,          /* Auto-detect best available */
    CLIP_OSC52,         /* OSC 52 terminal escape sequence */
    CLIP_WL_COPY,       /* Wayland wl-copy/wl-paste */
    CLIP_XCLIP,         /* X11 xclip */
    CLIP_XSEL,          /* X11 xsel */
    CLIP_CUSTOM         /* User-defined commands */
} clipboard_provider_t;

/* Clipboard configuration (loaded from TOML) */
typedef struct {
    clipboard_provider_t provider;      /* Current provider */
    clipboard_provider_t detected;      /* Auto-detected provider */
    bool sync_kills;                    /* Sync kill ring to clipboard */
    bool osc52_enabled;                 /* Allow OSC 52 if terminal supports */
    char copy_cmd[256];                 /* Custom copy command */
    char paste_cmd[256];                /* Custom paste command */
} clipboard_config_t;

/* Global clipboard configuration */
extern clipboard_config_t g_clipboard_config;

/* Initialize clipboard subsystem (auto-detect if needed) */
int clipboard_init(void);

/* Shutdown clipboard subsystem */
void clipboard_cleanup(void);

/* Copy text to system clipboard
 * Returns: 0 on success, -1 on error
 */
int clipboard_set(const char *text, size_t len);

/* Paste text from system clipboard
 * buf: output buffer
 * maxlen: maximum bytes to read
 * Returns: number of bytes read, or -1 on error
 */
int clipboard_get(char *buf, size_t maxlen);

/* Get name of current provider */
const char *clipboard_provider_name(void);

/* Check if clipboard is available */
bool clipboard_available(void);

/* Configuration helpers (called by settings.c) */
void clipboard_set_provider(const char *name);
void clipboard_set_sync_kills(bool sync);
void clipboard_set_osc52_enabled(bool enabled);
void clipboard_set_custom_copy(const char *cmd);
void clipboard_set_custom_paste(const char *cmd);

/* OSC 52 helpers */
bool clipboard_osc52_supported(void);

#endif /* CLIPBOARD_H */

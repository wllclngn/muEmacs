/*
 * uep_providers.h - UEP Provider Registry
 *
 * Maps filetypes to provider commands from TOML config.
 *
 * C23 compliant
 */

#ifndef UEP_PROVIDERS_H
#define UEP_PROVIDERS_H

#include <stdbool.h>

/* Maximum filetypes and commands */
#define UEP_MAX_FILETYPES 32
#define UEP_MAX_COMMANDS 8

/*
 * Get provider command for a filetype and operation.
 *
 * filetype: e.g., "python", "c", "rust"
 * command: e.g., "format", "lint"
 *
 * Returns: Command string (e.g., "black -q -") or nullptr if not configured.
 *          String is owned by registry, do not free.
 */
const char *uep_get_provider(const char *filetype, const char *command);

/*
 * Register a provider from TOML config.
 *
 * filetype: e.g., "python"
 * command: e.g., "format"
 * provider_cmd: e.g., "black -q -"
 *
 * Called by settings.c when parsing [extensions.*] sections.
 */
void uep_register_provider(const char *filetype, const char *command, const char *provider_cmd);

/*
 * Set global timeout for provider calls (milliseconds).
 */
void uep_set_timeout(int timeout_ms);

/*
 * Get current timeout.
 */
int uep_get_timeout(void);

/*
 * Clear all registered providers.
 * Used for config reload.
 */
void uep_clear_providers(void);

/*
 * Detect filetype from buffer filename.
 *
 * Returns: Filetype string (e.g., "python", "c") or "text" if unknown.
 *          String is statically allocated, do not free.
 */
const char *uep_detect_filetype(const char *filename);

#endif /* UEP_PROVIDERS_H */

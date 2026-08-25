/*
 * uep_scripts.h - Layer 2: Script Extensions
 *
 * Auto-discover and register scripts as M-x commands.
 * Drop any executable in ~/.config/muemacs/scripts/ and it becomes a command.
 *
 * C23 compliant
 */

#ifndef UEP_SCRIPTS_H
#define UEP_SCRIPTS_H

#include <stdbool.h>

/*
 * Initialize the scripts subsystem.
 * Should be called after settings are loaded.
 */
void uep_scripts_init(void);

/*
 * Set the scripts directory path.
 * Called by settings parser when scripts_dir is found.
 */
void uep_scripts_set_dir(const char *dir);

/*
 * Load all scripts from the configured directory.
 * Each executable file becomes an M-x command.
 */
int uep_scripts_load(void);

/*
 * Reload scripts (rescan directory).
 * Useful after adding new scripts.
 */
int uep_scripts_reload(void);

/*
 * List all loaded scripts.
 * Returns count of loaded scripts.
 */
int uep_scripts_list(void);

/*
 * Cleanup scripts subsystem.
 */
void uep_scripts_cleanup(void);

/*
 * Check if a command name is a script command.
 * Returns the script path if found, nullptr otherwise.
 */
const char *uep_scripts_find(const char *name);

/*
 * Execute a script by name.
 * Pipes current buffer to stdin, captures stdout.
 * Returns 0 on success, -1 on failure.
 */
int uep_scripts_exec(const char *name);

#endif /* UEP_SCRIPTS_H */

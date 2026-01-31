/*
 * extension.h - μEmacs Extension System (Layer 3)
 *
 * C23 shared libraries loaded with dlopen().
 * Extensions have full editor API access.
 *
 * C23 compliant
 */

#ifndef UEMACS_EXTENSION_H
#define UEMACS_EXTENSION_H

#include <stddef.h>
#include <stdbool.h>

/* Forward declarations */
struct uemacs_api;
struct buffer;

/* Extension entry point symbol name */
#define UEMACS_EXTENSION_ENTRY uemacs_extension_entry

/* API version for compatibility checking */
#define UEMACS_API_VERSION 4

/* Build-time API version - passed by uep_build.py via compiler flag.
 * Extensions should use this in their struct uemacs_extension.
 * Falls back to UEMACS_API_VERSION if not provided by build system. */
#ifndef UEMACS_API_VERSION_BUILD
#define UEMACS_API_VERSION_BUILD UEMACS_API_VERSION
#endif

/* Maximum loaded extensions */
#define UEMACS_MAX_EXTENSIONS 64

/* Extension descriptor - extensions must provide this */
struct uemacs_extension {
    int api_version;            /* Must match UEMACS_API_VERSION */
    const char *name;           /* Short name: "my-git-workflow" */
    const char *version;        /* Semantic version: "1.0.0" */
    const char *description;    /* Human-readable description */

    /* Lifecycle callbacks */
    int  (*init)(struct uemacs_api *api);   /* Called on load, return 0 on success */
    void (*cleanup)(void);                   /* Called on unload */
};

/* Extension entry function type */
typedef struct uemacs_extension *(*extension_entry_fn)(void);

/* Extension loader API */

/* Load extension from .so file path */
int extension_load(const char *path);

/* Unload extension by name */
int extension_unload(const char *name);

/* List loaded extensions (displays in message line) */
void extension_list(void);

/* Load all extensions from directory */
int extension_load_dir(const char *dir);

/* Get count of loaded extensions */
int extension_count(void);

/* Poll for pending extensions and init them (called from main loop) */
int extension_poll_pending(void);

/* Check if async loading is in progress */
bool extension_loading_in_progress(void);

/* Initialize extension subsystem */
void extension_init(void);

/* Cleanup extension subsystem (unloads all extensions) */
void extension_cleanup(void);

/* Interactive commands */
int extension_load_cmd(int f, int n);
int extension_unload_cmd(int f, int n);
int extension_list_cmd(int f, int n);

#endif /* UEMACS_EXTENSION_H */

/*
 * extension.c - μEmacs Extension Loader (Layer 3)
 *
 * Loads C23 shared libraries via dlopen().
 * Extensions have full editor API access.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "uep/extension.h"
#include "uep/extension_api.h"
#include "internal/memory.h"
#include "util/logger.h"

/* Loaded extension tracking */
typedef struct {
    char *path;                         /* Full path to .so */
    void *handle;                       /* dlopen handle */
    struct uemacs_extension *ext;       /* Extension descriptor */
    bool active;                        /* Is this slot in use? */
} loaded_extension_t;

static loaded_extension_t extensions[UEMACS_MAX_EXTENSIONS];
static int extension_count_internal = 0;
static bool extension_initialized = false;

/* Auto-load directory from settings */
static char autoload_dir[1024] = "";

/* Auto-build setting (default: enabled) */
static bool extension_auto_build = true;

/* Forward declaration */
extern struct uemacs_api *uemacs_get_api(void);

/* Set the auto-load directory (called from settings parser) */
void extension_set_autoload_dir(const char *dir) {
    if (!dir || !*dir) return;

    /* Expand ~ if present */
    if (dir[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(autoload_dir, sizeof(autoload_dir), "%s%s", home, dir + 1);
        } else {
            strncpy(autoload_dir, dir, sizeof(autoload_dir) - 1);
            autoload_dir[sizeof(autoload_dir) - 1] = '\0';
        }
    } else {
        strncpy(autoload_dir, dir, sizeof(autoload_dir) - 1);
        autoload_dir[sizeof(autoload_dir) - 1] = '\0';
    }

    LOG_INFOF("Extension: Auto-load directory set to %s", autoload_dir);
}

/* Get the auto-load directory */
const char *extension_get_autoload_dir(void) {
    return autoload_dir[0] ? autoload_dir : NULL;
}

/* Set auto-build enabled/disabled (called from settings parser) */
void extension_set_auto_build(bool enabled) {
    extension_auto_build = enabled;
    LOG_INFOF("Extension: Auto-build %s", enabled ? "enabled" : "disabled");
}

/* Get auto-build setting */
bool extension_get_auto_build(void) {
    return extension_auto_build;
}

/* Auto-load extensions from configured directory */
void extension_autoload(void) {
    if (!autoload_dir[0]) {
        LOG_DEBUG("Extension: No auto-load directory configured");
        return;
    }

    LOG_INFOF("Extension: Auto-loading from %s", autoload_dir);
    int loaded = extension_load_dir(autoload_dir);
    if (loaded > 0) {
        LOG_INFOF("Extension: Auto-loaded %d extension(s)", loaded);
    } else if (loaded == 0) {
        LOG_DEBUG("Extension: No extensions found in auto-load directory");
    }
}

/* Initialize extension subsystem */
void extension_init(void) {
    if (extension_initialized) return;

    memset(extensions, 0, sizeof(extensions));
    extension_count_internal = 0;
    extension_initialized = true;

    LOG_INFO("Extension: Subsystem initialized");
}

/* Find extension by name */
static loaded_extension_t *find_extension(const char *name) {
    if (!name) return nullptr;
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].ext &&
            extensions[i].ext->name &&
            strcmp(extensions[i].ext->name, name) == 0) {
            return &extensions[i];
        }
    }
    return nullptr;
}

/* Find extension by path */
static loaded_extension_t *find_extension_by_path(const char *path) {
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].path &&
            strcmp(extensions[i].path, path) == 0) {
            return &extensions[i];
        }
    }
    return nullptr;
}

/* Find free slot */
static loaded_extension_t *find_free_slot(void) {
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (!extensions[i].active) {
            return &extensions[i];
        }
    }
    return nullptr;
}

/* Load extension from .so file path */
int extension_load(const char *path) {
    if (!path || !*path) {
        LOG_ERROR("Extension: No path provided");
        return -1;
    }

    if (!extension_initialized) {
        extension_init();
    }

    /* Check if already loaded */
    if (find_extension_by_path(path)) {
        LOG_WARNF("Extension: Already loaded: %s", path);
        return -1;
    }

    /* Find free slot */
    loaded_extension_t *slot = find_free_slot();
    if (!slot) {
        LOG_ERROR("Extension: Maximum extensions reached");
        return -1;
    }

    /* Open shared library
     * RTLD_NODELETE: Keep library in memory even after dlclose().
     * This is required for extensions with language runtimes (Crystal, Go,
     * Haskell) whose GC heaps cannot be cleanly deallocated on unload.
     * The OS reclaims all memory on process exit.
     */
    LOG_INFOF("Extension: Loading %s", path);

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!handle) {
        LOG_ERRORF("Extension: dlopen failed: %s", dlerror());
        return -1;
    }

    /* Clear any existing error */
    dlerror();

    /* Find entry point - use union to avoid ISO C warning about void* to fn ptr */
    union {
        void *ptr;
        extension_entry_fn fn;
    } entry_u;
    entry_u.ptr = dlsym(handle, "uemacs_extension_entry");
    extension_entry_fn entry = entry_u.fn;
    const char *dl_error = dlerror();
    if (dl_error) {
        LOG_ERRORF("Extension: dlsym failed: %s", dl_error);
        dlclose(handle);
        return -1;
    }

    if (!entry) {
        LOG_ERROR("Extension: Entry point is NULL");
        dlclose(handle);
        return -1;
    }

    /* Get extension descriptor */
    struct uemacs_extension *ext = entry();
    if (!ext) {
        LOG_ERROR("Extension: Entry function returned NULL");
        dlclose(handle);
        return -1;
    }

    /* Validate extension */
    if (!ext->name || !*ext->name) {
        LOG_ERROR("Extension: No name provided");
        dlclose(handle);
        return -1;
    }

    /* Check API version */
    if (ext->api_version != UEMACS_API_VERSION) {
        LOG_ERRORF("Extension: API version mismatch for '%s' (HAVE: API v%d; NEED: API v%d)",
                   ext->name, ext->api_version, UEMACS_API_VERSION);
        dlclose(handle);
        return -2;  /* Special code for API mismatch */
    }

    /* Check for duplicate name */
    if (find_extension(ext->name)) {
        LOG_ERRORF("Extension: Name already registered: %s", ext->name);
        dlclose(handle);
        return -1;
    }

    /* Initialize extension */
    if (ext->init) {
        struct uemacs_api *api = uemacs_get_api();
        int init_result = ext->init(api);
        if (init_result != 0) {
            LOG_ERRORF("Extension: init() failed with code %d: %s",
                       init_result, ext->name);
            dlclose(handle);
            return -1;
        }
    }

    /* Store in slot */
    slot->path = strdup(path);
    slot->handle = handle;
    slot->ext = ext;
    slot->active = true;
    extension_count_internal++;

    LOG_INFOF("Extension: Loaded '%s' v%s",
              ext->name,
              ext->version ? ext->version : "?.?.?");

    if (ext->description) {
        LOG_INFOF("Extension: %s", ext->description);
    }

    return 0;
}

/* Unload extension by name */
int extension_unload(const char *name) {
    if (!name || !*name) {
        LOG_ERROR("Extension: No name provided for unload");
        return -1;
    }

    loaded_extension_t *slot = find_extension(name);
    if (!slot) {
        LOG_WARNF("Extension: Not found: %s", name);
        return -1;
    }

    /* Copy name before dlclose - the original points to memory inside the .so */
    char name_copy[256];
    strncpy(name_copy, name, sizeof(name_copy) - 1);
    name_copy[sizeof(name_copy) - 1] = '\0';

    LOG_INFOF("Extension: Unloading '%s'", name_copy);

    /* Call cleanup */
    if (slot->ext && slot->ext->cleanup) {
        slot->ext->cleanup();
    }

    /* Close library - after this, 'name' pointer is INVALID */
    if (slot->handle) {
        dlclose(slot->handle);
    }

    /* Free path */
    free(slot->path);

    /* Clear slot */
    memset(slot, 0, sizeof(*slot));
    extension_count_internal--;

    LOG_INFOF("Extension: Unloaded '%s'", name_copy);
    return 0;
}

/* List loaded extensions */
void extension_list(void) {
    if (extension_count_internal == 0) {
        mlwrite("No extensions loaded");
        return;
    }

    mlwrite("Loaded extensions:");
    for (int i = 0; i < UEMACS_MAX_EXTENSIONS; i++) {
        if (extensions[i].active && extensions[i].ext) {
            struct uemacs_extension *ext = extensions[i].ext;
            mlwrite("  %s v%s - %s",
                    ext->name,
                    ext->version ? ext->version : "?.?.?",
                    ext->description ? ext->description : "(no description)");
        }
    }
}

/*
 * Build extension using uep_build.py universal builder
 * Returns 0 on success, 1 on build failure, 2 on no method found
 */
static int build_extension(const char *ext_path) {
    char cmd[2048];

    /* Find uep_build.py - try installed location first, then source tree */
    const char *build_script = NULL;
    static char script_path[1024] = {0};

    const char *script_locations[] = {
        UEMACS_DATA_DIR "/uep_build.py",      /* Installed */
        "/usr/local/share/muemacs/uep_build.py",
        "/usr/share/muemacs/uep_build.py",
        "scripts/uep_build.py",                /* Source tree */
        "../scripts/uep_build.py",
        NULL
    };

    for (int i = 0; script_locations[i]; i++) {
        if (access(script_locations[i], R_OK) == 0) {
            build_script = script_locations[i];
            break;
        }
    }

    if (!build_script) {
        LOG_WARN("Extension: Cannot find uep_build.py");
        return 2;
    }

    /* Build command: python3 uep_build.py <ext_path> */
    snprintf(cmd, sizeof(cmd), "python3 '%s' '%s' 2>&1", build_script, ext_path);

    LOG_INFOF("Extension: Building %s", ext_path);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        LOG_ERROR("Extension: Failed to run builder");
        return 1;
    }

    /* Read builder output */
    char output[2048] = {0};
    size_t total = 0;
    while (fgets(output + total, sizeof(output) - total - 1, fp)) {
        total = strlen(output);
        if (total >= sizeof(output) - 100) break;
    }

    int status = pclose(fp);
    int exit_code = WEXITSTATUS(status);

    if (exit_code == 0) {
        LOG_INFO("Extension: Build successful");
        if (output[0]) LOG_DEBUGF("Extension: %s", output);
    } else if (exit_code == 2) {
        LOG_WARNF("Extension: No build method for %s", ext_path);
    } else {
        LOG_ERRORF("Extension: Build failed: %s", output);
    }

    return exit_code;
}

/*
 * Check if path is a directory that looks like an extension project.
 * (Has source files or build manifests)
 */
static bool is_extension_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }

    /* Check for common build indicators */
    const char *indicators[] = {
        "Cargo.toml", "go.mod", "Makefile", "build.py",
        "dub.json", "build.zig", "Package.swift", "shard.yml",
        NULL
    };

    for (int i = 0; indicators[i]; i++) {
        char check[1024];
        snprintf(check, sizeof(check), "%s/%s", path, indicators[i]);
        if (access(check, F_OK) == 0) {
            return true;
        }
    }

    /* Check for source files */
    DIR *dp = opendir(path);
    if (!dp) return false;

    struct dirent *entry;
    bool has_source = false;
    while ((entry = readdir(dp)) != NULL) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len > 2) {
            const char *ext = name + len - 2;
            if (strcmp(ext, ".c") == 0 || strcmp(ext, ".d") == 0 ||
                strcmp(ext, ".v") == 0) {
                has_source = true;
                break;
            }
        }
        if (len > 3) {
            const char *ext = name + len - 3;
            if (strcmp(ext, ".rs") == 0 || strcmp(ext, ".go") == 0 ||
                strcmp(ext, ".hs") == 0 || strcmp(ext, ".ml") == 0) {
                has_source = true;
                break;
            }
        }
    }
    closedir(dp);
    return has_source;
}

/* Load all extensions from directory */
int extension_load_dir(const char *dir) {
    if (!dir || !*dir) {
        LOG_ERROR("Extension: No directory provided");
        return -1;
    }

    DIR *dp = opendir(dir);
    if (!dp) {
        LOG_WARNF("Extension: Cannot open directory: %s", dir);
        return -1;
    }

    int loaded = 0;
    struct dirent *entry;

    /*
     * First pass: Build extensions if auto_build is enabled.
     * Handles both single .c files and subdirectories (Rust, Go, etc.)
     */
    if (extension_auto_build) {
        while ((entry = readdir(dp)) != nullptr) {
            if (entry->d_name[0] == '.') continue;

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

            struct stat st;
            if (stat(full_path, &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                /* Subdirectory - check if it's an extension project */
                if (is_extension_dir(full_path)) {
                    LOG_DEBUGF("Extension: Found project dir: %s", entry->d_name);
                    build_extension(full_path);
                }
            } else if (S_ISREG(st.st_mode)) {
                /* Regular file - check for .c source */
                size_t len = strlen(entry->d_name);
                if (len > 2 && strcmp(entry->d_name + len - 2, ".c") == 0) {
                    LOG_DEBUGF("Extension: Found C source: %s", entry->d_name);
                    build_extension(full_path);
                }
            }
        }
        rewinddir(dp);
    }

    /* Second pass: load all .so files */
    while ((entry = readdir(dp)) != nullptr) {
        if (entry->d_name[0] == '.') continue;

        size_t len = strlen(entry->d_name);

        /* Check for .so in current directory */
        if (len > 3 && strcmp(entry->d_name + len - 3, ".so") == 0) {
            char so_path[1024];
            snprintf(so_path, sizeof(so_path), "%s/%s", dir, entry->d_name);

            struct stat so_stat;
            if (stat(so_path, &so_stat) != 0 || !S_ISREG(so_stat.st_mode)) {
                continue;
            }

            /* Load it */
            int result = extension_load(so_path);
            if (result == 0) {
                loaded++;
            } else if (result == -2) {
                /* API mismatch */
                const char *filename = entry->d_name;
                char basename[256];
                size_t fn_len = len - 3;  /* Remove .so */
                if (fn_len >= sizeof(basename)) fn_len = sizeof(basename) - 1;
                strncpy(basename, filename, fn_len);
                basename[fn_len] = '\0';

                LOG_WARNF("Extension '%s' cannot load (HAVE: API v?; NEED: API v%d). "
                          "Provide source for auto-recompile.",
                          filename, UEMACS_API_VERSION);
                mlwrite("WARNING: '%s' needs recompile. Provide source for auto-build.",
                        filename);
            }
        }

        /* Also check subdirectories for .so files */
        char sub_path[PATH_MAX];
        int sub_len = snprintf(sub_path, sizeof(sub_path), "%s/%s", dir, entry->d_name);
        if (sub_len < 0 || (size_t)sub_len >= sizeof(sub_path))
            continue;  /* Path too long, skip */

        struct stat st;
        if (stat(sub_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            /* Look for .so in subdirectory - use remaining space */
            char so_in_dir[PATH_MAX];
            int so_len = snprintf(so_in_dir, sizeof(so_in_dir), "%s/%s.so", sub_path, entry->d_name);
            if (so_len < 0 || (size_t)so_len >= sizeof(so_in_dir))
                continue;  /* Path too long, skip */

            struct stat so_stat;
            if (stat(so_in_dir, &so_stat) == 0 && S_ISREG(so_stat.st_mode)) {
                int result = extension_load(so_in_dir);
                if (result == 0) {
                    loaded++;
                }
            }
        }
    }

    closedir(dp);

    LOG_INFOF("Extension: Loaded %d extensions from %s", loaded, dir);
    return loaded;
}

/* Get count of loaded extensions */
int extension_count(void) {
    return extension_count_internal;
}

/* Cleanup extension subsystem */
void extension_cleanup(void) {
    if (!extension_initialized) return;

    LOG_INFO("Extension: Shutting down...");

    /* Unload all extensions in reverse order */
    for (int i = UEMACS_MAX_EXTENSIONS - 1; i >= 0; i--) {
        if (extensions[i].active && extensions[i].ext) {
            extension_unload(extensions[i].ext->name);
        }
    }

    extension_initialized = false;
    LOG_INFO("Extension: Subsystem cleaned up");
}

/* Interactive command: Load extension */
int extension_load_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    char path[1024];

    if (minibuf_read("Extension path: ", path, sizeof(path)) != true) {
        return false;
    }

    /* Expand ~ if present */
    char expanded[1024];
    if (path[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
        } else {
            strncpy(expanded, path, sizeof(expanded) - 1);
            expanded[sizeof(expanded) - 1] = '\0';
        }
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
        expanded[sizeof(expanded) - 1] = '\0';
    }

    if (extension_load(expanded) == 0) {
        mlwrite("Extension loaded");
        return true;
    } else {
        mlwrite("Failed to load extension");
        return false;
    }
}

/* Interactive command: Unload extension */
int extension_unload_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    char name[256];

    if (minibuf_read("Extension name: ", name, sizeof(name)) != true) {
        return false;
    }

    if (extension_unload(name) == 0) {
        mlwrite("Extension unloaded");
        return true;
    } else {
        mlwrite("Failed to unload extension");
        return false;
    }
}

/* Interactive command: List extensions */
int extension_list_cmd([[maybe_unused]] int f, [[maybe_unused]] int n) {
    extension_list();
    return true;
}

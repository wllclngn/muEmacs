/*
 * uep_providers.c - UEP Provider Registry
 *
 * Maps filetypes to provider commands.
 * Pattern after clipboard.c provider detection.
 *
 * C23 compliant
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "uep/uep_providers.h"
#include "internal/memory.h"
#include "util/logger.h"

/* Provider entry: filetype + command -> provider_cmd */
typedef struct {
    char filetype[32];
    char command[32];
    char provider_cmd[256];
} provider_entry_t;

/* Provider registry */
static provider_entry_t providers[UEP_MAX_FILETYPES * UEP_MAX_COMMANDS];
static int provider_count = 0;
static int provider_timeout_ms = 5000;  /* Default 5 seconds */

/* Extension to filetype mapping */
static const struct {
    const char *ext;
    const char *filetype;
} extension_map[] = {
    /* C/C++ */
    {".c",    "c"},
    {".h",    "c"},
    {".cpp",  "cpp"},
    {".cc",   "cpp"},
    {".cxx",  "cpp"},
    {".hpp",  "cpp"},
    {".hxx",  "cpp"},

    /* Python */
    {".py",   "python"},
    {".pyw",  "python"},
    {".pyi",  "python"},

    /* Rust */
    {".rs",   "rust"},

    /* Go */
    {".go",   "go"},

    /* JavaScript/TypeScript */
    {".js",   "javascript"},
    {".mjs",  "javascript"},
    {".cjs",  "javascript"},
    {".jsx",  "javascript"},
    {".ts",   "typescript"},
    {".tsx",  "typescript"},

    /* Web */
    {".html", "html"},
    {".htm",  "html"},
    {".css",  "css"},
    {".scss", "scss"},
    {".sass", "sass"},
    {".less", "less"},

    /* Data formats */
    {".json", "json"},
    {".yaml", "yaml"},
    {".yml",  "yaml"},
    {".toml", "toml"},
    {".xml",  "xml"},

    /* Shell */
    {".sh",   "sh"},
    {".bash", "bash"},
    {".zsh",  "zsh"},
    {".fish", "fish"},

    /* Other languages */
    {".rb",   "ruby"},
    {".pl",   "perl"},
    {".lua",  "lua"},
    {".java", "java"},
    {".kt",   "kotlin"},
    {".scala", "scala"},
    {".hs",   "haskell"},
    {".ml",   "ocaml"},
    {".fs",   "fsharp"},
    {".cs",   "csharp"},
    {".swift", "swift"},
    {".zig",  "zig"},
    {".nim",  "nim"},
    {".v",    "vlang"},
    {".d",    "d"},
    {".elm",  "elm"},
    {".ex",   "elixir"},
    {".exs",  "elixir"},
    {".erl",  "erlang"},
    {".clj",  "clojure"},
    {".lisp", "lisp"},
    {".el",   "elisp"},
    {".scm",  "scheme"},
    {".rkt",  "racket"},

    /* Markup */
    {".md",   "markdown"},
    {".rst",  "rst"},
    {".tex",  "latex"},

    /* Config */
    {".ini",  "ini"},
    {".conf", "conf"},
    {".cfg",  "conf"},

    /* SQL */
    {".sql",  "sql"},

    /* Makefile */
    {"Makefile", "make"},
    {"makefile", "make"},
    {"GNUmakefile", "make"},
    {".mk",   "make"},

    /* CMake */
    {"CMakeLists.txt", "cmake"},
    {".cmake", "cmake"},

    /* Docker */
    {"Dockerfile", "dockerfile"},
    {".dockerfile", "dockerfile"},

    {nullptr, nullptr}
};

/* Get provider command */
const char *uep_get_provider(const char *filetype, const char *command) {
    if (!filetype || !command) return nullptr;

    for (int i = 0; i < provider_count; i++) {
        if (strcasecmp(providers[i].filetype, filetype) == 0 &&
            strcasecmp(providers[i].command, command) == 0) {
            LOG_DEBUGF("UEP: Found provider for %s/%s: %s",
                       filetype, command, providers[i].provider_cmd);
            return providers[i].provider_cmd;
        }
    }

    LOG_DEBUGF("UEP: No provider for %s/%s", filetype, command);
    return nullptr;
}

/* Register a provider */
void uep_register_provider(const char *filetype, const char *command,
                           const char *provider_cmd) {
    if (!filetype || !command || !provider_cmd) return;
    if (provider_count >= UEP_MAX_FILETYPES * UEP_MAX_COMMANDS) {
        LOG_WARN("UEP: Provider registry full");
        return;
    }

    /* Check if already registered - update if so */
    for (int i = 0; i < provider_count; i++) {
        if (strcasecmp(providers[i].filetype, filetype) == 0 &&
            strcasecmp(providers[i].command, command) == 0) {
            strncpy(providers[i].provider_cmd, provider_cmd,
                    sizeof(providers[i].provider_cmd) - 1);
            LOG_INFOF("UEP: Updated provider %s/%s = %s",
                      filetype, command, provider_cmd);
            return;
        }
    }

    /* Add new entry */
    provider_entry_t *e = &providers[provider_count];
    strncpy(e->filetype, filetype, sizeof(e->filetype) - 1);
    strncpy(e->command, command, sizeof(e->command) - 1);
    strncpy(e->provider_cmd, provider_cmd, sizeof(e->provider_cmd) - 1);
    provider_count++;

    LOG_INFOF("UEP: Registered provider %s/%s = %s",
              filetype, command, provider_cmd);
}

/* Set timeout */
void uep_set_timeout(int timeout_ms) {
    if (timeout_ms > 0) {
        provider_timeout_ms = timeout_ms;
        LOG_DEBUGF("UEP: Timeout set to %d ms", timeout_ms);
    }
}

/* Get timeout */
int uep_get_timeout(void) {
    return provider_timeout_ms;
}

/* Clear all providers */
void uep_clear_providers(void) {
    provider_count = 0;
    LOG_DEBUG("UEP: Provider registry cleared");
}

/* Detect filetype from filename */
const char *uep_detect_filetype(const char *filename) {
    if (!filename || !*filename) return "text";

    /* Get basename */
    const char *base = strrchr(filename, '/');
    base = base ? base + 1 : filename;

    /* Check for exact filename matches first (Makefile, CMakeLists.txt, etc.) */
    for (int i = 0; extension_map[i].ext; i++) {
        if (extension_map[i].ext[0] != '.') {
            /* Exact filename match */
            if (strcasecmp(base, extension_map[i].ext) == 0) {
                return extension_map[i].filetype;
            }
        }
    }

    /* Find extension */
    const char *ext = strrchr(base, '.');
    if (!ext) return "text";

    /* Look up extension */
    for (int i = 0; extension_map[i].ext; i++) {
        if (extension_map[i].ext[0] == '.') {
            if (strcasecmp(ext, extension_map[i].ext) == 0) {
                return extension_map[i].filetype;
            }
        }
    }

    return "text";
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdatomic.h>

#include "config.h"

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "tiny_toml.h"
#include "keymap.h"
#include "memory.h"
#include "util/logger.h"
#include "terminal/palette.h"
#include "git_status.h"
#include "clipboard.h"
#include "uep/uep_providers.h"
#include "uep/extension_api.h"

// Settings loader with XDG Base Directory support
// Priority order:
//   1. User config: ~/.config/muemacs/settings.toml (XDG_CONFIG_HOME)
//   2. System installed: UEMACS_DATA_DIR/editor/settings.toml
//   3. In-tree fallback: UEMACS_SOURCE_EDITOR_DIR/settings.toml

static char user_config_path[512] = {0};
static char user_config_dir[512] = {0};

// Get XDG config directory or fallback to ~/.config
static const char* get_user_config_dir(void) {
    if (user_config_dir[0] != '\0') {
        return user_config_dir;
    }
    
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && xdg_config[0] != '\0') {
        snprintf(user_config_dir, sizeof(user_config_dir), "%s/muemacs", xdg_config);
    } else {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(user_config_dir, sizeof(user_config_dir), "%s/.config/muemacs", home);
        } else {
            user_config_dir[0] = '\0';
        }
    }
    return user_config_dir;
}

// Get user config file path
static const char* get_user_config_file(void) {
    if (user_config_path[0] != '\0') {
        return user_config_path;
    }
    
    const char* dir = get_user_config_dir();
    if (dir[0] != '\0') {
        snprintf(user_config_path, sizeof(user_config_path), "%s/settings.toml", dir);
    }
    return user_config_path;
}

static const char* settings_file(void) { return UEMACS_DATA_DIR "/editor/settings.toml"; }

static FILE* open_settings_fp(void) {
    // Try user config first (highest priority)
    const char* user_config = get_user_config_file();
    if (user_config[0] != '\0') {
        LOG_DEBUGF("SETTINGS: Trying user config: %s", user_config);
        FILE* fp = fopen(user_config, "r");
        if (fp) {
            LOG_DEBUGF("SETTINGS: Opened user config: %s", user_config);
            return fp;
        }
    }

    // Try system installed config
    LOG_DEBUGF("SETTINGS: Trying system config: %s", UEMACS_DATA_DIR "/editor/settings.toml");
    FILE* fp = fopen(UEMACS_DATA_DIR "/editor/settings.toml", "r");
    if (fp) {
        LOG_DEBUG("SETTINGS: Opened system config: " UEMACS_DATA_DIR "/editor/settings.toml");
        return fp;
    }

    // Fallback to in-tree config
    LOG_DEBUGF("SETTINGS: Trying in-tree fallback: %s", UEMACS_SOURCE_EDITOR_DIR "/settings.toml");
    fp = fopen(UEMACS_SOURCE_EDITOR_DIR "/settings.toml", "r");
    if (fp) {
        LOG_DEBUG("SETTINGS: Opened in-tree config: " UEMACS_SOURCE_EDITOR_DIR "/settings.toml");
    }
    return fp;
}

static int ensure_dir_abs(const char* dir) {
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return mkdir(dir, 0700) == 0;
}

static void ensure_settings_dir(void) {
    // Try to create user config directory first (XDG)
    const char* user_dir = get_user_config_dir();
    if (user_dir[0] != '\0' && ensure_dir_abs(user_dir)) {
        return; // User config dir created successfully
    }

    /* Prefer installed data dir; if not writable, fallback to project tree */
    if (!ensure_dir_abs(UEMACS_DATA_DIR "/editor")) {
        (void)ensure_dir_abs(UEMACS_SOURCE_EDITOR_DIR);
    }
}

/* TOML Callback to apply settings */
static void apply_setting(const char *section, const char *key, const char *value, enum toml_type type, void *user_data) {
    (void)user_data;
    int int_val = 0;
    int bool_val = 0;
    
    if (type == TOML_INT) {
        char *endptr;
        errno = 0;
        long val = strtol(value, &endptr, 10);
        if (errno == 0 && endptr != value) int_val = (int)val;
    }
    if (type == TOML_BOOL) bool_val = (strcmp(value, "true") == 0);

    /* [editor] section */
    if (strcmp(section, "editor") == 0) {
        if (strcmp(key, "column_width") == 0 && int_val > 0) {
            writing_mode_enable(true, int_val);
        }
        if (strcmp(key, "wrap") == 0) {
            if (bool_val) {
                int col = (fillcol > 0 ? fillcol : 80);
                writing_mode_enable(true, col);
            } else {
                if (curbp) curbp->b_mode &= ~MDWRAP;
            }
        }
        if (strcmp(key, "evil_mode") == 0) {
            if (bool_val && !vim_mode_active) {
                evil_mode(0, 1);
            } else if (!bool_val && vim_mode_active) {
                evil_mode(0, 1);
            }
        }
        if (strcmp(key, "tab_width") == 0 && int_val >= 1 && int_val <= 16) {
            tabmask = int_val - 1;  /* tab_width=8 -> tabmask=0x07 */
        }
        if (strcmp(key, "auto_save_interval") == 0 && int_val >= 0) {
            gasave = int_val;  /* 0 = disabled */
        }
        if (strcmp(key, "scroll_step") == 0 && int_val >= 1) {
            scrollcount = int_val;
        }
        if (strcmp(key, "trim_trailing_whitespace") == 0) {
            trim_trailing_whitespace = bool_val;
        }
        if (strcmp(key, "insert_final_newline") == 0) {
            insert_final_newline = bool_val;
        }
    }

    /* [visual] section */
    else if (strcmp(section, "visual") == 0) {
        if (strcmp(key, "ruler") == 0) column_ruler_enabled = bool_val;
        if (strcmp(key, "ruler_col") == 0 && int_val > 0) column_ruler_column = int_val;
        if (strcmp(key, "highlight_line") == 0) highlight_current_line = bool_val;
        if (strcmp(key, "highlight_style") == 0) hiline_style = int_val;
        if (strcmp(key, "ruler_style") == 0) ruler_style = int_val;
        sgarbf = true;
    }
    
    /* [theme] section */
    else if (strcmp(section, "theme") == 0) {
        LOG_DEBUGF("THEME: Parsing key='%s' value='%s' type=%d", key, value, type);
        /* Legacy intensity settings (may be deprecated) */
        if (strcmp(key, "highlight_intensity") == 0) highlight_intensity_pct = int_val;
        if (strcmp(key, "ruler_intensity") == 0) ruler_intensity_pct = int_val;
        if (strcmp(key, "intersection_intensity") == 0) intersection_intensity_pct = int_val;
        if (strcmp(key, "highlight_strategy") == 0) highlight_strategy = int_val;

        /* Hex color parsing helper macro */
        #define PARSE_HEX_RGB(keyname, bg_field, rgb_field) \
            if (strcmp(key, keyname) == 0 && palette_parse_hex(value, &r, &g, &b)) { \
                bg_field = -2; \
                rgb_field[0] = (unsigned char)r; \
                rgb_field[1] = (unsigned char)g; \
                rgb_field[2] = (unsigned char)b; \
            }

        #define PARSE_HEX_FG(keyname, rgb_field) \
            if (strcmp(key, keyname) == 0 && palette_parse_hex(value, &r, &g, &b)) { \
                rgb_field[0] = (unsigned char)r; \
                rgb_field[1] = (unsigned char)g; \
                rgb_field[2] = (unsigned char)b; \
            }

        /* Truecolor hex colors - always parse, render code checks capability */
        if (type == TOML_STRING) {
            int r, g, b;

            /* Backgrounds */
            PARSE_HEX_RGB("modeline_bg", g_palette.modeline_bg, g_palette.modeline_rgb)
            if (strcmp(key, "modeline_bg") == 0) {
                LOG_DEBUGF("THEME: modeline_bg parsed -> #%02X%02X%02X",
                           g_palette.modeline_rgb[0],
                           g_palette.modeline_rgb[1],
                           g_palette.modeline_rgb[2]);
            }
            PARSE_HEX_FG("modeline_fg", g_palette.modeline_fg_rgb)
            PARSE_HEX_RGB("highlight_bg", g_palette.highlight_bg, g_palette.highlight_rgb)
            PARSE_HEX_RGB("ruler_bg", g_palette.ruler_bg, g_palette.ruler_rgb)
            PARSE_HEX_RGB("selection_bg", g_palette.selection_bg, g_palette.selection_rgb)
            PARSE_HEX_RGB("search_bg", g_palette.search_bg, g_palette.search_bg_rgb)
            PARSE_HEX_RGB("message_bg", g_palette.message_bg, g_palette.message_bg_rgb)

            /* Search foreground (optional) */
            if (strcmp(key, "search_fg") == 0 && value[0] != '\0' && palette_parse_hex(value, &r, &g, &b)) {
                g_palette.search_fg = -2;
                g_palette.search_fg_rgb[0] = (unsigned char)r;
                g_palette.search_fg_rgb[1] = (unsigned char)g;
                g_palette.search_fg_rgb[2] = (unsigned char)b;
                g_palette.search_fg_set = true;
            }

            /* Vim mode indicator colors */
            PARSE_HEX_FG("mode_normal", g_palette.mode_normal_rgb)
            PARSE_HEX_FG("mode_insert", g_palette.mode_insert_rgb)
            PARSE_HEX_FG("mode_visual", g_palette.mode_visual_rgb)
            PARSE_HEX_FG("mode_replace", g_palette.mode_replace_rgb)
            PARSE_HEX_FG("mode_evil", g_palette.mode_evil_rgb)

            /* Accent and message colors */
            PARSE_HEX_FG("accent", g_palette.accent_rgb)
            PARSE_HEX_FG("error", g_palette.error_rgb)
            PARSE_HEX_FG("warning", g_palette.warning_rgb)
            PARSE_HEX_FG("success", g_palette.success_rgb)
        }

        #undef PARSE_HEX_RGB
        #undef PARSE_HEX_FG

        sgarbf = true;
    }

    /* [theme.syntax] section - syntax highlighting colors */
    else if (strcmp(section, "theme.syntax") == 0) {
        LOG_DEBUGF("SYNTAX THEME: Parsing key='%s' value='%s' type=%d", key, value, type);

        /* Helper macro for syntax color parsing */
        #define PARSE_SYNTAX_RGB(keyname, rgb_field) \
            if (strcmp(key, keyname) == 0 && palette_parse_hex(value, &r, &g, &b)) { \
                rgb_field[0] = (unsigned char)r; \
                rgb_field[1] = (unsigned char)g; \
                rgb_field[2] = (unsigned char)b; \
            }

        /* Helper for style parsing */
        #define PARSE_STYLE(keyname, style_field) \
            if (strcmp(key, keyname) == 0) { \
                style_field = 0; \
                if (strstr(value, "bold")) style_field |= 0x10; \
                if (strstr(value, "italic")) style_field |= 0x20; \
                if (strstr(value, "underline")) style_field |= 0x40; \
            }

        /* Boolean: enabled */
        if (strcmp(key, "enabled") == 0) {
            g_palette.syntax_enabled = bool_val;
        }
        /* Boolean: prefer_builtin (built-in lexers vs extension lexers) */
        if (strcmp(key, "prefer_builtin") == 0) {
            g_palette.syntax_prefer_builtin = bool_val;
        }

        /* Hex colors */
        if (type == TOML_STRING) {
            int r, g, b;

            PARSE_SYNTAX_RGB("keyword", g_palette.syntax_keyword_rgb)
            PARSE_SYNTAX_RGB("string", g_palette.syntax_string_rgb)
            PARSE_SYNTAX_RGB("comment", g_palette.syntax_comment_rgb)
            PARSE_SYNTAX_RGB("number", g_palette.syntax_number_rgb)
            PARSE_SYNTAX_RGB("type", g_palette.syntax_type_rgb)
            PARSE_SYNTAX_RGB("function", g_palette.syntax_function_rgb)
            PARSE_SYNTAX_RGB("operator", g_palette.syntax_operator_rgb)
            PARSE_SYNTAX_RGB("preprocessor", g_palette.syntax_preproc_rgb)
            PARSE_SYNTAX_RGB("constant", g_palette.syntax_constant_rgb)
            PARSE_SYNTAX_RGB("variable", g_palette.syntax_variable_rgb)
            PARSE_SYNTAX_RGB("attribute", g_palette.syntax_attribute_rgb)
            PARSE_SYNTAX_RGB("escape", g_palette.syntax_escape_rgb)
            PARSE_SYNTAX_RGB("regex", g_palette.syntax_regex_rgb)
            PARSE_SYNTAX_RGB("special", g_palette.syntax_special_rgb)

            /* Style flags */
            PARSE_STYLE("keyword_style", g_palette.syntax_keyword_style)
            PARSE_STYLE("comment_style", g_palette.syntax_comment_style)
            PARSE_STYLE("string_style", g_palette.syntax_string_style)
            PARSE_STYLE("type_style", g_palette.syntax_type_style)
            PARSE_STYLE("function_style", g_palette.syntax_function_style)
        }

        #undef PARSE_SYNTAX_RGB
        #undef PARSE_STYLE

        sgarbf = true;
    }

    /* [modeline] section */
    else if (strcmp(section, "modeline") == 0) {
        if (strcmp(key, "show_git") == 0) modeline_show_git = bool_val;
        if (strcmp(key, "show_stats") == 0) modeline_show_stats = bool_val;
        if (strcmp(key, "show_modes") == 0) modeline_show_modes = bool_val;
        if (strcmp(key, "show_position") == 0) modeline_show_position = bool_val;
        if (strcmp(key, "extension_position") == 0 && type == TOML_STRING) {
            if (strcmp(value, "left") == 0) modeline_ext_position = 0;
            else if (strcmp(value, "right") == 0) modeline_ext_position = 1;
            else if (strcmp(value, "auto") == 0) modeline_ext_position = 2;
        }
        if (strcmp(key, "extension_max_width") == 0 && int_val >= 0) {
            modeline_ext_max_width = int_val;
        }
        sgarbf = true;
    }

    /* [backup] section */
    else if (strcmp(section, "backup") == 0) {
        if (strcmp(key, "enabled") == 0) make_backup = bool_val;
        if (strcmp(key, "directory") == 0 && type == TOML_STRING) {
            safe_strcpy(backup_dir, value, sizeof(backup_dir));
        }
    }

    /* [cursor] section */
    else if (strcmp(section, "cursor") == 0) {
        if (strcmp(key, "style") == 0) {
            if (type == TOML_STRING) {
                if (strcmp(value, "block") == 0) cursor_style = 0;
                else if (strcmp(value, "bar") == 0) cursor_style = 1;
                else if (strcmp(value, "underline") == 0) cursor_style = 2;
            } else if (type == TOML_INT) {
                cursor_style = int_val;
            }
        }
    }

    /* [wrap] section - soft wrap settings */
    else if (strcmp(section, "wrap") == 0) {
        if (strcmp(key, "soft_wrap") == 0 && curwp) {
            curwp->w_wrap_col = bool_val ? 80 : 0;  /* Enable with default 80 */
        }
        if (strcmp(key, "soft_wrap_column") == 0 && int_val > 0 && curwp) {
            if (curwp->w_wrap_col > 0) {  /* Only set if enabled */
                curwp->w_wrap_col = int_val;
            }
        }
        sgarbf = true;
    }

    /* [terminal] section */
    else if (strcmp(section, "terminal") == 0) {
        if (strcmp(key, "enabled") == 0) terminal_enabled = bool_val;
        if (strcmp(key, "height_percent") == 0 && int_val >= 10 && int_val <= 80) {
            terminal_height_percent = int_val;
        }
        if (strcmp(key, "shell") == 0 && type == TOML_STRING) {
            safe_strcpy(terminal_shell, value, sizeof(terminal_shell));
        }
        if (strcmp(key, "scrollback") == 0 && int_val >= 0) {
            terminal_scrollback = int_val;
        }
        if (strcmp(key, "close_on_exit") == 0) terminal_close_on_exit = bool_val;
    }

    /* [clipboard] section - clipboard provider configuration */
    else if (strcmp(section, "clipboard") == 0) {
        if (strcmp(key, "provider") == 0 && type == TOML_STRING) {
            clipboard_set_provider(value);
        }
        else if (strcmp(key, "sync_kills") == 0) {
            clipboard_set_sync_kills(bool_val);
        }
        else if (strcmp(key, "osc52_enabled") == 0) {
            clipboard_set_osc52_enabled(bool_val);
        }
        else if (strcmp(key, "copy_command") == 0 && type == TOML_STRING) {
            clipboard_set_custom_copy(value);
        }
        else if (strcmp(key, "paste_command") == 0 && type == TOML_STRING) {
            clipboard_set_custom_paste(value);
        }
    }

    /* [keys] section - legacy key configuration (removed, use [bindings]) */
    else if (strcmp(section, "keys") == 0) {
        /* Legacy meta/command_prefix/repeat/abort/quote settings removed */
        /* Use [bindings] section for key binding configuration */
    }

    /* [behavior] section - display and behavior flags */
    else if (strcmp(section, "behavior") == 0) {
        if (strcmp(key, "echo_commands") == 0) discmd = bool_val;
        else if (strcmp(key, "echo_input") == 0) disinp = bool_val;
        else if (strcmp(key, "page_overlap") == 0 && int_val >= 0) overlap = int_val;
        else if (strcmp(key, "justify") == 0) justflag = bool_val;
        else if (strcmp(key, "allow_null") == 0) nullflag = bool_val;
    }

    /* [performance] section - tuning parameters */
    else if (strcmp(section, "performance") == 0) {
        if (strcmp(key, "frame_interval") == 0 && int_val >= 50 && int_val <= 200) {
            perf_frame_interval = int_val;
        }
        else if (strcmp(key, "escape_timeout") == 0 && int_val >= 50 && int_val <= 200) {
            perf_escape_timeout = int_val;
        }
        else if (strcmp(key, "parallel_threshold") == 0 && int_val >= 100) {
            perf_parallel_threshold = int_val;
        }
        else if (strcmp(key, "max_threads") == 0 && int_val >= 1 && int_val <= 32) {
            perf_max_threads = int_val;
        }
    }

    /* [extensions] global settings */
    else if (strcmp(section, "extensions") == 0) {
        if (strcmp(key, "timeout") == 0 && int_val > 0) {
            uep_set_timeout(int_val);
            LOG_DEBUGF("UEP: Set timeout to %d ms", int_val);
        }
        else if (strcmp(key, "extension_dir") == 0 && type == TOML_STRING) {
            /* Store extension directory for auto-loading */
            extern void extension_set_autoload_dir(const char *dir);
            extension_set_autoload_dir(value);
            LOG_DEBUGF("Extension: Set autoload dir to %s", value);
        }
        else if (strcmp(key, "scripts_dir") == 0 && type == TOML_STRING) {
            /* Store scripts directory for Layer 2 */
            extern void uep_scripts_set_dir(const char *dir);
            uep_scripts_set_dir(value);
            LOG_DEBUGF("Scripts: Set scripts dir to %s", value);
        }
        else if (strcmp(key, "auto_build") == 0) {
            /* Enable/disable auto-building of extensions from source */
            extern void extension_set_auto_build(bool enabled);
            extension_set_auto_build(bool_val);
            LOG_DEBUGF("Extension: Set auto_build to %s", bool_val ? "true" : "false");
        }
        else if (strcmp(key, "init_timeout") == 0 && int_val >= 1000) {
            /* Set extension initialization timeout (minimum 1 second) */
            extern void ext_host_set_init_timeout(int timeout_ms);
            ext_host_set_init_timeout(int_val);
            LOG_DEBUGF("Extension: Set init_timeout to %d ms", int_val);
        }
    }

    /* [extensions.*] filetype-specific providers */
    else if (strncmp(section, "extensions.", 11) == 0) {
        const char *filetype = section + 11;  /* "python", "c", "rust", etc. */
        if (type == TOML_STRING && *filetype && *key && *value) {
            uep_register_provider(filetype, key, value);
            LOG_DEBUGF("UEP: Registered %s/%s = %s", filetype, key, value);
        }
    }

    /* [extension.*] per-extension configuration settings
     * e.g., [extension.mouse] scroll_lines = 3
     * Extensions query these via api->config_int("mouse", "scroll_lines", default) */
    else if (strncmp(section, "extension.", 10) == 0) {
        const char *ext_name = section + 10;  /* "mouse", "lsp", etc. */
        if (*ext_name && *key) {
            if (type == TOML_INT) {
                char *endptr;
                long val = strtol(value, &endptr, 10);
                if (endptr != value) {
                    extension_config_set_int(ext_name, key, (int)val);
                }
            }
            else if (type == TOML_BOOL) {
                extension_config_set_bool(ext_name, key, strcmp(value, "true") == 0);
            }
            else if (type == TOML_STRING) {
                extension_config_set_string(ext_name, key, value);
            }
        }
    }

    /* [hooks.*] declarative event hooks
     * e.g., [hooks.buffer_save] command = "trim-whitespace" */
    else if (strncmp(section, "hooks.", 6) == 0) {
        const char *hook_name = section + 6;  /* "buffer_save", "idle", etc. */
        if (*hook_name && type == TOML_STRING && strcmp(key, "command") == 0) {
            extension_register_hook_command(hook_name, value);
        }
    }

    /* [keybindings.*] sections */
    else if (strncmp(section, "keybindings.", 12) == 0) {
        if (type == TOML_STRING) {
            /* Find the function */
            fn_t func = fncmatch(value);
            LOG_DEBUGF("TOML: [%s] key='%s' func='%s' -> %s",
                       section, key, value, func ? "OK" : "nil");
            if (func) {
                /* Clean up key name (remove surrounding quotes if present) */
                char key_clean[128];
                safe_strcpy(key_clean, key, sizeof(key_clean));
                size_t klen = strlen(key_clean);
                if (klen >= 2 && (key_clean[0] == '"' || key_clean[0] == '\'')) {
                    // Shift left and remove both quotes
                    memmove(key_clean, key_clean + 1, klen - 2);
                    key_clean[klen - 2] = '\0';
                }
                LOG_DEBUGF("TOML: key_clean='%s' (len=%zu)", key_clean, strlen(key_clean));

                /* Resolve key to modern format */
                keymap_key_t key_code = stock_key(key_clean);
                LOG_DEBUGF("TOML: stock_key('%s') -> code=0x%X mods=0x%X",
                           key_clean, key_code.code, key_code.modifiers);

                /* Determine target keymap
                 * Supported sections:
                 *   keybindings.global  - Global keymap (default emacs bindings)
                 *   keybindings.ctlx    - C-x prefix keymap
                 *   keybindings.meta    - ESC/Meta prefix keymap
                 *   keybindings.help    - C-h help prefix keymap
                 *   keybindings.normal  - Vim normal mode (evil mode)
                 *   keybindings.visual  - Vim visual mode (evil mode)
                 *   keybindings.insert  - Insert mode (alias for global)
                 */
                struct keymap *target_map = nullptr;
                if (strcmp(section, "keybindings.global") == 0) {
                    target_map = atomic_load(&global_keymap);
                } else if (strcmp(section, "keybindings.ctlx") == 0) {
                    target_map = atomic_load(&ctlx_keymap);
                } else if (strcmp(section, "keybindings.meta") == 0) {
                    target_map = atomic_load(&meta_keymap);
                } else if (strcmp(section, "keybindings.help") == 0) {
                    target_map = atomic_load(&help_keymap);
                } else if (strcmp(section, "keybindings.normal") == 0) {
                    target_map = atomic_load(&vim_normal_keymap);
                } else if (strcmp(section, "keybindings.visual") == 0) {
                    target_map = atomic_load(&vim_visual_keymap);
                } else if (strcmp(section, "keybindings.insert") == 0) {
                    /* Insert mode bindings only apply when evil_mode is active.
                     * When evil_mode is off, skip these to avoid overwriting global bindings. */
                    if (vim_mode_active) {
                        target_map = atomic_load(&global_keymap);
                    }
                }

                /* Bind it */
                if (target_map) {
                    keymap_bind(target_map, key_code, func);
                    LOG_DEBUGF("TOML: BOUND key code=0x%X mods=0x%X to %s in %s",
                               key_code.code, key_code.modifiers, value, section);
                } else {
                    LOG_DEBUGF("TOML: NO TARGET MAP for %s", section);
                }
            }
        }
    }
}

int settings_load(int f, int n) {
    (void)f; (void)n;
    LOG_DEBUG("SETTINGS: settings_load() called");
    FILE* fp = open_settings_fp();
    if (!fp) {
        LOG_DEBUG("SETTINGS: No config file found");
        return true; /* No config, defaults apply */
    }
    LOG_DEBUG("SETTINGS: Config file opened successfully");
    
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (sz <= 0 || sz > 1<<20) { fclose(fp); return true; }

    char* buf = SAFE_ALLOC_SIZED(char, (size_t)sz + 1, "settings_toml");
    if (!buf) { fclose(fp); return true; }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    buf[rd] = '\0'; // Safe null termination
    fclose(fp);

    toml_parse(buf, apply_setting, nullptr);

    /* Sync git status with TOML setting */
    git_status_set_enabled(modeline_show_git);

    /* Initialize clipboard subsystem (auto-detect if needed) */
    clipboard_init();

    SAFE_FREE(buf);
    return true;
}

int save_settings_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    
    const char* user_config = get_user_config_file();
    const char* path = user_config[0] ? user_config : UEMACS_SOURCE_EDITOR_DIR "/settings.toml";
    
    FILE* fp = fopen(path, "w");
    if (!fp) {
        LOG_ERRORF("Settings: Save failed - %s (%s)", path, strerror(errno));
        mlwrite("[COULD NOT SAVE SETTINGS: %s]", strerror(errno));
        return false;
    }
    
    int wrap = (curbp && (curbp->b_mode & MDWRAP)) ? 1 : 0;
    int col = (fillcol > 0 ? fillcol : 80);
    
    toml_write_section(fp, "editor");
    toml_write_int(fp, "column_width", col);
    toml_write_bool(fp, "wrap", wrap);
    toml_write_bool(fp, "evil_mode", vim_mode_active);
    toml_write_int(fp, "tab_width", tabmask + 1);
    toml_write_int(fp, "auto_save_interval", gasave);
    toml_write_int(fp, "scroll_step", scrollcount);
    toml_write_bool(fp, "trim_trailing_whitespace", trim_trailing_whitespace);
    toml_write_bool(fp, "insert_final_newline", insert_final_newline);

    toml_write_section(fp, "visual");
    toml_write_bool(fp, "ruler", column_ruler_enabled);
    toml_write_int(fp, "ruler_col", column_ruler_column);
    toml_write_bool(fp, "highlight_line", highlight_current_line);
    toml_write_int(fp, "highlight_style", hiline_style);
    toml_write_int(fp, "ruler_style", ruler_style);
    
    toml_write_section(fp, "theme");
    /* Legacy intensity settings (deprecated) */
    toml_write_int(fp, "highlight_intensity", highlight_intensity_pct);
    toml_write_int(fp, "ruler_intensity", ruler_intensity_pct);
    toml_write_int(fp, "intersection_intensity", intersection_intensity_pct);
    toml_write_int(fp, "highlight_strategy", highlight_strategy);

    /* Helper macro to write hex colors */
    #define WRITE_HEX(key, rgb) do { \
        char hex[8]; \
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", (rgb)[0], (rgb)[1], (rgb)[2]); \
        toml_write_string(fp, key, hex); \
    } while(0)

    /* Background colors */
    WRITE_HEX("modeline_bg", g_palette.modeline_rgb);
    WRITE_HEX("highlight_bg", g_palette.highlight_rgb);
    WRITE_HEX("ruler_bg", g_palette.ruler_rgb);
    WRITE_HEX("selection_bg", g_palette.selection_rgb);
    WRITE_HEX("search_bg", g_palette.search_bg_rgb);
    WRITE_HEX("message_bg", g_palette.message_bg_rgb);

    /* Search foreground (optional) */
    if (g_palette.search_fg_set) {
        WRITE_HEX("search_fg", g_palette.search_fg_rgb);
    } else {
        toml_write_string(fp, "search_fg", "");
    }

    /* Vim mode indicator colors */
    WRITE_HEX("mode_normal", g_palette.mode_normal_rgb);
    WRITE_HEX("mode_insert", g_palette.mode_insert_rgb);
    WRITE_HEX("mode_visual", g_palette.mode_visual_rgb);
    WRITE_HEX("mode_replace", g_palette.mode_replace_rgb);
    WRITE_HEX("mode_evil", g_palette.mode_evil_rgb);

    /* Accent and message colors */
    WRITE_HEX("accent", g_palette.accent_rgb);
    WRITE_HEX("error", g_palette.error_rgb);
    WRITE_HEX("warning", g_palette.warning_rgb);
    WRITE_HEX("success", g_palette.success_rgb);

    #undef WRITE_HEX
    
    toml_write_section(fp, "modeline");
    toml_write_bool(fp, "show_git", modeline_show_git);
    toml_write_bool(fp, "show_stats", modeline_show_stats);
    toml_write_bool(fp, "show_modes", modeline_show_modes);
    toml_write_bool(fp, "show_position", modeline_show_position);
    const char *ext_pos = modeline_ext_position == 0 ? "left" :
                          modeline_ext_position == 1 ? "right" : "auto";
    toml_write_string(fp, "extension_position", ext_pos);
    toml_write_int(fp, "extension_max_width", modeline_ext_max_width);

    toml_write_section(fp, "backup");
    toml_write_bool(fp, "enabled", make_backup);
    if (backup_dir[0] != '\0') {
        toml_write_string(fp, "directory", backup_dir);
    }

    toml_write_section(fp, "cursor");
    const char *style_name = cursor_style == 1 ? "bar" : cursor_style == 2 ? "underline" : "block";
    toml_write_string(fp, "style", style_name);

    toml_write_section(fp, "terminal");
    toml_write_bool(fp, "enabled", terminal_enabled);
    toml_write_int(fp, "height_percent", terminal_height_percent);
    if (terminal_shell[0] != '\0') {
        toml_write_string(fp, "shell", terminal_shell);
    }
    toml_write_int(fp, "scrollback", terminal_scrollback);
    toml_write_bool(fp, "close_on_exit", terminal_close_on_exit);

    toml_write_section(fp, "clipboard");
    toml_write_string(fp, "provider", clipboard_provider_name());
    toml_write_bool(fp, "sync_kills", g_clipboard_config.sync_kills);
    toml_write_bool(fp, "osc52_enabled", g_clipboard_config.osc52_enabled);
    if (g_clipboard_config.copy_cmd[0] != '\0') {
        toml_write_string(fp, "copy_command", g_clipboard_config.copy_cmd);
    }
    if (g_clipboard_config.paste_cmd[0] != '\0') {
        toml_write_string(fp, "paste_command", g_clipboard_config.paste_cmd);
    }

    fclose(fp);
    mlwrite("[SETTINGS SAVED TO %s]", path);
    return true;
}

int open_user_config_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    const char* path = get_user_config_file();
    
    if (path[0] == '\0') path = settings_file();
    
    // Create file if missing
    FILE* fp = fopen(path, "a");
    if (fp) fclose(fp);

    // Open the file into a new buffer
    struct buffer* bp = bfind("settings.toml", true, 0);
    if (!bp) return false;
    safe_strcpy(bp->b_fname, path, NFILEN);
    swbuffer(bp);
    bclear(bp);
    readin(path, false);
    mlwrite("[OPENED %s]", path);
    return true;
}

int list_settings_cmd(int f, int n) {
    (void)f; (void)n;
    int wrap = (curbp && (curbp->b_mode & MDWRAP)) ? 1 : 0;
    mlwrite("SETTINGS: COLUMN_WIDTH=%d WRAP=%s EVIL=%s", 
        (fillcol>0?fillcol:0), 
        wrap?"ON":"OFF",
        vim_mode_active?"ON":"OFF");
    return true;
}

int set_column_width_cmd(int f, int n) {
    int col = n;
    if (!f) {
        char buf[16] = {0};
        int s = minibuf_read("COLUMN WIDTH: ", buf, (int)sizeof(buf));
        if (s != true) return (s == ABORT) ? false : true;
        char *endptr;
        errno = 0;
        long val = strtol(buf, &endptr, 10);
        if (errno != 0 || endptr == buf) {
            mlwrite("[INVALID NUMBER]");
            return false;
        }
        col = (int)val;
    }
    if (col < 1) col = 1;
    if (col > 10000) col = 10000;
    writing_mode_enable(true, col);
    return true;
}

/* Toggle soft wrap for current window */
int soft_wrap_toggle_cmd(int f, int n) {
    if (!curwp) return false;

    if (f) {
        /* With argument, set wrap column directly */
        curwp->w_wrap_col = (n > 0) ? n : 0;
    } else {
        /* Toggle: off -> 80, on -> off */
        curwp->w_wrap_col = (curwp->w_wrap_col > 0) ? 0 : 80;
    }

    sgarbf = true;
    curwp->w_flag |= WFHARD;

    if (curwp->w_wrap_col > 0) {
        mlwrite("[SOFT WRAP AT COLUMN %d]", curwp->w_wrap_col);
    } else {
        mlwrite("[SOFT WRAP DISABLED]");
    }
    return true;
}

/* Set soft wrap column */
int soft_wrap_column_cmd(int f, int n) {
    int col = n;
    if (!f) {
        char buf[16] = {0};
        int s = minibuf_read("SOFT WRAP COLUMN: ", buf, (int)sizeof(buf));
        if (s != true) return (s == ABORT) ? false : true;
        char *endptr;
        errno = 0;
        long val = strtol(buf, &endptr, 10);
        if (errno != 0 || endptr == buf) {
            mlwrite("[INVALID NUMBER]");
            return false;
        }
        col = (int)val;
    }
    if (col < 0) col = 0;
    if (col > 10000) col = 10000;

    if (curwp) {
        curwp->w_wrap_col = col;
        sgarbf = true;
        curwp->w_flag |= WFHARD;
    }

    if (col > 0) {
        mlwrite("[SOFT WRAP AT COLUMN %d]", col);
    } else {
        mlwrite("[SOFT WRAP DISABLED]");
    }
    return true;
}
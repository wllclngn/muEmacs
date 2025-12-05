#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>

#include "config.h"

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_safe.h"

// Settings loader with XDG Base Directory support
// Priority order:
//   1. User config: ~/.config/muemacs/settings.json (XDG_CONFIG_HOME)
//   2. System installed: UEMACS_DATA_DIR/editor/settings.json
//   3. In-tree fallback: UEMACS_SOURCE_EDITOR_DIR/settings.json

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
        snprintf(user_config_path, sizeof(user_config_path), "%s/settings.json", dir);
    }
    return user_config_path;
}

static const char* settings_dir(void) { return UEMACS_DATA_DIR "/editor"; }

static const char* settings_file(void) { return UEMACS_DATA_DIR "/editor/settings.json"; }

// No write-directory probing; writes go to project file.

static const char* bundled_settings_source_file(void) { return UEMACS_SOURCE_EDITOR_DIR "/settings.json"; }

static FILE* open_settings_fp(void) {
    // Try user config first (highest priority)
    const char* user_config = get_user_config_file();
    if (user_config[0] != '\0') {
        FILE* fp = fopen(user_config, "r");
        if (fp) return fp;
    }
    
    // Try system installed config
    FILE* fp = fopen(UEMACS_DATA_DIR "/editor/settings.json", "r");
    if (fp) return fp;
    
    // Fallback to in-tree config
    return fopen(UEMACS_SOURCE_EDITOR_DIR "/settings.json", "r");
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

// Tiny JSON scanner for flat key:value pairs we recognize
static int json_bool(const char* s, const char* key, int* out) {
    const char* p = strstr(s, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    if (strncmp(p, "true", 4) == 0) { *out = 1; return 1; }
    if (strncmp(p, "false", 5) == 0) { *out = 0; return 1; }
    return 0;
}

static int json_int(const char* s, const char* key, int* out) {
    const char* p = strstr(s, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    char* end = nullptr;
    long v = strtol(p, &end, 10);
    if (end == p) return 0;
    *out = (int)v;
    return 1;
}

int settings_load(int f, int n) {
    (void)f; (void)n;
    FILE* fp = open_settings_fp();
    if (!fp) {
        // No settings file present; run with defaults and do not create anything.
        return true;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1<<20) { fclose(fp); return true; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return true; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    (void)rd;
    buf[sz] = '\0';
    fclose(fp);

    int col = 0; int wrap = -1;
    if (json_int(buf, "\"column_width\"", &col) && col > 0) {
        writing_mode_enable(true, col);
    }
    if (json_bool(buf, "\"wrap\"", &wrap)) {
        if (wrap == 0) {
            // disable word wrap if currently enabled
            if (curbp) curbp->b_mode &= ~MDWRAP;
        } else {
            if (col > 0) writing_mode_enable(true, col);
            else writing_mode_enable(true, (fillcol > 0 ? fillcol : 80));
        }
    }

    // Visual helpers
    int ruler = -1, rcol = 0, hll = -1, hls = -1, rls = -1;
    if (json_bool(buf, "\"ruler\"", &ruler)) {
        column_ruler_enabled = ruler ? 1 : 0;
        sgarbf = true;
    }
    if (json_int(buf, "\"rulercol\"", &rcol) && rcol > 0) {
        column_ruler_column = rcol;
        sgarbf = true;
    }
    if (json_bool(buf, "\"highlightline\"", &hll)) {
        highlight_current_line = hll ? 1 : 0;
        sgarbf = true;
    }
    if (json_int(buf, "\"hilinestyle\"", &hls)) {
        if (hls < 0) { hls = 0; }
        if (hls > 4) { hls = 4; } // 4 = reverse (Vim-like)
        hiline_style = hls;
        sgarbf = true;
    }
    if (json_int(buf, "\"rulerstyle\"", &rls)) {
        if (rls < 0) { rls = 0; }
        if (rls > 4) { rls = 4; } // 4 = reverse (Vim-like)
        ruler_style = rls;
        sgarbf = true;
    }

    // Highlight tuning (optional)
    int hip=-1, rip=-1, iip=-1, hst=-1;
    if (json_int(buf, "\"highlight_intensity\"", &hip)) {
        if (hip < 0) {
            hip = 0;
        }
        if (hip > 50) {
            hip = 50;
        }
        highlight_intensity_pct = hip;
        sgarbf = true;
    }
    if (json_int(buf, "\"ruler_intensity\"", &rip)) {
        if (rip < 0) {
            rip = 0;
        }
        if (rip > 50) {
            rip = 50;
        }
        ruler_intensity_pct = rip;
        sgarbf = true;
    }
    if (json_int(buf, "\"intersection_intensity\"", &iip)) {
        if (iip < 0) {
            iip = 0;
        }
        if (iip > 50) {
            iip = 50;
        }
        intersection_intensity_pct = iip;
        sgarbf = true;
    }
    if (json_int(buf, "\"highlight_strategy\"", &hst)) {
        if (hst < 0) {
            hst = 0;
        }
        if (hst > 2) {
            hst = 2;
        }
        highlight_strategy = hst;
        sgarbf = true;
    }

    // Modeline toggles
    int mg=-1, ms=-1, mm=-1, mp=-1;
    if (json_bool(buf, "\"modeline_show_git\"", &mg)) modeline_show_git = mg ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_stats\"", &ms)) modeline_show_stats = ms ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_modes\"", &mm)) modeline_show_modes = mm ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_position\"", &mp)) modeline_show_position = mp ? 1 : 0;

    free(buf);
    return true;
}

int save_settings_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    
    // Try to save to user config first (highest priority)
    const char* user_config = get_user_config_file();
    const char* path = nullptr;
    FILE* fp = nullptr;
    
    if (user_config[0] != '\0') {
        fp = fopen(user_config, "w");
        if (fp) {
            path = user_config;
        }
    }
    
    // Fallback to installed data dir or project file
    if (!fp) {
        path = UEMACS_DATA_DIR "/editor/settings.json";
        fp = fopen(path, "w");
    }
    if (!fp) {
        path = UEMACS_SOURCE_EDITOR_DIR "/settings.json";
        fp = fopen(path, "w");
    }
    
    if (!fp) { 
        mlwrite("(Could not save settings: %s)", strerror(errno)); 
        return false; 
    }
    int wrap = (curbp && (curbp->b_mode & MDWRAP)) ? 1 : 0;
    int col = (fillcol > 0 ? fillcol : 80);
    fprintf(fp, "{\n");
    fprintf(fp, "  \"column_width\": %d,\n", col);
    fprintf(fp, "  \"wrap\": %s,\n", wrap ? "true" : "false");
    fprintf(fp, "  \"ruler\": %s,\n", column_ruler_enabled ? "true" : "false");
    fprintf(fp, "  \"rulercol\": %d,\n", column_ruler_column);
    fprintf(fp, "  \"highlightline\": %s,\n", highlight_current_line ? "true" : "false");
    fprintf(fp, "  \"hilinestyle\": %d,\n", hiline_style);
    fprintf(fp, "  \"rulerstyle\": %d,\n", ruler_style);
    fprintf(fp, "  \"highlight_intensity\": %d,\n", highlight_intensity_pct);
    fprintf(fp, "  \"ruler_intensity\": %d,\n", ruler_intensity_pct);
    fprintf(fp, "  \"intersection_intensity\": %d,\n", intersection_intensity_pct);
    fprintf(fp, "  \"highlight_strategy\": %d,\n", highlight_strategy);
    fprintf(fp, "  \"modeline_show_git\": %s,\n", modeline_show_git ? "true" : "false");
    fprintf(fp, "  \"modeline_show_stats\": %s,\n", modeline_show_stats ? "true" : "false");
    fprintf(fp, "  \"modeline_show_modes\": %s,\n", modeline_show_modes ? "true" : "false");
    fprintf(fp, "  \"modeline_show_position\": %s\n", modeline_show_position ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    mlwrite("(Settings saved to %s)", path);
    return true;
}

int open_user_config_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    const char* path = settings_file();
    // Create file if missing
    FILE* fp = fopen(path, "a");
    if (fp) fclose(fp);

    // Open the file into a new buffer
    struct buffer* bp = bfind("settings.json", true, 0);
    if (!bp) return false;
    safe_strcpy(bp->b_fname, path, NFILEN);
    swbuffer(bp);
    bclear(bp);
    readin(path, false);
    mlwrite("(Opened %s)", path);
    return true;
}

int list_settings_cmd(int f, int n) {
    (void)f; (void)n;
    int wrap = (curbp && (curbp->b_mode & MDWRAP)) ? 1 : 0;
    mlwrite("Settings: column_width=%d wrap=%s", (fillcol>0?fillcol:0), wrap?"on":"off");
    return true;
}

int set_column_width_cmd(int f, int n) {
    int col = n;
    if (!f) {
        char buf[16] = {0};
        int s = mlreply("Column width: ", buf, (int)sizeof(buf));
        if (s != true) return (s == ABORT) ? false : true;
        col = atoi(buf);
    }
    if (col < 1) col = 1;
    if (col > 10000) col = 10000;
    writing_mode_enable(true, col);
    return true;
}

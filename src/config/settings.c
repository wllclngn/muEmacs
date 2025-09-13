#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#include "config.h"

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_safe.h"

// Project-contained JSON settings, no fallbacks.
// Single in-tree location (absolute, compiled at configure time):
//   UEMACS_SOURCE_EDITOR_DIR/settings.json

static const char* settings_dir(void) { return UEMACS_SOURCE_EDITOR_DIR; }

static const char* settings_file(void) { return UEMACS_SOURCE_EDITOR_DIR "/settings.json"; }

// No write-directory probing; writes go to project file.

static const char* bundled_settings_source_file(void) {
    return UEMACS_SOURCE_EDITOR_DIR "/settings.json";
}

static FILE* open_settings_fp(void) {
    return fopen(UEMACS_SOURCE_EDITOR_DIR "/settings.json", "r");
}

static int ensure_dir_abs(const char* dir) {
    struct stat st;
    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    return mkdir(dir, 0700) == 0;
}

static void ensure_settings_dir(void) {
    /* Ensure the single project settings directory exists */
    (void)ensure_dir_abs(UEMACS_SOURCE_EDITOR_DIR);
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
    char* end = NULL;
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
        return TRUE;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 1<<20) { fclose(fp); return TRUE; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return TRUE; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    (void)rd;
    buf[sz] = '\0';
    fclose(fp);

    int col = 0; int wrap = -1;
    if (json_int(buf, "\"column_width\"", &col) && col > 0) {
        writing_mode_enable(TRUE, col);
    }
    if (json_bool(buf, "\"wrap\"", &wrap)) {
        if (wrap == 0) {
            // disable word wrap if currently enabled
            if (curbp) curbp->b_mode &= ~MDWRAP;
        } else {
            if (col > 0) writing_mode_enable(TRUE, col);
            else writing_mode_enable(TRUE, (fillcol > 0 ? fillcol : 80));
        }
    }

    // Visual helpers
    int ruler = -1, rcol = 0, hll = -1, hls = -1, rls = -1;
    if (json_bool(buf, "\"ruler\"", &ruler)) {
        column_ruler_enabled = ruler ? 1 : 0;
        sgarbf = TRUE;
    }
    if (json_int(buf, "\"rulercol\"", &rcol) && rcol > 0) {
        column_ruler_column = rcol;
        sgarbf = TRUE;
    }
    if (json_bool(buf, "\"highlightline\"", &hll)) {
        highlight_current_line = hll ? 1 : 0;
        sgarbf = TRUE;
    }
    if (json_int(buf, "\"hilinestyle\"", &hls)) {
        if (hls < 0) { hls = 0; }
        if (hls > 3) { hls = 3; }
        hiline_style = hls;
        sgarbf = TRUE;
    }
    if (json_int(buf, "\"rulerstyle\"", &rls)) {
        if (rls < 0) { rls = 0; }
        if (rls > 3) { rls = 3; }
        ruler_style = rls;
        sgarbf = TRUE;
    }

    // Modeline toggles
    int mg=-1, ms=-1, mm=-1, mp=-1;
    if (json_bool(buf, "\"modeline_show_git\"", &mg)) modeline_show_git = mg ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_stats\"", &ms)) modeline_show_stats = ms ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_modes\"", &mm)) modeline_show_modes = mm ? 1 : 0;
    if (json_bool(buf, "\"modeline_show_position\"", &mp)) modeline_show_position = mp ? 1 : 0;

    free(buf);
    return TRUE;
}

int save_settings_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    /* Only write to the project settings file */
    const char* path = UEMACS_SOURCE_EDITOR_DIR "/settings.json";
    FILE* fp = fopen(path, "w");
    if (!fp) { mlwrite("(Could not save settings: %s)", strerror(errno)); return FALSE; }
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
    fprintf(fp, "  \"modeline_show_git\": %s,\n", modeline_show_git ? "true" : "false");
    fprintf(fp, "  \"modeline_show_stats\": %s,\n", modeline_show_stats ? "true" : "false");
    fprintf(fp, "  \"modeline_show_modes\": %s,\n", modeline_show_modes ? "true" : "false");
    fprintf(fp, "  \"modeline_show_position\": %s\n", modeline_show_position ? "true" : "false");
    fprintf(fp, "}\n");
    fclose(fp);
    mlwrite("(Settings saved to %s)", path);
    return TRUE;
}

int open_user_config_cmd(int f, int n) {
    (void)f; (void)n;
    ensure_settings_dir();
    const char* path = settings_file();
    // Create file if missing
    FILE* fp = fopen(path, "a");
    if (fp) fclose(fp);

    // Open the file into a new buffer
    struct buffer* bp = bfind("settings.json", TRUE, 0);
    if (!bp) return FALSE;
    safe_strcpy(bp->b_fname, path, NFILEN);
    swbuffer(bp);
    bclear(bp);
    readin(path, FALSE);
    mlwrite("(Opened %s)", path);
    return TRUE;
}

int list_settings_cmd(int f, int n) {
    (void)f; (void)n;
    int wrap = (curbp && (curbp->b_mode & MDWRAP)) ? 1 : 0;
    mlwrite("Settings: column_width=%d wrap=%s", (fillcol>0?fillcol:0), wrap?"on":"off");
    return TRUE;
}

int set_column_width_cmd(int f, int n) {
    int col = n;
    if (!f) {
        char buf[16] = {0};
        int s = mlreply("Column width: ", buf, (int)sizeof(buf));
        if (s != TRUE) return (s == ABORT) ? FALSE : TRUE;
        col = atoi(buf);
    }
    if (col < 1) col = 1;
    if (col > 10000) col = 10000;
    writing_mode_enable(TRUE, col);
    return TRUE;
}

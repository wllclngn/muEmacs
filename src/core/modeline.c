/*
 * modeline.c - Modern Status Line for μEmacs
 *
 * Implements the modern unified status line matching user's terminal aesthetic.
 * Format: ● MODE │ filename │ type │ encoding │ modified │ size │ position │ time
 *
 * C23 compliant
 */

#include <time.h>
#include <strings.h>  /* strcasecmp */

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "line.h"
#include "utf8.h"
#include "string_utils.h"
#include "editor_mode.h"
#include "terminal/palette.h"
#include "uep/extension_api.h"
#include "internal/git_status.h"
#include "internal/memory.h"
#include "internal/display_internal.h"
#include "../util/display_width.h"

/*
 * Detect file type from filename extension
 */
static const char* get_filetype_string(const char *filename)
{
    if (!filename || !filename[0])
        return "TEXT";

    /* Find the last dot in the filename */
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return "TEXT";

    const char *ext = dot + 1;

    /* Programming languages */
    if (strcasecmp(ext, "c") == 0) return "C";
    if (strcasecmp(ext, "h") == 0) return "C";
    if (strcasecmp(ext, "cpp") == 0 || strcasecmp(ext, "cc") == 0 ||
      strcasecmp(ext, "cxx") == 0) return "C++";
    if (strcasecmp(ext, "hpp") == 0 || strcasecmp(ext, "hh") == 0) return "C++";
    if (strcasecmp(ext, "py") == 0) return "PYTHON";
    if (strcasecmp(ext, "go") == 0) return "GO";
    if (strcasecmp(ext, "rs") == 0) return "RUST";
    if (strcasecmp(ext, "js") == 0) return "JS";
    if (strcasecmp(ext, "ts") == 0) return "TS";
    if (strcasecmp(ext, "jsx") == 0) return "JSX";
    if (strcasecmp(ext, "tsx") == 0) return "TSX";
    if (strcasecmp(ext, "java") == 0) return "JAVA";
    if (strcasecmp(ext, "rb") == 0) return "RUBY";
    if (strcasecmp(ext, "php") == 0) return "PHP";
    if (strcasecmp(ext, "pl") == 0 || strcasecmp(ext, "pm") == 0) return "PERL";
    if (strcasecmp(ext, "lua") == 0) return "LUA";
    if (strcasecmp(ext, "hs") == 0) return "HASKELL";
    if (strcasecmp(ext, "ml") == 0 || strcasecmp(ext, "mli") == 0) return "OCAML";
    if (strcasecmp(ext, "ex") == 0 || strcasecmp(ext, "exs") == 0) return "ELIXIR";
    if (strcasecmp(ext, "erl") == 0) return "ERLANG";
    if (strcasecmp(ext, "clj") == 0) return "CLOJURE";
    if (strcasecmp(ext, "scala") == 0) return "SCALA";
    if (strcasecmp(ext, "kt") == 0) return "KOTLIN";
    if (strcasecmp(ext, "swift") == 0) return "SWIFT";
    if (strcasecmp(ext, "zig") == 0) return "ZIG";
    if (strcasecmp(ext, "nim") == 0) return "NIM";
    if (strcasecmp(ext, "v") == 0) return "V";
    if (strcasecmp(ext, "d") == 0) return "D";
    if (strcasecmp(ext, "cs") == 0) return "C#";
    if (strcasecmp(ext, "fs") == 0 || strcasecmp(ext, "fsx") == 0) return "F#";
    if (strcasecmp(ext, "vb") == 0) return "VB";

    /* Shell/scripting */
    if (strcasecmp(ext, "sh") == 0) return "SHELL";
    if (strcasecmp(ext, "bash") == 0) return "BASH";
    if (strcasecmp(ext, "zsh") == 0) return "ZSH";
    if (strcasecmp(ext, "fish") == 0) return "FISH";
    if (strcasecmp(ext, "ps1") == 0) return "PWSH";

    /* Markup/data */
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) return "HTML";
    if (strcasecmp(ext, "css") == 0) return "CSS";
    if (strcasecmp(ext, "scss") == 0 || strcasecmp(ext, "sass") == 0) return "SCSS";
    if (strcasecmp(ext, "xml") == 0) return "XML";
    if (strcasecmp(ext, "json") == 0) return "JSON";
    if (strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0) return "YAML";
    if (strcasecmp(ext, "toml") == 0) return "TOML";
    if (strcasecmp(ext, "ini") == 0) return "INI";
    if (strcasecmp(ext, "conf") == 0 || strcasecmp(ext, "cfg") == 0) return "CONF";
    if (strcasecmp(ext, "md") == 0 || strcasecmp(ext, "markdown") == 0) return "MD";
    if (strcasecmp(ext, "rst") == 0) return "RST";
    if (strcasecmp(ext, "tex") == 0) return "TEX";
    if (strcasecmp(ext, "org") == 0) return "ORG";

    /* Build/config */
    if (strcasecmp(ext, "mk") == 0) return "MAKE";
    if (strcasecmp(ext, "cmake") == 0) return "CMAKE";
    if (strcasecmp(ext, "dockerfile") == 0) return "DOCKER";

    /* SQL/DB */
    if (strcasecmp(ext, "sql") == 0) return "SQL";

    /* Lisp family */
    if (strcasecmp(ext, "el") == 0) return "ELISP";
    if (strcasecmp(ext, "lisp") == 0 || strcasecmp(ext, "cl") == 0) return "LISP";
    if (strcasecmp(ext, "scm") == 0 || strcasecmp(ext, "ss") == 0) return "SCHEME";
    if (strcasecmp(ext, "rkt") == 0) return "RACKET";

    /* Assembly */
    if (strcasecmp(ext, "asm") == 0 || strcasecmp(ext, "s") == 0) return "ASM";

    /* Text/docs */
    if (strcasecmp(ext, "txt") == 0) return "TEXT";
    if (strcasecmp(ext, "log") == 0) return "LOG";
    if (strcasecmp(ext, "diff") == 0 || strcasecmp(ext, "patch") == 0) return "DIFF";

    /* Vim */
    if (strcasecmp(ext, "vim") == 0) return "VIM";
    if (strcasecmp(ext, "vimrc") == 0) return "VIM";

    return "TEXT";
}

/*
 * Count total lines in buffer (fallback when cache unavailable)
 */
static int getlinecount_modern(struct buffer *bp)
{
    struct line *lp;
    int count = 0;

    lp = lforw(bp->b_linep);
    while (lp != bp->b_linep) {
        count++;
        lp = lforw(lp);
    }
    return count;
}

/*
 * Classic Linus Torvalds uEmacs/PK 4.0 modeline
 * Format: --  uEmacs/PK 4.0: buffername (MODES) filename  Bot/Top/All/nn%
 */
void classic_modeline(struct window *wp)
{
    struct buffer *bp = wp->w_bufp;
    int n = wp->w_toprow + wp->w_ntrows;

    if (n < 0 || n >= term.t_mrow || !vscreen || !vscreen[n])
        return;

    /* Check for extension full takeover FIRST */
    if (extension_modeline_has_full_override()) {
        char *full_ml = extension_get_modeline_full();
        if (full_ml) {
            vscreen[n]->v_flag |= VFCHG | VFREQ;
            vtmove(n, 0);
            for (char *fcp = full_ml; *fcp && vtcol < term.t_ncol; fcp++)
                vtputc(*fcp);
            while (vtcol < term.t_ncol)
                vtputc(' ');
            free(full_ml);
            return;
        }
    }

    vscreen[n]->v_flag |= VFCHG | VFREQ;
    vtmove(n, 0);

    /* Modified indicator: -- unmodified, ** modified */
    char mod_ch = (bp->b_flag & BFCHG) ? '*' : '-';
    vtputc(mod_ch);
    vtputc(mod_ch);

    /* View mode indicator */
    vtputc((bp->b_mode & MDVIEW) ? '%' : ' ');
    vtputc(' ');

    /* Editor name */
    const char *ename = "uEmacs/PK 4.0: ";
    for (const char *p = ename; *p; p++)
        vtputc(*p);

    /* Buffer name */
    for (const char *p = bp->b_bname; *p && vtcol < term.t_ncol - 20; p++)
        vtputc(*p);
    vtputc(' ');

    /* Active modes in parens */
    vtputc('(');
    for (int i = 0; i < NUMMODES - 1; i++) {
        if (bp->b_mode & (1 << i))
            vtputc(modecode[i]);
    }
    vtputc(')');
    vtputc(' ');

    /* Filename if different from buffer name */
    if (bp->b_fname[0] && strcmp(bp->b_fname, bp->b_bname) != 0) {
        for (const char *p = bp->b_fname; *p && vtcol < term.t_ncol - 10; p++)
            vtputc(*p);
    }

    /* Position indicator: All/Top/Bot/nn% */
    int current_line = get_line_number_cached(wp);
    int total_lines = 0;
    long dummy_size = 0;
    int dummy_words = 0;
    buffer_get_stats_fast(bp, &total_lines, &dummy_size, &dummy_words);

    char pos[16];
    if (total_lines <= wp->w_ntrows)
        safe_strcpy(pos, "All", sizeof(pos));
    else if (current_line <= 1)
        safe_strcpy(pos, "Top", sizeof(pos));
    else if (current_line >= total_lines)
        safe_strcpy(pos, "Bot", sizeof(pos));
    else
        snprintf(pos, sizeof(pos), "%d%%", (current_line * 100) / total_lines);

    /* Pad to right-align position indicator */
    int pos_len = (int)strlen(pos);
    int target_col = term.t_ncol - pos_len - 1;
    while (vtcol < target_col)
        vtputc(' ');

    for (const char *p = pos; *p && vtcol < term.t_ncol; p++)
        vtputc(*p);

    /* Fill remainder */
    while (vtcol < term.t_ncol)
        vtputc(' ');
}

static int modeline_style = 0;  // 0 = classic, 1 = modern

/*
 * Toggle modeline style between classic and modern (M-x modeline-style)
 */
int modeline_style_cmd(int f, int n)
{
    (void)f; (void)n;
    modeline_style = modeline_style ? 0 : 1;
    sgarbf = true;
    mlwrite("[Modeline: %s]", modeline_style ? "modern" : "classic");
    return true;
}

/*
 * Modern modeline matching user's preferred format
 */
void modern_modeline(struct window *wp)
{
    char left_info[256];
    char right_info[128];
    struct buffer *bp = wp->w_bufp;
    int n = wp->w_toprow + wp->w_ntrows;
    int col_pos = 0;
    char *cp;

    /* Bounds check for vscreen array access */
    if (n < 0 || n >= term.t_mrow || !vscreen || !vscreen[n]) {
        return;  /* Safety: don't access invalid vscreen */
    }

    /* Check for extension full takeover FIRST */
    if (extension_modeline_has_full_override()) {
        char *full_ml = extension_get_modeline_full();
        if (full_ml) {
            /* Render the extension's modeline directly */
            vscreen[n]->v_flag |= VFCHG | VFREQ;
            vtmove(n, 0);

            /* Output the full modeline string */
            for (char *fcp = full_ml; *fcp && vtcol < term.t_ncol; fcp++) {
                vtputc(*fcp);
            }
            /* Pad remainder with spaces */
            while (vtcol < term.t_ncol) {
                vtputc(' ');
            }
            free(full_ml);
            return;
        }
    }

    /* Set up virtual screen - VFREQ flag tells updateline() this is status line */
    vscreen[n]->v_flag |= VFCHG | VFREQ;
    vtmove(n, 0);

    /* --- VIM MODE INDICATOR --- */
    char vim_mode_str[32] = "";
    bool show_evil_splash = false;
    int vim_active = atomic_load(&vim_mode_active);

    if (vim_active) {
        long current_time = (long)time(nullptr);
        if (evil_mode_start_time > 0 && (current_time - evil_mode_start_time) < 3) {
            show_evil_splash = true;
        }

        if (show_evil_splash) {
            safe_strcpy(vim_mode_str, "   EVIL", sizeof(vim_mode_str));
        } else {
            enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
            switch (mode) {
                case MODE_NORMAL:      safe_strcpy(vim_mode_str, "   NORMAL", sizeof(vim_mode_str)); break;
                case MODE_INSERT:      safe_strcpy(vim_mode_str, "   INSERT", sizeof(vim_mode_str)); break;
                case MODE_VISUAL:      safe_strcpy(vim_mode_str, "   VISUAL", sizeof(vim_mode_str)); break;
                case MODE_VISUAL_LINE: safe_strcpy(vim_mode_str, "   V-LINE", sizeof(vim_mode_str)); break;
                case MODE_REPLACE:     safe_strcpy(vim_mode_str, "   REPLACE", sizeof(vim_mode_str)); break;
                default:               safe_strcpy(vim_mode_str, "   NORMAL", sizeof(vim_mode_str)); break;
            }
        }
    }

    /* Get cached file statistics instantly (O(1) operation) */
    long file_size = 0;
    int total_lines = 0;
    int word_count = 0;
    buffer_get_stats_fast(bp, &total_lines, &file_size, &word_count);

    /* Ensure valid stats - force recalc if they look uninitialized */
    if (total_lines <= 0 || (file_size == 0 && bp->b_linep && lforw(bp->b_linep) != bp->b_linep)) {
        buffer_mark_stats_dirty(bp);
        buffer_get_stats_fast(bp, &total_lines, &file_size, &word_count);
    }

    /* Get cached line number instantly (O(1) operation) */
    int current_line = get_line_number_cached(wp);

    /* Format left: [MODE] FILENAME  TYPE  ENCODING  [MODIFIED] [WE] */
    const char *mod_indicator = (bp->b_flag & BFCHG) ? "    Δ" : "";

    /* --- GIT STATUS INTEGRATION --- */
    char git_info[64] = "";
    if (modeline_show_git) {
        git_status_request_async(nullptr);
        git_status_get_cached(git_info, sizeof(git_info));
    }

    /* --- EXTENSION MODELINE SEGMENTS --- */
    char *ext_high = nullptr;
    char *ext_low = nullptr;

    if (modeline_ext_position == 0) {
        ext_high = extension_get_modeline_segments(-1);
    } else if (modeline_ext_position == 1) {
        ext_low = extension_get_modeline_segments(-1);
    } else {
        ext_high = extension_get_modeline_segments(UEMACS_MODELINE_URGENCY_HIGH);
        ext_low = extension_get_modeline_segments(UEMACS_MODELINE_URGENCY_LOW);
    }

    const char* fname = bp->b_fname[0] ? bp->b_fname : bp->b_bname;

    /* Build vim mode prefix for status line */
    char vim_prefix[96] = "";
    if (vim_active && vim_mode_str[0]) {
        if (show_evil_splash) {
            safe_strcpy(vim_prefix, vim_mode_str, sizeof(vim_prefix));
        } else {
            enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
            int mode_idx;
            switch (mode) {
                case MODE_NORMAL:      mode_idx = 0; break;
                case MODE_INSERT:      mode_idx = 1; break;
                case MODE_VISUAL:      mode_idx = 2; break;
                case MODE_VISUAL_LINE: mode_idx = 3; break;
                case MODE_REPLACE:     mode_idx = 5; break;
                default:               mode_idx = 0; break;
            }
            snprintf(vim_prefix, sizeof(vim_prefix), "%s%s\x1b[39m", sgr_mode_color(mode_idx), vim_mode_str);
        }
    }

    const char *ftype = get_filetype_string(fname);

    if (vim_active && vim_mode_str[0]) {
        if (modeline_show_modes) {
            if (git_info[0])
                safe_snprintf(left_info, sizeof(left_info), "%s    %s    %s    %s    UTF-8%s", vim_prefix, fname, git_info, ftype, mod_indicator);
            else
                safe_snprintf(left_info, sizeof(left_info), "%s    %s    %s    UTF-8%s", vim_prefix, fname, ftype, mod_indicator);
        } else {
            if (git_info[0])
                safe_snprintf(left_info, sizeof(left_info), "%s    %s    %s%s", vim_prefix, fname, git_info, mod_indicator);
            else
                safe_snprintf(left_info, sizeof(left_info), "%s    %s%s", vim_prefix, fname, mod_indicator);
        }
    } else if (modeline_show_modes) {
        if (git_info[0])
            safe_snprintf(left_info, sizeof(left_info), "   %s    %s    %s    UTF-8%s", fname, git_info, ftype, mod_indicator);
        else
            safe_snprintf(left_info, sizeof(left_info), "   %s    %s    UTF-8%s", fname, ftype, mod_indicator);
    } else {
        if (git_info[0])
            safe_snprintf(left_info, sizeof(left_info), "   %s    %s%s", fname, git_info, mod_indicator);
        else
            safe_snprintf(left_info, sizeof(left_info), "   %s%s", fname, mod_indicator);
    }

    /* Append high-urgency extension segments to left side */
    if (ext_high && ext_high[0]) {
        size_t curr_len = strlen(left_info);
        if (curr_len + strlen(ext_high) + 5 < sizeof(left_info)) {
            strncat(left_info, "    ", sizeof(left_info) - curr_len - 1);
            strncat(left_info, ext_high, sizeof(left_info) - strlen(left_info) - 1);
        }
    }

    /* Format right: C{COL} L{LINE}/{TOTAL}  {SIZE} {WORDS} */
    char size_str[32];
    if (file_size < 1024) {
        snprintf(size_str, sizeof(size_str), "%ldB", file_size);
    } else if (file_size < 1048576) {
        snprintf(size_str, sizeof(size_str), "%.2fKB", (double)file_size / 1024.0);
    } else if (file_size < 1073741824) {
        snprintf(size_str, sizeof(size_str), "%.2fMB", (double)file_size / 1048576.0);
    } else if (file_size < 1099511627776LL) {
        snprintf(size_str, sizeof(size_str), "%.2fGB", (double)file_size / 1073741824.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.2fTB", (double)file_size / 1099511627776.0);
    }

    /* Fast UTF-8 aware column calculation using atomic cache */
    int current_col = calculate_display_column_cached(wp->w_dotp, wp->w_doto, 8) + 1;

    right_info[0] = '\0';
    if (ext_low && ext_low[0]) {
        strncat(right_info, ext_low, sizeof(right_info) - 1);
        strncat(right_info, "    ", sizeof(right_info) - strlen(right_info) - 1);
    }
    if (modeline_show_position) {
        char pos[64];
        snprintf(pos, sizeof(pos), "C%d    L%d/%d", current_col, current_line, total_lines);
        strncat(right_info, pos, sizeof(right_info) - strlen(right_info) - 1);
    }
    if (modeline_show_stats) {
        char stats[64];
        snprintf(stats, sizeof(stats), "    %s    %dW", size_str, word_count);
        strncat(right_info, stats, sizeof(right_info) - strlen(right_info) - 1);
    }
    strncat(right_info, "   ", sizeof(right_info) - strlen(right_info) - 1);

    /* Display left info with proper UTF-8 handling for delta symbol */
    int right_len = (int)strlen(right_info);
    int left_len = (int)strlen(left_info);

    /* Render left_info with UTF-8 handling, passing ANSI escapes through */
    int i = 0;
    while (i < left_len && col_pos < term.t_ncol - right_len - 1) {
        unsigned char byte = (unsigned char)left_info[i];

        /* Output ANSI escape sequences directly to terminal (for colors) */
        if (byte == 0x1b) {
            int seq_start = i;
            i++;
            while (i < left_len) {
                unsigned char eb = (unsigned char)left_info[i];
                i++;
                if ((eb >= 'A' && eb <= 'Z') || (eb >= 'a' && eb <= 'z'))
                    break;
            }
            char seq_buf[64];
            int seq_len = i - seq_start;
            if (seq_len < (int)sizeof(seq_buf)) {
                memcpy(seq_buf, &left_info[seq_start], (size_t)seq_len);
                seq_buf[seq_len] = '\0';
                vtputs(seq_buf);
            }
            continue;
        }

        /* Normal character processing with UTF-8 */
        unicode_t c;
        int bytes = (int)utf8_to_unicode(left_info, (unsigned)i, (unsigned)left_len, &c);
        if (bytes > 0) {
            vtputc((int)c);
            i += bytes;
            col_pos++;
        } else {
            break;
        }
    }

    /* Fill middle with spaces */
    while (col_pos < term.t_ncol - right_len) {
        vtputc(' ');
        col_pos++;
    }

    /* Display right info */
    cp = right_info;
    while (*cp && col_pos < term.t_ncol) {
        vtputc(*cp++);
        col_pos++;
    }

    /* Fill remainder */
    while (col_pos < term.t_ncol) {
        vtputc(' ');
        col_pos++;
    }

    /* Cleanup extension segment strings */
    if (ext_high) free(ext_high);
    if (ext_low) free(ext_low);
}

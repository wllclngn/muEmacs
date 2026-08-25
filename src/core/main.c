/* main.c
 *
 * μEmacs main loop and command processing (modern Linux build).
 * Initializes subsystems, parses args, reads input files, and
 * drives the editor loop.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

/* Make global definitions not external. */
#define	maindef

#include "estruct.h" /* Global structures and defines. */
#include "edef.h"    /* Global definitions. */
#include "efunc.h"   /* Function declarations and name table. */
#include "line.h"    /* Line operations (linsert, lgetc, etc.) */
#include "keymap.h"  /* Keymap system functions. */
#include "version.h"
#include "string_utils.h"
#include "memory.h"
#include "error.h"
#include "../util/display_width.h"
#include "util/logger.h"
#include "terminal/signal_handler.h"
#include "terminal/terminal.h"  /* Modern Terminal System */
#include "terminal/input_state.h" /* Unified input parser */
#include "editor_mode.h"        /* Vim mode state for replace mode */
#include "uep/ext_host.h"       /* Out-of-process extension host */
#include "util/file_preload.h"  /* Async file preloading */

#ifndef GOOD
#define GOOD    0
#endif

#include <unistd.h>
static void emergencyexit(int);
void check_emergency_exit(void);
#ifdef SIGWINCH
extern void sizesignal(int);
void check_pending_resize(void);
#endif

struct main_args {
    int viewflag;		// view mode flag
    int gotoflag;		// goto line flag
    int gline;		// line to goto
    int searchflag;		// search at startup flag
    int errflag;		// C error processing flag
    int startflag;		// startup file executed flag
    char pat[NPAT];		// search pattern
    struct buffer *firstbp;	// first buffer pointer
};

struct main_state {
    int c;			// command character (legacy format, for compatibility)
    int f;			// default flag
    int n;			// numeric repeat count
    int mflag;		// negative flag on repeat
    int basec;		// c stripped of meta character
    int saveflag;		// temp store for lastflag
    int newc;		// new character
    input_key_event_t evt;	// current key event (modern format)
};

static int handle_help_version(int argc, char **argv);
static const char *extract_first_filename(int argc, char **argv);
static void initialize_editor(void);
static int parse_command_line(int argc, char **argv, struct main_args *args);
static void process_input_files(struct main_args *args, struct main_state *state);
static int main_editor_loop(struct main_args *args, struct main_state *state);

void usage(int status)
{
  printf("Usage: %s filename\n", PROGRAM_NAME);
  printf("   or: %s [options]\n\n", PROGRAM_NAME);
  fputs("      +          start at the end of file\n", stdout);
  fputs("      +<n>       start at line <n>\n", stdout);
  fputs("      -g[G]<n>   go to line <n>\n", stdout);
  fputs("      --help     display this help and exit\n", stdout);
  fputs("      --version  output version information and exit\n", stdout);

  exit(status);
}

int muemacs_main_entry(int argc, char **argv)
{
    struct main_args args = {0};
    struct main_state state = {0};
    int result;

    // Crash handlers first — catch SIGSEGV/SIGABRT even during init
    signal_install_crash_handlers();

    // Initialize debug logging (no-op if UEMACS_DEBUG_LOG=0)
    logger_init();
    LOG_INFO("uEmacs: Starting...");

    // Handle version and help early
    if (handle_help_version(argc, argv))
        return EXIT_SUCCESS;

    // Start async file preload BEFORE editor init (runs in parallel)
    const char *first_file = extract_first_filename(argc, argv);
    if (first_file) {
        LOG_INFOF("uEmacs: Preloading file: %s", first_file);
        file_preload_start(first_file);
    }

    // Initialize editor subsystems (runs IN PARALLEL with file preload)
    initialize_editor();
    
    if (!parse_command_line(argc, argv, &args)) {
        return EXIT_FAILURE;
    }
    
    // Process input files
    process_input_files(&args, &state);
    
    // Run main editor loop
    result = main_editor_loop(&args, &state);

    // Cleanup extension system
    LOG_INFO("uEmacs: Shutting down...");
    extern void extension_cleanup(void);
    extern void extension_api_cleanup(void);
    extension_cleanup();
    extension_api_cleanup();

    // Close debug logging (no-op if UEMACS_DEBUG_LOG=0)
    logger_close();

    return result;
}

// Extract first filename from argv for early preloading
// Returns nullptr if no file argument found
static const char *extract_first_filename(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '+') continue;          // Skip +line
        if (argv[i][0] == '-') continue;          // Skip -flags
        if (argv[i][0] == '@') continue;          // Skip @macros
        return argv[i];                            // First bare filename
    }
    return nullptr;
}

// Handle --help and --version early
static int handle_help_version(int argc, char **argv)
{
    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0) {
            usage(EXIT_FAILURE);
        }
        if (strcmp(argv[1], "--version") == 0) {
            version();
            return 1; // Signal to exit
        }
    }
    return 0; // Continue
}

static void initialize_editor(void)
{
    vtinit();		// Display
    display_width_init();	// UTF-8 display width calculations
    edinit("main");		// Buffers, windows
    varinit();		// user variables
    keymap_init_defaults();	// Initialize keymap infrastructure (prefix bindings only)

    // Initialize vim keymaps BEFORE settings_load so TOML can bind to them
    extern void vim_init_keymaps(void);
    vim_init_keymaps();

#ifdef DEBUG
    // Verify names table is sorted for binary search
    extern void verify_names_sorted(void);
    verify_names_sorted();
#endif

    // Load user settings from TOML - this is the single source of truth for keybindings
    settings_load(false, 0);

    // Watch settings.toml for live reload (inotify IN_CLOSE_WRITE)
    settings_watch_init();

    // Initialize palette (environment overrides after TOML)
    extern void palette_init(void);
    palette_init();

    // Force initial display before extension loading (extensions may batch-build,
    // which can take tens of seconds - don't leave the user staring at a blank screen)
    update(true);

    // Initialize extension system (Layer 3)
    extern void extension_init(void);
    extern void extension_api_init(void);
    extern void extension_autoload(void);
    extension_api_init();
    extension_init();

    // Auto-load extensions from configured directory
    extension_autoload();

    // Initialize and load scripts (Layer 2)
    extern void uep_scripts_init(void);
    extern int uep_scripts_load(void);
    uep_scripts_init();
    uep_scripts_load();
}

static int parse_command_line(int argc, char **argv, struct main_args *args)
{
    int carg;
    struct buffer *bp;
    int firstfile = true;
    char bname[NBUFN];
    
    args->viewflag = false;
    args->gotoflag = false;
    args->gline = 0;
    args->searchflag = false;
    args->startflag = false;
    args->errflag = false;
    args->firstbp = nullptr;

    // Parse the command line
    for (carg = 1; carg < argc; ++carg) {
        if (argv[carg][0] == '+') {
            args->gotoflag = true;
            char *endptr;
            long val = strtol(&argv[carg][1], &endptr, 10);
            args->gline = (*endptr == '\0' && val > 0) ? (int)val : 1;
        } else
        if (argv[carg][0] == '-') {
            switch (argv[carg][1]) {
            case 'a': case 'A':
                args->errflag = true;
                break;
            case 'e': case 'E':
                args->viewflag = false;
                break;
            case 'g': case 'G':
                args->gotoflag = true;
                {
                    char *endptr;
                    long val = strtol(&argv[carg][2], &endptr, 10);
                    args->gline = (*endptr == '\0' && val > 0) ? (int)val : 1;
                }
                break;
            case 'n': case 'N':
                nullflag = true;
                break;
            case 'r': case 'R':
                restflag = true;
                break;
            case 's': case 'S':
                args->searchflag = true;
                safe_strcpy(args->pat, &argv[carg][2], NPAT);
                break;
            case 'v': case 'V':
                args->viewflag = true;
                break;
            default:
                break;
            }
        } else if (argv[carg][0] == '@') {
            // Process Startup macros
            if (startup(&argv[carg][1]) == true)
                args->startflag = true;
        } else {
            // Process an input file
            makename(bname, argv[carg]);
            unqname(bname);

            bp = bfind(bname, true, 0);
            safe_strcpy(bp->b_fname, argv[carg], NFILEN);
            bp->b_active = false;
            if (firstfile) {
                args->firstbp = bp;
                firstfile = false;
            }

            // Set the modes appropriately
            if (args->viewflag)
                bp->b_mode |= MDVIEW;
        }
    }

    return 1; // Success
}

static void process_input_files(struct main_args *args, struct main_state *state)
{
    struct buffer *bp;

    // Signal handlers (SIGHUP, SIGTERM, etc.) are now set up via sigaction()
    // in signal_handlers_init() called from curses.c during terminal init

    // if we are C error parsing... run it!
    if (args->errflag) {
        if (startup("error.cmd") == true)
            args->startflag = true;
    }

    // if invoked with no other startup files, run the system startup file here
    if (args->startflag == false) {
        (void)startup("");  // best-effort: editor continues even without startup file
        args->startflag = true;
    }
    discmd = true;		// P.K.

    // if there are any files to read, read the first one!
    bp = bfind("main", false, 0);
    if (args->firstbp != nullptr && (gflags & GFREAD)) {
        if (swbuffer(args->firstbp) == true)
            (void)zotbuf(bp);  // cleanup default buffer; failure non-fatal
    } else {
        bp->b_mode |= (uint32_t)gmode;
    }

    // Deal with startup gotos and searches
    if (args->gotoflag && args->searchflag) {
        update(false);
        mlwrite("[CANNOT SEARCH AND GOTO AT THE SAME TIME]");
    } else if (args->gotoflag) {
        if (gotoline(true, args->gline) == false) {
            update(false);
            mlwrite("[BOGUS GOTO ARGUMENT]");
        }
    } else if (args->searchflag) {
        if (forwhunt(false, 0) == false)
            update(false);
    }
}

static int main_editor_loop(struct main_args *args, struct main_state *state)
{
    // Setup to process commands.
    lastflag = 0;  // Fake last flags.

loop:
    // Check for pending signals first
    check_emergency_exit();
#ifdef SIGWINCH
    check_pending_resize();
#endif

    /* Poll out-of-process extensions for pending messages (non-blocking) */
    ext_host_poll_nonblocking();

    /* Initialize any extensions that finished loading in background */
    extern int extension_poll_pending(void);
    extension_poll_pending();

    /* Check for config file changes (non-blocking inotify) */
    settings_check_reload();

    /* Check EVIL flash timer - refresh modeline when 3-second splash expires */
    {
        static bool evil_flash_was_active = false;
        bool evil_flash_active = (evil_mode_start_time > 0 &&
                     (time(nullptr) - evil_mode_start_time) < 3);
        if (evil_flash_was_active && !evil_flash_active) {
            /* Flash just expired - refresh modelines */
            upmode();
        }
        evil_flash_was_active = evil_flash_active;
    }

    /*
    * Fix up the screen (montauk/OUROBOROS pattern)
    *
    * Only call update() when the display actually needs updating.
    * This eliminates ~88,000 redundant update() calls per second
    * when idle, reducing CPU usage from ~100% to <1%.
    */
    if (display_needs_update()) {
        update(false);
        mark_display_clean();
    }

    /* Check for terminal output asynchronously */
    extern void check_terminal_output(void);
    check_terminal_output();

    /* Update display after terminal output (shell prompt, command output, etc.) */
    if (display_needs_update()) {
        update(false);
        mark_display_clean();
    }

    /* Fire extension idle hooks before blocking on input */
    extension_emit_idle();

    /* Get the next command from the keyboard (unified parser) */
    if (input_read_event(&state->evt) < 0) {
        return EXIT_SUCCESS;  /* EOF or error */
    }
    state->c = evt_char(&state->evt);  /* Character code only */

    /* Fire pre-dispatch raw input event FIRST. Modal-editing extensions
    * (c_evil / vim) subscribe to "input:raw" to build count prefixes,
    * operator-pending state, and consume keys before the core keymap
    * looks them up. If the extension sets event->consumed, we skip both
    * the legacy input:key hook and the binding dispatch entirely. */
    if (extension_emit_raw_key(&state->evt)) {
        goto loop;
    }

    /* Fire extension key hooks - if any hook consumes the key, skip normal processing */
    /* Map control chars back to raw values for hooks (parser converts Enter→Ctrl-M, Tab→Ctrl-I) */
    int hook_key;
    if (evt_is_enter(&state->evt)) {
        hook_key = '\r';
    } else if (evt_is_tab(&state->evt)) {
        hook_key = '\t';
    } else {
        hook_key = state->c;
    }
    if (extension_emit_key(hook_key)) {
        goto loop;  /* Key was handled by extension */
    }

    /* Clear command line if something is displayed */
    if (mpresf != false) {
        mlerase();
        if (display_needs_update()) {
            update(false);
            mark_display_clean();
        }
    }
    state->f = false;
    state->n = 1;

    /* META-# processing: M-0 through M-9 and M-- set numeric argument */
    if (evt_is_meta_digit(&state->evt) ||
      (evt_has_meta(&state->evt) && state->c == '-')) {
        state->f = true;
        state->n = 0;
        state->mflag = 1;
        /* Process digits/minus until non-digit */
        while ((state->c >= '0' && state->c <= '9') || state->c == '-') {
            if (state->c == '-') {
                if ((state->mflag == -1) || (state->n != 0))
                    break;
                state->mflag = -1;
            } else {
                state->n = state->n * 10 + (state->c - '0');
            }
            if ((state->n == 0) && (state->mflag == -1))
                mlwrite("ARG:");
            else
                mlwrite("ARG: %d", state->n * state->mflag);

            if (input_read_event(&state->evt) < 0) return EXIT_FAILURE;
            state->c = evt_char(&state->evt);
        }
        state->n = state->n * state->mflag;

        /* If ESC follows numeric arg, treat next key as Meta-prefixed */
        if (evt_is_esc(&state->evt)) {
            if (input_read_event(&state->evt) < 0) return EXIT_FAILURE;
            state->evt.modifiers |= MOD_META;  /* Add meta to next key */
        }
    }

    /* ^U repeat argument processing. Not in vim mode: there C-u is a
    * scroll command dispatched via the vim keymap, and counts are
    * plain digits handled by the modal extension. */
    if (!vim_mode_active && evt_is_ctrl(&state->evt, 'U')) {
        state->f = true;
        state->n = 4;
        state->mflag = 0;
        mlwrite("ARG: 4");
        while (input_read_event(&state->evt) >= 0) {
            state->c = evt_char(&state->evt);
            bool is_digit = (state->c >= '0' && state->c <= '9');
            bool is_dash = (state->c == '-');
            bool is_repeat = evt_is_ctrl(&state->evt, 'U');
            if (!is_digit && !is_dash && !is_repeat)
                break;
            if (is_repeat) {
                if ((state->n > 0) == ((state->n * 4) > 0))
                    state->n = state->n * 4;
                else
                    state->n = 1;
            } else if (is_dash) {
                if (state->mflag)
                    break;
                state->n = 0;
                state->mflag = -1;
            } else {
                if (!state->mflag) {
                    state->n = 0;
                    state->mflag = 1;
                }
                state->n = 10 * state->n + state->c - '0';
            }
            mlwrite("ARG: %d",
                (state->mflag >= 0) ? state->n : (state->n ? -state->n : -1));
        }
        if (state->mflag == -1) {
            if (state->n == 0)
                state->n++;
            state->n = -state->n;
        }

        /* If ESC follows ^U arg, treat next key as Meta-prefixed */
        if (evt_is_esc(&state->evt)) {
            if (input_read_event(&state->evt) < 0) return EXIT_FAILURE;
            state->evt.modifiers |= MOD_META;
        }
    }

    /* Execute the command using event-based dispatch */
    execute_event(&state->evt, state->f, state->n);
    goto loop;
}


/*
 * Initialize all of the buffers and windows. The buffer name is passed down
 * as an argument, because the main routine may have been told to read in a
 * file by default, and we want the buffer name to be right.
 */
void edinit(const char *bname)
{
    struct buffer *bp;
    struct window *wp;

    bp = bfind(bname, true, 0);	/* First buffer         */
    blistp = bfind("*List*", true, BFINVS);	/* Buffer list buffer   */
    wp = (struct window *)safe_alloc(sizeof(struct window), "first window", __FILE__, __LINE__);	/* First window         */
    if (bp == nullptr || wp == nullptr || blistp == nullptr) {
        REPORT_ERROR(ERR_MEMORY, "FAILED TO INITIALIZE CORE EDITOR STRUCTURES");
        exit(1);
    }
    curbp = bp;		/* Make this current    */
    wheadp = wp;
    curwp = wp;
    wp->w_wndp = nullptr;	/* Initialize window    */
    wp->w_bufp = bp;
    bp->b_nwnd = 1;		/* Displayed.           */
    wp->w_linep = bp->b_linep;
    wp->w_dotp = bp->b_linep;
    wp->w_doto = 0;
    wp->w_markp = nullptr;
    wp->w_marko = 0;
    wp->w_toprow = 0;
    wp->w_ntrows = term.t_nrow - 1;	/* "-1" for mode line.  */
    wp->w_wrap_col = 0;		/* Soft wrap disabled by default */
    wp->w_force = 0;
    wp->w_flag = WFMODE | WFHARD;	/* Full.                */
}

/*
 * execute_event - Modern command execution with event-based binding lookup
 *
 * This is the primary dispatch path for keys with known events.
 * Uses getbind_event() directly, avoiding legacy key code conversion.
 */
int execute_event(input_key_event_t *evt, int f, int n)
{
    int status;
    fn_t execfunc;
    int c = (evt->type == KEY_CHAR) ? (int)evt->code : -1;

    /* 2025 Terminal Input Intercept */
    extern bool terminal_handle_key_event(input_key_event_t *evt);
    if (curbp && (curbp->b_mode & MDTBUFFER)) {
        if (terminal_handle_key_event(evt)) {
            return true;
        }
    }

    /* Mouse event handling - dispatch to extension hooks */
    if (evt->type == KEY_MOUSE) {
        extension_emit_mouse(evt);
        return true;  /* Always consume mouse events */
    }

    /* Event-based binding lookup - no legacy conversion needed */
    execfunc = getbind_event(evt);

    if (execfunc != nullptr) {
        atomic_store_explicit((_Atomic int*)&thisflag, 0, memory_order_relaxed);
        status = (*execfunc)(f, n);
        atomic_store_explicit((_Atomic int*)&lastflag,
                  atomic_load_explicit((_Atomic int*)&thisflag, memory_order_acquire),
                  memory_order_release);
        return status;
    }

    /* Word wrap check - only for space character */
    if (c == ' ' && (curwp->w_bufp->b_mode & MDWRAP) && fillcol > 0 &&
      n >= 0 && getccol(false) > fillcol &&
      (curwp->w_bufp->b_mode & MDVIEW) == false)
        wrapword(false, 1);

    /* Self-insert for printable characters */
    if ((c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0x10FFFF)) {
        if (n <= 0) {
            lastflag = 0;
            return n < 0 ? false : true;
        }
        thisflag = 0;

        /* Overwrite mode handling */
        int in_replace_mode = (curwp->w_bufp->b_mode & MDOVER) ||
          (vim_mode_active && atomic_load(&g_vim_state.current_mode) == MODE_REPLACE);
        if (in_replace_mode &&
          curwp->w_doto < llength(curwp->w_dotp) &&
          (lgetc(curwp->w_dotp, curwp->w_doto) != '\t' ||
          (curwp->w_doto) % 8 == 7))
            ldelchar(1, false);

        /* Smart typography transform via extension hook */
        int transformed_char = c;
        int transform_result = extension_emit_char_insert(c, &transformed_char);
        if (transform_result != 0) {
            /* Transform requested - need to insert as UTF-8 */
            if (transform_result == -1) {
                /* Em-dash case: delete previous char first */
                move_char_backward(false, 1);
                ldelchar(1, false);
            }
            /* Encode Unicode codepoint to UTF-8 and insert */
            char utf8_buf[8];
            unsigned utf8_len = unicode_to_utf8((unsigned)transformed_char, utf8_buf);
            utf8_buf[utf8_len] = '\0';
            for (int i = 0; i < n; i++) {
                status = linstr(utf8_buf);
                if (!status) break;
            }
            if ((c == '}' || c == ')') && (curbp->b_mode & MDCMOD) != 0)
                fence_match(c);
            lastflag = thisflag;
            return status;
        }

        /* No transform - insert original character.
        * For ASCII (c <= 0x7F), linsert handles it directly as a single byte.
        * For multi-byte codepoints (compose-key input, direct Unicode entry),
        * we must UTF-8-encode: linsert(n, c) would truncate c to (char)c and
        * write a single garbage byte. */
        if ((unsigned)c <= 0x7F) {
            status = linsert(n, c);
        } else {
            char utf8_buf[8];
            unsigned utf8_len = unicode_to_utf8((unsigned)c, utf8_buf);
            utf8_buf[utf8_len] = '\0';
            status = true;
            for (int i = 0; i < n; i++) {
                status = linstr(utf8_buf);
                if (!status) break;
            }
        }
        if ((c == '}' || c == ')') && (curbp->b_mode & MDCMOD) != 0)
            fence_match(c);
        lastflag = thisflag;
        return status;
    }

    lastflag = 0;
    return false;
}

/*
 * Fancy quit command, as implemented by Norm. If the any buffer has
 * changed do a write on that buffer and exit emacs, otherwise simply exit.
 */
int quickexit(int f, int n)
{
    struct buffer *bp;	/* scanning pointer to buffers */
    struct buffer *oldcb;	/* original current buffer */
    int status;

    oldcb = curbp;		/* save in case we fail */

    bp = bheadp;
    while (bp != nullptr) {
        if ((bp->b_flag & BFCHG) != 0	/* Changed.             */
          && (bp->b_flag & BFTRUNC) == 0	/* Not truncated P.K.   */
          && (bp->b_flag & BFINVS) == 0) {	/* Real.                */
            curbp = bp;	/* make that buffer cur */
            mlwrite("[SAVING %s]", bp->b_fname);
            if ((status = filesave(f, n)) != true) {
                curbp = oldcb;	/* restore curbp */
                return status;
            }
        }
        bp = bp->b_bufp;	/* on to the next buffer */
    }
    quit(f, n);		/* conditionally quit   */
    return true;
}

// Check and handle pending emergency exit - called from main loop
// Uses modern signal_handler.c infrastructure (sigaction with SA_RESTART)
void check_emergency_exit(void)
{
    // signal_should_exit() checks sig_hup_pending, sig_term_pending, sig_pipe_pending
    // These are set by signal_handler.c which uses proper sigaction() handlers
    extern bool signal_should_exit(void);
    extern void signal_clear_exit(void);

    if (signal_should_exit()) {
        signal_clear_exit();
        quickexit(false, 0);
        quit(true, 0);
    }
}

/*
 * Quit command. If an argument, always quit. Otherwise confirm if a buffer
 * has been changed and not written out. Normally bound to "C-X C-C".
 */
int quit(int f, int n)
{
    int s;

    if (f != false		/* Argument forces it.  */
      || anycb() == false	/* All buffers clean.   */
      /* User says it's OK.   */
      || (s =
        mlyesno("Modified buffers exist. Leave anyway")) == true) {
/* Session saving removed - keeping exit clean and simple */
        LOG_INFO("uEmacs: Exiting via quit command");

        /* Cleanup extension system */
        extern void extension_cleanup(void);
        extern void extension_api_cleanup(void);
        extension_cleanup();
        extension_api_cleanup();

        logger_close();
        vttidy();
        if (f)
            exit(n);
        else
            exit(GOOD);
    }
    mlwrite("");
    return s;
}

/*
 * Begin a keyboard macro.
 * Error if not at the top level in keyboard processing. Set up variables and
 * return.
 */
int ctlxlp(int f, int n)
{
    if (atomic_load(&kbdmode) != MACRO_STOP) {
        mlwrite("MACRO ALREADY ACTIVE");
        return false;
    }
    mlwrite("[START MACRO]");
    g_macro.count = 0;
    g_macro.playback_pos = 0;
    atomic_store(&kbdmode, MACRO_RECORD);
    return true;
}

/*
 * End keyboard macro. Check for the same limit conditions as the above
 * routine. Set up the variables and return to the caller.
 */
int ctlxrp(int f, int n)
{
    int mode = atomic_load(&kbdmode);
    if (mode == MACRO_STOP) {
        mlwrite("MACRO NOT ACTIVE");
        return false;
    }
    if (mode == MACRO_RECORD) {
        mlwrite("[END MACRO]");
        atomic_store(&kbdmode, MACRO_STOP);
    }
    return true;
}

/*
 * Execute a macro.
 * The command argument is the number of times to loop. Quit as soon as a
 * command gets an error. Return true if all ok, else false.
 */
int ctlxe(int f, int n)
{
    if (atomic_load(&kbdmode) != MACRO_STOP) {
        mlwrite("MACRO ALREADY ACTIVE");
        return false;
    }
    if (n <= 0)
        return true;
    if (g_macro.count == 0) {
        mlwrite("NO MACRO RECORDED");
        return false;
    }
    kbdrep = n;		/* remember how many times to execute */
    g_macro.playback_pos = 0;
    atomic_store(&kbdmode, MACRO_PLAY);
    return true;
}

/*
 * Abort.
 * Beep the beeper. Kill off any keyboard macro, etc., that is in progress.
 * Sometimes called as a routine, to do general aborting of stuff.
 */
int ctrlg(int f, int n)
{
    TTbeep();
    atomic_store(&kbdmode, MACRO_STOP);
    mlwrite("[ABORTED]");
    return ABORT;
}

/*
 * tell the user that this command is illegal while we are in
 * VIEW (read-only) mode
 */
int rdonly(void)
{
    TTbeep();
    mlwrite("[KEY ILLEGAL IN VIEW MODE]");
    return false;
}

int resterr(void)
{
    TTbeep();
    mlwrite("[THAT COMMAND IS RESTRICTED]");
    return false;
}

/* user function that does NOTHING */
int nullproc(int f, int n)
{
    return true;
}

/*
 * Recursive prefix key dispatcher. Handles multi-level prefix sequences
 * like C-x r s, looping until a terminal command is found or an error
 * occurs. is_meta applies the M-x == M-X uppercase convention.
 */
int prefix_dispatch(struct keymap *initial_map, bool is_meta, int f, int n)
{
    struct keymap *current_map = initial_map;
    int depth = 0;
    const int max_depth = 10;  /* Prevent infinite loops */

    while (depth < max_depth) {
        /* Get the next key */
        input_key_event_t evt;
        if (input_read_event(&evt) < 0) return false;

        /* Build lookup key */
        uint32_t code = evt.code;
        uint16_t mods = 0;

        /* Case normalization:
        * - For meta prefix: M-x == M-X (emacs convention)
        * - For C-x prefix: only normalize when Ctrl is held (kitty protocol)
        */
        if (is_meta) {
            if (code >= 'a' && code <= 'z') {
                code -= ('a' - 'A');
            }
        } else {
            if ((evt.modifiers & MOD_CTRL) && code >= 'a' && code <= 'z') {
                code -= ('a' - 'A');
            }
        }

        if (evt.modifiers & MOD_CTRL) mods |= MOD_CTRL;

        keymap_key_t lookup_key = keymap_key_make(code, mods);

        /* Look it up in current keymap */
        struct keymap_entry *entry = keymap_lookup(current_map, lookup_key);
        if (!entry) {
            mlwrite("[KEY NOT BOUND]");
            return false;
        }

        if (entry->is_prefix) {
            /* Another prefix - descend into sub-map */
            struct keymap *submap = entry->binding.map;
            if (!submap) {
                mlwrite("[INVALID PREFIX MAP]");
                return false;
            }
            current_map = submap;
            depth++;
            continue;
        }

        /* Terminal command - execute it */
        fn_t execfunc = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
        if (execfunc != nullptr) {
            return (*execfunc)(f, n);
        }

        mlwrite("[KEY NOT BOUND]");
        return false;
    }

    mlwrite("[PREFIX TOO DEEP]");
    return false;
}

/* Meta prefix function - supports recursive prefixes */
int metafn(int f, int n)
{
    struct keymap *mkm = atomic_load_explicit(&meta_keymap, memory_order_acquire);
    if (!mkm) {
        mlwrite("[NO META KEYMAP]");
        return false;
    }
    return prefix_dispatch(mkm, true, f, n);
}

/* Control-X prefix function - supports recursive prefixes */
int cex(int f, int n)
{
    struct keymap *ckm = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
    if (!ckm) {
        mlwrite("[NO C-X KEYMAP]");
        return false;
    }
    return prefix_dispatch(ckm, false, f, n);
}

/* External entry point for generic prefix dispatcher in bind.c */
int prefix_dispatch_external(struct keymap *initial_map, bool is_meta, int f, int n)
{
    return prefix_dispatch(initial_map, is_meta, f, n);
}

/* dummy function for binding to universal-argument */
int unarg(int f, int n)
{
    return true;
}

/*****		Compiler specific Library functions	****/

/*	On some primitave operation systems, and when emacs is used as
    a subprogram to a larger project, emacs needs to de-alloc its
    own used memory
*/

/* Remove conflicting stubs - will be resolved elsewhere */

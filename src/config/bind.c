/*	bind.c
 *
 *	This file is for functions having to do with key bindings,
 *	descriptions, help commands and startup file.
 *
 *	Written 11-feb-86 by Daniel Lawrence
 *	Modified by Petri Kutvonen
 *  Refactored for modern C23/Linux by Mod (2025)
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "string_utils.h"
#include "epath.h"
#include "line.h"
#include "util.h"
#include "error.h"
#include "file_utils.h"
#include "keymap.h"
#include "util/logger.h"
#include "terminal/input_state.h"

// (modern_keymaps_initialized removed - keymaps now init in main.c)

// Pending prefix keymap for generic_prefix_dispatch()
// This enables user-defined prefix keys without hardcoding dispatchers
static _Atomic(struct keymap *) g_pending_prefix = nullptr;

extern void vim_init_keymaps(void);
extern int prefix_dispatch_external(struct keymap *initial_map, bool is_meta, int f, int n);

// Forward declarations
char *getfname(fn_t func);
void cmdstr_key(keymap_key_t key, char *seq);

int help(int f, int n)
{				/* give me some help!!!!
				   bring up a fake buffer and read the help file
				   into it with view mode                 */
	struct window *wp;	/* scaning pointer to windows */
	struct buffer *bp;	/* buffer pointer to help */
	const char *fname = nullptr;	/* ptr to file returned by flook() */

	/* first check if we are already here */
	bp = bfind("emacs.hlp", false, BFINVS);

	if (bp == nullptr) {
		fname = flook(pathname[1], false);
		if (fname == nullptr) {
			REPORT_ERROR(ERR_FILE_NOT_FOUND, "HELP FILE IS NOT ONLINE");
			return false;
		}
	}

	/* split the current window to make room for the help stuff */
	if (window_split(false, 1) == false)
		return false;

	if (bp == nullptr) {
		/* and read the stuff in */
		if (getfile(fname, false) == false)
			return false;
	} else if (swbuffer(bp) != true)
		return false;

	/* make this window in VIEW mode, update all mode lines */
	curwp->w_bufp->b_mode |= MDVIEW;
	curwp->w_bufp->b_flag |= BFINVS;
	wp = wheadp;
	while (wp != nullptr) {
		wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}
	return true;
}

int describe_key_binding(int f, int n)
{				/* describe the command for a certain key */
	int c;		/* key to describe */
	char *ptr;	/* string pointer to scan output strings */
	char outseq[NSTRING];	/* output buffer for command sequence */

	/* prompt the user to type us a key to describe */
	mlwrite(": DESCRIBE-KEY ");

	/* get the command sequence to describe
	   change it to something we can print as well */
	keymap_key_t key = read_key(false);
	cmdstr_key(key, &outseq[0]);

	/* and dump it out */
	ostring(outseq);
	ostring(" ");

	/* find the right ->function */
	input_key_event_t evt = {
		.code = key.code,
		.modifiers = key.modifiers,
		.type = KEY_CHAR
	};
	if ((ptr = getfname(getbind_event(&evt))) == nullptr)
		ptr = "Not Bound";

	/* output the command sequence */
	ostring(ptr);
	return true;
}

/*
 * bindtokey:
 *	add a new key to the key binding table
 *
 * int f, n;		command arguments [IGNORED]
 */
int bindtokey(int f, int n)
{
	fn_t kfunc;	     /* ptr to the requested function to bind to */
	char outseq[80];     /* output buffer for keystroke sequence */

	/* prompt the user to type in a key to bind */
	mlwrite(": BIND-TO-KEY ");

	/* get the function name to bind it to */
	kfunc = getname();
	if (kfunc == nullptr) {
		REPORT_ERROR(ERR_COMMAND_UNKNOWN, "NO SUCH FUNCTION");
		return false;
	}
	ostring(" ");

	/* get the command sequence to bind */
	keymap_key_t key = read_key((kfunc == metafn) || (kfunc == cex) ||
		    (kfunc == unarg) || (kfunc == ctrlg));

	/* change it to something we can print as well */
	cmdstr_key(key, &outseq[0]);

	/* and dump it out */
	ostring(outseq);

	/* Determine target keymap based on key modifiers */
	struct keymap *dst = nullptr;
	if (key.modifiers & MOD_META) {
		dst = atomic_load_explicit(&meta_keymap, memory_order_acquire);
		key.modifiers &= (uint16_t)~MOD_META;  /* Clear meta since using meta map */
	} else {
		dst = atomic_load_explicit(&global_keymap, memory_order_acquire);
	}

	if (dst) {
		if (!keymap_bind(dst, key, kfunc)) {
			mlwrite("FAILED TO BIND KEY IN MODERN KEYMAP");
			return false;
		}
	} else {
		mlwrite("FAILED TO RESOLVE KEYMAP");
		return false;
	}

	return true;
}

/*
 * unbindkey:
 *	delete a key from the key binding table
 *
 * int f, n;		command arguments [IGNORED]
 */
int unbindkey(int f, int n)
{
	char outseq[80];	/* output buffer for keystroke sequence */

	/* prompt the user to type in a key to unbind */
	mlwrite(": UNBIND-KEY ");

	/* get the command sequence to unbind */
	keymap_key_t key = read_key(false);

	/* change it to something we can print as well */
	cmdstr_key(key, &outseq[0]);

	/* and dump it out */
	ostring(outseq);

	/* Determine target keymap based on key modifiers */
	struct keymap *dst = nullptr;
	if (key.modifiers & MOD_META) {
		dst = atomic_load_explicit(&meta_keymap, memory_order_acquire);
		key.modifiers &= (uint16_t)~MOD_META;
	} else {
		dst = atomic_load_explicit(&global_keymap, memory_order_acquire);
	}

	if (dst) {
		if (!keymap_unbind(dst, key)) {
			REPORT_ERROR(ERR_COMMAND_UNKNOWN, "KEY NOT BOUND");
			return false;
		}
	} else {
		REPORT_ERROR(ERR_COMMAND_UNKNOWN, "KEY NOT BOUND");
		return false;
	}
	return true;
}


/*
 * localsetkey:
 *	Bind a key to a function in the current buffer's local keymap.
 *	Creates the buffer-local keymap if it doesn't exist.
 */
int localsetkey(int f, int n)
{
	fn_t kfunc;
	char outseq[80];

	/* Must have a current buffer */
	if (!curbp) {
		mlwrite("[NO CURRENT BUFFER]");
		return false;
	}

	/* Prompt the user */
	mlwrite(": LOCAL-SET-KEY ");

	/* Get the function name to bind */
	kfunc = getname();
	if (kfunc == nullptr) {
		REPORT_ERROR(ERR_COMMAND_UNKNOWN, "NO SUCH FUNCTION");
		return false;
	}
	ostring(" ");

	/* Get the key sequence */
	keymap_key_t key = read_key((kfunc == metafn) || (kfunc == cex) ||
		    (kfunc == unarg) || (kfunc == ctrlg));
	cmdstr_key(key, &outseq[0]);
	ostring(outseq);

	/* Create buffer-local keymap if needed */
	if (!curbp->b_local_keymap) {
		curbp->b_local_keymap = keymap_create("buffer-local");
		if (!curbp->b_local_keymap) {
			mlwrite("[FAILED TO CREATE LOCAL KEYMAP]");
			return false;
		}
	}

	/* Bind to buffer-local keymap */
	if (!keymap_bind(curbp->b_local_keymap, key, kfunc)) {
		mlwrite("[FAILED TO BIND LOCAL KEY]");
		return false;
	}

	mlwrite("[BOUND LOCALLY]");
	return true;
}

/*
 * localunsetkey:
 *	Remove a key binding from the current buffer's local keymap.
 */
int localunsetkey(int f, int n)
{
	char outseq[80];

	/* Must have a current buffer with local keymap */
	if (!curbp) {
		mlwrite("[NO CURRENT BUFFER]");
		return false;
	}
	if (!curbp->b_local_keymap) {
		mlwrite("[NO LOCAL BINDINGS]");
		return false;
	}

	/* Prompt the user */
	mlwrite(": LOCAL-UNSET-KEY ");

	/* Get the key sequence */
	keymap_key_t key = read_key(false);
	cmdstr_key(key, &outseq[0]);
	ostring(outseq);

	/* Unbind from buffer-local keymap */
	if (!keymap_unbind(curbp->b_local_keymap, key)) {
		mlwrite("[KEY NOT LOCALLY BOUND]");
		return false;
	}

	mlwrite("[UNBOUND LOCALLY]");
	return true;
}

/* describe bindings
 * bring up a fake buffer and list the key bindings
 * into it with view mode
 */
int describe_all_bindings(int f, int n)
{
	buildlist(true, "");
	return true;
}

int apropos_command(int f, int n)
{				/* Apropos (List functions that match a substring) */
	char mstring[NSTRING];	/* string to match cmd names to */
	int status;		/* status return */

	status = minibuf_read("APROPOS STRING: ", mstring, NSTRING - 1);
	if (status != true)
		return status;

	return buildlist(false, mstring);
}

/*
 * build a binding list (limited or full)
 *
 * int type;		true = full list,   false = partial list
 * char *mstring;	match string if a partial list
 */
/* helper: append binding lines for a given keymap and prefix modifier */
static void append_bindings_for_map(struct keymap *map, const char *prefix_str,
                                    struct name_bind *nptr, char *outseq, int *cpos)
{
    if (!map || !nptr || !outseq || !cpos) return;
    for (int i = 0; i < KEYMAP_HASH_SIZE; i++) {
        struct keymap_entry *entry = map->table[i];
        while (entry) {
            if (!entry->is_prefix) {
                fn_t fn = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
                if (fn == nptr->n_func) {
                    /* pad spaces */
                    while (*cpos < 28) outseq[(*cpos)++] = ' ';
                    /* Add prefix string (e.g., "^X " or "M-") if provided */
                    if (prefix_str && prefix_str[0]) {
                        size_t plen = strlen(prefix_str);
                        memcpy(&outseq[*cpos], prefix_str, plen);
                        *cpos += (int)plen;
                    }
                    /* Add the key binding */
                    cmdstr_key(entry->key, &outseq[*cpos]);
                    SAFE_STRCAT(outseq, "\n");
                    if (linstr(outseq) != true) return;
                    *cpos = 0; /* reset line */
                }
            }
            entry = entry->next;
        }
    }
}

int buildlist(int type, const char *mstring)
{
	struct window *wp;         /* scanning pointer to windows */
	struct name_bind *nptr;          /* pointer into the name binding table */
	struct buffer *bp;    /* buffer to put binding list into */
	int cpos;             /* current position to use in outseq */
	char outseq[80];      /* output buffer for keystroke sequence */

	/* split the current window to make room for the binding list */
	if (window_split(false, 1) == false)
		return false;

	/* and get a buffer for it */
	bp = bfind("*Binding list*", true, 0);
	if (bp == nullptr || bclear(bp) == false) {
		REPORT_ERROR(ERR_BUFFER_INVALID, "CANNOT DISPLAY BINDING LIST");
		return false;
	}

	/* let us know this is in progress */
	mlwrite("[BUILDING BINDING LIST]");

	/* disconect the current buffer */
	if (--curbp->b_nwnd == 0) {	/* Last use.            */
		curbp->b_dotp = curwp->w_dotp;
		curbp->b_doto = curwp->w_doto;
		curbp->b_markp = curwp->w_markp;
		curbp->b_marko = curwp->w_marko;
	}

	/* connect the current window to this buffer */
	curbp = bp;		/* make this buffer current in current window */
	bp->b_mode = 0;		/* no modes active in binding list */
	bp->b_nwnd++;		/* mark us as more in use */
	wp = curwp;
	wp->w_bufp = bp;
	wp->w_linep = bp->b_linep;
	wp->w_flag = WFHARD | WFFORCE;
	wp->w_dotp = bp->b_dotp;
	wp->w_doto = bp->b_doto;
	wp->w_markp = nullptr;
	wp->w_marko = 0;

	/* build the contents of this window, inserting it line by line */
	struct keymap *gkm_list = atomic_load_explicit(&global_keymap, memory_order_acquire);
	struct keymap *ckm_list = atomic_load_explicit(&ctlx_keymap, memory_order_acquire);
	struct keymap *mkm_list = atomic_load_explicit(&meta_keymap, memory_order_acquire);

	nptr = &names[0];
	while (nptr->n_func != nullptr) {

		/* add in the command name */
        SAFE_STRCPY(outseq, nptr->n_name);
		cpos = (int)strlen(outseq);

		/* if we are executing an apropos command..... */
		if (type == false &&
		    /* and current string doesn't include the search string */
		    string_contains(outseq, mstring) == false)
			goto fail;

		/* append all modern keymap bindings for this function */
		append_bindings_for_map(gkm_list, "", nptr, outseq, &cpos);
		append_bindings_for_map(ckm_list, "^X ", nptr, outseq, &cpos);
		append_bindings_for_map(mkm_list, "M-", nptr, outseq, &cpos);

		/* if no key was bound, we need to dump it anyway */
		if (cpos > 0) {
			outseq[cpos++] = '\n';
			outseq[cpos] = 0;
			if (linstr(outseq) != true)
				return false;
		}

	      fail:		/* and on to the next name */
		++nptr;
	}

	curwp->w_bufp->b_mode |= MDVIEW;	/* put this buffer view mode */
	curbp->b_flag &= ~BFCHG;	/* don't flag this as a change */
	wp->w_dotp = lforw(bp->b_linep);	/* back to the beginning */
	wp->w_doto = 0;
	wp = wheadp;		/* and update ALL mode lines */
	while (wp != nullptr) {
		wp->w_flag |= WFMODE;
		wp = wp->w_wndp;
	}
	mlwrite("");		/* clear the mode line */
	return true;
}


/*
 * does source include sub?
 *
 * char *source;	string to search in
 * char *sub;		substring to look for
 */
int string_contains(const char *source, const char *sub)
{
	const char *sp;		/* ptr into source */
	const char *nxtsp;		/* next ptr into source */
	const char *tp;		/* ptr into substring */

	/* for each character in the source string */
	sp = source;
	while (*sp) {
		tp = sub;
		nxtsp = sp;

		/* is the substring here? */
		while (*tp) {
			if (*nxtsp++ != *tp)
				break;
			else
				tp++;
		}

		/* yes, return a success */
		if (*tp == 0)
			return true;

		/* no, onward */
		sp++;
	}
	return false;
}

/*
 * read_key: Read a key from keyboard or macro and return modern keymap_key_t
 *
 * int mflag;		going for a meta sequence? (ignored, kept for compat)
 */
keymap_key_t read_key([[maybe_unused]] int mflag)
{
	char tok[NSTRING];

	/* check to see if we are executing a command line */
	if (clexec) {
		macarg(tok);
		return stock_key(tok);
	}

	/* or the normal way - read event and convert to keymap_key_t */
	input_key_event_t evt;
	if (input_read_event(&evt) < 0) return keymap_key_make(0, 0);

	uint16_t mods = 0;
	if (evt.modifiers & MOD_CTRL) mods |= MOD_CTRL;
	if (evt.modifiers & (MOD_META | MOD_ALT)) mods |= MOD_META;
	return keymap_key_make(evt.code, mods);
}

/*
 * execute the startup file
 *
 * char *sfname;	name of startup file (null if default)
 */
int startup(const char *sfname)
{
	const char *fname;		/* resulting file name to execute */

	/* look up the startup file */
	if (*sfname != 0)
		fname = flook(sfname, true);
	else
		fname = flook(pathname[0], true);

	/* if it isn't around, don't sweat it */
	if (fname == nullptr)
		return true;

	/* otherwise, execute the sucker */
	return dofile(fname);
}

/*
 * Look up the existance of a file along the normal or PATH
 * environment variable. Look first in the HOME directory if
 * asked and possible
 *
 * char *fname;		base file name to search for
 * int hflag;		Look in the HOME environment variable first?
 */
const char *flook(const char *fname, int hflag)
{
	char *home;	/* path to home directory */
	char *path;	/* environmental PATH variable */
	char *sp;	/* pointer into path spec */
	size_t i;		/* index */
	static _Thread_local char fspec[NSTRING];	/* Thread-safe path spec */

	if (hflag) {
		home = getenv("HOME");
		if (home != nullptr) {
			/* build home dir file spec */
            SAFE_STRCPY(fspec, home);
            SAFE_STRCAT(fspec, "/");
            SAFE_STRCAT(fspec, fname);

			/* and try it out */
			if (ffropen(fspec) == FIOSUC) {
				(void)ffclose();
				return fspec;
			}
		}
	}

	/* always try the current directory first */
	if (ffropen(fname) == FIOSUC) {
		(void)ffclose();
		return fname;
	}
	/* get the PATH variable */
	path = getenv("PATH");
	if (path != nullptr)
		while (*path) {

			/* build next possible file spec */
			sp = fspec;
			while (*path && (*path != PATHCHR))
				*sp++ = *path++;

			/* add a terminating dir separator if we need it */
			if (sp != fspec)
				*sp++ = '/';
			*sp = 0;
            SAFE_STRCAT(fspec, fname);

			/* and try it out */
			if (ffropen(fspec) == FIOSUC) {
				(void)ffclose();
				return fspec;
			}

			if (*path == PATHCHR)
				++path;
		}

	/* look it up via the old table method */
	for (i = 2; i < ARRAY_SIZE(pathname); i++) {
        SAFE_STRCPY(fspec, pathname[i]);
        SAFE_STRCAT(fspec, fname);

		/* and try it out */
		if (ffropen(fspec) == FIOSUC) {
			(void)ffclose();
			return fspec;
		}
	}

	return nullptr;		/* no such luck */
}

/*
 * cmdstr_key: Convert a keymap_key_t to a readable string
 */
void cmdstr_key(keymap_key_t key, char *seq)
{
	char *ptr = seq;

	if (key.modifiers & MOD_META) {
		*ptr++ = 'M';
		*ptr++ = '-';
	}
	if (key.modifiers & MOD_CTRL) {
		*ptr++ = '^';
	}
	*ptr++ = (char)(key.code & 0xFF);
	*ptr = '\0';
}

/* Forward declaration for vim mode switching */
extern int vim_enter_normal_mode_external(int f, int n);

/*
 * generic_prefix_dispatch: Dispatcher for user-defined prefix keys.
 *
 * When getbind_event() encounters a prefix binding that isn't ctlx_keymap
 * or meta_keymap, it stores the target keymap in g_pending_prefix and
 * returns this function. This allows arbitrary keys to become prefixes.
 */
int generic_prefix_dispatch(int f, int n)
{
	struct keymap *km = atomic_exchange(&g_pending_prefix, nullptr);
	if (!km) {
		mlwrite("[NO PENDING PREFIX]");
		return false;
	}
	return prefix_dispatch_external(km, false, f, n);
}

/*
 * getbind_event:
 *	Event-based key binding lookup - no legacy format conversion.
 *	This is the modern replacement for getbind() that works directly
 *	with input_key_event_t to avoid the legacy integer key format.
 */
fn_t getbind_event(input_key_event_t *evt)
{
    if (!evt || evt->type == KEY_NONE) return nullptr;

    /* Filter out non-bindable event types.
     * These are terminal state events, not user key presses. */
    switch (evt->type) {
    case KEY_FOCUS_IN:
    case KEY_FOCUS_OUT:
    case KEY_PASTE_START:
    case KEY_PASTE_END:
    case KEY_CSI_UNKNOWN:
    case KEY_MOUSE:
    case KEY_OSC_RECEIVED:
        return nullptr;
    default:
        break;
    }

    // Build key code from event
    uint32_t code = evt->code;
    uint8_t mods = (uint8_t)evt->modifiers;

    LOG_DEBUGF("BIND: input type=%d code=0x%X ('%c') mods=0x%X",
               evt->type, code,
               (code >= 0x20 && code < 0x7F) ? code : '?',
               mods);

    // KEY_SPECIAL events already have SPECIAL_* codes - use directly
    // (keymaps store SPECIAL_UP/DOWN/etc from stock_key())
    if (evt->type == KEY_SPECIAL || code >= SPECIAL_KEY_BASE) {
        LOG_DEBUGF("BIND: special key code=0x%X", code);
    }

    // Convert C0 control characters (0x00-0x1F) to uppercase letters for lookup
    // e.g., Ctrl-N comes as code=0x0E, MOD_CTRL - keytab wants CONTROL|'N'
    if ((mods & MOD_CTRL) && code < 0x20) {
        uint32_t old_code = code;
        code = code + '@';  // 0x0E -> 0x4E ('N')
        LOG_DEBUGF("BIND: C0 ctrl->letter 0x%X -> 0x%X ('%c')", old_code, code, code);
    }

    // Convert lowercase letters to uppercase when Ctrl is pressed
    // Modern terminals (kitty protocol) send code='f' mods=MOD_CTRL instead of C0 control
    if ((mods & MOD_CTRL) && code >= 'a' && code <= 'z') {
        uint32_t old_code = code;
        code = code - ('a' - 'A');  // 'f' -> 'F'
        LOG_DEBUGF("BIND: lowercase ctrl->upper 0x%X -> 0x%X ('%c')", old_code, code, code);
    }

    // Handle ESC in Vim Insert/Replace modes -> return to Normal
    if (vim_mode_active && code == 0x1B && (mods & MOD_CTRL) == 0) {
        enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
        if (mode == MODE_INSERT || mode == MODE_REPLACE) {
            LOG_DEBUGF("BIND: vim %s ESC -> normal mode",
                       mode == MODE_INSERT ? "insert" : "replace");
            return vim_enter_normal_mode_external;
        }
    }

    // Select keymap based on modifiers
    struct keymap *map = nullptr;
    const char *map_name __attribute__((unused)) = "global";

    if (vim_mode_active) {
        enum editor_mode mode = atomic_load(&g_vim_state.current_mode);
        if (mode == MODE_NORMAL) {
            map = atomic_load_explicit(&vim_normal_keymap, memory_order_acquire);
            map_name = "vim-normal";
        } else if (mode == MODE_VISUAL || mode == MODE_VISUAL_LINE || mode == MODE_VISUAL_BLOCK) {
            map = atomic_load_explicit(&vim_visual_keymap, memory_order_acquire);
            map_name = "vim-visual";
        }
    }

    if (!map) {
        map = atomic_load_explicit(&global_keymap, memory_order_acquire);
        map_name = "global";
    }

    // Build modern keymap_key_t for lookup
    uint16_t key_mods = 0;
    if (mods & MOD_CTRL) key_mods |= MOD_CTRL;

    // Check for prefix key sequences (Ctrl-X, Meta/Alt)
    // MOD_ALT (0x02) = terminal Alt key, MOD_META (0x20) = ESC prefix
    if (mods & (MOD_META | MOD_ALT)) {
        map = atomic_load_explicit(&meta_keymap, memory_order_acquire);
        map_name = "meta";
        // Key in meta keymap doesn't have META flag, just the base key
        key_mods &= (uint16_t)~MOD_META;
    }

    keymap_key_t lookup_key = keymap_key_make(code, key_mods);
    LOG_DEBUGF("BIND: lookup code=0x%X mods=0x%X in map=%s", code, key_mods, map_name);

    // Check buffer-local keymap first (highest priority)
    if (curbp && curbp->b_local_keymap) {
        struct keymap_entry *entry = keymap_lookup(curbp->b_local_keymap, lookup_key);
        if (entry) {
            LOG_DEBUGF("BIND: FOUND in buffer-local keymap");
            if (!entry->is_prefix) {
                fn_t result = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
                return result;
            }
            // Buffer-local prefix
            struct keymap *ckm = atomic_load(&ctlx_keymap);
            struct keymap *mkm = atomic_load(&meta_keymap);
            struct keymap *bound_map = atomic_load(&entry->binding.map);
            if (bound_map == ckm) return cex;
            if (bound_map == mkm) return metafn;
            atomic_store(&g_pending_prefix, bound_map);
            return generic_prefix_dispatch;
        }
    }

    if (map) {
        struct keymap_entry *entry = keymap_lookup(map, lookup_key);
        if (entry) {
            if (!entry->is_prefix) {
                fn_t result = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
                char *fname = getfname(result);
                LOG_DEBUGF("BIND: FOUND in %s -> %s", map_name, fname ? fname : "(unknown)");
                return result;
            }
            // For prefix entries, return the appropriate dispatcher
            struct keymap *ckm = atomic_load(&ctlx_keymap);
            struct keymap *mkm = atomic_load(&meta_keymap);
            struct keymap *bound_map = atomic_load(&entry->binding.map);
            if (bound_map == ckm) {
                LOG_DEBUGF("BIND: FOUND prefix CTLX in %s", map_name);
                return cex;
            }
            if (bound_map == mkm) {
                LOG_DEBUGF("BIND: FOUND prefix META in %s", map_name);
                return metafn;
            }
            // User-defined prefix - use generic dispatcher
            LOG_DEBUGF("BIND: FOUND user-defined prefix in %s", map_name);
            atomic_store(&g_pending_prefix, bound_map);
            return generic_prefix_dispatch;
        }

        // Fallback to global keymap if vim mode didn't have binding
        // This allows Ctrl+F, Ctrl+B, etc. to work in vim normal mode
        if (vim_mode_active && map != atomic_load(&global_keymap)) {
            struct keymap *gkm = atomic_load_explicit(&global_keymap, memory_order_acquire);
            if (gkm) {
                entry = keymap_lookup(gkm, lookup_key);
                if (entry) {
                    if (!entry->is_prefix) {
                        fn_t result = atomic_load_explicit(&entry->binding.cmd, memory_order_relaxed);
                        char *fname = getfname(result);
                        LOG_DEBUGF("BIND: FOUND (vim fallback to global) -> %s", fname ? fname : "(unknown)");
                        return result;
                    }
                    struct keymap *ckm2 = atomic_load(&ctlx_keymap);
                    struct keymap *mkm2 = atomic_load(&meta_keymap);
                    struct keymap *bound_map2 = atomic_load(&entry->binding.map);
                    if (bound_map2 == ckm2) {
                        LOG_DEBUG("BIND: FOUND prefix CTLX (vim fallback)");
                        return cex;
                    }
                    if (bound_map2 == mkm2) {
                        LOG_DEBUG("BIND: FOUND prefix META (vim fallback)");
                        return metafn;
                    }
                    // User-defined prefix in vim fallback
                    LOG_DEBUG("BIND: FOUND user-defined prefix (vim fallback)");
                    atomic_store(&g_pending_prefix, bound_map2);
                    return generic_prefix_dispatch;
                }
            }
        }
    }

    LOG_DEBUGF("BIND: NOT FOUND key=0x%08X", lookup_key);
    return nullptr;
}

/*
 * getfname:
 *	This function takes a ptr to function and gets the name
 *	associated with it.
 */
char *getfname(fn_t func)
{
	struct name_bind *nptr;	/* pointer into the name binding table */

	/* skim through the table, looking for a match */
	nptr = &names[0];
	while (nptr->n_func != nullptr) {
		if (nptr->n_func == func)
			return nptr->n_name;
		++nptr;
	}
	return nullptr;
}

/*
 * match fname to a function in the names table using binary search
 * and return any match or nullptr if none
 *
 * char *fname;		name to attempt to match
 * 
 * Performance: O(log n) - max 8 comparisons for 192 entries
 */
int (*fncmatch(const char *fname)) (int, int)
{
	if (!fname || !fname[0]) return nullptr;
	
	/* Binary search through sorted names table */
	/* Find table size by scanning for nullptr terminator */
	int left = 0;
	int right = 0;
	while (names[right].n_name != nullptr) {
		right++;
	}
	right--;  /* Point to last valid entry */
	
	while (left <= right) {
		int mid = left + (right - left) / 2;  /* Avoid overflow */
		int cmp = strcmp(fname, names[mid].n_name);
		
		if (cmp == 0) {
			/* Found exact match */
			return names[mid].n_func;
		} else if (cmp < 0) {
			/* Search left half */
			right = mid - 1;
		} else {
			/* Search right half */
			left = mid + 1;
		}
	}
	
	/* Not found in static table - check dynamic extension commands */
	extern fn_t extension_find_command(const char *name);
	fn_t ext_func = extension_find_command(fname);
	if (ext_func) {
		return ext_func;
	}

	/* Not found anywhere */
	return nullptr;
}

/*
 * stock_key:
 *	String key name TO keymap_key_t
 *	Modern version of stock() that returns the new key format.
 */
keymap_key_t stock_key(const char *keyname)
{
	uint32_t code = 0;
	uint16_t mods = 0;

	/* Handle special key names first */
	if (strcmp(keyname, "DELETE") == 0 || strcmp(keyname, "Del") == 0) {
		return keymap_key_make(SPECIAL_DELETE, 0);
	}
	if (strcmp(keyname, "INSERT") == 0 || strcmp(keyname, "Ins") == 0) {
		return keymap_key_make(SPECIAL_INSERT, 0);
	}
	if (strcmp(keyname, "HOME") == 0) {
		return keymap_key_make(SPECIAL_HOME, 0);
	}
	if (strcmp(keyname, "END") == 0) {
		return keymap_key_make(SPECIAL_END, 0);
	}
	if (strcmp(keyname, "PAGEUP") == 0 || strcmp(keyname, "PgUp") == 0) {
		return keymap_key_make(SPECIAL_PAGEUP, 0);
	}
	if (strcmp(keyname, "PAGEDOWN") == 0 || strcmp(keyname, "PgDn") == 0) {
		return keymap_key_make(SPECIAL_PAGEDOWN, 0);
	}
	if (strcmp(keyname, "UP") == 0) {
		return keymap_key_make(SPECIAL_UP, 0);
	}
	if (strcmp(keyname, "DOWN") == 0) {
		return keymap_key_make(SPECIAL_DOWN, 0);
	}
	if (strcmp(keyname, "RIGHT") == 0) {
		return keymap_key_make(SPECIAL_RIGHT, 0);
	}
	if (strcmp(keyname, "LEFT") == 0) {
		return keymap_key_make(SPECIAL_LEFT, 0);
	}
	if (strcmp(keyname, "BACKSPACE") == 0 || strcmp(keyname, "BS") == 0) {
		return keymap_key_make(0x7F, 0);  /* DEL/Backspace = 0x7F */
	}

	/* META prefix */
	if (*keyname == 'M' && *(keyname + 1) == '-') {
		mods |= MOD_META;
		keyname += 2;
	}

	/* ^X prefix for C-x keymap - handled by caller selecting target keymap */
	if (*keyname == '^' && *(keyname + 1) == 'X') {
		keyname += 2;
		/* If there's nothing after ^X, it's the prefix key itself */
		if (*keyname == '\0') {
			return keymap_key_make('X', MOD_CTRL);
		}
		/* Skip any space after ^X */
		while (*keyname == ' ') keyname++;
	}

	/* Control key prefix */
	if (*keyname == '^' && *(keyname + 1) != '\0') {
		/* Special case: ^[ is ESC (0x1B), not Ctrl+[ */
		if (*(keyname + 1) == '[') {
			return keymap_key_make(0x1B, 0);
		}
		mods |= MOD_CTRL;
		keyname++;
	}

	char cur_char = *keyname;

	/* Raw control character */
	if (cur_char < 32) {
		mods |= MOD_CTRL;
		cur_char += 'A';
	}

	/* Uppercase control and meta keys for consistency */
	if ((mods & (MOD_CTRL | MOD_META)) && cur_char >= 'a' && cur_char <= 'z') {
		cur_char -= 32;
	}

	code = (uint32_t)cur_char;
	return keymap_key_make(code, mods);
}

/*
 * string key name to binding name....
 *
 * char *skey;		name of key to get binding for
 */
char *transbind(const char *skey)
{
	char *bindname;

	/* Parse key string and convert to event for lookup */
	keymap_key_t key = stock_key(skey);
	input_key_event_t evt = {
		.code = key.code,
		.modifiers = key.modifiers,
		.type = (key.code >= SPECIAL_KEY_BASE) ? KEY_SPECIAL : KEY_CHAR
	};
	bindname = getfname(getbind_event(&evt));
	if (bindname == nullptr)
		bindname = "ERROR";

	return bindname;
}

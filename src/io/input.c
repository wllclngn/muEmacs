/*	input.c
 *
 *	Various input routines
 *
 *	written by Daniel Lawrence 5/9/86
 *	modified by Petri Kutvonen
 */

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <stdlib.h>
#include <stdatomic.h>

#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "file_utils.h"
#include "string_utils.h"
#include "keymap.h"
#include "util/logger.h"
#include "terminal/input_state.h"

#define	COMPLC	1

/* Unified input parser - VT500 state machine for all escape sequence handling */
static input_parser_t g_input_parser;
static bool g_input_parser_initialized = false;

/* Initialize input parser (called lazily on first use) */
static void ensure_input_parser_initialized(void) {
    if (!g_input_parser_initialized) {
        input_parser_init(&g_input_parser);
        g_input_parser_initialized = true;
    }
}

/*
 * minibuf_confirm: Ask a yes/no question in the minibuffer.
 *
 * Returns true for Y, false for N, ABORT for Ctrl-G or ESC (evil-mode).
 */
int minibuf_confirm(const char *prompt)
{
    char buf[NPAT];
    input_key_event_t evt;
    int attempts = 0;
    const int max_attempts = 3;

    /* Auto-confirm in non-interactive modes */
    if (!term.t_getchar) {
        LOG_DEBUGF("minibuf_confirm: no terminal, auto-confirming '%s'", prompt);
        return true;
    }
    if (!isatty(STDIN_FILENO)) {
        LOG_DEBUGF("minibuf_confirm: stdin not a tty, auto-confirming '%s'", prompt);
        return true;
    }

    for (;;) {
        safe_strcpy(buf, prompt, NPAT);
        safe_strcat(buf, " [Y/N]? ", NPAT);
        mlwrite(buf);
        TTflush();

        if (input_read_event(&evt) < 0) {
            LOG_DEBUGF("minibuf_confirm: input error, auto-confirming");
            return true;
        }

        /* Ctrl-G aborts */
        if (evt_is_ctrl(&evt, 'G'))
            return ABORT;

        /* ESC aborts in evil-mode */
        if (evt_is_esc(&evt) && vim_mode_active)
            return ABORT;

        int c = evt_char(&evt);
        if (c == 'y' || c == 'Y')
            return true;
        if (c == 'n' || c == 'N')
            return false;

        if (++attempts >= max_attempts) {
            LOG_WARNF("minibuf_confirm: max attempts reached, auto-confirming '%s'", prompt);
            return true;
        }
    }
}

/* Legacy wrapper */
int mlyesno(const char *prompt)
{
    return minibuf_confirm(prompt);
}


/*
 * minibuf_read_command: Read a command name with tab-completion.
 *
 * Space/Tab triggers completion attempt. Enter executes.
 * Ctrl-G aborts, ESC aborts in evil-mode.
 */
fn_t minibuf_read_command(void)
{
    int cpos = 0;
    input_key_event_t evt;
    char *sp;
    struct name_bind *ffp;
    struct name_bind *cffp;
    struct name_bind *lffp;
    char buf[NSTRING];

    /* If executing a macro, get arg from command line */
    if (clexec) {
        if (macarg(buf) != true)
            return nullptr;
        return fncmatch(&buf[0]);
    }

    /* Interactive: build command name from keyboard */
    while (true) {
        if (input_read_event(&evt) < 0)
            return nullptr;

        /* Enter: execute the command */
        if (evt_is_enter(&evt)) {
            buf[cpos] = '\0';
            return fncmatch(&buf[0]);
        }

        /* Ctrl-G aborts */
        if (evt_is_ctrl(&evt, 'G')) {
            ctrlg(false, 0);
            TTflush();
            return nullptr;
        }

        /* ESC aborts in evil-mode */
        if (evt_is_esc(&evt) && vim_mode_active) {
            ctrlg(false, 0);
            TTflush();
            return nullptr;
        }

        /* Backspace/Delete: erase character */
        if (evt_is_backspace(&evt)) {
            if (cpos != 0) {
                TTputc('\b');
                TTputc(' ');
                TTputc('\b');
                --ttcol;
                --cpos;
                TTflush();
            }
            continue;
        }

        /* Ctrl-U: kill line */
        if (evt_is_ctrl(&evt, 'U')) {
            while (cpos != 0) {
                TTputc('\b');
                TTputc(' ');
                TTputc('\b');
                --cpos;
                --ttcol;
            }
            TTflush();
            continue;
        }

        /* Space, Tab, or ESC (emacs mode): attempt completion */
        if (evt_is_char(&evt, ' ') || evt_is_tab(&evt) ||
          (evt_is_esc(&evt) && !vim_mode_active)) {
            buf[cpos] = '\0';
            ffp = &names[0];
            while (ffp->n_func != nullptr) {
                if (strncmp(buf, ffp->n_name, strlen(buf)) == 0) {
                    /* Possible match - is it unique? */
                    if ((ffp + 1)->n_func == nullptr ||
                      strncmp(buf, (ffp + 1)->n_name, strlen(buf)) != 0) {
                        /* Unique match - print and return */
                        sp = ffp->n_name + cpos;
                        while (*sp)
                            TTputc(*sp++);
                        TTflush();
                        return ffp->n_func;
                    } else {
                        /* Multiple matches - partial complete */
                        lffp = (ffp + 1);
                        while ((lffp + 1)->n_func != nullptr) {
                            if (strncmp(buf, (lffp + 1)->n_name, strlen(buf)) != 0)
                                break;
                            ++lffp;
                        }

                        /* Complete as much as possible */
                        while (true) {
                            buf[cpos] = ffp->n_name[cpos];
                            cffp = ffp + 1;
                            while (cffp <= lffp) {
                                if (cffp->n_name[cpos] != buf[cpos])
                                    goto onward;
                                ++cffp;
                            }
                            TTputc(buf[cpos++]);
                        }
                    }
                }
                ++ffp;
            }

            /* No match - beep */
            TTbeep();
           onward:;
            TTflush();
            continue;
        }

        /* Regular character input */
        int c = evt_char(&evt);
        if (c > ' ' && cpos < NSTRING - 1) {
            buf[cpos++] = (char)c;
            TTputc(c);
            ++ttcol;
            TTflush();
        }
    }
}

/* Read a command name from the minibuffer */
fn_t getname(void)
{
    return minibuf_read_command();
}

/* Test helper: reset input parser state */
void input_reset_parser_state(void)
{
    if (g_input_parser_initialized) {
        input_parser_reset(&g_input_parser);
    }
}

/*
 * macro_record_event: Record an input event for macro playback.
 * Called automatically by input_read_event() when in MACRO_RECORD mode.
 */
static void macro_record_event(const input_key_event_t *evt)
{
    if (atomic_load(&kbdmode) == MACRO_RECORD) {
        if (g_macro.count < NKBDM - 1) {
            g_macro.events[g_macro.count++] = *evt;
        } else {
            /* Macro buffer full */
            atomic_store(&kbdmode, MACRO_STOP);
            TTbeep();
        }
    }
}

/*
 * macro_get_event: Get next event from macro playback buffer.
 * Returns true if event was retrieved, false if playback complete.
 */
static bool macro_get_event(input_key_event_t *out)
{
    if (atomic_load(&kbdmode) != MACRO_PLAY) {
        return false;
    }

    if (g_macro.playback_pos < g_macro.count) {
        *out = g_macro.events[g_macro.playback_pos++];
        return true;
    }

    /* End of this repetition */
    if (--kbdrep > 0) {
        /* More repetitions - restart from beginning */
        g_macro.playback_pos = 0;
        *out = g_macro.events[g_macro.playback_pos++];
        return true;
    }

    /* All repetitions complete */
    atomic_store(&kbdmode, MACRO_STOP);
    update(false);
    return false;
}

/*
 * input_read_event:
 *    Get one keystroke using the unified state machine parser.
 *    Returns input_key_event_t directly - no legacy conversion.
 *    Handles all escape sequences, UTF-8, and bracketed paste.
 *    Integrates with keyboard macro system for recording/playback.
 */
int input_read_event(input_key_event_t *out)
{
    ensure_input_parser_initialized();

    /* Macro playback takes precedence - return pre-recorded events */
    if (macro_get_event(out)) {
        LOG_DEBUGF("INPUT: macro playback type=%d code=0x%X mods=0x%X",
                   out->type, out->code, out->modifiers);
        return 1;
    }

    for (;;) {
        /* First check if parser has pending bytes from pushback */
        int pending = input_parser_get_pending(&g_input_parser);
        if (pending >= 0) {
            LOG_DEBUGF("INPUT: pending byte=0x%02X", pending);
            if (input_parser_feed(&g_input_parser, (uint8_t)pending, out) > 0) {
                if (out->type == KEY_CHAR || out->type == KEY_SPECIAL) {
                    LOG_DEBUGF("INPUT: event type=%d code=0x%X ('%c') mods=0x%X",
                               out->type, out->code,
                               (out->code >= 0x20 && out->code < 0x7F) ? out->code : '?',
                               out->modifiers);
                    macro_record_event(out);
                    return 1;
                }
                if (out->type == KEY_CSI_UNKNOWN) {
                    /* Unknown CSI sequence - emit ESC so input isn't lost */
                    out->type = KEY_CHAR;
                    out->code = 0x1B;
                    LOG_DEBUG("INPUT: unknown CSI, emitting ESC");
                    macro_record_event(out);
                    return 1;
                }
                /* Focus events, paste markers are skipped */
            }
            continue;
        }

        /* Check if there's a pending ESC that should be emitted */
        if (input_parser_has_pending_esc(&g_input_parser)) {
            /* Check if more input is available */
            if (!input_pending()) {
                /* No more input - flush ESC as standalone key */
                if (input_parser_flush_esc(&g_input_parser, out) > 0) {
                    LOG_DEBUG("INPUT: flushed standalone ESC");
                    macro_record_event(out);
                    return 1;
                }
            }
            /* More input available - let it continue the sequence */
        }

        /* Get a byte from terminal */
        int byte = input_read_byte();
        if (byte < 0) {
            /* Error or EOF */
            LOG_DEBUGF("INPUT: EOF/error byte=%d", byte);
            return -1;
        }

        LOG_DEBUGF("INPUT: raw byte=0x%02X ('%c')", byte,
                   (byte >= 0x20 && byte < 0x7F) ? byte : '?');

        /* Feed to parser */
        if (input_parser_feed(&g_input_parser, (uint8_t)byte, out) > 0) {
            if (out->type == KEY_CHAR || out->type == KEY_SPECIAL) {
                LOG_DEBUGF("INPUT: event type=%d code=0x%X ('%c') mods=0x%X",
                           out->type, out->code,
                           (out->code >= 0x20 && out->code < 0x7F) ? out->code : '?',
                           out->modifiers);
                macro_record_event(out);
                return 1;
            }
            if (out->type == KEY_CSI_UNKNOWN) {
                /* Unknown CSI sequence - emit ESC so input isn't lost */
                out->type = KEY_CHAR;
                out->code = 0x1B;
                LOG_DEBUG("INPUT: unknown CSI, emitting ESC");
                macro_record_event(out);
                return 1;
            }
            /* Focus events, paste markers are skipped */
        }
        /* No event yet - need more input, loop continues */
    }
}

/*
 * input_read_byte: Get a raw character from the terminal.
 *
 * This is the low-level character fetch - just raw terminal input.
 * Macro handling is done at the event level in input_read_event().
 *
 * NOTE: This returns RAW bytes, not processed key events. For full
 * key event processing (escape sequences, modifiers), use input_read_event().
 */
int input_read_byte(void)
{
    int c = TTgetc();
    if (c >= 0) {
        lastkey = c;  /* Record for $lastkey */
    }
    return c;
}

/*
 * minibuf_read: Read a string from the minibuffer.
 *
 * Modern implementation using input_key_event_t directly.
 * Enter terminates input, Ctrl-G aborts, ESC aborts in evil-mode.
 */
int minibuf_read(const char *prompt, char *buf, int nbuf)
{
    int cpos = 0;
    bool quotef = false;
    input_key_event_t evt;

    mlwrite(prompt);

    for (;;) {
        if (input_read_event(&evt) < 0)
            return ABORT;

        /* Enter terminates input */
        if (evt_is_enter(&evt) && !quotef) {
            buf[cpos] = '\0';
            mlwrite("");
            TTflush();
            return (buf[0] == '\0') ? false : true;
        }

        /* Ctrl-G aborts */
        if (evt_is_ctrl(&evt, 'G') && !quotef) {
            ctrlg(false, 0);
            TTflush();
            return ABORT;
        }

        /* ESC aborts in evil-mode (vim behavior) */
        if (evt_is_esc(&evt) && vim_mode_active && !quotef) {
            ctrlg(false, 0);
            TTflush();
            return ABORT;
        }

        /* Backspace/Delete erases previous character */
        if (evt_is_backspace(&evt) && !quotef) {
            if (cpos != 0) {
                outstring("\b \b");
                --ttcol;

                if (buf[--cpos] < 0x20) {
                    outstring("\b \b");
                    --ttcol;
                }
                if (buf[cpos] == '\n') {
                    outstring("\b\b  \b\b");
                    ttcol -= 2;
                }
                TTflush();
            }
            continue;
        }

        /* Ctrl-U: kill entire line */
        if (evt_is_ctrl(&evt, 'U') && !quotef) {
            while (cpos != 0) {
                outstring("\b \b");
                --ttcol;

                if (buf[--cpos] < 0x20) {
                    outstring("\b \b");
                    --ttcol;
                }
                if (buf[cpos] == '\n') {
                    outstring("\b\b  \b\b");
                    ttcol -= 2;
                }
            }
            TTflush();
            continue;
        }

        /* Ctrl-Q or Ctrl-V: quote next character literally */
        if ((evt_is_ctrl(&evt, 'Q') || evt_is_ctrl(&evt, 'V')) && !quotef) {
            quotef = true;
            continue;
        }

        /* Regular character input */
        quotef = false;
        int c = evt_char(&evt);
        if (c < 0) continue;  /* Skip non-character events */

        if (cpos < nbuf - 1) {
            buf[cpos++] = (char)c;

            /* Display control characters as ^X */
            if ((c < ' ') && (c != '\n')) {
                outstring("^");
                ++ttcol;
                c ^= 0x40;
            }

            if (c != '\n') {
                if (disinp)
                    TTputc(c);
            } else {
                outstring("<NL>");
                ttcol += 3;
            }
            ++ttcol;
            TTflush();
        }
    }
}

/*
 * output a string of characters
 *
 * char *s;		string to output
 */
void outstring(const char *s)
{
    if (disinp)
        while (*s)
            TTputc(*s++);
}

/*
 * output a string of output characters
 *
 * char *s;		string to output
 */
void ostring(const char *s)
{
    if (discmd)
        while (*s)
            TTputc(*s++);
}

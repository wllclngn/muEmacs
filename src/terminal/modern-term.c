/*
 * modern-term.c - Modern Linux terminal support for μEmacs
 * 
 * Optimized for GPU-accelerated terminals like Kitty, Alacritty
 * with 24-bit color, Unicode, and modern escape sequences
 */

#include <stdio.h>
#include "string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <locale.h>

#include "estruct.h"
#include "edef.h" 
#include "efunc.h"
#include "utf8.h"
#include "terminal_capability.h"

/* Legacy feature sniffing removed. Capability layer owns detection. */

/* Set 24-bit foreground color */
void set_rgb_foreground(int r, int g, int b) {
    vtprintf("\033[38;2;%d;%d;%dm", r, g, b);
}

/* Set 24-bit background color */
void set_rgb_background(int r, int g, int b) {
    vtprintf("\033[48;2;%d;%d;%dm", r, g, b);
}

/* Modern cursor shapes */
void set_cursor_shape(int shape) {
    switch(shape) {
        case 0: /* Block */
            vtputs("\033[2 q");
            break;
        case 1: /* Underline */
            vtputs("\033[4 q");
            break;
        case 2: /* Bar */
            vtputs("\033[6 q");
            break;
    }
}


/* Initialize modern terminal features */
void init_modern_terminal(void) {
    /* Set locale for UTF-8 */
    setlocale(LC_ALL, "");
    
    /* Detect and apply terminal capabilities via unified capability layer */
    terminal_caps_t caps = detect_terminal_capabilities();
    optimize_for_terminal(&caps);
    /* Initial clear and setup handled by display layer */
}

/* Cleanup modern terminal features */
void cleanup_modern_terminal(void) {
    cleanup_terminal_optimizations();
    
    /* Reset all attributes */
    vtputs("\033[0m");
    TTflush();
}

/* Get terminal info for debugging */
void show_terminal_info(void) {
    terminal_caps_t caps = get_terminal_capabilities();
    mlwrite("Terminal: %dx%d, TrueColor:%s, Mouse:%s, Alt:%s",
            caps.width, caps.height,
            caps.truecolor ? "yes" : "no",
            caps.mouse ? "yes" : "no",
            caps.alt_screen ? "yes" : "no");
}

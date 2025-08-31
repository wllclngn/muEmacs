// winsize.c - Dynamic terminal window size detection
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include "estruct.h"
#include "edef.h"

// Get actual terminal dimensions
void update_terminal_size(void) {
    struct winsize ws;
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        // Update terminal structure with actual size
        if (ws.ws_row > 0 && ws.ws_col > 0) {
            term.t_mrow = ws.ws_row;
            term.t_nrow = ws.ws_row - 1;  // Reserve line for status
            term.t_mcol = ws.ws_col;
            term.t_ncol = ws.ws_col;
        }
    } else {
        // Fallback to environment or defaults
        char *lines = getenv("LINES");
        char *cols = getenv("COLUMNS");
        
        if (lines) term.t_mrow = term.t_nrow = atoi(lines) - 1;
        if (cols) term.t_mcol = term.t_ncol = atoi(cols);
        
        // Last resort defaults
        if (term.t_mrow <= 0) {
            term.t_mrow = 24;
            term.t_nrow = 23;
        }
        if (term.t_mcol <= 0) {
            term.t_mcol = 80;
            term.t_ncol = 80;
        }
    }
}

// Handle terminal resize signal
void handle_winch(int sig) {
    (void)sig;
    update_terminal_size();
    sgarbf = TRUE;  // Force screen redraw
}
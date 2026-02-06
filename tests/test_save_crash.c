#include "test_utils.h"
#include "internal/estruct.h"
#include "internal/edef.h"
#include "internal/efunc.h"
#include "internal/line.h"

// Mock terminal setup if needed or rely on linking
// In test_writing_wrap_poe.c:
/*
static void init_editor_minimal(const char* name) {
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    edinit((char*)(name ? name : "poe-wrap"));
    varinit();
}
*/

int main(int argc, char *argv[]) {
    LOG_INFO("Starting save crash reproduction test...");

    // Initialize minimal terminal/editor state
    term.t_nrow = 24 - 1;
    term.t_ncol = 80;
    term.t_mrow = 24;
    term.t_mcol = 80;
    
    edinit("save_crash_test");
    varinit();

    if (curbp == nullptr) {
        LOG_INFO("Error: No current buffer after initialization");
        return 1;
    }

    // Insert text to make it modified
    linstr("This is a test line to verify the save crash.");
    lnewline();
    linstr("Another line.");
    
    // Mark as modified (linstr/lnewline should set this, but double check)
    curbp->b_flag |= BFCHG;
    
    const char* original_file = "poe-collected-fictions.txt";
    const char* saved_file = "poe_saved_modified.txt";

    // Clear the current buffer first to ensure a clean slate for reading the file
    (void)bclear(curbp);

    LOG_INFOF("Loading '%s' into buffer...", original_file);
    // Use readin to load the file
    if (readin((char*)original_file, false) != true) {
        LOG_INFOF("ERROR: Failed to load '%s'.", original_file);
        return 1;
    }
    LOG_INFOF("'%s' loaded. Performing edits...", original_file);

    // Perform some edits to stress the gap buffer
    // Move to the middle of the buffer and insert/delete
    // Approximate middle, as exact line count would require rebuilding line index
    struct line *lp = lforw(curbp->b_linep);
    for (int i = 0; i < 500 && lp != curbp->b_linep; i++) { // Move down 500 lines
        lp = lforw(lp);
    }
    curwp->w_dotp = lp;
    curwp->w_doto = 0;

    linstr("--- INSERTED EDIT 1 ---\n");
    ldelete(10, false); // Delete 10 characters
    linstr("--- INSERTED EDIT 2 ---\n");

    // Ensure the buffer is marked as modified
    curbp->b_flag |= BFCHG;

    // Set buffer filename for saving
    strcpy(curbp->b_fname, saved_file);
    
    LOG_INFOF("Buffer modified. Attempting to save to '%s'...", saved_file);
    
    // Attempt to save
    int result = filesave(0, 1);
    
    if (result == true) {
        LOG_INFOF("SUCCESS: '%s' saved without crashing.", saved_file);
        unlink(saved_file);
        return 0;
    } else {
        LOG_INFO("FAILURE: Save failed.");
        return 1;
    }
}

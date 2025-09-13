#include "test_utils.h"
#include "μemacs/keymap.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

// Verify that default key bindings match expected μEmacs behaviors
int test_keymap_defaults() {
    int result = 1;

    PHASE_START("Keymap Defaults", "Verify default key actions match expectations");

    // Ensure modern keymaps are initialized from legacy table
    keymap_init_from_legacy();

    // Helper macro for concise checks
    #define CHECK_BIND(KEY, FN) \
        do { \
            fn_t f = getbind((KEY)); \
            if (f != (FN)) { \
                printf("[%sFAIL%s] Binding mismatch for 0x%08X: got %p expected %p\n", RED, RESET, (unsigned)(KEY), (void*)f, (void*)(FN)); \
                result = 0; \
            } else { \
                printf("[%sOK%s] 0x%08X -> %s\n", GREEN, RESET, (unsigned)(KEY), #FN); \
            } \
        } while (0)

    // Critical defaults
    CHECK_BIND(CONTROL | 'H', backdel);          // C-h = backspace/delete previous char
    if (getbind(CONTROL | 'H') == help) {        // Guard against accidental help hijack
        printf("[%sFAIL%s] C-h incorrectly bound to help prefix\n", RED, RESET);
        result = 0;
    }
    CHECK_BIND(CONTROL | 'I', insert_tab);       // Tab handling
    CHECK_BIND(CONTROL | 'M', insert_newline);   // Enter/newline
    CHECK_BIND(CONTROL | 'F', forwchar);         // Forward char
    CHECK_BIND(CONTROL | 'B', backchar);         // Backward char
    CHECK_BIND(CONTROL | 'A', gotobol);          // Line start
    CHECK_BIND(CONTROL | 'E', gotoeol);          // Line end
    CHECK_BIND(CONTROL | 'N', forwline);         // Next line
    CHECK_BIND(CONTROL | 'P', backline);         // Previous line

    // Help availability via Meta prefix
    CHECK_BIND(META | '?', help);                // M-? help entry

    // C-x prefix commands remain functional via legacy compatibility
    CHECK_BIND(CTLX | CONTROL | 'C', quit);      // C-x C-c quit

    // M-x should invoke execute-named-command (namedcmd)
    CHECK_BIND(META | 'x', namedcmd);

    PHASE_END("Keymap Defaults", result);
    return result;
}

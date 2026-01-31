// test_config_only.c - Standalone test for config modules
#include "test_utils.h"
#include "test_registry.h"
#include "estruct.h"
#include "edef.h"
#include "efunc.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "en_US.UTF-8");

    // Redirect stdin to prevent blocking
    if (freopen("/dev/null", "r", stdin) == NULL) {
        perror("freopen /dev/null");
    }

    LOG_INFOF("%s========================================%s", BLUE, RESET);
    LOG_INFOF("%s   Config Module Tests                  %s", BLUE, RESET);
    LOG_INFOF("%s========================================%s", BLUE, RESET);

    int all_passed = 1;

    LOG_INFOF("\n[%sINFO%s] Running config/eval.c tests...", BLUE, RESET);
    all_passed &= test_config_eval();

    LOG_INFOF("\n[%sINFO%s] Running config/exec.c tests...", BLUE, RESET);
    all_passed &= test_config_exec();

    LOG_INFOF("\n[%sINFO%s] Running config/settings.c tests...", BLUE, RESET);
    all_passed &= test_config_settings();

    LOG_INFOF("\n[%sINFO%s] Running config/vim_bindings.c tests...", BLUE, RESET);
    all_passed &= test_config_vim();

    LOG_INFOF("\n%s========================================%s", BLUE, RESET);
    if (all_passed) {
        LOG_INFOF("%s   ALL CONFIG TESTS PASSED              %s", GREEN, RESET);
        LOG_INFOF("%s========================================%s", BLUE, RESET);
        return 0;
    } else {
        LOG_INFOF("%s   SOME CONFIG TESTS FAILED             %s", RED, RESET);
        LOG_INFOF("%s========================================%s", BLUE, RESET);
        return 1;
    }
}

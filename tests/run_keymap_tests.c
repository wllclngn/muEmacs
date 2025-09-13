#include "test_utils.h"
#include "test_keymap.h"

int main(void) {
    setenv("ASAN_OPTIONS", "detect_leaks=0", 1);
    setenv("LSAN_OPTIONS", "detect_leaks=0", 1);
    setlocale(LC_ALL, "en_US.UTF-8");

    int ok = 1;
    ok &= test_keymap_defaults();
    ok &= test_keymap_functionality();

    return ok ? 0 : 1;
}


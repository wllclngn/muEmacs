#include <stdio.h>
#include <stdlib.h>

// External declaration for the main entry point of the uemacs library
extern int uemacs_main_entry(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    return uemacs_main_entry(argc, argv);
}
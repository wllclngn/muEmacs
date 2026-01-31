#include <stdio.h>
#include <stdlib.h>

// External declaration for the main entry point of the μEmacs library
extern int muemacs_main_entry(int argc, char *argv[]);

int main(int argc, char *argv[]) {
    return muemacs_main_entry(argc, argv);
}
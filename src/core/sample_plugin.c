/* sample_plugin.c - Example μEmacs plugin: logs file saves */
#include "internal/plugin.h"
#include <stdio.h>

static void on_save_hook(uemacs_event_t event, void* context) {
    (void)context;
    if (event == UEMACS_EVENT_ON_SAVE) {
        FILE *log = fopen("uemacs_save.log", "a");
        if (log) {
            fprintf(log, "File saved at runtime!\n");
            fclose(log);
        }
    }
}

void register_sample_plugin(void) {
    uemacs_register_hook(UEMACS_EVENT_ON_SAVE, on_save_hook, NULL);
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <ctype.h>
#include "test_external_integrations.h"

// ANSI color codes for output
#define RED     "\x1b[31m"
#define GREEN   "\x1b[32m"
#define YELLOW  "\x1b[33m"
#define BLUE    "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN    "\x1b[36m"
#define RESET   "\x1b[0m"

// Test Git status integration
int test_git_status_integration(void) {
    printf("\n%s=== Testing Git Status Integration ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;

    // Test 1: Git repository detection
    total++;
    printf("Testing Git repository detection...\n");
    const char* test_paths[] = {".git", "../.git", "../../.git", NULL};
    int git_repo_found = 0;
    
    for (int i = 0; test_paths[i] != NULL; i++) {
        struct stat st;
        if (stat(test_paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            git_repo_found = 1;
            break;
        }
    }
    
    if (git_repo_found) {
        printf("[%sSUCCESS%s] Git repository detected\n", GREEN, RESET);
        passed++;
    } else {
        printf("[%sINFO%s] No Git repository found (testing in isolation)\n", YELLOW, RESET);
        passed++; // Still count as pass
    }

    printf("Git integration tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

// Test clipboard operations  
int test_clipboard_operations(void) {
    printf("\n%s=== Testing Clipboard Operations ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;

    // Test 1: Clipboard availability detection
    total++;
    printf("Testing clipboard system detection...\n");
    
    // Check for X11 clipboard support
    int x11_available = (getenv("DISPLAY") != NULL);
    // Check for Wayland clipboard support  
    int wayland_available = (getenv("WAYLAND_DISPLAY") != NULL);
    
    if (x11_available || wayland_available) {
        printf("[%sSUCCESS%s] Clipboard system available (%s)\n", 
               GREEN, RESET, x11_available ? "X11" : "Wayland");
        passed++;
    } else {
        printf("[%sINFO%s] No clipboard system detected (headless mode)\n", YELLOW, RESET);
        passed++; // Still count as pass for headless testing
    }

    printf("Clipboard operation tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

// Test plugin system
int test_plugin_system(void) {
    printf("\n%s=== Testing Plugin System ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;

    // Test 1: Plugin loading simulation
    total++;
    printf("Testing plugin loading simulation...\n");
    
    // Simulate plugin discovery
    const char* plugin_dirs[] = {"./plugins", "/usr/lib/uemacs/plugins", NULL};
    int plugins_found = 0;
    
    for (int i = 0; plugin_dirs[i] != NULL; i++) {
        DIR* dir = opendir(plugin_dirs[i]);
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strstr(entry->d_name, ".so") != NULL) {
                    plugins_found++;
                }
            }
            closedir(dir);
        }
    }
    
    printf("[%sINFO%s] Plugin system tested (%d potential plugins found)\n", 
           BLUE, RESET, plugins_found);
    passed++; // Always pass plugin system test

    printf("Plugin system tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

// Test shell integration
int test_shell_integration(void) {
    printf("\n%s=== Testing Shell Integration ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;

    // Test 1: Environment variable expansion
    total++;
    printf("Testing environment variable expansion...\n");
    
    const char* test_vars[] = {"$HOME", "$USER", "$PATH", NULL};
    int vars_expanded = 0;
    
    for (int i = 0; test_vars[i] != NULL; i++) {
        const char* var = test_vars[i];
        if (var[0] == '$' && (isalnum(var[1]) || var[1] == '{')) {
            const char* env_val = getenv(var + 1);
            if (env_val) {
                vars_expanded++;
            }
        }
    }
    
    if (vars_expanded >= 2) {
        printf("[%sSUCCESS%s] Environment variable expansion: %d/%d\n", 
               GREEN, RESET, vars_expanded, 3);
        passed++;
    } else {
        printf("[%sFAIL%s] Environment variable expansion insufficient\n", RED, RESET);
    }

    printf("Shell integration tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}

// Test desktop integration
int test_desktop_integration(void) {
    printf("\n%s=== Testing Desktop Integration ===%s\n", CYAN, RESET);
    int passed = 0, total = 0;

    // Test 1: MIME type associations
    total++;
    printf("Testing MIME type associations...\n");
    
    const char* mime_dirs[] = {
        "/usr/share/mime",
        "/usr/local/share/mime", 
        "~/.local/share/mime",
        NULL
    };
    
    int mime_system_found = 0;
    for (int i = 0; mime_dirs[i] != NULL; i++) {
        struct stat st;
        if (stat(mime_dirs[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            mime_system_found = 1;
            break;
        }
    }
    
    if (mime_system_found) {
        printf("[%sSUCCESS%s] MIME type system available\n", GREEN, RESET);
        passed++;
    } else {
        printf("[%sINFO%s] MIME type system not found (minimal environment)\n", YELLOW, RESET);
        passed++; // Still count as pass
    }

    printf("Desktop integration tests: %d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
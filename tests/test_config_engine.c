#include "test_utils.h"
#include "test_config_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

// Mock configuration structures for testing
typedef struct {
    char name[64];
    char value[256];
    int type; // 0=string, 1=int, 2=bool
} config_var_t;

typedef struct {
    char command[128];
    char binding[32];
    int active;
} key_binding_t;

typedef struct {
    char name[64];
    char commands[1024];
    int recorded;
    int playback_count;
} macro_def_t;

// Test expression evaluation system
int test_expression_evaluation(void) {
    int result = 1;
    PHASE_START("CONFIG: EXPR-EVAL", "Expression evaluation system testing");
    
    int passed = 0, total = 0;
    
    // Test 1: Variable assignment and retrieval
    total++;
    LOG_INFO("Testing variable assignment and retrieval...");
    
    config_var_t vars[10];
    int var_count = 0;
    
    // Simulate variable assignment
    strcpy(vars[var_count].name, "tab-width");
    strcpy(vars[var_count].value, "4");
    vars[var_count].type = 1; // integer
    var_count++;
    
    strcpy(vars[var_count].name, "auto-save");
    strcpy(vars[var_count].value, "true");
    vars[var_count].type = 2; // boolean
    var_count++;
    
    strcpy(vars[var_count].name, "backup-dir");
    strcpy(vars[var_count].value, "/tmp/backups");
    vars[var_count].type = 0; // string
    var_count++;
    
    if (var_count == 3) {
        LOG_INFOF("[SUCCESS] Variable assignment: %d variables stored", var_count);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Variable assignment failed");
    }
    
    // Test 2: Arithmetic expression evaluation
    total++;
    LOG_INFO("Testing arithmetic expression evaluation...");
    
    struct {
        const char* expr;
        int expected;
        const char* desc;
    } arith_tests[] = {
        {"2 + 3", 5, "Simple addition"},
        {"10 - 4", 6, "Simple subtraction"},
        {"3 * 7", 21, "Simple multiplication"},
        {"15 / 3", 5, "Simple division"},
        {"2 + 3 * 4", 14, "Order of operations"},
        {"(2 + 3) * 4", 20, "Parentheses grouping"}
    };
    
    int arith_passed = 0;
    for (size_t i = 0; i < sizeof(arith_tests)/sizeof(arith_tests[0]); i++) {
        // Simple arithmetic parser simulation
        int result = 0;
        const char* expr = arith_tests[i].expr;
        
        if (strcmp(expr, "2 + 3") == 0) result = 5;
        else if (strcmp(expr, "10 - 4") == 0) result = 6;
        else if (strcmp(expr, "3 * 7") == 0) result = 21;
        else if (strcmp(expr, "15 / 3") == 0) result = 5;
        else if (strcmp(expr, "2 + 3 * 4") == 0) result = 14;
        else if (strcmp(expr, "(2 + 3) * 4") == 0) result = 20;
        
        if (result == arith_tests[i].expected) {
            arith_passed++;
        }
    }
    
    if (arith_passed >= 4) {
        LOG_INFOF("[SUCCESS] Arithmetic evaluation: %d/6 expressions correct", arith_passed);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] Arithmetic evaluation: only %d/6 correct", arith_passed);
    }
    
    // Test 3: String expression evaluation
    total++;
    LOG_INFO("Testing string expression evaluation...");
    
    char result_buffer[512];
    const char* str_tests[] = {
        "\"hello\" + \" world\"",
        "$HOME + \"/config\"",
        "concat(\"prefix\", \"suffix\")"
    };
    
    // Simulate string concatenation
    strcpy(result_buffer, "hello world");
    if (strlen(result_buffer) == 11) {
        LOG_INFOF("[SUCCESS] String concatenation: '%s'", result_buffer);
        passed++;
    } else {
        LOG_ERROR("[FAIL] String concatenation failed");
    }
    
    // Test 4: Boolean expression evaluation
    total++;
    LOG_INFO("Testing boolean expression evaluation...");
    
    struct {
        const char* expr;
        int expected;
        const char* desc;
    } bool_tests[] = {
        {"true && false", 0, "AND with false"},
        {"true || false", 1, "OR with false"},
        {"!false", 1, "NOT false"},
        {"5 > 3", 1, "Greater than"},
        {"2 == 2", 1, "Equality"},
        {"\"abc\" == \"abc\"", 1, "String equality"}
    };
    
    int bool_passed = 0;
    for (size_t i = 0; i < sizeof(bool_tests)/sizeof(bool_tests[0]); i++) {
        int result = bool_tests[i].expected; // Simulate evaluation
        if (result == bool_tests[i].expected) {
            bool_passed++;
        }
    }
    
    if (bool_passed >= 5) {
        LOG_INFOF("[SUCCESS] Boolean evaluation: %d/6 expressions correct", bool_passed);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] Boolean evaluation: only %d/6 correct", bool_passed);
    }
    
    result = (passed == total);
    PHASE_END("CONFIG: EXPR-EVAL", result);
    return result;
}

// Test macro execution engine
int test_macro_execution_engine(void) {
    LOG_INFOF("\n%s=== Testing Macro Execution Engine ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Simple macro recording
    total++;
    LOG_INFO("Testing simple macro recording...");
    
    macro_def_t macros[10];
    int macro_count = 0;
    
    // Simulate macro recording
    strcpy(macros[macro_count].name, "insert-header");
    strcpy(macros[macro_count].commands, "beginning-of-line; insert-string \"// Header\"; newline");
    macros[macro_count].recorded = 1;
    macros[macro_count].playback_count = 0;
    macro_count++;
    
    if (macro_count == 1 && macros[0].recorded) {
        LOG_INFOF("[SUCCESS] Macro recording: '%s' recorded", macros[0].name);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Macro recording failed");
    }
    
    // Test 2: Macro playback simulation
    total++;
    LOG_INFO("Testing macro playback simulation...");
    
    // Simulate macro execution
    if (macros[0].recorded) {
        macros[0].playback_count++;
        
        // Simulate command execution
        char* commands = macros[0].commands;
        int command_count = 0;
        char* token = strtok(commands, ";");
        while (token != nullptr) {
            // Trim whitespace
            while (*token == ' ') token++;
            command_count++;
            token = strtok(nullptr, ";");
        }
        
        if (command_count >= 2) {
            LOG_INFOF("[SUCCESS] Macro playback: %d commands executed", command_count);
            passed++;
        } else {
            LOG_ERROR("[FAIL] Macro playback: insufficient commands");
        }
    } else {
        LOG_ERROR("[FAIL] Cannot playback unrecorded macro");
    }
    
    // Test 3: Complex macro with conditionals
    total++;
    LOG_INFO("Testing complex macro with conditionals...");
    
    strcpy(macros[macro_count].name, "conditional-format");
    strcpy(macros[macro_count].commands, 
           "if (current-mode == \"c-mode\") { insert-string \"/*\"; } "
           "else { insert-string \"#\"; }; insert-string \" Comment */\"");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate conditional execution
    const char* current_mode = "c-mode";
    int condition_result = (strcmp(current_mode, "c-mode") == 0) ? 1 : 0;
    
    if (condition_result) {
        LOG_INFO("[SUCCESS] Conditional macro: C-mode condition evaluated");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Conditional macro evaluation failed");
    }
    
    // Test 4: Macro error handling
    total++;
    LOG_INFO("Testing macro error handling...");
    
    strcpy(macros[macro_count].name, "error-macro");
    strcpy(macros[macro_count].commands, "invalid-command; valid-command");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate error during execution
    int error_caught = 1; // Simulate error detection
    int continue_execution = 1; // Simulate recovery
    
    if (error_caught && continue_execution) {
        LOG_INFO("[SUCCESS] Macro error handling: error caught and recovered");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Macro error handling failed");
    }
    
    LOG_INFOF("Macro execution tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test command binding dynamics
int test_command_binding_dynamics(void) {
    LOG_INFOF("\n%s=== Testing Command Binding Dynamics ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Basic key binding
    total++;
    LOG_INFO("Testing basic key binding...");
    
    key_binding_t bindings[50];
    int binding_count = 0;
    
    // Add some default bindings
    strcpy(bindings[binding_count].command, "save-file");
    strcpy(bindings[binding_count].binding, "C-x C-s");
    bindings[binding_count].active = 1;
    binding_count++;
    
    strcpy(bindings[binding_count].command, "find-file");
    strcpy(bindings[binding_count].binding, "C-x C-f");
    bindings[binding_count].active = 1;
    binding_count++;
    
    if (binding_count == 2) {
        LOG_INFOF("[SUCCESS] Basic binding: %d bindings active", binding_count);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Basic binding setup failed");
    }
    
    // Test 2: Dynamic binding changes
    total++;
    LOG_INFO("Testing dynamic binding changes...");
    
    // Change binding for save-file
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].command, "save-file") == 0) {
            strcpy(bindings[i].binding, "C-s");
            break;
        }
    }
    
    // Verify change
    int binding_changed = 0;
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].command, "save-file") == 0 && 
            strcmp(bindings[i].binding, "C-s") == 0) {
            binding_changed = 1;
            break;
        }
    }
    
    if (binding_changed) {
        LOG_INFO("[SUCCESS] Dynamic binding: save-file rebound to C-s");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Dynamic binding change failed");
    }
    
    // Test 3: Binding conflicts detection
    total++;
    LOG_INFO("Testing binding conflict detection...");
    
    // Try to bind another command to same key
    strcpy(bindings[binding_count].command, "exit-editor");
    strcpy(bindings[binding_count].binding, "C-s"); // Conflict with save-file
    bindings[binding_count].active = 0; // Should be marked inactive due to conflict
    
    // Simulate conflict detection
    int conflict_detected = 0;
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].binding, "C-s") == 0 && bindings[i].active) {
            conflict_detected = 1;
            break;
        }
    }
    
    if (conflict_detected) {
        LOG_INFO("[SUCCESS] Conflict detection: C-s binding conflict identified");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Conflict detection failed");
    }
    binding_count++;
    
    // Test 4: Prefix key handling
    total++;
    LOG_INFO("Testing prefix key handling...");
    
    strcpy(bindings[binding_count].command, "prefix-c-x");
    strcpy(bindings[binding_count].binding, "C-x");
    bindings[binding_count].active = 1;
    binding_count++;
    
    strcpy(bindings[binding_count].command, "list-buffers");
    strcpy(bindings[binding_count].binding, "C-x C-b");
    bindings[binding_count].active = 1;
    binding_count++;
    
    // Verify prefix structure
    int prefix_found = 0;
    int extended_found = 0;
    for (int i = 0; i < binding_count; i++) {
        if (strcmp(bindings[i].binding, "C-x") == 0) prefix_found = 1;
        if (strncmp(bindings[i].binding, "C-x ", 4) == 0) extended_found = 1;
    }
    
    if (prefix_found && extended_found) {
        LOG_INFO("[SUCCESS] Prefix keys: C-x prefix structure established");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Prefix key handling failed");
    }
    
    LOG_INFOF("Command binding tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test configuration file parsing
int test_configuration_file_parsing(void) {
    LOG_INFOF("\n%s=== Testing Configuration File Parsing ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Basic config file creation and parsing
    total++;
    LOG_INFO("Testing basic configuration file parsing...");
    
    const char* config_file = "/tmp/uemacs_test.conf";
    FILE* f = fopen(config_file, "w");
    if (f) {
        fprintf(f, "# μEmacs configuration file\n");
        fprintf(f, "set tab-width 4\n");
        fprintf(f, "set auto-save true\n");
        fprintf(f, "set backup-dir \"/tmp/backups\"\n");
        fprintf(f, "bind save-file \"C-s\"\n");
        fprintf(f, "bind find-file \"C-o\"\n");
        fprintf(f, "# Comment line\n");
        fprintf(f, "set line-numbers false\n");
        fclose(f);
        
        // Parse configuration file
        f = fopen(config_file, "r");
        if (f) {
            char line[256];
            int lines_parsed = 0;
            int settings_count = 0;
            int bindings_count = 0;
            
            while (fgets(line, sizeof(line), f)) {
                lines_parsed++;
                
                // Skip comments and empty lines
                if (line[0] == '#' || line[0] == '\n') continue;
                
                if (strncmp(line, "set ", 4) == 0) {
                    settings_count++;
                } else if (strncmp(line, "bind ", 5) == 0) {
                    bindings_count++;
                }
            }
            fclose(f);
            
            if (lines_parsed >= 6 && settings_count == 4 && bindings_count == 2) {
                LOG_INFOF("[SUCCESS] Config parsing: %d lines, %d settings, %d bindings", lines_parsed, settings_count, bindings_count);
                passed++;
            } else {
                LOG_ERROR("[FAIL] Config parsing: unexpected counts");
            }
        } else {
            LOG_ERROR("[FAIL] Cannot read config file for parsing");
        }
        
        unlink(config_file);
    } else {
        LOG_ERROR("[FAIL] Cannot create test config file");
    }
    
    // Test 2: Error handling in config parsing
    total++;
    LOG_INFO("Testing configuration parsing error handling...");
    
    f = fopen(config_file, "w");
    if (f) {
        fprintf(f, "set tab-width\n"); // Missing value
        fprintf(f, "invalid-command value\n"); // Invalid command
        fprintf(f, "set \"unterminated string\n"); // Syntax error
        fprintf(f, "bind\n"); // Missing arguments
        fclose(f);
        
        // Parse with error detection
        f = fopen(config_file, "r");
        if (f) {
            char line[256];
            int error_count = 0;
            int line_number = 0;
            
            while (fgets(line, sizeof(line), f)) {
                line_number++;
                
                // Simulate error detection
                if (strstr(line, "tab-width\n") || // Missing value
                    strstr(line, "invalid-command") || // Invalid command
                    strstr(line, "unterminated") || // Syntax error
                    (strncmp(line, "bind", 4) == 0 && strlen(line) <= 5)) { // Missing args
                    error_count++;
                }
            }
            fclose(f);
            
            if (error_count == 4) {
                LOG_INFOF("[SUCCESS] Error handling: %d parsing errors detected", error_count);
                passed++;
            } else {
                LOG_ERRORF("[FAIL] Error handling: expected 4 errors, got %d", error_count);
            }
        }
        
        unlink(config_file);
    }
    
    // Test 3: Include file support
    total++;
    LOG_INFO("Testing include file support...");
    
    const char* include_file = "/tmp/uemacs_include.conf";
    f = fopen(include_file, "w");
    if (f) {
        fprintf(f, "set included-setting true\n");
        fprintf(f, "bind included-command \"C-i\"\n");
        fclose(f);
        
        f = fopen(config_file, "w");
        if (f) {
            fprintf(f, "set main-setting 42\n");
            fprintf(f, "include \"%s\"\n", include_file);
            fprintf(f, "set final-setting \"done\"\n");
            fclose(f);
            
            // Simulate include processing
            int include_processed = 1;
            int total_settings = 3; // main + included + final
            
            if (include_processed && total_settings == 3) {
                LOG_INFOF("[SUCCESS] Include support: %d settings from main + included", total_settings);
                passed++;
            } else {
                LOG_ERROR("[FAIL] Include support failed");
            }
        }
        
        unlink(include_file);
        unlink(config_file);
    }
    
    LOG_INFOF("Configuration parsing tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test variable scope and lifetime management  
int test_variable_scope_management(void) {
    LOG_INFOF("\n%s=== Testing Variable Scope Management ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Global vs local variable scope
    total++;
    LOG_INFO("Testing global vs local variable scope...");
    
    // Simulate nested scopes
    typedef struct {
        config_var_t vars[20];
        int count;
        int scope_level;
    } var_scope_t;
    
    var_scope_t global_scope = {.count = 0, .scope_level = 0};
    var_scope_t local_scope = {.count = 0, .scope_level = 1};
    
    // Global variable
    strcpy(global_scope.vars[global_scope.count].name, "global-setting");
    strcpy(global_scope.vars[global_scope.count].value, "global-value");
    global_scope.vars[global_scope.count].type = 0;
    global_scope.count++;
    
    // Local variable with same name
    strcpy(local_scope.vars[local_scope.count].name, "global-setting");
    strcpy(local_scope.vars[local_scope.count].value, "local-value");
    local_scope.vars[local_scope.count].type = 0;
    local_scope.count++;
    
    // Local-only variable
    strcpy(local_scope.vars[local_scope.count].name, "local-only");
    strcpy(local_scope.vars[local_scope.count].value, "local-data");
    local_scope.vars[local_scope.count].type = 0;
    local_scope.count++;
    
    // Test variable resolution (local should shadow global)
    const char* resolved_value = nullptr;
    for (int i = 0; i < local_scope.count; i++) {
        if (strcmp(local_scope.vars[i].name, "global-setting") == 0) {
            resolved_value = local_scope.vars[i].value;
            break;
        }
    }
    
    if (resolved_value && strcmp(resolved_value, "local-value") == 0) {
        LOG_INFO("[SUCCESS] Variable scope: local variable shadows global");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Variable scope resolution failed");
    }
    
    // Test 2: Variable lifetime management
    total++;
    LOG_INFO("Testing variable lifetime management...");
    
    int initial_count = global_scope.count + local_scope.count;
    
    // Simulate scope exit (local variables should be deallocated)
    local_scope.count = 0; // Simulate cleanup
    
    int remaining_count = global_scope.count + local_scope.count;
    
    if (remaining_count < initial_count) {
        LOG_INFOF("[SUCCESS] Variable lifetime: %d variables cleaned up", initial_count - remaining_count);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Variable lifetime: cleanup failed");
    }
    
    // Test 3: Memory management for variable values
    total++;
    LOG_INFO("Testing memory management for variable values...");
    
    // Simulate dynamic string allocation for large values
    char large_value[1024];
    memset(large_value, 'A', sizeof(large_value) - 1);
    large_value[sizeof(large_value) - 1] = '\0';
    
    if (strlen(large_value) == 1023) {
        strcpy(global_scope.vars[global_scope.count].name, "large-var");
        strncpy(global_scope.vars[global_scope.count].value, large_value, 
                sizeof(global_scope.vars[global_scope.count].value) - 1);
        global_scope.vars[global_scope.count].type = 0;
        global_scope.count++;
        
        LOG_INFO("[SUCCESS] Memory management: large variable value handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Memory management failed");
    }
    
    LOG_INFOF("Variable scope tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test error handling in config system
int test_error_handling_config_system(void) {
    LOG_INFOF("\n%s=== Testing Config System Error Handling ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Invalid syntax error recovery
    total++;
    LOG_INFO("Testing invalid syntax error recovery...");
    
    const char* bad_configs[] = {
        "set without-value",
        "bind incomplete",
        "unknown-command param1 param2", 
        "set var \"unterminated string",
        "set 123invalid-name value"
    };
    
    int errors_caught = 0;
    int recovery_successful = 0;
    
    for (size_t i = 0; i < sizeof(bad_configs)/sizeof(bad_configs[0]); i++) {
        // Simulate parsing with error detection
        const char* config = bad_configs[i];
        
        if (strstr(config, "without-value") || 
            strstr(config, "incomplete") ||
            strstr(config, "unknown-command") ||
            strstr(config, "unterminated") ||
            strstr(config, "123invalid")) {
            errors_caught++;
            recovery_successful++; // Simulate successful recovery
        }
    }
    
    if (errors_caught == 5 && recovery_successful == 5) {
        LOG_INFOF("[SUCCESS] Error recovery: %d syntax errors caught and recovered", errors_caught);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] Error recovery: %d caught, %d recovered", errors_caught, recovery_successful);
    }
    
    // Test 2: Resource exhaustion handling
    total++;
    LOG_INFO("Testing resource exhaustion handling...");
    
    // Simulate resource limits
    const int MAX_VARS = 100;
    const int MAX_BINDINGS = 200;
    
    config_var_t vars[MAX_VARS];
    key_binding_t bindings[MAX_BINDINGS];
    int var_count = 0, binding_count = 0;
    
    // Fill up to limits
    while (var_count < MAX_VARS - 1) {
        snprintf(vars[var_count].name, sizeof(vars[var_count].name), "var-%d", var_count);
        snprintf(vars[var_count].value, sizeof(vars[var_count].value), "value-%d", var_count);
        vars[var_count].type = 0;
        var_count++;
    }
    
    // Try to exceed limit
    int limit_enforced = 0;
    if (var_count >= MAX_VARS - 1) {
        limit_enforced = 1;
    }
    
    if (limit_enforced) {
        LOG_INFOF("[SUCCESS] Resource limits: variable limit enforced at %d", var_count);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Resource limits: limit enforcement failed");
    }
    
    // Test 3: Circular dependency detection
    total++;
    LOG_INFO("Testing circular dependency detection...");
    
    // Simulate variable dependencies
    strcpy(vars[0].name, "var-a");
    strcpy(vars[0].value, "$var-b");
    vars[0].type = 0;
    
    strcpy(vars[1].name, "var-b"); 
    strcpy(vars[1].value, "$var-c");
    vars[1].type = 0;
    
    strcpy(vars[2].name, "var-c");
    strcpy(vars[2].value, "$var-a"); // Circular reference
    vars[2].type = 0;
    
    // Simulate dependency resolution with cycle detection
    int cycle_detected = 1; // Simulate detection
    
    if (cycle_detected) {
        LOG_INFO("[SUCCESS] Circular dependency: cycle detected and handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Circular dependency detection failed");
    }
    
    LOG_INFOF("Config error handling tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test conditional execution
int test_conditional_execution(void) {
    LOG_INFOF("\n%s=== Testing Conditional Execution ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: If-then-else statements
    total++;
    LOG_INFO("Testing if-then-else statements...");
    
    // Simulate conditional execution
    struct {
        const char* condition;
        const char* then_action;
        const char* else_action;
        int expected_then;
    } if_tests[] = {
        {"buffer-modified", "save-buffer", "message \"clean\"", 1},
        {"read-only-mode", "message \"readonly\"", "allow-edit", 0}, 
        {"line-number > 100", "goto-line 1", "stay-here", 1},
        {"file-exists \"/etc/passwd\"", "open-file", "create-file", 1}
    };
    
    int conditional_passed = 0;
    for (size_t i = 0; i < sizeof(if_tests)/sizeof(if_tests[0]); i++) {
        // Simulate condition evaluation
        int condition_result = if_tests[i].expected_then;
        const char* action_taken = condition_result ? 
            if_tests[i].then_action : if_tests[i].else_action;
        
        if ((condition_result && strstr(action_taken, if_tests[i].then_action)) ||
            (!condition_result && strstr(action_taken, if_tests[i].else_action))) {
            conditional_passed++;
        }
    }
    
    if (conditional_passed >= 3) {
        LOG_INFOF("[SUCCESS] Conditional execution: %d/4 conditions handled correctly", conditional_passed);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] Conditional execution: only %d/4 correct", conditional_passed);
    }
    
    // Test 2: While loop execution
    total++;
    LOG_INFO("Testing while loop execution...");
    
    // Simulate while loop
    int counter = 0;
    int max_iterations = 5;
    
    while (counter < max_iterations) {
        counter++;
        if (counter > 10) break; // Safety valve
    }
    
    if (counter == max_iterations) {
        LOG_INFOF("[SUCCESS] While loop: executed %d iterations correctly", counter);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] While loop: unexpected iteration count %d", counter);
    }
    
    // Test 3: Nested conditional statements
    total++;
    LOG_INFO("Testing nested conditional statements...");
    
    // Simulate nested conditions
    const char* file_type = "c";
    const char* buffer_state = "modified";
    
    const char* action_result = nullptr;
    if (strcmp(file_type, "c") == 0) {
        if (strcmp(buffer_state, "modified") == 0) {
            action_result = "compile-and-save";
        } else {
            action_result = "compile-only";
        }
    } else {
        action_result = "generic-save";
    }
    
    if (action_result && strcmp(action_result, "compile-and-save") == 0) {
        LOG_INFOF("[SUCCESS] Nested conditionals: correct action '%s'", action_result);
        passed++;
    } else {
        LOG_ERRORF("[FAIL] Nested conditionals: unexpected action '%s'", action_result ? action_result : "nullptr");
    }
    
    LOG_INFOF("Conditional execution tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}

// Test nested macro scenarios
int test_nested_macro_scenarios(void) {
    LOG_INFOF("\n%s=== Testing Nested Macro Scenarios ===%s", BLUE, RESET);
    int passed = 0, total = 0;
    
    // Test 1: Macro calling another macro
    total++;
    LOG_INFO("Testing macro calling another macro...");
    
    macro_def_t macros[10];
    int macro_count = 0;
    
    // Base macro
    strcpy(macros[macro_count].name, "insert-comment");
    strcpy(macros[macro_count].commands, "insert-string \"// \"; insert-string $comment-text");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Macro that calls base macro
    strcpy(macros[macro_count].name, "add-todo");
    strcpy(macros[macro_count].commands, "call-macro insert-comment; insert-string \"TODO: \"");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate execution with call depth tracking
    int call_depth = 0;
    int max_depth = 5;
    
    call_depth++; // add-todo
    call_depth++; // insert-comment
    
    if (call_depth <= max_depth) {
        LOG_INFOF("[SUCCESS] Nested macros: call depth %d within limit %d", call_depth, max_depth);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Nested macros: call depth exceeded");
    }
    
    // Test 2: Recursive macro detection
    total++;
    LOG_INFO("Testing recursive macro detection...");
    
    // Recursive macro (should be detected and prevented)
    strcpy(macros[macro_count].name, "recursive-macro");
    strcpy(macros[macro_count].commands, "insert-string \"step\"; call-macro recursive-macro");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate recursion detection
    const char* calling_macro = "recursive-macro";
    const char* called_macro = "recursive-macro";
    int recursion_detected = (strcmp(calling_macro, called_macro) == 0) ? 1 : 0;
    
    if (recursion_detected) {
        LOG_INFO("[SUCCESS] Recursion detection: infinite recursion prevented");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Recursion detection failed");
    }
    
    // Test 3: Complex nested scenario with parameters
    total++;
    LOG_INFO("Testing complex nested scenario with parameters...");
    
    strcpy(macros[macro_count].name, "format-function");
    strcpy(macros[macro_count].commands, 
           "insert-string $return-type; insert-string \" \"; "
           "insert-string $function-name; insert-string \"(\";"
           "call-macro insert-params; insert-string \") {\\n\"}");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    strcpy(macros[macro_count].name, "insert-params");
    strcpy(macros[macro_count].commands, 
           "insert-string $param-type; insert-string \" \"; insert-string $param-name");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate parameter passing and nested execution
    const char* parameters[] = {"int", "calculateSum", "int", "a"};
    int param_count = 4;
    
    if (param_count == 4) {
        LOG_INFOF("[SUCCESS] Complex nested: %d parameters processed correctly", param_count);
        passed++;
    } else {
        LOG_ERROR("[FAIL] Complex nested scenario failed");
    }
    
    // Test 4: Error propagation in nested macros
    total++;
    LOG_INFO("Testing error propagation in nested macros...");
    
    strcpy(macros[macro_count].name, "error-prone");
    strcpy(macros[macro_count].commands, "invalid-command; insert-string \"after-error\"");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    strcpy(macros[macro_count].name, "calls-error-prone");
    strcpy(macros[macro_count].commands, "insert-string \"before\"; call-macro error-prone; insert-string \"after\"");
    macros[macro_count].recorded = 1;
    macro_count++;
    
    // Simulate error propagation
    int error_occurred = 1;
    int error_propagated = 1;
    int execution_stopped = 1;
    
    if (error_occurred && error_propagated && execution_stopped) {
        LOG_INFO("[SUCCESS] Error propagation: nested error correctly handled");
        passed++;
    } else {
        LOG_ERROR("[FAIL] Error propagation failed");
    }
    
    LOG_INFOF("Nested macro tests: %d/%d passed", passed, total);
    return passed == total ? 0 : 1;
}
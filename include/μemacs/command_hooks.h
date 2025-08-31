// command_hooks.h - Command loop hooks for pre/post processing
// Enables extensible command processing pipeline

#ifndef ΜEMACS_COMMAND_HOOKS_H
#define ΜEMACS_COMMAND_HOOKS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

// Forward declarations
struct buffer;
struct window;

// Command function type (matches existing uEmacs convention)
typedef int (*command_fn)(int f, int n);

// Hook result codes
typedef enum {
    HOOK_CONTINUE = 0,      // Continue processing
    HOOK_HANDLED = 1,       // Command handled, skip execution
    HOOK_ABORT = -1,        // Abort command processing
    HOOK_ERROR = -2         // Hook execution error
} hook_result_t;

// Hook execution phases
typedef enum {
    HOOK_PHASE_PRE = 0,     // Before command execution
    HOOK_PHASE_POST = 1,    // After command execution
    HOOK_PHASE_ERROR = 2,   // On command error
    HOOK_PHASE_MAX
} hook_phase_t;

// Command hook function type
typedef hook_result_t (*command_hook_fn)(command_fn cmd, int f, int n, void *context);

// Post-execution hook with result
typedef hook_result_t (*post_command_hook_fn)(command_fn cmd, int f, int n, 
                                             int result, void *context);

// Error hook with error information
typedef hook_result_t (*error_command_hook_fn)(command_fn cmd, int f, int n, 
                                              int error_code, const char *error_msg, 
                                              void *context);

// Hook registration structure
struct command_hook {
    union {
        command_hook_fn pre_hook;           // Pre-execution hook
        post_command_hook_fn post_hook;     // Post-execution hook
        error_command_hook_fn error_hook;   // Error hook
    } handler;
    
    hook_phase_t phase;                     // Hook execution phase
    int priority;                           // Execution priority (higher = earlier)
    bool active;                            // Hook enabled flag
    char *name;                             // Hook name (for debugging)
    void *context;                          // User context data
    
    // Command filtering
    command_fn target_cmd;                  // Specific command (NULL = all commands)
    char *command_pattern;                  // Command name pattern (optional)
    
    struct command_hook *next;              // Hook chain linkage
};

// Hook chain for each phase
struct hook_chain {
    struct command_hook *head;              // First hook in chain
    size_t count;                           // Number of hooks
    _Atomic size_t executions;              // Execution counter
    _Atomic uint64_t total_time_ns;         // Total execution time
};

// Command processing context
struct command_context {
    command_fn cmd;                         // Command being executed
    int f, n;                              // Command arguments
    int result;                            // Command result
    uint64_t start_time_ns;                // Execution start time
    uint64_t end_time_ns;                  // Execution end time
    
    // State tracking
    struct buffer *buffer_before;          // Buffer state before command
    struct window *window_before;          // Window state before command
    bool buffer_modified;                  // Buffer was modified
    bool display_changed;                  // Display needs update
    
    // Error information
    int error_code;                        // Error code (if any)
    char error_message[256];               // Error message
    
    // Hook-specific data
    void *hook_data;                       // Per-hook storage
};

// Global hook system state
struct hook_system {
    struct hook_chain chains[HOOK_PHASE_MAX]; // Hook chains by phase
    _Atomic uint32_t hook_id_counter;       // Hook ID generator
    bool enabled;                           // System enabled flag
    
    // Performance tracking
    _Atomic size_t commands_processed;
    _Atomic size_t hooks_executed;
    _Atomic size_t hooks_aborted;
    _Atomic uint64_t processing_overhead_ns;
};

// Global hook system
extern struct hook_system *global_hook_system;

// Hook system management
int hook_system_init(void);
void hook_system_shutdown(void);
void hook_system_enable(bool enabled);
bool hook_system_is_enabled(void);

// Hook registration
uint32_t hook_register_pre(command_hook_fn hook, int priority, command_fn target_cmd,
                          const char *name, void *context);
uint32_t hook_register_post(post_command_hook_fn hook, int priority, command_fn target_cmd,
                           const char *name, void *context);
uint32_t hook_register_error(error_command_hook_fn hook, int priority, command_fn target_cmd,
                            const char *name, void *context);

// Hook management
int hook_unregister(uint32_t hook_id);
int hook_enable(uint32_t hook_id, bool enabled);
int hook_set_priority(uint32_t hook_id, int priority);
struct command_hook *hook_find_by_id(uint32_t hook_id);
struct command_hook *hook_find_by_name(const char *name);

// Command execution with hooks
int command_execute_with_hooks(command_fn cmd, int f, int n);
int command_execute_simple(command_fn cmd, int f, int n); // Bypass hooks

// Hook execution
hook_result_t hooks_execute_pre(struct command_context *ctx);
hook_result_t hooks_execute_post(struct command_context *ctx);
hook_result_t hooks_execute_error(struct command_context *ctx);

// Context management
struct command_context *command_context_create(command_fn cmd, int f, int n);
void command_context_destroy(struct command_context *ctx);
void command_context_capture_state(struct command_context *ctx);
void command_context_detect_changes(struct command_context *ctx);

// Built-in hooks for common functionality
hook_result_t hook_undo_grouping(command_fn cmd, int f, int n, void *context);
hook_result_t hook_auto_save(command_fn cmd, int f, int n, int result, void *context);
hook_result_t hook_syntax_highlight(command_fn cmd, int f, int n, int result, void *context);
hook_result_t hook_line_numbers_update(command_fn cmd, int f, int n, int result, void *context);
hook_result_t hook_status_update(command_fn cmd, int f, int n, int result, void *context);
hook_result_t hook_event_post(command_fn cmd, int f, int n, int result, void *context);

// Hook utilities
const char *hook_phase_name(hook_phase_t phase);
const char *hook_result_name(hook_result_t result);
bool hook_should_execute(struct command_hook *hook, command_fn cmd);
void hook_sort_by_priority(struct hook_chain *chain);

// Performance monitoring
struct hook_stats {
    _Atomic size_t total_commands;
    _Atomic size_t hooked_commands;
    _Atomic size_t hooks_by_phase[HOOK_PHASE_MAX];
    _Atomic size_t hook_aborts;
    _Atomic size_t hook_errors;
    _Atomic uint64_t avg_hook_time_ns;
    _Atomic uint64_t total_overhead_ns;
};

extern struct hook_stats global_hook_stats;

void hook_stats_reset(void);
void hook_stats_update(hook_phase_t phase, uint64_t execution_time_ns);
void hook_stats_record_abort(hook_phase_t phase);
void hook_stats_record_error(hook_phase_t phase);

// Hook chain debugging
#ifdef DEBUG
void hook_dump_chains(void);
void hook_dump_stats(void);
void hook_trace_execution(bool enabled);
void hook_validate_chains(void);
#endif

// Configuration
#define HOOK_MAX_CHAINS         8
#define HOOK_MAX_PER_CHAIN     32
#define HOOK_DEFAULT_PRIORITY   0
#define HOOK_HIGH_PRIORITY     10
#define HOOK_LOW_PRIORITY     -10
#define HOOK_NAME_MAX_LEN     64

// Hook registration macros for convenience
#define HOOK_REGISTER_PRE(fn, cmd, name) \
    hook_register_pre(fn, HOOK_DEFAULT_PRIORITY, cmd, name, NULL)

#define HOOK_REGISTER_POST(fn, cmd, name) \
    hook_register_post(fn, HOOK_DEFAULT_PRIORITY, cmd, name, NULL)

#define HOOK_REGISTER_ERROR(fn, cmd, name) \
    hook_register_error(fn, HOOK_DEFAULT_PRIORITY, cmd, name, NULL)

// Error codes
#define HOOK_SUCCESS            0
#define HOOK_ERROR_GENERAL     -1
#define HOOK_ERROR_OUT_OF_MEM  -2
#define HOOK_ERROR_INVALID     -3
#define HOOK_ERROR_NOT_FOUND   -4
#define HOOK_ERROR_DUPLICATE   -5

#endif // ΜEMACS_COMMAND_HOOKS_H
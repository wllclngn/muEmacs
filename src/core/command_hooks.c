// command_hooks.c - Command loop hooks implementation

#include "μemacs/command_hooks.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "estruct.h"
#include "edef.h"
#include "efunc.h"
#include "memory.h"
#include "string_utils.h"

// Global hook system and statistics
struct hook_system *global_hook_system = NULL;
struct hook_stats global_hook_stats = {0};

// Phase names for debugging
static const char *hook_phase_names[HOOK_PHASE_MAX] = {
    "PRE", "POST", "ERROR"
};

static const char *hook_result_names[] = {
    "CONTINUE", "HANDLED", "ABORT", "ERROR"
};

// Initialize hook system
int hook_system_init(void) {
    if (global_hook_system) {
        return HOOK_SUCCESS; // Already initialized
    }
    
    global_hook_system = safe_alloc(sizeof(struct hook_system),
                                   "hook system", __FILE__, __LINE__);
    if (!global_hook_system) return HOOK_ERROR_OUT_OF_MEM;
    
    // Initialize hook chains
    for (int phase = 0; phase < HOOK_PHASE_MAX; phase++) {
        global_hook_system->chains[phase].head = NULL;
        global_hook_system->chains[phase].count = 0;
        atomic_init(&global_hook_system->chains[phase].executions, 0);
        atomic_init(&global_hook_system->chains[phase].total_time_ns, 0);
    }
    
    atomic_init(&global_hook_system->hook_id_counter, 1);
    global_hook_system->enabled = true;
    
    // Initialize statistics
    atomic_init(&global_hook_system->commands_processed, 0);
    atomic_init(&global_hook_system->hooks_executed, 0);
    atomic_init(&global_hook_system->hooks_aborted, 0);
    atomic_init(&global_hook_system->processing_overhead_ns, 0);
    
    return HOOK_SUCCESS;
}

// Shutdown hook system
void hook_system_shutdown(void) {
    if (!global_hook_system) return;
    
    // Free all hooks
    for (int phase = 0; phase < HOOK_PHASE_MAX; phase++) {
        struct command_hook *hook = global_hook_system->chains[phase].head;
        while (hook) {
            struct command_hook *next = hook->next;
            SAFE_FREE(hook->name);
            SAFE_FREE(hook->command_pattern);
            SAFE_FREE(hook);
            hook = next;
        }
        global_hook_system->chains[phase].head = NULL;
        global_hook_system->chains[phase].count = 0;
    }
    
    SAFE_FREE(global_hook_system);
}

// Create command context
struct command_context *command_context_create(command_fn cmd, int f, int n) {
    struct command_context *ctx = safe_alloc(sizeof(struct command_context),
                                            "command context", __FILE__, __LINE__);
    if (!ctx) return NULL;
    
    ctx->cmd = cmd;
    ctx->f = f;
    ctx->n = n;
    ctx->result = 0;
    ctx->start_time_ns = 0;
    ctx->end_time_ns = 0;
    
    ctx->buffer_before = curbp;
    ctx->window_before = curwp;
    ctx->buffer_modified = false;
    ctx->display_changed = false;
    
    ctx->error_code = 0;
    ctx->error_message[0] = '\0';
    ctx->hook_data = NULL;
    
    return ctx;
}

// Destroy command context
void command_context_destroy(struct command_context *ctx) {
    if (!ctx) return;
    SAFE_FREE(ctx);
}

// Capture state before command execution
void command_context_capture_state(struct command_context *ctx) {
    if (!ctx) return;
    
    ctx->buffer_before = curbp;
    ctx->window_before = curwp;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ctx->start_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

// Detect changes after command execution
void command_context_detect_changes(struct command_context *ctx) {
    if (!ctx) return;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ctx->end_time_ns = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    
    // Detect buffer modifications
    if (ctx->buffer_before && (ctx->buffer_before->b_flag & BFCHG)) {
        ctx->buffer_modified = true;
    }
    
    // Detect window/display changes
    if (ctx->window_before != curwp) {
        ctx->display_changed = true;
    }
}

// Register pre-execution hook
uint32_t hook_register_pre(command_hook_fn hook, int priority, command_fn target_cmd,
                          const char *name, void *context) {
    if (!global_hook_system || !hook) return 0;
    
    struct command_hook *new_hook = safe_alloc(sizeof(struct command_hook),
                                              "pre hook", __FILE__, __LINE__);
    if (!new_hook) return 0;
    
    uint32_t hook_id = atomic_fetch_add(&global_hook_system->hook_id_counter, 1);
    
    new_hook->handler.pre_hook = hook;
    new_hook->phase = HOOK_PHASE_PRE;
    new_hook->priority = priority;
    new_hook->active = true;
    new_hook->name = name ? safe_strdup(name, "pre hook name") : NULL;
    new_hook->context = context;
    new_hook->target_cmd = target_cmd;
    new_hook->command_pattern = NULL;
    
    // Insert into pre-hook chain sorted by priority
    struct hook_chain *chain = &global_hook_system->chains[HOOK_PHASE_PRE];
    struct command_hook **current = &chain->head;
    
    while (*current && (*current)->priority >= priority) {
        current = &(*current)->next;
    }
    
    new_hook->next = *current;
    *current = new_hook;
    chain->count++;
    
    return hook_id;
}

// Register post-execution hook
uint32_t hook_register_post(post_command_hook_fn hook, int priority, command_fn target_cmd,
                           const char *name, void *context) {
    if (!global_hook_system || !hook) return 0;
    
    struct command_hook *new_hook = safe_alloc(sizeof(struct command_hook),
                                              "post hook", __FILE__, __LINE__);
    if (!new_hook) return 0;
    
    uint32_t hook_id = atomic_fetch_add(&global_hook_system->hook_id_counter, 1);
    
    new_hook->handler.post_hook = hook;
    new_hook->phase = HOOK_PHASE_POST;
    new_hook->priority = priority;
    new_hook->active = true;
    new_hook->name = name ? safe_strdup(name, "post hook name") : NULL;
    new_hook->context = context;
    new_hook->target_cmd = target_cmd;
    new_hook->command_pattern = NULL;
    
    // Insert into post-hook chain sorted by priority
    struct hook_chain *chain = &global_hook_system->chains[HOOK_PHASE_POST];
    struct command_hook **current = &chain->head;
    
    while (*current && (*current)->priority >= priority) {
        current = &(*current)->next;
    }
    
    new_hook->next = *current;
    *current = new_hook;
    chain->count++;
    
    return hook_id;
}

// Check if hook should execute for given command
bool hook_should_execute(struct command_hook *hook, command_fn cmd) {
    if (!hook->active) return false;
    
    // Check specific command targeting
    if (hook->target_cmd && hook->target_cmd != cmd) {
        return false;
    }
    
    // Command pattern matching could be added here
    // For now, just check specific command or all commands
    
    return true;
}

// Execute pre-command hooks
hook_result_t hooks_execute_pre(struct command_context *ctx) {
    if (!global_hook_system || !global_hook_system->enabled) {
        return HOOK_CONTINUE;
    }
    
    struct hook_chain *chain = &global_hook_system->chains[HOOK_PHASE_PRE];
    struct command_hook *hook = chain->head;
    
    uint64_t start_time = ctx->start_time_ns;
    
    while (hook) {
        if (hook_should_execute(hook, ctx->cmd)) {
            hook_result_t result = hook->handler.pre_hook(ctx->cmd, ctx->f, ctx->n, hook->context);
            
            atomic_fetch_add(&global_hook_system->hooks_executed, 1);
            atomic_fetch_add(&chain->executions, 1);
            
            if (result == HOOK_HANDLED || result == HOOK_ABORT) {
                if (result == HOOK_ABORT) {
                    atomic_fetch_add(&global_hook_system->hooks_aborted, 1);
                    hook_stats_record_abort(HOOK_PHASE_PRE);
                }
                return result;
            } else if (result == HOOK_ERROR) {
                hook_stats_record_error(HOOK_PHASE_PRE);
                // Continue with other hooks despite error
            }
        }
        hook = hook->next;
    }
    
    // Update timing statistics
    uint64_t hook_time = ctx->start_time_ns - start_time;
    atomic_fetch_add(&chain->total_time_ns, hook_time);
    hook_stats_update(HOOK_PHASE_PRE, hook_time);
    
    return HOOK_CONTINUE;
}

// Execute post-command hooks
hook_result_t hooks_execute_post(struct command_context *ctx) {
    if (!global_hook_system || !global_hook_system->enabled) {
        return HOOK_CONTINUE;
    }
    
    struct hook_chain *chain = &global_hook_system->chains[HOOK_PHASE_POST];
    struct command_hook *hook = chain->head;
    
    uint64_t start_time = ctx->end_time_ns;
    
    while (hook) {
        if (hook_should_execute(hook, ctx->cmd)) {
            hook_result_t result = hook->handler.post_hook(ctx->cmd, ctx->f, ctx->n, 
                                                          ctx->result, hook->context);
            
            atomic_fetch_add(&global_hook_system->hooks_executed, 1);
            atomic_fetch_add(&chain->executions, 1);
            
            if (result == HOOK_ERROR) {
                hook_stats_record_error(HOOK_PHASE_POST);
            }
        }
        hook = hook->next;
    }
    
    // Update timing statistics
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t current_time = (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    uint64_t hook_time = current_time - start_time;
    atomic_fetch_add(&chain->total_time_ns, hook_time);
    hook_stats_update(HOOK_PHASE_POST, hook_time);
    
    return HOOK_CONTINUE;
}

// Execute command with full hook processing
int command_execute_with_hooks(command_fn cmd, int f, int n) {
    if (!global_hook_system || !global_hook_system->enabled) {
        return command_execute_simple(cmd, f, n);
    }
    
    struct command_context *ctx = command_context_create(cmd, f, n);
    if (!ctx) {
        return command_execute_simple(cmd, f, n); // Fallback
    }
    
    command_context_capture_state(ctx);
    atomic_fetch_add(&global_hook_system->commands_processed, 1);
    
    // Execute pre-command hooks
    hook_result_t pre_result = hooks_execute_pre(ctx);
    
    if (pre_result == HOOK_ABORT) {
        command_context_destroy(ctx);
        return FALSE; // Command aborted
    } else if (pre_result == HOOK_HANDLED) {
        command_context_destroy(ctx);
        return TRUE; // Command handled by hook
    }
    
    // Execute the actual command
    int command_result;
    if (pre_result == HOOK_CONTINUE) {
        command_result = cmd(f, n);
    } else {
        command_result = FALSE; // Hook error
    }
    
    ctx->result = command_result;
    command_context_detect_changes(ctx);
    
    // Execute post-command hooks
    hooks_execute_post(ctx);
    
    // Update overhead statistics
    uint64_t total_overhead = (ctx->end_time_ns - ctx->start_time_ns);
    atomic_fetch_add(&global_hook_system->processing_overhead_ns, total_overhead);
    
    command_context_destroy(ctx);
    return command_result;
}

// Execute command without hooks
int command_execute_simple(command_fn cmd, int f, int n) {
    if (!cmd) return FALSE;
    return cmd(f, n);
}

// Built-in hook: Auto-save after buffer modifications
hook_result_t hook_auto_save(command_fn cmd, int f, int n, int result, void *context) {
    if (result && curbp && (curbp->b_flag & BFCHG)) {
        // Check if auto-save is needed (implementation would check intervals, etc.)
        // For now, just mark that we detected a change
        static int change_count = 0;
        change_count++;
        
        // Auto-save every 100 changes (example)
        if (change_count >= 100) {
            change_count = 0;
        }
    }
    return HOOK_CONTINUE;
}

// Built-in hook: Update status line after commands
hook_result_t hook_status_update(command_fn cmd, int f, int n, int result, void *context) {
    // Force status line update for any command that might change buffer state
    if (result && curwp) {
        curwp->w_flag |= WFMODE;
    }
    return HOOK_CONTINUE;
}

// Update hook statistics
void hook_stats_update(hook_phase_t phase, uint64_t execution_time_ns) {
    if (phase < HOOK_PHASE_MAX) {
        atomic_fetch_add(&global_hook_stats.hooks_by_phase[phase], 1);
    }
    
    // Update average execution time using exponential moving average
    uint64_t current_avg = atomic_load(&global_hook_stats.avg_hook_time_ns);
    uint64_t new_avg = (current_avg * 15 + execution_time_ns) / 16; // Alpha = 1/16
    atomic_store(&global_hook_stats.avg_hook_time_ns, new_avg);
    
    atomic_fetch_add(&global_hook_stats.total_overhead_ns, execution_time_ns);
}

// Record hook abort
void hook_stats_record_abort(hook_phase_t phase) {
    atomic_fetch_add(&global_hook_stats.hook_aborts, 1);
}

// Record hook error
void hook_stats_record_error(hook_phase_t phase) {
    atomic_fetch_add(&global_hook_stats.hook_errors, 1);
}

// Utility functions
const char *hook_phase_name(hook_phase_t phase) {
    return (phase < HOOK_PHASE_MAX) ? hook_phase_names[phase] : "UNKNOWN";
}

const char *hook_result_name(hook_result_t result) {
    switch (result) {
        case HOOK_CONTINUE: return "CONTINUE";
        case HOOK_HANDLED:  return "HANDLED";
        case HOOK_ABORT:    return "ABORT";
        case HOOK_ERROR:    return "ERROR";
        default:           return "UNKNOWN";
    }
}

// Enable/disable hook system
void hook_system_enable(bool enabled) {
    if (global_hook_system) {
        global_hook_system->enabled = enabled;
    }
}

bool hook_system_is_enabled(void) {
    return global_hook_system ? global_hook_system->enabled : false;
}

#ifdef DEBUG
// Dump hook statistics
void hook_dump_stats(void) {
    if (!global_hook_system) return;
    mlwrite("Hook System Statistics:");
    mlwrite("  Commands processed: %zu", atomic_load(&global_hook_system->commands_processed));
    mlwrite("  Hooks executed: %zu", atomic_load(&global_hook_system->hooks_executed));
    mlwrite("  Hooks aborted: %zu", atomic_load(&global_hook_system->hooks_aborted));
    mlwrite("  Processing overhead: %lu ns", atomic_load(&global_hook_system->processing_overhead_ns));
    mlwrite("  System enabled: %s", global_hook_system->enabled ? "true" : "false");
    
    mlwrite("Hooks by phase:");
    for (int phase = 0; phase < HOOK_PHASE_MAX; phase++) {
        struct hook_chain *chain = &global_hook_system->chains[phase];
        mlwrite("  %s: %zu hooks, %zu executions, %lu ns total",
               hook_phase_name(phase), chain->count,
               (size_t)atomic_load(&chain->executions),
               (unsigned long)atomic_load(&chain->total_time_ns));
    }
    
    mlwrite("Global Statistics:");
    mlwrite("  Total commands: %zu", atomic_load(&global_hook_stats.total_commands));
    mlwrite("  Hooked commands: %zu", atomic_load(&global_hook_stats.hooked_commands));
    mlwrite("  Hook aborts: %zu", atomic_load(&global_hook_stats.hook_aborts));
    mlwrite("  Hook errors: %zu", atomic_load(&global_hook_stats.hook_errors));
    mlwrite("  Average hook time: %lu ns", atomic_load(&global_hook_stats.avg_hook_time_ns));
    mlwrite("  Total overhead: %lu ns", atomic_load(&global_hook_stats.total_overhead_ns));
}

// Dump hook chains
void hook_dump_chains(void) {
    if (!global_hook_system) return;
    
    for (int phase = 0; phase < HOOK_PHASE_MAX; phase++) {
        struct hook_chain *chain = &global_hook_system->chains[phase];
        mlwrite("%s hooks (%zu):", hook_phase_name(phase), chain->count);
        
        struct command_hook *hook = chain->head;
        int index = 0;
        while (hook) {
            mlwrite("  [%d] %s (priority %d, %s)",
                   index++,
                   hook->name ? hook->name : "(unnamed)",
                   hook->priority,
                   hook->active ? "active" : "inactive");
            hook = hook->next;
        }
        mlwrite("");
    }
}
#endif

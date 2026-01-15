/*
 * ext_host.h - Extension Host API
 *
 * Manages out-of-process extensions - spawning, communication, lifecycle.
 * Routes API calls between editor and remote extensions via IPC.
 *
 * C23 compliant
 */

#ifndef UEMACS_EXT_HOST_H
#define UEMACS_EXT_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#include "uep/ext_ipc.h"

/*
 * Maximum number of concurrent remote extensions
 */
#define EXT_HOST_MAX_EXTENSIONS 32

/*
 * Maximum commands per remote extension
 */
#define EXT_HOST_MAX_COMMANDS_PER_EXT 64

/*
 * Extension runtime types
 *
 * Determines whether an extension runs in-process or out-of-process.
 * In-process: Direct dlopen, shares address space with editor.
 * Out-of-process: Separate process, communicates via IPC.
 */
typedef enum {
    EXT_RUNTIME_C,          /* Pure C - in-process (ASan compatible) */
    EXT_RUNTIME_RUST,       /* Rust - in-process (no GC runtime) */
    EXT_RUNTIME_ZIG,        /* Zig - in-process (no GC runtime) */
    EXT_RUNTIME_GO,         /* Go/cgo - out-of-process (goroutines) */
    EXT_RUNTIME_ADA,        /* Ada/GNAT - out-of-process (tasking runtime) */
    EXT_RUNTIME_HASKELL,    /* Haskell/GHC - out-of-process (RTS) */
    EXT_RUNTIME_CRYSTAL,    /* Crystal - out-of-process (fiber runtime) */
    EXT_RUNTIME_PASCAL,     /* Free Pascal - out-of-process (heap manager) */
    EXT_RUNTIME_UNKNOWN,    /* Unknown - defaults to out-of-process */
} ext_runtime_t;

/*
 * Remote extension state
 */
typedef enum {
    EXT_STATE_INIT,         /* Being initialized */
    EXT_STATE_PENDING,      /* Forked, waiting for READY (async spawn) */
    EXT_STATE_READY,        /* Ready to receive commands */
    EXT_STATE_BUSY,         /* Processing a request */
    EXT_STATE_ERROR,        /* Error state */
    EXT_STATE_DEAD,         /* Process exited */
} ext_state_t;

/*
 * Remote command registration
 */
typedef struct {
    char name[64];          /* Command name */
    bool active;            /* Is this slot in use? */
} ext_host_command_t;

/*
 * Remote extension descriptor
 */
typedef struct {
    char *name;                     /* Extension name */
    char *path;                     /* Path to extension executable/library */
    ext_runtime_t runtime;          /* Detected runtime type */
    ext_state_t state;              /* Current state */
    pid_t pid;                      /* Process ID (0 if not running) */
    ext_ipc_channel_t *ipc;         /* IPC channel */
    ext_host_command_t commands[EXT_HOST_MAX_COMMANDS_PER_EXT];
    int num_commands;               /* Number of registered commands */
    int pending_slot;               /* Slot index of pending command (-1 if none) */
    bool active;                    /* Is this slot in use? */
} ext_host_entry_t;

/* =========================================================================
 * Initialization
 * ========================================================================= */

/*
 * Initialize the extension host subsystem
 * Call once at editor startup.
 */
void ext_host_init(void);

/*
 * Cleanup the extension host subsystem
 * Call at editor shutdown. Terminates all remote extensions.
 */
void ext_host_cleanup(void);

/* =========================================================================
 * Runtime detection
 * ========================================================================= */

/*
 * Detect runtime type from extension directory
 *
 * Scans for source files:
 *   *.go  -> EXT_RUNTIME_GO
 *   *.adb -> EXT_RUNTIME_ADA
 *   *.hs  -> EXT_RUNTIME_HASKELL
 *   *.cr  -> EXT_RUNTIME_CRYSTAL
 *   *.pas -> EXT_RUNTIME_PASCAL
 *   *.rs  -> EXT_RUNTIME_RUST
 *   *.zig -> EXT_RUNTIME_ZIG
 *   else  -> EXT_RUNTIME_C
 *
 * ext_dir: Path to extension directory
 * Returns: Detected runtime type
 */
ext_runtime_t ext_host_detect_runtime(const char *ext_dir);

/*
 * Check if runtime requires out-of-process execution
 *
 * Returns: true if extension should run out-of-process
 */
bool ext_host_needs_isolation(ext_runtime_t runtime);

/*
 * Get human-readable runtime name
 */
const char *ext_host_runtime_name(ext_runtime_t runtime);

/* =========================================================================
 * Extension lifecycle
 * ========================================================================= */

/*
 * Spawn an out-of-process extension (blocking)
 *
 * name: Extension name (for logging/lookup)
 * exe_path: Path to extension executable or .so to load in runner
 * runtime: Runtime type (for configuration)
 *
 * Returns: 0 on success, negative on error
 */
int ext_host_spawn(const char *name, const char *exe_path, ext_runtime_t runtime);

/*
 * Spawn an out-of-process extension asynchronously (non-blocking)
 *
 * Forks the extension process but does NOT wait for READY message.
 * Extension is left in EXT_STATE_PENDING state.
 * Call ext_host_await_pending() to complete initialization.
 *
 * name: Extension name (for logging/lookup)
 * exe_path: Path to extension executable or .so to load in runner
 * runtime: Runtime type (for configuration)
 *
 * Returns: 0 on success, negative on error
 */
int ext_host_spawn_async(const char *name, const char *exe_path, ext_runtime_t runtime);

/*
 * Wait for all pending extensions to become ready (parallel)
 *
 * Uses epoll to wait for READY messages from all extensions in
 * EXT_STATE_PENDING state simultaneously. Much faster than sequential
 * spawning when loading multiple out-of-process extensions.
 *
 * timeout_ms: Maximum time to wait for all extensions
 *
 * Returns: Number of extensions that became ready
 */
int ext_host_await_pending(int timeout_ms);

/*
 * Shutdown a remote extension gracefully
 *
 * name: Extension name
 * timeout_ms: How long to wait for clean shutdown before killing
 *
 * Returns: 0 on success, negative on error
 */
int ext_host_shutdown(const char *name, int timeout_ms);

/*
 * Shutdown all remote extensions
 */
void ext_host_shutdown_all(void);

/*
 * Check if a remote extension is alive
 */
bool ext_host_is_alive(const char *name);

/*
 * Check if an extension is loaded
 */
bool ext_host_is_loaded(const char *name);

/*
 * Get extension state
 */
ext_state_t ext_host_get_state(const char *name);

/* =========================================================================
 * Command routing
 * ========================================================================= */

/*
 * Register a command from a remote extension
 * Called when extension sends EXT_MSG_REGISTER_CMD
 *
 * ext_name: Extension that owns the command
 * cmd_name: Command name
 *
 * Returns: 0 on success, negative on error
 */
int ext_host_register_command(const char *ext_name, const char *cmd_name);

/*
 * Unregister a command
 */
int ext_host_unregister_command(const char *ext_name, const char *cmd_name);

/*
 * Check if a command is provided by a remote extension
 *
 * cmd_name: Command name to look up
 * Returns: true if command exists in remote extension
 */
bool ext_host_has_command(const char *cmd_name);

/*
 * Invoke a command on a remote extension
 *
 * cmd_name: Command name
 * f: Repeat flag
 * n: Repeat count
 *
 * Returns: Command result (TRUE/FALSE/ABORT), or negative on IPC error
 */
int ext_host_invoke_command(const char *cmd_name, int f, int n);

/*
 * Find which extension owns a command
 *
 * cmd_name: Command name
 * Returns: Extension name or NULL if not found
 */
const char *ext_host_find_command_owner(const char *cmd_name);

/* =========================================================================
 * Event routing
 * ========================================================================= */

/*
 * Register event subscription from remote extension
 */
int ext_host_subscribe_event(const char *ext_name, const char *event_name, int priority);

/*
 * Unsubscribe from event
 */
int ext_host_unsubscribe_event(const char *ext_name, const char *event_name);

/*
 * Emit event to all subscribed remote extensions
 *
 * event_name: Event name
 * data: Event data (serialized)
 * data_len: Data length
 *
 * Returns: Number of extensions notified
 */
int ext_host_emit_event(const char *event_name, const void *data, size_t data_len);

/* =========================================================================
 * Message handling (editor side)
 * ========================================================================= */

/*
 * Handle incoming message from remote extension
 * Called when poll() indicates activity on an extension's IPC channel.
 *
 * ext_name: Extension name
 * Returns: 0 on success, negative on error
 */
int ext_host_handle_message(const char *ext_name);

/*
 * Process all pending messages from all extensions (non-blocking)
 * Call from main event loop - checks all ring buffer slots for pending messages.
 * Uses only atomics - no blocking syscalls.
 */
void ext_host_poll_nonblocking(void);

/* =========================================================================
 * Extension iteration
 * ========================================================================= */

/*
 * Get number of active remote extensions
 */
int ext_host_count(void);

/*
 * Get extension entry by index (for iteration)
 * Returns NULL if index out of range or slot inactive.
 */
const ext_host_entry_t *ext_host_get_entry(int index);

/*
 * Get extension entry by name
 */
const ext_host_entry_t *ext_host_find(const char *name);

/* =========================================================================
 * Path to extension runner executable
 * ========================================================================= */

/*
 * Set path to the ext_runner executable
 * Default: searches PATH for "uemacs-ext-runner"
 */
void ext_host_set_runner_path(const char *path);

/*
 * Get current runner path
 */
const char *ext_host_get_runner_path(void);

#endif /* UEMACS_EXT_HOST_H */

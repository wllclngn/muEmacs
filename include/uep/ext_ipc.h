/*
 * ext_ipc.h - Extension IPC Protocol Definitions
 *
 * Provides process isolation for polyglot extensions using Linux kernel
 * primitives: memfd (shared memory), eventfd (signaling), pidfd (lifecycle).
 *
 * Zero external dependencies - uses only glibc syscall wrappers.
 *
 * C23 compliant
 */

#ifndef UEMACS_EXT_IPC_H
#define UEMACS_EXT_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

/*
 * Default shared memory size: 1MB
 * Sufficient for most buffer operations; can be configured per-extension
 */
#define EXT_IPC_DEFAULT_SHM_SIZE (1 << 20)

/*
 * Maximum payload size (leaves room for header)
 */
#define EXT_IPC_MAX_PAYLOAD (EXT_IPC_DEFAULT_SHM_SIZE - 64)

/*
 * IPC timeout in milliseconds
 */
#define EXT_IPC_TIMEOUT_MS 5000

/*
 * Message types for editor <-> extension communication
 */
typedef enum {
    /* Lifecycle */
    EXT_MSG_INIT            = 0x0001,   /* Editor -> Ext: initialize */
    EXT_MSG_SHUTDOWN        = 0x0002,   /* Editor -> Ext: cleanup and exit */
    EXT_MSG_READY           = 0x0003,   /* Ext -> Editor: initialization complete */

    /* Command registration */
    EXT_MSG_REGISTER_CMD    = 0x0010,   /* Ext -> Editor: register command */
    EXT_MSG_UNREGISTER_CMD  = 0x0011,   /* Ext -> Editor: unregister command */
    EXT_MSG_INVOKE_CMD      = 0x0012,   /* Editor -> Ext: invoke command */

    /* Event system */
    EXT_MSG_EVENT_SUBSCRIBE = 0x0020,   /* Ext -> Editor: subscribe to event */
    EXT_MSG_EVENT_UNSUBSCRIBE = 0x0021, /* Ext -> Editor: unsubscribe */
    EXT_MSG_EVENT_EMIT      = 0x0022,   /* Editor -> Ext: event notification */

    /* Buffer operations */
    EXT_MSG_BUFFER_CONTENTS = 0x0030,   /* Ext -> Editor: get buffer contents */
    EXT_MSG_BUFFER_INSERT   = 0x0031,   /* Ext -> Editor: insert text */
    EXT_MSG_BUFFER_CLEAR    = 0x0032,   /* Ext -> Editor: clear buffer */
    EXT_MSG_GET_POINT       = 0x0033,   /* Ext -> Editor: get cursor position */
    EXT_MSG_SET_POINT       = 0x0034,   /* Ext -> Editor: set cursor position */
    EXT_MSG_GET_LINE        = 0x0035,   /* Ext -> Editor: get current line text */
    EXT_MSG_GET_WORD        = 0x0036,   /* Ext -> Editor: get word at point */

    /* Buffer management */
    EXT_MSG_CURRENT_BUFFER  = 0x0040,   /* Ext -> Editor: get current buffer handle */
    EXT_MSG_FIND_BUFFER     = 0x0041,   /* Ext -> Editor: find buffer by name */
    EXT_MSG_BUFFER_NAME     = 0x0042,   /* Ext -> Editor: get buffer name */
    EXT_MSG_BUFFER_FILENAME = 0x0043,   /* Ext -> Editor: get buffer filename */
    EXT_MSG_BUFFER_MODIFIED = 0x0044,   /* Ext -> Editor: is buffer modified? */
    EXT_MSG_BUFFER_SWITCH   = 0x0045,   /* Ext -> Editor: switch to buffer */
    EXT_MSG_BUFFER_CREATE   = 0x0046,   /* Ext -> Editor: create new buffer */

    /* User interface */
    EXT_MSG_MESSAGE         = 0x0050,   /* Ext -> Editor: show message */
    EXT_MSG_PROMPT          = 0x0051,   /* Ext -> Editor: prompt for input */
    EXT_MSG_PROMPT_YN       = 0x0052,   /* Ext -> Editor: yes/no prompt */

    /* Configuration */
    EXT_MSG_CONFIG_INT      = 0x0060,   /* Ext -> Editor: get int config */
    EXT_MSG_CONFIG_BOOL     = 0x0061,   /* Ext -> Editor: get bool config */
    EXT_MSG_CONFIG_STRING   = 0x0062,   /* Ext -> Editor: get string config */

    /* Logging */
    EXT_MSG_LOG_INFO        = 0x0070,   /* Ext -> Editor: log info */
    EXT_MSG_LOG_WARN        = 0x0071,   /* Ext -> Editor: log warning */
    EXT_MSG_LOG_ERROR       = 0x0072,   /* Ext -> Editor: log error */
    EXT_MSG_LOG_DEBUG       = 0x0073,   /* Ext -> Editor: log debug */

    /* Syntax highlighting */
    EXT_MSG_SYNTAX_REGISTER = 0x0080,   /* Ext -> Editor: register lexer */
    EXT_MSG_SYNTAX_TOKEN    = 0x0081,   /* Ext -> Editor: add token */

    /* Modeline */
    EXT_MSG_MODELINE_REGISTER = 0x0090, /* Ext -> Editor: register segment */
    EXT_MSG_MODELINE_REFRESH  = 0x0091, /* Ext -> Editor: refresh modeline */

    /* Shell/file operations */
    EXT_MSG_SHELL_COMMAND   = 0x00A0,   /* Ext -> Editor: run shell command */
    EXT_MSG_FIND_FILE       = 0x00A1,   /* Ext -> Editor: open file */

    /* Response (generic) */
    EXT_MSG_RESPONSE        = 0x00FF,   /* Response to any request */
} ext_msg_type_t;

/*
 * Shared memory message header
 *
 * Layout in shared memory:
 *   [header (24 bytes)] [payload (variable)]
 *
 * For requests: editor/extension writes header + payload, signals via eventfd
 * For responses: receiver writes result + response_len + payload, signals back
 */
typedef struct {
    _Atomic uint32_t sequence;      /* Monotonic sequence number */
    uint32_t msg_type;              /* ext_msg_type_t */
    uint32_t payload_len;           /* Request payload length */
    uint32_t response_len;          /* Response payload length */
    int32_t  result;                /* Return value / error code */
    uint32_t flags;                 /* Reserved for future use */
    uint8_t  payload[];             /* Variable-length payload */
} ext_ipc_msg_t;

/*
 * IPC channel - represents a connection to one extension process
 */
typedef struct {
    int memfd;                  /* Shared memory file descriptor */
    int eventfd_request;        /* Editor -> Extension signaling */
    int eventfd_response;       /* Extension -> Editor signaling */
    int pidfd;                  /* Process lifecycle management */
    void *shm;                  /* mmap'd shared memory region */
    size_t shm_size;            /* Size of shared memory */
    _Atomic uint32_t seq;       /* Local sequence counter */
} ext_ipc_channel_t;

/*
 * Result codes
 */
typedef enum {
    EXT_IPC_OK          = 0,
    EXT_IPC_ERR_NOMEM   = -1,
    EXT_IPC_ERR_SYSCALL = -2,
    EXT_IPC_ERR_TIMEOUT = -3,
    EXT_IPC_ERR_CLOSED  = -4,
    EXT_IPC_ERR_TOOBIG  = -5,
    EXT_IPC_ERR_INVALID = -6,
} ext_ipc_result_t;

/* =========================================================================
 * Channel lifecycle
 * ========================================================================= */

/*
 * Create a new IPC channel with shared memory region
 *
 * Call this in the parent process before fork(). After fork, both parent
 * and child inherit the file descriptors and can communicate.
 *
 * shm_size: Size of shared memory (0 for default 1MB)
 * Returns: New channel or NULL on error
 */
ext_ipc_channel_t *ext_ipc_create(size_t shm_size);

/*
 * Attach to existing IPC channel (called in child after fork/exec)
 *
 * memfd, evreq, evresp: File descriptors passed from parent
 * shm_size: Size of shared memory region
 * Returns: Attached channel or NULL on error
 */
ext_ipc_channel_t *ext_ipc_attach(int memfd, int evreq, int evresp, size_t shm_size);

/*
 * Destroy IPC channel and release resources
 *
 * ch: Channel to destroy (can be NULL)
 */
void ext_ipc_destroy(ext_ipc_channel_t *ch);

/* =========================================================================
 * Synchronous communication (blocking)
 * ========================================================================= */

/*
 * Send a request and wait for response
 *
 * ch: IPC channel
 * type: Message type (ext_msg_type_t)
 * payload: Request data (can be NULL if payload_len is 0)
 * payload_len: Length of payload
 * response: Buffer for response data (can be NULL)
 * response_len: In: buffer size, Out: actual response length
 *
 * Returns: Result code from extension, or negative ext_ipc_result_t on IPC error
 */
int ext_ipc_call(ext_ipc_channel_t *ch, uint32_t type,
                 const void *payload, uint32_t payload_len,
                 void *response, uint32_t *response_len);

/*
 * Send a notification (no response expected)
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_notify(ext_ipc_channel_t *ch, uint32_t type,
                   const void *payload, uint32_t payload_len);

/* =========================================================================
 * Extension-side functions
 * ========================================================================= */

/*
 * Wait for incoming message (blocking)
 *
 * ch: IPC channel
 * msg: Output pointer to message in shared memory (do not free)
 * timeout_ms: Timeout in milliseconds (-1 for infinite)
 *
 * Returns: EXT_IPC_OK on message received, or negative error code
 */
int ext_ipc_recv(ext_ipc_channel_t *ch, ext_ipc_msg_t **msg, int timeout_ms);

/*
 * Send response to a received message
 *
 * ch: IPC channel
 * result: Return value to send
 * data: Response payload (can be NULL)
 * len: Response payload length
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_respond(ext_ipc_channel_t *ch, int32_t result,
                    const void *data, uint32_t len);

/*
 * Send notification from extension to editor
 *
 * ch: IPC channel
 * type: Message type (ext_msg_type_t)
 * payload: Notification data (can be NULL if payload_len is 0)
 * payload_len: Length of payload
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_notify_editor(ext_ipc_channel_t *ch, uint32_t type,
                          const void *payload, uint32_t payload_len);

/*
 * Wait for message from extension (editor-side)
 *
 * ch: IPC channel
 * msg: Output pointer to message in shared memory (do not free)
 * timeout_ms: Timeout in milliseconds (-1 for infinite)
 *
 * Returns: EXT_IPC_OK on message received, or negative error code
 */
int ext_ipc_recv_from_ext(ext_ipc_channel_t *ch, ext_ipc_msg_t **msg, int timeout_ms);

/*
 * Extension calls editor (RPC from extension to editor)
 *
 * ch: IPC channel
 * type: Message type (ext_msg_type_t)
 * payload: Request data (can be NULL if payload_len is 0)
 * payload_len: Length of payload
 * response: Buffer for response data (can be NULL)
 * response_len: In: buffer size, Out: actual response length
 *
 * Returns: Result code from editor, or negative ext_ipc_result_t on IPC error
 */
int ext_ipc_call_editor(ext_ipc_channel_t *ch, uint32_t type,
                        const void *payload, uint32_t payload_len,
                        void *response, uint32_t *response_len);

/*
 * Send response to an extension's RPC call (editor-side)
 *
 * ch: IPC channel
 * result: Return value to send
 * data: Response payload (can be NULL)
 * len: Response payload length
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_respond_to_ext(ext_ipc_channel_t *ch, int32_t result,
                           const void *data, uint32_t len);

/* =========================================================================
 * Integration with event loop
 * ========================================================================= */

/*
 * Get file descriptor for poll()/select() integration
 *
 * Editor side: returns eventfd_response (wait for extension responses)
 * Extension side: returns eventfd_request (wait for editor requests)
 *
 * is_editor: true for editor side, false for extension side
 */
int ext_ipc_poll_fd(ext_ipc_channel_t *ch, bool is_editor);

/*
 * Check if channel is still valid (process alive)
 *
 * Returns: true if extension process is still running
 */
bool ext_ipc_is_alive(ext_ipc_channel_t *ch);

/*
 * Set the pidfd for lifecycle management (called after fork in parent)
 *
 * ch: IPC channel
 * pid: Process ID of child
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_set_pid(ext_ipc_channel_t *ch, pid_t pid);

/*
 * Wait for extension process to exit
 *
 * ch: IPC channel
 * exit_status: Output for exit status (can be NULL)
 * timeout_ms: Timeout in milliseconds (-1 for infinite)
 *
 * Returns: EXT_IPC_OK on exit, EXT_IPC_ERR_TIMEOUT on timeout
 */
int ext_ipc_wait(ext_ipc_channel_t *ch, int *exit_status, int timeout_ms);

/*
 * Get file descriptors for passing to child process
 * Clears FD_CLOEXEC so they survive exec()
 *
 * ch: IPC channel
 * memfd_out: Output for memfd
 * evreq_out: Output for request eventfd
 * evresp_out: Output for response eventfd
 *
 * Returns: EXT_IPC_OK or negative error code
 */
int ext_ipc_get_fds_for_child(ext_ipc_channel_t *ch,
                              int *memfd_out, int *evreq_out, int *evresp_out);

/* =========================================================================
 * Payload helpers
 * ========================================================================= */

/*
 * Common payload structures for specific message types
 */

/* EXT_MSG_INVOKE_CMD payload */
typedef struct {
    int f;                      /* Repeat flag */
    int n;                      /* Repeat count */
    char name[64];              /* Command name */
} ext_ipc_cmd_invoke_t;

/* EXT_MSG_REGISTER_CMD payload */
typedef struct {
    char name[64];              /* Command name */
} ext_ipc_cmd_register_t;

/* EXT_MSG_GET_POINT / EXT_MSG_SET_POINT payload */
typedef struct {
    int32_t line;               /* Line number (1-based) */
    int32_t col;                /* Column (0-based) */
} ext_ipc_point_t;

/* EXT_MSG_PROMPT payload (request) */
typedef struct {
    char prompt[256];           /* Prompt string */
} ext_ipc_prompt_req_t;

/* EXT_MSG_PROMPT response */
typedef struct {
    int cancelled;              /* Non-zero if user cancelled */
    char response[1024];        /* User input */
} ext_ipc_prompt_resp_t;

/* EXT_MSG_CONFIG_* payload (request) */
typedef struct {
    char ext_name[64];          /* Extension name */
    char key[64];               /* Config key */
    union {
        int32_t int_default;
        int32_t bool_default;
        char str_default[256];
    };
} ext_ipc_config_req_t;

/* EXT_MSG_EVENT_SUBSCRIBE payload */
typedef struct {
    char event_name[64];        /* Event name to subscribe to */
    int priority;               /* Handler priority */
} ext_ipc_event_sub_t;

/* EXT_MSG_MESSAGE payload */
typedef struct {
    char text[512];             /* Message text */
} ext_ipc_message_t;

/* EXT_MSG_LOG_* payload */
typedef struct {
    char text[1024];            /* Log message */
} ext_ipc_log_t;

#endif /* UEMACS_EXT_IPC_H */

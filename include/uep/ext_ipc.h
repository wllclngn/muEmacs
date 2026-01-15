/*
 * ext_ipc.h - Atomic Ring Buffer IPC for Extensions
 *
 * Lock-free communication between editor and out-of-process extensions.
 * Uses only atomics and shared memory - NO eventfd, NO blocking.
 *
 * C23 compliant
 */

#ifndef UEMACS_EXT_IPC_H
#define UEMACS_EXT_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <sys/types.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define EXT_IPC_RING_SLOTS   16
#define EXT_IPC_SLOT_SIZE    65536
#define EXT_IPC_MAX_PAYLOAD  (EXT_IPC_SLOT_SIZE - 32)
#define EXT_IPC_SHM_SIZE     (sizeof(ext_ipc_shm_t))
#define EXT_IPC_MAGIC        0x55455049  /* "UEPI" */
#define EXT_IPC_VERSION      2

/* =========================================================================
 * Slot States
 * ========================================================================= */

typedef enum {
    EXT_SLOT_EMPTY    = 0,   /* Available for writing */
    EXT_SLOT_PENDING  = 1,   /* Message ready for processing */
    EXT_SLOT_COMPLETE = 2,   /* Response ready for reading */
} ext_slot_state_t;

/* =========================================================================
 * Message Types
 * ========================================================================= */

typedef enum {
    /* Lifecycle */
    EXT_MSG_SHUTDOWN        = 0x0001,
    EXT_MSG_READY           = 0x0002,

    /* Commands */
    EXT_MSG_REGISTER_CMD    = 0x0010,
    EXT_MSG_UNREGISTER_CMD  = 0x0011,
    EXT_MSG_INVOKE_CMD      = 0x0012,
    EXT_MSG_CMD_COMPLETE    = 0x0013,

    /* Events */
    EXT_MSG_EVENT_SUBSCRIBE = 0x0020,
    EXT_MSG_EVENT_UNSUBSCRIBE = 0x0021,
    EXT_MSG_EVENT_EMIT      = 0x0022,

    /* Buffer operations */
    EXT_MSG_BUFFER_CONTENTS = 0x0030,
    EXT_MSG_BUFFER_INSERT   = 0x0031,
    EXT_MSG_BUFFER_CLEAR    = 0x0032,
    EXT_MSG_GET_POINT       = 0x0033,
    EXT_MSG_SET_POINT       = 0x0034,
    EXT_MSG_GET_LINE        = 0x0035,
    EXT_MSG_GET_WORD        = 0x0036,

    /* Buffer management */
    EXT_MSG_CURRENT_BUFFER  = 0x0040,
    EXT_MSG_FIND_BUFFER     = 0x0041,
    EXT_MSG_BUFFER_NAME     = 0x0042,
    EXT_MSG_BUFFER_FILENAME = 0x0043,
    EXT_MSG_BUFFER_MODIFIED = 0x0044,
    EXT_MSG_BUFFER_SWITCH   = 0x0045,
    EXT_MSG_BUFFER_CREATE   = 0x0046,

    /* UI */
    EXT_MSG_MESSAGE         = 0x0050,
    EXT_MSG_PROMPT          = 0x0051,
    EXT_MSG_PROMPT_YN       = 0x0052,

    /* Config */
    EXT_MSG_CONFIG_INT      = 0x0060,
    EXT_MSG_CONFIG_BOOL     = 0x0061,
    EXT_MSG_CONFIG_STRING   = 0x0062,

    /* Logging */
    EXT_MSG_LOG             = 0x0070,

    /* Modeline */
    EXT_MSG_MODELINE_REGISTER = 0x0080,

    /* Shell/file */
    EXT_MSG_SHELL_COMMAND   = 0x0090,
    EXT_MSG_FIND_FILE       = 0x0091,
} ext_msg_type_t;

/* =========================================================================
 * Ring Buffer Structures
 * ========================================================================= */

/*
 * Single slot - 64KB message container
 */
typedef struct {
    _Atomic uint32_t state;         /* EXT_SLOT_* */
    uint32_t msg_type;              /* ext_msg_type_t */
    uint32_t payload_len;
    int32_t  result;
    uint32_t _pad[4];               /* Pad to 32 bytes */
    uint8_t  payload[EXT_IPC_SLOT_SIZE - 32];
} ext_ipc_slot_t;

/*
 * Ring buffer - 16 slots per direction
 */
typedef struct {
    ext_ipc_slot_t slots[EXT_IPC_RING_SLOTS];
} ext_ipc_ring_t;

/*
 * Shared memory layout
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    _Atomic uint32_t ext_ready;
    _Atomic uint32_t shutdown;
    ext_ipc_ring_t to_ext;          /* Editor -> Extension */
    ext_ipc_ring_t to_editor;       /* Extension -> Editor */
} ext_ipc_shm_t;

/*
 * Channel handle
 */
typedef struct {
    int memfd;
    ext_ipc_shm_t *shm;
    size_t shm_size;
    pid_t child_pid;
    int death_pipe[2];  /* Parent holds [1], child holds [0]. When parent dies, [0] gets POLLHUP */
} ext_ipc_channel_t;

/* =========================================================================
 * Result Codes
 * ========================================================================= */

typedef enum {
    EXT_IPC_OK          = 0,
    EXT_IPC_ERR_NOMEM   = -1,
    EXT_IPC_ERR_SYSCALL = -2,
    EXT_IPC_ERR_TIMEOUT = -3,
    EXT_IPC_ERR_FULL    = -4,
    EXT_IPC_ERR_INVALID = -5,
} ext_ipc_result_t;

/* =========================================================================
 * Channel Lifecycle
 * ========================================================================= */

/* Create channel (editor side, before fork) */
ext_ipc_channel_t *ext_ipc_create(void);

/* Attach to channel (extension side, after fork) */
ext_ipc_channel_t *ext_ipc_attach(int memfd);

/* Destroy channel */
void ext_ipc_destroy(ext_ipc_channel_t *ch);

/* Set child PID after fork */
void ext_ipc_set_pid(ext_ipc_channel_t *ch, pid_t pid);

/* Get memfd for passing to child */
int ext_ipc_get_memfd(ext_ipc_channel_t *ch);

/* =========================================================================
 * Ring Buffer Operations
 * ========================================================================= */

/* Find empty slot in ring, returns slot index or -1 */
int ext_ipc_find_empty_slot(ext_ipc_ring_t *ring);

/* Find pending slot in ring, returns slot index or -1 */
int ext_ipc_find_pending_slot(ext_ipc_ring_t *ring);

/* Write message to slot and mark PENDING */
void ext_ipc_slot_write(ext_ipc_slot_t *slot, uint32_t msg_type,
                        const void *payload, uint32_t len);

/* Complete slot with response */
void ext_ipc_slot_complete(ext_ipc_slot_t *slot, int32_t result,
                           const void *data, uint32_t len);

/* Release slot (mark EMPTY) */
void ext_ipc_slot_release(ext_ipc_slot_t *slot);

/* Spin-wait for slot to become COMPLETE, returns false on timeout */
bool ext_ipc_slot_wait_complete(ext_ipc_slot_t *slot, int timeout_ms);

/* =========================================================================
 * Payload Structures
 * ========================================================================= */

typedef struct {
    char name[64];
} ext_ipc_cmd_register_t;

typedef struct {
    int f;
    int n;
    char name[64];
} ext_ipc_cmd_invoke_t;

typedef struct {
    int32_t result;
    char message[256];
} ext_ipc_cmd_response_t;

typedef struct {
    int32_t line;
    int32_t col;
} ext_ipc_point_t;

typedef struct {
    char text[512];
} ext_ipc_message_t;

typedef struct {
    char event_name[64];
    int priority;
} ext_ipc_event_sub_t;

typedef struct {
    char prompt[256];
} ext_ipc_prompt_req_t;

typedef struct {
    int cancelled;
    char response[1024];
} ext_ipc_prompt_resp_t;

typedef struct {
    char name[64];
    int priority;
} ext_ipc_modeline_req_t;

#endif /* UEMACS_EXT_IPC_H */

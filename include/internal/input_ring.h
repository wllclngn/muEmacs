/* input_ring.h - Lock-free SPSC ring buffer for terminal input
 *
 * Single-Producer (terminal read thread/path)
 * Single-Consumer (input parser/keystroke processor)
 *
 * Uses C11/C23 atomics for memory ordering.
 * Cache-line aligned to prevent false sharing.
 */

#ifndef INPUT_RING_H
#define INPUT_RING_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Ring buffer size - must be power of 2 for efficient masking */
#define INPUT_RING_SIZE 8192
#define INPUT_RING_MASK (INPUT_RING_SIZE - 1)

/* Cache line size for alignment (typical x86-64) */
#define CACHE_LINE_SIZE 64

typedef struct {
    /* Data buffer - cache-line aligned */
    _Alignas(CACHE_LINE_SIZE) uint8_t buf[INPUT_RING_SIZE];

    /* Producer writes here (tail), consumer reads here (head)
     * Separate cache lines to prevent false sharing */
    _Alignas(CACHE_LINE_SIZE) _Atomic size_t head;  /* Consumer position */
    _Alignas(CACHE_LINE_SIZE) _Atomic size_t tail;  /* Producer position */
} input_ring_t;

/* Global input ring buffer instance */
extern input_ring_t g_input_ring;

/* Initialize the ring buffer (call once at startup) */
static inline void input_ring_init(input_ring_t *ring) {
    memset(ring->buf, 0, INPUT_RING_SIZE);
    atomic_store_explicit(&ring->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0, memory_order_relaxed);
}

/* Returns number of bytes available to read */
static inline size_t input_ring_available(const input_ring_t *ring) {
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    return (tail - head) & INPUT_RING_MASK;
}

/* Returns number of bytes of free space for writing */
static inline size_t input_ring_free_space(const input_ring_t *ring) {
    return INPUT_RING_SIZE - 1 - input_ring_available(ring);
}

/* Check if ring is empty */
static inline bool input_ring_empty(const input_ring_t *ring) {
    return input_ring_available(ring) == 0;
}

/* Check if ring is full */
static inline bool input_ring_full(const input_ring_t *ring) {
    return input_ring_available(ring) == INPUT_RING_SIZE - 1;
}

/* Producer: Write a single byte. Returns false if full. */
static inline bool input_ring_push(input_ring_t *ring, uint8_t byte) {
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t next_tail = (tail + 1) & INPUT_RING_MASK;
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);

    if (next_tail == head) {
        return false;  /* Full */
    }

    ring->buf[tail] = byte;
    atomic_store_explicit(&ring->tail, next_tail, memory_order_release);
    return true;
}

/* Producer: Write multiple bytes. Returns number of bytes written. */
static inline size_t input_ring_push_bulk(input_ring_t *ring,
                                          const uint8_t *data,
                                          size_t len) {
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    size_t available_space = (head - tail - 1) & INPUT_RING_MASK;

    if (len > available_space) {
        len = available_space;
    }
    if (len == 0) {
        return 0;
    }

    /* Handle wrap-around: may need two memcpy calls */
    size_t first_chunk = INPUT_RING_SIZE - tail;
    if (first_chunk > len) {
        first_chunk = len;
    }

    memcpy(&ring->buf[tail], data, first_chunk);

    if (len > first_chunk) {
        /* Wrap around to beginning of buffer */
        memcpy(&ring->buf[0], data + first_chunk, len - first_chunk);
    }

    size_t new_tail = (tail + len) & INPUT_RING_MASK;
    atomic_store_explicit(&ring->tail, new_tail, memory_order_release);
    return len;
}

/* Consumer: Read a single byte. Returns -1 if empty. */
static inline int input_ring_pop(input_ring_t *ring) {
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    if (head == tail) {
        return -1;  /* Empty */
    }

    uint8_t byte = ring->buf[head];
    size_t next_head = (head + 1) & INPUT_RING_MASK;
    atomic_store_explicit(&ring->head, next_head, memory_order_release);
    return byte;
}

/* Consumer: Peek at next byte without removing. Returns -1 if empty. */
static inline int input_ring_peek(const input_ring_t *ring) {
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    if (head == tail) {
        return -1;  /* Empty */
    }

    return ring->buf[head];
}

/* Consumer: Peek at byte at offset from head. Returns -1 if not available. */
static inline int input_ring_peek_at(const input_ring_t *ring, size_t offset) {
    size_t avail = input_ring_available(ring);
    if (offset >= avail) {
        return -1;
    }

    size_t head = atomic_load_explicit(&ring->head, memory_order_acquire);
    return ring->buf[(head + offset) & INPUT_RING_MASK];
}

/* Consumer: Read multiple bytes. Returns number of bytes read. */
static inline size_t input_ring_pop_bulk(input_ring_t *ring,
                                         uint8_t *dest,
                                         size_t max_len) {
    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);
    size_t available = (tail - head) & INPUT_RING_MASK;

    if (max_len > available) {
        max_len = available;
    }
    if (max_len == 0) {
        return 0;
    }

    /* Handle wrap-around */
    size_t first_chunk = INPUT_RING_SIZE - head;
    if (first_chunk > max_len) {
        first_chunk = max_len;
    }

    memcpy(dest, &ring->buf[head], first_chunk);

    if (max_len > first_chunk) {
        memcpy(dest + first_chunk, &ring->buf[0], max_len - first_chunk);
    }

    size_t new_head = (head + max_len) & INPUT_RING_MASK;
    atomic_store_explicit(&ring->head, new_head, memory_order_release);
    return max_len;
}

/* Consumer: Skip n bytes without copying. Returns bytes actually skipped. */
static inline size_t input_ring_skip(input_ring_t *ring, size_t n) {
    size_t avail = input_ring_available(ring);
    if (n > avail) {
        n = avail;
    }

    size_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    size_t new_head = (head + n) & INPUT_RING_MASK;
    atomic_store_explicit(&ring->head, new_head, memory_order_release);
    return n;
}

/* Reset the ring buffer (not thread-safe - use only when idle) */
static inline void input_ring_reset(input_ring_t *ring) {
    atomic_store_explicit(&ring->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0, memory_order_relaxed);
}

#endif /* INPUT_RING_H */

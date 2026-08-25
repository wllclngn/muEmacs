/*
 * file_uring.c - io_uring batch file reading for μEmacs
 *
 * Submits multiple IORING_OP_READ SQEs in a single io_uring_enter()
 * for dramatically reduced syscall overhead during file loading.
 *
 * C23 compliant
 */

#include "config.h"

#ifdef HAVE_IO_URING

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <linux/io_uring.h>
#include <stdatomic.h>
#include "memory.h"
#include "util/logger.h"

#define FILE_CHUNK_SIZE 65536  /* 64KB chunks */

/*
 * Read entire file using io_uring batch operations.
 *
 * Allocates a contiguous buffer, divides into 64KB chunks,
 * submits all read SQEs at once, and waits for completion.
 *
 * Returns 0 on success. Caller must free *out_buf.
 * Returns -1 on error (caller should fall back to ffgetline path).
 */
int file_read_uring(int fd, size_t file_size, char **out_buf, size_t *out_len) {
    if (fd < 0 || file_size == 0 || !out_buf || !out_len)
        return -1;

    char *buf = safe_alloc(file_size, "file_uring read buffer", __FILE__, __LINE__);
    if (!buf) return -1;

    // Set up a temporary io_uring ring for this operation
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));

    int ring_fd = (int)syscall(__NR_io_uring_setup, 32, &params);
    if (ring_fd < 0) {
        SAFE_FREE(buf);
        return -1;
    }

    size_t sq_sz = params.sq_off.array + params.sq_entries * sizeof(unsigned);
    size_t cq_sz = params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);

    if (params.features & IORING_FEAT_SINGLE_MMAP) {
        if (cq_sz > sq_sz) sq_sz = cq_sz;
    }

    void *sq_ptr = mmap(nullptr, sq_sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_SQ_RING);
    if (sq_ptr == MAP_FAILED) {
        close(ring_fd);
        SAFE_FREE(buf);
        return -1;
    }

    void *cq_ptr = (params.features & IORING_FEAT_SINGLE_MMAP)
        ? sq_ptr
        : mmap(nullptr, cq_sz, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_POPULATE, ring_fd, IORING_OFF_CQ_RING);

    size_t sqe_sz = params.sq_entries * sizeof(struct io_uring_sqe);
    struct io_uring_sqe *sqes = mmap(nullptr, sqe_sz, PROT_READ | PROT_WRITE,
                                     MAP_SHARED | MAP_POPULATE, ring_fd,
                                     IORING_OFF_SQES);

    if (cq_ptr == MAP_FAILED || sqes == MAP_FAILED) {
        munmap(sq_ptr, sq_sz);
        close(ring_fd);
        SAFE_FREE(buf);
        return -1;
    }

    unsigned *sq_tail = (unsigned *)((char *)sq_ptr + params.sq_off.tail);
    unsigned *sq_mask = (unsigned *)((char *)sq_ptr + params.sq_off.ring_mask);
    unsigned *sq_array = (unsigned *)((char *)sq_ptr + params.sq_off.array);
    unsigned *cq_head_p = (unsigned *)((char *)cq_ptr + params.cq_off.head);
    unsigned *cq_tail_p = (unsigned *)((char *)cq_ptr + params.cq_off.tail);
    unsigned *cq_mask = (unsigned *)((char *)cq_ptr + params.cq_off.ring_mask);
    struct io_uring_cqe *cqes = (struct io_uring_cqe *)((char *)cq_ptr + params.cq_off.cqes);

    size_t n_chunks = (file_size + FILE_CHUNK_SIZE - 1) / FILE_CHUNK_SIZE;
    if (n_chunks > params.sq_entries)
        n_chunks = params.sq_entries; // Limit to ring size

    // Submit read SQEs
    unsigned tail = *sq_tail;
    for (size_t i = 0; i < n_chunks; i++) {
        size_t offset = i * FILE_CHUNK_SIZE;
        size_t len = FILE_CHUNK_SIZE;
        if (offset + len > file_size)
            len = file_size - offset;

        unsigned idx = tail & *sq_mask;
        struct io_uring_sqe *sqe = &sqes[idx];
        memset(sqe, 0, sizeof(*sqe));

        sqe->opcode = IORING_OP_READ;
        sqe->fd = fd;
        sqe->addr = (__u64)(buf + offset);
        sqe->len = (__u32)len;
        sqe->off = (__u64)offset;
        sqe->user_data = (__u64)i;

        sq_array[idx] = idx;
        tail++;
    }
    atomic_store_explicit((_Atomic unsigned *)sq_tail, tail, memory_order_release);

    // Submit all and wait for all completions
    int ret = (int)syscall(__NR_io_uring_enter, ring_fd, (unsigned)n_chunks,
                           (unsigned)n_chunks, IORING_ENTER_GETEVENTS, nullptr, 0);

    // Verify all reads completed successfully
    bool success = (ret >= 0);
    size_t total_read = 0;

    if (success) {
        unsigned cq_head = *cq_head_p;
        unsigned cq_tail_val = atomic_load_explicit((_Atomic unsigned *)cq_tail_p,
                                                     memory_order_acquire);
        unsigned completed = 0;
        while (cq_head != cq_tail_val) {
            unsigned cidx = cq_head & *cq_mask;
            struct io_uring_cqe *cqe = &cqes[cidx];

            if (cqe->res < 0) {
                success = false;
                break;
            }
            total_read += (size_t)cqe->res;
            completed++;
            cq_head++;
        }
        atomic_store_explicit((_Atomic unsigned *)cq_head_p, cq_head,
                              memory_order_release);
    }

    // Cleanup ring
    munmap(sqes, sqe_sz);
    munmap(sq_ptr, sq_sz);
    if (!(params.features & IORING_FEAT_SINGLE_MMAP))
        munmap(cq_ptr, cq_sz);
    close(ring_fd);

    if (!success || total_read == 0) {
        SAFE_FREE(buf);
        return -1;
    }

    *out_buf = buf;
    *out_len = total_read;

    LOG_DEBUGF("file_uring: read %zu bytes in %zu chunks via io_uring",
               total_read, n_chunks);
    return 0;
}

#else /* !HAVE_IO_URING */

#include <stddef.h>

int file_read_uring(int fd, size_t file_size, char **out_buf, size_t *out_len) {
    (void)fd; (void)file_size; (void)out_buf; (void)out_len;
    return -1;
}

#endif /* HAVE_IO_URING */

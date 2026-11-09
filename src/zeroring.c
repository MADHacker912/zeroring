#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#include "zeroring.h"

typedef struct {
    uint64_t req_id;
    zeroring_op_t op;
    struct timespec submit_time;
    void *user_ctx;
} internal_request_t;

static uint64_t timespec_diff_ns(const struct timespec *start, const struct timespec *end) {
    uint64_t s_ns = (uint64_t)start->tv_sec * 1000000000ULL + (uint64_t)start->tv_nsec;
    uint64_t e_ns = (uint64_t)end->tv_sec * 1000000000ULL + (uint64_t)end->tv_nsec;
    return (e_ns >= s_ns) ? (e_ns - s_ns) : 0;
}

int zeroring_init(zeroring_engine_t *engine, uint32_t queue_depth, bool direct_io) {
    if (!engine) return -EINVAL;
    if (queue_depth == 0) queue_depth = ZERORING_DEFAULT_QUEUE_DEPTH;

    memset(engine, 0, sizeof(*engine));
    engine->queue_depth = queue_depth;
    engine->direct_io = direct_io;

    int ret = io_uring_queue_init(queue_depth, &engine->ring, 0);
    if (ret < 0) {
        return ret;
    }

    engine->is_initialized = true;
    return 0;
}

int zeroring_open(const char *pathname, bool direct_io, bool create) {
    if (!pathname) return -EINVAL;

    int flags = O_RDWR;
    if (create) {
        flags |= O_CREAT;
    }
    if (direct_io) {
#ifdef O_DIRECT
        flags |= O_DIRECT;
#endif
    }

    int fd = open(pathname, flags, 0644);
    if (fd < 0) {
        return -errno;
    }
    return fd;
}

void *zeroring_alloc_buffer(size_t size) {
    if (size == 0) return NULL;
    /* Round up to block size */
    size_t aligned_size = (size + ZERORING_BLOCK_SIZE - 1) & ~(ZERORING_BLOCK_SIZE - 1);
    void *ptr = NULL;
    if (posix_memalign(&ptr, ZERORING_BLOCK_SIZE, aligned_size) != 0) {
        return NULL;
    }
    return ptr;
}

void zeroring_free_buffer(void *ptr) {
    free(ptr);
}

static int prep_io(zeroring_engine_t *engine, int fd, void *buf, size_t nbytes, uint64_t offset,
                   uint64_t req_id, void *user_ctx, zeroring_op_t op) {
    if (!engine || !engine->is_initialized || fd < 0 || !buf) {
        return -EINVAL;
    }

    if (engine->direct_io) {
        /* O_DIRECT enforces block alignment on buffer address and transfer size */
        if (((uintptr_t)buf % ZERORING_BLOCK_SIZE) != 0 || (nbytes % ZERORING_BLOCK_SIZE) != 0 || (offset % ZERORING_BLOCK_SIZE) != 0) {
            return -EINVAL;
        }
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(&engine->ring);
    if (!sqe) {
        return -EBUSY; /* Ring queue full */
    }

    internal_request_t *req = (internal_request_t *)malloc(sizeof(internal_request_t));
    if (!req) {
        return -ENOMEM;
    }
    req->req_id = req_id;
    req->op = op;
    req->user_ctx = user_ctx;
    clock_gettime(CLOCK_MONOTONIC, &req->submit_time);

    if (op == ZERORING_OP_WRITE) {
        io_uring_prep_write(sqe, fd, buf, nbytes, offset);
    } else {
        io_uring_prep_read(sqe, fd, buf, nbytes, offset);
    }

    io_uring_sqe_set_data(sqe, req);
    return 0;
}

int zeroring_prep_write(zeroring_engine_t *engine, int fd, const void *buf, size_t nbytes,
                        uint64_t offset, uint64_t req_id, void *user_ctx) {
    return prep_io(engine, fd, (void *)buf, nbytes, offset, req_id, user_ctx, ZERORING_OP_WRITE);
}

int zeroring_prep_read(zeroring_engine_t *engine, int fd, void *buf, size_t nbytes,
                       uint64_t offset, uint64_t req_id, void *user_ctx) {
    return prep_io(engine, fd, buf, nbytes, offset, req_id, user_ctx, ZERORING_OP_READ);
}

int zeroring_submit(zeroring_engine_t *engine) {
    if (!engine || !engine->is_initialized) return -EINVAL;
    return io_uring_submit(&engine->ring);
}

int zeroring_wait_completions(zeroring_engine_t *engine, uint32_t wait_nr,
                              zeroring_completion_cb cb, void *cb_arg) {
    if (!engine || !engine->is_initialized) return -EINVAL;

    struct io_uring_cqe *cqe = NULL;
    int ret = 0;

    if (wait_nr > 0) {
        ret = io_uring_wait_cqe_nr(&engine->ring, &cqe, wait_nr);
        if (ret < 0) return ret;
    }

    unsigned head;
    unsigned count = 0;
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    io_uring_for_each_cqe(&engine->ring, head, cqe) {
        count++;
        internal_request_t *req = (internal_request_t *)io_uring_cqe_get_data(cqe);
        if (req) {
            if (cb) {
                zeroring_completion_t comp;
                comp.req_id = req->req_id;
                comp.op = req->op;
                comp.res = cqe->res;
                comp.latency_ns = timespec_diff_ns(&req->submit_time, &end_time);
                comp.user_ctx = req->user_ctx;
                cb(&comp, cb_arg);
            }
            free(req);
        }
    }

    io_uring_cq_advance(&engine->ring, count);
    return (int)count;
}

void zeroring_destroy(zeroring_engine_t *engine) {
    if (engine && engine->is_initialized) {
        io_uring_queue_exit(&engine->ring);
        engine->is_initialized = false;
    }
}

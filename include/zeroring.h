#ifndef ZERORING_H
#define ZERORING_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <liburing.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZERORING_BLOCK_SIZE 4096
#define ZERORING_DEFAULT_QUEUE_DEPTH 64

typedef enum {
    ZERORING_OP_READ = 0,
    ZERORING_OP_WRITE = 1
} zeroring_op_t;

typedef struct {
    uint64_t req_id;
    zeroring_op_t op;
    int res;            /* bytes transferred or negative errno */
    uint64_t latency_ns;
    void *user_ctx;
} zeroring_completion_t;

typedef void (*zeroring_completion_cb)(const zeroring_completion_t *comp, void *arg);

typedef struct {
    struct io_uring ring;
    uint32_t queue_depth;
    bool direct_io;
    bool is_initialized;
} zeroring_engine_t;

/**
 * Initialize the zero-copy io_uring storage engine.
 * @param engine Pointer to engine struct.
 * @param queue_depth Ring buffer queue depth (e.g., 64, 128, 512).
 * @param direct_io If true, engine enforces O_DIRECT page cache bypass.
 * @return 0 on success, negative error code on failure.
 */
int zeroring_init(zeroring_engine_t *engine, uint32_t queue_depth, bool direct_io);

/**
 * Open a file with direct I/O alignment flags.
 */
int zeroring_open(const char *pathname, bool direct_io, bool create);

/**
 * Allocate a 4096-byte memory-aligned buffer required for O_DIRECT DMA.
 */
void *zeroring_alloc_buffer(size_t size);

/**
 * Free memory-aligned buffer.
 */
void zeroring_free_buffer(void *ptr);

/**
 * Submit an asynchronous write request.
 */
int zeroring_prep_write(zeroring_engine_t *engine, int fd, const void *buf, size_t nbytes, uint64_t offset, uint64_t req_id, void *user_ctx);

/**
 * Submit an asynchronous read request.
 */
int zeroring_prep_read(zeroring_engine_t *engine, int fd, void *buf, size_t nbytes, uint64_t offset, uint64_t req_id, void *user_ctx);

/**
 * Flush prepared requests from SQ to the kernel / device.
 * @return Number of submitted requests or negative error.
 */
int zeroring_submit(zeroring_engine_t *engine);

/**
 * Wait for at least `wait_nr` completions and process them via callback.
 * @return Number of processed completions or negative error.
 */
int zeroring_wait_completions(zeroring_engine_t *engine, uint32_t wait_nr, zeroring_completion_cb cb, void *cb_arg);

/**
 * Destroy the engine and release io_uring ring resources.
 */
void zeroring_destroy(zeroring_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif /* ZERORING_H */

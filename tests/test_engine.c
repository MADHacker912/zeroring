#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "zeroring.h"

#define TEST_FILE "/tmp/zeroring_test.bin"
#define TEST_BLOCKS 8
#define TOTAL_BYTES (TEST_BLOCKS * ZERORING_BLOCK_SIZE)

static int g_completions_received = 0;
static int g_bytes_transferred = 0;

static void test_cb(const zeroring_completion_t *comp, void *arg) {
    (void)arg;
    g_completions_received++;
    if (comp->res > 0) {
        g_bytes_transferred += comp->res;
    }
    assert(comp->res >= 0 && "I/O operation returned error");
    assert(comp->latency_ns > 0 && "Latency should be non-zero");
}

void test_aligned_alloc(void) {
    printf("[TEST] Testing memory-aligned buffer allocation...\n");
    void *buf = zeroring_alloc_buffer(ZERORING_BLOCK_SIZE);
    assert(buf != NULL);
    assert(((uintptr_t)buf % ZERORING_BLOCK_SIZE) == 0 && "Buffer not 4096-aligned");
    zeroring_free_buffer(buf);
    printf("  -> PASS: Memory alignment verified.\n");
}

void test_direct_io_alignment_enforcement(void) {
    printf("[TEST] Testing O_DIRECT alignment rejection...\n");
    zeroring_engine_t engine;
    assert(zeroring_init(&engine, 16, true) == 0);

    char unaligned_buf[100];
    int fd = zeroring_open(TEST_FILE, true, true);
    assert(fd >= 0);

    /* Unaligned address and unaligned size should be rejected with -EINVAL */
    int ret = zeroring_prep_write(&engine, fd, unaligned_buf, sizeof(unaligned_buf), 0, 1, NULL);
    assert(ret == -EINVAL && "Engine failed to reject unaligned Direct I/O buffer");

    close(fd);
    zeroring_destroy(&engine);
    printf("  -> PASS: Direct I/O validation guards functioning.\n");
}

void test_roundtrip_integrity(void) {
    printf("[TEST] Testing zero-copy async write and read roundtrip integrity...\n");
    zeroring_engine_t engine;
    assert(zeroring_init(&engine, 32, true) == 0);

    int fd = zeroring_open(TEST_FILE, true, true);
    assert(fd >= 0);

    /* Prepare test data */
    char *write_buf = (char *)zeroring_alloc_buffer(TOTAL_BYTES);
    char *read_buf = (char *)zeroring_alloc_buffer(TOTAL_BYTES);
    assert(write_buf && read_buf);

    for (size_t i = 0; i < TOTAL_BYTES; i++) {
        write_buf[i] = (char)((i % 251) + 1);
    }
    memset(read_buf, 0, TOTAL_BYTES);

    /* 1. Asynchronous Write */
    g_completions_received = 0;
    g_bytes_transferred = 0;
    assert(zeroring_prep_write(&engine, fd, write_buf, TOTAL_BYTES, 0, 100, NULL) == 0);
    assert(zeroring_submit(&engine) == 1);
    assert(zeroring_wait_completions(&engine, 1, test_cb, NULL) == 1);
    assert(g_completions_received == 1);
    assert(g_bytes_transferred == TOTAL_BYTES);

    /* 2. Asynchronous Read */
    g_completions_received = 0;
    g_bytes_transferred = 0;
    assert(zeroring_prep_read(&engine, fd, read_buf, TOTAL_BYTES, 0, 101, NULL) == 0);
    assert(zeroring_submit(&engine) == 1);
    assert(zeroring_wait_completions(&engine, 1, test_cb, NULL) == 1);
    assert(g_completions_received == 1);
    assert(g_bytes_transferred == TOTAL_BYTES);

    /* 3. Byte-level verification */
    assert(memcmp(write_buf, read_buf, TOTAL_BYTES) == 0 && "Data corruption detected!");

    zeroring_free_buffer(write_buf);
    zeroring_free_buffer(read_buf);
    close(fd);
    zeroring_destroy(&engine);
    unlink(TEST_FILE);
    printf("  -> PASS: 100%% byte-level integrity confirmed over zero-copy O_DIRECT path.\n");
}

void test_batch_pipeline(void) {
    printf("[TEST] Testing batched concurrent queue pipeline (16 operations)...\n");
    zeroring_engine_t engine;
    assert(zeroring_init(&engine, 64, true) == 0);

    int fd = zeroring_open(TEST_FILE, true, true);
    assert(fd >= 0);

    const int BATCH_COUNT = 16;
    char *buffers[BATCH_COUNT];
    for (int i = 0; i < BATCH_COUNT; i++) {
        buffers[i] = (char *)zeroring_alloc_buffer(ZERORING_BLOCK_SIZE);
        memset(buffers[i], 'A' + i, ZERORING_BLOCK_SIZE);
        int ret = zeroring_prep_write(&engine, fd, buffers[i], ZERORING_BLOCK_SIZE, i * ZERORING_BLOCK_SIZE, i, NULL);
        assert(ret == 0);
    }

    int submitted = zeroring_submit(&engine);
    assert(submitted == BATCH_COUNT);

    g_completions_received = 0;
    g_bytes_transferred = 0;
    int reaped = 0;
    while (reaped < BATCH_COUNT) {
        int n = zeroring_wait_completions(&engine, 1, test_cb, NULL);
        if (n > 0) reaped += n;
    }

    assert(reaped == BATCH_COUNT);
    assert(g_completions_received == BATCH_COUNT);
    assert(g_bytes_transferred == BATCH_COUNT * ZERORING_BLOCK_SIZE);

    for (int i = 0; i < BATCH_COUNT; i++) {
        zeroring_free_buffer(buffers[i]);
    }
    close(fd);
    zeroring_destroy(&engine);
    unlink(TEST_FILE);
    printf("  -> PASS: Batched concurrent pipeline verified without stalling.\n");
}

int main(void) {
    printf("=========================================\n");
    printf("   RUNNING ZERORING TEST SUITE\n");
    printf("=========================================\n");

    test_aligned_alloc();
    test_direct_io_alignment_enforcement();
    test_roundtrip_integrity();
    test_batch_pipeline();

    printf("\n>>> ALL TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}

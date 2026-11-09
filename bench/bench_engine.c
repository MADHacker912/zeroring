#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "zeroring.h"

#define BENCH_FILE_POSIX "/tmp/zeroring_bench_posix.bin"
#define BENCH_FILE_URING "/tmp/zeroring_bench_uring.bin"
#define ITERATIONS 5000
#define BENCH_BLOCK_SIZE ZERORING_BLOCK_SIZE

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void nop_cb(const zeroring_completion_t *comp, void *arg) {
    (void)comp;
    (void)arg;
}

void bench_posix(void *buf) {
    int fd = open(BENCH_FILE_POSIX, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open posix");
        return;
    }

    double start = get_time_sec();
    for (int i = 0; i < ITERATIONS; i++) {
        ssize_t written = pwrite(fd, buf, BENCH_BLOCK_SIZE, (off_t)i * BENCH_BLOCK_SIZE);
        if (written < 0) {
            perror("pwrite");
            break;
        }
    }
    double elapsed = get_time_sec() - start;

    double mb = (double)(ITERATIONS * BENCH_BLOCK_SIZE) / (1024.0 * 1024.0);
    double throughput = mb / elapsed;
    double iops = (double)ITERATIONS / elapsed;
    double avg_lat_us = (elapsed / (double)ITERATIONS) * 1e6;

    printf("[1] POSIX pwrite (Standard Kernel Page Cache):\n");
    printf("    Throughput:  %.2f MB/s\n", throughput);
    printf("    IOPS:        %.0f ops/sec\n", iops);
    printf("    Avg Latency: %.2f us\n", avg_lat_us);

    close(fd);
    unlink(BENCH_FILE_POSIX);
}

void bench_zeroring(void *buf) {
    zeroring_engine_t engine;
    if (zeroring_init(&engine, 128, true) != 0) {
        perror("zeroring_init");
        return;
    }

    int fd = zeroring_open(BENCH_FILE_URING, true, true);
    if (fd < 0) {
        perror("zeroring_open");
        return;
    }

    double start = get_time_sec();
    int batch_size = 32;
    for (int i = 0; i < ITERATIONS; i += batch_size) {
        int cur_batch = (i + batch_size <= ITERATIONS) ? batch_size : (ITERATIONS - i);
        for (int b = 0; b < cur_batch; b++) {
            uint64_t offset = (uint64_t)(i + b) * BENCH_BLOCK_SIZE;
            zeroring_prep_write(&engine, fd, buf, BENCH_BLOCK_SIZE, offset, i + b, NULL);
        }
        zeroring_submit(&engine);
        int reaped = 0;
        while (reaped < cur_batch) {
            int n = zeroring_wait_completions(&engine, 1, nop_cb, NULL);
            if (n > 0) reaped += n;
        }
    }
    double elapsed = get_time_sec() - start;

    double mb = (double)(ITERATIONS * BENCH_BLOCK_SIZE) / (1024.0 * 1024.0);
    double throughput = mb / elapsed;
    double iops = (double)ITERATIONS / elapsed;
    double avg_lat_us = (elapsed / (double)ITERATIONS) * 1e6;

    printf("[2] ZeroRing (io_uring + O_DIRECT Page Cache Bypass):\n");
    printf("    Throughput:  %.2f MB/s\n", throughput);
    printf("    IOPS:        %.0f ops/sec\n", iops);
    printf("    Avg Latency: %.2f us\n", avg_lat_us);

    close(fd);
    zeroring_destroy(&engine);
    unlink(BENCH_FILE_URING);
}

int main(void) {
    printf("====================================================\n");
    printf("  ZERORING BENCHMARK: %d I/O Operations (4KB Blocks)\n", ITERATIONS);
    printf("====================================================\n");

    void *buf = zeroring_alloc_buffer(BENCH_BLOCK_SIZE);
    memset(buf, 0xAB, BENCH_BLOCK_SIZE);

    bench_posix(buf);
    printf("----------------------------------------------------\n");
    bench_zeroring(buf);
    printf("====================================================\n");

    zeroring_free_buffer(buf);
    return 0;
}

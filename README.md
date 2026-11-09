# ZeroRing (⚡)

> **High-Performance Zero-Copy Kernel-Bypass Direct I/O Storage Engine in C using Linux `io_uring` and `O_DIRECT`**

`ZeroRing` is an asynchronous, zero-copy storage engine engineered to bypass the Linux kernel page cache and minimize context switches for ultra-low latency flash storage operations.

---

## 🚀 Key Architectural Highlights

- **Linux `io_uring` Ring Buffers**: Uses shared memory Submission Queues (SQ) and Completion Queues (CQ) between user space and kernel space, eliminating synchronous syscall overhead.
- **Kernel Page-Cache Bypass (`O_DIRECT`)**: Initiates direct DMA (Direct Memory Access) transfers between flash storage and user-space memory buffers, eliminating double-buffering and cache thrashing.
- **Strict 4KB Memory Alignment**: Provides memory-aligned buffer allocation (`posix_memalign`) to satisfy NVMe/SSD physical sector boundaries.
- **Batched Asynchronous Execution**: Submit batches of up to 64/128/512 operations in a single atomic kernel entry.
- **Nanosecond Latency Tracking**: Integrated monotonic clock tracking on every completed request.

---

## 📐 Architecture Overview

```
 [ User Application ]
         │
         ▼
 [ ZeroRing Engine ]
         │
         ├── [ posix_memalign (4KB Aligned Buffer) ] ── (Zero Copy DMA Buffer)
         │
         ├── [ Submission Queue (SQ) ]  ◄── (No Context Switch)
         │           │
         │           ▼
         │   [ Linux io_uring ]
         │           │
         │           ▼  (O_DIRECT DMA Transfer - Kernel Page Cache Bypassed)
         │     [ NVMe / SSD ]
         │           │
         └── [ Completion Queue (CQ) ]  ◄── (Event Reaping Callback)
```

---

## 🛠️ Requirements & Build Instructions

### Prerequisites
- Linux Kernel ≥ 5.1
- GCC or Clang
- `liburing-dev`

```bash
# Ubuntu / Debian
sudo apt update && sudo apt install -y liburing-dev build-essential
```

### Building the Project
```bash
# Build static library, unit test binary, and benchmark suite
make
```

### Running the Test Suite
```bash
make test
```

### Running Performance Benchmarks
```bash
make bench
```

---

## 🧪 Test Suite Coverage

The automated test suite (`tests/test_engine.c`) validates:
- `test_aligned_alloc`: Memory boundary verification for 4096-byte page alignment.
- `test_direct_io_alignment_enforcement`: Direct I/O rejection of misaligned memory or size boundaries.
- `test_roundtrip_integrity`: End-to-end byte-level consistency check comparing async write and read buffers.
- `test_batch_pipeline`: Concurrent queue saturation and non-blocking completion harvesting.

---

## 📊 Benchmark Results

Benchmarked on Ubuntu Linux x86_64:

```text
====================================================
  ZERORING BENCHMARK: 5000 I/O Operations (4KB Blocks)
====================================================
[1] POSIX pwrite (Standard Kernel Page Cache):
    Throughput:  546.47 MB/s
    IOPS:        139897 ops/sec
    Avg Latency: 7.15 us
----------------------------------------------------
[2] ZeroRing (io_uring + O_DIRECT Page Cache Bypass):
    Throughput:  403.81 MB/s
    IOPS:        103374 ops/sec
    Avg Latency: 9.67 us
====================================================
```
*Note: POSIX write hits RAM dirty-pages without disk persistence; ZeroRing enforces real storage controller transfer without copying data into kernel memory.*

---

## 📄 License
MIT License.

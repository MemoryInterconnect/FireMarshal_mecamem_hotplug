# OmniXtend Character Device Driver

A simple Linux character device driver for RISC-V that provides direct user-space access to OmniXtend remote memory via DMA controller.

## Overview

This driver creates a character device `/dev/omnichar` that allows applications to:
- Read from OmniXtend remote memory using DMA
- Write to OmniXtend remote memory using DMA
- Seek to arbitrary offsets
- Query device size and statistics via ioctl

## Features

- **Direct Memory Access**: Byte-level read/write access to 512MB OmniXtend remote memory
- **DMA Transfers**: Automatic chunking of large transfers into 1MB DMA operations
- **Cache Coherency**: Proper cache flushing for RISC-V custom cache instructions
- **Statistics Tracking**: DMA operation counters accessible via ioctl
- **Simple API**: Standard file operations (open, read, write, lseek, close)

## Hardware Configuration

- **DMA Controller**: `0x9000000` (32 bytes)
- **PLIC**: `0xC000000` (64MB)
- **OmniXtend Remote Memory**: `0x200000000` (512MB)
- **DMA IRQ**: 1
- **Hart ID**: 0

## Building

### Prerequisites

- RISC-V cross-compilation toolchain
- Linux kernel 5.7 source tree
- Python 3 (for vermagic patching)

### Build Command

```bash
export PATH="/path/to/riscv-tools/bin:$PATH"
make
```

### Build Outputs

- `omni_chardev.ko` - Kernel module (481 KB)
- Module has clean vermagic (no modversions)

## Installation

### Load Module

```bash
insmod omni_chardev.ko
```

### Optional: Custom Size

```bash
insmod omni_chardev.ko omni_size_mb=1024
```

### Verify

```bash
ls -l /dev/omnichar
dmesg | tail
```

Expected output:
```
crw------- 1 root root 248, 0 Oct 29 07:36 /dev/omnichar
```

### Unload Module

```bash
rmmod omni_chardev
```

## Usage

### Basic Read/Write

```c
#include <fcntl.h>
#include <unistd.h>

int fd = open("/dev/omnichar", O_RDWR);

// Write data
char data[] = "Hello OmniXtend";
write(fd, data, sizeof(data));

// Seek back
lseek(fd, 0, SEEK_SET);

// Read data
char buf[256];
read(fd, buf, sizeof(data));

close(fd);
```

### ioctl Commands

#### Get Device Size

```c
#define OMNI_IOC_GET_SIZE _IOR('O', 1, unsigned long)

unsigned long size;
ioctl(fd, OMNI_IOC_GET_SIZE, &size);
printf("Device size: %lu bytes\n", size);
```

#### Get Statistics

```c
#define OMNI_IOC_GET_STATS _IOR('O', 2, struct omni_stats_ioctl)

struct omni_stats_ioctl {
    uint64_t dma_reads;
    uint64_t dma_writes;
    uint64_t dma_errors;
    uint64_t dma_timeouts;
    uint64_t irq_count;
};

struct omni_stats_ioctl stats;
ioctl(fd, OMNI_IOC_GET_STATS, &stats);
printf("DMA reads: %llu\n", stats.dma_reads);
```

#### Reset Statistics

```c
#define OMNI_IOC_RESET_STATS _IO('O', 3)

ioctl(fd, OMNI_IOC_RESET_STATS, 0);
```

## Testing

### Build Test Program

```bash
riscv64-unknown-linux-gnu-gcc -o test_omnichar test_omnichar.c
```

### Run Tests

```bash
./test_omnichar
```

Tests performed:
1. Open/close device
2. Query device size
3. Simple write and read back
4. Seek to offset and verify
5. Large transfer (2MB) to test DMA chunking
6. Statistics verification

## Implementation Details

### DMA Transfer Flow

1. **Write Operation**:
   - Copy data from user space to kernel DMA buffer
   - Flush source cache (DMA buffer)
   - Setup DMA: DMA buffer → OmniXtend memory
   - Poll for DMA completion
   - Flush destination cache (OmniXtend memory)

2. **Read Operation**:
   - Flush source cache (OmniXtend memory)
   - Setup DMA: OmniXtend memory → DMA buffer
   - Poll for DMA completion
   - Flush destination cache (DMA buffer)
   - Copy data to user space

### DMA Chunking

Large transfers are automatically split into 1MB chunks to match the DMA buffer size. The driver handles this transparently.

### Concurrency

The driver allows only one process to open the device at a time (enforced by `dev_mutex`). DMA operations are protected by `dma_lock`.

### Cache Coherency

Uses RISC-V custom cache flush instruction (`.word 0xfc050073`) to maintain coherency between:
- CPU caches
- DMA controller
- OmniXtend remote memory

## Performance

- **DMA Buffer Size**: 1 MB
- **Max Single Transfer**: Limited by available memory
- **Overhead**: ~10 µs per DMA poll iteration
- **Timeout**: 5 seconds per DMA operation

## Troubleshooting

### Device Not Created

Check kernel log:
```bash
dmesg | grep omni
```

### Permission Denied

```bash
sudo chmod 666 /dev/omnichar
```

### DMA Timeout

Increase timeout in header:
```c
#define DMA_TIMEOUT_MS 10000  // 10 seconds
```

### Module Load Fails

Check vermagic compatibility:
```bash
modinfo omni_chardev.ko | grep vermagic
uname -r
```

## Module Parameters

- `omni_size_mb` (default: 512): OmniXtend memory size in MB
  ```bash
  insmod omni_chardev.ko omni_size_mb=1024
  ```

## Differences from Block Device Driver

| Feature | Character Device | Block Device |
|---------|------------------|--------------|
| Device Node | `/dev/omnichar` | `/dev/omniblk` |
| Access Granularity | Byte-level | 512-byte sectors |
| API | read/write/lseek | bio/request queue |
| Use Case | Direct memory access | Filesystem storage |
| Complexity | Simpler | More complex (blk-mq) |
| Performance | Good for sequential | Optimized for random I/O |

## Files

- `omni_chardev.h` - Header with definitions and structures
- `omni_chardev.c` - Main driver implementation (600 lines)
- `Makefile` - Build system with vermagic patching
- `test_omnichar.c` - Test program
- `README.md` - This file

## License

GPL v2

## References

- [SPEC.md](SPEC.md) - Hardware specification
- [TODO_chardev.md](TODO_chardev.md) - Implementation checklist
- Linux Device Drivers, 3rd Edition, Chapter 3: Character Drivers
- RISC-V PLIC specification

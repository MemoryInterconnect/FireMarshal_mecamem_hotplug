# Quick Start Guide: OmniXtend Character Device Driver

## Installation on Target System

### 1. Load the Module

```bash
# Copy module to target system (if needed)
# Module is already in overlay: /lib/modules/omni_chardev.ko

# Load module
insmod /lib/modules/omni_chardev.ko

# Verify device created
ls -l /dev/omnichar
# Expected: crw------- 1 root root 248, 0 ...

# Check kernel messages
dmesg | tail -10
```

Expected output:
```
OmniXtend Character Device Driver v0.1.0
Mapped DMA @ 0x9000000, PLIC @ 0xc000000, OmniXtend @ 0x200000000
Allocated DMA buffer: 1024 KB @ phys 0x...
PLIC initialized for DMA IRQ 1
Device registered: major=248, size=512 MB
DMA buffer: 1024 KB
```

### 2. Set Permissions (if needed)

```bash
chmod 666 /dev/omnichar
```

### 3. Test Basic Operations

#### Simple Write and Read

```bash
# Write test data
echo "Hello OmniXtend" > /dev/omnichar

# Read back (first 16 bytes)
dd if=/dev/omnichar bs=1 count=16
# Output: Hello OmniXtend
```

#### Using dd for Larger Transfers

```bash
# Write 10MB of zeros
dd if=/dev/zero of=/dev/omnichar bs=1M count=10

# Write 10MB of random data
dd if=/dev/urandom of=/dev/omnichar bs=1M count=10

# Read back 10MB
dd if=/dev/omnichar of=/tmp/test.bin bs=1M count=10
```

#### Test with seek

```bash
# Write at specific offset (4096 bytes)
echo "Data at offset 4096" | dd of=/dev/omnichar bs=1 seek=4096

# Read from that offset
dd if=/dev/omnichar bs=1 skip=4096 count=19
```

## Programming Examples

### Example 1: Simple Read/Write

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = open("/dev/omnichar", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Write
    char *msg = "Hello from app!";
    write(fd, msg, strlen(msg) + 1);

    // Seek back
    lseek(fd, 0, SEEK_SET);

    // Read
    char buf[256];
    read(fd, buf, 256);
    printf("Read: %s\n", buf);

    close(fd);
    return 0;
}
```

### Example 2: Using ioctl

```c
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>

#define OMNI_IOC_GET_SIZE   _IOR('O', 1, unsigned long)
#define OMNI_IOC_GET_STATS  _IOR('O', 2, struct omni_stats_ioctl)

struct omni_stats_ioctl {
    uint64_t dma_reads;
    uint64_t dma_writes;
    uint64_t dma_errors;
    uint64_t dma_timeouts;
    uint64_t irq_count;
};

int main() {
    int fd = open("/dev/omnichar", O_RDWR);

    // Get size
    unsigned long size;
    ioctl(fd, OMNI_IOC_GET_SIZE, &size);
    printf("Device size: %lu MB\n", size / (1024*1024));

    // Get statistics
    struct omni_stats_ioctl stats;
    ioctl(fd, OMNI_IOC_GET_STATS, &stats);
    printf("DMA reads:  %llu\n", stats.dma_reads);
    printf("DMA writes: %llu\n", stats.dma_writes);

    close(fd);
    return 0;
}
```

### Example 3: Large File Copy

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define CHUNK_SIZE (1024 * 1024)  // 1MB

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    int in_fd = open(argv[1], O_RDONLY);
    int out_fd = open("/dev/omnichar", O_WRONLY);

    char *buf = malloc(CHUNK_SIZE);
    ssize_t n;

    while ((n = read(in_fd, buf, CHUNK_SIZE)) > 0) {
        write(out_fd, buf, n);
    }

    free(buf);
    close(in_fd);
    close(out_fd);

    printf("File copied to OmniXtend memory\n");
    return 0;
}
```

## Common Use Cases

### 1. Memory Testing

Test OmniXtend memory integrity:

```bash
# Write pattern
dd if=/dev/urandom of=/tmp/pattern.bin bs=1M count=100

# Copy to OmniXtend
dd if=/tmp/pattern.bin of=/dev/omnichar bs=1M

# Read back
dd if=/dev/omnichar of=/tmp/readback.bin bs=1M count=100

# Compare
cmp /tmp/pattern.bin /tmp/readback.bin && echo "Memory test PASSED"
```

### 2. Persistent Storage

Use as raw storage for custom applications:

```c
// Store configuration
struct config {
    int version;
    char name[256];
    // ...
};

int fd = open("/dev/omnichar", O_RDWR);
struct config cfg = { .version = 1, .name = "myapp" };

// Write config at offset 0
lseek(fd, 0, SEEK_SET);
write(fd, &cfg, sizeof(cfg));

// Read back later
lseek(fd, 0, SEEK_SET);
read(fd, &cfg, sizeof(cfg));
```

### 3. Data Buffer

Use as shared memory buffer:

```bash
# Process A: Write data
echo "Shared data" > /dev/omnichar

# Process B: Read data (after ensuring sync)
cat /dev/omnichar
```

### 4. Performance Testing

Measure DMA throughput:

```bash
# Write test
time dd if=/dev/zero of=/dev/omnichar bs=1M count=100
# Calculate: 100 MB / time = throughput

# Read test
time dd if=/dev/omnichar of=/dev/null bs=1M count=100
```

## Debugging

### Check Module Status

```bash
# List loaded modules
lsmod | grep omni

# Module information
modinfo /lib/modules/omni_chardev.ko

# Kernel messages
dmesg | grep -i omni
```

### Monitor Statistics

Create a monitoring script:

```bash
#!/bin/bash
# monitor_stats.sh

while true; do
    echo "=== Statistics at $(date) ==="
    # Use your own ioctl wrapper or test program
    ./test_omnichar | grep -A5 "Statistics"
    sleep 5
done
```

### Common Issues

#### 1. Device Not Found

```bash
# Check if module loaded
lsmod | grep omni_chardev

# Check device node
ls -l /dev/omnichar

# Check kernel log
dmesg | tail
```

#### 2. Permission Denied

```bash
# Fix permissions
sudo chmod 666 /dev/omnichar

# Or run as root
sudo ./test_omnichar
```

#### 3. DMA Timeout

Check if hardware is properly initialized:
```bash
# Kernel messages should show PLIC init
dmesg | grep PLIC

# Check for timeout errors
dmesg | grep -i timeout
```

## Unloading

```bash
# Remove module
rmmod omni_chardev

# Verify removed
lsmod | grep omni
ls /dev/omnichar  # Should not exist
```

## Performance Tips

1. **Use large transfers**: Transfers >= 1MB are more efficient (match DMA buffer size)
2. **Align to cache lines**: For best performance, align offsets to 64-byte boundaries
3. **Sequential access**: Sequential read/write is faster than random access
4. **Minimize seeks**: Reduce lseek calls when possible

## Comparison with Block Device

| Operation | Character Device | Block Device |
|-----------|------------------|--------------|
| Random byte write | Efficient | Less efficient (512B minimum) |
| Sequential I/O | Good | Better (optimized) |
| File system | Not supported | Supported (mkfs, mount) |
| Direct memory access | Yes | No |
| Seeking | Arbitrary byte | 512-byte sectors |

## Integration with Applications

### Using with mmap (future enhancement)

Currently not supported, but planned for future versions.

### Using with async I/O

Standard async I/O can be used with proper error handling.

## Additional Resources

- `README.md` - Detailed documentation
- `test_omnichar.c` - Complete test program with examples
- `SPEC.md` - Hardware specification
- Kernel logs: `dmesg`
- Module info: `modinfo omni_chardev.ko`

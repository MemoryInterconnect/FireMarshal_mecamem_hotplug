# OmniXtend Block Device Driver Specification

## Overview

This specification describes a Linux block device driver for RISC-V that uses OmniXtend remote memory as backing storage, accessed via a DMA controller with interrupt support through the Linux IRQ subsystem.

## Target Platform

- **Architecture**: RISC-V 64-bit
- **Linux Kernel**: 6.16.x (reference: `../boards/default/linux/`)
- **Hardware**: DMA controller with PLIC interrupt support

## Hardware Resources

### DMA Controller
- **Base Address**: `0x9000000`
- **Register Layout**:
  | Offset | Name | Description |
  |--------|------|-------------|
  | `0x00` | DMA_SRC_ADDR_LO | Source address bits [31:0] |
  | `0x04` | DMA_SRC_ADDR_HI | Source address bits [63:32] |
  | `0x08` | DMA_DST_ADDR_LO | Destination address bits [31:0] |
  | `0x0C` | DMA_DST_ADDR_HI | Destination address bits [63:32] |
  | `0x10` | DMA_LENGTH_LO | Transfer length bits [31:0] |
  | `0x14` | DMA_LENGTH_HI | Transfer length bits [63:32] |
  | `0x18` | DMA_CONTROL | Control register (bit 0: start transfer) |
  | `0x1C` | DMA_STATUS | Status register (bit 0: transfer complete) |

### OmniXtend Remote Memory
- **Base Address**: `0x200000000`
- **Size**: 512 MB (default, configurable via module parameter)
- **Access**: Via DMA controller or direct CPU access

### Interrupt Configuration
- **DMA IRQ Number**: 1
- **Interrupt Controller**: PLIC (managed by kernel's `irq-sifive-plic` driver)

**Note**: The device driver does NOT access PLIC registers directly. All interrupt management is handled through the Linux IRQ subsystem (`request_irq()`, `free_irq()`).

## Driver Architecture

### Module Type
Standard Linux block device driver using `blk-mq` (multi-queue block layer)

### Device Characteristics
- **Device Name**: `/dev/omniblk`
- **Block Size**: 512 bytes (standard sector size)
- **Queue Depth**: 1 (single hardware queue)
- **DMA Alignment**: 64 bytes (cache line size)

### Memory Management

#### Bounce Buffers
Since DMA requires physically contiguous memory:
- Allocate bounce buffer using `kmalloc()` with `GFP_KERNEL | GFP_DMA`
- Size: 1 MB (configurable)
- Single pre-allocated buffer to reduce allocation overhead

#### Cache Coherency
The RISC-V platform requires explicit cache flushing:

```c
/* Custom cache flush instruction: CFLUSH_D_L1 */
static inline void cflush_d_l1(uint64_t addr) {
    register uint64_t a0 asm("a0") = addr;
    asm volatile (".word 0xfc050073" : : "r"(a0) : "memory");
}

/* Flush cache range with memory barriers */
void flush_dcache_range(uint64_t start_addr, uint64_t length) {
    uint64_t cache_line_size = 64;
    uint64_t end_addr = start_addr + length;

    asm volatile("fence rw, rw" ::: "memory");
    for (uint64_t addr = start_addr; addr < end_addr; addr += cache_line_size) {
        cflush_d_l1(addr);
    }
    asm volatile("fence rw, rw" ::: "memory");
}
```

Cache flush requirements:
- **Before DMA write** (local → OmniXtend): Flush source (DMA buffer)
- **After DMA write** (local → OmniXtend): Flush destination (OmniXtend memory)
- **Before DMA read** (OmniXtend → local): Flush source (OmniXtend memory)
- **After DMA read** (OmniXtend → local): Flush destination (DMA buffer)

### DMA Transfer Flow

#### Setup DMA Transfer
```c
void dma_setup_transfer(void __iomem *dma_base, u64 src, u64 dst, u32 len) {
    iowrite32((u32)(src & 0xFFFFFFFF), dma_base + DMA_SRC_ADDR_LO);
    iowrite32((u32)(src >> 32), dma_base + DMA_SRC_ADDR_HI);
    iowrite32((u32)(dst & 0xFFFFFFFF), dma_base + DMA_DST_ADDR_LO);
    iowrite32((u32)(dst >> 32), dma_base + DMA_DST_ADDR_HI);
    iowrite32(len, dma_base + DMA_LENGTH_LO);
    iowrite32(0, dma_base + DMA_LENGTH_HI);
}

void dma_start(void __iomem *dma_base) {
    iowrite32(1, dma_base + DMA_CONTROL);
}

u32 dma_read_status(void __iomem *dma_base) {
    return ioread32(dma_base + DMA_STATUS);
}
```

#### I/O Request Flow (Write)
1. Block layer submits request to driver
2. Copy data from bio to bounce buffer
3. Flush bounce buffer cache
4. Setup DMA transfer (bounce buffer → OmniXtend memory)
5. Reinitialize completion
6. Start DMA transfer
7. Wait for completion (interrupt-driven with timeout)
8. Flush OmniXtend memory cache
9. Complete request to block layer

#### I/O Request Flow (Read)
1. Block layer submits request to driver
2. Flush OmniXtend memory cache
3. Setup DMA transfer (OmniXtend memory → bounce buffer)
4. Reinitialize completion
5. Start DMA transfer
6. Wait for completion (interrupt-driven with timeout)
7. Flush bounce buffer cache
8. Copy data from bounce buffer to bio
9. Complete request to block layer

### Interrupt Handling

#### Registering Interrupt Handler
Use the Linux IRQ subsystem - do NOT access PLIC registers directly:

```c
#define DMA_IRQ_NUM 1

static irqreturn_t omni_dma_irq_handler(int irq, void *dev_id)
{
    struct omni_blkdev *dev = dev_id;
    u32 status;

    /* Read DMA status to check if this interrupt is for us */
    status = dma_read_status(dev->dma_base);

    if (!(status & DMA_STATUS_DONE))
        return IRQ_NONE;  /* Not our interrupt (for shared IRQ) */

    /* Signal completion to waiting thread */
    complete(&dev->dma_complete);

    return IRQ_HANDLED;
}

/* In module init */
ret = request_irq(DMA_IRQ_NUM, omni_dma_irq_handler,
                  IRQF_SHARED, "omniblk", dev);
if (ret) {
    pr_err("Failed to request IRQ %d: %d\n", DMA_IRQ_NUM, ret);
    goto err_cleanup;
}

/* In module exit */
free_irq(DMA_IRQ_NUM, dev);
```

#### Waiting for DMA Completion
```c
#define DMA_TIMEOUT_MS 5000

int wait_for_dma(struct omni_blkdev *dev)
{
    unsigned long timeout;

    timeout = wait_for_completion_timeout(&dev->dma_complete,
                                          msecs_to_jiffies(DMA_TIMEOUT_MS));

    if (timeout == 0) {
        u32 status = dma_read_status(dev->dma_base);
        pr_err("DMA timeout (status=0x%x)\n", status);
        return -ETIMEDOUT;
    }

    return 0;
}
```

### Error Handling

#### DMA Timeout
- Timeout value: 5 seconds per request
- Action: Fail the request with `-EIO`
- Log error with DMA status register value

#### Invalid Requests
- Out-of-range sector access: return `-EINVAL`
- Oversized requests: split into multiple DMA operations (chunk size = bounce buffer size)

### Concurrency and Locking

#### DMA Controller Access
- **Lock Type**: Mutex (DMA operations may sleep waiting for completion)
- **Scope**: Protect DMA setup, start, and wait sequence
- **Completion**: Use `struct completion` for interrupt-to-thread signaling

```c
struct omni_blkdev {
    void __iomem *dma_base;
    void *dma_buffer;
    dma_addr_t dma_buffer_phys;
    size_t dma_buffer_size;

    struct mutex dma_mutex;
    struct completion dma_complete;

    /* ... other fields ... */
};
```

### Device Initialization

#### Module Load Sequence
1. Allocate device structure with `kzalloc()`
2. Initialize mutex and completion: `mutex_init()`, `init_completion()`
3. Map DMA controller registers: `ioremap()`
4. Allocate bounce buffer: `kmalloc(GFP_KERNEL | GFP_DMA)`
5. Request IRQ: `request_irq()`
6. Register block device: `register_blkdev()`
7. Allocate and initialize blk-mq tag set
8. Allocate gendisk and request queue
9. Set disk capacity and add disk: `set_capacity()`, `add_disk()`

#### Module Unload Sequence
1. Remove disk: `del_gendisk()`
2. Cleanup blk-mq tag set
3. Unregister block device: `unregister_blkdev()`
4. Free IRQ: `free_irq()`
5. Free bounce buffer: `kfree()`
6. Unmap DMA registers: `iounmap()`
7. Free device structure: `kfree()`

### Configuration Parameters

#### Module Parameters
```c
static unsigned int omni_size_mb = 512;
module_param(omni_size_mb, uint, 0644);
MODULE_PARM_DESC(omni_size_mb, "OmniXtend memory size in MB (default: 512)");
```

## Device Tree Binding

```dts
omnixtend_blkdev@9000000 {
    compatible = "omnixtend,dma-blkdev";
    reg = <0x0 0x9000000 0x0 0x1000>;    /* DMA controller */
    reg-names = "dma";
    interrupts = <1>;
    interrupt-parent = <&plic>;
};
```

The OmniXtend remote memory base address (`0x200000000`) is defined in the driver as it represents a fixed memory-mapped region.

## Testing Strategy

### Functional Tests
Based on `omni_scenario_test.c`:
1. CPU direct write to OmniXtend memory
2. CPU direct read from OmniXtend memory (verify write)
3. DMA local-to-local transfer
4. DMA local-to-OmniXtend transfer
5. DMA OmniXtend-to-local transfer
6. End-to-end verification

### Block Device Tests
- Sequential read/write with `dd`
- Random I/O with `fio`
- Filesystem creation and mounting (ext4)
- Large file operations

### Interrupt Tests
- Verify interrupt handler is called (check `/proc/interrupts`)
- Timeout handling (simulate stuck DMA)

## Kernel APIs Used

- `register_blkdev()` / `unregister_blkdev()`
- `blk_mq_alloc_tag_set()` / `blk_mq_free_tag_set()`
- `blk_mq_init_queue()` / `blk_cleanup_queue()`
- `alloc_disk()` / `put_disk()` / `add_disk()` / `del_gendisk()`
- `request_irq()` / `free_irq()`
- `ioremap()` / `iounmap()`
- `kmalloc()` / `kfree()`
- `virt_to_phys()`
- `mutex_init()` / `mutex_lock()` / `mutex_unlock()`
- `init_completion()` / `reinit_completion()` / `complete()` / `wait_for_completion_timeout()`

## References

- Linux kernel source: `drivers/block/brd.c` (RAM disk reference)
- Linux kernel source: `drivers/block/null_blk/main.c` (null block device)
- Linux kernel source: `drivers/irqchip/irq-sifive-plic.c` (PLIC driver)
- Linux kernel source: `drivers/dma/dw/core.c` (DMA driver example)
- Linux kernel documentation: `Documentation/core-api/irq/irq-domain.rst`
- RISC-V PLIC specification

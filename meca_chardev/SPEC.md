# OmniXtend Block Device Driver Specification

## Overview

This specification describes a Linux block device driver for RISC-V that uses the OmniXtend remote memory as backing storage, accessed via a DMA controller with PLIC interrupt support.

## Target Platform

- **Architecture**: RISC-V 64-bit
- **Linux Kernel**: 5.7.0
- **Hardware**: DMA controller with PLIC interrupt controller

## Hardware Resources

### DMA Controller
- **Base Address**: `0x9000000`
- **Register Layout**:
  - `0x00`: DMA_SRC_ADDR_LO (source address bits [31:0])
  - `0x04`: DMA_SRC_ADDR_HI (source address bits [63:32])
  - `0x08`: DMA_DST_ADDR_LO (destination address bits [31:0])
  - `0x0C`: DMA_DST_ADDR_HI (destination address bits [63:32])
  - `0x10`: DMA_LENGTH_LO (transfer length bits [31:0])
  - `0x14`: DMA_LENGTH_HI (transfer length bits [63:32])
  - `0x18`: DMA_CONTROL (bit 0: start transfer)
  - `0x1C`: DMA_STATUS (bit 0: transfer complete)

### PLIC (Platform-Level Interrupt Controller)
- **Base Address**: `0xC000000`
- **DMA IRQ Number**: 1
- **Hart ID**: 0

### OmniXtend Remote Memory
- **Base Address**: `0x200000000`
- **Size**: To be determined (default: 512 MB)
- **Access**: Via DMA controller or direct CPU access

## Driver Architecture

### Module Type
Standard Linux block device driver (`blkdev`)

### Device Characteristics
- **Device Name**: `/dev/omniblk`
- **Block Size**: 4096 bytes (standard page size)
- **Queue Depth**: 64 (configurable)
- **DMA Alignment**: 64 bytes (cache line size)

### Memory Management

#### Bounce Buffers
Since DMA requires physically contiguous memory:
- Allocate bounce buffers for DMA operations using `kmalloc()` with `GFP_DMA`
- Size: Match max I/O request size (e.g., 1 MB)
- Pool of pre-allocated buffers to reduce allocation overhead

#### Cache Coherency
The RISC-V platform requires explicit cache flushing:
- **Before DMA read** (OmniXtend → local): Flush source cache range
- **After DMA read** (OmniXtend → local): Invalidate destination cache range
- **Before DMA write** (local → OmniXtend): Flush source cache range
- **After DMA write** (local → OmniXtend): Flush destination cache range

Custom cache flush instruction: `.word 0xfc050073` (CFLUSH_D_L1)

### Request Processing

#### I/O Request Flow
1. Block layer submits bio/request to driver queue
2. Driver validates request (range, alignment)
3. Allocate/acquire bounce buffer if needed
4. For write: copy data from page cache to bounce buffer
5. Setup DMA transfer (source, destination, length)
6. Flush appropriate cache ranges
7. Start DMA transfer (write to CONTROL register)
8. Wait for interrupt or poll STATUS register
9. Handle completion in interrupt handler
10. For read: copy data from bounce buffer to page cache
11. Complete request to block layer

#### Request Mapping
- **Read request**: DMA from OmniXtend remote memory to local bounce buffer
- **Write request**: DMA from local bounce buffer to OmniXtend remote memory
- **Address calculation**: `omni_addr = OMNI_BASE + (sector * 512)`

### Interrupt Handling

#### PLIC Configuration
- Set DMA IRQ priority (e.g., 3)
- Set hart threshold to 0
- Enable DMA IRQ for hart 0
- Register interrupt handler with Linux IRQ subsystem

#### IRQ Handler Responsibilities
1. Claim interrupt from PLIC
2. Verify IRQ number matches DMA IRQ
3. Read DMA STATUS register
4. Wake up waiting request or schedule tasklet
5. Complete PLIC interrupt (write to CLAIM register)
6. Return IRQ_HANDLED

#### Fallback Mechanism
If interrupt doesn't arrive within timeout:
- Poll DMA STATUS register
- Log warning about missing interrupt
- Continue with I/O completion

### Error Handling

#### DMA Timeout
- Timeout value: 5 seconds per request
- Action: Fail the request with `-EIO`
- Log error with DMA register dump

#### Invalid Requests
- Out-of-range sector access: return `-EINVAL`
- Non-aligned requests: handle with bounce buffer
- Oversized requests: split into multiple DMA operations

#### DMA Errors
- Check STATUS register for error bits (if available)
- Retry up to 3 times on transient errors
- Report permanent failures to block layer

### Concurrency and Locking

#### DMA Controller Access
- **Lock Type**: Spinlock (DMA operations are short)
- **Scope**: Protect DMA register access and state changes
- **Critical Sections**: DMA setup, start, and status check

#### Request Queue
- Use kernel block layer locking (`blk_mq` locks)
- No additional locking needed for queue operations

### Device Initialization

#### Module Load Sequence
1. Register platform device or parse device tree
2. Request and map memory regions (DMA controller, PLIC, OmniXtend)
3. Allocate bounce buffer pool
4. Initialize PLIC for DMA interrupts
5. Request IRQ from Linux
6. Register block device with `register_blkdev()`
7. Allocate and initialize `gendisk` and request queue
8. Add disk to system (`add_disk()`)

#### Module Unload Sequence
1. Remove disk (`del_gendisk()`)
2. Cleanup request queue (`blk_cleanup_queue()`)
3. Free IRQ
4. Free bounce buffers
5. Unmap memory regions
6. Unregister block device

### Performance Considerations

#### DMA Transfer Size
- Optimal size: 64 KB to 1 MB per transfer
- Avoid very small transfers (< 4 KB) - consider batching

#### Queue Management
- Use `blk-mq` (multi-queue) if kernel supports
- Single hardware queue mapping to DMA controller
- Tag-based request tracking

#### Cache Optimization
- Flush only necessary cache lines (64-byte aligned ranges)
- Use memory barriers (`mb()`, `wmb()`, `rmb()`) appropriately

### Configuration Parameters

#### Module Parameters
- `omni_size`: OmniXtend memory size in MB (default: 512)
- `max_request_size`: Maximum I/O request size in KB (default: 1024)
- `queue_depth`: Request queue depth (default: 64)
- `use_interrupt`: Use interrupt or polling (default: 1 = interrupt)

#### Sysfs Attributes
- `/sys/block/omniblk/omni/dma_address`: DMA controller base address
- `/sys/block/omniblk/omni/remote_address`: OmniXtend base address
- `/sys/block/omniblk/omni/stats`: Statistics (DMA count, errors, etc.)

## Device Tree Binding (Optional)

```dts
omnixtend_blkdev@9000000 {
    compatible = "omnixtend,dma-blkdev";
    reg = <0x0 0x9000000 0x0 0x1000>,      /* DMA controller */
          <0x0 0xC000000 0x0 0x4000000>,   /* PLIC */
          <0x2 0x00000000 0x0 0x20000000>; /* OmniXtend 512MB */
    reg-names = "dma", "plic", "storage";
    interrupts = <1>;
    interrupt-parent = <&plic>;
};
```

## Testing Strategy

### Unit Tests
- DMA register read/write
- PLIC initialization and interrupt handling
- Cache flush operations
- Bounce buffer allocation/deallocation

### Functional Tests
- Sequential read/write operations
- Random I/O patterns
- Large file copy to/from device
- Filesystem creation and mounting (ext4)

### Stress Tests
- Concurrent I/O from multiple processes
- Long-duration I/O workloads
- Error injection (simulate DMA timeouts)

### Performance Benchmarks
- `dd` sequential read/write throughput
- `fio` random I/O performance
- Compare with ramdisk baseline

## Kernel APIs Used

- `register_blkdev()` / `unregister_blkdev()`
- `blk_alloc_queue()` / `blk_cleanup_queue()`
- `blk_mq_init_queue()` (for multi-queue)
- `alloc_disk()` / `put_disk()` / `add_disk()` / `del_gendisk()`
- `request_irq()` / `free_irq()`
- `ioremap()` / `iounmap()`
- `__flush_dcache_area()` or custom cache flush
- `dma_alloc_coherent()` or `kmalloc(GFP_DMA)`
- `bio_for_each_segment()` for bio processing

## References

- Linux Device Drivers, 3rd Edition (Chapter 16: Block Drivers)
- Linux kernel source: `drivers/block/brd.c` (RAM disk reference)
- Linux kernel source: `drivers/block/null_blk.c` (null block device)
- RISC-V PLIC specification
- OmniXtend specification (if available)

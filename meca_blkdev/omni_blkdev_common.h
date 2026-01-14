/*
 * omni_blkdev_common.h - OmniXtend Block Device Driver Common Header
 *
 * Shared definitions and inline functions for the block device driver
 */

#ifndef _OMNI_BLKDEV_COMMON_H
#define _OMNI_BLKDEV_COMMON_H

#include <linux/types.h>
#include <linux/io.h>

/* Driver version */
#define OMNI_BLKDEV_VERSION "1.0.0"
#define OMNI_BLKDEV_NAME "omniblk"

/* Hardware addresses */
#define DMA_BASE_ADDR           0x9000000ULL
#define OMNI_REMOTE_MEM_BASE    0x200000000ULL

/* DMA controller register offsets */
#define DMA_SRC_ADDR_LO         0x00
#define DMA_SRC_ADDR_HI         0x04
#define DMA_DST_ADDR_LO         0x08
#define DMA_DST_ADDR_HI         0x0C
#define DMA_LENGTH_LO           0x10
#define DMA_LENGTH_HI           0x14
#define DMA_CONTROL             0x18
#define DMA_STATUS              0x1C

/* Hardware configuration */
#define DMA_IRQ_NUM             1
#define CACHE_LINE_SIZE         64
#define DMA_STATUS_DONE         0x4

/* Driver defaults */
#define DEFAULT_OMNI_SIZE_MB    512
#define DMA_BUFFER_SIZE         (1024 * 1024)  /* 1 MB */

/* Block device configuration */
#define OMNI_SECTOR_SIZE        512
#define OMNI_QUEUE_DEPTH        64

/* Timeouts */
#define DMA_TIMEOUT_MS          5000

/*
 * Register access helper functions
 */
static inline void omni_write_reg32(void __iomem *base, u32 offset, u32 value)
{
	iowrite32(value, base + offset);
}

static inline u32 omni_read_reg32(void __iomem *base, u32 offset)
{
	return ioread32(base + offset);
}

#ifdef DEBUG
static inline void omni_write_reg32_debug(void __iomem *base, u32 offset,
					  u32 value)
{
	pr_debug("omni_write: 0x%p = 0x%x\n", base + offset, value);
	iowrite32(value, base + offset);
}

static inline u32 omni_read_reg32_debug(void __iomem *base, u32 offset)
{
	u32 value = ioread32(base + offset);
	pr_debug("omni_read: 0x%p = 0x%x\n", base + offset, value);
	return value;
}
#else
#define omni_write_reg32_debug(base, offset, value) \
	omni_write_reg32(base, offset, value)
#define omni_read_reg32_debug(base, offset) \
	omni_read_reg32(base, offset)
#endif

/*
 * Cache flush - RISC-V custom instruction (CFLUSH_D_L1)
 * Enable if experiencing data corruption issues
 */
static inline void omni_flush_dcache_line(u64 addr)
{
#ifdef CONFIG_OMNI_CACHE_FLUSH
	register u64 a0 asm("a0") = addr;
	asm volatile("fence rw, rw" ::: "memory");
	asm volatile(".word 0xfc050073" : : "r"(a0) : "memory");
	asm volatile("fence rw, rw" ::: "memory");
#else
	(void)addr;
#endif
}

static inline void omni_flush_dcache_range(u64 start_addr, u64 length)
{
#ifdef CONFIG_OMNI_CACHE_FLUSH
	u64 addr;
	u64 end = start_addr + length;

	for (addr = start_addr & ~(CACHE_LINE_SIZE - 1);
	     addr < end;
	     addr += CACHE_LINE_SIZE) {
		omni_flush_dcache_line(addr);
	}
#else
	(void)start_addr;
	(void)length;
#endif
}

#endif /* _OMNI_BLKDEV_COMMON_H */

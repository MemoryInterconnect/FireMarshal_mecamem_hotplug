/*
 * omni_chardev_common.h - OmniXtend Character Device Driver Common Header
 *
 * Shared definitions and inline functions for both driver implementations
 */

#ifndef _OMNI_CHARDEV_COMMON_H
#define _OMNI_CHARDEV_COMMON_H

#include <linux/types.h>
#include <linux/io.h>

/* Driver version */
#define OMNI_CHARDEV_VERSION "0.2.0"
#define OMNI_CHARDEV_NAME "omnichar"
#define OMNI_CLASS_NAME "omnixtend"

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
#define DMA_STATUS_DONE         0x1

/* Driver defaults */
//#define USE_LOCAL
#ifdef USE_LOCAL
#define DEFAULT_OMNI_SIZE_MB    1
#else
#define DEFAULT_OMNI_SIZE_MB    512
#endif
#define DMA_BUFFER_SIZE         (1024 * 1024)  /* 1 MB */

/* Timeouts */
#define DMA_TIMEOUT_MS          5000
#define DMA_POLL_INTERVAL_US    10

/* ioctl commands */
#define OMNI_IOC_MAGIC 'O'
#define OMNI_IOC_GET_SIZE       _IOR(OMNI_IOC_MAGIC, 1, unsigned long)
#define OMNI_IOC_GET_STATS      _IOR(OMNI_IOC_MAGIC, 2, struct omni_stats_ioctl)
#define OMNI_IOC_RESET_STATS    _IO(OMNI_IOC_MAGIC, 3)

/* ioctl data structure */
struct omni_stats_ioctl {
	__u64 dma_reads;
	__u64 dma_writes;
	__u64 dma_errors;
	__u64 dma_timeouts;
	__u64 irq_count;
};

/*
 * Common inline helper functions
 */

/* Register access with optional debug output */
static inline void omni_write_reg32_debug(void __iomem *base, u32 offset,
					   u32 value, bool debug)
{
	if (debug)
		printk("%s 0x%p 0x%x\n", __func__, base + offset, value);
	iowrite32(value, base + offset);
}

static inline u32 omni_read_reg32_debug(void __iomem *base, u32 offset,
					 bool debug)
{
	u32 value = ioread32(base + offset);
	if (debug)
		printk("%s 0x%p 0x%x\n", __func__, base + offset, value);
	return value;
}

/* Cache flush - RISC-V custom instruction (currently disabled) */
static inline void omni_flush_dcache_line(u64 addr)
{
	/* Enable if experiencing data corruption */
	/*
	asm volatile("fence rw, rw");
	asm volatile(".word 0xfc050073" : : "r"(addr));
	asm volatile("fence rw, rw");
	*/
	(void)addr;  /* Suppress unused warning */
}

static inline void omni_flush_dcache_range(u64 start_addr, u64 length)
{
	u64 addr;
	u64 end = start_addr + length;

	for (addr = start_addr & ~(CACHE_LINE_SIZE - 1);
	     addr < end;
	     addr += CACHE_LINE_SIZE) {
		omni_flush_dcache_line(addr);
	}
}

#endif /* _OMNI_CHARDEV_COMMON_H */

/*
 * omni_blkdev.h - OmniXtend Block Device Driver Header
 *
 * Device structure and definitions for the interrupt-based block device driver
 */

#ifndef _OMNI_BLKDEV_H
#define _OMNI_BLKDEV_H

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/atomic.h>
#include <linux/platform_device.h>

#include "omni_blkdev_common.h"

/*
 * Command structure embedded in each request (PDU - Private Data Unit)
 * This is allocated via blk_mq_tag_set.cmd_size
 */
struct omni_cmd {
	struct omni_blkdev *dev;
	blk_status_t status;
};

/*
 * Main device structure for OmniXtend block device
 */
struct omni_blkdev {
	/* Platform device reference */
	struct platform_device *pdev;

	/* Block device */
	struct gendisk *disk;
	struct blk_mq_tag_set tag_set;
	int major;

	/* Hardware resources */
	void __iomem *dma_base;
	int dma_irq;

	/* OmniXtend remote memory (physical address) */
	dma_addr_t omni_mem_phys;

	/* DMA bounce buffer */
	void *dma_buffer;
	dma_addr_t dma_buffer_phys;
	size_t dma_buffer_size;

	/* Synchronization */
	struct mutex dma_mutex;		/* Protects DMA operations */
	struct completion dma_complete;	/* Signals DMA completion */

	/* Device parameters */
	size_t omni_size_bytes;		/* Total size in bytes */
	sector_t capacity_sectors;	/* Total size in 512-byte sectors */

	/* Statistics (atomic for lock-free updates) */
	atomic64_t dma_reads;
	atomic64_t dma_writes;
	atomic64_t dma_errors;
	atomic64_t dma_timeouts;
	atomic64_t irq_count;
};

#endif /* _OMNI_BLKDEV_H */

/*
 * omni_chardev_polling.h - OmniXtend Character Device Driver Header (Polling Version)
 *
 * Header for polling-based character device driver
 */

#ifndef _OMNI_CHARDEV_POLLING_H
#define _OMNI_CHARDEV_POLLING_H

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include "omni_chardev_common.h"

/* Device structure for polling-based driver */
struct omni_chardev {
	/* Character device */
	struct cdev cdev;
	dev_t dev_num;
	struct class *class;
	struct device *device;

	/* Hardware resources */
	void __iomem *dma_base;
	void __iomem *omni_base;
	int dma_irq;

	/* DMA buffer */
	void *dma_buffer;
	dma_addr_t dma_buffer_phys;
	size_t dma_buffer_size;

	/* Synchronization - uses spinlock (atomic context) */
	struct mutex dev_mutex;
	spinlock_t dma_lock;

	/* Device parameters */
	size_t omni_size_bytes;

	/* Statistics */
	atomic64_t dma_reads;
	atomic64_t dma_writes;
	atomic64_t dma_errors;
	atomic64_t dma_timeouts;
	atomic64_t irq_count;

	/* State */
	bool device_open;
};

#endif /* _OMNI_CHARDEV_POLLING_H */

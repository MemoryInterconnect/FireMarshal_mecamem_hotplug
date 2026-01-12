/*
 * omni_chardev.h - OmniXtend Character Device Driver Header (Interrupt Version)
 *
 * Header for interrupt-based character device driver
 */

#ifndef _OMNI_CHARDEV_IRQ_H
#define _OMNI_CHARDEV_IRQ_H

#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/completion.h>

#include "omni_chardev_common.h"

/* Device structure for interrupt-based driver */
struct omni_chardev {
	/* Character device */
	struct cdev cdev;
	dev_t dev_num;
	struct class *class;
	struct device *device;

	/* Hardware resources */
	void __iomem *dma_base;
	int dma_irq;

	/* Allocated kernel memory (instead of OMNI_REMOTE_MEM_BASE) */
	void *omni_mem;
	dma_addr_t omni_mem_phys;

	/* DMA buffer */
	void *dma_buffer;
	dma_addr_t dma_buffer_phys;
	size_t dma_buffer_size;

	/* Synchronization - uses mutexes (can sleep) */
	struct mutex dev_mutex;
	struct mutex dma_mutex;
	struct completion dma_complete;

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

#endif /* _OMNI_CHARDEV_IRQ_H */

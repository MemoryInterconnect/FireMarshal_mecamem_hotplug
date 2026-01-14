/*
 * omni_blkdev_irq.c - OmniXtend Block Device Driver (Platform Driver)
 *
 * Platform driver for access to OmniXtend remote memory via DMA controller.
 * Uses Linux blk-mq infrastructure for request handling and gets interrupt
 * configuration from device tree.
 *
 * Copyright (C) 2024
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/bio.h>
#include <linux/highmem.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "omni_blkdev.h"

/* Global device pointer (single instance) */
static struct omni_blkdev *g_omni_dev;
static int omni_major;

/* Module parameters */
static unsigned int omni_size_mb = DEFAULT_OMNI_SIZE_MB;
module_param(omni_size_mb, uint, 0644);
MODULE_PARM_DESC(omni_size_mb, "OmniXtend memory size in MB (default: 512)");

/*****************************************************************************
 * DMA Helper Functions
 *****************************************************************************/

static void dma_setup_transfer(struct omni_blkdev *dev, u64 src, u64 dst,
			       u32 len)
{
	omni_write_reg32(dev->dma_base, DMA_SRC_ADDR_LO,
			 (u32)(src & 0xFFFFFFFF));
	omni_write_reg32(dev->dma_base, DMA_SRC_ADDR_HI,
			 (u32)(src >> 32));
	omni_write_reg32(dev->dma_base, DMA_DST_ADDR_LO,
			 (u32)(dst & 0xFFFFFFFF));
	omni_write_reg32(dev->dma_base, DMA_DST_ADDR_HI,
			 (u32)(dst >> 32));
	omni_write_reg32(dev->dma_base, DMA_LENGTH_LO, len);
	omni_write_reg32(dev->dma_base, DMA_LENGTH_HI, 0);
}

static void dma_start(struct omni_blkdev *dev)
{
	omni_write_reg32(dev->dma_base, DMA_CONTROL, 1);
}

static u32 dma_read_status(struct omni_blkdev *dev)
{
	return omni_read_reg32(dev->dma_base, DMA_STATUS);
}

/*****************************************************************************
 * Interrupt Handler
 *****************************************************************************/

static irqreturn_t omni_dma_irq_handler(int irq, void *dev_id)
{
	struct omni_blkdev *dev = (struct omni_blkdev *)dev_id;
	u32 status;

	status = dma_read_status(dev);

	if (!(status & DMA_STATUS_DONE))
		return IRQ_NONE;

	/* Signal completion to waiting thread */
	complete(&dev->dma_complete);
	atomic64_inc(&dev->irq_count);

	return IRQ_HANDLED;
}

/*****************************************************************************
 * DMA Wait Function
 *****************************************************************************/

static int omni_wait_for_dma(struct omni_blkdev *dev)
{
	unsigned long timeout;

	timeout = wait_for_completion_timeout(&dev->dma_complete,
					      msecs_to_jiffies(DMA_TIMEOUT_MS));

	if (timeout == 0) {
		u32 status = dma_read_status(dev);
		pr_err("omniblk: DMA timeout (status=0x%x)\n", status);
		atomic64_inc(&dev->dma_timeouts);
		return -ETIMEDOUT;
	}

	return 0;
}

/*****************************************************************************
 * DMA Transfer Functions
 *****************************************************************************/

/*
 * Perform a single DMA transfer (up to dma_buffer_size bytes)
 * For reads: OmniXtend -> DMA buffer
 * For writes: DMA buffer -> OmniXtend
 */
static int omni_do_dma_transfer(struct omni_blkdev *dev, u64 omni_offset,
				size_t len, bool is_write)
{
	u64 omni_addr = dev->omni_mem_phys + omni_offset;
	int ret;

	/* Flush caches before transfer */
	if (is_write) {
		omni_flush_dcache_range(dev->dma_buffer_phys, len);
		dma_setup_transfer(dev, dev->dma_buffer_phys, omni_addr, len);
	} else {
		omni_flush_dcache_range(omni_addr, len);
		dma_setup_transfer(dev, omni_addr, dev->dma_buffer_phys, len);
	}

	/* Reinitialize completion before starting DMA */
	reinit_completion(&dev->dma_complete);

	/* Start DMA and wait for completion */
	dma_start(dev);
	ret = omni_wait_for_dma(dev);

	if (ret) {
		atomic64_inc(&dev->dma_errors);
		return ret;
	}

	/* Flush caches after transfer */
	if (is_write) {
		omni_flush_dcache_range(omni_addr, len);
		atomic64_inc(&dev->dma_writes);
	} else {
		omni_flush_dcache_range(dev->dma_buffer_phys, len);
		atomic64_inc(&dev->dma_reads);
	}

	return 0;
}

/*****************************************************************************
 * Request Processing
 *****************************************************************************/

/*
 * Process a single block request
 * Iterates over all bio_vecs and performs DMA transfers
 */
static blk_status_t omni_handle_request(struct omni_blkdev *dev,
					struct request *rq)
{
	struct bio_vec bvec;
	struct req_iterator iter;
	sector_t sector = blk_rq_pos(rq);
	bool is_write = (req_op(rq) == REQ_OP_WRITE);
	blk_status_t status = BLK_STS_OK;
	void *buf;
	size_t offset;
	size_t chunk_size;
	int ret;

	mutex_lock(&dev->dma_mutex);

	rq_for_each_segment(bvec, rq, iter) {
		size_t len = bvec.bv_len;
		u64 omni_offset = sector * OMNI_SECTOR_SIZE;

		/* Map the page for CPU access */
		buf = kmap_local_page(bvec.bv_page) + bvec.bv_offset;

		offset = 0;
		while (offset < len) {
			chunk_size = min(len - offset, dev->dma_buffer_size);

			if (is_write) {
				/* Copy data to DMA buffer first */
				memcpy(dev->dma_buffer, buf + offset,
				       chunk_size);
			}

			/* Perform DMA transfer */
			ret = omni_do_dma_transfer(dev, omni_offset + offset,
						   chunk_size, is_write);
			if (ret) {
				kunmap_local(buf);
				status = BLK_STS_IOERR;
				goto out;
			}

			if (!is_write) {
				/* Copy data from DMA buffer to page */
				memcpy(buf + offset, dev->dma_buffer,
				       chunk_size);
			}

			offset += chunk_size;
		}

		kunmap_local(buf);

		/* Advance sector position */
		sector += len / OMNI_SECTOR_SIZE;
	}

out:
	mutex_unlock(&dev->dma_mutex);
	return status;
}

/*****************************************************************************
 * blk-mq Operations
 *****************************************************************************/

static blk_status_t omni_queue_rq(struct blk_mq_hw_ctx *hctx,
				  const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	struct omni_blkdev *dev = rq->q->queuedata;
	blk_status_t status;

	/* Only handle read/write operations */
	switch (req_op(rq)) {
	case REQ_OP_READ:
	case REQ_OP_WRITE:
		break;
	default:
		return BLK_STS_IOERR;
	}

	/* Mark request as started */
	blk_mq_start_request(rq);

	/* Process the request */
	status = omni_handle_request(dev, rq);

	/* Complete the request */
	blk_mq_end_request(rq, status);

	return BLK_STS_OK;
}

static const struct blk_mq_ops omni_mq_ops = {
	.queue_rq = omni_queue_rq,
};

/*****************************************************************************
 * Block Device Operations
 *****************************************************************************/

static int omni_open(struct gendisk *disk, blk_mode_t mode)
{
	pr_info("omniblk: device opened\n");
	return 0;
}

static void omni_release(struct gendisk *disk)
{
	pr_info("omniblk: device released\n");
}

static const struct block_device_operations omni_fops = {
	.owner = THIS_MODULE,
	.open = omni_open,
	.release = omni_release,
};

/*****************************************************************************
 * Platform Driver Probe/Remove
 *****************************************************************************/

static int omni_blkdev_probe(struct platform_device *pdev)
{
	struct omni_blkdev *dev;
	struct resource *res;
	struct queue_limits lim = {
		.logical_block_size = OMNI_SECTOR_SIZE,
		.physical_block_size = OMNI_SECTOR_SIZE,
		.max_hw_sectors = DMA_BUFFER_SIZE / OMNI_SECTOR_SIZE,
	};
	int ret;
	int irq;

	pr_info("omniblk: Probing OmniXtend Block Device Driver v%s\n",
		OMNI_BLKDEV_VERSION);

	/* Only allow one instance */
	if (g_omni_dev) {
		pr_err("omniblk: Device already exists\n");
		return -EEXIST;
	}

	/* Allocate device structure */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->pdev = pdev;
	platform_set_drvdata(pdev, dev);

	/* Initialize device parameters */
	dev->omni_size_bytes = (size_t)omni_size_mb * 1024 * 1024;
	dev->capacity_sectors = dev->omni_size_bytes / OMNI_SECTOR_SIZE;
	dev->omni_mem_phys = OMNI_REMOTE_MEM_BASE;

	/* Initialize synchronization primitives */
	mutex_init(&dev->dma_mutex);
	init_completion(&dev->dma_complete);

	/* Initialize statistics */
	atomic64_set(&dev->dma_reads, 0);
	atomic64_set(&dev->dma_writes, 0);
	atomic64_set(&dev->dma_errors, 0);
	atomic64_set(&dev->dma_timeouts, 0);
	atomic64_set(&dev->irq_count, 0);

	/* Get DMA controller registers from device tree */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return -ENODEV;
	}

	dev->dma_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->dma_base)) {
		dev_err(&pdev->dev, "Failed to map DMA controller\n");
		return PTR_ERR(dev->dma_base);
	}

	dev_info(&pdev->dev, "Mapped DMA controller @ 0x%llx (size 0x%llx)\n",
		 (unsigned long long)res->start,
		 (unsigned long long)resource_size(res));

	/* Allocate DMA buffer */
	dev->dma_buffer_size = DMA_BUFFER_SIZE;
	dev->dma_buffer = devm_kmalloc(&pdev->dev, dev->dma_buffer_size,
				       GFP_KERNEL | GFP_DMA);
	if (!dev->dma_buffer) {
		dev_err(&pdev->dev, "Failed to allocate DMA buffer\n");
		return -ENOMEM;
	}
	dev->dma_buffer_phys = virt_to_phys(dev->dma_buffer);

	dev_info(&pdev->dev, "Allocated DMA buffer: %zu KB @ phys 0x%llx\n",
		 dev->dma_buffer_size / 1024,
		 (unsigned long long)dev->dma_buffer_phys);

	/* Get IRQ from device tree */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ from device tree\n");
		return irq;
	}
	dev->dma_irq = irq;

	/* Request IRQ */
	ret = devm_request_irq(&pdev->dev, dev->dma_irq, omni_dma_irq_handler,
			       IRQF_SHARED, OMNI_BLKDEV_NAME, dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n",
			dev->dma_irq, ret);
		return ret;
	}
	dev_info(&pdev->dev, "Registered IRQ %d (from device tree)\n",
		 dev->dma_irq);

	/* Register block device major number */
	dev->major = register_blkdev(0, OMNI_BLKDEV_NAME);
	if (dev->major < 0) {
		dev_err(&pdev->dev, "Failed to register block device\n");
		return dev->major;
	}
	omni_major = dev->major;
	dev_info(&pdev->dev, "Registered major number %d\n", dev->major);

	/* Setup blk-mq tag set */
	dev->tag_set.ops = &omni_mq_ops;
	dev->tag_set.nr_hw_queues = 1;
	dev->tag_set.queue_depth = OMNI_QUEUE_DEPTH;
	dev->tag_set.numa_node = NUMA_NO_NODE;
	dev->tag_set.cmd_size = sizeof(struct omni_cmd);
	dev->tag_set.flags = BLK_MQ_F_BLOCKING;

	ret = blk_mq_alloc_tag_set(&dev->tag_set);
	if (ret) {
		dev_err(&pdev->dev, "Failed to allocate tag set: %d\n", ret);
		goto err_unregister_blkdev;
	}

	/* Allocate disk (creates queue automatically) */
	dev->disk = blk_mq_alloc_disk(&dev->tag_set, &lim, dev);
	if (IS_ERR(dev->disk)) {
		ret = PTR_ERR(dev->disk);
		dev_err(&pdev->dev, "Failed to allocate disk: %d\n", ret);
		goto err_free_tagset;
	}

	/* Configure disk */
	dev->disk->major = dev->major;
	dev->disk->first_minor = 0;
	dev->disk->minors = 1;
	dev->disk->fops = &omni_fops;
	dev->disk->private_data = dev;
	snprintf(dev->disk->disk_name, DISK_NAME_LEN, OMNI_BLKDEV_NAME);
	set_capacity(dev->disk, dev->capacity_sectors);

	/* Store device pointer in queue */
	dev->disk->queue->queuedata = dev;

	/* Add disk to system */
	ret = add_disk(dev->disk);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add disk: %d\n", ret);
		goto err_put_disk;
	}

	g_omni_dev = dev;

	dev_info(&pdev->dev,
		 "Device registered: /dev/%s, %zu MB (%llu sectors)\n",
		 OMNI_BLKDEV_NAME, dev->omni_size_bytes / (1024 * 1024),
		 (unsigned long long)dev->capacity_sectors);

	return 0;

err_put_disk:
	put_disk(dev->disk);
err_free_tagset:
	blk_mq_free_tag_set(&dev->tag_set);
err_unregister_blkdev:
	unregister_blkdev(dev->major, OMNI_BLKDEV_NAME);
	return ret;
}

static void omni_blkdev_remove(struct platform_device *pdev)
{
	struct omni_blkdev *dev = platform_get_drvdata(pdev);

	if (!dev)
		return;

	dev_info(&pdev->dev, "Removing driver\n");

	/* Remove disk from system */
	del_gendisk(dev->disk);
	put_disk(dev->disk);

	/* Free tag set */
	blk_mq_free_tag_set(&dev->tag_set);

	/* Unregister block device */
	unregister_blkdev(dev->major, OMNI_BLKDEV_NAME);

	/* Print statistics */
	dev_info(&pdev->dev,
		 "Stats - reads: %lld, writes: %lld, errors: %lld, "
		 "timeouts: %lld, irqs: %lld\n",
		 atomic64_read(&dev->dma_reads),
		 atomic64_read(&dev->dma_writes),
		 atomic64_read(&dev->dma_errors),
		 atomic64_read(&dev->dma_timeouts),
		 atomic64_read(&dev->irq_count));

	g_omni_dev = NULL;

	dev_info(&pdev->dev, "Driver removed\n");
}

/*****************************************************************************
 * Platform Driver Definition
 *****************************************************************************/

static const struct of_device_id omni_blkdev_of_match[] = {
	{ .compatible = "etri,omni-dma" },
	{ }
};
MODULE_DEVICE_TABLE(of, omni_blkdev_of_match);

static struct platform_driver omni_blkdev_driver = {
	.probe = omni_blkdev_probe,
	.remove = omni_blkdev_remove,
	.driver = {
		.name = "omni-blkdev",
		.of_match_table = omni_blkdev_of_match,
	},
};

module_platform_driver(omni_blkdev_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("OmniXtend Team");
MODULE_DESCRIPTION("OmniXtend Block Device Driver for RISC-V (Platform Driver)");
MODULE_VERSION(OMNI_BLKDEV_VERSION);

/*
 * Qualcomm Technologies HIDMA DMA engine low level code
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dmaengine.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/iopoll.h>
#include <linux/kfifo.h>
#include <linux/bitops.h>

#include "hidma.h"

#define EVRE_SIZE			16	/* each EVRE is 16 bytes */

#define TRCA_CTRLSTS_OFFSET		0x000
#define TRCA_RING_LOW_OFFSET		0x008
#define TRCA_RING_HIGH_OFFSET		0x00C
#define TRCA_RING_LEN_OFFSET		0x010
#define TRCA_READ_PTR_OFFSET		0x018
#define TRCA_WRITE_PTR_OFFSET		0x020
#define TRCA_DOORBELL_OFFSET		0x400

#define EVCA_CTRLSTS_OFFSET		0x000
#define EVCA_INTCTRL_OFFSET		0x004
#define EVCA_RING_LOW_OFFSET		0x008
#define EVCA_RING_HIGH_OFFSET		0x00C
#define EVCA_RING_LEN_OFFSET		0x010
#define EVCA_READ_PTR_OFFSET		0x018
#define EVCA_WRITE_PTR_OFFSET		0x020
#define EVCA_DOORBELL_OFFSET		0x400

#define EVCA_IRQ_STAT_OFFSET		0x100
#define EVCA_IRQ_CLR_OFFSET		0x108
#define EVCA_IRQ_EN_OFFSET		0x110

#define EVRE_CFG_IDX			0
#define EVRE_LEN_IDX			1
#define EVRE_DEST_LOW_IDX		2
#define EVRE_DEST_HI_IDX		3

#define EVRE_ERRINFO_BIT_POS		24
#define EVRE_CODE_BIT_POS		28

#define EVRE_ERRINFO_MASK		GENMASK(3, 0)
#define EVRE_CODE_MASK			GENMASK(3, 0)

#define CH_CONTROL_MASK		GENMASK(7, 0)
#define CH_STATE_MASK			GENMASK(7, 0)
#define CH_STATE_BIT_POS		0x8

#define IRQ_EV_CH_EOB_IRQ_BIT_POS	0
#define IRQ_EV_CH_WR_RESP_BIT_POS	1
#define IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS 9
#define IRQ_TR_CH_DATA_RD_ER_BIT_POS	10
#define IRQ_TR_CH_DATA_WR_ER_BIT_POS	11
#define IRQ_TR_CH_INVALID_TRE_BIT_POS	14

#define	ENABLE_IRQS (BIT(IRQ_EV_CH_EOB_IRQ_BIT_POS)	| \
		BIT(IRQ_EV_CH_WR_RESP_BIT_POS)		| \
		BIT(IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS)	| \
		BIT(IRQ_TR_CH_DATA_RD_ER_BIT_POS)	| \
		BIT(IRQ_TR_CH_DATA_WR_ER_BIT_POS)	| \
		BIT(IRQ_TR_CH_INVALID_TRE_BIT_POS))

#define HIDMA_INCREMENT_ITERATOR(iter, size, ring_size)	\
do {								\
	iter += size;						\
	if (iter >= ring_size)					\
		iter -= ring_size;				\
} while (0)

#define HIDMA_CH_STATE(val)	\
	((val >> CH_STATE_BIT_POS) & CH_STATE_MASK)

enum ch_command {
	CH_DISABLE = 0,
	CH_ENABLE = 1,
	CH_SUSPEND = 2,
	CH_RESET = 9,
};

enum ch_state {
	CH_DISABLED = 0,
	CH_ENABLED = 1,
	CH_RUNNING = 2,
	CH_SUSPENDED = 3,
	CH_STOPPED = 4,
	CH_ERROR = 5,
	CH_IN_RESET = 9,
};

enum tre_type {
	TRE_MEMCPY = 3,
	TRE_MEMSET = 4,
};

enum evre_type {
	EVRE_DMA_COMPLETE = 0x23,
	EVRE_IMM_DATA = 0x24,
};

enum err_code {
	EVRE_STATUS_COMPLETE = 1,
	EVRE_STATUS_ERROR = 4,
};

void hidma_ll_free(struct hidma_lldev *lldev, u32 tre_ch)
{
	struct hidma_tre *tre;

	if (tre_ch >= lldev->nr_tres) {
		dev_err(lldev->dev, "invalid TRE number in free:%d", tre_ch);
		return;
	}

	tre = &lldev->trepool[tre_ch];
	if (atomic_read(&tre->allocated) != true) {
		dev_err(lldev->dev, "trying to free an unused TRE:%d", tre_ch);
		return;
	}

	atomic_set(&tre->allocated, 0);
}

int hidma_ll_request(struct hidma_lldev *lldev, u32 dma_sig,
		     const char *dev_name,
		     void (*callback)(void *data), void *data, u32 *tre_ch)
{
	unsigned int i;
	struct hidma_tre *tre;
	u32 *tre_local;

	if (!tre_ch || !lldev)
		return -EINVAL;

	/* need to have at least one empty spot in the queue */
	for (i = 0; i < lldev->nr_tres - 1; i++) {
		if (atomic_add_unless(&lldev->trepool[i].allocated, 1, 1))
			break;
	}

	if (i == (lldev->nr_tres - 1))
		return -ENOMEM;

	tre = &lldev->trepool[i];
	tre->dma_sig = dma_sig;
	tre->dev_name = dev_name;
	tre->callback = callback;
	tre->data = data;
	tre->idx = i;
	tre->status = 0;
	tre->queued = 0;
	lldev->tx_status_list[i].err_code = 0;
	tre->lldev = lldev;
	tre_local = &tre->tre_local[0];
	tre_local[TRE_CFG_IDX] = TRE_MEMCPY;
	tre_local[TRE_CFG_IDX] |= (lldev->chidx & 0xFF) << 8;
	tre_local[TRE_CFG_IDX] |= BIT(16);	/* set IEOB */
	*tre_ch = i;
	if (callback)
		callback(data);
	return 0;
}

/*
 * Multiple TREs may be queued and waiting in the
 * pending queue.
 */
static void hidma_ll_tre_complete(unsigned long arg)
{
	struct hidma_lldev *lldev = (struct hidma_lldev *)arg;
	struct hidma_tre *tre;

	while (kfifo_out(&lldev->handoff_fifo, &tre, 1)) {
		/* call the user if it has been read by the hardware */
		if (tre->callback)
			tre->callback(tre->data);
	}
}

/*
 * Called to handle the interrupt for the channel.
 * Return a positive number if TRE or EVRE were consumed on this run.
 * Return a positive number if there are pending TREs or EVREs.
 * Return 0 if there is nothing to consume or no pending TREs/EVREs found.
 */
static int hidma_handle_tre_completion(struct hidma_lldev *lldev)
{
	struct hidma_tre *tre;
	u32 evre_write_off;
	u32 evre_ring_size = lldev->evre_ring_size;
	u32 tre_ring_size = lldev->tre_ring_size;
	u32 num_completed = 0, tre_iterator, evre_iterator;
	unsigned long flags;

	evre_write_off = readl_relaxed(lldev->evca + EVCA_WRITE_PTR_OFFSET);
	tre_iterator = lldev->tre_processed_off;
	evre_iterator = lldev->evre_processed_off;

	if ((evre_write_off > evre_ring_size) ||
	    ((evre_write_off % EVRE_SIZE) != 0)) {
		dev_err(lldev->dev, "HW reports invalid EVRE write offset\n");
		return 0;
	}

	/*
	 * By the time control reaches here the number of EVREs and TREs
	 * may not match. Only consume the ones that hardware told us.
	 */
	while ((evre_iterator != evre_write_off)) {
		u32 *current_evre = lldev->evre_ring + evre_iterator;
		u32 cfg;
		u8 err_info;

		spin_lock_irqsave(&lldev->lock, flags);
		tre = lldev->pending_tre_list[tre_iterator / TRE_SIZE];
		if (!tre) {
			spin_unlock_irqrestore(&lldev->lock, flags);
			dev_warn(lldev->dev,
				 "tre_index [%d] and tre out of sync\n",
				 tre_iterator / TRE_SIZE);
			HIDMA_INCREMENT_ITERATOR(tre_iterator, TRE_SIZE,
						 tre_ring_size);
			HIDMA_INCREMENT_ITERATOR(evre_iterator, EVRE_SIZE,
						 evre_ring_size);
			continue;
		}
		lldev->pending_tre_list[tre->tre_index] = NULL;

		/*
		 * Keep track of pending TREs that SW is expecting to receive
		 * from HW. We got one now. Decrement our counter.
		 */
		lldev->pending_tre_count--;
		if (lldev->pending_tre_count < 0) {
			dev_warn(lldev->dev,
				 "tre count mismatch on completion");
			lldev->pending_tre_count = 0;
		}

		spin_unlock_irqrestore(&lldev->lock, flags);

		cfg = current_evre[EVRE_CFG_IDX];
		err_info = cfg >> EVRE_ERRINFO_BIT_POS;
		err_info &= EVRE_ERRINFO_MASK;
		lldev->tx_status_list[tre->idx].err_info = err_info;
		lldev->tx_status_list[tre->idx].err_code =
		    (cfg >> EVRE_CODE_BIT_POS) & EVRE_CODE_MASK;
		tre->queued = 0;

		kfifo_put(&lldev->handoff_fifo, tre);
		tasklet_schedule(&lldev->task);

		HIDMA_INCREMENT_ITERATOR(tre_iterator, TRE_SIZE,
					 tre_ring_size);
		HIDMA_INCREMENT_ITERATOR(evre_iterator, EVRE_SIZE,
					 evre_ring_size);

		/*
		 * Read the new event descriptor written by the HW.
		 * As we are processing the delivered events, other events
		 * get queued to the SW for processing.
		 */
		evre_write_off =
		    readl_relaxed(lldev->evca + EVCA_WRITE_PTR_OFFSET);
		num_completed++;
	}

	if (num_completed) {
		u32 evre_read_off = (lldev->evre_processed_off +
				     EVRE_SIZE * num_completed);
		u32 tre_read_off = (lldev->tre_processed_off +
				    TRE_SIZE * num_completed);

		evre_read_off = evre_read_off % evre_ring_size;
		tre_read_off = tre_read_off % tre_ring_size;

		writel(evre_read_off, lldev->evca + EVCA_DOORBELL_OFFSET);

		/* record the last processed tre offset */
		lldev->tre_processed_off = tre_read_off;
		lldev->evre_processed_off = evre_read_off;
	}

	return num_completed;
}

void hidma_cleanup_pending_tre(struct hidma_lldev *lldev, u8 err_info,
			       u8 err_code)
{
	u32 tre_iterator;
	struct hidma_tre *tre;
	u32 tre_ring_size = lldev->tre_ring_size;
	int num_completed = 0;
	u32 tre_read_off;
	unsigned long flags;

	tre_iterator = lldev->tre_processed_off;
	while (lldev->pending_tre_count) {
		int tre_index = tre_iterator / TRE_SIZE;

		spin_lock_irqsave(&lldev->lock, flags);
		tre = lldev->pending_tre_list[tre_index];
		if (!tre) {
			spin_unlock_irqrestore(&lldev->lock, flags);
			HIDMA_INCREMENT_ITERATOR(tre_iterator, TRE_SIZE,
						 tre_ring_size);
			continue;
		}
		lldev->pending_tre_list[tre_index] = NULL;
		lldev->pending_tre_count--;
		if (lldev->pending_tre_count < 0) {
			dev_warn(lldev->dev,
				 "tre count mismatch on completion");
			lldev->pending_tre_count = 0;
		}
		spin_unlock_irqrestore(&lldev->lock, flags);

		lldev->tx_status_list[tre->idx].err_info = err_info;
		lldev->tx_status_list[tre->idx].err_code = err_code;
		tre->queued = 0;

		kfifo_put(&lldev->handoff_fifo, tre);
		tasklet_schedule(&lldev->task);

		HIDMA_INCREMENT_ITERATOR(tre_iterator, TRE_SIZE,
					 tre_ring_size);
		num_completed++;
	}
	tre_read_off = (lldev->tre_processed_off + TRE_SIZE * num_completed);

	tre_read_off = tre_read_off % tre_ring_size;

	/* record the last processed tre offset */
	lldev->tre_processed_off = tre_read_off;
}

static int hidma_ll_reset(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	val = readl(lldev->trca + TRCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_RESET << 16;
	writel(val, lldev->trca + TRCA_CTRLSTS_OFFSET);

	/*
	 * Delay 10ms after reset to allow DMA logic to quiesce.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->trca + TRCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_DISABLED, 1000,
				 10000);
	if (ret) {
		dev_err(lldev->dev, "transfer channel did not reset\n");
		return ret;
	}

	val = readl(lldev->evca + EVCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_RESET << 16;
	writel(val, lldev->evca + EVCA_CTRLSTS_OFFSET);

	/*
	 * Delay 10ms after reset to allow DMA logic to quiesce.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->evca + EVCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_DISABLED, 1000,
				 10000);
	if (ret)
		return ret;

	lldev->trch_state = CH_DISABLED;
	lldev->evch_state = CH_DISABLED;
	return 0;
}

static void hidma_ll_enable_irq(struct hidma_lldev *lldev, u32 irq_bits)
{
	writel(irq_bits, lldev->evca + EVCA_IRQ_EN_OFFSET);
}

/*
 * The interrupt handler for HIDMA will try to consume as many pending
 * EVRE from the event queue as possible. Each EVRE has an associated
 * TRE that holds the user interface parameters. EVRE reports the
 * result of the transaction. Hardware guarantees ordering between EVREs
 * and TREs. We use last processed offset to figure out which TRE is
 * associated with which EVRE. If two TREs are consumed by HW, the EVREs
 * are in order in the event ring.
 *
 * This handler will do a one pass for consuming EVREs. Other EVREs may
 * be delivered while we are working. It will try to consume incoming
 * EVREs one more time and return.
 *
 * For unprocessed EVREs, hardware will trigger another interrupt until
 * all the interrupt bits are cleared.
 *
 * Hardware guarantees that by the time interrupt is observed, all data
 * transactions in flight are delivered to their respective places and
 * are visible to the CPU.
 *
 * On demand paging for IOMMU is only supported for PCIe via PRI
 * (Page Request Interface) not for HIDMA. All other hardware instances
 * including HIDMA work on pinned DMA addresses.
 *
 * HIDMA is not aware of IOMMU presence since it follows the DMA API. All
 * IOMMU latency will be built into the data movement time. By the time
 * interrupt happens, IOMMU lookups + data movement has already taken place.
 *
 * While the first read in a typical PCI endpoint ISR flushes all outstanding
 * requests traditionally to the destination, this concept does not apply
 * here for this HW.
 */
static void hidma_ll_int_handler_internal(struct hidma_lldev *lldev)
{
	u32 status;
	u32 enable;
	u32 cause;
	int repeat = 2;
	unsigned long timeout;

	/*
	 * Fine tuned for this HW...
	 *
	 * This ISR has been designed for this particular hardware. Relaxed
	 * read and write accessors are used for performance reasons due to
	 * interrupt delivery guarantees. Do not copy this code blindly and
	 * expect that to work.
	 */
	status = readl_relaxed(lldev->evca + EVCA_IRQ_STAT_OFFSET);
	enable = readl_relaxed(lldev->evca + EVCA_IRQ_EN_OFFSET);
	cause = status & enable;

	if ((cause & (BIT(IRQ_TR_CH_INVALID_TRE_BIT_POS))) ||
	    (cause & BIT(IRQ_TR_CH_TRE_RD_RSP_ER_BIT_POS)) ||
	    (cause & BIT(IRQ_EV_CH_WR_RESP_BIT_POS)) ||
	    (cause & BIT(IRQ_TR_CH_DATA_RD_ER_BIT_POS)) ||
	    (cause & BIT(IRQ_TR_CH_DATA_WR_ER_BIT_POS))) {
		u8 err_code = EVRE_STATUS_ERROR;
		u8 err_info = 0xFF;

		/* Clear out pending interrupts */
		writel(cause, lldev->evca + EVCA_IRQ_CLR_OFFSET);

		dev_err(lldev->dev, "error 0x%x, resetting...\n", cause);

		hidma_cleanup_pending_tre(lldev, err_info, err_code);

		/* reset the channel for recovery */
		if (hidma_ll_setup(lldev)) {
			dev_err(lldev->dev,
				"channel reinitialize failed after error\n");
			return;
		}
		hidma_ll_enable_irq(lldev, ENABLE_IRQS);
		return;
	}

	/*
	 * Try to consume as many EVREs as possible.
	 * skip this loop if the interrupt is spurious.
	 */
	while (cause && repeat) {
		unsigned long start = jiffies;

		/* This timeout should be sufficent for core to finish */
		timeout = start + msecs_to_jiffies(500);

		while (lldev->pending_tre_count) {
			hidma_handle_tre_completion(lldev);
			if (time_is_before_jiffies(timeout)) {
				dev_warn(lldev->dev,
					 "ISR timeout %lx-%lx from %lx [%d]\n",
					 jiffies, timeout, start,
					 lldev->pending_tre_count);
				break;
			}
		}

		/* We consumed TREs or there are pending TREs or EVREs. */
		writel_relaxed(cause, lldev->evca + EVCA_IRQ_CLR_OFFSET);

		/*
		 * Another interrupt might have arrived while we are
		 * processing this one. Read the new cause.
		 */
		status = readl_relaxed(lldev->evca + EVCA_IRQ_STAT_OFFSET);
		enable = readl_relaxed(lldev->evca + EVCA_IRQ_EN_OFFSET);
		cause = status & enable;

		repeat--;
	}
}

static int hidma_ll_enable(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	val = readl(lldev->evca + EVCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_ENABLE << 16;
	writel(val, lldev->evca + EVCA_CTRLSTS_OFFSET);

	ret = readl_poll_timeout(lldev->evca + EVCA_CTRLSTS_OFFSET, val,
				 (HIDMA_CH_STATE(val) == CH_ENABLED) ||
				 (HIDMA_CH_STATE(val) == CH_RUNNING), 1000,
				 10000);
	if (ret) {
		dev_err(lldev->dev, "event channel did not get enabled\n");
		return ret;
	}

	val = readl(lldev->trca + TRCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_ENABLE << 16;
	writel(val, lldev->trca + TRCA_CTRLSTS_OFFSET);

	ret = readl_poll_timeout(lldev->trca + TRCA_CTRLSTS_OFFSET, val,
				 (HIDMA_CH_STATE(val) == CH_ENABLED) ||
				 (HIDMA_CH_STATE(val) == CH_RUNNING), 1000,
				 10000);
	if (ret) {
		dev_err(lldev->dev, "transfer channel did not get enabled\n");
		return ret;
	}

	lldev->trch_state = CH_ENABLED;
	lldev->evch_state = CH_ENABLED;

	return 0;
}

int hidma_ll_resume(struct hidma_lldev *lldev)
{
	return hidma_ll_enable(lldev);
}

static void hidma_ll_hw_start(struct hidma_lldev *lldev)
{
	unsigned long irqflags;

	spin_lock_irqsave(&lldev->lock, irqflags);
	writel(lldev->tre_write_offset, lldev->trca + TRCA_DOORBELL_OFFSET);
	spin_unlock_irqrestore(&lldev->lock, irqflags);
}

bool hidma_ll_isenabled(struct hidma_lldev *lldev)
{
	u32 val;

	val = readl(lldev->trca + TRCA_CTRLSTS_OFFSET);
	lldev->trch_state = HIDMA_CH_STATE(val);
	val = readl(lldev->evca + EVCA_CTRLSTS_OFFSET);
	lldev->evch_state = HIDMA_CH_STATE(val);

	/* both channels have to be enabled before calling this function */
	if (((lldev->trch_state == CH_ENABLED) ||
	     (lldev->trch_state == CH_RUNNING)) &&
	    ((lldev->evch_state == CH_ENABLED) ||
	     (lldev->evch_state == CH_RUNNING)))
		return true;

	return false;
}

void hidma_ll_queue_request(struct hidma_lldev *lldev, u32 tre_ch)
{
	struct hidma_tre *tre;
	unsigned long flags;

	tre = &lldev->trepool[tre_ch];

	/* copy the TRE into its location in the TRE ring */
	spin_lock_irqsave(&lldev->lock, flags);
	tre->tre_index = lldev->tre_write_offset / TRE_SIZE;
	lldev->pending_tre_list[tre->tre_index] = tre;
	memcpy(lldev->tre_ring + lldev->tre_write_offset, &tre->tre_local[0],
	       TRE_SIZE);
	lldev->tx_status_list[tre->idx].err_code = 0;
	lldev->tx_status_list[tre->idx].err_info = 0;
	tre->queued = 1;
	lldev->pending_tre_count++;
	lldev->tre_write_offset = (lldev->tre_write_offset + TRE_SIZE)
	    % lldev->tre_ring_size;
	spin_unlock_irqrestore(&lldev->lock, flags);
}

void hidma_ll_start(struct hidma_lldev *lldev)
{
	hidma_ll_hw_start(lldev);
}

/*
 * Note that even though we stop this channel
 * if there is a pending transaction in flight
 * it will complete and follow the callback.
 * This request will prevent further requests
 * to be made.
 */
int hidma_ll_pause(struct hidma_lldev *lldev)
{
	u32 val;
	int ret;

	val = readl(lldev->evca + EVCA_CTRLSTS_OFFSET);
	lldev->evch_state = HIDMA_CH_STATE(val);
	val = readl(lldev->trca + TRCA_CTRLSTS_OFFSET);
	lldev->trch_state = HIDMA_CH_STATE(val);

	/* already suspended by this OS */
	if ((lldev->trch_state == CH_SUSPENDED) ||
	    (lldev->evch_state == CH_SUSPENDED))
		return 0;

	/* already stopped by the manager */
	if ((lldev->trch_state == CH_STOPPED) ||
	    (lldev->evch_state == CH_STOPPED))
		return 0;

	val = readl(lldev->trca + TRCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_SUSPEND << 16;
	writel(val, lldev->trca + TRCA_CTRLSTS_OFFSET);

	/*
	 * Start the wait right after the suspend is confirmed.
	 * Do a polled read up to 1ms and 10ms maximum.
	 */
	ret = readl_poll_timeout(lldev->trca + TRCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_SUSPENDED, 1000,
				 10000);
	if (ret)
		return ret;

	val = readl(lldev->evca + EVCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_SUSPEND << 16;
	writel(val, lldev->evca + EVCA_CTRLSTS_OFFSET);

	/*
	 * Start the wait right after the suspend is confirmed
	 * Delay up to 10ms after reset to allow DMA logic to quiesce.
	 */
	ret = readl_poll_timeout(lldev->evca + EVCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_SUSPENDED, 1000,
				 10000);
	if (ret)
		return ret;

	lldev->trch_state = CH_SUSPENDED;
	lldev->evch_state = CH_SUSPENDED;
	return 0;
}

void hidma_ll_set_transfer_params(struct hidma_lldev *lldev, u32 tre_ch,
				  dma_addr_t src, dma_addr_t dest, u32 len,
				  u32 flags)
{
	struct hidma_tre *tre;
	u32 *tre_local;

	if (tre_ch >= lldev->nr_tres) {
		dev_err(lldev->dev,
			"invalid TRE number in transfer params:%d", tre_ch);
		return;
	}

	tre = &lldev->trepool[tre_ch];
	if (atomic_read(&tre->allocated) != true) {
		dev_err(lldev->dev,
			"trying to set params on an unused TRE:%d", tre_ch);
		return;
	}

	tre_local = &tre->tre_local[0];
	tre_local[TRE_LEN_IDX] = len;
	tre_local[TRE_SRC_LOW_IDX] = lower_32_bits(src);
	tre_local[TRE_SRC_HI_IDX] = upper_32_bits(src);
	tre_local[TRE_DEST_LOW_IDX] = lower_32_bits(dest);
	tre_local[TRE_DEST_HI_IDX] = upper_32_bits(dest);
	tre->int_flags = flags;
}

/*
 * Called during initialization and after an error condition
 * to restore hardware state.
 */
int hidma_ll_setup(struct hidma_lldev *lldev)
{
	int rc;
	u64 addr;
	u32 val;
	u32 nr_tres = lldev->nr_tres;

	lldev->pending_tre_count = 0;
	lldev->tre_processed_off = 0;
	lldev->evre_processed_off = 0;
	lldev->tre_write_offset = 0;

	/* disable interrupts */
	hidma_ll_enable_irq(lldev, 0);

	/* clear all pending interrupts */
	val = readl(lldev->evca + EVCA_IRQ_STAT_OFFSET);
	writel(val, lldev->evca + EVCA_IRQ_CLR_OFFSET);

	rc = hidma_ll_reset(lldev);
	if (rc)
		return rc;

	/*
	 * Clear all pending interrupts again.
	 * Otherwise, we observe reset complete interrupts.
	 */
	val = readl(lldev->evca + EVCA_IRQ_STAT_OFFSET);
	writel(val, lldev->evca + EVCA_IRQ_CLR_OFFSET);

	/* disable interrupts again after reset */
	hidma_ll_enable_irq(lldev, 0);

	addr = lldev->tre_ring_handle;
	writel(lower_32_bits(addr), lldev->trca + TRCA_RING_LOW_OFFSET);
	writel(upper_32_bits(addr), lldev->trca + TRCA_RING_HIGH_OFFSET);
	writel(lldev->tre_ring_size, lldev->trca + TRCA_RING_LEN_OFFSET);

	addr = lldev->evre_ring_handle;
	writel(lower_32_bits(addr), lldev->evca + EVCA_RING_LOW_OFFSET);
	writel(upper_32_bits(addr), lldev->evca + EVCA_RING_HIGH_OFFSET);
	writel(EVRE_SIZE * nr_tres, lldev->evca + EVCA_RING_LEN_OFFSET);

	/* support IRQ only for now */
	val = readl(lldev->evca + EVCA_INTCTRL_OFFSET);
	val &= ~0xF;
	val |= 0x1;
	writel(val, lldev->evca + EVCA_INTCTRL_OFFSET);

	/* clear all pending interrupts and enable them */
	writel(ENABLE_IRQS, lldev->evca + EVCA_IRQ_CLR_OFFSET);
	hidma_ll_enable_irq(lldev, ENABLE_IRQS);

	rc = hidma_ll_enable(lldev);
	if (rc)
		return rc;

	return rc;
}

struct hidma_lldev *hidma_ll_init(struct device *dev, u32 nr_tres,
				  void __iomem *trca, void __iomem *evca,
				  u8 chidx)
{
	u32 required_bytes;
	struct hidma_lldev *lldev;
	int rc;

	if (!trca || !evca || !dev || !nr_tres)
		return NULL;

	/* need at least four TREs */
	if (nr_tres < 4)
		return NULL;

	/* need an extra space */
	nr_tres += 1;

	lldev = devm_kzalloc(dev, sizeof(struct hidma_lldev), GFP_KERNEL);
	if (!lldev)
		return NULL;

	lldev->evca = evca;
	lldev->trca = trca;
	lldev->dev = dev;
	lldev->trepool = devm_kcalloc(lldev->dev, nr_tres,
				      sizeof(struct hidma_tre), GFP_KERNEL);
	if (!lldev->trepool)
		return NULL;

	required_bytes = sizeof(lldev->pending_tre_list[0]);
	lldev->pending_tre_list = devm_kcalloc(dev, nr_tres, required_bytes,
					       GFP_KERNEL);
	if (!lldev->pending_tre_list)
		return NULL;

	lldev->tx_status_list = devm_kcalloc(dev, nr_tres,
					     sizeof(lldev->tx_status_list[0]),
					     GFP_KERNEL);
	if (!lldev->tx_status_list)
		return NULL;

	lldev->tre_ring = dmam_alloc_coherent(dev, (TRE_SIZE + 1) * nr_tres,
					      &lldev->tre_ring_handle,
					      GFP_KERNEL);
	if (!lldev->tre_ring)
		return NULL;

	memset(lldev->tre_ring, 0, (TRE_SIZE + 1) * nr_tres);
	lldev->tre_ring_size = TRE_SIZE * nr_tres;
	lldev->nr_tres = nr_tres;

	/* the TRE ring has to be TRE_SIZE aligned */
	if (!IS_ALIGNED(lldev->tre_ring_handle, TRE_SIZE)) {
		u8 tre_ring_shift;

		tre_ring_shift = lldev->tre_ring_handle % TRE_SIZE;
		tre_ring_shift = TRE_SIZE - tre_ring_shift;
		lldev->tre_ring_handle += tre_ring_shift;
		lldev->tre_ring += tre_ring_shift;
	}

	lldev->evre_ring = dmam_alloc_coherent(dev, (EVRE_SIZE + 1) * nr_tres,
					       &lldev->evre_ring_handle,
					       GFP_KERNEL);
	if (!lldev->evre_ring)
		return NULL;

	memset(lldev->evre_ring, 0, (EVRE_SIZE + 1) * nr_tres);
	lldev->evre_ring_size = EVRE_SIZE * nr_tres;

	/* the EVRE ring has to be EVRE_SIZE aligned */
	if (!IS_ALIGNED(lldev->evre_ring_handle, EVRE_SIZE)) {
		u8 evre_ring_shift;

		evre_ring_shift = lldev->evre_ring_handle % EVRE_SIZE;
		evre_ring_shift = EVRE_SIZE - evre_ring_shift;
		lldev->evre_ring_handle += evre_ring_shift;
		lldev->evre_ring += evre_ring_shift;
	}
	lldev->nr_tres = nr_tres;
	lldev->chidx = chidx;

	rc = kfifo_alloc(&lldev->handoff_fifo,
			 nr_tres * sizeof(struct hidma_tre *), GFP_KERNEL);
	if (rc)
		return NULL;

	rc = hidma_ll_setup(lldev);
	if (rc)
		return NULL;

	spin_lock_init(&lldev->lock);
	tasklet_init(&lldev->task, hidma_ll_tre_complete, (unsigned long)lldev);
	lldev->initialized = 1;
	hidma_ll_enable_irq(lldev, ENABLE_IRQS);
	return lldev;
}

int hidma_ll_uninit(struct hidma_lldev *lldev)
{
	int rc = 0;
	u32 val;

	if (!lldev)
		return -ENODEV;

	if (lldev->initialized) {
		u32 required_bytes;

		lldev->initialized = 0;

		required_bytes = sizeof(struct hidma_tre) * lldev->nr_tres;
		tasklet_kill(&lldev->task);
		memset(lldev->trepool, 0, required_bytes);
		lldev->trepool = NULL;
		lldev->pending_tre_count = 0;
		lldev->tre_write_offset = 0;

		rc = hidma_ll_reset(lldev);

		/*
		 * Clear all pending interrupts again.
		 * Otherwise, we observe reset complete interrupts.
		 */
		val = readl(lldev->evca + EVCA_IRQ_STAT_OFFSET);
		writel(val, lldev->evca + EVCA_IRQ_CLR_OFFSET);
		hidma_ll_enable_irq(lldev, 0);
	}
	return rc;
}

irqreturn_t hidma_ll_inthandler(int chirq, void *arg)
{
	struct hidma_lldev *lldev = arg;

	hidma_ll_int_handler_internal(lldev);
	return IRQ_HANDLED;
}

enum dma_status hidma_ll_status(struct hidma_lldev *lldev, u32 tre_ch)
{
	enum dma_status ret = DMA_ERROR;
	unsigned long flags;
	u8 err_code;

	spin_lock_irqsave(&lldev->lock, flags);
	err_code = lldev->tx_status_list[tre_ch].err_code;

	if (err_code & EVRE_STATUS_COMPLETE)
		ret = DMA_COMPLETE;
	else if (err_code & EVRE_STATUS_ERROR)
		ret = DMA_ERROR;
	else
		ret = DMA_IN_PROGRESS;
	spin_unlock_irqrestore(&lldev->lock, flags);

	return ret;
}

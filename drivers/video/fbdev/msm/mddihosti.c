/* Copyright (c) 2008-2010, 2012 The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 Rudolf Tammekivi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include "mddihost.h"
#include "mddihosti.h"

#define MDDI_DEFAULT_TIMER_LENGTH	5000 /* 5 seconds */
#define MDDI_RTD_FREQ			60000 /* send RTD every 60 seconds */
#define MDDI_CLIENT_STATUS_FREQ		60000 /* get status pkt every 60 secs */

#define MDDI_DEFAULT_REV_PKT_SIZE		0x20

#ifndef FEATURE_MDDI_DISABLE_REVERSE
static bool mddi_rev_ptr_workaround = true;
#define MDDI_REG_READ_RETRY_MAX		20
static uint32_t mddi_reg_read_retry;
static bool mddi_enable_reg_read_retry = true;
static bool mddi_enable_reg_read_retry_once = false;

#define MDDI_MAX_REV_PKT_SIZE			0x60
#define MDDI_CLIENT_CAPABILITY_REV_PKT_SIZE	0x60
#define MDDI_VIDEO_REV_PKT_SIZE			0x40
#define MDDI_REV_BUFFER_SIZE			MDDI_MAX_REV_PKT_SIZE
static uint8_t rev_packet_data[MDDI_MAX_REV_PKT_SIZE];

static bool mddi_client_capability_request = false;

#define MAX_MDDI_REV_HANDLERS 2
#define INVALID_PKT_TYPE 0xFFFF

typedef void (*mddi_rev_handler_type) (void *);

typedef struct {
	mddi_rev_handler_type handler; /* ISR to be executed */
	uint16_t pkt_type;
} mddi_rev_pkt_handler_type;
static mddi_rev_pkt_handler_type mddi_rev_pkt_handler[MAX_MDDI_REV_HANDLERS] =
	{ {NULL, INVALID_PKT_TYPE}, {NULL, INVALID_PKT_TYPE} };

static bool mddi_rev_encap_user_request = false;
static mddi_linked_list_notify_type mddi_rev_user;

#endif /* FEATURE_MDDI_DISABLE_REVERSE */

#define MDDI_MAX_REV_DATA_SIZE  128

struct timer_list mddi_host_timer;
uint32_t mddi_host_core_version;
int32_t mddi_client_type;
uint32_t *mddi_reg_read_value_ptr;

static mddi_client_capability_type mddi_client_capability_pkt;
static spinlock_t mddi_host_spin_lock;

typedef enum {
	MDDI_REV_IDLE,
#ifndef FEATURE_MDDI_DISABLE_REVERSE
	MDDI_REV_REG_READ_ISSUED,
	MDDI_REV_REG_READ_SENT,
	MDDI_REV_ENCAP_ISSUED,
	MDDI_REV_STATUS_REQ_ISSUED,
	MDDI_REV_CLIENT_CAP_ISSUED
#endif
} mddi_rev_link_state_type;

typedef enum {
	MDDI_LINK_DISABLED,
	MDDI_LINK_HIBERNATING,
	MDDI_LINK_ACTIVATING,
	MDDI_LINK_ACTIVE
} mddi_host_link_state_type;

typedef enum {
	MDDI_DRIVER_RESET,	/* host core registers have not been written. */
	MDDI_DRIVER_DISABLED,	/* registers written, interrupts disabled. */
	MDDI_DRIVER_ENABLED	/* registers written, interrupts enabled. */
} mddi_host_driver_state_type;

typedef struct {
	uint32_t count;
	uint32_t in_count;
	uint32_t disp_req_count;
	uint32_t state_change_count;
	uint32_t ll_done_count;
	uint32_t rev_avail_count;
	uint32_t error_count;
	uint32_t rev_encap_count;
	uint32_t llist_ptr_write_1;
	uint32_t llist_ptr_write_2;
} mddi_host_int_type;

typedef struct {
	uint32_t fwd_crc_count;
	uint32_t rev_crc_count;
	uint32_t pri_underflow;
	uint32_t sec_underflow;
	uint32_t rev_overflow;
	uint32_t pri_overwrite;
	uint32_t sec_overwrite;
	uint32_t rev_overwrite;
	uint32_t dma_failure;
	uint32_t rtd_failure;
	uint32_t reg_read_failure;
#ifdef FEATURE_MDDI_UNDERRUN_RECOVERY
	uint32_t pri_underrun_detected;
#endif
} mddi_host_stat_type;

typedef struct {
	uint32_t rtd_cnt;
	uint32_t rev_enc_cnt;
	uint32_t vid_cnt;
	uint32_t reg_acc_cnt;
	uint32_t cli_stat_cnt;
	uint32_t cli_cap_cnt;
	uint32_t reg_read_cnt;
	uint32_t link_active_cnt;
	uint32_t link_hibernate_cnt;
	uint32_t vsync_response_cnt;
	uint32_t fwd_crc_cnt;
	uint32_t rev_crc_cnt;
} mddi_log_params_struct_type;

typedef struct {
	uint32_t rtd_value;
	uint32_t rtd_counter;
	uint32_t client_status_cnt;
	bool rev_ptr_written;
	uint8_t *rev_ptr_start;
	uint8_t *rev_ptr_curr;
	uint32_t mddi_rev_ptr_write_val;
	dma_addr_t rev_data_dma_addr;
	uint16_t rev_pkt_size;
	mddi_rev_link_state_type rev_state;
	mddi_host_link_state_type link_state;
	mddi_host_driver_state_type driver_state;
	bool disable_hibernation;
	uint32_t saved_int_reg;
	uint32_t saved_int_en;
	mddi_linked_list_type *llist_ptr;
	dma_addr_t llist_dma_addr;
	mddi_linked_list_type *llist_dma_ptr;
	uint32_t *rev_data_buf;
	struct completion mddi_llist_avail_comp;
	bool mddi_waiting_for_llist_avail;
	mddi_host_int_type int_type;
	mddi_host_stat_type stats;
	mddi_log_params_struct_type log_parms;
	mddi_llist_info_type llist_info;
	mddi_linked_list_notify_type llist_notify[MDDI_MAX_NUM_LLIST_ITEMS];
} mddi_host_cntl_type;
static mddi_host_cntl_type mhctl;

mddi_linked_list_type *llist_extern;
mddi_linked_list_type *llist_dma_extern;
mddi_linked_list_notify_type *llist_extern_notify;

static void mddi_report_errors(uint32_t int_reg)
{
	mddi_host_cntl_type *pmhctl = &mhctl;

	if (int_reg & MDDI_INT_PRI_UNDERFLOW) {
		pmhctl->stats.pri_underflow++;
		MDDI_MSG_ERR("!!! MDDI Primary Underflow !!!\n");
	}
	if (int_reg & MDDI_INT_SEC_UNDERFLOW) {
		pmhctl->stats.sec_underflow++;
		MDDI_MSG_ERR("!!! MDDI Secondary Underflow !!!\n");
	}
	if (int_reg & MDDI_INT_PRI_OVERWRITE) {
		pmhctl->stats.pri_overwrite++;
		MDDI_MSG_ERR("!!! MDDI Primary Overwrite !!!\n");
	}
	if (int_reg & MDDI_INT_SEC_OVERWRITE) {
		pmhctl->stats.sec_overwrite++;
		MDDI_MSG_ERR("!!! MDDI Secondary Overwrite !!!\n");
	}
#ifndef FEATURE_MDDI_DISABLE_REVERSE
	if (int_reg & MDDI_INT_REV_OVERFLOW) {
		pmhctl->stats.rev_overflow++;
		MDDI_MSG_ERR("!!! MDDI Reverse Overflow !!!\n");
		pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
		mddi_host_reg_out(REV_PTR, pmhctl->mddi_rev_ptr_write_val);

	}
	if (int_reg & MDDI_INT_CRC_ERROR) {
		MDDI_MSG_ERR("!!! MDDI Reverse CRC Error !!!\n");
	}
	if (int_reg & MDDI_INT_REV_OVERWRITE) {
		pmhctl->stats.rev_overwrite++;
		/* This will show up normally and is not a problem */
		MDDI_MSG_DEBUG("MDDI Reverse Overwrite!\n");
	}
	if (int_reg & MDDI_INT_RTD_FAILURE) {
		mddi_host_reg_outm(INTEN, MDDI_INT_RTD_FAILURE, 0);
		pmhctl->stats.rtd_failure++;
		MDDI_MSG_ERR("!!! MDDI RTD Failure !!!\n");
	}
#endif
	if (int_reg & MDDI_INT_DMA_FAILURE) {
		pmhctl->stats.dma_failure++;
		MDDI_MSG_ERR("!!! MDDI DMA Abort !!!\n");
	}
}

static void mddi_report_state_change(uint32_t int_reg)
{
	mddi_host_cntl_type *pmhctl = &mhctl;

	if ((pmhctl->saved_int_reg & MDDI_INT_IN_HIBERNATION) &&
	    (pmhctl->saved_int_reg & MDDI_INT_LINK_ACTIVE)) {
		/* recover from condition where the io_clock was turned off by
		 * the clock driver during a transition to hibernation. The
		 * io_clock disable is to prevent MDP/MDDI underruns when
		 * changing ARM clock speeds. In the process of halting the
		 * ARM, the hclk divider needs to be set to 1. When it is set
		 * to 1, there is a small time (usecs) when hclk is off or slow,
		 * and this can cause an underrun. To prevent the underrun,
		 * clock driver turns off the MDDI io_clock before making the
		 * change. */
		mddi_host_reg_out(CMD, MDDI_CMD_POWERUP);
	}

	if (int_reg & MDDI_INT_LINK_ACTIVE) {
		pmhctl->link_state = MDDI_LINK_ACTIVE;
		pmhctl->log_parms.link_active_cnt++;
		pmhctl->rtd_value = mddi_host_reg_in(RTD_VAL);
		MDDI_MSG_DEBUG("!!! MDDI Active RTD:0x%x!!!\n",
			       pmhctl->rtd_value);
		/* now interrupt on hibernation */
		mddi_host_reg_outm(INTEN,
				   MDDI_INT_IN_HIBERNATION |
				   MDDI_INT_LINK_ACTIVE,
				   MDDI_INT_IN_HIBERNATION);

#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (mddi_rev_ptr_workaround) {
			/* HW CR: need to reset reverse register stuff */
			pmhctl->rev_ptr_written = false;
			pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
		}
#endif
	}

	if (int_reg & MDDI_INT_IN_HIBERNATION) {
		pmhctl->link_state = MDDI_LINK_HIBERNATING;
		pmhctl->log_parms.link_hibernate_cnt++;
		MDDI_MSG_DEBUG("!!! MDDI Hibernating !!!\n");

		if (mddi_client_type == 2) {
			mddi_host_reg_out(PAD_CTL, 0x402a850f);
			mddi_host_reg_out(PAD_CAL, 0x10220020);
			mddi_host_reg_out(TA1_LEN, 0x0010);
			mddi_host_reg_out(TA2_LEN, 0x0040);
		}

		/* now interrupt on link_active */
#ifdef FEATURE_MDDI_DISABLE_REVERSE
		mddi_host_reg_outm(INTEN,
				   MDDI_INT_MDDI_IN | MDDI_INT_IN_HIBERNATION |
				   MDDI_INT_LINK_ACTIVE,
				   MDDI_INT_LINK_ACTIVE);
#else
		mddi_host_reg_outm(INTEN,
				   MDDI_INT_MDDI_IN | MDDI_INT_IN_HIBERNATION |
				   MDDI_INT_LINK_ACTIVE,
				   MDDI_INT_MDDI_IN | MDDI_INT_LINK_ACTIVE);

		pmhctl->rtd_counter = MDDI_RTD_FREQ;

		if (pmhctl->rev_state != MDDI_REV_IDLE) {
			/* a rev_encap will not wake up the link, so we do that
			   here */
			pmhctl->link_state = MDDI_LINK_ACTIVATING;
			mddi_host_reg_out(CMD, MDDI_CMD_LINK_ACTIVE);
		}
#endif

		if (pmhctl->disable_hibernation) {
			mddi_host_reg_out(CMD, MDDI_CMD_HIBERNATE);
			mddi_host_reg_out(CMD, MDDI_CMD_LINK_ACTIVE);
			pmhctl->link_state = MDDI_LINK_ACTIVATING;
		}

#ifdef FEATURE_MDDI_UNDERRUN_RECOVERY
		if (pmhctl->llist_info.transmitting_start_idx !=
		    UNASSIGNED_INDEX &&
		    ((pmhctl->saved_int_reg & (MDDI_INT_PRI_LINK_LIST_DONE |
		      MDDI_INT_PRI_PTR_READ)) == MDDI_INT_PRI_PTR_READ)) {
			mddi_linked_list_type *llist_dma;
			llist_dma = pmhctl->llist_dma_ptr;
			/*
			 * All indications are that we have not received a
			 * linked list done interrupt, due to an underrun
			 * condition. Recovery attempt is to send again.
			 */
			dmb();
			/* Write to primary pointer register again */
			mddi_host_reg_out(PRI_PTR,
					  &llist_dma[pmhctl->llist_info.
					  transmitting_start_idx]);
			pmhctl->stats.pri_underrun_detected++;
		}
#endif
	}
}

void mddi_host_timer_service(unsigned long data)
{
	unsigned long time_ms = MDDI_DEFAULT_TIMER_LENGTH;

#ifndef FEATURE_MDDI_DISABLE_REVERSE
	mddi_host_cntl_type *pmhctl = &mhctl;
	unsigned long flags;
	pmhctl->rtd_counter += time_ms;
	pmhctl->client_status_cnt += time_ms;

	if (pmhctl->client_status_cnt >= MDDI_CLIENT_STATUS_FREQ) {
		if (pmhctl->link_state == MDDI_LINK_HIBERNATING &&
		    pmhctl->client_status_cnt > MDDI_CLIENT_STATUS_FREQ) {
			/*
			 * special case where we are hibernating
			 * and mddi_host_isr is not firing, so
			 * kick the link so that the status can
			 * be retrieved
			 */

			/* need to wake up link before issuing
			 * rev encap command
			 */
			MDDI_MSG_INFO("wake up link!\n");
			spin_lock_irqsave(&mddi_host_spin_lock, flags);
			pmhctl->link_state = MDDI_LINK_ACTIVATING;
			mddi_host_reg_out(CMD, MDDI_CMD_LINK_ACTIVE);
			spin_unlock_irqrestore(&mddi_host_spin_lock, flags);
		} else if (pmhctl->link_state == MDDI_LINK_ACTIVE &&
			   pmhctl->disable_hibernation) {
			/*
			 * special case where we have disabled
					 * hibernation and mddi_host_isr
			 * is not firing, so enable interrupt
			 * for no pkts pending, which will
			 * generate an interrupt
			 */
			MDDI_MSG_INFO("kick isr!\n");
			spin_lock_irqsave(&mddi_host_spin_lock, flags);
			mddi_host_reg_outm(INTEN,
					   MDDI_INT_NO_CMD_PKTS_PEND,
					   MDDI_INT_NO_CMD_PKTS_PEND);
			spin_unlock_irqrestore(&mddi_host_spin_lock, flags);
		}
	}
#endif /* FEATURE_MDDI_DISABLE_REVERSE */

	mutex_lock(&mddi_timer_lock);
	init_timer(&mddi_host_timer);

	if (!mddi_timer_shutdown_flag) {
		mddi_host_timer.function = mddi_host_timer_service;
		mddi_host_timer.data = 0;
		mddi_host_timer.expires = jiffies + ((time_ms * HZ) / 1000);
		add_timer(&mddi_host_timer);
	}
	mutex_unlock(&mddi_timer_lock);

	return;
}

static void mddi_process_link_list_done(void)
{
	mddi_host_cntl_type *pmhctl = &mhctl;

	/* normal forward linked list packet(s) were sent */
	if (pmhctl->llist_info.transmitting_start_idx == UNASSIGNED_INDEX) {
		MDDI_MSG_ERR("**** getting LL done, but no list ****\n");
	} else {
		uint16_t idx;

#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (pmhctl->rev_state == MDDI_REV_REG_READ_ISSUED) {
			/* special case where a register read packet was sent */
			pmhctl->rev_state = MDDI_REV_REG_READ_SENT;
			if (pmhctl->llist_info.reg_read_idx == UNASSIGNED_INDEX) {
				MDDI_MSG_ERR
				    ("**** getting LL done, but no list ****\n");
			}
		}
#endif
		for (idx = pmhctl->llist_info.transmitting_start_idx;;) {
			uint16_t next_idx = pmhctl->llist_notify[idx].next_idx;
			/* with reg read we don't release the waiting tcb until after
			 * the reverse encapsulation has completed.
			 */
			if (idx != pmhctl->llist_info.reg_read_idx) {
				/* notify task that may be waiting on this completion */
				if (pmhctl->llist_notify[idx].waiting) {
					complete(&pmhctl->llist_notify[idx].
						  done_comp);
				}

				if (pmhctl->llist_notify[idx].done_cb)
					(*pmhctl->llist_notify[idx].done_cb)();

				pmhctl->llist_notify[idx].in_use = false;
				pmhctl->llist_notify[idx].waiting = false;
				pmhctl->llist_notify[idx].done_cb = NULL;
				if (idx < MDDI_NUM_DYNAMIC_LLIST_ITEMS) {
					/* static LLIST items are configured
					   only once */
					pmhctl->llist_notify[idx].next_idx =
						UNASSIGNED_INDEX;
				}
				/*
				 * currently, all linked list packets are
				 * register access, so we can increment the
				 * counter for that packet type here.
				 */
				pmhctl->log_parms.reg_acc_cnt++;
			}
			if (idx == pmhctl->llist_info.transmitting_end_idx)
				break;
			idx = next_idx;
			if (idx == UNASSIGNED_INDEX)
				MDDI_MSG_CRIT("MDDI linked list corruption!\n");
		}

		pmhctl->llist_info.transmitting_start_idx = UNASSIGNED_INDEX;
		pmhctl->llist_info.transmitting_end_idx = UNASSIGNED_INDEX;

		if (pmhctl->mddi_waiting_for_llist_avail) {
			if (!
			    (pmhctl->
			     llist_notify[pmhctl->llist_info.next_free_idx].
			     in_use)) {
				pmhctl->mddi_waiting_for_llist_avail = false;
				complete(&(pmhctl->mddi_llist_avail_comp));
			}
		}
	}

	/* Turn off MDDI_INT_PRI_LINK_LIST_DONE interrupt */
	mddi_host_reg_outm(INTEN, MDDI_INT_PRI_LINK_LIST_DONE, 0);

}

static void mddi_queue_forward_linked_list(void)
{
	uint16_t first_pkt_index;
	mddi_linked_list_type *llist_dma;
	mddi_host_cntl_type *pmhctl = &mhctl;
	llist_dma = pmhctl->llist_dma_ptr;

	first_pkt_index = UNASSIGNED_INDEX;

	if (pmhctl->llist_info.transmitting_start_idx == UNASSIGNED_INDEX) {
#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (pmhctl->llist_info.reg_read_waiting) {
			if (pmhctl->rev_state == MDDI_REV_IDLE) {
				/*
				 * we have a register read to send and
				 * can send it now
				 */
				pmhctl->rev_state = MDDI_REV_REG_READ_ISSUED;
				mddi_reg_read_retry = 0;
				first_pkt_index =
				    pmhctl->llist_info.waiting_start_idx;
				pmhctl->llist_info.reg_read_waiting = false;
			}
		} else
#endif
		{
			/*
			 * not register read to worry about, go ahead and write
			 * anything that may be on the waiting list.
			 */
			first_pkt_index = pmhctl->llist_info.waiting_start_idx;
		}
	}

	if (first_pkt_index != UNASSIGNED_INDEX) {
		pmhctl->llist_info.transmitting_start_idx =
		    pmhctl->llist_info.waiting_start_idx;
		pmhctl->llist_info.transmitting_end_idx =
		    pmhctl->llist_info.waiting_end_idx;
		pmhctl->llist_info.waiting_start_idx = UNASSIGNED_INDEX;
		pmhctl->llist_info.waiting_end_idx = UNASSIGNED_INDEX;

		/* write to the primary pointer register */
		MDDI_MSG_DEBUG("MDDI writing primary ptr with idx=%d\n",
			       first_pkt_index);

		pmhctl->int_type.llist_ptr_write_2++;

		dmb();
		mddi_host_reg_out(PRI_PTR, &llist_dma[first_pkt_index]);

		/* enable interrupt when complete */
		mddi_host_reg_outm(INTEN, MDDI_INT_PRI_LINK_LIST_DONE,
				   MDDI_INT_PRI_LINK_LIST_DONE);

	}

}

#ifndef FEATURE_MDDI_DISABLE_REVERSE
static void mddi_read_rev_packet(uint8_t *data_ptr)
{
	uint16_t i, length;
	mddi_host_cntl_type *pmhctl = &mhctl;

	uint8_t *rev_ptr_overflow =
		(pmhctl->rev_ptr_start + MDDI_REV_BUFFER_SIZE);

	/* first determine the length and handle invalid lengths */
	length = *pmhctl->rev_ptr_curr++;
	if (pmhctl->rev_ptr_curr >= rev_ptr_overflow)
		pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
	length |= ((*pmhctl->rev_ptr_curr++) << 8);
	if (pmhctl->rev_ptr_curr >= rev_ptr_overflow)
		pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
	if (length > (pmhctl->rev_pkt_size - 2)) {
		MDDI_MSG_ERR("Invalid rev pkt length %d\n", length);
		/* rev_pkt_size should always be <= rev_ptr_size so limit to
		 * packet size */
		length = pmhctl->rev_pkt_size - 2;
	}

	/* If the data pointer is NULL, just increment the pmhctl->rev_ptr_curr.
	 * Loop around if necessary. Don't bother reading the data.
	 */
	if (!data_ptr) {
		pmhctl->rev_ptr_curr += length;
		if (pmhctl->rev_ptr_curr >= rev_ptr_overflow)
			pmhctl->rev_ptr_curr -= MDDI_REV_BUFFER_SIZE;
		return;
	}

	data_ptr[0] = length & 0x0ff;
	data_ptr[1] = length >> 8;
	data_ptr += 2;
	/* copy the data to data_ptr uint8_t-at-a-time */
	for (i = 0; (i < length) && (pmhctl->rev_ptr_curr < rev_ptr_overflow);
	     i++)
		*data_ptr++ = *pmhctl->rev_ptr_curr++;
	if (pmhctl->rev_ptr_curr >= rev_ptr_overflow)
		pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
	for (; (i < length) && (pmhctl->rev_ptr_curr < rev_ptr_overflow); i++)
		*data_ptr++ = *pmhctl->rev_ptr_curr++;
}

static void mddi_process_rev_packets(void)
{
	uint32_t rev_packet_count;
	uint16_t i;
	uint32_t crc_errors;
	bool mddi_reg_read_successful = false;
	mddi_host_cntl_type *pmhctl = &mhctl;

	pmhctl->log_parms.rev_enc_cnt++;
	if ((pmhctl->rev_state != MDDI_REV_ENCAP_ISSUED) &&
	    (pmhctl->rev_state != MDDI_REV_STATUS_REQ_ISSUED) &&
	    (pmhctl->rev_state != MDDI_REV_CLIENT_CAP_ISSUED)) {
		MDDI_MSG_ERR("Wrong state %d for reverse int\n",
			     pmhctl->rev_state);
	}
	/* Turn off MDDI_INT_REV_AVAIL interrupt */
	mddi_host_reg_outm(INTEN, MDDI_INT_REV_DATA_AVAIL, 0);

	/* Clear rev data avail int */
	mddi_host_reg_out(INT, MDDI_INT_REV_DATA_AVAIL);

	/* Get Number of packets */
	rev_packet_count = mddi_host_reg_in(REV_PKT_CNT);

	if ((pmhctl->rev_state == MDDI_REV_CLIENT_CAP_ISSUED) &&
	    (rev_packet_count > 0) &&
	    (mddi_host_core_version == 0x28 ||
	     mddi_host_core_version == 0x30)) {

		uint32_t int_reg;
		uint32_t max_count = 0;

		mddi_host_reg_out(REV_PTR, pmhctl->mddi_rev_ptr_write_val);
		int_reg = mddi_host_reg_in(INT);
		while ((int_reg & 0x100000) == 0) {
			udelay(3);
			int_reg = mddi_host_reg_in(INT);
			if (++max_count > 100)
				break;
		}
	}

	/* Get CRC error count */
	crc_errors = mddi_host_reg_in(REV_CRC_ERR);
	if (crc_errors != 0) {
		pmhctl->log_parms.rev_crc_cnt += crc_errors;
		pmhctl->stats.rev_crc_count += crc_errors;
		MDDI_MSG_ERR("!!! MDDI %d Reverse CRC Error(s) !!!\n",
			     crc_errors);
		/* also issue an RTD to attempt recovery */
		pmhctl->rtd_counter = MDDI_RTD_FREQ;
	}

	pmhctl->rtd_value = mddi_host_reg_in(RTD_VAL);

	MDDI_MSG_DEBUG("MDDI rev pkt cnt=%d, ptr=0x%x, RTD:0x%x\n",
		       rev_packet_count,
		       pmhctl->rev_ptr_curr - pmhctl->rev_ptr_start,
		       pmhctl->rtd_value);

	if (!rev_packet_count) {
		MDDI_MSG_ERR("Reverse pkt sent, no data rxd\n");
		if (mddi_reg_read_value_ptr)
			*mddi_reg_read_value_ptr = -EBUSY;
	}

	/* order the reads */
	dmb();

	for (i = 0; i < rev_packet_count; i++) {
		mddi_rev_packet_type *rev_pkt_ptr;

		mddi_read_rev_packet(rev_packet_data);

		rev_pkt_ptr = (mddi_rev_packet_type *) rev_packet_data;

		if (rev_pkt_ptr->packet_length > pmhctl->rev_pkt_size) {
			MDDI_MSG_ERR("!!!invalid packet size: %d\n",
				     rev_pkt_ptr->packet_length);
		}

		MDDI_MSG_DEBUG("MDDI rev pkt 0x%x size 0x%x\n",
			       rev_pkt_ptr->packet_type,
			       rev_pkt_ptr->packet_length);

		/* Do whatever you want to do with the data based on the packet
		 * type */
		switch (rev_pkt_ptr->packet_type) {
		case 66: /* Client Capability */
			{
				mddi_client_capability_type
				    *client_capability_pkt_ptr;

				client_capability_pkt_ptr =
				    (mddi_client_capability_type *)
				    rev_packet_data;
				MDDI_MSG_NOTICE
				    ("Client Capability: Week=%d, Year=%d\n",
				     client_capability_pkt_ptr->
				     Week_of_Manufacture,
				     client_capability_pkt_ptr->
				     Year_of_Manufacture);
				memcpy((void *)&mddi_client_capability_pkt,
				       (void *)rev_packet_data,
				       sizeof(mddi_client_capability_type));
				pmhctl->log_parms.cli_cap_cnt++;
			}
			break;

		case 70: /* Display Status */
			{
				mddi_client_status_type *client_status_pkt_ptr;

				client_status_pkt_ptr =
				    (mddi_client_status_type *) rev_packet_data;
				if ((client_status_pkt_ptr->crc_error_count !=
				     0)
				    || (client_status_pkt_ptr->
					reverse_link_request != 0)) {
					MDDI_MSG_ERR
					    ("Client Status: RevReq=%d, CrcErr=%d\n",
					     client_status_pkt_ptr->
					     reverse_link_request,
					     client_status_pkt_ptr->
					     crc_error_count);
				} else {
					MDDI_MSG_DEBUG
					    ("Client Status: RevReq=%d, CrcErr=%d\n",
					     client_status_pkt_ptr->
					     reverse_link_request,
					     client_status_pkt_ptr->
					     crc_error_count);
				}
				pmhctl->log_parms.fwd_crc_cnt +=
				    client_status_pkt_ptr->crc_error_count;
				pmhctl->stats.fwd_crc_count +=
				    client_status_pkt_ptr->crc_error_count;
				pmhctl->log_parms.cli_stat_cnt++;
			}
			break;

		case 146: /* register access packet */
			{
				mddi_register_access_packet_type
				    * regacc_pkt_ptr;
				uint32_t data_count;

				regacc_pkt_ptr =
				    (mddi_register_access_packet_type *)
				    rev_packet_data;

				/* Bits[0:13] - read data count */
				data_count = regacc_pkt_ptr->read_write_info
					& 0x3FFF;
				MDDI_MSG_DEBUG("\n MDDI rev read: 0x%x",
					regacc_pkt_ptr->read_write_info);
				MDDI_MSG_DEBUG("Reg Acc parse reg=0x%x,"
					"value=0x%x\n", regacc_pkt_ptr->
					register_address, regacc_pkt_ptr->
					register_data_list[0]);

				/* Copy register value to location passed in */
				if (mddi_reg_read_value_ptr) {
				if (data_count && data_count <=
					MDDI_HOST_MAX_CLIENT_REG_IN_SAME_ADDR) {
					memcpy(mddi_reg_read_value_ptr,
						(void *)&regacc_pkt_ptr->
						register_data_list[0],
						data_count * 4);
					mddi_reg_read_successful = true;
					mddi_reg_read_value_ptr = NULL;
				}
				}
				pmhctl->log_parms.reg_read_cnt++;
			}
			break;

		case INVALID_PKT_TYPE:	/* 0xFFFF */
			MDDI_MSG_ERR("!!!INVALID_PKT_TYPE rcvd\n");
			break;

		default:	/* any other packet */
			{
				uint16_t hdlr;

				for (hdlr = 0; hdlr < MAX_MDDI_REV_HANDLERS;
				     hdlr++) {
					if (!mddi_rev_pkt_handler[hdlr].handler)
						continue;
					if (mddi_rev_pkt_handler[hdlr].
					    pkt_type ==
					    rev_pkt_ptr->packet_type) {
						(*(mddi_rev_pkt_handler[hdlr].
						  handler)) (rev_pkt_ptr);
						break;
					}
				}
				if (hdlr >= MAX_MDDI_REV_HANDLERS)
					MDDI_MSG_ERR("MDDI unknown rev pkt\n");
			}
			break;
		}
	}
	if ((pmhctl->rev_ptr_curr + pmhctl->rev_pkt_size) >=
	    (pmhctl->rev_ptr_start + MDDI_REV_BUFFER_SIZE)) {
		pmhctl->rev_ptr_written = false;
	}

	if (pmhctl->rev_state == MDDI_REV_ENCAP_ISSUED) {
		pmhctl->rev_state = MDDI_REV_IDLE;
		if (mddi_rev_user.waiting) {
			mddi_rev_user.waiting = false;
			complete(&(mddi_rev_user.done_comp));
		} else if (pmhctl->llist_info.reg_read_idx == UNASSIGNED_INDEX) {
			MDDI_MSG_ERR
			    ("Reverse Encap state, but no reg read in progress\n");
		} else {
			if ((!mddi_reg_read_successful) &&
			    (mddi_reg_read_retry < MDDI_REG_READ_RETRY_MAX) &&
			    (mddi_enable_reg_read_retry)) {
				/*
				 * There is a race condition that can happen
				 * where the reverse encapsulation message is
				 * sent out by the MDDI host before the register
				 * read packet is sent. As a work-around for
				 * that problem we issue the reverse
				 * encapsulation one more time before giving up.
				 */
				if (mddi_enable_reg_read_retry_once)
					mddi_reg_read_retry =
					    MDDI_REG_READ_RETRY_MAX;
				else
					mddi_reg_read_retry++;
				pmhctl->rev_state = MDDI_REV_REG_READ_SENT;
				pmhctl->stats.reg_read_failure++;
			} else {
				uint16_t reg_read_idx =
				    pmhctl->llist_info.reg_read_idx;

				mddi_reg_read_retry = 0;
				if (pmhctl->llist_notify[reg_read_idx].waiting) {
					complete(&
						 (pmhctl->
						  llist_notify[reg_read_idx].
						  done_comp));
				}
				pmhctl->llist_info.reg_read_idx =
				    UNASSIGNED_INDEX;
				if (pmhctl->llist_notify[reg_read_idx].
				    done_cb != NULL) {
					(*
					 (pmhctl->llist_notify[reg_read_idx].
					  done_cb)) ();
				}
				pmhctl->llist_notify[reg_read_idx].next_idx =
				    UNASSIGNED_INDEX;
				pmhctl->llist_notify[reg_read_idx].in_use =
				    false;
				pmhctl->llist_notify[reg_read_idx].waiting =
				    false;
				pmhctl->llist_notify[reg_read_idx].done_cb =
				    NULL;
				if (!mddi_reg_read_successful)
					pmhctl->stats.reg_read_failure++;
			}
		}
	} else if (pmhctl->rev_state == MDDI_REV_CLIENT_CAP_ISSUED) {
		if (mddi_host_core_version == 0x28 ||
		    mddi_host_core_version == 0x30) {
			mddi_host_reg_out(FIFO_ALLOC, 0x00);
			pmhctl->rev_ptr_written = true;
			mddi_host_reg_out(REV_PTR,
				pmhctl->mddi_rev_ptr_write_val);
			pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
			mddi_host_reg_out(CMD, 0xC00);
		}

		if (mddi_rev_user.waiting) {
			mddi_rev_user.waiting = false;
			complete(&(mddi_rev_user.done_comp));
		}
		pmhctl->rev_state = MDDI_REV_IDLE;
	} else {
		pmhctl->rev_state = MDDI_REV_IDLE;
	}

	/* pmhctl->rev_state = MDDI_REV_IDLE; */

	/* Re-enable interrupt */
	mddi_host_reg_outm(INTEN, MDDI_INT_REV_DATA_AVAIL,
			   MDDI_INT_REV_DATA_AVAIL);
}

static void mddi_issue_reverse_encapsulation(void)
{
	uint16_t i;
	mddi_host_cntl_type *pmhctl = &mhctl;
	/* Only issue a reverse encapsulation packet if:
	 * 1) another reverse is not in progress (MDDI_REV_IDLE).
	 * 2) a register read has been sent (MDDI_REV_REG_READ_SENT).
	 * 3) forward is not in progress, because of a hw bug in client that
	 *    causes forward crc errors on packet immediately after rev encap.
	 */
	if (((pmhctl->rev_state == MDDI_REV_IDLE) ||
	     (pmhctl->rev_state == MDDI_REV_REG_READ_SENT)) &&
	    (pmhctl->llist_info.transmitting_start_idx == UNASSIGNED_INDEX)) {
		uint32_t mddi_command = MDDI_CMD_SEND_REV_ENCAP;

		if ((pmhctl->rev_state == MDDI_REV_REG_READ_SENT) ||
		    (mddi_rev_encap_user_request == true)) {
			if (pmhctl->link_state == MDDI_LINK_HIBERNATING) {
				/* need to wake up link before issuing rev encap
				 * command */
				MDDI_MSG_DEBUG("wake up link!\n");
				pmhctl->link_state = MDDI_LINK_ACTIVATING;
				mddi_host_reg_out(CMD, MDDI_CMD_LINK_ACTIVE);
			} else {
				if (pmhctl->rtd_counter >= MDDI_RTD_FREQ) {
					MDDI_MSG_DEBUG
					    ("mddi sending RTD command!\n");
					mddi_host_reg_out(CMD,
							  MDDI_CMD_SEND_RTD);
					pmhctl->rtd_counter = 0;
					pmhctl->log_parms.rtd_cnt++;
				}
				if (pmhctl->rev_state != MDDI_REV_REG_READ_SENT) {
					/* this is generic reverse request by
					 * user, so reset the waiting flag. */
					mddi_rev_encap_user_request = false;
				}
				/* link is active so send reverse encap to get
				 * register read results */
				pmhctl->rev_state = MDDI_REV_ENCAP_ISSUED;
				mddi_command = MDDI_CMD_SEND_REV_ENCAP;
				MDDI_MSG_DEBUG("sending rev encap!\n");
			}
		} else if (pmhctl->client_status_cnt >= MDDI_CLIENT_STATUS_FREQ
			   || mddi_client_capability_request) {
			if (pmhctl->link_state == MDDI_LINK_HIBERNATING) {
				/* only wake up the link if it client status is
				 * overdue */
				if ((pmhctl->client_status_cnt >=
				     (MDDI_CLIENT_STATUS_FREQ * 2))
				    || mddi_client_capability_request) {
					/* need to wake up link before issuing
					 * rev encap command */
					MDDI_MSG_DEBUG("wake up link!\n");
					pmhctl->link_state =
					    MDDI_LINK_ACTIVATING;
					mddi_host_reg_out(CMD,
							  MDDI_CMD_LINK_ACTIVE);
				}
			} else {
				if (pmhctl->rtd_counter >= MDDI_RTD_FREQ) {
					MDDI_MSG_DEBUG
					    ("mddi sending RTD command!\n");
					mddi_host_reg_out(CMD,
							  MDDI_CMD_SEND_RTD);
					pmhctl->rtd_counter = 0;
					pmhctl->log_parms.rtd_cnt++;
				}
				/* periodically get client status */
				MDDI_MSG_DEBUG
				    ("mddi sending rev enc! (get status)\n");
				if (mddi_client_capability_request) {
					pmhctl->rev_state =
					    MDDI_REV_CLIENT_CAP_ISSUED;
					mddi_command = MDDI_CMD_GET_CLIENT_CAP;
					mddi_client_capability_request = false;
				} else {
					pmhctl->rev_state =
					    MDDI_REV_STATUS_REQ_ISSUED;
					pmhctl->client_status_cnt = 0;
					mddi_command =
					    MDDI_CMD_GET_CLIENT_STATUS;
				}
			}
		}
		if ((pmhctl->rev_state == MDDI_REV_ENCAP_ISSUED) ||
		    (pmhctl->rev_state == MDDI_REV_STATUS_REQ_ISSUED) ||
		    (pmhctl->rev_state == MDDI_REV_CLIENT_CAP_ISSUED)) {
			pmhctl->int_type.rev_encap_count++;
			if (!pmhctl->rev_ptr_written) {
				MDDI_MSG_DEBUG("writing reverse pointer!\n");
				pmhctl->rev_ptr_written = true;
				if ((pmhctl->rev_state ==
				     MDDI_REV_CLIENT_CAP_ISSUED) &&
				    (mddi_host_core_version == 0x28 ||
				     mddi_host_core_version == 0x30)) {
					pmhctl->rev_ptr_written = false;
					mddi_host_reg_out(FIFO_ALLOC, 0x02);
				} else
					mddi_host_reg_out(REV_PTR,
						  pmhctl->
						  mddi_rev_ptr_write_val);
			}

			for (i = 0; i < MDDI_MAX_REV_DATA_SIZE / 4; i++)
				pmhctl->rev_data_buf[i] = 0xdddddddd;

			/* send reverse encapsulation to get needed data */
			mddi_host_reg_out(CMD, mddi_command);
		}
	}

}

static void mddi_process_client_initiated_wakeup(void)
{
	/* Disable MDDI_INT Interrupt, we detect client initiated wakeup one
	 * time for each entry into hibernation */
	mddi_host_reg_outm(INTEN, MDDI_INT_MDDI_IN, 0);
}
#endif /* FEATURE_MDDI_DISABLE_REVERSE */

static void mddi_host_isr(void)
{
	uint32_t int_reg, int_en;
#ifndef FEATURE_MDDI_DISABLE_REVERSE
	uint32_t status_reg;
#endif
	mddi_host_cntl_type *pmhctl = &mhctl;

	int_reg = mddi_host_reg_in(INT);
	int_en = mddi_host_reg_in(INTEN);
	pmhctl->saved_int_reg = int_reg;
	pmhctl->saved_int_en = int_en;
	int_reg = int_reg & int_en;
	pmhctl->int_type.count++;


#ifndef FEATURE_MDDI_DISABLE_REVERSE
	status_reg = mddi_host_reg_in(STAT);

	if ((int_reg & MDDI_INT_MDDI_IN) ||
	    ((int_en & MDDI_INT_MDDI_IN) &&
	     ((int_reg == 0) || (status_reg & MDDI_STAT_CLIENT_WAKEUP_REQ)))) {
		/*
		 * The MDDI_IN condition will clear itself, and so it is
		 * possible that MDDI_IN was the reason for the isr firing,
		 * even though the interrupt register does not have the
		 * MDDI_IN bit set. To check if this was the case we need to
		 * look at the status register bit that signifies a client
		 * initiated wakeup. If the status register bit is set, as well
		 * as the MDDI_IN interrupt enabled, then we treat this as a
		 * client initiated wakeup.
		 */
		if (int_reg & MDDI_INT_MDDI_IN)
			pmhctl->int_type.in_count++;
		mddi_process_client_initiated_wakeup();
	}
#endif

	if (int_reg & MDDI_INT_LINK_STATE_CHANGES) {
		pmhctl->int_type.state_change_count++;
		mddi_report_state_change(int_reg);
	}

	if (int_reg & MDDI_INT_PRI_LINK_LIST_DONE) {
		pmhctl->int_type.ll_done_count++;
		mddi_process_link_list_done();
	}
#ifndef FEATURE_MDDI_DISABLE_REVERSE
	if (int_reg & MDDI_INT_REV_DATA_AVAIL) {
		pmhctl->int_type.rev_avail_count++;
		mddi_process_rev_packets();
	}
#endif

	if (int_reg & MDDI_INT_ERROR_CONDITIONS) {
		pmhctl->int_type.error_count++;
		mddi_report_errors(int_reg);

		mddi_host_reg_out(INT, int_reg & MDDI_INT_ERROR_CONDITIONS);
	}
#ifndef FEATURE_MDDI_DISABLE_REVERSE
	mddi_issue_reverse_encapsulation();

	if ((pmhctl->rev_state != MDDI_REV_ENCAP_ISSUED) &&
	    (pmhctl->rev_state != MDDI_REV_STATUS_REQ_ISSUED))
#endif
		/* don't want simultaneous reverse and forward with Eagle */
		mddi_queue_forward_linked_list();

	if (int_reg & MDDI_INT_NO_CMD_PKTS_PEND) {
		/* this interrupt is used to kick the isr when hibernation is
		   disabled */
		mddi_host_reg_outm(INTEN, MDDI_INT_NO_CMD_PKTS_PEND, 0);
	}
}

void mddi_host_isr_primary(void)
{
	mddi_host_isr();
}
EXPORT_SYMBOL(mddi_host_isr_primary);

static void mddi_host_initialize_registers(void)
{
	uint32_t pad_reg_val;
	mddi_host_cntl_type *pmhctl = &mhctl;

	if (pmhctl->driver_state == MDDI_DRIVER_ENABLED)
		return;

	/* MDDI Reset command */
	mddi_host_reg_out(CMD, MDDI_CMD_RESET);

	/* Version register (= 0x01) */
	mddi_host_reg_out(VERSION, 0x0001);

	/* Bytes per subframe register */
	mddi_host_reg_out(BPS, MDDI_HOST_BYTES_PER_SUBFRAME);

	/* Subframes per media frames register (= 0x03) */
	mddi_host_reg_out(SPM, 0x0003);

	/* Turn Around 1 register (= 0x05) */
	mddi_host_reg_out(TA1_LEN, 0x0005);

	/* Turn Around 2 register (= 0x0C) */
	mddi_host_reg_out(TA2_LEN, MDDI_HOST_TA2_LEN);

	/* Drive hi register (= 0x96) */
	mddi_host_reg_out(DRIVE_HI, 0x0096);

	/* Drive lo register (= 0x32) */
	mddi_host_reg_out(DRIVE_LO, 0x0032);

	/* Display wakeup count register (= 0x3c) */
	mddi_host_reg_out(DISP_WAKE, 0x003c);

	/* Reverse Rate Divisor register (= 0x2) */
	mddi_host_reg_out(REV_RATE_DIV, MDDI_HOST_REV_RATE_DIV);

#ifndef FEATURE_MDDI_DISABLE_REVERSE
	/* Reverse Pointer Size */
	mddi_host_reg_out(REV_SIZE, MDDI_REV_BUFFER_SIZE);

	/* Rev Encap Size */
	mddi_host_reg_out(REV_ENCAP_SZ, pmhctl->rev_pkt_size);
#endif

	/* Periodic Rev Encap */
	/* don't send periodically */
	mddi_host_reg_out(CMD, MDDI_CMD_PERIODIC_REV_ENCAP);

	pad_reg_val = mddi_host_reg_in(PAD_CTL);
	if (pad_reg_val == 0) {
		/* If we are turning on band gap, need to wait 5us before turning
		 * on the rest of the PAD */
		mddi_host_reg_out(PAD_CTL, 0x08000);
		udelay(5);
	}
	/* Recommendation from PAD hw team */
	mddi_host_reg_out(PAD_CTL, 0xa850f);

	pad_reg_val = 0x00220020;

	mddi_host_reg_out(PAD_IO_CTL, 0x00320000);
	mddi_host_reg_out(PAD_CAL, pad_reg_val);

	mddi_host_core_version = mddi_host_reg_inm(CORE_VER, 0xffff);

#ifndef FEATURE_MDDI_DISABLE_REVERSE
	if (mddi_host_core_version >= 8)
		mddi_rev_ptr_workaround = false;
	pmhctl->rev_ptr_curr = pmhctl->rev_ptr_start;
#endif

	if ((mddi_host_core_version > 8) && (mddi_host_core_version < 0x19))
		mddi_host_reg_out(TEST, 0x2);

	/* Need an even number for counts */
	mddi_host_reg_out(DRIVER_START_CNT, 0x60006);

	/* automatically hibernate after 1 empty subframe */
	if (pmhctl->disable_hibernation)
		mddi_host_reg_out(CMD, MDDI_CMD_HIBERNATE);
	else
		mddi_host_reg_out(CMD, MDDI_CMD_HIBERNATE | 1);

	/* Bring up link if display (client) requests it */
	mddi_host_reg_out(CMD, MDDI_CMD_DISP_IGNORE);
}

void mddi_host_configure_interrupts(bool enable)
{
	unsigned long flags;
	mddi_host_cntl_type *pmhctl = &mhctl;

	spin_lock_irqsave(&mddi_host_spin_lock, flags);

	/* Clear MDDI Interrupt enable reg */
	mddi_host_reg_out(INTEN, 0);

	spin_unlock_irqrestore(&mddi_host_spin_lock, flags);

	if (enable) {
		pmhctl->driver_state = MDDI_DRIVER_ENABLED;

		/* Set MDDI Interrupt enable reg -- Enable Reverse data avail */
#ifdef FEATURE_MDDI_DISABLE_REVERSE
		mddi_host_reg_out(INTEN,
				  MDDI_INT_ERROR_CONDITIONS |
				  MDDI_INT_LINK_STATE_CHANGES);
#else
		/* Reverse Pointer register */
		pmhctl->rev_ptr_written = false;

		mddi_host_reg_out(INTEN,
				  MDDI_INT_REV_DATA_AVAIL |
				  MDDI_INT_ERROR_CONDITIONS |
				  MDDI_INT_LINK_STATE_CHANGES);
		pmhctl->rtd_counter = MDDI_RTD_FREQ;
		pmhctl->client_status_cnt = 0;
#endif
	} else {
		if (pmhctl->driver_state == MDDI_DRIVER_ENABLED)
			pmhctl->driver_state = MDDI_DRIVER_DISABLED;
	}

}

/*
 * mddi_host_client_cnt_reset:
 * reset client_status_cnt to 0 to make sure host does not
 * send RTD cmd to client right after resume before mddi
 * client be powered up. this fix "MDDI RTD Failure" problem
 */
void mddi_host_client_cnt_reset(void)
{
	unsigned long flags;
	mddi_host_cntl_type *pmhctl;

	pmhctl = &mhctl;
	spin_lock_irqsave(&mddi_host_spin_lock, flags);
	pmhctl->client_status_cnt = 0;
	spin_unlock_irqrestore(&mddi_host_spin_lock, flags);
}

static void mddi_host_powerup(void)
{
	mddi_host_cntl_type *pmhctl = &mhctl;

	if (pmhctl->link_state != MDDI_LINK_DISABLED)
		return;

	mddi_host_initialize_registers();
	mddi_host_configure_interrupts(true);

	pmhctl->link_state = MDDI_LINK_ACTIVATING;

	/* Link activate command */
	mddi_host_reg_out(CMD, MDDI_CMD_LINK_ACTIVE);

	MDDI_MSG_NOTICE("MDDI Host: Activating Link\n");

	/* Initialize the timer */
	/* TODO: ONLY PRIM */
	mddi_host_timer_service(0);
}

/* Write out the MDDI configuration registers */
void mddi_host_init(void)
{
	mddi_host_cntl_type *pmhctl;

	uint16_t idx;

	pmhctl = &mhctl;

	pmhctl->llist_ptr = dma_alloc_coherent(NULL, MDDI_LLIST_POOL_SIZE,
					       &(pmhctl->llist_dma_addr),
					       GFP_KERNEL);
	pmhctl->llist_dma_ptr = (void *)pmhctl->llist_dma_addr;

#ifdef FEATURE_MDDI_DISABLE_REVERSE
	pmhctl->rev_data_buf = NULL;
	if (!pmhctl->llist_ptr) {
		MDDI_MSG_CRIT("unable to alloc non-cached memory\n");
	}
#else
	mddi_rev_user.waiting = false;
	init_completion(&mddi_rev_user.done_comp);
	pmhctl->rev_data_buf = dma_alloc_coherent(NULL, MDDI_MAX_REV_DATA_SIZE,
						  &(pmhctl->rev_data_dma_addr),
						  GFP_KERNEL);
	if (!pmhctl->llist_ptr || !pmhctl->rev_data_buf) {
		MDDI_MSG_CRIT("unable to alloc non-cached memory\n");
	}
#endif

	llist_extern = pmhctl->llist_ptr;
	llist_dma_extern = pmhctl->llist_dma_ptr;
	llist_extern_notify = pmhctl->llist_notify;

	for (idx = 0; idx < UNASSIGNED_INDEX; idx++)
		init_completion(&pmhctl->llist_notify[idx].done_comp);

	init_completion(&(pmhctl->mddi_llist_avail_comp));
	spin_lock_init(&mddi_host_spin_lock);
	pmhctl->mddi_waiting_for_llist_avail = false;
	pmhctl->mddi_rev_ptr_write_val = pmhctl->rev_data_dma_addr;
	pmhctl->rev_ptr_start = (void*)pmhctl->rev_data_buf;

	pmhctl->rev_pkt_size = MDDI_DEFAULT_REV_PKT_SIZE;
	pmhctl->rev_state = MDDI_REV_IDLE;
	pmhctl->link_state = MDDI_LINK_DISABLED;
	pmhctl->driver_state = MDDI_DRIVER_DISABLED;
	pmhctl->disable_hibernation = false;

	/* initialize llist variables */
	pmhctl->llist_info.transmitting_start_idx = UNASSIGNED_INDEX;
	pmhctl->llist_info.transmitting_end_idx = UNASSIGNED_INDEX;
	pmhctl->llist_info.waiting_start_idx = UNASSIGNED_INDEX;
	pmhctl->llist_info.waiting_end_idx = UNASSIGNED_INDEX;
	pmhctl->llist_info.reg_read_idx = UNASSIGNED_INDEX;
	pmhctl->llist_info.next_free_idx = MDDI_FIRST_DYNAMIC_LLIST_IDX;
	pmhctl->llist_info.reg_read_waiting = false;

	pmhctl->int_type.count = 0;
	pmhctl->int_type.in_count = 0;
	pmhctl->int_type.disp_req_count = 0;
	pmhctl->int_type.state_change_count = 0;
	pmhctl->int_type.ll_done_count = 0;
	pmhctl->int_type.rev_avail_count = 0;
	pmhctl->int_type.error_count = 0;
	pmhctl->int_type.rev_encap_count = 0;
	pmhctl->int_type.llist_ptr_write_1 = 0;
	pmhctl->int_type.llist_ptr_write_2 = 0;

	pmhctl->stats.fwd_crc_count = 0;
	pmhctl->stats.rev_crc_count = 0;
	pmhctl->stats.pri_underflow = 0;
	pmhctl->stats.sec_underflow = 0;
	pmhctl->stats.rev_overflow = 0;
	pmhctl->stats.pri_overwrite = 0;
	pmhctl->stats.sec_overwrite = 0;
	pmhctl->stats.rev_overwrite = 0;
	pmhctl->stats.dma_failure = 0;
	pmhctl->stats.rtd_failure = 0;
	pmhctl->stats.reg_read_failure = 0;
#ifdef FEATURE_MDDI_UNDERRUN_RECOVERY
	pmhctl->stats.pri_underrun_detected = 0;
#endif

	pmhctl->log_parms.rtd_cnt = 0;
	pmhctl->log_parms.rev_enc_cnt = 0;
	pmhctl->log_parms.vid_cnt = 0;
	pmhctl->log_parms.reg_acc_cnt = 0;
	pmhctl->log_parms.cli_stat_cnt = 0;
	pmhctl->log_parms.cli_cap_cnt = 0;
	pmhctl->log_parms.reg_read_cnt = 0;
	pmhctl->log_parms.link_active_cnt = 0;
	pmhctl->log_parms.link_hibernate_cnt = 0;
	pmhctl->log_parms.fwd_crc_cnt = 0;
	pmhctl->log_parms.rev_crc_cnt = 0;
	pmhctl->log_parms.vsync_response_cnt = 0;

	mddi_client_capability_pkt.packet_length = 0;

	mddi_host_powerup();
}

#ifndef FEATURE_MDDI_DISABLE_REVERSE
uint32_t mddi_get_client_id(void)
{
	static bool client_detection_try = false;
	static uint32_t mddi_client_id = 0;
	mddi_host_cntl_type *pmhctl;
	unsigned long flags;
	uint16_t saved_rev_pkt_size;
	int ret;

	if (!client_detection_try) {
		/* Toshiba display requires larger drive_lo value */
		mddi_host_reg_out(DRIVE_LO, 0x0050);

		pmhctl = &mhctl;

		saved_rev_pkt_size = pmhctl->rev_pkt_size;

		/* Increase Rev Encap Size */
		pmhctl->rev_pkt_size = MDDI_CLIENT_CAPABILITY_REV_PKT_SIZE;
		mddi_host_reg_out(REV_ENCAP_SZ, pmhctl->rev_pkt_size);

		/* disable hibernation temporarily */
		if (!pmhctl->disable_hibernation)
			mddi_host_reg_out(CMD, MDDI_CMD_HIBERNATE);

		mddi_rev_user.waiting = true;
		reinit_completion(&mddi_rev_user.done_comp);

		spin_lock_irqsave(&mddi_host_spin_lock, flags);

		mddi_client_capability_request = true;

		if (pmhctl->rev_state == MDDI_REV_IDLE) {
			/* attempt to send the reverse encapsulation now */
			mddi_issue_reverse_encapsulation();
		}
		spin_unlock_irqrestore(&mddi_host_spin_lock, flags);

		wait_for_completion_killable(&(mddi_rev_user.done_comp));

		/* Set Rev Encap Size back to its original value */
		pmhctl->rev_pkt_size = saved_rev_pkt_size;
		mddi_host_reg_out(REV_ENCAP_SZ, pmhctl->rev_pkt_size);

		/* reenable auto-hibernate */
		if (!pmhctl->disable_hibernation)
			mddi_host_reg_out(CMD, MDDI_CMD_HIBERNATE | 1);

		mddi_host_reg_out(DRIVE_LO, 0x0032);
		client_detection_try = true;

		mddi_client_id =
			mddi_client_capability_pkt.Mfr_Name << 16 |
			mddi_client_capability_pkt.Product_Code;

		if (!mddi_client_id)
			mddi_disable(1);

		ret = mddi_client_power(mddi_client_id);
		if (ret < 0)
			MDDI_MSG_ERR("mddi_client_power return %d", ret);
	}
	return mddi_client_id;
}
#endif

uint16_t mddi_get_next_free_llist_item(bool wait)
{
	unsigned long flags;
	uint16_t ret_idx;
	bool forced_wait = false;
	mddi_host_cntl_type *pmhctl = &mhctl;

	ret_idx = pmhctl->llist_info.next_free_idx;

	pmhctl->llist_info.next_free_idx++;
	if (pmhctl->llist_info.next_free_idx >= MDDI_NUM_DYNAMIC_LLIST_ITEMS)
		pmhctl->llist_info.next_free_idx = MDDI_FIRST_DYNAMIC_LLIST_IDX;
	spin_lock_irqsave(&mddi_host_spin_lock, flags);
	if (pmhctl->llist_notify[ret_idx].in_use) {
		if (!wait) {
			pmhctl->llist_info.next_free_idx = ret_idx;
			ret_idx = UNASSIGNED_INDEX;
		} else {
			forced_wait = true;
			reinit_completion(&pmhctl->mddi_llist_avail_comp);
		}
	}
	spin_unlock_irqrestore(&mddi_host_spin_lock, flags);

	if (forced_wait) {
		wait_for_completion_killable(&pmhctl->mddi_llist_avail_comp);
		MDDI_MSG_ERR("task waiting on mddi llist item\n");
	}

	if (ret_idx != UNASSIGNED_INDEX) {
		pmhctl->llist_notify[ret_idx].waiting = false;
		pmhctl->llist_notify[ret_idx].done_cb = NULL;
		pmhctl->llist_notify[ret_idx].in_use = true;
		pmhctl->llist_notify[ret_idx].next_idx = UNASSIGNED_INDEX;
	}

	return ret_idx;
}

uint16_t mddi_get_reg_read_llist_item(bool wait)
{
#ifdef FEATURE_MDDI_DISABLE_REVERSE
	MDDI_MSG_CRIT("No reverse link available\n");
	return false;
#else
	unsigned long flags;
	uint16_t ret_idx;
	bool error = false;
	mddi_host_cntl_type *pmhctl = &mhctl;

	spin_lock_irqsave(&mddi_host_spin_lock, flags);
	if (pmhctl->llist_info.reg_read_idx != UNASSIGNED_INDEX) {
		/* need to block here or is this an error condition? */
		error = true;
		ret_idx = UNASSIGNED_INDEX;
	}
	spin_unlock_irqrestore(&mddi_host_spin_lock, flags);

	if (!error) {
		ret_idx = pmhctl->llist_info.reg_read_idx =
			mddi_get_next_free_llist_item(wait);
		/* clear the reg_read_waiting flag */
		pmhctl->llist_info.reg_read_waiting = false;
	}

	if (error)
		MDDI_MSG_ERR("***** Reg read still in progress! ****\n");
	return ret_idx;
#endif
}

void mddi_queue_forward_packets(uint16_t first_llist_idx,
				uint16_t last_llist_idx,
				bool wait,
				mddi_llist_done_cb_type llist_done_cb)
{
	unsigned long flags;
	mddi_linked_list_type *llist;
	mddi_linked_list_type *llist_dma;
	mddi_host_cntl_type *pmhctl = &mhctl;

	if ((first_llist_idx >= UNASSIGNED_INDEX) ||
	    (last_llist_idx >= UNASSIGNED_INDEX)) {
		MDDI_MSG_ERR("MDDI queueing invalid linked list\n");
		return;
	}

	if (pmhctl->link_state == MDDI_LINK_DISABLED)
		MDDI_MSG_CRIT("MDDI host powered down!\n");

	llist = pmhctl->llist_ptr;
	llist_dma = pmhctl->llist_dma_ptr;

	pmhctl->llist_notify[last_llist_idx].waiting = wait;
	if (wait)
		reinit_completion(&pmhctl->llist_notify[last_llist_idx].done_comp);
	pmhctl->llist_notify[last_llist_idx].done_cb = llist_done_cb;

	spin_lock_irqsave(&mddi_host_spin_lock, flags);

	if ((pmhctl->llist_info.transmitting_start_idx == UNASSIGNED_INDEX) &&
	    (pmhctl->llist_info.waiting_start_idx == UNASSIGNED_INDEX) &&
	    (pmhctl->rev_state == MDDI_REV_IDLE)) {
		/* no packets are currently transmitting */
#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (first_llist_idx == pmhctl->llist_info.reg_read_idx) {
			/* This is the special case where the packet is a
			 * register read. */
			pmhctl->rev_state = MDDI_REV_REG_READ_ISSUED;
			mddi_reg_read_retry = 0;
		}
#endif
		/* assign transmitting index values */
		pmhctl->llist_info.transmitting_start_idx = first_llist_idx;
		pmhctl->llist_info.transmitting_end_idx = last_llist_idx;

		pmhctl->int_type.llist_ptr_write_1++;
		/* Write to primary pointer register */
		dmb();
		mddi_host_reg_out(PRI_PTR, &llist_dma[first_llist_idx]);

		/* enable interrupt when complete */
		mddi_host_reg_outm(INTEN, MDDI_INT_PRI_LINK_LIST_DONE,
				   MDDI_INT_PRI_LINK_LIST_DONE);

	} else if (pmhctl->llist_info.waiting_start_idx == UNASSIGNED_INDEX) {
#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (first_llist_idx == pmhctl->llist_info.reg_read_idx) {
			/*
			 * we have a register read to send but need to wait
			 * for current reverse activity to end or there are
			 * packets currently transmitting
			 */
			pmhctl->llist_info.reg_read_waiting = true;
		}
#endif

		/* assign waiting index values */
		pmhctl->llist_info.waiting_start_idx = first_llist_idx;
		pmhctl->llist_info.waiting_end_idx = last_llist_idx;
	} else {
		uint16_t prev_end_idx = pmhctl->llist_info.waiting_end_idx;
#ifndef FEATURE_MDDI_DISABLE_REVERSE
		if (first_llist_idx == pmhctl->llist_info.reg_read_idx) {
			/*
			 * we have a register read to send but need to wait
			 * for current reverse activity to end or there are
			 * packets currently transmitting
			 */
			pmhctl->llist_info.reg_read_waiting = true;
		}
#endif

		llist = pmhctl->llist_ptr;

		/* clear end flag in previous last packet */
		llist[prev_end_idx].link_controller_flags = 0;
		pmhctl->llist_notify[prev_end_idx].next_idx = first_llist_idx;

		/* set the next_packet_pointer of the previous last packet */
		llist[prev_end_idx].next_packet_pointer =
			(void *)(&llist_dma[first_llist_idx]);

		/* assign new waiting last index value */
		pmhctl->llist_info.waiting_end_idx = last_llist_idx;
	}

	spin_unlock_irqrestore(&mddi_host_spin_lock, flags);
}

/* Copyright (c) 2008-2009, The Linux Foundation. All rights reserved.
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

#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#include "mddihost.h"
#include "mddihosti.h"

static struct semaphore mddi_host_mutex;
extern uint32_t *mddi_reg_read_value_ptr;

void mddi_init(void)
{
	sema_init(&mddi_host_mutex, 1);

	down(&mddi_host_mutex);
	mddi_host_init();
	up(&mddi_host_mutex);
	mdelay(10);
}

int mddi_host_register_read(uint32_t reg_addr, uint32_t *reg_value_ptr,
			    bool wait)
{
	mddi_linked_list_type *curr_llist_ptr;
	mddi_register_access_packet_type *regacc_pkt_ptr;
	uint16_t curr_llist_idx;
	int ret = 0;

	if (in_interrupt())
		MDDI_MSG_CRIT("Called from ISR context\n");

	down(&mddi_host_mutex);

	mddi_reg_read_value_ptr = reg_value_ptr;
	curr_llist_idx = mddi_get_reg_read_llist_item(true);
	if (curr_llist_idx == UNASSIGNED_INDEX) {
		up(&mddi_host_mutex);

		/* need to change this to some sort of wait */
		MDDI_MSG_ERR("Attempting to queue up more than 1 reg read\n");
		return -EINVAL;
	}

	curr_llist_ptr = &llist_extern[curr_llist_idx];
	curr_llist_ptr->link_controller_flags = 0x11;
	curr_llist_ptr->packet_header_count = 14;
	curr_llist_ptr->packet_data_count = 0;

	curr_llist_ptr->next_packet_pointer = NULL;
	curr_llist_ptr->packet_data_pointer = NULL;
	curr_llist_ptr->reserved = 0;

	regacc_pkt_ptr = &curr_llist_ptr->packet_header.register_pkt;

	regacc_pkt_ptr->packet_length = curr_llist_ptr->packet_header_count;
	regacc_pkt_ptr->packet_type = 146; /* register access packet */
	regacc_pkt_ptr->bClient_ID = 0;
	regacc_pkt_ptr->read_write_info = 0x8001;
	regacc_pkt_ptr->register_address = reg_addr;

	/* now adjust pointers */
	mddi_queue_forward_packets(curr_llist_idx, curr_llist_idx, wait, NULL);
	up(&mddi_host_mutex);

	if (wait) {
		int wait_ret;

		mddi_linked_list_notify_type *llist_notify_ptr;
		llist_notify_ptr = &llist_extern_notify[curr_llist_idx];
		wait_ret = wait_for_completion_timeout(
					&llist_notify_ptr->done_comp, 5 * HZ);

		if (wait_ret <= 0)
			ret = -EBUSY;

		if (wait_ret < 0)
			MDDI_MSG_ERR("%s: failed to wait for completion!\n",
				__func__);
		else if (!wait_ret)
			MDDI_MSG_ERR("%s: Timed out waiting!\n", __func__);

		if (!ret && (mddi_reg_read_value_ptr == reg_value_ptr) &&
			(*reg_value_ptr == -EBUSY)) {
			MDDI_MSG_ERR("%s - failed to get data from client",
				   __func__);
			mddi_reg_read_value_ptr = NULL;
			ret = -EBUSY;
		}
	}

	MDDI_MSG_DEBUG("Reg Read value=0x%x\n", *reg_value_ptr);

	return ret;
}

int mddi_host_register_write(uint32_t reg_addr, uint32_t reg_val,
			     enum mddi_data_packet_size_type packet_size,
			     bool wait, mddi_llist_done_cb_type done_cb)
{
	mddi_linked_list_type *curr_llist_ptr;
	mddi_linked_list_type *curr_llist_dma_ptr;
	mddi_register_access_packet_type *regacc_pkt_ptr;
	uint16_t curr_llist_idx;
	int ret = 0;

	if (in_interrupt())
		MDDI_MSG_CRIT("Called from ISR context\n");

	down(&mddi_host_mutex);

	curr_llist_idx = mddi_get_next_free_llist_item(true);
	curr_llist_ptr = &llist_extern[curr_llist_idx];
	curr_llist_dma_ptr = &llist_dma_extern[curr_llist_idx];

	curr_llist_ptr->link_controller_flags = 1;
	curr_llist_ptr->packet_header_count = 14;
	curr_llist_ptr->packet_data_count = 4;

	curr_llist_ptr->next_packet_pointer = NULL;
	curr_llist_ptr->reserved = 0;

	regacc_pkt_ptr = &curr_llist_ptr->packet_header.register_pkt;

	regacc_pkt_ptr->packet_length =
		curr_llist_ptr->packet_header_count + packet_size;
	regacc_pkt_ptr->packet_type = 146; /* register access packet */
	regacc_pkt_ptr->bClient_ID = 0;
	regacc_pkt_ptr->read_write_info = 0x0001;
	regacc_pkt_ptr->register_address = reg_addr;
	regacc_pkt_ptr->register_data_list[0] = reg_val;

	MDDI_MSG_DEBUG("Reg Access write reg=0x%x, value=0x%x\n",
		       regacc_pkt_ptr->register_address,
		       regacc_pkt_ptr->register_data_list[0]);

	regacc_pkt_ptr = &curr_llist_dma_ptr->packet_header.register_pkt;
	curr_llist_ptr->packet_data_pointer =
		(&regacc_pkt_ptr->register_data_list[0]);

	/* now adjust pointers */
	mddi_queue_forward_packets(curr_llist_idx, curr_llist_idx, wait,
				   done_cb);
	up(&mddi_host_mutex);

	if (wait) {
		int wait_ret;

		mddi_linked_list_notify_type *llist_notify_ptr;
		llist_notify_ptr = &llist_extern_notify[curr_llist_idx];
		wait_ret = wait_for_completion_timeout(
					&llist_notify_ptr->done_comp, 5 * HZ);

		if (wait_ret <= 0)
			ret = -EBUSY;

		if (wait_ret < 0)
			MDDI_MSG_ERR("%s: failed to wait for completion!\n",
				__func__);
		else if (!wait_ret)
			MDDI_MSG_ERR("%s: Timed out waiting!\n", __func__);
	}

	return ret;
}

bool mddi_host_register_read_int(uint32_t reg_addr, uint32_t *reg_value_ptr)
{
	mddi_linked_list_type *curr_llist_ptr;
	mddi_register_access_packet_type *regacc_pkt_ptr;
	uint16_t curr_llist_idx;

	if (!in_interrupt())
		MDDI_MSG_CRIT("Called from TASK context\n");

	if (down_trylock(&mddi_host_mutex) != 0)
		return false;

	mddi_reg_read_value_ptr = reg_value_ptr;
	curr_llist_idx = mddi_get_reg_read_llist_item(false);
	if (curr_llist_idx == UNASSIGNED_INDEX) {
		up(&mddi_host_mutex);
		return false;
	}

	curr_llist_ptr = &llist_extern[curr_llist_idx];
	curr_llist_ptr->link_controller_flags = 0x11;
	curr_llist_ptr->packet_header_count = 14;
	curr_llist_ptr->packet_data_count = 0;

	curr_llist_ptr->next_packet_pointer = NULL;
	curr_llist_ptr->packet_data_pointer = NULL;
	curr_llist_ptr->reserved = 0;

	regacc_pkt_ptr = &curr_llist_ptr->packet_header.register_pkt;

	regacc_pkt_ptr->packet_length = curr_llist_ptr->packet_header_count;
	regacc_pkt_ptr->packet_type = 146; /* register access packet */
	regacc_pkt_ptr->bClient_ID = 0;
	regacc_pkt_ptr->read_write_info = 0x8001;
	regacc_pkt_ptr->register_address = reg_addr;

	/* now adjust pointers */
	mddi_queue_forward_packets(curr_llist_idx, curr_llist_idx, false, NULL);
	up(&mddi_host_mutex);

	return true;
}

bool mddi_host_register_write_int(uint32_t reg_addr, uint32_t reg_val,
				  mddi_llist_done_cb_type done_cb)
{
	mddi_linked_list_type *curr_llist_ptr;
	mddi_linked_list_type *curr_llist_dma_ptr;
	mddi_register_access_packet_type *regacc_pkt_ptr;
	uint16_t curr_llist_idx;

	if (!in_interrupt())
		MDDI_MSG_CRIT("Called from TASK context\n");

	if (down_trylock(&mddi_host_mutex) != 0)
		return false;

	curr_llist_idx = mddi_get_next_free_llist_item(false);
	if (curr_llist_idx == UNASSIGNED_INDEX) {
		up(&mddi_host_mutex);
		return false;
	}

	curr_llist_ptr = &llist_extern[curr_llist_idx];
	curr_llist_dma_ptr = &llist_dma_extern[curr_llist_idx];

	curr_llist_ptr->link_controller_flags = 1;
	curr_llist_ptr->packet_header_count = 14;
	curr_llist_ptr->packet_data_count = 4;

	curr_llist_ptr->next_packet_pointer = NULL;
	curr_llist_ptr->reserved = 0;

	regacc_pkt_ptr = &curr_llist_ptr->packet_header.register_pkt;

	regacc_pkt_ptr->packet_length = curr_llist_ptr->packet_header_count + 4;
	regacc_pkt_ptr->packet_type = 146; /* register access packet */
	regacc_pkt_ptr->bClient_ID = 0;
	regacc_pkt_ptr->read_write_info = 0x0001;
	regacc_pkt_ptr->register_address = reg_addr;
	regacc_pkt_ptr->register_data_list[0] = reg_val;

	regacc_pkt_ptr = &curr_llist_dma_ptr->packet_header.register_pkt;
	curr_llist_ptr->packet_data_pointer =
		&(regacc_pkt_ptr->register_data_list[0]);

	/* now adjust pointers */
	mddi_queue_forward_packets(curr_llist_idx, curr_llist_idx, false,
				   done_cb);
	up(&mddi_host_mutex);

	return true;
}

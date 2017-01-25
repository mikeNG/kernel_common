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

#ifndef MDDIHOST_H
#define MDDIHOST_H

#include "mddi.h"
#include <linux/delay.h>

#define FEATURE_MDDI_DISABLE_REVERSE
#define FEATURE_MDDI_UNDERRUN_RECOVERY

enum mddi_data_packet_size_type {
	MDDI_DATA_PACKET_4_BYTES  = 4,
	MDDI_DATA_PACKET_8_BYTES  = 8,
	MDDI_DATA_PACKET_12_BYTES = 12,
	MDDI_DATA_PACKET_16_BYTES = 16,
	MDDI_DATA_PACKET_24_BYTES = 24
};

typedef void (*mddi_llist_done_cb_type) (void);

#define MDDI_DEFAULT_PRIM_PIX_ATTR 0xC3
#define MDDI_DEFAULT_SECD_PIX_ATTR 0xC0

extern unsigned char mddi_timer_shutdown_flag;
extern struct mutex mddi_timer_lock;

void mddi_init(void);

int mddi_host_register_read(uint32_t reg_addr, uint32_t *reg_value_ptr,
			    bool wait);
int mddi_host_register_write(uint32_t reg_addr, uint32_t reg_val,
			     enum mddi_data_packet_size_type packet_size,
			     bool wait, mddi_llist_done_cb_type done_cb);
bool mddi_host_register_write_int(uint32_t reg_addr, uint32_t reg_val,
				  mddi_llist_done_cb_type done_cb);
bool mddi_host_register_read_int(uint32_t reg_addr, uint32_t *reg_value_ptr);

#define mddi_queue_register_read(reg, val_ptr, wait, sig) \
	mddi_host_register_read(reg, val_ptr, wait)

#define mddi_queue_register_write(reg, val, wait, sig) \
	mddi_host_register_write(reg, val, MDDI_DATA_PACKET_4_BYTES, wait, NULL)

#define mddi_queue_register_write_extn(reg, val, pkt_size, wait, sig) \
	mddi_host_register_write(reg, val, pkt_size, wait, NULL)

#define mddi_queue_register_write_int(reg, val) \
	mddi_host_register_write_int(reg, val, NULL)

#define mddi_queue_register_read_int(reg, val_ptr) \
	mddi_host_register_read_int(reg, val_ptr)

#define mddi_queue_register_writes(reg_ptr, val, wait, sig) \
	mddi_host_register_writes(reg_ptr, val, wait, sig)

static inline void mddi_wait(uint16_t time_ms)
{
	mdelay(time_ms);
}

#endif /* MDDIHOST_H */

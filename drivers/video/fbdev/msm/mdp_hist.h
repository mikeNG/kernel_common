/* Copyright (c) 2008-2013, The Linux Foundation. All rights reserved.
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


#ifndef __MDP_HIST_H__
#define __MDP_HIST_H__

struct mdp_hist_mgmt {
	uint32_t block;
	uint32_t irq_term;
	uint32_t intr;
	uint32_t base;
	struct completion mdp_hist_comp;
	struct mutex mdp_hist_mutex;
	struct mutex mdp_do_hist_mutex;
	bool mdp_is_hist_start, mdp_is_hist_data;
	bool mdp_is_hist_valid, mdp_is_hist_init;
	uint8_t frame_cnt, bit_mask, num_bins;
	struct work_struct mdp_histogram_worker;
	struct mdp_histogram_data *hist;
	uint32_t *c0, *c1, *c2;
	uint32_t *extra_info;
};

enum {
	MDP_HIST_MGMT_DMA_P = 0,
	MDP_HIST_MGMT_DMA_S,
	MDP_HIST_MGMT_VG_1,
	MDP_HIST_MGMT_VG_2,
	MDP_HIST_MGMT_MAX,
};

extern struct mdp_hist_mgmt *mdp_hist_mgmt_array[];

int mdp_histogram_destroy(void);
int mdp_histogram_init(void);
int mdp_histogram_block2mgmt(uint32_t block, struct mdp_hist_mgmt **mgmt);
int mdp_histogram_ctrl(bool en, uint32_t block);
int mdp_histogram_ctrl_all(bool en);
int mdp_histogram_start(struct mdp_histogram_start_req *req);
int mdp_histogram_stop(struct fb_info *info, uint32_t block);
int mdp_do_histogram(struct fb_info *info, struct mdp_histogram_data *hist);
void mdp_histogram_handle_isr(struct mdp_hist_mgmt *mgmt);

#endif /* __MDP_HIST_H__ */

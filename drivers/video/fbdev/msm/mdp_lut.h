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


#ifndef __MDP_LUT_H__
#define __MDP_LUT_H__

#define MDP_HIST_LUT_SIZE (256)

struct mdp_hist_lut_mgmt {
	uint32_t block;
	struct mutex lock;
	struct list_head list;
};

struct mdp_hist_lut_info {
	uint32_t block;
	bool is_enabled, has_sel_update;
	int bank_sel;
};

extern uint32_t last_lut[];
extern spinlock_t mdp_lut_push_lock;

int mdp_hist_lut_destroy(void);
int mdp_hist_lut_init(void);
int mdp_hist_lut_config(struct mdp_hist_lut_data *data);
void mdp_lut_status_restore(void);
void mdp_lut_status_backup(void);
int mdp_lut_update_nonlcdc(struct fb_info *info, struct fb_cmap *cmap);
void mdp_lut_enable(void);

#endif /* __MDP_VSYNC_H__ */

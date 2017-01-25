/* Copyright (c) 2008-2014, The Linux Foundation. All rights reserved.
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

#ifndef MSM_FB_H
#define MSM_FB_H

#include "mdp.h"

#ifdef CONFIG_FB_MSM_TRIPLE_BUFFER
#define MSM_FB_NUM	3
#else
#define MSM_FB_NUM	2
#endif

#define MFD_KEY		0x11161126

struct msm_fb_data_type {
	int key;
	int index;

	int fb_page;

	struct panel_id panel;
	struct msm_panel_info panel_info;

	DISP_TARGET dest;
	struct fb_info *fbi;

	struct device *dev;
	uint32_t fb_imgType;
	bool overlay_play_enable;

	MDPIBUF ibuf;

	/* vsync */
	bool use_mdp_vsync;
	__u32 total_lcd_lines;

	int ref_cnt;
	bool panel_power_on;
	struct mutex op_mutex;

	struct mdp_dma_data *dma;
	struct device_attribute dev_attr;

	void (*dma_fnc) (struct msm_fb_data_type *mfd);
	int (*cursor_update) (struct fb_info *info,
			      struct fb_cursor *cursor);
	int (*lut_update) (struct fb_info *info,
			      struct fb_cmap *cmap);
	int (*do_histogram) (struct fb_info *info,
			      struct mdp_histogram_data *hist);
	int (*start_histogram) (struct mdp_histogram_start_req *req);
	int (*stop_histogram) (struct fb_info *info, uint32_t block);
	void (*vsync_ctrl) (int enable);
	void (*vsync_init) (int cndx);
	void *vsync_show;
	void *cursor_buf;
	dma_addr_t *cursor_buf_phys;

	struct platform_device *pdev;

	__u32 var_xres;
	__u32 var_yres;

	struct timer_list msmfb_no_update_notify_timer;
	struct completion msmfb_update_notify;
	struct completion msmfb_no_update_notify;
	struct ion_client *iclient;
	struct ion_handle *ihandle;
	struct mdp_buf_type *ov0_wb_buf;
	struct mdp_buf_type *ov1_wb_buf;
	uint32_t mem_hid;
	size_t fb_size;
	u32 acq_fen_cnt;
	struct sync_fence *acq_fen[MDP_MAX_FENCE_FD];
	int cur_rel_fen_fd;
	struct sync_pt *cur_rel_sync_pt;
	struct sync_fence *cur_rel_fence;
	struct sync_fence *last_rel_fence;
	struct sw_sync_timeline *timeline;
	int timeline_value;
	u32 last_acq_fen_cnt;
	struct sync_fence *last_acq_fen[MDP_MAX_FENCE_FD];
	struct mutex sync_mutex;
	struct completion commit_comp;
	u32 is_committing;
	struct work_struct commit_work;
	void *msm_fb_backup;
};

struct msm_fb_backup_type {
	struct fb_info info;
	struct mdp_display_commit disp_commit;
};

struct msm_fb_data_type *msm_fb_alloc_device(struct device *dev);

void msm_fb_wait_for_fence(struct msm_fb_data_type *mfd);
int msm_fb_signal_timeline(struct msm_fb_data_type *mfd);

#endif /* MSM_FB_H */

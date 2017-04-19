/* Copyright (c) 2008-2013, The Linux Foundation. All rights reserved.
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

#ifndef MSM_FB_PANEL_H
#define MSM_FB_PANEL_H

#include "msm_fb_def.h"

struct msm_fb_data_type;

struct panel_id {
	uint16_t id;
	uint16_t type;
};

/* panel type list */
#define NO_PANEL		0xffff	/* No Panel */
#define MDDI_PANEL		1	/* MDDI */
#define LCDC_PANEL		3	/* internal LCDC type */
#define TV_PANEL		5	/* TV */
#define HDMI_PANEL		6	/* HDMI TV */
#define DTV_PANEL		7	/* DTV */
#define WRITEBACK_PANEL		10	/* Wifi display */

/* panel class */
typedef enum {
	DISPLAY_LCD = 0,	/* lcd = ebi2/mddi */
	DISPLAY_LCDC,		/* lcdc */
	DISPLAY_TV,		/* TV Out */
	DISPLAY_EXT_MDDI,	/* External MDDI */
} DISP_TARGET;

/* panel device locaiton */
typedef enum {
	DISPLAY_1 = 0,		/* attached as first device */
	DISPLAY_2,		/* attached on second device */
	DISPLAY_3,		/* attached on third writeback device */
	MAX_PHYS_TARGET_NUM,
} DISP_TARGET_PHYS;

/* panel info type */
struct lcd_panel_info {
	u32 vsync_enable;
	u32 primary_vsync_init;
	u32 primary_rdptr_irq;
	u32 primary_start_pos;
	u32 vsync_threshold_continue;
	u32 vsync_threshold_start;
	u32 total_lines;
	u32 refx100;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 v_pulse_width;
	u32 hw_vsync_mode;
	u32 blt_ctrl;
	u32 rev;
};

struct mddi_panel_info {
	u32 vdopkt;
};

struct msm_panel_info {
	struct device *dev;
	u32 xres;
	u32 yres;
	u32 bpp;
	u32 type;
	DISP_TARGET_PHYS pdest;
	u32 clk_rate;
	u32 frame_rate;

	struct mddi_panel_info mddi;
	struct lcd_panel_info lcd;
};

#endif /* MSM_FB_PANEL_H */

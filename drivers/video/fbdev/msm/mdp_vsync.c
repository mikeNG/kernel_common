/* Copyright (c) 2008-2009, 2012-2013 The Linux Foundation. All rights reserved.
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

#include <linux/clk.h>
#include <linux/module.h>
#include "msm_fb.h"

#define MDP_SYNC_CFG_0		0x100
#define MDP_SYNC_STATUS_0	0x10c
#define MDP_SYNC_CFG_1		0x104
#define MDP_SYNC_STATUS_1	0x110
#define MDP_PRIM_VSYNC_OUT_CTRL	0x118
#define MDP_SEC_VSYNC_OUT_CTRL	0x11C
#define MDP_VSYNC_SEL		0x124
#define MDP_PRIM_VSYNC_INIT_VAL	0x128
#define MDP_SEC_VSYNC_INIT_VAL	0x12C

#define VSYNC_ABOVE_TH		4
#define VSYNC_START_TH		1

struct mdp_vsync_data {
	struct clk *vsync_clk;
	uint32_t vsync_cnt_cfg;
};
static struct mdp_vsync_data *vsync_data;

void vsync_clk_prepare_enable(void)
{
	struct mdp_vsync_data *data = vsync_data;
	clk_prepare_enable(data->vsync_clk);
}

void vsync_clk_disable_unprepare(void)
{
	struct mdp_vsync_data *data = vsync_data;
	clk_disable_unprepare(data->vsync_clk);
}

static void mdp_set_sync_cfg_0(struct msm_fb_data_type *mfd, int vsync_cnt)
{
	unsigned long cfg;

	if (mfd->panel_info.lcd.total_lines)
		cfg = mfd->panel_info.lcd.total_lines;
	else
		cfg = mfd->total_lcd_lines - 1;

	cfg <<= MDP_SYNCFG_HGT_LOC;
	if (mfd->panel_info.lcd.hw_vsync_mode)
		cfg |= MDP_SYNCFG_VSYNC_EXT_EN;
	cfg |= (MDP_SYNCFG_VSYNC_INT_EN | vsync_cnt);

	MDP_OUTP(MDP_BASE + MDP_SYNC_CFG_0, cfg);
}

static void mdp_set_sync_cfg_1(struct msm_fb_data_type *mfd, int vsync_cnt)
{
	unsigned long cfg;

	if (mfd->panel_info.lcd.total_lines)
		cfg = mfd->panel_info.lcd.total_lines;
	else
		cfg = mfd->total_lcd_lines - 1;

	cfg <<= MDP_SYNCFG_HGT_LOC;
	if (mfd->panel_info.lcd.hw_vsync_mode)
		cfg |= MDP_SYNCFG_VSYNC_EXT_EN;
	cfg |= (MDP_SYNCFG_VSYNC_INT_EN | vsync_cnt);

	MDP_OUTP(MDP_BASE + MDP_SYNC_CFG_1, cfg);
}

void mdp_vsync_cfg_regs(struct msm_fb_data_type *mfd, bool first_time)
{
	struct mdp_vsync_data *data = vsync_data;
	int vsync_load_cnt;

	if (first_time)
		vsync_clk_prepare_enable();

	mdp_set_sync_cfg_0(mfd, data->vsync_cnt_cfg);

	if (mdp_hw_revision < MDP4_REVISION_V2_1)
		mdp_set_sync_cfg_1(mfd, data->vsync_cnt_cfg);

	/*
	 * load the last line + 1 to be in the
	 * safety zone
	 */
	vsync_load_cnt = mfd->panel_info.yres;

	/* line counter init value at the next pulse */
	MDP_OUTP(MDP_BASE + MDP_PRIM_VSYNC_INIT_VAL, vsync_load_cnt);
	if (mdp_hw_revision < MDP4_REVISION_V2_1)
		MDP_OUTP(MDP_BASE + MDP_SEC_VSYNC_INIT_VAL, vsync_load_cnt);

	/*
	 * external vsync source pulse width and
	 * polarity flip
	 */
	MDP_OUTP(MDP_BASE + MDP_PRIM_VSYNC_OUT_CTRL, BIT(0));
	if (mdp_hw_revision < MDP4_REVISION_V2_1) {
		MDP_OUTP(MDP_BASE + MDP_SEC_VSYNC_OUT_CTRL, BIT(0));
		MDP_OUTP(MDP_BASE + MDP_VSYNC_SEL, 0x20);
	}

	/* threshold */
	MDP_OUTP(MDP_BASE + 0x200, (VSYNC_ABOVE_TH << 16) | VSYNC_START_TH);

	if (first_time)
		vsync_clk_disable_unprepare();
}

int mdp_config_vsync(struct msm_fb_data_type *mfd)
{
	struct mdp_vsync_data *data = vsync_data;

	uint32_t total_porch_lines;

	uint32_t vsync_cnt_cfg_dem;
	unsigned long mdp_vsync_clk_speed_hz;

	/* vsync on primary lcd only for now */
	if ((mfd->dest != DISPLAY_LCD) ||
	    (mfd->panel_info.pdest != DISPLAY_1)) {
		mfd->panel_info.lcd.vsync_enable = false;
		pr_err("%s: failed!\n", __func__);
		return -EPERM;
	}

	if (!mfd->panel_info.lcd.vsync_enable) {
		pr_err("%s: vsync not enabled\n", __func__);
		return -EINVAL;
	}

	total_porch_lines = mfd->panel_info.lcd.v_back_porch +
		mfd->panel_info.lcd.v_front_porch +
		mfd->panel_info.lcd.v_pulse_width;
	mfd->total_lcd_lines = mfd->panel_info.yres + total_porch_lines;
	mfd->use_mdp_vsync = 1;

	mdp_vsync_clk_speed_hz = clk_get_rate(data->vsync_clk);

	if (mdp_vsync_clk_speed_hz == 0) {
		pr_err("%s: vsync clk is 0\n", __func__);
		mfd->use_mdp_vsync = 0;
		return -EINVAL;
	}

	/*
	 * Do this calculation in 2 steps for
	 * rounding uint32_t properly.
	 */
	vsync_cnt_cfg_dem =
		(mfd->panel_info.lcd.refx100 * mfd->total_lcd_lines) / 100;
	data->vsync_cnt_cfg = mdp_vsync_clk_speed_hz / vsync_cnt_cfg_dem;
	mdp_vsync_cfg_regs(mfd, true);

	return 0;
}

int mdp_vsync_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mdp_vsync_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->vsync_clk = devm_clk_get(dev, "vsync");
	if (IS_ERR(data->vsync_clk)) {
		ret = PTR_ERR(data->vsync_clk);
		dev_err(dev, "failed to get 'vsync' clk\n");
		return ret;
	}

	vsync_data = data;

	return 0;
}

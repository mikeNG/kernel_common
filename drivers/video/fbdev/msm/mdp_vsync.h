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


#ifndef __MDP_VSYNC_H__
#define __MDP_VSYNC_H__

void vsync_clk_prepare_enable(void);
void vsync_clk_disable_unprepare(void);
void mdp_vsync_cfg_regs(struct msm_fb_data_type *mfd, bool first_time);
int mdp_config_vsync(struct msm_fb_data_type *mfd);
int mdp_vsync_init(struct platform_device *pdev);

#endif /* __MDP_VSYNC_H__ */

/*
 * MSM MDDI Transport
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2007-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 Rudolf Tammekivi
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MDDI_H__
#define __MDDI_H__

#include "msm_fb_panel.h"

extern void __iomem *msm_pmdh_base;

#define MDDI_MSG_EMERG(msg, ...) \
	pr_emerg(msg, ## __VA_ARGS__)

#define MDDI_MSG_ALERT(msg, ...) \
	pri_alert(msg, ## __VA_ARGS__)

#define MDDI_MSG_CRIT(msg, ...) \
	pr_crit(msg, ## __VA_ARGS__)

#define MDDI_MSG_ERR(msg, ...) \
	pr_err(msg, ## __VA_ARGS__)

#define MDDI_MSG_WARNING(msg, ...) \
	pr_warning(msg, ## __VA_ARGS__)

#define MDDI_MSG_NOTICE(msg, ...) \
	pr_notice(msg, ## __VA_ARGS__)

#define MDDI_MSG_INFO(msg, ...) \
	pr_info(msg, ## __VA_ARGS__)

#define MDDI_MSG_DEBUG(msg, ...) \
	pr_debug(msg, ## __VA_ARGS__)

int mddi_panel_register(struct device *dev, struct msm_panel_info *pinfo);

#endif /* __MDDI_H__ */

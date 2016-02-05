/*
 * Qualcomm Technologies HIDMA VFIO Reset Driver
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/iopoll.h>

#include "vfio_platform_private.h"

#define TRCA_CTRLSTS_OFFSET		0x000
#define EVCA_CTRLSTS_OFFSET		0x000

#define CH_CONTROL_MASK		GENMASK(7, 0)
#define CH_STATE_MASK			GENMASK(7, 0)
#define CH_STATE_BIT_POS		0x8

#define HIDMA_CH_STATE(val)	\
	((val >> CH_STATE_BIT_POS) & CH_STATE_MASK)

#define EVCA_IRQ_EN_OFFSET		0x110

#define CH_RESET			9
#define CH_DISABLED			0

int vfio_platform_qcomhidma_reset(struct vfio_platform_device *vdev)
{
	struct vfio_platform_region *trreg;
	struct vfio_platform_region *evreg;
	u32 val;
	int ret;

	if (vdev->num_regions != 2)
		return -ENODEV;

	trreg = &vdev->regions[0];
	if (!trreg->ioaddr) {
		trreg->ioaddr =
			ioremap_nocache(trreg->addr, trreg->size);
		if (!trreg->ioaddr)
			return -ENOMEM;
	}

	evreg = &vdev->regions[1];
	if (!evreg->ioaddr) {
		evreg->ioaddr =
			ioremap_nocache(evreg->addr, evreg->size);
		if (!evreg->ioaddr)
			return -ENOMEM;
	}

	/* disable IRQ */
	writel(0, evreg->ioaddr + EVCA_IRQ_EN_OFFSET);

	/* reset both transfer and event channels */
	val = readl(trreg->ioaddr + TRCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_RESET << 16;
	writel(val, trreg->ioaddr + TRCA_CTRLSTS_OFFSET);

	ret = readl_poll_timeout(trreg->ioaddr + TRCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_DISABLED, 1000,
				 10000);
	if (ret)
		return ret;

	val = readl(evreg->ioaddr + EVCA_CTRLSTS_OFFSET);
	val &= ~(CH_CONTROL_MASK << 16);
	val |= CH_RESET << 16;
	writel(val, evreg->ioaddr + EVCA_CTRLSTS_OFFSET);

	ret = readl_poll_timeout(evreg->ioaddr + EVCA_CTRLSTS_OFFSET, val,
				 HIDMA_CH_STATE(val) == CH_DISABLED, 1000,
				 10000);
	if (ret)
		return ret;

	pr_info("HIDMA channel reset\n");
	return 0;
}
module_vfio_reset_handler("qcom,hidma-1.0", "QCOM8061",
			  vfio_platform_qcomhidma_reset);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Reset support for Qualcomm Technologies HIDMA device");

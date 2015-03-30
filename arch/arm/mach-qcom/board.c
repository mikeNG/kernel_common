/* Copyright (c) 2010-2014 The Linux Foundation. All rights reserved.
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

#include <linux/init.h>

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <asm/mach/arch.h>

static const char * const qcom_dt_match[] __initconst = {
	"qcom,apq8064",
	"qcom,apq8074-dragonboard",
	"qcom,apq8084",
	"qcom,ipq8062",
	"qcom,ipq8064",
	"qcom,msm8660-surf",
	"qcom,msm8960-cdp",
	"qcom,msm7x30",
	NULL
};

DT_MACHINE_START(QCOM_DT, "Qualcomm (Flattened Device Tree)")
	.dt_compat = qcom_dt_match,
MACHINE_END


static int __init qcom_atomic_pool_size_set(void)
{
#define ATOMIC_DMA_COHERENT_POOL_SIZE	SZ_2M

	init_dma_coherent_pool_size(ATOMIC_DMA_COHERENT_POOL_SIZE);

	return 0;
}

/*
 * This should happen before atomic_pool_init(), which is a
 * postcore_initcall.
 */
core_initcall(qcom_atomic_pool_size_set);

/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
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

#include <asm/idmap.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <soc/qcom/proc_comm.h>

static void proccomm_reset_pm_power_off(void)
{
	/* Disable interrupts */
	local_irq_disable();
	local_fiq_disable();
	msm_proc_comm(PCOM_POWER_DOWN, NULL, NULL);
	while (1)
		;
}

static int proccomm_reset_restart(struct notifier_block *this,
				  unsigned long mode, void *cmd)
{
	uint32_t restart_reason = 0x77665501;

	if (cmd) {
		if (!strncmp(cmd, "bootloader", 10)) {
			restart_reason = 0x77665500;
		} else if (!strncmp(cmd, "recovery", 8)) {
			restart_reason = 0x77665502;
		} else if (!strncmp(cmd, "eraseflash", 10)) {
			restart_reason = 0x776655EF;
		} else if (!strncmp(cmd, "oem-", 4)) {
			unsigned long code;
			int res;
			res = kstrtoul(cmd + 4, 16, &code);
			code &= 0xff;
			restart_reason = 0x6f656d00 | code;
		}
	}

	pr_debug("The reset reason is %x\n", restart_reason);

	/* Disable interrupts */
	local_irq_disable();
	local_fiq_disable();

	/*
	 * Take out a flat memory mapping and will
	 * insert a 1:1 mapping in place of
	 * the user-mode pages to ensure predictable results
	 * This function takes care of flushing the caches
	 * and flushing the TLB.
	 */
	setup_mm_for_reboot();

	msm_proc_comm(PCOM_RESET_CHIP, &restart_reason, NULL);
	while (1)
		;

	return NOTIFY_DONE;
}

static struct notifier_block proccomm_reset_restart_nb = {
	.notifier_call = proccomm_reset_restart,
	.priority = 128,
};

static int proccomm_reset_probe(struct platform_device *pdev)
{
	register_restart_handler(&proccomm_reset_restart_nb);
	pm_power_off = proccomm_reset_pm_power_off;
	return 0;
}

static const struct of_device_id proccomm_reset_of_match[] = {
	{ .compatible = "qcom,proccomm-reset", },
	{ /* end of table */ },
};

static struct platform_driver proccomm_reset_driver = {
	.probe = proccomm_reset_probe,
	.driver = {
		.name = "proccomm-reset",
		.of_match_table = proccomm_reset_of_match,
	},
};

static int __init proccomm_reset_init(void)
{
	return platform_driver_register(&proccomm_reset_driver);
}
module_init(proccomm_reset_init);

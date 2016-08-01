/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 Rudolf Tammekivi
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <soc/qcom/proc_comm.h>

#include "clk-pcom.h"

static int pc_clk_enable(struct clk_hw *hw)
{
	int rc;
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id;

	if (p->flags & ALWAYS_ON)
		return 0;

	rc = msm_proc_comm(PCOM_CLKCTL_RPC_ENABLE, &id, NULL);
	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static void pc_clk_disable(struct clk_hw *hw)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id;

	if (p->flags & ALWAYS_ON)
		return;

	msm_proc_comm(PCOM_CLKCTL_RPC_DISABLE, &id, NULL);
}

static int pc_clk_set_rate(struct clk_hw *hw, unsigned long new_rate,
			   unsigned long p_rate)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id, rate = new_rate;
	int rc;

	/*
	 * The rate _might_ be rounded off to the nearest KHz value by the
	 * remote function. So a return value of 0 doesn't necessarily mean
	 * that the exact rate was set successfully.
	 */
	if (p->flags & CLK_MIN)
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_MIN_RATE, &id, &rate);
	else if (p->flags & CLK_MAX)
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_MAX_RATE, &id, &rate);
	else
		rc = msm_proc_comm(PCOM_CLKCTL_RPC_SET_RATE, &id, &rate);
	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static unsigned long pc_clk_recalc_rate(struct clk_hw *hw, unsigned long p_rate)
{
	unsigned id = to_clk_pcom(hw)->id;
	if (msm_proc_comm(PCOM_CLKCTL_RPC_RATE, &id, NULL))
		return 0;
	else
		return id;
}

static int pc_clk_is_enabled(struct clk_hw *hw)
{
	unsigned id = to_clk_pcom(hw)->id;
	if (msm_proc_comm(PCOM_CLKCTL_RPC_ENABLED, &id, NULL))
		return 0;
	else
		return id;
}

static long pc_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *p_rate)
{
	/* Not really supported; pc_clk_set_rate() does rounding on it's own. */
	return rate;
}

const struct clk_ops pc_clk_ops = {
	.enable = pc_clk_enable,
	.disable = pc_clk_disable,
	.set_rate = pc_clk_set_rate,
	.recalc_rate = pc_clk_recalc_rate,
	.is_enabled = pc_clk_is_enabled,
	.round_rate = pc_clk_round_rate,
};
EXPORT_SYMBOL(pc_clk_ops);

static int pc_clk_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	rcdev->ops->assert(rcdev, id);
	udelay(1);
	rcdev->ops->deassert(rcdev, id);
	return 0;
}

static int pc_clk_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	int rc = msm_proc_comm(PCOM_CLKCTL_RPC_RESET_ASSERT,
			       (unsigned*)&id, NULL);

	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

static int pc_clk_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	int rc = msm_proc_comm(PCOM_CLKCTL_RPC_RESET_DEASSERT,
			       (unsigned*)&id, NULL);

	if (rc < 0)
		return rc;
	else
		return (int)id < 0 ? -EINVAL : 0;
}

struct reset_control_ops pc_clk_reset_ops = {
	.reset		= pc_clk_reset,
	.assert		= pc_clk_assert,
	.deassert	= pc_clk_deassert,
};
EXPORT_SYMBOL(pc_clk_reset_ops);

struct clk *devm_clk_register_pcom(struct device *dev,
				   struct clk_pcom *pclk)
{
	return devm_clk_register(dev, &pclk->hw);
}
EXPORT_SYMBOL_GPL(devm_clk_register_pcom);

static int pc_xo_clk_enable(struct clk_hw *hw)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id;
	unsigned enable = 1;
	return msm_proc_comm(PCOM_CLKCTL_RPC_SRC_REQUEST, &id, &enable);
}

static void pc_xo_clk_disable(struct clk_hw *hw)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id;
	unsigned enable = 0;
	msm_proc_comm(PCOM_CLKCTL_RPC_SRC_REQUEST, &id, &enable);
}

static unsigned long pc_xo_recalc_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	struct clk_pcom *p = to_clk_pcom(hw);
	return p->flags;
}

const struct clk_ops pc_xo_clk_ops = {
	.enable = pc_xo_clk_enable,
	.disable = pc_xo_clk_disable,
	.recalc_rate = pc_xo_recalc_rate,
};
EXPORT_SYMBOL(pc_xo_clk_ops);

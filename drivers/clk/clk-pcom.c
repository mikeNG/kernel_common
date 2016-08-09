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
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <soc/qcom/proc_comm.h>

#include <dt-bindings/clock/qcom,proccomm-clock.h>

#define BOOT_ON		BIT(0)
#define CLK_MIN		BIT(1)
#define CLK_MAX		BIT(2)
#define ALWAYS_ON	BIT(3)

struct clk_pcom_desc {
	unsigned id;
	const char *name;
	unsigned long flags;
};

#define CLK_PCOM(clk_id, clk_flags)	\
{					\
	.id = P_##clk_id,		\
	.name = #clk_id,		\
	.flags = clk_flags,		\
}

struct clk_pcom_driver_data {
	struct clk_onecell_data onecell;
	struct reset_controller_dev rcdev;
};

struct clk_pcom {
	unsigned id;
	unsigned long flags;
	struct clk_hw hw;
};

static inline struct clk_pcom *to_clk_pcom(struct clk_hw *hw)
{
	return container_of(hw, struct clk_pcom, hw);
}

static int pc_clk_enable(struct clk_hw *hw)
{
	int rc;
	struct clk_pcom *p = to_clk_pcom(hw);
	unsigned id = p->id;

	/* Ignore clocks that are always on. */
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

	/* Ignore clocks that are always on. */
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

static struct clk_ops clk_ops_pcom = {
	.enable = pc_clk_enable,
	.disable = pc_clk_disable,
	.set_rate = pc_clk_set_rate,
	.recalc_rate = pc_clk_recalc_rate,
	.is_enabled = pc_clk_is_enabled,
	.round_rate = pc_clk_round_rate,
};

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

static struct reset_control_ops pc_clk_reset_ops = {
	.reset		= pc_clk_reset,
	.assert		= pc_clk_assert,
	.deassert	= pc_clk_deassert,
};

static void __init pc_clk_setup(struct device_node *node,
	const struct clk_pcom_desc *descs, size_t size)
{
	int i, ret;
	struct clk_pcom_driver_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("%s: failed to allocate memory\n", __func__);
		return;
	}

	data->onecell.clks = kzalloc(P_NR_CLKS * sizeof(*data->onecell.clks),
				     GFP_KERNEL);
	if (!data->onecell.clks) {
		pr_err("%s: failed to allocate memory for onecell\n", __func__);
		return;
	}
	data->onecell.clk_num = P_NR_CLKS;

	for (i = 0; i < size; i++) {
		const struct clk_pcom_desc *desc = &descs[i];
		struct clk *c;
		struct clk_pcom *p;
		struct clk_hw *hw;
		struct clk_init_data init;

		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p) {
			pr_err("%s: failed to allocate memory, bailing\n",
			       __func__);
			return;
		}

		p->id = desc->id;
		p->flags = desc->flags;

		init.name = desc->name;
		init.ops = &clk_ops_pcom;
		init.parent_names = NULL;
		init.num_parents = 0;
		init.flags = CLK_IS_ROOT;

		if (!(p->flags & BOOT_ON))
			init.flags |= CLK_IGNORE_UNUSED;

		hw = &p->hw;
		hw->init = &init;

		c = clk_register(NULL, hw);
		if (IS_ERR(c)) {
			ret = PTR_ERR(c);
			pr_err("%s: failed to register clk %i:%s ret=%d\n",
			       __func__, i, init.name, ret);
			continue;
		}

		/* Add to onecell by desc->id as devices request clocks by ID */
		data->onecell.clks[desc->id] = c;
	}

	ret = of_clk_add_provider(node, of_clk_src_onecell_get, &data->onecell);
	if (ret) {
		pr_err("%s: failed to add of provider ret=%d\n", __func__, ret);
		return;
	}

	data->rcdev.of_node = node;
	data->rcdev.ops = &pc_clk_reset_ops;
	data->rcdev.nr_resets = P_NR_CLKS;
	ret = reset_controller_register(&data->rcdev);
	if (ret) {
		pr_err("%s: failed to register reset controller ret=%d\n",
		       __func__, ret);
		return;
	}
}

static const struct clk_pcom_desc pc_clocks_7x30[] = {
	CLK_PCOM(ADM_CLK, 0),
	CLK_PCOM(ADSP_CLK, 0),
	CLK_PCOM(AXI_ROTATOR_CLK, 0),
	CLK_PCOM(CAMIF_PAD_P_CLK, 0),
	CLK_PCOM(CAM_M_CLK, 0),
	CLK_PCOM(CE_CLK, 0),
	CLK_PCOM(CODEC_SSBI_CLK, 0),
	CLK_PCOM(CSI0_CLK, 0),
	CLK_PCOM(CSI0_P_CLK, 0),
	CLK_PCOM(CSI0_VFE_CLK, 0),
	CLK_PCOM(EBI1_CLK, CLK_MIN | ALWAYS_ON),
	CLK_PCOM(EBI1_FIXED_CLK, CLK_MIN | ALWAYS_ON),
	CLK_PCOM(ECODEC_CLK, 0),
	CLK_PCOM(EMDH_CLK, CLK_MIN | CLK_MAX),
	CLK_PCOM(EMDH_P_CLK, 0),
	CLK_PCOM(GP_CLK, 0),
	CLK_PCOM(GRP_2D_CLK, 0),
	CLK_PCOM(GRP_2D_P_CLK, 0),
	CLK_PCOM(GRP_3D_CLK, 0),
	CLK_PCOM(GRP_3D_P_CLK, 0),
	CLK_PCOM(HDMI_CLK, 0),
	CLK_PCOM(I2C_2_CLK, 0),
	CLK_PCOM(I2C_CLK, 0),
	CLK_PCOM(IMEM_CLK, 0),
	CLK_PCOM(JPEG_CLK, CLK_MIN),
	CLK_PCOM(JPEG_P_CLK, 0),
	CLK_PCOM(LPA_CODEC_CLK, 0),
	CLK_PCOM(LPA_CORE_CLK, 0),
	CLK_PCOM(LPA_P_CLK, 0),
	CLK_PCOM(MDC_CLK, 0),
	CLK_PCOM(MDP_CLK, CLK_MIN),
	CLK_PCOM(MDP_LCDC_PAD_PCLK_CLK, 0),
	CLK_PCOM(MDP_LCDC_PCLK_CLK, 0),
	CLK_PCOM(MDP_P_CLK, 0),
	CLK_PCOM(MDP_VSYNC_CLK, 0),
	CLK_PCOM(MFC_CLK, 0),
	CLK_PCOM(MFC_DIV2_CLK, 0),
	CLK_PCOM(MFC_P_CLK, 0),
	CLK_PCOM(MI2S_CODEC_RX_M_CLK, 0),
	CLK_PCOM(MI2S_CODEC_RX_S_CLK, 0),
	CLK_PCOM(MI2S_CODEC_TX_M_CLK, 0),
	CLK_PCOM(MI2S_CODEC_TX_S_CLK, 0),
	CLK_PCOM(MI2S_M_CLK, 0),
	CLK_PCOM(MI2S_S_CLK, 0),
	CLK_PCOM(PMDH_CLK, CLK_MIN | CLK_MAX),
	CLK_PCOM(PMDH_P_CLK, 0),
	CLK_PCOM(QUP_I2C_CLK, 0),
	CLK_PCOM(ROTATOR_IMEM_CLK, 0),
	CLK_PCOM(ROTATOR_P_CLK, 0),
	CLK_PCOM(SDAC_CLK, 0),
	CLK_PCOM(SDAC_M_CLK, 0),
	CLK_PCOM(SDC1_CLK, 0),
	CLK_PCOM(SDC1_P_CLK, 0),
	CLK_PCOM(SDC2_CLK, 0),
	CLK_PCOM(SDC2_P_CLK, 0),
	CLK_PCOM(SDC3_CLK, 0),
	CLK_PCOM(SDC3_P_CLK, 0),
	CLK_PCOM(SDC4_CLK, 0),
	CLK_PCOM(SDC4_P_CLK, 0),
	CLK_PCOM(SPI_CLK, 0),
	CLK_PCOM(SPI_P_CLK, 0),
	CLK_PCOM(TSIF_P_CLK, 0),
	CLK_PCOM(TSIF_REF_CLK, 0),
	CLK_PCOM(TV_DAC_CLK, 0),
	CLK_PCOM(TV_ENC_CLK, 0),
	CLK_PCOM(UART1DM_CLK, 0),
	CLK_PCOM(UART1_CLK, 0),
	CLK_PCOM(UART2DM_CLK, 0),
	CLK_PCOM(UART2_CLK, 0),
	CLK_PCOM(UART3_CLK, 0),
	CLK_PCOM(USB_HS2_CLK, 0),
	CLK_PCOM(USB_HS2_CORE_CLK, 0),
	CLK_PCOM(USB_HS2_P_CLK, 0),
	CLK_PCOM(USB_HS3_CLK, 0),
	CLK_PCOM(USB_HS3_CORE_CLK, 0),
	CLK_PCOM(USB_HS3_P_CLK, 0),
	CLK_PCOM(USB_HS_CLK, 0),
	CLK_PCOM(USB_HS_CORE_CLK, 0),
	CLK_PCOM(USB_HS_P_CLK, 0),
	CLK_PCOM(USB_PHY_CLK, CLK_MIN),
	CLK_PCOM(VFE_CAMIF_CLK, 0),
	CLK_PCOM(VFE_CLK, 0),
	CLK_PCOM(VFE_MDC_CLK, 0),
	CLK_PCOM(VFE_P_CLK, 0),
	CLK_PCOM(VPE_CLK, 0),
};

static void __init pc_clk_setup_7x30(struct device_node *node)
{
	pc_clk_setup(node, pc_clocks_7x30, ARRAY_SIZE(pc_clocks_7x30));
}
CLK_OF_DECLARE(pcom_clk, "qcom,proccomm-clock-7x30", pc_clk_setup_7x30);

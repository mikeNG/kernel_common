/*
 * Copyright (c) 2007-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017 Rudolf Tammekivi
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define SCSS_CLK_CTL_ADDR	0x04
#define SCSS_CLK_SEL_ADDR	0x08

enum acpuclk_msm7x30_src {
	SRC_LPXO,
	SRC_AXI,
	SRC_PLL1,
	SRC_PLL2,
	SRC_PLL3,
};

static const u8 acpuclk_translate_src[] = {
	[SRC_LPXO] = 0,
	[SRC_AXI] = 1,
	[SRC_PLL1] = 2,
	[SRC_PLL2] = 3,
	[SRC_PLL3] = 5,
};

static const char * const acpuclk_parents[] = {
	"lpxo",
	"pcom_ebi1_fixed_clk",
	"pll1",
	"pll2",
	"pll3",
};

struct axi_pair {
	unsigned long cpu_freq;
	unsigned long axi_freq;
};

struct clk_acpu {
	void __iomem *base;
	struct clk *axi_clk;
	struct axi_pair *axi_pairs;
	int axi_pairs_count;

	struct clk_hw hw;
	struct notifier_block clk_nb;
};

#define to_clk_acpu(_hw) \
	container_of(_hw, struct clk_acpu, hw)

static unsigned long acpuclk_msm7x30_clk_recalc_rate(struct clk_hw *hw,
						     unsigned long parent_rate)
{
	struct clk_acpu *a = to_clk_acpu(hw);
	uint32_t div, src_num;
	uint32_t reg_clksel = readl_relaxed(a->base + SCSS_CLK_SEL_ADDR);
	uint32_t reg_clkctl = readl_relaxed(a->base + SCSS_CLK_CTL_ADDR);

	src_num = reg_clksel & 0x1;
	div = ((reg_clkctl >> (8 -  (8 * src_num))) & 0xF) + 1;

	return parent_rate / div;
}

static int acpuclk_msm7x30_clk_determine_rate(struct clk_hw *hw,
					      struct clk_rate_request *req)
{
	int i;

	pr_debug("%s: requested freq %lu\n", __func__, req->rate);

	/* Try to find parent clock that can be divided to match requested
	 * frequency. */
	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, i);
		unsigned long parent_rate, divider;
		uint32_t remainder;

		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		if (parent_rate == 0 || req->rate > parent_rate)
			continue;

		divider = parent_rate;
		remainder = do_div(divider, req->rate);

		if (!remainder) {
			pr_debug("%s: found exact match %d with divider %lu\n",
				 __func__, i, divider);
			req->best_parent_hw = parent;
			req->best_parent_rate = parent_rate;
			return 0;
		}
	}

	/* ACPU clock has free access to PLL2, so use that if ACPU clock cannot
	 * divide itself to match the frequency. */
	req->best_parent_hw = clk_hw_get_parent_by_index(hw, SRC_PLL2);
	req->best_parent_rate = req->rate;

	return 0;
}

static int acpuclk_msm7x30_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_acpu *a = to_clk_acpu(hw);
	uint32_t reg_clksel, reg_clkctl, src_num;

	pr_debug("%s: changing parent to %d\n", __func__, index);

	reg_clksel = readl_relaxed(a->base + SCSS_CLK_SEL_ADDR);

	/* CLK_SEL_SRC1NO */
	src_num = reg_clksel & 1;

	/* Program clock source and divider with 0. */
	reg_clkctl = readl_relaxed(a->base + SCSS_CLK_CTL_ADDR);
	reg_clkctl &= ~(0xFF << (8 * src_num));
	reg_clkctl |= acpuclk_translate_src[index] << (4 + 8 * src_num);
	writel_relaxed(reg_clkctl, a->base + SCSS_CLK_CTL_ADDR);

	/* Toggle clock source. */
	reg_clksel ^= 1;

	/* Program clock source selection. */
	writel_relaxed(reg_clksel, a->base + SCSS_CLK_SEL_ADDR);

	/* Make sure switch to new source is complete. */
	mb();

	return 0;
}

static u8 acpuclk_msm7x30_clk_get_parent(struct clk_hw *hw)
{
	struct clk_acpu *a = to_clk_acpu(hw);
	int i;
	uint32_t sel, src_num;
	u32 reg_clksel, reg_clkctl;

	pr_debug("%s: requesting parent\n", __func__);

	reg_clksel = readl_relaxed(a->base + SCSS_CLK_SEL_ADDR);
	reg_clkctl = readl_relaxed(a->base + SCSS_CLK_CTL_ADDR);

	src_num = reg_clksel & 0x1;
	sel = (reg_clkctl >> (12 - (8 * src_num))) & 0x7;

	for (i = 0; i < ARRAY_SIZE(acpuclk_translate_src); i++) {
		if (acpuclk_translate_src[i] == sel)
			return i;
	}

	return 0;
}

static int acpuclk_msm7x30_clk_set_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long parent_rate)
{
	struct clk_acpu *a = to_clk_acpu(hw);
	uint32_t reg_clksel, reg_clkctl, src_num, sel, div;

	/* Divider is offset by 1, 0 means divide by 1. */
	div = (parent_rate / rate) - 1;

	reg_clksel = readl_relaxed(a->base + SCSS_CLK_SEL_ADDR);

	/* CLK_SEL_SRC1NO */
	src_num = reg_clksel & 1;

	/* Program clock source and divider. */
	reg_clkctl = readl_relaxed(a->base + SCSS_CLK_CTL_ADDR);

	/* Copy parent selector from old parent. */
	sel = (reg_clkctl >> (12 - (8 * src_num))) & 0x7;

	reg_clkctl &= ~(0xFF << (8 * src_num));
	reg_clkctl |= sel << (4 + 8 * src_num);
	reg_clkctl |= div << (0 + 8 * src_num);
	writel_relaxed(reg_clkctl, a->base + SCSS_CLK_CTL_ADDR);

	/* Toggle clock source. */
	reg_clksel ^= 1;

	/* Program clock source selection. */
	writel_relaxed(reg_clksel, a->base + SCSS_CLK_SEL_ADDR);

	/* Make sure switch to new source is complete. */
	mb();

	return 0;
}

static int acpuclk_msm7x30_clk_set_rate_and_parent(struct clk_hw *hw,
						   unsigned long rate,
						   unsigned long parent_rate,
						   u8 index)
{
	struct clk_acpu *a = to_clk_acpu(hw);
	uint32_t reg_clksel, reg_clkctl, src_num, div;

	/* Divider is offset by 1, 0 means divide by 1. */
	div = (parent_rate / rate) - 1;

	reg_clksel = readl_relaxed(a->base + SCSS_CLK_SEL_ADDR);

	/* CLK_SEL_SRC1NO */
	src_num = reg_clksel & 1;

	/* Program clock source and divider. */
	reg_clkctl = readl_relaxed(a->base + SCSS_CLK_CTL_ADDR);
	reg_clkctl &= ~(0xFF << (8 * src_num));
	reg_clkctl |= acpuclk_translate_src[index] << (4 + 8 * src_num);
	reg_clkctl |= div << (0 + 8 * src_num);
	writel_relaxed(reg_clkctl, a->base + SCSS_CLK_CTL_ADDR);

	/* Toggle clock source. */
	reg_clksel ^= 1;

	/* Program clock source selection. */
	writel_relaxed(reg_clksel, a->base + SCSS_CLK_SEL_ADDR);

	/* Make sure switch to new source is complete. */
	mb();

	return 0;
}

static const struct clk_ops acpuclk_msm7x30_clk_ops = {
	.recalc_rate = acpuclk_msm7x30_clk_recalc_rate,
	.determine_rate = acpuclk_msm7x30_clk_determine_rate,
	.set_parent = acpuclk_msm7x30_clk_set_parent,
	.get_parent = acpuclk_msm7x30_clk_get_parent,
	.set_rate = acpuclk_msm7x30_clk_set_rate,
	.set_rate_and_parent = acpuclk_msm7x30_clk_set_rate_and_parent,
};

/*
 * This notifier function is called for the pre-rate and post-rate change
 * notifications of the parent clock of cpuclk.
 */
static int acpuclk_msm7x30_notifier_cb(struct notifier_block *nb,
				       unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct clk_acpu *a = container_of(nb, struct clk_acpu, clk_nb);
	int i;
	int err = 0;
	bool incr = ndata->new_rate > ndata->old_rate;
	unsigned long axi_freq = a->axi_pairs[0].axi_freq;

	for (i = 0; i < a->axi_pairs_count; i++) {
		if (ndata->new_rate >= a->axi_pairs[i].cpu_freq) {
			axi_freq = a->axi_pairs[i].axi_freq;
			break;
		}
	}

	pr_debug("%s: new CPU freq %lu, new AXI freq %lu, incr %d\n",
		 __func__, ndata->new_rate, axi_freq, incr);

	if ((event == PRE_RATE_CHANGE && incr) ||
	    (event == POST_RATE_CHANGE && !incr)) {
		clk_set_rate(a->axi_clk, axi_freq);
	}

	return notifier_from_errno(err);
}

static int acpuclk_msm7x30_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource *res;
	struct clk_acpu *acpuclk;
	struct clk_init_data init = { };
	struct clk *clk;

	int count;
	int i;
	struct property *prop;
	const __be32 *p;
	u32 val;

	uint32_t reg_clksel;

	acpuclk = devm_kzalloc(dev, sizeof(*acpuclk), GFP_KERNEL);
	if (!acpuclk)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	acpuclk->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(acpuclk->base))
		return PTR_ERR(acpuclk->base);

	acpuclk->axi_clk = devm_clk_get(dev, 0);
	if (IS_ERR(acpuclk->axi_clk))
		return PTR_ERR(acpuclk->axi_clk);

	count = of_property_count_elems_of_size(node, "axi-pairs-hz",
						sizeof(u32));
	if (count % 2) {
		dev_err(dev, "axi-pairs misconfigured\n");
		return -ENODEV;
	}

	acpuclk->axi_pairs_count = count / 2;
	acpuclk->axi_pairs = devm_kcalloc(dev, acpuclk->axi_pairs_count,
					  sizeof(*acpuclk->axi_pairs),
					  GFP_KERNEL);

	count = 0;
	i = 0;
	of_property_for_each_u32(node, "axi-pairs-hz", prop, p, val) {
		if (i & 0x1) {
			acpuclk->axi_pairs[count].axi_freq = val;
			count++;
		} else {
			acpuclk->axi_pairs[count].cpu_freq = val;
		}
		i++;
	}

	/* Default to max freq. */
	clk_set_rate(acpuclk->axi_clk, acpuclk->axi_pairs[0].axi_freq);

	reg_clksel = readl_relaxed(acpuclk->base + SCSS_CLK_SEL_ADDR);

	/* Determine the ACPU clock rate. */
	switch ((reg_clksel >> 1) & 0x3) {
	case 0:	/* Running off the output of the raw clock source mux. */
		break;
	case 2:	/* Running off of the SCPLL selected through the core mux. */
		/* Switch to run off of the SCPLL selected through the raw
		 * clock source mux. */
		dev_dbg(dev, "Reprogramming ACPU source to raw clock source\n");
		break;
	default:
		dev_err(dev, "ACPU clock reports invalid source\n");
		return -EINVAL;
	}

	init.name = "acpuclk";
	init.ops = &acpuclk_msm7x30_clk_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = acpuclk_parents;
	init.num_parents = ARRAY_SIZE(acpuclk_parents);

	acpuclk->hw.init = &init;

	clk = devm_clk_register(dev, &acpuclk->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	acpuclk->clk_nb.notifier_call = acpuclk_msm7x30_notifier_cb;
	clk_notifier_register(clk, &acpuclk->clk_nb);

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return 0;
}

static const struct of_device_id acpuclk_msm7x30_match_table[] = {
	{ .compatible = "qcom,acpuclk-msm7x30" },
	{ }
};
MODULE_DEVICE_TABLE(of, acpuclk_msm7x30_match_table);

static struct platform_driver acpuclk_msm7x30_driver = {
	.probe		= acpuclk_msm7x30_probe,
	.driver		= {
		.name	= "acpuclk-msm7x30",
		.of_match_table = acpuclk_msm7x30_match_table,
	},
};

static int __init acpuclk_msm7x30_init(void)
{
	return platform_driver_register(&acpuclk_msm7x30_driver);
}
core_initcall(acpuclk_msm7x30_init);

static void __exit acpuclk_msm7x30_exit(void)
{
	platform_driver_unregister(&acpuclk_msm7x30_driver);
}
module_exit(acpuclk_msm7x30_exit);

MODULE_DESCRIPTION("ACPUCLOCK MSM 7X30 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:acpuclk-msm7x30");

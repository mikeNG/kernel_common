/*
 * MSM MDDI Transport
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2007-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016-2017 Rudolf Tammekivi
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
#define DEBUG
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "mddihost.h"
#include "mddihosti.h"
#include "msm_fb.h"

void __iomem *msm_pmdh_base;

DEFINE_MUTEX(mddi_timer_lock);
unsigned char mddi_timer_shutdown_flag;

struct mddi_driver_data {
	struct clk *core_clk;
	struct clk *iface_clk;
	int irq;
	struct mutex clk_mutex;
	bool clk_enabled;
};

static void mddi_clk_enable(struct mddi_driver_data *data, bool enable)
{
	mutex_lock(&data->clk_mutex);
	if (enable && !data->clk_enabled) {
		clk_prepare_enable(data->core_clk);
		clk_prepare_enable(data->iface_clk);
		enable_irq(data->irq);
		if (mddi_host_timer.function)
			mddi_host_timer_service(0);
	} else if (!enable && data->clk_enabled) {
		if (mddi_host_timer.function) {
			mutex_lock(&mddi_timer_lock);
			mddi_timer_shutdown_flag = 1;
			mutex_unlock(&mddi_timer_lock);
			del_timer_sync(&mddi_host_timer);
			mutex_lock(&mddi_timer_lock);
			mddi_timer_shutdown_flag = 0;
			mutex_unlock(&mddi_timer_lock);
		}
		disable_irq(data->irq);
		clk_disable_unprepare(data->core_clk);
		clk_disable_unprepare(data->iface_clk);
	}
	data->clk_enabled = enable;
	mutex_unlock(&data->clk_mutex);
};

static irqreturn_t mddi_irq(int irq, void *data)
{
	mddi_host_isr_primary();
	return IRQ_HANDLED;
}

int mddi_panel_register(struct device *dev, struct msm_panel_info *pinfo)
{
	struct mddi_driver_data *data;
	struct msm_fb_data_type *mfd = msm_fb_alloc_device(dev);
	int ret;
	unsigned long rate;

	if (!dev->parent) {
		dev_err(dev, "failed to find parent device\n");
		return -EINVAL;
	}
	dev = dev->parent;

	data = dev_get_drvdata(dev);

	if (IS_ERR(mfd)) {
		dev_err(dev, "failed to alloc fb device\n");
		return PTR_ERR(mfd);
	}

	if (!pm_runtime_status_suspended(dev))
		mddi_clk_enable(data, false);

	rate = clk_round_rate(data->core_clk, pinfo->clk_rate * 2);
	clk_set_rate(data->core_clk, rate);

	if (!pm_runtime_status_suspended(dev))
		mddi_clk_enable(data, true);

	if (!mddi_client_type)
		mddi_client_type = pinfo->lcd.rev;

	/* Make ready for MDP */
	mfd->panel_info = *pinfo;
	mfd->dest = DISPLAY_LCD;
	mfd->panel.type = pinfo->type;
	mfd->panel.id = 1;

	if (mfd->index == 0)
		mfd->fb_imgType = MSMFB_DEFAULT_TYPE;
	else
		mfd->fb_imgType = MDP_RGB_565;

	ret = mdp_panel_register(dev, mfd);
	if (ret) {
		dev_err(dev, "failed to register mdp panel\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(mddi_panel_register);

static int mddi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct mddi_driver_data *data;
	int ret;

	struct resource *res;
	void __iomem *base;
	unsigned long rate;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->clk_mutex);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	/* TODO: Eventually this should disappear */
	msm_pmdh_base = base;

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		ret = data->irq;
		dev_err(dev, "failed to get interrupt ret=%d\n", ret);
		return ret;
	}

	data->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(data->core_clk)) {
		ret = PTR_ERR(data->core_clk);
		dev_err(dev, "failed to get 'core' clk ret=%d\n", ret);
		return ret;
	}

	data->iface_clk = devm_clk_get(dev, "iface");
	if (IS_ERR(data->iface_clk)) {
		ret = PTR_ERR(data->core_clk);
		dev_err(dev, "failed to get 'iface' clk ret=%d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, data->irq, mddi_irq, 0, dev_name(dev),
			       data);
	if (ret) {
		dev_err(dev, "failed to request irq ret=%d\n", ret);
		return ret;
	}
	disable_irq(data->irq);

	/* Set to low safe value initially. */
	rate = clk_round_rate(data->core_clk, 49000000);
	ret = clk_set_rate(data->core_clk, rate);
	if (ret) {
		dev_err(dev, "failed to set 'core' clk to %lu ret=%d\n",
			rate, ret);
		return ret;
	}

	mddi_clk_enable(data, true);

	mddi_init();

	mddi_clk_enable(data, false);

	platform_set_drvdata(pdev, data);
	pm_runtime_enable(dev);

	/* Add our display panels. */
	of_platform_populate(node, NULL, NULL, dev);

	return 0;
}

static int mddi_runtime_suspend(struct device *dev)
{
	struct mddi_driver_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "suspending\n");

	mddi_clk_enable(data, false);

	return 0;
}

static int mddi_runtime_resume(struct device *dev)
{
	struct mddi_driver_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "resuming\n");

	mddi_clk_enable(data, true);

	return 0;
}

static struct dev_pm_ops mddi_dev_pm_ops = {
	.runtime_suspend = mddi_runtime_suspend,
	.runtime_resume = mddi_runtime_resume,
};

static const struct of_device_id mddi_match_table[] = {
	{ .compatible = "qcom,mddi" },
	{ }
};
MODULE_DEVICE_TABLE(of, mddi_match_table);

static struct platform_driver mddi_driver = {
	.probe		= mddi_probe,
	.driver		= {
		.name	= "mddi",
		.pm	= &mddi_dev_pm_ops,
		.of_match_table = mddi_match_table,
	},
};
module_platform_driver(mddi_driver);

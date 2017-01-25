
/*
 * MDDI Generic Panel
 * Copyright (C) 2017  Rudolf Tammekivi <rtammekivi@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see [http://www.gnu.org/licenses/].
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "msm_fb.h"
#include "mddi.h"
#include "mddihost.h"
#include "mddihosti.h"

enum mddi_panel_table {
	PANEL_ON,
	PANEL_OFF,
	PANEL_INIT,
	PANEL_MAX
};

struct mddi_panel_driver_data {
	u32 *panel_table[PANEL_MAX];
	int panel_table_size[PANEL_MAX];
};

static void mddi_write_commands(struct mddi_panel_driver_data *data,
				enum mddi_panel_table index)
{
	int i;
	u32 *table = data->panel_table[index];

	for (i = 0; i < data->panel_table_size[index]; i += 2) {
		if (!table[i]) {
			mddi_wait(table[i+1]);
			continue;
		}
		mddi_queue_register_write(table[i], table[i+1], true, 0);
	}
}

static const char *const dt_table_match[] = {
	[PANEL_ON] = "panel-on",
	[PANEL_OFF] = "panel-off",
	[PANEL_INIT] = "panel-init",
};

static int mddi_populate_tables(struct device *dev,
				struct mddi_panel_driver_data *data)
{
	int i;
	struct device_node *node = dev->of_node;
	int ret;

	for (i = 0; i < PANEL_MAX; i++) {
		int count = of_property_count_elems_of_size(node,
							    dt_table_match[i],
							    sizeof(u32));
		if (count < 1)
			continue;

		data->panel_table_size[i] = count;
		data->panel_table[i] = devm_kcalloc(dev, count, sizeof(u32),
						    GFP_KERNEL);
		ret = of_property_read_u32_array(node, dt_table_match[i],
						 data->panel_table[i], count);
		if (ret) {
			dev_dbg(dev, "failed to read '%s' ret=%d\n",
				dt_table_match[i], ret);
			devm_kfree(dev, data->panel_table[i]);
			data->panel_table_size[i] = 0;
			continue;
		}
	}
	return 0;
}

static int mddi_panel_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mddi_panel_driver_data *data;
	int ret;

	struct msm_panel_info pinfo;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = mddi_populate_tables(dev, data);
	if (ret)
		return ret;

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.dev = dev;
	pinfo.lcd.vsync_enable = true;
	pinfo.lcd.hw_vsync_mode = true;
	pinfo.mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
	pinfo.pdest = DISPLAY_1;
	pinfo.type = MDDI_PANEL;

	/* OR important properties. */
	ret |= of_property_read_u32(node, "clock-frequency", &pinfo.clk_rate);
	ret |= of_property_read_u32(node, "panel-bpp", &pinfo.bpp);
	ret |= of_property_read_u32(node, "panel-refx100", &pinfo.lcd.refx100);
	of_property_read_u32(node, "panel-rev", &pinfo.lcd.rev);
	of_property_read_u32(node, "panel-vback", &pinfo.lcd.v_back_porch);
	of_property_read_u32(node, "panel-vfront", &pinfo.lcd.v_front_porch);
	of_property_read_u32(node, "panel-vpulse", &pinfo.lcd.v_pulse_width);
	ret |= of_property_read_u32(node, "panel-xres", &pinfo.xres);
	ret |= of_property_read_u32(node, "panel-yres", &pinfo.yres);
	if (ret) {
		dev_err(dev, "failed to read some properties\n");
		return ret;
	}

	mddi_write_commands(data, PANEL_INIT);

	platform_set_drvdata(pdev, data);
	pm_runtime_enable(dev);

	ret = mddi_panel_register(dev, &pinfo);
	if (ret) {
		dev_err(dev, "failed to register panel\n");
		return ret;
	}

	return 0;
}

static int mddi_panel_runtime_suspend(struct device *dev)
{
	struct mddi_panel_driver_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "suspending\n");

	mddi_write_commands(data, PANEL_OFF);
	return 0;
}

static int mddi_panel_runtime_resume(struct device *dev)
{
	struct mddi_panel_driver_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "resuming\n");

	mddi_host_client_cnt_reset();
	mddi_write_commands(data, PANEL_ON);
	return 0;
}

static struct dev_pm_ops mddi_panel_dev_pm_ops = {
	.runtime_suspend = mddi_panel_runtime_suspend,
	.runtime_resume = mddi_panel_runtime_resume,
};

static const struct of_device_id mddi_panel_match_table[] = {
	{ .compatible = "qcom,mddi-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, mddi_match_table);

static struct platform_driver mddi_panel_driver = {
	.probe		= mddi_panel_probe,
	.driver		= {
		.name	= "mddi-panel",
		.pm	= &mddi_panel_dev_pm_ops,
		.of_match_table = mddi_panel_match_table,
	},
};
module_platform_driver(mddi_panel_driver);

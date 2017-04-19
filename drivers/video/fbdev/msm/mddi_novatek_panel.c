/*
 * Copyright (c) 2009-2010 Sony Ericsson Mobile Communications
 * Copyright (C) 2017 Rudolf Tammekivi <rtammekivi@gmail.com>
 *
 * All source code in this file is licensed under the following license
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can find it at http://www.fsf.org
 */

#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include "msm_fb.h"
#include "mddi.h"
#include "mddihost.h"
#include "mddihosti.h"

enum novatek_cmd_table {
	I2C_DETECT,
	PANEL_INIT,
	PANEL_SETUP,
	PANEL_TURNON,
	PANEL_TURNOFF,
	PANEL_TAKEDOWN,
	PANEL_STANDBY,
	PANEL_MAX
};

struct novatek_data {
	struct device *dev;
	u32 *panel_table[PANEL_MAX];
	int panel_table_size[PANEL_MAX];

	struct i2c_client *client;

	struct regulator_bulk_data regs[2];
	int gpio_reset;
};

static void novatek_reset(struct novatek_data *data)
{
	if (data->gpio_reset < 0)
		return;

	msleep(10);
	gpio_set_value(data->gpio_reset, 1);
	msleep(5); /* hw spec says: 2 ms */
	gpio_set_value(data->gpio_reset, 0);
	msleep(5); /* hw spec says: 2 ms */
	gpio_set_value(data->gpio_reset, 1);
	msleep(30); /* hw spec says: 20 ms */
}

static int novatek_power(struct novatek_data *data, bool on)
{
	int ret;

	if (on) {
		ret = regulator_bulk_enable(ARRAY_SIZE(data->regs), data->regs);

		if (!ret)
			novatek_reset(data);

		return ret;
	}

	return regulator_bulk_disable(ARRAY_SIZE(data->regs), data->regs);
}

static void novatek_write_commands(struct novatek_data *data,
				   enum novatek_cmd_table index)
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

static const char *const dt_tbl_match[] = {
	[I2C_DETECT] = "i2c-detect",
	[PANEL_INIT] = "panel-init",
	[PANEL_SETUP] = "panel-setup",
	[PANEL_TURNON] = "panel-turnon",
	[PANEL_TURNOFF] = "panel-turnoff",
	[PANEL_TAKEDOWN] = "panel-takedown",
	[PANEL_STANDBY] = "panel-standby",
};

static int novatek_populate_tables(struct novatek_data *data)
{
	int ret;
	struct device *dev = data->dev;
	struct device_node *node = dev->of_node;
	int i;

	for (i = 0; i < PANEL_MAX; i++) {
		int count = of_property_count_u32_elems(node, dt_tbl_match[i]);

		if (count < 1)
			continue;

		data->panel_table_size[i] = count;
		data->panel_table[i] = devm_kcalloc(dev, count, sizeof(u32),
						    GFP_KERNEL);

		ret = of_property_read_u32_array(node, dt_tbl_match[i],
						 data->panel_table[i], count);
		if (ret) {
			dev_err(dev, "failed to read '%s' ret=%d\n",
				dt_tbl_match[i], ret);
			devm_kfree(dev, data->panel_table[i]);
			data->panel_table[i] = NULL;
			data->panel_table_size[i] = 0;
			continue;
		}
	}

	return 0;
}

static int novatek_i2c_read(struct i2c_client *client, u16 reg,
			    u16 *data, int size)
{
	int ret;
	u8 i2c_data[4];

	i2c_data[0] = reg >> 8;
	i2c_data[1] = reg & 0xFF;
	i2c_data[2] = reg >> 8;
	i2c_data[3] = reg & 0xFF;
	i2c_master_send(client, i2c_data, size);
	ret = i2c_master_recv(client, i2c_data, size);

	*data = (i2c_data[0] << 8) | i2c_data[1];

	return ret;
}

static int novatek_mddi_detect(struct novatek_data *data)
{
	int ret;
	struct device *dev = data->dev;
	struct device_node *node = dev->of_node;

	struct device_node *i2c_bus;
	struct i2c_adapter *i2c_adap;
	u32 i2c_addr;

	int i;
	u32 *table = data->panel_table[I2C_DETECT];

	/* I2C is optional, return if not found. */
	i2c_bus = of_parse_phandle(node, "i2c-bus", 0);
	if (!i2c_bus)
		return 0;

	if (!data->panel_table_size[I2C_DETECT])
		return 0;

	i2c_adap = of_find_i2c_adapter_by_node(i2c_bus);
	if (!i2c_adap) {
		dev_err(dev, "failed to find i2c bus\n");
		return -ENODEV;
	}

	of_node_put(i2c_bus);

	ret = of_property_read_u32(node, "i2c-addr", &i2c_addr);
	if (ret) {
		dev_err(dev, "failed to get 'i2c-addr'\n");
		put_device(&i2c_adap->dev);
		return ret;
	}

	data->client = i2c_new_dummy(i2c_adap, i2c_addr);
	if (!data->client) {
		dev_err(dev, "failed to create i2c client\n");
		return -ENODEV;
	}

	put_device(&i2c_adap->dev);

	novatek_power(data, true);

	for (i = 0; i < data->panel_table_size[I2C_DETECT]; i += 2) {
		u16 value = 0;

		ret = novatek_i2c_read(data->client, table[i], &value, 4);
		if (ret < 0)
			goto err_fail;

		if (value != table[i+1]) {
			dev_dbg(dev, "panel reg 0x%x value 0x%x != 0x%x\n",
				table[i], table[i+1], value);
			ret = -ENODEV;
			goto err_fail;
		}
		dev_dbg(dev, "found match reg 0x%x val 0x%x\n",
			table[i], table[i+1]);
	}

	dev_info(dev, "found panel\n");

	novatek_power(data, false);

	return 0;
err_fail:
	novatek_power(data, false);
	i2c_unregister_device(data->client);
	data->client = NULL;
	return ret;
}

static int novatek_panel_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct novatek_data *data;

	struct msm_panel_info pinfo;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.dev = dev;
	pinfo.lcd.vsync_enable = true;
	pinfo.lcd.hw_vsync_mode = true;
	pinfo.mddi.vdopkt = MDDI_DEFAULT_PRIM_PIX_ATTR;
	pinfo.pdest = DISPLAY_1;
	pinfo.type = MDDI_PANEL;

	ret = novatek_populate_tables(data);
	if (ret)
		return ret;

	data->regs[0].supply = "vdd";
	data->regs[1].supply = "vio";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(data->regs), data->regs);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		return ret;
	}

	/* Needs proper evaluation. Set up according to NT35582. */
	ret = regulator_set_voltage(data->regs[0].consumer, 2500000, 3300000);
	if (ret) {
		dev_err(dev, "failed to set voltage for 'vdd'\n");
		return ret;
	}

	ret = regulator_set_voltage(data->regs[1].consumer, 1800000, 1800000);
	if (ret) {
		dev_err(dev, "failed to set voltage for 'vio'\n");
		return ret;
	}

	/* Reset GPIO is optional. */
	data->gpio_reset = of_get_named_gpio(node, "gpio-reset", 0);
	if (data->gpio_reset >= 0) {
		ret = devm_gpio_request(dev, data->gpio_reset, "reset");
		if (ret) {
			dev_err(dev, "failed to request 'reset' ret=%d\n", ret);
			return ret;
		}
	}

	/* I2C bus is optional. However, if the I2C bus is specified, ensure
	 * match or fail.  */
	ret = novatek_mddi_detect(data);
	if (ret)
		return ret;

	ret = of_property_read_u32(node, "clock-frequency", &pinfo.clk_rate);
	if (ret) {
		dev_err(dev, "failed to read 'clock-frequency'\n");
		return ret;
	}

	ret = of_property_read_u32(node, "panel-bpp", &pinfo.bpp);
	if (ret) {
		dev_err(dev, "failed to read 'panel-bpp'\n");
		return ret;
	}

	ret = of_property_read_u32(node, "panel-refx100", &pinfo.lcd.refx100);
	if (ret) {
		dev_err(dev, "failed to read 'panel-refx100'\n");
		return ret;
	}

	ret = of_property_read_u32(node, "panel-rev", &pinfo.lcd.rev);
	if (ret) {
		dev_err(dev, "failed to read 'panel-rev'\n");
		return ret;
	}

	ret = of_property_read_u32(node, "panel-xres", &pinfo.xres);
	if (ret) {
		dev_err(dev, "failed to read 'panel-xres'\n");
		return ret;
	}

	ret = of_property_read_u32(node, "panel-yres", &pinfo.yres);
	if (ret) {
		dev_err(dev, "failed to read 'panel-yres'\n");
		return ret;
	}

	if (of_property_read_u32(node, "panel-vback", &pinfo.lcd.v_back_porch))
		pinfo.lcd.v_back_porch = 0;

	if (of_property_read_u32(node, "panel-vfront", &pinfo.lcd.v_front_porch))
		pinfo.lcd.v_front_porch = 0;

	if (of_property_read_u32(node, "panel-vpulse", &pinfo.lcd.v_pulse_width))
		pinfo.lcd.v_pulse_width = 0;

	pm_runtime_enable(dev);

	ret = mddi_panel_register(dev, &pinfo);
	if (ret) {
		dev_err(dev, "failed to register panel\n");
		return ret;
	}

	return 0;
}

static int novatek_panel_runtime_suspend(struct device *dev)
{
	struct novatek_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "suspending\n");

	novatek_write_commands(data, PANEL_TURNOFF);
	novatek_write_commands(data, PANEL_TAKEDOWN);
	novatek_write_commands(data, PANEL_STANDBY);

	novatek_power(data, false);

	return 0;
}

static int novatek_panel_runtime_resume(struct device *dev)
{
	struct novatek_data *data = dev_get_drvdata(dev);
	dev_dbg(dev, "resuming\n");

	mddi_host_client_cnt_reset();

	novatek_power(data, true);

	novatek_write_commands(data, PANEL_INIT);
	novatek_write_commands(data, PANEL_SETUP);
	novatek_write_commands(data, PANEL_TURNON);

	return 0;
}

static struct dev_pm_ops novatek_panel_dev_pm_ops = {
	.runtime_suspend = novatek_panel_runtime_suspend,
	.runtime_resume = novatek_panel_runtime_resume,
};

static const struct of_device_id novatek_panel_match_table[] = {
	{ .compatible = "novatek,mddi-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, novatek_panel_match_table);

static struct platform_driver novatek_panel_driver = {
	.probe		= novatek_panel_probe,
	.driver		= {
		.name	= "novatek-mddi-panel",
		.pm	= &novatek_panel_dev_pm_ops,
		.of_match_table = novatek_panel_match_table,
	},
};
module_platform_driver(novatek_panel_driver);

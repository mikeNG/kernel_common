/*
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016-2017, Rudolf Tammekivi
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

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <soc/qcom/proc_comm.h>

#define PCOM_RAIL_MODE_AUTO	0
#define PCOM_RAIL_MODE_MANUAL	1

/*
 * Wrappers for the msm_proc_comm() calls.
 */
static inline int set_rail_mode(int pcom_id, int mode)
{
	int rc;

	rc = msm_proc_comm(PCOM_CLKCTL_RPC_RAIL_CONTROL, &pcom_id, &mode);
	if (!rc && pcom_id)
		rc = -EINVAL;

	return rc;
}

static int set_rail_state(int pcom_id, int state)
{
	int rc;

	rc = msm_proc_comm(state, &pcom_id, NULL);
	if (!rc && pcom_id)
		rc = -EINVAL;

	return rc;
}

struct proccomm_footswitch_data {
	struct regulator_desc	rdesc;
	unsigned		id;
	struct clk		*apb_clk;

	bool			enabled;
};

static int proccomm_fs_set_clocks(struct proccomm_footswitch_data *data,
				  bool enable)
{
	if (enable)
		clk_prepare_enable(data->apb_clk);
	else
		clk_disable_unprepare(data->apb_clk);

	return 0;
}

static int proccomm_fs_enable(struct regulator_dev *rdev)
{
	int ret;
	struct proccomm_footswitch_data *data = rdev_get_drvdata(rdev);

	proccomm_fs_set_clocks(data, true);

	ret = set_rail_state(data->id, PCOM_CLKCTL_RPC_RAIL_ENABLE);
	if (ret) {
		dev_err(rdev_get_dev(rdev),
			"failed to enable regulator %d (%s): %d\n",
			data->id, data->rdesc.name, ret);
	} else {
		dev_dbg(rdev_get_dev(rdev), "enabled regulator %d (%s)\n",
			data->id, data->rdesc.name);
		data->enabled = true;
	}

	proccomm_fs_set_clocks(data, false);

	return ret;
}

static int proccomm_fs_disable(struct regulator_dev *rdev)
{
	int ret;
	struct proccomm_footswitch_data *data = rdev_get_drvdata(rdev);

	proccomm_fs_set_clocks(data, true);

	ret = set_rail_state(data->id, PCOM_CLKCTL_RPC_RAIL_DISABLE);
	if (ret) {
		dev_err(rdev_get_dev(rdev),
			"failed to disable regulator %d (%s): %d\n",
			data->id, data->rdesc.name, ret);
	} else {
		dev_dbg(rdev_get_dev(rdev), "disabled regulator %d (%s)\n",
			data->id, data->rdesc.name);
		data->enabled = 0;
	}

	proccomm_fs_set_clocks(data, false);

	return ret;
}

static int proccomm_fs_is_enabled(struct regulator_dev *rdev)
{
	struct proccomm_footswitch_data *data = rdev_get_drvdata(rdev);
	return data->enabled;
}

static struct regulator_ops proccomm_fs_ops = {
	.enable		= proccomm_fs_enable,
	.disable	= proccomm_fs_disable,
	.is_enabled	= proccomm_fs_is_enabled,
};

static int proccomm_fs_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct proccomm_footswitch_data *data;

	struct regulator_init_data *initdata;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct regulator_config config = { };

	if (!node) {
		dev_err(dev, "no device tree data supplied\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	rdesc = &data->rdesc;

	ret = of_property_read_u32(node, "id", &data->id);
	if (ret) {
		dev_err(dev, "failed to read 'id'\n");
		return ret;
	}

	/*
	 * Set footswitch in manual mode (ie. not controlled along
	 * with pcom clocks).
	 */
	ret = set_rail_mode(data->id, PCOM_RAIL_MODE_MANUAL);
	if (ret) {
		dev_err(dev, "failed to set rail mode for %d\n", data->id);
		return ret;
	}

	data->apb_clk = devm_clk_get(dev, "core");
	if (IS_ERR(data->apb_clk))
		data->apb_clk = NULL;

	initdata = of_get_regulator_init_data(dev, node, rdesc);
	if (!initdata) {
		dev_err(dev, "failed to get regulator init data\n");
		return -ENOMEM;
	}

	rdesc->name = initdata->constraints.name;
	rdesc->ops = &proccomm_fs_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->owner = THIS_MODULE;

	config.dev = dev;
	config.init_data = initdata;
	config.driver_data = data;
	config.of_node = node;

	rdev = devm_regulator_register(dev, rdesc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "failed to register regulator\n");
		return PTR_ERR(rdev);;
	}

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static struct of_device_id proccomm_fs_of_match_table[] = {
	{ .compatible = "qcom,proccomm-footswitch", },
	{ }
};
MODULE_DEVICE_TABLE(of, proccomm_fs_of_match_table);

static struct platform_driver proccomm_fs_driver = {
	.probe	= proccomm_fs_probe,
	.driver	= {
		.name		= "proccomm-footswitch",
		.owner		= THIS_MODULE,
		.of_match_table	= proccomm_fs_of_match_table,
	},
};

static int __init proccomm_fs_init(void)
{
	return platform_driver_register(&proccomm_fs_driver);
}
subsys_initcall(proccomm_fs_init);

static void __exit proccomm_fs_exit(void)
{
	platform_driver_unregister(&proccomm_fs_driver);
}
module_exit(proccomm_fs_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ProcComm footswitch driver");
MODULE_VERSION("1.0");

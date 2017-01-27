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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of.h>
#include <soc/qcom/proc_comm.h>

#define MV_TO_UV(mv)		((mv)*1000)
#define UV_TO_MV(uv)		((uv)/1000)

/*
 * Wrappers for the msm_proc_comm() calls.
 * Does basic impedance matching between what the proccomm interface
 * expects and how the driver sees the world.
 */

/* Converts a proccomm error to an errno value. */
static int _pcom_err_to_linux_errno(unsigned error)
{
	if (!error)		/* 0 == no error */
		return 0;
	else if (error & 0x1F)	/* bits 0..4 => parameter 1..5 out of range */
		return -EDOM;
	else if (error & 0x100)	/* bit 8 => feature not supported */
		return -ENOSYS;
	else			/* anything else non-zero: unknown error */
		return -EINVAL;
}

/* vreg_switch: (vreg ID, on/off) => (return code, <null>) */
static int _vreg_switch(unsigned vreg_id, bool enable)
{
	unsigned _enable = !!enable;

	return msm_proc_comm(PCOM_VREG_SWITCH, &vreg_id, &_enable);
}

/* vreg_set_level: (vreg ID, mV) => (return code, <null>) */
static int _vreg_set_level(unsigned vreg_id, int level_mV)
{
	int ret;
	unsigned _level = (unsigned)level_mV;

	ret = msm_proc_comm(PCOM_VREG_SET_LEVEL, &vreg_id, &_level);
	if (ret)
		return ret;

	return _pcom_err_to_linux_errno(vreg_id);
}

/* vreg_pull_down: (pull down, vreg ID) => (<null>, <null>) */
/* Returns error code from msm_proc_comm. */
static int _vreg_pull_down(unsigned vreg_id, bool pull_down)
{
	unsigned _pull_down = !!pull_down;

	return msm_proc_comm(PCOM_VREG_PULLDOWN, &_pull_down, &vreg_id);
}

struct proccomm_regulator_data {
	struct regulator_desc	rdesc;
	unsigned		id;
	int			rise_time;
	bool			pulldown;
	bool			negative;

	int			last_voltage;
	bool			enabled;
};

static int proccomm_vreg_enable(struct regulator_dev *rdev)
{
	int ret;
	struct proccomm_regulator_data *data = rdev_get_drvdata(rdev);

	ret = _vreg_switch(data->id, true);
	if (ret) {
		dev_err(rdev_get_dev(rdev),
			"failed to enable regulator %d (%s): %d\n",
			data->id, data->rdesc.name, ret);
	} else {
		dev_dbg(rdev_get_dev(rdev), "enabled regulator %d (%s)\n",
			data->id, data->rdesc.name);
		data->enabled = 1;
	}

	return ret;
}

static int proccomm_vreg_disable(struct regulator_dev *rdev)
{
	int ret;
	struct proccomm_regulator_data *data = rdev_get_drvdata(rdev);

	ret = _vreg_switch(data->id, false);
	if (ret) {
		dev_err(rdev_get_dev(rdev),
			"failed to disable regulator %d (%s): %d\n",
			data->id, data->rdesc.name, ret);
	} else {
		dev_dbg(rdev_get_dev(rdev), "disabled regulator %d (%s)\n",
			data->id, data->rdesc.name);
		data->enabled = 0;
	}

	return ret;
}

static int proccomm_vreg_is_enabled(struct regulator_dev *rdev)
{
	struct proccomm_regulator_data *data = rdev_get_drvdata(rdev);
	return data->enabled;
}

static int proccomm_vreg_get_voltage(struct regulator_dev *rdev)
{
	struct proccomm_regulator_data *data = rdev_get_drvdata(rdev);
	return MV_TO_UV(data->last_voltage);
}

static int proccomm_vreg_set_voltage(struct regulator_dev *rdev,
				     int min_uV, int max_uV, unsigned *sel)
{
	int ret;
	struct proccomm_regulator_data *data = rdev_get_drvdata(rdev);
	int level_mV = UV_TO_MV(min_uV);

	ret = _vreg_set_level(data->id,
			      data->negative ? -level_mV : level_mV);
	if (ret) {
		dev_err(rdev_get_dev(rdev),
			"failed to set voltage for regulator %d (%s) "
			"to %d mV: %d\n",
			data->id, data->rdesc.name, level_mV, ret);
	} else {
		dev_dbg(rdev_get_dev(rdev),
			"voltage for regulator %d (%s) set to %d mV\n",
			data->id, data->rdesc.name, level_mV);
		data->last_voltage = level_mV;
	}

	return ret;
}

static struct regulator_ops proccomm_vreg_ops = {
	.enable		= proccomm_vreg_enable,
	.disable	= proccomm_vreg_disable,
	.is_enabled	= proccomm_vreg_is_enabled,
	.get_voltage	= proccomm_vreg_get_voltage,
	.set_voltage	= proccomm_vreg_set_voltage,
	.list_voltage	= regulator_list_voltage_linear,
};

static int proccomm_vreg_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct proccomm_regulator_data *data;

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
		dev_err(dev, "failed to read 'id' ret=%d\n", ret);
		return ret;
	}

	of_property_read_u32(node, "rise-time", &rdesc->enable_time);
	data->pulldown = of_property_read_bool(node, "pulldown");
	data->negative = of_property_read_bool(node, "negative");

	if (data->pulldown) {
		ret = _vreg_pull_down(data->id, data->pulldown);
		if (ret) {
			dev_err(dev, "failed to pull down regulator ret=%d\n",
				ret);
			return ret;
		}
	}

	initdata = of_get_regulator_init_data(dev, node, rdesc);
	if (!initdata) {
		dev_err(dev, "failed to get regulator init data\n");
		return -ENOMEM;
	}

	rdesc->min_uV = initdata->constraints.min_uV;
	if (initdata->constraints.valid_ops_mask & REGULATOR_CHANGE_VOLTAGE) {
		rdesc->uV_step = 25000;
		rdesc->n_voltages = ((initdata->constraints.max_uV -
				      initdata->constraints.min_uV) /
				     rdesc->uV_step) + 1;
	} else {
		rdesc->n_voltages = 1;
	}

	rdesc->name = initdata->constraints.name;
	rdesc->ops = &proccomm_vreg_ops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->owner = THIS_MODULE;

	config.dev = dev;
	config.init_data = initdata;
	config.driver_data = data;
	config.of_node = node;

	rdev = devm_regulator_register(dev, rdesc, &config);
	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(dev, "failed to register regulator ret=%d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static struct of_device_id proccomm_vreg_of_match_table[] = {
	{ .compatible = "qcom,proccomm-regulator", },
	{ }
};
MODULE_DEVICE_TABLE(of, proccomm_vreg_of_match_table);

static struct platform_driver proccomm_vreg_driver = {
	.probe	= proccomm_vreg_probe,
	.driver	= {
		.name		= "proccomm-regulator",
		.owner		= THIS_MODULE,
		.of_match_table	= proccomm_vreg_of_match_table,
	},
};

static int __init proccomm_vreg_init(void)
{
	return platform_driver_register(&proccomm_vreg_driver);
}
subsys_initcall(proccomm_vreg_init);

static void __exit proccomm_vreg_exit(void)
{
	platform_driver_unregister(&proccomm_vreg_driver);
}
module_exit(proccomm_vreg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ProcComm regulator driver");
MODULE_VERSION("1.0");

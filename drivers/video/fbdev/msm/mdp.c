/* drivers/video/msm_fb/mdp.c
 *
 * MSM MDP Interface (used by framebuffer core)
 *
 * Copyright (c) 2007-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2007 Google Incorporated
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/of_platform.h>
#include "mdp.h"
#include "msm_fb.h"
#include "mdp4.h"

struct mdp_driver_data {
	struct device *dev;
	struct regulator *footswitch;
	bool footswitch_on;

	int irq;
	int irq_on;
	int irq_mask;

	struct mutex suspend_mutex;
	spinlock_t lock;

	struct clk *core_clk;
	struct clk *iface_clk;
	struct clk *lut_clk;

	uint32_t display_intf;
	struct msm_fb_data_type *mfd;
	struct mdp_dma_data dma;
};
static struct mdp_driver_data *mdp_data;

enum msm_mdp_hw_revision mdp_rev;
u32 mdp_max_clk = 266667000;

uint32_t mdp_intr_mask = MDP4_ANY_INTR_MASK;

static MDP_BLOCK_TYPE mdp_debug[MDP_MAX_BLOCK];

spinlock_t mdp_spin_lock;

uint32_t mdp_block2base(uint32_t block)
{
	uint32_t base = 0x0;
	switch (block) {
	case MDP_BLOCK_DMA_P:
		base = 0x90000;
		break;
	case MDP_BLOCK_DMA_S:
		base = 0xA0000;
		break;
	case MDP_BLOCK_VG_1:
		base = 0x20000;
		break;
	case MDP_BLOCK_VG_2:
		base = 0x30000;
		break;
	case MDP_BLOCK_RGB_1:
		base = 0x40000;
		break;
	case MDP_BLOCK_RGB_2:
		base = 0x50000;
		break;
	case MDP_BLOCK_OVERLAY_0:
		base = 0x10000;
		break;
	case MDP_BLOCK_OVERLAY_1:
		base = 0x18000;
		break;
	case MDP_BLOCK_OVERLAY_2:
		base = (mdp_rev >= MDP_REV_43) ? 0x88000 : 0;
		break;
	default:
		break;
	}
	return base;
}

#define DEFAULT_FRAME_RATE	60

u32 mdp_get_panel_framerate(struct msm_fb_data_type *mfd)
{
	u32 frame_rate = 0;
	struct msm_panel_info *panel_info = &mfd->panel_info;

	if (mfd->dest == DISPLAY_LCD)
		frame_rate = panel_info->lcd.refx100 / 100;
	pr_debug("%s type=%d frame_rate=%d\n", __func__,
		 panel_info->type, frame_rate);

	if (frame_rate)
		return frame_rate;

	return DEFAULT_FRAME_RATE;
}

/*
 * mdp_enable_irq: can not be called from isr
 */
void mdp_enable_irq(uint32_t term)
{
	struct mdp_driver_data *data = mdp_data;
	unsigned long irq_flags;

	spin_lock_irqsave(&data->lock, irq_flags);
	if (data->irq_mask & term) {
		pr_err("%s: MDP IRQ term-0x%x is already set, mask=%x irq=%d\n",
		       __func__, term, data->irq_mask, data->irq_on);
	} else {
		data->irq_mask |= term;
		if (data->irq_mask && !data->irq_on) {
			data->irq_on = 1;
			enable_irq(data->irq);
		}
	}
	spin_unlock_irqrestore(&data->lock, irq_flags);
}

/*
 * mdp_disable_irq: can not be called from isr
 */
void mdp_disable_irq(uint32_t term)
{
	struct mdp_driver_data *data = mdp_data;
	unsigned long irq_flags;

	spin_lock_irqsave(&data->lock, irq_flags);
	if (!(data->irq_mask & term)) {
		pr_err("%s: MDP IRQ term-0x%x is NOT set, mask=%x irq=%d\n",
		       __func__, term, data->irq_mask, data->irq_on);
	} else {
		data->irq_mask &= ~term;
		if (!data->irq_mask && data->irq_on) {
			data->irq_on = 0;
			disable_irq(data->irq);
		}
	}
	spin_unlock_irqrestore(&data->lock, irq_flags);
}

void mdp_disable_irq_nosync(uint32_t term)
{
	struct mdp_driver_data *data = mdp_data;

	spin_lock(&data->lock);
	if (!(data->irq_mask & term)) {
		pr_err("%s: MDP IRQ term-0x%x is NOT set, mask=%x irq=%d\n",
		       __func__, term, data->irq_mask, data->irq_on);
	} else {
		data->irq_mask &= ~term;
		if (!data->irq_mask && data->irq_on) {
			data->irq_on = 0;
			disable_irq_nosync(data->irq);
		}
	}
	spin_unlock(&data->lock);
}

void mdp_pipe_kickoff_simplified(uint32_t term)
{
	if (term == MDP_OVERLAY0_TERM) {
		mdp_lut_enable();
		outpdw(MDP_BASE + 0x0004, 0);
	} else if (term == MDP_DMAP_TERM) {
		outpdw(MDP_BASE + 0x000c, 0);
	} else if (term == MDP_DMA_S_TERM) {
		outpdw(MDP_BASE + 0x0010, 0);
	}
}

/*
 * mdp_clk_disable_unprepare(void) called from thread context
 */
static void mdp_clk_disable_unprepare(void)
{
	struct mdp_driver_data *data = mdp_data;

	mb();
	vsync_clk_disable_unprepare();

	if (data->lut_clk)
		clk_disable_unprepare(data->lut_clk);

	if (data->iface_clk)
		clk_disable_unprepare(data->iface_clk);

	clk_disable_unprepare(data->core_clk);
}

/*
 * mdp_clk_prepare_enable(void) called from thread context
 */
static void mdp_clk_prepare_enable(void)
{
	struct mdp_driver_data *data = mdp_data;

	clk_prepare_enable(data->core_clk);

	if (data->iface_clk)
		clk_prepare_enable(data->iface_clk);

	if (data->lut_clk)
		clk_prepare_enable(data->lut_clk);

	vsync_clk_prepare_enable();
}

/*
 * mdp_clk_ctrl: called from thread context
 */
void mdp_clk_ctrl(int on)
{
	struct mdp_driver_data *data = mdp_data;
	static int mdp_clk_cnt;

	mutex_lock(&data->suspend_mutex);
	if (on) {
		if (mdp_clk_cnt == 0)
			mdp_clk_prepare_enable();
		mdp_clk_cnt++;
	} else {
		if (mdp_clk_cnt) {
			mdp_clk_cnt--;
			if (mdp_clk_cnt == 0)
				mdp_clk_disable_unprepare();
		} else {
			pr_err("%s: unbalanced disables\n", __func__);
		}
	}
	pr_debug("%s: on=%d cnt=%d\n", __func__, on, mdp_clk_cnt);
	mutex_unlock(&data->suspend_mutex);
}

static void mdp_drv_init(void)
{
	int i;

	for (i = 0; i < MDP_MAX_BLOCK; i++)
		mdp_debug[i] = 0;

	/* initialize spin lock and workqueue */
	spin_lock_init(&mdp_spin_lock);
	spin_lock_init(&mdp_lut_push_lock);
}

enum msm_mdp_sub_revision mdp_hw_revision = MDP4_REVISION_NONE;

/*
 * mdp_hw_revision:
 * 0 == V1
 * 1 == V2
 * 2 == V2.1
 *
 */
static void mdp_hw_version(struct mdp_driver_data *data,
			   uintptr_t hw_revision_addr)
{
	void __iomem *cp;
	uint32_t *hp;

	/* tlmmgpio2 shadow */
	cp = ioremap(hw_revision_addr, 0x16);

	if (!cp)
		return;

	hp = (uint32_t *)cp; /* HW_REVISION_NUMBER */
	mdp_hw_revision = *hp;
	iounmap(cp);

	mdp_hw_revision >>= 28; /* bit 31:28 */
	mdp_hw_revision &= 0x0f;

	pr_info("MDP HW revision is %x\n", mdp_hw_revision);
}

static DEFINE_MUTEX(mdp_clk_lock);

int mdp_set_core_clk(unsigned long rate)
{
	struct mdp_driver_data *data = mdp_data;
	int ret;

	mutex_lock(&mdp_clk_lock);
	ret = clk_set_rate(data->core_clk, rate);
	mutex_unlock(&mdp_clk_lock);
	if (ret)
		pr_err("%s: failed to set clk rate ret=%d\n", __func__, ret);

	return ret;
}

int mdp_clk_round_rate(unsigned long rate)
{
	struct mdp_driver_data *data = mdp_data;
	return clk_round_rate(data->core_clk, rate);
}

unsigned long mdp_get_core_clk(void)
{
	struct mdp_driver_data *data = mdp_data;
	unsigned long clk_rate;

	mutex_lock(&mdp_clk_lock);
	clk_rate = clk_get_rate(data->core_clk);
	mutex_unlock(&mdp_clk_lock);

	return clk_rate;
}

int mdp_panel_register(struct device *dev, struct msm_fb_data_type *mfd)
{
	struct mdp_driver_data *data;
	struct platform_device *msm_fb_dev;
	int ret;

	if (!dev->parent) {
		dev_err(dev, "failed to find parent device\n");
		return -EINVAL;
	}
	dev = dev->parent;

	data = dev_get_drvdata(dev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	data->mfd = mfd;

	msm_fb_dev = platform_device_alloc("msm_fb", 0);
	if (!msm_fb_dev) {
		dev_err(dev, "failed to allocate 'msm_fb'\n");
		return -ENOMEM;
	}
	msm_fb_dev->dev.parent = dev;

	mfd->ov0_wb_buf = devm_kzalloc(dev, sizeof(*mfd->ov0_wb_buf),
				       GFP_KERNEL);
	mfd->ov1_wb_buf = devm_kzalloc(dev, sizeof(*mfd->ov1_wb_buf),
				       GFP_KERNEL);
	mfd->mem_hid = BIT(ION_SF_HEAP_ID);
	mfd->fb_size = mfd->panel_info.xres * mfd->panel_info.yres *
		       (mfd->panel_info.bpp / 8) * MSM_FB_NUM;

	mdp_clk_ctrl(1);

	switch (mfd->panel.type) {
	case MDDI_PANEL:
		mfd->dma_fnc = mdp4_mddi_overlay;
		mfd->vsync_init = mdp4_mddi_rdptr_init;
		mfd->vsync_show = mdp4_mddi_show_event;
		mfd->dma = &data->dma;
		mfd->lut_update = mdp_lut_update_nonlcdc;
		mfd->do_histogram = mdp_do_histogram;
		mfd->start_histogram = mdp_histogram_start;
		mfd->stop_histogram = mdp_histogram_stop;
		mdp4_display_intf_sel(PRIMARY_INTF_SEL, MDDI_INTF);

		mdp_config_vsync(mfd);
		break;
	default:
		pr_err("%s: unknown panel type %d\n", __func__,
		       mfd->panel.type);
		ret = -ENODEV;
		goto mdp_probe_err;
	}

	if (mdp_rev >= MDP_REV_40)
		data->display_intf = inpdw(MDP_BASE + 0x0038);

	mdp_clk_ctrl(0);

	mfd->panel_info.frame_rate = mdp_get_panel_framerate(mfd);

	/* set driver data */
	platform_set_drvdata(msm_fb_dev, mfd);

	ret = platform_device_add(msm_fb_dev);
	if (ret) {
		goto mdp_probe_err;
	}

	pm_runtime_enable(dev);

	return 0;

mdp_probe_err:
	platform_device_put(msm_fb_dev);
	return ret;
}

static int mdp_clk_irq_init(struct platform_device *pdev,
			    struct mdp_driver_data *data)
{
	struct device *dev = &pdev->dev;
	int ret;
	unsigned long clk_rate;

	ret = devm_request_irq(dev, data->irq, mdp4_isr, 0, dev_name(dev), 0);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}
	disable_irq(data->irq);

	data->footswitch = devm_regulator_get(dev, "vdd");
	if (IS_ERR(data->footswitch)) {
		data->footswitch = NULL;
	} else {
		ret = regulator_enable(data->footswitch);
		if (ret) {
			dev_err(dev, "failed to enable 'vdd' regulator\n");
			return ret;
		}
		data->footswitch_on = true;
	}

	data->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(data->core_clk)) {
		ret = PTR_ERR(data->core_clk);
		dev_err(dev, "failed to get 'core' clk\n");
		return ret;
	}

	data->iface_clk = devm_clk_get(dev, "iface");
	if (IS_ERR(data->iface_clk))
		data->iface_clk = NULL;

	if (mdp_rev >= MDP_REV_42) {
		data->lut_clk = devm_clk_get(dev, "lut");
		if (IS_ERR(data->lut_clk)) {
			ret = PTR_ERR(data->lut_clk);
			dev_err(dev, "failed to get 'lut' clk\n");
			return ret;
		}
	} else {
		data->lut_clk = NULL;
	}

	clk_rate = mdp_max_clk;

	mdp_set_core_clk(clk_rate);
	if (data->lut_clk)
		clk_set_rate(data->lut_clk, clk_rate);

	pr_info("MDP core @%lu\n", clk_rate);

	if (mdp_rev == MDP_REV_42) {
		mdp_clk_ctrl(1);
		/* DSI Video Timing generator disable */
		outpdw(MDP_BASE + 0xE0000, 0x0);
		/* Clear MDP Interrupt Enable register */
		outpdw(MDP_BASE + 0x50, 0x0);
		/* Set Overlay Proc 0 to reset state */
		outpdw(MDP_BASE + 0x10004, 0x3);
		mdp_clk_ctrl(0);
	}
	return 0;
}

static int mdp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mdp_driver_data *data;
	int ret;
	uint32_t hw_revision_addr = 0;

	struct resource *res;

	mdp_drv_init();

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* TODO: Should go away */
	mdp_data = data;

	data->dev = dev;
	mutex_init(&data->suspend_mutex);
	spin_lock_init(&data->lock);
	init_completion(&data->dma.comp);
	init_completion(&data->dma.dmap_comp);
	sema_init(&data->dma.mutex, 1);
	mutex_init(&data->dma.ov_mutex);

	ret = of_property_read_u32(node, "mdp-max-clk", &mdp_max_clk);
	if (ret) {
		dev_err(dev, "failed to read 'mdp-max-clk' ret=%d\n", ret);
		return ret;
	}

	of_property_read_u32(node, "hw-revision-addr", &hw_revision_addr);

	if (hw_revision_addr)
		mdp_hw_version(data, hw_revision_addr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	msm_mdp_base = devm_ioremap_resource(&pdev->dev, res);

	if (unlikely(!msm_mdp_base))
		return -ENOMEM;

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		pr_err("mdp: can not get mdp irq\n");
		return -ENOMEM;
	}

	mdp_rev = (enum msm_mdp_hw_revision)of_device_get_match_data(dev);

	ret = mdp_clk_irq_init(pdev, data);
	if (ret)
		return ret;

	ret = mdp_vsync_init(pdev);
	if (ret) {
		dev_err(dev, "failed to init vsync\n");
		return ret;
	}

	mdp4_hw_init();

	mdp_hw_cursor_init();

	/* initialize Post Processing data*/
	mdp_hist_lut_init();
	mdp_histogram_init();

	platform_set_drvdata(pdev, data);

	of_platform_populate(node, NULL, NULL, dev);

	return 0;
}

static int mdp_remove(struct platform_device *pdev)
{
	/*free post processing memory*/
	mdp_histogram_destroy();
	mdp_hist_lut_destroy();

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static void mdp_footswitch_ctrl(struct mdp_driver_data *data, bool on)
{
	int ret;

	if (!data->footswitch || mdp_rev <= MDP_REV_41)
		return;

	mutex_lock(&data->suspend_mutex);

	if (on && !data->footswitch_on) {
		ret = regulator_enable(data->footswitch);
		if (ret)
			return;
	} else if (!on && data->footswitch_on) {
		regulator_disable(data->footswitch);
	}

	data->footswitch_on = on;

	mutex_unlock(&data->suspend_mutex);
}

static int mdp_runtime_suspend(struct device *dev)
{
	struct mdp_driver_data *data = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = data->mfd;

	dev_dbg(dev, "suspending\n");

	mdp_clk_ctrl(1);

	if (mfd->panel.type == MDDI_PANEL)
		mdp4_mddi_off(mfd);

	mdp_histogram_ctrl_all(false);
	mdp_lut_status_backup();

	mdp_clk_ctrl(0);

	mdp_footswitch_ctrl(data, false);

	return 0;
}

static int mdp_runtime_resume(struct device *dev)
{
	struct mdp_driver_data *data = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = data->mfd;

	int ret = 0;
	int i;

	dev_dbg(dev, "resuming\n");

	mdp_footswitch_ctrl(data, true);

	mdp_clk_ctrl(1);

	if (mdp_rev >= MDP_REV_40) {
		mdp4_hw_init();

		/* Initialize HistLUT to last LUT */
		for (i = 0; i < MDP_HIST_LUT_SIZE; i++) {
			MDP_OUTP(MDP_BASE + 0x94800 + i*4, last_lut[i]);
			MDP_OUTP(MDP_BASE + 0x94C00 + i*4, last_lut[i]);
		}

		mdp_lut_status_restore();

		outpdw(MDP_BASE + 0x0038, data->display_intf);
	}

	mdp_histogram_ctrl_all(true);

	if (mfd->panel.type == MDDI_PANEL) {
		mdp_vsync_cfg_regs(mfd, false);
		mdp4_mddi_on(mfd);
	}

	mdp_clk_ctrl(0);

	return ret;
}

static struct dev_pm_ops mdp_dev_pm_ops = {
	.runtime_suspend = mdp_runtime_suspend,
	.runtime_resume = mdp_runtime_resume,
};

static const struct of_device_id mdp_match_table[] = {
	{ .compatible = "qcom,mdp40", .data = (void *)MDP_REV_40 },
	{ .compatible = "qcom,mdp41", .data = (void *)MDP_REV_41 },
	{ .compatible = "qcom,mdp42", .data = (void *)MDP_REV_42 },
	{ }
};
MODULE_DEVICE_TABLE(of, mdp_match_table);

static struct platform_driver mdp_driver = {
	.probe = mdp_probe,
	.remove = mdp_remove,
	.driver = {
		.name = "mdp",
		.pm = &mdp_dev_pm_ops,
		.of_match_table = mdp_match_table,
	},
};
module_platform_driver(mdp_driver);

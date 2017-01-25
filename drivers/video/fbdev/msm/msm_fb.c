/*
 * drivers/video/msm/msm_fb.c
 *
 * Core MSM framebuffer driver.
 *
 * Copyright (C) 2007 Google Incorporated
 * Copyright (c) 2008-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016-2017, Rudolf Tammekivi
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

#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include "../../../staging/android/sync.h"
#include "../../../staging/android/sw_sync.h"

#include "msm_fb.h"
#include "mdp4.h"

unsigned char *msm_mdp_base;
unsigned char *msm_emdh_base;

static bool bf_supported;

bool align_buffer = true;
module_param(align_buffer, bool, 0644);

#define MAX_FBI_LIST 32
static struct fb_info *fbi_list[MAX_FBI_LIST];
static int fbi_list_index;

static u32 msm_fb_pseudo_palette[16] = {
	0x00000000, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
	0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff
};

u32 msm_fb_debug_enabled;
/* Setting msm_fb_msg_level to 8 prints out ALL messages */
u32 msm_fb_msg_level = 7;

static int msm_fb_register(struct msm_fb_data_type *mfd);
static int msm_fb_open(struct fb_info *info, int user);
static int msm_fb_release(struct fb_info *info, int user);
static int msm_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info);
static int msm_fb_check_var(struct fb_var_screeninfo *var,
			    struct fb_info *info);
static int msm_fb_set_par(struct fb_info *info);
static int msm_fb_blank_sub(int blank_mode, struct fb_info *info);
static int msm_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg);
static void msm_fb_commit_wq_handler(struct work_struct *work);
static int msm_fb_pan_idle(struct msm_fb_data_type *mfd);
static void msm_fb_release_timeline(struct msm_fb_data_type *mfd);

#define WAIT_FENCE_FIRST_TIMEOUT (3 * MSEC_PER_SEC)
#define WAIT_FENCE_FINAL_TIMEOUT (10 * MSEC_PER_SEC)
/* Display op timeout should be greater than total timeout */
#define WAIT_DISP_OP_TIMEOUT (WAIT_FENCE_FIRST_TIMEOUT +\
        WAIT_FENCE_FINAL_TIMEOUT) * MDP_MAX_FENCE_FD
#define MAX_TIMELINE_NAME_LEN 16

static DEFINE_MUTEX(msm_fb_notify_update_sem);
static void msmfb_no_update_notify_timer_cb(unsigned long data)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)data;
	if (!mfd)
		pr_err("%s mfd NULL\n", __func__);
	complete(&mfd->msmfb_no_update_notify);
}

static int msm_fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->cursor_update)
		return -ENODEV;

	return mfd->cursor_update(info, cursor);
}

unsigned char hdmi_prim_display = 0;

static ssize_t msm_fb_msm_fb_type(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct fb_info *fbi = dev_get_drvdata(dev);
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)fbi->par;

	switch (mfd->panel_info.type) {
	case NO_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "no panel\n");
		break;
	case MDDI_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "mddi panel\n");
		break;
	case LCDC_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "lcdc panel\n");
		break;
	case TV_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "tv panel\n");
		break;
	case HDMI_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "hdmi panel\n");
		break;
	case DTV_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "dtv panel\n");
		break;
	case WRITEBACK_PANEL:
		ret = snprintf(buf, PAGE_SIZE, "writeback panel\n");
		break;
	default:
		ret = snprintf(buf, PAGE_SIZE, "unknown panel\n");
		break;
	}

	return ret;
}

static ssize_t msm_fb_get_kcal(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d %d %d\n", kcal_r, kcal_g, kcal_b);
}

static ssize_t msm_fb_set_kcal(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	uint32_t r = 0, g = 0, b = 0;

	if (count > 12)
		return -EINVAL;

	sscanf(buf, "%d %d %d", &r, &g, &b);

	if (r < 0 || r > 255)
		return -EINVAL;
	if (g < 0 || g > 255)
		return -EINVAL;
	if (b < 0 || b > 255)
		return -EINVAL;

	kcal_r = r;
	kcal_g = g;
	kcal_b = b;

	pr_info("%s: r=%d g=%d b=%d\n", __func__, r, g, b);

	mdp4_mixer_gc_lut_setup(0);
	mdp4_mixer_gc_lut_setup(1);

	return count;
}

static DEVICE_ATTR(msm_fb_type, S_IRUGO, msm_fb_msm_fb_type, NULL);
static DEVICE_ATTR(kcal, S_IRUGO | S_IWUSR | S_IWGRP,
		   msm_fb_get_kcal, msm_fb_set_kcal);

static struct attribute *msm_fb_attrs[] = {
	&dev_attr_msm_fb_type.attr,
	&dev_attr_kcal.attr,
	NULL,
};
static struct attribute_group msm_fb_attr_group = {
	.attrs = msm_fb_attrs,
};

static int msm_fb_create_sysfs(struct msm_fb_data_type *mfd)
{
	int rc;

	rc = sysfs_create_group(&mfd->fbi->dev->kobj, &msm_fb_attr_group);
	if (rc)
		MSM_FB_ERR("%s: sysfs group creation failed, rc=%d\n", __func__,
			rc);
	return rc;
}
static void msm_fb_remove_sysfs(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);
	sysfs_remove_group(&mfd->fbi->dev->kobj, &msm_fb_attr_group);
}

static int msm_fb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);
	int ret;

	dev_info(dev, "%s\n", __func__);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	mfd->pdev = pdev;

	mfd->iclient = msm_ion_client_create(dev_name(dev));
	if (IS_ERR(mfd->iclient)) {
		ret = PTR_ERR(mfd->iclient);
		dev_err(dev, "failed to create ion client\n");
		return ret;
	}

	mfd->overlay_play_enable = true;

	bf_supported = mdp4_overlay_borderfill_supported();

	ret = msm_fb_register(mfd);
	if (ret)
		return ret;

	msm_fb_create_sysfs(mfd);

	if (mfd->vsync_init != NULL) {
		mfd->vsync_init(0);

		mfd->dev_attr.attr.name = "vsync_event";
		mfd->dev_attr.attr.mode = S_IRUGO;
		mfd->dev_attr.show = mfd->vsync_show;
		sysfs_attr_init(&mfd->dev_attr.attr);

		ret = sysfs_create_file(&mfd->fbi->dev->kobj,
					&mfd->dev_attr.attr);
		if (ret) {
			pr_err("%s: sysfs creation failed, ret=%d\n",
				__func__, ret);
			return ret;
		}

		kobject_uevent(&mfd->fbi->dev->kobj, KOBJ_ADD);
	}

	if (mfd->timeline == NULL) {
		char timeline_name[MAX_TIMELINE_NAME_LEN];
		snprintf(timeline_name, sizeof(timeline_name),
			 "mdp_fb_%d", mfd->index);
		mfd->timeline = sw_sync_timeline_create(timeline_name);
		if (mfd->timeline == NULL) {
			pr_err("%s: cannot create time line", __func__);
			return -ENOMEM;
		} else {
			mfd->timeline_value = 0;
		}
	}

	return 0;
}

static int msm_fb_remove(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd = platform_get_drvdata(pdev);

	msm_fb_pan_idle(mfd);

	msm_fb_remove_sysfs(pdev);

	if (mfd->msmfb_no_update_notify_timer.function)
		del_timer(&mfd->msmfb_no_update_notify_timer);
	complete(&mfd->msmfb_no_update_notify);
	complete(&mfd->msmfb_update_notify);

	/* remove /dev/fb* */
	unregister_framebuffer(mfd->fbi);

	ion_free(mfd->iclient, mfd->ihandle);

	return 0;
}

static int msm_fb_blank_sub(int blank_mode, struct fb_info *info)
{
	struct msm_fb_data_type *mfd = info->par;

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		if (mfd->panel_power_on)
			break;

		/* Wake panel, this will trigger chain wakeup. */
		pm_runtime_get_sync(mfd->panel_info.dev);
		mfd->panel_power_on = true;
		break;
	case FB_BLANK_POWERDOWN:
		if (!mfd->panel_power_on)
			break;

		if (mfd->msmfb_no_update_notify_timer.function)
			del_timer(&mfd->msmfb_no_update_notify_timer);
		complete(&mfd->msmfb_no_update_notify);

		/* clean fb to prevent displaying old fb */
		if (info->screen_base)
			memset(info->screen_base, 0x0, info->fix.smem_len);

		/* Put panel to sleep, this will trigger chain sleep. */
		pm_runtime_put_sync(mfd->panel_info.dev);
		mfd->panel_power_on = false;

		msm_fb_release_timeline(mfd);

		break;
	default:
		pr_err("%s: unsupported blank mode %d\n", __func__, blank_mode);
		break;
	};

	return 0;
}

static void msm_fb_fillrect(struct fb_info *info,
			    const struct fb_fillrect *rect)
{
	struct msm_fb_data_type *mfd = info->par;
	msm_fb_pan_idle(mfd);
	cfb_fillrect(info, rect);
	if (info->var.yoffset == 0) {
		struct fb_var_screeninfo var;

		var = info->var;
		var.reserved[0] = 0x54445055;
		var.reserved[1] = (rect->dy << 16) | (rect->dx);
		var.reserved[2] = ((rect->dy + rect->height) << 16) |
		    (rect->dx + rect->width);

		msm_fb_pan_display(&var, info);
	}
}

static void msm_fb_copyarea(struct fb_info *info,
			    const struct fb_copyarea *area)
{
	struct msm_fb_data_type *mfd = info->par;

	msm_fb_pan_idle(mfd);
	cfb_copyarea(info, area);
	if (info->var.yoffset == 0) {
		struct fb_var_screeninfo var;

		var = info->var;
		var.reserved[0] = 0x54445055;
		var.reserved[1] = (area->dy << 16) | (area->dx);
		var.reserved[2] = ((area->dy + area->height) << 16) |
		    (area->dx + area->width);

		msm_fb_pan_display(&var, info);
	}
}

static void msm_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct msm_fb_data_type *mfd = info->par;

	msm_fb_pan_idle(mfd);
	cfb_imageblit(info, image);
	if (info->var.yoffset == 0) {
		struct fb_var_screeninfo var;

		var = info->var;
		var.reserved[0] = 0x54445055;
		var.reserved[1] = (image->dy << 16) | (image->dx);
		var.reserved[2] = ((image->dy + image->height) << 16) |
		    (image->dx + image->width);

		msm_fb_pan_display(&var, info);
	}
}

static int msm_fb_blank(int blank_mode, struct fb_info *info)
{
	return msm_fb_blank_sub(blank_mode, info);
}

static int msm_fb_set_lut(struct fb_cmap *cmap, struct fb_info *info)
{
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->lut_update)
		return -ENODEV;

	mfd->lut_update(info, cmap);
	return 0;
}

static struct fb_ops msm_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = msm_fb_open,
	.fb_release = msm_fb_release,
	.fb_check_var = msm_fb_check_var,	/* vinfo check */
	.fb_set_par = msm_fb_set_par,	/* set the video mode according to info->var */
	.fb_blank = msm_fb_blank,	/* blank display */
	.fb_pan_display = msm_fb_pan_display,	/* pan display */
	.fb_fillrect = msm_fb_fillrect,	/* Draws a rectangle */
	.fb_copyarea = msm_fb_copyarea,	/* Copy data from area to another */
	.fb_imageblit = msm_fb_imageblit,	/* Draws a image to the display */
	.fb_ioctl = msm_fb_ioctl,	/* perform fb specific ioctl (optional) */
};

static int msm_fb_register(struct msm_fb_data_type *mfd)
{
	int ret = -ENODEV;
	int bpp;
	struct msm_panel_info *panel_info = &mfd->panel_info;
	struct fb_info *fbi = mfd->fbi;
	struct fb_fix_screeninfo *fix = &fbi->fix;
	struct fb_var_screeninfo *var = &fbi->var;
	int *id;

	ion_phys_addr_t phys;
	size_t size;

	/*
	 * fb info initialization
	 */

	fix->type_aux = 0;			/* if type == FB_TYPE_INTERLEAVED_PLANES */
	fix->visual = FB_VISUAL_TRUECOLOR;	/* True Color */
	fix->ywrapstep = 0;			/* No support */
	fix->mmio_start = 0;			/* No MMIO Address */
	fix->mmio_len = 0;			/* No MMIO Address */
	fix->accel = FB_ACCEL_NONE;		/* FB_ACCEL_MSM needes to be added in fb.h */

	var->xoffset = 0;			/* Offset from virtual to visible */
	var->yoffset = 0;			/* resolution */
	var->grayscale = 0;			/* No graylevels */
	var->nonstd = 0;			/* standard pixel format */
	var->activate = FB_ACTIVATE_VBL;	/* activate it at vsync */
	var->height = -1;			/* height of picture in mm */
	var->width = -1;			/* width of picture in mm */
	var->accel_flags = 0;			/* acceleration flags */
	var->sync = 0;				/* see FB_SYNC_* */
	var->rotate = 0;			/* angle we rotate counter clockwise */

	switch (mfd->fb_imgType) {
	case MDP_RGB_565:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 11;
		var->blue.length = 5;
		var->green.length = 6;
		var->red.length = 5;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 2;
		break;

	case MDP_RGB_888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 0;
		var->green.offset = 8;
		var->red.offset = 16;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 3;
		break;

	case MDP_ARGB_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 0;
		var->green.offset = 8;
		var->red.offset = 16;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 24;
		var->transp.length = 8;
		bpp = 4;
		break;

	case MDP_RGBA_8888:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->xpanstep = 1;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;
		var->blue.offset = 8;
		var->green.offset = 16;
		var->red.offset = 24;
		var->blue.length = 8;
		var->green.length = 8;
		var->red.length = 8;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 8;
		bpp = 4;
		break;

	case MDP_YCRYCB_H2V1:
		/* ToDo: need to check TV-Out YUV422i framebuffer format */
		/*       we might need to create new type define */
		fix->type = FB_TYPE_INTERLEAVED_PLANES;
		fix->xpanstep = 2;
		fix->ypanstep = 1;
		var->vmode = FB_VMODE_NONINTERLACED;

		/* how about R/G/B offset? */
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 11;
		var->blue.length = 5;
		var->green.length = 6;
		var->red.length = 5;
		var->blue.msb_right = 0;
		var->green.msb_right = 0;
		var->red.msb_right = 0;
		var->transp.offset = 0;
		var->transp.length = 0;
		bpp = 2;
		break;

	default:
		pr_err("%s: Invalid image type %d\n", __func__,
		       mfd->fb_imgType);
		ret = -EINVAL;
		return ret;
	}

	var->xres = panel_info->xres;
	var->yres = panel_info->yres;
	var->xres_virtual = panel_info->xres;
	var->yres_virtual = panel_info->yres * mfd->fb_page;
	var->bits_per_pixel = bpp * 8; /* FrameBuffer color depth */
	var->pixclock = mfd->panel_info.clk_rate;
	var->reserved[3] = panel_info->frame_rate;

	fix->line_length = panel_info->xres * bpp;

	mfd->ihandle = ion_alloc(mfd->iclient,
				 fix->line_length * var->yres_virtual,
				 SZ_4K, mfd->mem_hid, 0);
	ion_phys(mfd->iclient, mfd->ihandle, &phys, &size);

	fix->smem_start = phys;
	fix->smem_len = size;

	fbi->screen_base = ion_map_kernel(mfd->iclient, mfd->ihandle);

	/* Empty the buffer. */
	if (fbi->screen_base)
		memset(fbi->screen_base, 0x0, fix->smem_len);

	mfd->var_xres = panel_info->xres;
	mfd->var_yres = panel_info->yres;

	/*
	 * id field for fb app
	 */
	id = (int *)&mfd->panel;

	switch (mdp_rev) {
	case MDP_REV_20:
		snprintf(fix->id, sizeof(fix->id), "msmfb20_%x", (__u32) *id);
		break;
	case MDP_REV_22:
		snprintf(fix->id, sizeof(fix->id), "msmfb22_%x", (__u32) *id);
		break;
	case MDP_REV_30:
		snprintf(fix->id, sizeof(fix->id), "msmfb30_%x", (__u32) *id);
		break;
	case MDP_REV_303:
		snprintf(fix->id, sizeof(fix->id), "msmfb303_%x", (__u32) *id);
		break;
	case MDP_REV_31:
		snprintf(fix->id, sizeof(fix->id), "msmfb31_%x", (__u32) *id);
		break;
	case MDP_REV_40:
		snprintf(fix->id, sizeof(fix->id), "msmfb40_%x", (__u32) *id);
		break;
	case MDP_REV_41:
		snprintf(fix->id, sizeof(fix->id), "msmfb41_%x", (__u32) *id);
		break;
	case MDP_REV_42:
		snprintf(fix->id, sizeof(fix->id), "msmfb42_%x", (__u32) *id);
		break;
	case MDP_REV_43:
		snprintf(fix->id, sizeof(fix->id), "msmfb43_%x", (__u32) *id);
		break;
	case MDP_REV_44:
		snprintf(fix->id, sizeof(fix->id), "msmfb44_%x", (__u32) *id);
		break;
	default:
		snprintf(fix->id, sizeof(fix->id), "msmfb0_%x", (__u32) *id);
		break;
	}

	fbi->fbops = &msm_fb_ops;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->pseudo_palette = msm_fb_pseudo_palette;

	mfd->ref_cnt = 0;
	mfd->panel_power_on = false;

	init_timer(&mfd->msmfb_no_update_notify_timer);
	mfd->msmfb_no_update_notify_timer.function =
			msmfb_no_update_notify_timer_cb;
	mfd->msmfb_no_update_notify_timer.data = (unsigned long)mfd;
	init_completion(&mfd->msmfb_update_notify);
	init_completion(&mfd->msmfb_no_update_notify);
	init_completion(&mfd->commit_comp);
	mutex_init(&mfd->sync_mutex);
	INIT_WORK(&mfd->commit_work, msm_fb_commit_wq_handler);
	mfd->msm_fb_backup = kzalloc(sizeof(struct msm_fb_backup_type),
		GFP_KERNEL);
	if (mfd->msm_fb_backup == 0) {
		pr_err("error: not enough memory!\n");
		return -ENOMEM;
	}

	mfd->panel_power_on = false;

	/* cursor memory allocation */
	if (mfd->cursor_update) {
		mfd->cursor_buf = dma_alloc_coherent(NULL,
					MDP_CURSOR_SIZE,
					mfd->cursor_buf_phys,
					GFP_KERNEL);

		if (!mfd->cursor_buf)
			mfd->cursor_update = 0;
	}

	if (mfd->lut_update) {
		ret = fb_alloc_cmap(&fbi->cmap, 256, 0);
		if (ret)
			printk(KERN_ERR "%s: fb_alloc_cmap() failed!\n",
					__func__);
	}

	if (register_framebuffer(fbi) < 0) {
		if (mfd->lut_update)
			fb_dealloc_cmap(&fbi->cmap);

		if (mfd->cursor_buf)
			dma_free_coherent(NULL,
				MDP_CURSOR_SIZE,
				mfd->cursor_buf,
				*mfd->cursor_buf_phys);
		return -EPERM;
	}

	MSM_FB_INFO
	    ("FrameBuffer[%d] %dx%d size=%d bytes is registered successfully!\n",
	     mfd->index, fbi->var.xres, fbi->var.yres, fbi->fix.smem_len);

	return 0;
}

static int msm_fb_open(struct fb_info *info, int user)
{
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->ref_cnt) {
		mdp_set_dma_pan_info(info, NULL, true);
	}

	mfd->ref_cnt++;

	return 0;
}

static int msm_fb_release(struct fb_info *info, int user)
{
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->ref_cnt) {
		pr_err("%s: No references\n", __func__);
		return -EINVAL;
	}

	mfd->ref_cnt--;

	if (!mfd->ref_cnt)
		msm_fb_blank_sub(FB_BLANK_POWERDOWN, mfd->fbi);

	return 0;
}

void msm_fb_wait_for_fence(struct msm_fb_data_type *mfd)
{
	int i, ret = 0;
	/* buf sync */
	for (i = 0; i < mfd->acq_fen_cnt; i++) {
		ret = sync_fence_wait(mfd->acq_fen[i],
				WAIT_FENCE_FIRST_TIMEOUT);
		if (ret == -ETIME) {
			pr_warn("%s: sync_fence_wait timed out!"
				"Waiting %ld more seconds\n",
				__func__,WAIT_FENCE_FINAL_TIMEOUT/MSEC_PER_SEC);
			ret = sync_fence_wait(mfd->acq_fen[i],
					WAIT_FENCE_FINAL_TIMEOUT);
		}
		if (ret < 0) {
			pr_err("%s: sync_fence_wait failed! ret = %x\n",
				__func__, ret);
			break;
		}
		sync_fence_put(mfd->acq_fen[i]);
	}
	if (ret < 0) {
		while (i < mfd->acq_fen_cnt) {
			sync_fence_put(mfd->acq_fen[i]);
			i++;
		}
	}
	mfd->acq_fen_cnt = 0;
}
int msm_fb_signal_timeline(struct msm_fb_data_type *mfd)
{
	mutex_lock(&mfd->sync_mutex);
	if (mfd->timeline && !list_empty((const struct list_head *)
				(&(mfd->timeline->obj.active_list_head)))) {
		sw_sync_timeline_inc(mfd->timeline, 1);
		mfd->timeline_value++;
	}
	mfd->last_rel_fence = mfd->cur_rel_fence;
	mfd->cur_rel_fence = 0;
	mutex_unlock(&mfd->sync_mutex);
	return 0;
}

static void msm_fb_release_timeline(struct msm_fb_data_type *mfd)
{
	mutex_lock(&mfd->sync_mutex);
	if (mfd->timeline) {
		sw_sync_timeline_inc(mfd->timeline, 2);
		mfd->timeline_value += 2;
	}
	mfd->last_rel_fence = 0;
	mfd->cur_rel_fence = 0;
	mutex_unlock(&mfd->sync_mutex);
}

static DEFINE_SEMAPHORE(msm_fb_pan_sem);

static int msm_fb_pan_idle(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	mutex_lock(&mfd->sync_mutex);
	if (mfd->is_committing) {
		mutex_unlock(&mfd->sync_mutex);
		ret = wait_for_completion_interruptible_timeout(
				&mfd->commit_comp,
			msecs_to_jiffies(WAIT_DISP_OP_TIMEOUT));
		if (ret < 0)
			ret = -ERESTARTSYS;
		else if (!ret)
			pr_err("%s wait for commit_comp timeout %d %d",
				__func__, ret, mfd->is_committing);
		if (ret <= 0) {
			mutex_lock(&mfd->sync_mutex);
			mfd->is_committing = 0;
			complete_all(&mfd->commit_comp);
			mutex_unlock(&mfd->sync_mutex);
		}
	} else {
		mutex_unlock(&mfd->sync_mutex);
	}
	return ret;
}
static int msm_fb_pan_display_ex(struct fb_info *info,
		struct mdp_display_commit *disp_commit)
{
	struct msm_fb_data_type *mfd = info->par;
	struct msm_fb_backup_type *fb_backup;
	struct fb_var_screeninfo *var = &disp_commit->var;
	u32 wait_for_finish = disp_commit->wait_for_finish;
	int ret = 0;
	if (disp_commit->flags &
		MDP_DISPLAY_COMMIT_OVERLAY) {
		if (!mfd->panel_power_on) /* suspended */
			return -EPERM;
	} else {
		/*
		 * WFD panel info was not getting updated,
		 * in case of resolution other than 1280x720
		 */
		mfd->var_xres = info->var.xres;
		mfd->var_yres = info->var.yres;
		/*
		 * If framebuffer is 2, io pan display is not allowed.
		 */
		if (bf_supported && info->node == 2) {
			pr_err("%s: no pan display for fb%d!",
				   __func__, info->node);
			return -EPERM;
		}

		if (var->xoffset > (info->var.xres_virtual - info->var.xres))
			return -EINVAL;

		if (var->yoffset > (info->var.yres_virtual - info->var.yres))
			return -EINVAL;
	}
	msm_fb_pan_idle(mfd);

	mutex_lock(&mfd->sync_mutex);

	if (!(disp_commit->flags &
		MDP_DISPLAY_COMMIT_OVERLAY)) {
		if (info->fix.xpanstep)
			info->var.xoffset =
				(var->xoffset / info->fix.xpanstep) *
					info->fix.xpanstep;

		if (info->fix.ypanstep)
			info->var.yoffset =
				(var->yoffset / info->fix.ypanstep) *
					info->fix.ypanstep;
	}
	fb_backup = (struct msm_fb_backup_type *)mfd->msm_fb_backup;
	memcpy(&fb_backup->info, info, sizeof(struct fb_info));
	memcpy(&fb_backup->disp_commit, disp_commit,
		sizeof(struct mdp_display_commit));
	mfd->is_committing = 1;
	reinit_completion(&mfd->commit_comp);
	schedule_work(&mfd->commit_work);
	mutex_unlock(&mfd->sync_mutex);
	if (wait_for_finish)
		msm_fb_pan_idle(mfd);

	return ret;
}

static int msm_fb_pan_display(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct mdp_display_commit disp_commit;
	memset(&disp_commit, 0, sizeof(disp_commit));
	disp_commit.var = *var;
	disp_commit.wait_for_finish = true;
	return msm_fb_pan_display_ex(info, &disp_commit);
}

static int msm_fb_pan_display_sub(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	struct mdp_dirty_region dirty;
	struct mdp_dirty_region *dirtyPtr = NULL;
	struct msm_fb_data_type *mfd = info->par;

	/*
	 * If framebuffer is 2, io pen display is not allowed.
	 */
	if (bf_supported && info->node == 2) {
		pr_err("%s: no pan display for fb%d!",
		       __func__, info->node);
		return -EPERM;
	}

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;

	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	if (info->fix.xpanstep)
		info->var.xoffset =
		    (var->xoffset / info->fix.xpanstep) * info->fix.xpanstep;

	if (info->fix.ypanstep)
		info->var.yoffset =
		    (var->yoffset / info->fix.ypanstep) * info->fix.ypanstep;

	/* "UPDT" */
	if (var->reserved[0] == 0x54445055) {

		dirty.xoffset = var->reserved[1] & 0xffff;
		dirty.yoffset = (var->reserved[1] >> 16) & 0xffff;

		if ((var->reserved[2] & 0xffff) <= dirty.xoffset)
			return -EINVAL;
		if (((var->reserved[2] >> 16) & 0xffff) <= dirty.yoffset)
			return -EINVAL;

		dirty.width = (var->reserved[2] & 0xffff) - dirty.xoffset;
		dirty.height =
		    ((var->reserved[2] >> 16) & 0xffff) - dirty.yoffset;
		info->var.yoffset = var->yoffset;

		if (dirty.xoffset < 0)
			return -EINVAL;

		if (dirty.yoffset < 0)
			return -EINVAL;

		if ((dirty.xoffset + dirty.width) > info->var.xres)
			return -EINVAL;

		if ((dirty.yoffset + dirty.height) > info->var.yres)
			return -EINVAL;

		if ((dirty.width <= 0) || (dirty.height <= 0))
			return -EINVAL;

		dirtyPtr = &dirty;
	}
	complete(&mfd->msmfb_update_notify);
	mutex_lock(&msm_fb_notify_update_sem);
	if (mfd->msmfb_no_update_notify_timer.function)
		del_timer(&mfd->msmfb_no_update_notify_timer);

	mfd->msmfb_no_update_notify_timer.expires = jiffies + (2 * HZ);
	add_timer(&mfd->msmfb_no_update_notify_timer);
	mutex_unlock(&msm_fb_notify_update_sem);

	down(&msm_fb_pan_sem);
	msm_fb_wait_for_fence(mfd);

	mdp_set_dma_pan_info(info, dirtyPtr,
			     (var->activate & FB_ACTIVATE_VBL));
	/* async call */

	mdp_dma_pan_update(info);
	msm_fb_signal_timeline(mfd);

	up(&msm_fb_pan_sem);

	return 0;
}

static void msm_fb_commit_wq_handler(struct work_struct *work)
{
	struct msm_fb_data_type *mfd;
	struct fb_var_screeninfo *var;
	struct fb_info *info;
	struct msm_fb_backup_type *fb_backup;

	mfd = container_of(work, struct msm_fb_data_type, commit_work);
	fb_backup = (struct msm_fb_backup_type *)mfd->msm_fb_backup;
	info = &fb_backup->info;
	if (fb_backup->disp_commit.flags &
		MDP_DISPLAY_COMMIT_OVERLAY) {
			mdp4_overlay_commit(info);
	} else {
		var = &fb_backup->disp_commit.var;
		msm_fb_pan_display_sub(var, info);
	}

	mutex_lock(&mfd->sync_mutex);
	mfd->is_committing = 0;
	complete_all(&mfd->commit_comp);
	mutex_unlock(&mfd->sync_mutex);
}

static int msm_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct msm_fb_data_type *mfd = info->par;

	msm_fb_pan_idle(mfd);
	if (var->rotate != FB_ROTATE_UR)
		return -EINVAL;
	if (var->grayscale != info->var.grayscale)
		return -EINVAL;

	switch (var->bits_per_pixel) {
	case 16:
		if ((var->green.offset != 5) ||
			!((var->blue.offset == 11)
				|| (var->blue.offset == 0)) ||
			!((var->red.offset == 11)
				|| (var->red.offset == 0)) ||
			(var->blue.length != 5) ||
			(var->green.length != 6) ||
			(var->red.length != 5) ||
			(var->blue.msb_right != 0) ||
			(var->green.msb_right != 0) ||
			(var->red.msb_right != 0) ||
			(var->transp.offset != 0) ||
			(var->transp.length != 0))
				return -EINVAL;
		break;

	case 24:
		if ((var->blue.offset != 0) ||
			(var->green.offset != 8) ||
			(var->red.offset != 16) ||
			(var->blue.length != 8) ||
			(var->green.length != 8) ||
			(var->red.length != 8) ||
			(var->blue.msb_right != 0) ||
			(var->green.msb_right != 0) ||
			(var->red.msb_right != 0) ||
			!(((var->transp.offset == 0) &&
				(var->transp.length == 0)) ||
			  ((var->transp.offset == 24) &&
				(var->transp.length == 8))))
				return -EINVAL;
		break;

	case 32:
		/* Figure out if the user meant RGBA or ARGB
		   and verify the position of the RGB components */

		if (var->transp.offset == 24) {
			if ((var->blue.offset != 0) ||
			    (var->green.offset != 8) ||
			    (var->red.offset != 16))
				return -EINVAL;
		} else if (var->transp.offset == 0) {
			if ((var->blue.offset != 8) ||
			    (var->green.offset != 16) ||
			    (var->red.offset != 24))
				return -EINVAL;
		} else
			return -EINVAL;

		/* Check the common values for both RGBA and ARGB */

		if ((var->blue.length != 8) ||
		    (var->green.length != 8) ||
		    (var->red.length != 8) ||
		    (var->transp.length != 8) ||
		    (var->blue.msb_right != 0) ||
		    (var->green.msb_right != 0) ||
		    (var->red.msb_right != 0))
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	if ((var->xres_virtual <= 0) || (var->yres_virtual <= 0))
		return -EINVAL;

	if (!bf_supported ||
		(info->node != 1 && info->node != 2))
		if (info->fix.smem_len <
		    (var->xres_virtual*
		     var->yres_virtual*
		     (var->bits_per_pixel/8)))
			return -EINVAL;

	if ((var->xres == 0) || (var->yres == 0))
		return -EINVAL;

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;

	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	return 0;
}

static int msm_fb_set_par(struct fb_info *info)
{
	struct msm_fb_data_type *mfd = info->par;
	struct fb_var_screeninfo *var = &info->var;
	int bpp;

	msm_fb_pan_idle(mfd);

	switch (var->bits_per_pixel) {
	case 16:
		if (var->red.offset == 0)
			mfd->fb_imgType = MDP_BGR_565;
		else
			mfd->fb_imgType = MDP_RGB_565;
		bpp = 2;
		break;
	case 24:
		if ((var->transp.offset == 0) && (var->transp.length == 0)) {
			mfd->fb_imgType = MDP_RGB_888;
		} else if ((var->transp.offset == 24) &&
				(var->transp.length == 8)) {
			mfd->fb_imgType = MDP_ARGB_8888;
			info->var.bits_per_pixel = 32;
		}
		bpp = 3;
		break;
	case 32:
		if (var->transp.offset == 24)
			mfd->fb_imgType = MDP_ARGB_8888;
		else
			mfd->fb_imgType = MDP_RGBA_8888;
		bpp = 4;
		break;
	default:
		return -EINVAL;
	}

	mfd->fbi->fix.line_length = var->xres * bpp;

	return 0;
}

static int msmfb_vsync_ctrl(struct fb_info *info, void __user *argp)
{
	int enable, ret;
	struct msm_fb_data_type *mfd = info->par;

	ret = copy_from_user(&enable, argp, sizeof(enable));
	if (ret) {
		pr_err("%s:msmfb_overlay_vsync ioctl failed", __func__);
		return ret;
	}

	if (mfd->vsync_ctrl)
		mfd->vsync_ctrl(enable);
	else {
		pr_err("%s: Vsync IOCTL not supported", __func__);
		return -EINVAL;
	}

	return 0;
}

static int msmfb_overlay_get(struct fb_info *info, void __user *p)
{
	struct mdp_overlay req;
	int ret;

	if (copy_from_user(&req, p, sizeof(req)))
		return -EFAULT;

	ret = mdp4_overlay_get(info, &req);
	if (ret) {
		printk(KERN_ERR "%s: ioctl failed \n",
			__func__);
		return ret;
	}
	if (copy_to_user(p, &req, sizeof(req))) {
		printk(KERN_ERR "%s: copy2user failed \n",
			__func__);
		return -EFAULT;
	}

	return 0;
}

static int msmfb_overlay_set(struct fb_info *info, void __user *p)
{
	struct mdp_overlay req;
	int ret;

	if (copy_from_user(&req, p, sizeof(req)))
		return -EFAULT;

	ret = mdp4_overlay_set(info, &req);
	if (ret) {
		printk(KERN_ERR "%s: ioctl failed, rc=%d\n",
			__func__, ret);
		return ret;
	}

	if (copy_to_user(p, &req, sizeof(req))) {
		printk(KERN_ERR "%s: copy2user failed \n",
			__func__);
		return -EFAULT;
	}

	return 0;
}

static int msmfb_overlay_unset(struct fb_info *info, unsigned long *argp)
{
	int ret, ndx;

	ret = copy_from_user(&ndx, argp, sizeof(ndx));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	return mdp4_overlay_unset(info, ndx);
}

static int msmfb_overlay_vsync_ctrl(struct fb_info *info, void __user *argp)
{
	int ret;
	int enable;

	ret = copy_from_user(&enable, argp, sizeof(enable));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	ret = mdp4_overlay_vsync_ctrl(info, enable);

	return ret;
}

static int msmfb_overlay_play_wait(struct fb_info *info, unsigned long *argp)
{
	int ret;
	struct msmfb_overlay_data req;
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->overlay_play_enable) /* nothing to do */
		return 0;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	ret = mdp4_overlay_play_wait(info, &req);

	return ret;
}

static int msmfb_overlay_play(struct fb_info *info, unsigned long *argp)
{
	int ret;
	struct msmfb_overlay_data req;
	struct msm_fb_data_type *mfd = info->par;

	if (!mfd->overlay_play_enable) /* nothing to do */
		return 0;

	if (!mfd->panel_power_on) /* suspended */
		return -EPERM;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	complete(&mfd->msmfb_update_notify);
	mutex_lock(&msm_fb_notify_update_sem);
	if (mfd->msmfb_no_update_notify_timer.function)
		del_timer(&mfd->msmfb_no_update_notify_timer);

	mfd->msmfb_no_update_notify_timer.expires = jiffies + (2 * HZ);
	add_timer(&mfd->msmfb_no_update_notify_timer);
	mutex_unlock(&msm_fb_notify_update_sem);

	ret = mdp4_overlay_play(info, &req);

	return ret;
}

static int msmfb_overlay_play_enable(struct fb_info *info, unsigned long *argp)
{
	int ret, enable;
	struct msm_fb_data_type *mfd = info->par;

	ret = copy_from_user(&enable, argp, sizeof(enable));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	mfd->overlay_play_enable = enable;

	return 0;
}

static int msmfb_overlay_blt(struct fb_info *info, unsigned long *argp)
{
	int ret;
	struct msmfb_overlay_blt req;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("%s: failed ret=%d\n", __func__, ret);
		return ret;
	}

	ret = mdp4_overlay_blt(info, &req);

	return ret;
}

static int msmfb_overlay_3d_sbys(struct fb_info *info, unsigned long *argp)
{
	int	ret;
	struct msmfb_overlay_3d req;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("%s:msmfb_overlay_3d_ctrl ioctl failed\n",
			__func__);
		return ret;
	}

	ret = mdp4_overlay_3d_sbys(info, &req);

	return ret;
}

static int msmfb_mixer_info(struct fb_info *info, unsigned long *argp)
{
	int     ret, cnt;
	struct msmfb_mixer_info_req req;

	ret = copy_from_user(&req, argp, sizeof(req));
	if (ret) {
		pr_err("%s: failed\n", __func__);
		return ret;
	}

	cnt = mdp4_mixer_info(req.mixer_num, req.info);
	req.cnt = cnt;
	ret = copy_to_user(argp, &req, sizeof(req));
	if (ret)
		pr_err("%s:msmfb_overlay_blt_off ioctl failed\n",
		__func__);

	return cnt;
}

DEFINE_SEMAPHORE(msm_fb_ioctl_ppp_sem);
DEFINE_MUTEX(msm_fb_ioctl_lut_sem);

/* Set color conversion matrix from user space */
static void msmfb_set_color_conv(struct mdp_csc *p)
{
	mdp4_vg_csc_update(p);
}

static int msmfb_notify_update(struct fb_info *info, void __user *argp)
{
	int ret;
	unsigned int notify;
	struct msm_fb_data_type *mfd = info->par;

	ret = copy_from_user(&notify, argp, sizeof(unsigned int));
	if (ret) {
		pr_err("%s:ioctl failed\n", __func__);
		return ret;
	}

	if (notify > NOTIFY_UPDATE_STOP)
		return -EINVAL;

	if (notify == NOTIFY_UPDATE_START) {
		reinit_completion(&mfd->msmfb_update_notify);
		ret = wait_for_completion_interruptible_timeout(
		&mfd->msmfb_update_notify, 4*HZ);
	} else {
		reinit_completion(&mfd->msmfb_no_update_notify);
		ret = wait_for_completion_interruptible_timeout(
		&mfd->msmfb_no_update_notify, 4*HZ);
	}
	if (ret == 0)
		ret = -ETIMEDOUT;
	return (ret > 0) ? 0 : ret;
}

static int msmfb_handle_buf_sync_ioctl(struct msm_fb_data_type *mfd,
				       struct mdp_buf_sync *buf_sync)
{
	int i, ret = 0;
	u32 threshold;
	int acq_fen_fd[MDP_MAX_FENCE_FD];
	struct sync_fence *fence;

	if ((buf_sync->acq_fen_fd_cnt > MDP_MAX_FENCE_FD) ||
		(mfd->timeline == NULL))
		return -EINVAL;

	if (!mfd->panel_power_on)
		return -EPERM;

	if (buf_sync->acq_fen_fd_cnt)
		ret = copy_from_user(acq_fen_fd, buf_sync->acq_fen_fd,
				buf_sync->acq_fen_fd_cnt * sizeof(int));
	if (ret) {
		pr_err("%s:copy_from_user failed", __func__);
		return ret;
	}
	mutex_lock(&mfd->sync_mutex);
	for (i = 0; i < buf_sync->acq_fen_fd_cnt; i++) {
		fence = sync_fence_fdget(acq_fen_fd[i]);
		if (fence == NULL) {
			pr_info("%s: null fence! i=%d fd=%d\n", __func__, i,
				acq_fen_fd[i]);
			ret = -EINVAL;
			break;
		}
		mfd->acq_fen[i] = fence;
	}
	mfd->acq_fen_cnt = i;
	if (ret)
		goto buf_sync_err_1;
	if (buf_sync->flags & MDP_BUF_SYNC_FLAG_WAIT) {
		msm_fb_wait_for_fence(mfd);
	}
	if (mfd->panel.type == WRITEBACK_PANEL)
		threshold = 1;
	else
		threshold = 2;
	mfd->cur_rel_sync_pt = sw_sync_pt_create(mfd->timeline,
			mfd->timeline_value + threshold);
	if (mfd->cur_rel_sync_pt == NULL) {
		pr_err("%s: cannot create sync point", __func__);
		ret = -ENOMEM;
		goto buf_sync_err_1;
	}
	/* create fence */
	mfd->cur_rel_fence = sync_fence_create("mdp-fence",
			mfd->cur_rel_sync_pt);
	if (mfd->cur_rel_fence == NULL) {
		sync_pt_free(mfd->cur_rel_sync_pt);
		mfd->cur_rel_sync_pt = NULL;
		pr_err("%s: cannot create fence", __func__);
		ret = -ENOMEM;
		goto buf_sync_err_1;
	}
	/* create fd */
	mfd->cur_rel_fen_fd = get_unused_fd_flags(0);
	if (mfd->cur_rel_fen_fd < 0) {
		pr_err("%s: get_unused_fd_flags failed", __func__);
		ret  = -EIO;
		goto buf_sync_err_2;
	}
	sync_fence_install(mfd->cur_rel_fence, mfd->cur_rel_fen_fd);
	ret = copy_to_user(buf_sync->rel_fen_fd,
		&mfd->cur_rel_fen_fd, sizeof(int));
	if (ret) {
		pr_err("%s:copy_to_user failed", __func__);
		goto buf_sync_err_3;
	}
	mutex_unlock(&mfd->sync_mutex);
	return ret;
buf_sync_err_3:
	put_unused_fd(mfd->cur_rel_fen_fd);
buf_sync_err_2:
	sync_fence_put(mfd->cur_rel_fence);
	mfd->cur_rel_fence = NULL;
	mfd->cur_rel_fen_fd = 0;
buf_sync_err_1:
	for (i = 0; i < mfd->acq_fen_cnt; i++)
		sync_fence_put(mfd->acq_fen[i]);
	mfd->acq_fen_cnt = 0;
	mutex_unlock(&mfd->sync_mutex);
	return ret;
}

static int msmfb_display_commit(struct fb_info *info, unsigned long *argp)
{
	int ret;
	struct mdp_display_commit disp_commit;

	ret = copy_from_user(&disp_commit, argp, sizeof(disp_commit));
	if (ret) {
		pr_err("%s:copy_from_user failed", __func__);
		return ret;
	}

	ret = msm_fb_pan_display_ex(info, &disp_commit);

	return ret;
}

static int msmfb_get_metadata(struct msm_fb_data_type *mfd,
			      struct msmfb_metadata *metadata_ptr)
{
	int ret = 0;
	switch (metadata_ptr->op) {
	case metadata_op_frame_rate:
		metadata_ptr->data.panel_frame_rate =
			mdp_get_panel_framerate(mfd);
		break;
	default:
		pr_warn("Unsupported request to MDP META IOCTL.\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int msm_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct msm_fb_data_type *mfd;
	void __user *argp = (void __user *)arg;
	struct fb_cursor cursor;
	struct fb_cmap cmap;
	struct mdp_histogram_data hist;
	struct mdp_histogram_start_req hist_req;
	uint32_t block;
	struct mdp_csc csc_matrix;
	struct mdp_page_protection fb_page_protection;
	struct mdp_buf_sync buf_sync;
	struct msmfb_metadata mdp_metadata;
	int ret = 0;

	if (!info || !info->par)
		return -EINVAL;

	mfd = info->par;

	msm_fb_pan_idle(mfd);

	switch (cmd) {
	case MSMFB_OVERLAY_GET:
		ret = msmfb_overlay_get(info, argp);
		break;
	case MSMFB_OVERLAY_SET:
		ret = msmfb_overlay_set(info, argp);
		break;
	case MSMFB_OVERLAY_UNSET:
		ret = msmfb_overlay_unset(info, argp);
		break;
	case MSMFB_OVERLAY_PLAY:
		ret = msmfb_overlay_play(info, argp);
		break;
	case MSMFB_OVERLAY_PLAY_ENABLE:
		ret = msmfb_overlay_play_enable(info, argp);
		break;
	case MSMFB_OVERLAY_PLAY_WAIT:
		ret = msmfb_overlay_play_wait(info, argp);
		break;
	case MSMFB_OVERLAY_BLT:
		ret = msmfb_overlay_blt(info, argp);
		break;
	case MSMFB_OVERLAY_3D:
		ret = msmfb_overlay_3d_sbys(info, argp);
		break;
	case MSMFB_MIXER_INFO:
		ret = msmfb_mixer_info(info, argp);
		break;
	case MSMFB_WRITEBACK_INIT:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_START:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_STOP:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_QUEUE_BUFFER:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_DEQUEUE_BUFFER:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_TERMINATE:
		ret = -ENOTSUPP;
		break;
	case MSMFB_WRITEBACK_SET_MIRRORING_HINT:
		ret = -ENOTSUPP;
		break;
	case MSMFB_VSYNC_CTRL:
	case MSMFB_OVERLAY_VSYNC_CTRL:
		down(&msm_fb_ioctl_ppp_sem);
		if (mdp_rev >= MDP_REV_40)
			ret = msmfb_overlay_vsync_ctrl(info, argp);
		else
			ret = msmfb_vsync_ctrl(info, argp);
		up(&msm_fb_ioctl_ppp_sem);
		break;

	/* Ioctl for setting ccs matrix from user space */
	case MSMFB_SET_CCS_MATRIX:
		ret = copy_from_user(&csc_matrix, argp, sizeof(csc_matrix));
		if (ret) {
			pr_err("%s:MSMFB_SET_CSC_MATRIX ioctl failed\n",
				__func__);
			break;
		}
		down(&msm_fb_ioctl_ppp_sem);
		msmfb_set_color_conv(&csc_matrix);
		up(&msm_fb_ioctl_ppp_sem);

		break;

	/* Ioctl for getting ccs matrix to user space */
	case MSMFB_GET_CCS_MATRIX:
		ret = -EINVAL;
		break;

	case MSMFB_GRP_DISP:
		ret = -EFAULT;
		break;

	case MSMFB_CURSOR:
		ret = copy_from_user(&cursor, argp, sizeof(cursor));
		if (ret)
			break;

		ret = msm_fb_cursor(info, &cursor);
		break;

	case MSMFB_SET_LUT:
		ret = copy_from_user(&cmap, argp, sizeof(cmap));
		if (ret)
			break;

		mutex_lock(&msm_fb_ioctl_lut_sem);
		ret = msm_fb_set_lut(&cmap, info);
		mutex_unlock(&msm_fb_ioctl_lut_sem);
		break;

	case MSMFB_HISTOGRAM:
		if (!mfd->panel_power_on) {
			ret = -EPERM;
			break;
		}

		if (!mfd->do_histogram) {
			ret = -ENODEV;
			break;
		}

		ret = copy_from_user(&hist, argp, sizeof(hist));
		if (ret)
			break;

		ret = mfd->do_histogram(info, &hist);
		break;

	case MSMFB_HISTOGRAM_START:
		if (!mfd->panel_power_on) {
			ret = -EPERM;
			break;
		}

		if (!mfd->start_histogram) {
			ret = -ENODEV;
			break;
		}

		ret = copy_from_user(&hist_req, argp, sizeof(hist_req));
		if (ret)
			break;

		ret = mfd->start_histogram(&hist_req);
		break;

	case MSMFB_HISTOGRAM_STOP:
		if (!mfd->stop_histogram) {
			ret = -ENODEV;
			break;
		}

		ret = copy_from_user(&block, argp, sizeof(int));
		if (ret)
			break;

		ret = mfd->stop_histogram(info, block);
		break;


	case MSMFB_GET_PAGE_PROTECTION:
		fb_page_protection.page_protection
			= MDP_FB_PAGE_PROTECTION_WRITECOMBINE;
		ret = copy_to_user(argp, &fb_page_protection,
				sizeof(fb_page_protection));
		break;

	case MSMFB_NOTIFY_UPDATE:
		ret = msmfb_notify_update(info, argp);
		break;

	case MSMFB_BUFFER_SYNC:
		ret = copy_from_user(&buf_sync, argp, sizeof(buf_sync));
		if (ret)
			break;

		ret = msmfb_handle_buf_sync_ioctl(mfd, &buf_sync);

		if (!ret)
			ret = copy_to_user(argp, &buf_sync, sizeof(buf_sync));
		break;

	case MSMFB_DISPLAY_COMMIT:
		ret = msmfb_display_commit(info, argp);
		break;

	case MSMFB_METADATA_GET:
		ret = copy_from_user(&mdp_metadata, argp, sizeof(mdp_metadata));
		if (ret)
			break;

		ret = msmfb_get_metadata(mfd, &mdp_metadata);
		if (!ret)
			ret = copy_to_user(argp, &mdp_metadata,
				sizeof(mdp_metadata));

		break;

	default:
		MSM_FB_INFO("MDP: unknown ioctl (cmd=%x) received!\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

struct msm_fb_data_type *msm_fb_alloc_device(struct device *dev)
{
	struct fb_info *fbi;
	struct msm_fb_data_type *mfd;

	fbi = framebuffer_alloc(sizeof(*mfd), dev);
	if (!fbi) {
		dev_err(dev, "failed to allocate framebuffer\n");
		return ERR_PTR(-ENOMEM);
	}

	mfd = fbi->par;

	mfd->key = MFD_KEY;
	mfd->fbi = fbi;
	mfd->fb_page = MSM_FB_NUM;
	mfd->index = fbi_list_index;

	fbi_list[fbi_list_index++] = fbi;

	return mfd;
}

int get_fb_phys_info(unsigned long *start, unsigned long *len, int fb_num,
		     int subsys_id)
{
	struct fb_info *info;

	if (fb_num > MAX_FBI_LIST ||
		(subsys_id != DISPLAY_SUBSYSTEM_ID &&
		 subsys_id != ROTATOR_SUBSYSTEM_ID)) {
		pr_err("%s(): Invalid parameters\n", __func__);
		return -1;
	}

	info = fbi_list[fb_num];
	if (!info) {
		pr_err("%s(): info is NULL\n", __func__);
		return -1;
	}

	*start = info->fix.smem_start;
	*len = info->fix.smem_len;

	return 0;
}
EXPORT_SYMBOL(get_fb_phys_info);

static int msm_fb_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "suspending\n");
	return 0;
}

static int msm_fb_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "suspending\n");
	return 0;
}

static struct dev_pm_ops msm_fb_dev_pm_ops = {
	.runtime_suspend = msm_fb_runtime_suspend,
	.runtime_resume = msm_fb_runtime_resume,
};

static struct platform_driver msm_fb_driver = {
	.probe = msm_fb_probe,
	.remove = msm_fb_remove,
	.driver = {
		.name = "msm_fb",
		.pm = &msm_fb_dev_pm_ops,
	},
};
module_platform_driver(msm_fb_driver);

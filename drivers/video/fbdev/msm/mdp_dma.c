/* Copyright (c) 2008-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "msm_fb.h"

extern struct workqueue_struct *mdp_dma_wq;
extern bool align_buffer;

static int calc_fb_offset(struct msm_fb_data_type *mfd, struct fb_info *fbi,
			  int bpp)
{
	struct msm_panel_info *panel_info = &mfd->panel_info;
	int remainder, yres, offset;

	if (!align_buffer)
		return fbi->var.xoffset * bpp +
			fbi->var.yoffset * fbi->fix.line_length;

	yres = panel_info->yres;
	remainder = (fbi->fix.line_length*yres) & (PAGE_SIZE - 1);

	if (!remainder)
		remainder = PAGE_SIZE;

	if (fbi->var.yoffset < yres) {
		offset = (fbi->var.xoffset * bpp);
				/* iBuf->buf +=	fbi->var.xoffset * bpp + 0 *
				yres * fbi->fix.line_length; */
	} else if (fbi->var.yoffset >= yres && fbi->var.yoffset < 2 * yres) {
		offset = (fbi->var.xoffset * bpp + yres *
		fbi->fix.line_length + PAGE_SIZE - remainder);
	} else {
		offset = (fbi->var.xoffset * bpp + 2 * yres *
		fbi->fix.line_length + 2 * (PAGE_SIZE - remainder));
	}
	return offset;
}

void mdp_set_dma_pan_info(struct fb_info *info, struct mdp_dirty_region *dirty,
			  bool sync)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	struct fb_info *fbi = mfd->fbi;
	MDPIBUF *iBuf;
	int bpp = info->var.bits_per_pixel / 8;

	iBuf = &mfd->ibuf;

	iBuf->buf = (uint8_t *) info->fix.smem_start;
	iBuf->buf += calc_fb_offset(mfd, fbi, bpp);

	iBuf->ibuf_width = info->var.xres_virtual;
	iBuf->bpp = bpp;

	iBuf->vsync_enable = sync;

	if (dirty) {
		/*
		 * ToDo: dirty region check inside var.xoffset+xres
		 * <-> var.yoffset+yres
		 */
		iBuf->dma_x = dirty->xoffset % info->var.xres;
		iBuf->dma_y = dirty->yoffset % info->var.yres;
		iBuf->dma_w = dirty->width;
		iBuf->dma_h = dirty->height;
	} else {
		iBuf->dma_x = 0;
		iBuf->dma_y = 0;
		iBuf->dma_w = info->var.xres;
		iBuf->dma_h = info->var.yres;
	}
}

void mdp_dma_pan_update(struct fb_info *info)
{
	struct msm_fb_data_type *mfd = (struct msm_fb_data_type *)info->par;
	mfd->dma_fnc(mfd);
}

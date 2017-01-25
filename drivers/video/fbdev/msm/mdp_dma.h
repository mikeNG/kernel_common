/* Copyright (c) 2008-2013, The Linux Foundation. All rights reserved.
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
 *
 */


#ifndef __MDP_DMA_H__
#define __MDP_DMA_H__

struct mdp_dirty_region;

void mdp_set_dma_pan_info(struct fb_info *info, struct mdp_dirty_region *dirty,
			  bool sync);
void mdp_dma_pan_update(struct fb_info *info);

#endif /* __MDP_DMA_H__ */

/* Copyright (c) 2008-2010, The Linux Foundation. All rights reserved.
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

#ifndef MSM_FB_DEF_H
#define MSM_FB_DEF_H

#if defined(CONFIG_FB_MSM_DEFAULT_DEPTH_RGB565)
#define MSMFB_DEFAULT_TYPE MDP_RGB_565
#elif defined(CONFIG_FB_MSM_DEFAULT_DEPTH_ARGB8888)
#define MSMFB_DEFAULT_TYPE MDP_ARGB_8888
#elif defined(CONFIG_FB_MSM_DEFAULT_DEPTH_RGBA8888)
#define MSMFB_DEFAULT_TYPE MDP_RGBA_8888
#else
#define MSMFB_DEFAULT_TYPE MDP_RGB_565
#endif

#define outp32(addr, val) writel(val, addr)
#define outp16(addr, val) writew(val, addr)
#define outp8(addr, val) writeb(val, addr)
#define outp(addr, val) outp32(addr, val)


/*--------------------------------------------------------------------------*/

#define inp32(addr) readl(addr)
#define inp16(addr) readw(addr)
#define inp8(addr) readb(addr)
#define inp(addr) inp32(addr)

#define inpw(port)             readw(IOMEM(port))
#define outpw(port, val)       writew(val, IOMEM(port))
#define inpdw(port)            readl(IOMEM(port))
#define outpdw(port, val)      writel(val, IOMEM(port))

extern u32 msm_fb_msg_level;

/*
 * Message printing priorities:
 * LEVEL 0 KERN_EMERG (highest priority)
 * LEVEL 1 KERN_ALERT
 * LEVEL 2 KERN_CRIT
 * LEVEL 3 KERN_ERR
 * LEVEL 4 KERN_WARNING
 * LEVEL 5 KERN_NOTICE
 * LEVEL 6 KERN_INFO
 * LEVEL 7 KERN_DEBUG (Lowest priority)
 */
#define MSM_FB_EMERG(msg, ...)    \
	if (msm_fb_msg_level > 0)  \
		printk(KERN_EMERG msg, ## __VA_ARGS__);
#define MSM_FB_ALERT(msg, ...)    \
	if (msm_fb_msg_level > 1)  \
		printk(KERN_ALERT msg, ## __VA_ARGS__);
#define MSM_FB_CRIT(msg, ...)    \
	if (msm_fb_msg_level > 2)  \
		printk(KERN_CRIT msg, ## __VA_ARGS__);
#define MSM_FB_ERR(msg, ...)    \
	if (msm_fb_msg_level > 3)  \
		printk(KERN_ERR msg, ## __VA_ARGS__);
#define MSM_FB_WARNING(msg, ...)    \
	if (msm_fb_msg_level > 4)  \
		printk(KERN_WARNING msg, ## __VA_ARGS__);
#define MSM_FB_NOTICE(msg, ...)    \
	if (msm_fb_msg_level > 5)  \
		printk(KERN_NOTICE msg, ## __VA_ARGS__);
#define MSM_FB_INFO(msg, ...)    \
	if (msm_fb_msg_level > 6)  \
		printk(KERN_INFO msg, ## __VA_ARGS__);
#define MSM_FB_DEBUG(msg, ...)    \
	if (msm_fb_msg_level > 7)  \
		printk(KERN_DEBUG msg, ## __VA_ARGS__);

extern unsigned char *msm_mdp_base;
extern unsigned char *msm_emdh_base;

#endif /* MSM_FB_DEF_H */

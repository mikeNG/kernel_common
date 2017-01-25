/* Copyright (c) 2008-2013, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 Rudolf Tammekivi
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

#ifndef MDP_H
#define MDP_H

#include <linux/msm_mdp.h>
#include <linux/msm_ion.h>
#include <linux/platform_device.h>

#include "msm_fb_panel.h"

#include "mdp_dma.h"
#include "mdp_hist.h"
#include "mdp_lut.h"
#include "mdp_vsync.h"

enum msm_mdp_hw_revision {
	MDP_REV_20 = 1,
	MDP_REV_22,
	MDP_REV_30,
	MDP_REV_303,
	MDP_REV_31,
	MDP_REV_40,
	MDP_REV_41,
	MDP_REV_42,
	MDP_REV_43,
	MDP_REV_44,
};
extern enum msm_mdp_hw_revision mdp_rev;

enum msm_mdp_sub_revision {
	MDP4_REVISION_V1,
	MDP4_REVISION_V2,
	MDP4_REVISION_V2_1,
	MDP4_REVISION_NONE = 0xffffffff
};
extern enum msm_mdp_sub_revision mdp_hw_revision;

uint32_t mdp_block2base(uint32_t block);

extern spinlock_t mdp_spin_lock;

extern uint32_t mdp_intr_mask;

#define MDPOP_NOP               0
#define MDPOP_LR                BIT(0)	/* left to right flip */
#define MDPOP_UD                BIT(1)	/* up and down flip */
#define MDPOP_ROT90             BIT(2)	/* rotate image to 90 degree */
#define MDPOP_ROT180            (MDPOP_UD|MDPOP_LR)
#define MDPOP_ROT270            (MDPOP_ROT90|MDPOP_UD|MDPOP_LR)
#define MDPOP_ASCALE            BIT(7)
#define MDPOP_ALPHAB            BIT(8)	/* enable alpha blending */
#define MDPOP_TRANSP            BIT(9)	/* enable transparency */
#define MDPOP_DITHER            BIT(10)	/* enable dither */
#define MDPOP_SHARPENING	BIT(11) /* enable sharpening */
#define MDPOP_BLUR		BIT(12) /* enable blur */
#define MDPOP_FG_PM_ALPHA	BIT(13)

struct mdp_buf_type {
	struct ion_handle *ihdl;
	u32 write_addr;
	u32 read_addr;
};

struct mdp_table_entry {
	uint32_t reg;
	uint32_t val;
};

extern struct mdp_ccs mdp_ccs_yuv2rgb ;
extern struct mdp_ccs mdp_ccs_rgb2yuv ;
extern unsigned char hdmi_prim_display;

struct vsync {
	ktime_t vsync_time;
	struct device *dev;
	struct work_struct vsync_work;
	int vsync_irq_enabled;
	int disabled_clocks;
	struct completion vsync_wait;
	atomic_t suspend;
	atomic_t vsync_resume;
	int sysfs_created;
};

/*
 * MDP Image Structure
 */
typedef struct mdpImg_ {
	uint32_t imgType;		/* Image type */
	uint32_t *bmy_addr;	/* bitmap or y addr */
	uint32_t *cbcr_addr;	/* cbcr addr */
	uint32_t width;		/* image width */
	uint32_t mdpOp;		/* image opertion (rotation,flip up/down, alpha/tp) */
	uint32_t tpVal;		/* transparency color */
	uint32_t alpha;		/* alpha percentage 0%(0x0) ~ 100%(0x100) */
	int    sp_value;        /* sharpening strength */
} MDPIMG;

#define MDP_OUTP(addr, data) outpdw((addr), data)

#define MDP_BASE msm_mdp_base

typedef enum {
	MDP_BC_SCALE_POINT2_POINT4,
	MDP_BC_SCALE_POINT4_POINT6,
	MDP_BC_SCALE_POINT6_POINT8,
	MDP_BC_SCALE_POINT8_1,
	MDP_BC_SCALE_UP,
	MDP_PR_SCALE_POINT2_POINT4,
	MDP_PR_SCALE_POINT4_POINT6,
	MDP_PR_SCALE_POINT6_POINT8,
	MDP_PR_SCALE_POINT8_1,
	MDP_PR_SCALE_UP,
	MDP_SCALE_BLUR,
	MDP_INIT_SCALE
} MDP_SCALE_MODE;

typedef enum {
	MDP_BLOCK_POWER_OFF,
	MDP_BLOCK_POWER_ON
} MDP_BLOCK_POWER_STATE;

typedef enum {
	MDP_CMD_BLOCK,
	MDP_OVERLAY0_BLOCK,
	MDP_MASTER_BLOCK,
	MDP_PPP_BLOCK,
	MDP_DMA2_BLOCK,
	MDP_DMA3_BLOCK,
	MDP_DMA_S_BLOCK,
	MDP_DMA_E_BLOCK,
	MDP_OVERLAY1_BLOCK,
	MDP_OVERLAY2_BLOCK,
	MDP_MAX_BLOCK
} MDP_BLOCK_TYPE;

/* Let's keep Q Factor power of 2 for optimization */
#define MDP_SCALE_Q_FACTOR 512
#define MDP_MAX_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*4)
#define MDP_MIN_X_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/4)
#define MDP_MAX_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR*4)
#define MDP_MIN_Y_SCALE_FACTOR (MDP_SCALE_Q_FACTOR/4)

/* SHIM Q Factor */
#define PHI_Q_FACTOR          29
#define PQF_PLUS_5            (PHI_Q_FACTOR + 5)	/* due to 32 phases */
#define PQF_PLUS_4            (PHI_Q_FACTOR + 4)
#define PQF_PLUS_2            (PHI_Q_FACTOR + 2)	/* to get 4.0 */
#define PQF_MINUS_2           (PHI_Q_FACTOR - 2)	/* to get 0.25 */
#define PQF_PLUS_5_PLUS_2     (PQF_PLUS_5 + 2)
#define PQF_PLUS_5_MINUS_2    (PQF_PLUS_5 - 2)

#define MDP_CONVTP(tpVal) (((tpVal&0xF800)<<8)|((tpVal&0x7E0)<<5)|((tpVal&0x1F)<<3))

#define MDPOP_ROTATION (MDPOP_ROT90|MDPOP_LR|MDPOP_UD)
#define MDP_CHKBIT(val, bit) ((bit) == ((val) & (bit)))

/* overlay interface API defines */
typedef enum {
	MORE_IBUF,
	FINAL_IBUF,
	COMPLETE_IBUF
} MDP_IBUF_STATE;

struct mdp_dirty_region {
	__u32 xoffset;		/* source origin in the x-axis */
	__u32 yoffset;		/* source origin in the y-axis */
	__u32 width;		/* number of pixels in the x-axis */
	__u32 height;		/* number of pixels in the y-axis */
};

/*
 * MDP extended data types
 */
typedef struct mdp_roi_s {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	int32_t lcd_x;
	int32_t lcd_y;
	uint32_t dst_width;
	uint32_t dst_height;
} MDP_ROI;

typedef struct mdp_ibuf_s {
	uint8_t *buf;
	uint32_t bpp;
	uint32_t ibuf_type;
	uint32_t ibuf_width;
	uint32_t ibuf_height;

	MDP_ROI roi;
	MDPIMG mdpImg;

	int32_t dma_x;
	int32_t dma_y;
	uint32_t dma_w;
	uint32_t dma_h;

	uint32_t vsync_enable;
} MDPIBUF;

struct mdp_dma_data {
	struct mutex ov_mutex;
	struct semaphore mutex;
	struct completion comp;
	struct completion dmap_comp;
};

#define MDP_CMD_DEBUG_ACCESS_BASE   (MDP_BASE+0x10000)

#define MDP_DMA2_TERM 0x1
#define MDP_DMA3_TERM 0x2
#define MDP_PPP_TERM 0x4
#define MDP_DMA_S_TERM 0x8
#define MDP_DMA_E_TERM 0x10
#define MDP_OVERLAY0_TERM 0x20
#define MDP_OVERLAY1_TERM 0x40
#define MDP_DMAP_TERM MDP_DMA2_TERM	/* dmap == dma2 */
#define MDP_PRIM_VSYNC_TERM 0x100
#define MDP_EXTER_VSYNC_TERM 0x200
#define MDP_PRIM_RDPTR_TERM 0x400
#define MDP_OVERLAY2_TERM 0x80
#define MDP_HISTOGRAM_TERM_DMA_P 0x10000
#define MDP_HISTOGRAM_TERM_DMA_S 0x20000
#define MDP_HISTOGRAM_TERM_VG_1 0x40000
#define MDP_HISTOGRAM_TERM_VG_2 0x80000
#define MDP_VSYNC_TERM 0x1000

#define ACTIVE_START_X_EN BIT(31)
#define ACTIVE_START_Y_EN BIT(31)
#define ACTIVE_HIGH 0
#define ACTIVE_LOW 1
#define MDP_DMA_S_DONE  BIT(2)
#define MDP_DMA_E_DONE  BIT(3)
#define LCDC_FRAME_START    BIT(15)
#define LCDC_UNDERFLOW      BIT(16)

#define MDP_DMA_P_DONE 	BIT(14)

#define MDP_PPP_DONE 				BIT(0)
#define TV_OUT_DMA3_DONE    BIT(6)
#define TV_ENC_UNDERRUN     BIT(7)
#define MDP_PRIM_RDPTR      BIT(8)
#define TV_OUT_DMA3_START   BIT(13)
#define MDP_HIST_DONE       BIT(20)

/*MDP4 MDP histogram interrupts*/
/*note: these are only applicable on MDP4+ targets*/
#define INTR_VG1_HISTOGRAM		BIT(5)
#define INTR_VG2_HISTOGRAM		BIT(6)
#define INTR_DMA_P_HISTOGRAM		BIT(17)
#define INTR_DMA_S_HISTOGRAM		BIT(26)
/*end MDP4 MDP histogram interrupts*/

/* histogram interrupts */
#define INTR_HIST_DONE			BIT(1)
#define INTR_HIST_RESET_SEQ_DONE	BIT(0)

#define MDP_ANY_INTR_MASK (MDP_PPP_DONE| \
			MDP_DMA_P_DONE| \
			MDP_DMA_S_DONE| \
			MDP_DMA_E_DONE| \
			LCDC_UNDERFLOW| \
			TV_ENC_UNDERRUN)

#define MDP_TOP_LUMA       16
#define MDP_TOP_CHROMA     0
#define MDP_BOTTOM_LUMA    19
#define MDP_BOTTOM_CHROMA  3
#define MDP_LEFT_LUMA      22
#define MDP_LEFT_CHROMA    6
#define MDP_RIGHT_LUMA     25
#define MDP_RIGHT_CHROMA   9

#define CLR_G 0x0
#define CLR_B 0x1
#define CLR_R 0x2
#define CLR_ALPHA 0x3

#define CLR_Y  CLR_G
#define CLR_CB CLR_B
#define CLR_CR CLR_R

/* from lsb to msb */
#define MDP_GET_PACK_PATTERN(a,x,y,z,bit) (((a)<<(bit*3))|((x)<<(bit*2))|((y)<<bit)|(z))

/*
 * 0x0000 0x0004 0x0008 MDP sync config
 */
#define MDP_SYNCFG_HGT_LOC 21
#define MDP_SYNCFG_VSYNC_EXT_EN BIT(20)
#define MDP_SYNCFG_VSYNC_INT_EN BIT(19)
#define MDP_HW_VSYNC

/*
 * 0x0018 MDP VSYNC THREASH
 */
#define MDP_PRIM_BELOW_LOC 0
#define MDP_PRIM_ABOVE_LOC 8

/*
 * MDP_PRIMARY_VSYNC_OUT_CTRL
 * 0x0080,84,88 internal vsync pulse config
 */
#define VSYNC_PULSE_EN BIT(31)
#define VSYNC_PULSE_INV BIT(30)

/*
 * 0x008c MDP VSYNC CONTROL
 */
#define DISP0_VSYNC_MAP_VSYNC0 0
#define DISP0_VSYNC_MAP_VSYNC1 BIT(0)
#define DISP0_VSYNC_MAP_VSYNC2 BIT(0)|BIT(1)

#define DISP1_VSYNC_MAP_VSYNC0 0
#define DISP1_VSYNC_MAP_VSYNC1 BIT(2)
#define DISP1_VSYNC_MAP_VSYNC2 BIT(2)|BIT(3)

#define PRIMARY_LCD_SYNC_EN BIT(4)
#define PRIMARY_LCD_SYNC_DISABLE 0

#define SECONDARY_LCD_SYNC_EN BIT(5)
#define SECONDARY_LCD_SYNC_DISABLE 0

#define EXTERNAL_LCD_SYNC_EN BIT(6)
#define EXTERNAL_LCD_SYNC_DISABLE 0

/*
 * 0x101f0 MDP VSYNC Threshold
 */
#define VSYNC_THRESHOLD_ABOVE_LOC 0
#define VSYNC_THRESHOLD_BELOW_LOC 16
#define VSYNC_ANTI_TEAR_EN BIT(31)

/*
 * 0x10004 command config
 */
#define MDP_CMD_DBGBUS_EN BIT(0)

/*
 * 0x10124 or 0x101d4PPP source config
 */
#define PPP_SRC_C0G_8BITS (BIT(1)|BIT(0))
#define PPP_SRC_C1B_8BITS (BIT(3)|BIT(2))
#define PPP_SRC_C2R_8BITS (BIT(5)|BIT(4))
#define PPP_SRC_C3A_8BITS (BIT(7)|BIT(6))

#define PPP_SRC_C0G_6BITS BIT(1)
#define PPP_SRC_C1B_6BITS BIT(3)
#define PPP_SRC_C2R_6BITS BIT(5)

#define PPP_SRC_C0G_5BITS BIT(0)
#define PPP_SRC_C1B_5BITS BIT(2)
#define PPP_SRC_C2R_5BITS BIT(4)

#define PPP_SRC_C3_ALPHA_EN BIT(8)

#define PPP_SRC_BPP_INTERLVD_1BYTES 0
#define PPP_SRC_BPP_INTERLVD_2BYTES BIT(9)
#define PPP_SRC_BPP_INTERLVD_3BYTES BIT(10)
#define PPP_SRC_BPP_INTERLVD_4BYTES (BIT(10)|BIT(9))

#define PPP_SRC_BPP_ROI_ODD_X BIT(11)
#define PPP_SRC_BPP_ROI_ODD_Y BIT(12)
#define PPP_SRC_INTERLVD_2COMPONENTS BIT(13)
#define PPP_SRC_INTERLVD_3COMPONENTS BIT(14)
#define PPP_SRC_INTERLVD_4COMPONENTS (BIT(14)|BIT(13))

/*
 * RGB666 unpack format
 * TIGHT means R6+G6+B6 together
 * LOOSE means R6+2 +G6+2+ B6+2 (with MSB)
 * or 2+R6 +2+G6 +2+B6 (with LSB)
 */
#define PPP_SRC_UNPACK_TIGHT BIT(17)
#define PPP_SRC_UNPACK_LOOSE 0
#define PPP_SRC_UNPACK_ALIGN_LSB 0
#define PPP_SRC_UNPACK_ALIGN_MSB BIT(18)

#define PPP_SRC_FETCH_PLANES_INTERLVD 0
#define PPP_SRC_FETCH_PLANES_PSEUDOPLNR BIT(20)

#define PPP_SRC_WMV9_MODE BIT(21)	/* window media version 9 */

/*
 * 0x10138 PPP operation config
 */
#define PPP_OP_SCALE_X_ON BIT(0)
#define PPP_OP_SCALE_Y_ON BIT(1)

#define PPP_OP_CONVERT_RGB2YCBCR 0
#define PPP_OP_CONVERT_YCBCR2RGB BIT(2)
#define PPP_OP_CONVERT_ON BIT(3)

#define PPP_OP_CONVERT_MATRIX_PRIMARY 0
#define PPP_OP_CONVERT_MATRIX_SECONDARY BIT(4)

#define PPP_OP_LUT_C0_ON BIT(5)
#define PPP_OP_LUT_C1_ON BIT(6)
#define PPP_OP_LUT_C2_ON BIT(7)

/* rotate or blend enable */
#define PPP_OP_ROT_ON BIT(8)

#define PPP_OP_ROT_90 BIT(9)
#define PPP_OP_FLIP_LR BIT(10)
#define PPP_OP_FLIP_UD BIT(11)

#define PPP_OP_BLEND_ON BIT(12)

#define PPP_OP_BLEND_SRCPIXEL_ALPHA 0
#define PPP_OP_BLEND_DSTPIXEL_ALPHA BIT(13)
#define PPP_OP_BLEND_CONSTANT_ALPHA BIT(14)
#define PPP_OP_BLEND_SRCPIXEL_TRANSP (BIT(13)|BIT(14))

#define PPP_OP_BLEND_ALPHA_BLEND_NORMAL 0
#define PPP_OP_BLEND_ALPHA_BLEND_REVERSE BIT(15)

#define PPP_OP_DITHER_EN BIT(16)

#define PPP_OP_COLOR_SPACE_RGB 0
#define PPP_OP_COLOR_SPACE_YCBCR BIT(17)

#define PPP_OP_SRC_CHROMA_RGB 0
#define PPP_OP_SRC_CHROMA_H2V1 BIT(18)
#define PPP_OP_SRC_CHROMA_H1V2 BIT(19)
#define PPP_OP_SRC_CHROMA_420 (BIT(18)|BIT(19))
#define PPP_OP_SRC_CHROMA_COSITE 0
#define PPP_OP_SRC_CHROMA_OFFSITE BIT(20)

#define PPP_OP_DST_CHROMA_RGB 0
#define PPP_OP_DST_CHROMA_H2V1 BIT(21)
#define PPP_OP_DST_CHROMA_H1V2 BIT(22)
#define PPP_OP_DST_CHROMA_420 (BIT(21)|BIT(22))
#define PPP_OP_DST_CHROMA_COSITE 0
#define PPP_OP_DST_CHROMA_OFFSITE BIT(23)

#define PPP_BLEND_CALPHA_TRNASP BIT(24)

#define PPP_OP_BG_CHROMA_RGB 0
#define PPP_OP_BG_CHROMA_H2V1 BIT(25)
#define PPP_OP_BG_CHROMA_H1V2 BIT(26)
#define PPP_OP_BG_CHROMA_420 BIT(25)|BIT(26)
#define PPP_OP_BG_CHROMA_SITE_COSITE 0
#define PPP_OP_BG_CHROMA_SITE_OFFSITE BIT(27)
#define PPP_OP_DEINT_EN BIT(28)

#define PPP_BLEND_BG_USE_ALPHA_SEL      (1 << 0)
#define PPP_BLEND_BG_ALPHA_REVERSE      (1 << 3)
#define PPP_BLEND_BG_SRCPIXEL_ALPHA     (0 << 1)
#define PPP_BLEND_BG_DSTPIXEL_ALPHA     (1 << 1)
#define PPP_BLEND_BG_CONSTANT_ALPHA     (2 << 1)
#define PPP_BLEND_BG_CONST_ALPHA_VAL(x) ((x) << 24)

#define PPP_OP_DST_RGB 0
#define PPP_OP_DST_YCBCR BIT(30)
/*
 * 0x10150 PPP destination config
 */
#define PPP_DST_C0G_8BIT (BIT(0)|BIT(1))
#define PPP_DST_C1B_8BIT (BIT(3)|BIT(2))
#define PPP_DST_C2R_8BIT (BIT(5)|BIT(4))
#define PPP_DST_C3A_8BIT (BIT(7)|BIT(6))

#define PPP_DST_C0G_6BIT BIT(1)
#define PPP_DST_C1B_6BIT BIT(3)
#define PPP_DST_C2R_6BIT BIT(5)

#define PPP_DST_C0G_5BIT BIT(0)
#define PPP_DST_C1B_5BIT BIT(2)
#define PPP_DST_C2R_5BIT BIT(4)

#define PPP_DST_C3A_8BIT (BIT(7)|BIT(6))
#define PPP_DST_C3ALPHA_EN BIT(8)

#define PPP_DST_PACKET_CNT_INTERLVD_2ELEM BIT(9)
#define PPP_DST_PACKET_CNT_INTERLVD_3ELEM BIT(10)
#define PPP_DST_PACKET_CNT_INTERLVD_4ELEM (BIT(10)|BIT(9))
#define PPP_DST_PACKET_CNT_INTERLVD_6ELEM (BIT(11)|BIT(9))

#define PPP_DST_PACK_LOOSE 0
#define PPP_DST_PACK_TIGHT BIT(13)
#define PPP_DST_PACK_ALIGN_LSB 0
#define PPP_DST_PACK_ALIGN_MSB BIT(14)

#define PPP_DST_OUT_SEL_AXI 0
#define PPP_DST_OUT_SEL_MDDI BIT(15)

#define PPP_DST_BPP_2BYTES BIT(16)
#define PPP_DST_BPP_3BYTES BIT(17)
#define PPP_DST_BPP_4BYTES (BIT(17)|BIT(16))

#define PPP_DST_PLANE_INTERLVD 0
#define PPP_DST_PLANE_PLANAR BIT(18)
#define PPP_DST_PLANE_PSEUDOPLN BIT(19)

#define PPP_DST_TO_TV BIT(20)

#define PPP_DST_MDDI_PRIMARY 0
#define PPP_DST_MDDI_SECONDARY BIT(21)
#define PPP_DST_MDDI_EXTERNAL BIT(22)

/*
 * 0x10180 DMA config
 */
#define DMA_DSTC0G_8BITS			(BIT(1)|BIT(0))
#define DMA_DSTC1B_8BITS			(BIT(3)|BIT(2))
#define DMA_DSTC2R_8BITS			(BIT(5)|BIT(4))

#define DMA_DSTC0G_6BITS			BIT(1)
#define DMA_DSTC1B_6BITS			BIT(3)
#define DMA_DSTC2R_6BITS			BIT(5)

#define DMA_DSTC0G_5BITS			BIT(0)
#define DMA_DSTC1B_5BITS			BIT(2)
#define DMA_DSTC2R_5BITS			BIT(4)

#define DMA_PACK_TIGHT				BIT(6)
#define DMA_PACK_LOOSE				0
#define DMA_PACK_ALIGN_LSB			0
/*
 * use DMA_PACK_ALIGN_MSB if the upper 6 bits from 8 bits output
 * from LCDC block maps into 6 pins out to the panel
 */
#define DMA_PACK_ALIGN_MSB			BIT(7)
#define DMA_PACK_PATTERN_RGB \
       (MDP_GET_PACK_PATTERN(0, CLR_R, CLR_G, CLR_B, 2)<<8)
#define DMA_PACK_PATTERN_BGR \
       (MDP_GET_PACK_PATTERN(0, CLR_B, CLR_G, CLR_R, 2)<<8)
#define DMA_OUT_SEL_AHB				0
#define DMA_OUT_SEL_LCDC			BIT(20)
#define DMA_IBUF_FORMAT_RGB888			0
#define DMA_IBUF_FORMAT_xRGB8888_OR_ARGB8888	BIT(26)

#define DMA_OUT_SEL_MDDI			BIT(19)
#define DMA_AHBM_LCD_SEL_PRIMARY		0
#define DMA_AHBM_LCD_SEL_SECONDARY		0
#define DMA_IBUF_C3ALPHA_EN			0
#define DMA_BUF_FORMAT_RGB565			BIT(25)
#define DMA_DITHER_EN				BIT(24)	/* dma_p */
#define DMA_DEFLKR_EN				BIT(24)	/* dma_e */
#define DMA_MDDI_DMAOUT_LCD_SEL_PRIMARY		0
#define DMA_MDDI_DMAOUT_LCD_SEL_SECONDARY	0
#define DMA_MDDI_DMAOUT_LCD_SEL_EXTERNAL	0
#define DMA_IBUF_FORMAT_RGB565			BIT(25)
#define DMA_IBUF_NONCONTIGUOUS 0

/*
 * MDDI Register
 */
#define MDDI_VDO_PACKET_DESC_16	0x5565
#define MDDI_VDO_PACKET_DESC	0x5666	/* 18 bits */
#define MDDI_VDO_PACKET_DESC_24	0x5888

#define MDP_HIST_INTR_STATUS_OFF	(0x0014)
#define MDP_HIST_INTR_CLEAR_OFF		(0x0018)
#define MDP_HIST_INTR_ENABLE_OFF	(0x001C)

#define MDP_INTR_ENABLE		(msm_mdp_base + 0x0050)
#define MDP_INTR_STATUS		(msm_mdp_base + 0x0054)
#define MDP_INTR_CLEAR		(msm_mdp_base + 0x0058)
#define MDP_EBI2_LCD0		(msm_mdp_base + 0x0060)
#define MDP_EBI2_LCD1		(msm_mdp_base + 0x0064)
#define MDP_EBI2_PORTMAP_MODE	(msm_mdp_base + 0x0070)

#define MDP_DMA_P_HIST_INTR_STATUS 	(msm_mdp_base + 0x95014)
#define MDP_DMA_P_HIST_INTR_CLEAR 	(msm_mdp_base + 0x95018)
#define MDP_DMA_P_HIST_INTR_ENABLE 	(msm_mdp_base + 0x9501C)

#define MDP_FULL_BYPASS_WORD43  (msm_mdp_base + 0x101ac)

#define MDP_CSC_PFMVn(n)	(msm_mdp_base + 0x40400 + 4 * (n))
#define MDP_CSC_PRMVn(n)	(msm_mdp_base + 0x40440 + 4 * (n))
#define MDP_CSC_PRE_BV1n(n)	(msm_mdp_base + 0x40500 + 4 * (n))
#define MDP_CSC_PRE_BV2n(n)	(msm_mdp_base + 0x40540 + 4 * (n))
#define MDP_CSC_POST_BV1n(n)	(msm_mdp_base + 0x40580 + 4 * (n))
#define MDP_CSC_POST_BV2n(n)	(msm_mdp_base + 0x405c0 + 4 * (n))
#define MDP_CSC_PRE_LV1n(n)	(msm_mdp_base + 0x40580 + 4 * (n))

#define MDP_CURSOR_WIDTH 64
#define MDP_CURSOR_HEIGHT 64
#define MDP_CURSOR_SIZE (MDP_CURSOR_WIDTH*MDP_CURSOR_WIDTH*4)

#define MDP_DMA_P_LUT_C0_EN   BIT(0)
#define MDP_DMA_P_LUT_C1_EN   BIT(1)
#define MDP_DMA_P_LUT_C2_EN   BIT(2)
#define MDP_DMA_P_LUT_POST    BIT(4)

void mdp_hw_init(void);
void mdp_pipe_kickoff_simplified(uint32_t term);
void mdp_clk_ctrl(int on);

int mdp_hw_cursor_update(struct fb_info *info, struct fb_cursor *cursor);
int mdp_hw_cursor_sync_update(struct fb_info *info, struct fb_cursor *cursor);

void mdp_enable_irq(uint32_t term);
void mdp_disable_irq(uint32_t term);
void mdp_disable_irq_nosync(uint32_t term);
int mdp_get_uint8_ts_per_pixel(uint32_t format,
				 struct msm_fb_data_type *mfd);
int mdp_set_core_clk(unsigned long rate);
int mdp_clk_round_rate(unsigned long rate);

unsigned long mdp_get_core_clk(void);

u32 mdp_get_panel_framerate(struct msm_fb_data_type *mfd);

int mdp_panel_register(struct device *dev, struct msm_fb_data_type *mfd);

#endif /* MDP_H */

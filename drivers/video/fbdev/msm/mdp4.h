/*
 * Copyright (c) 2009-2013, The Linux Foundation. All rights reserved.
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

#ifndef MDP4_H
#define MDP4_H

#include <linux/interrupt.h>

extern uint32_t mdp_intr_mask;
extern struct mdp4_statistic mdp4_stat;
extern int kcal_r, kcal_g, kcal_b;
extern u32 mdp_max_clk;

#define MDP4_OVERLAYPROC0_BASE	0x10000
#define MDP4_OVERLAYPROC1_BASE	0x18000
#define MDP4_OVERLAYPROC2_BASE	0x88000

#define MDP4_VIDEO_BASE 0x20000
#define MDP4_VIDEO_OFF 0x10000
#define MDP4_VIDEO_CSC_OFF 0x4000

#define MDP4_RGB_BASE 0x40000
#define MDP4_RGB_OFF 0x10000

/* chip select controller */
#define CS_CONTROLLER_0 0x0707ffff
#define CS_CONTROLLER_1 0x03073f3f

enum { /* display */
	PRIMARY_INTF_SEL,
	SECONDARY_INTF_SEL,
	EXTERNAL_INTF_SEL
};

enum {
	LCDC_RGB_INTF,			/* 0 */
	DTV_INTF = LCDC_RGB_INTF,	/* 0 */
	MDDI_LCDC_INTF,			/* 1 */
	MDDI_INTF,			/* 2 */
	EBI2_INTF,			/* 3 */
	TV_INTF = EBI2_INTF		/* 3 */
};

enum {
	MDDI_PRIMARY_SET,
	MDDI_SECONDARY_SET,
	MDDI_EXTERNAL_SET
};

enum {
	EBI2_LCD0,
	EBI2_LCD1
};

#define MDP4_3D_NONE		0
#define MDP4_3D_SIDE_BY_SIDE	1
#define MDP4_3D_TOP_DOWN	2

#define MDP4_PANEL_MDDI		BIT(0)
#define MDP4_PANEL_LCDC		BIT(1)
#define MDP4_PANEL_DTV		BIT(2)
#define MDP4_PANEL_ATV		BIT(3)
#define MDP4_PANEL_WRITEBACK	BIT(6)

enum {
	OVERLAY_BLT_SWITCH_TG_ON,
	OVERLAY_BLT_SWITCH_TG_OFF,
	OVERLAY_BLT_ALWAYS_ON
};

enum {
	OVERLAY_MODE_NONE,
	OVERLAY_MODE_BLT
};

enum {
	OVERLAY_REFRESH_ON_DEMAND,
	OVERLAY_REFRESH_VSYNC,
	OVERLAY_REFRESH_VSYNC_HALF,
	OVERLAY_REFRESH_VSYNC_QUARTER
};

enum {
	OVERLAY_FRAMEBUF,
	OVERLAY_DIRECTOUT
};

/* system interrupts */
/*note histogram interrupts defined in mdp.h*/
#define INTR_OVERLAY0_DONE		BIT(0)
#define INTR_OVERLAY1_DONE		BIT(1)
#define INTR_DMA_S_DONE			BIT(2)
#define INTR_DMA_E_DONE			BIT(3)
#define INTR_DMA_P_DONE			BIT(4)
#define INTR_PRIMARY_VSYNC		BIT(7)
#define INTR_PRIMARY_INTF_UDERRUN	BIT(8)
#define INTR_EXTERNAL_VSYNC		BIT(9)
#define INTR_EXTERNAL_INTF_UDERRUN	BIT(10)
#define INTR_PRIMARY_RDPTR		BIT(11) /* read pointer */
#define INTR_OVERLAY2_DONE		BIT(30)

#define MDP4_ANY_INTR_MASK		(0)

enum {
	OVERLAY_PIPE_VG1, /* video/graphic */
	OVERLAY_PIPE_VG2,
	OVERLAY_PIPE_RGB1,
	OVERLAY_PIPE_RGB2,
	OVERLAY_PIPE_RGB3,
	OVERLAY_PIPE_VG3,
	OVERLAY_PIPE_VG4,
	OVERLAY_PIPE_MAX
};

enum {
	OVERLAY_TYPE_RGB,
	OVERLAY_TYPE_VIDEO,
	OVERLAY_TYPE_BF
};

enum {
	MDP4_MIXER0,
	MDP4_MIXER1,
	MDP4_MIXER2,
	MDP4_MIXER_MAX
};

enum {
	OVERLAY_PLANE_INTERLEAVED,
	OVERLAY_PLANE_PLANAR,
	OVERLAY_PLANE_PSEUDO_PLANAR
};

enum {
	MDP4_MIXER_STAGE_UNUNSED,	/* pipe not used */
	MDP4_MIXER_STAGE_BASE,
	MDP4_MIXER_STAGE0,		/* zorder 0 */
	MDP4_MIXER_STAGE1,		/* zorder 1 */
	MDP4_MIXER_STAGE2,		/* zorder 2 */
	MDP4_MIXER_STAGE3,		/* zorder 3 */
	MDP4_MIXER_STAGE_MAX
};

enum {
	MDP4_FRAME_FORMAT_LINEAR,
	MDP4_FRAME_FORMAT_ARGB_TILE,
	MDP4_FRAME_FORMAT_VIDEO_SUPERTILE
};

enum {
	MDP4_CHROMA_RGB,
	MDP4_CHROMA_H2V1,
	MDP4_CHROMA_H1V2,
	MDP4_CHROMA_420
};

#define CSC_MAX_BLOCKS 6

#define MDP4_BLEND_BG_TRANSP_EN		BIT(9)
#define MDP4_BLEND_FG_TRANSP_EN		BIT(8)
#define MDP4_BLEND_BG_MOD_ALPHA		BIT(7)
#define MDP4_BLEND_BG_INV_ALPHA		BIT(6)
#define MDP4_BLEND_BG_ALPHA_FG_CONST	(0 << 4)
#define MDP4_BLEND_BG_ALPHA_BG_CONST	(1 << 4)
#define MDP4_BLEND_BG_ALPHA_FG_PIXEL	(2 << 4)
#define MDP4_BLEND_BG_ALPHA_BG_PIXEL	(3 << 4)
#define MDP4_BLEND_FG_MOD_ALPHA		BIT(3)
#define MDP4_BLEND_FG_INV_ALPHA		BIT(2)
#define MDP4_BLEND_FG_ALPHA_FG_CONST	(0 << 0)
#define MDP4_BLEND_FG_ALPHA_BG_CONST	(1 << 0)
#define MDP4_BLEND_FG_ALPHA_FG_PIXEL	(2 << 0)
#define MDP4_BLEND_FG_ALPHA_BG_PIXEL	(3 << 0)

#define MDP4_FORMAT_SOLID_FILL		BIT(22)
#define MDP4_FORMAT_UNPACK_ALIGN_MSB	BIT(18)
#define MDP4_FORMAT_UNPACK_TIGHT	BIT(17)
#define MDP4_FORMAT_90_ROTATED		BIT(12)
#define MDP4_FORMAT_ALPHA_ENABLE	BIT(8)

#define MDP4_OP_DEINT_ODD_REF		BIT(19)
#define MDP4_OP_DEINT_EN		BIT(18)
#define MDP4_OP_IGC_LUT_EN		BIT(16)
#define MDP4_OP_DITHER_EN		BIT(15)
#define MDP4_OP_FLIP_UD			BIT(14)
#define MDP4_OP_FLIP_LR			BIT(13)
#define MDP4_OP_CSC_EN			BIT(11)
#define MDP4_OP_DST_DATA_YCBCR		BIT(10)
#define MDP4_OP_SRC_DATA_YCBCR		BIT(9)
#define MDP4_OP_SCALEY_FIR		(0 << 4)
#define MDP4_OP_SCALEY_MN_PHASE		(1 << 4)
#define MDP4_OP_SCALEY_PIXEL_RPT	(2 << 4)
#define MDP4_OP_SCALEX_FIR		(0 << 2)
#define MDP4_OP_SCALEX_MN_PHASE		(1 << 2)
#define MDP4_OP_SCALEX_PIXEL_RPT	(2 << 2)
#define MDP4_OP_SCALE_RGB_ENHANCED	(1 << 4)
#define MDP4_OP_SCALE_RGB_PIXEL_RPT	(0 << 3)
#define MDP4_OP_SCALE_RGB_BILINEAR	(1 << 3)
#define MDP4_OP_SCALE_ALPHA_PIXEL_RPT	(0 << 2)
#define MDP4_OP_SCALE_ALPHA_BILINEAR	(1 << 2)
#define MDP4_OP_SCALEY_EN		BIT(1)
#define MDP4_OP_SCALEX_EN		BIT(0)

#define MDP4_REV40_UP_SCALING_MAX	(8)
#define MDP4_REV41_OR_LATER_UP_SCALING_MAX	(20)

#define MDP4_PIPE_PER_MIXER		2

#define MDP4_MAX_PLANE			4
#define VSYNC_PERIOD			16

#ifdef BLT_RGB565
#define BLT_BPP 2
#else
#define BLT_BPP 3
#endif

struct mdp4_hsic_regs {
	int32_t params[NUM_HSIC_PARAM];
	int32_t conv_matrix[3][3];
	int32_t	pre_limit[6];
	int32_t post_limit[6];
	int32_t pre_bias[3];
	int32_t post_bias[3];
	int32_t dirty;
};

struct mdp4_iommu_pipe_info {
	struct ion_handle *ihdl[MDP4_MAX_PLANE];
	struct ion_handle *prev_ihdl[MDP4_MAX_PLANE];
	u8 mark_unmap;
};

#define IOMMU_FREE_LIST_MAX 32

struct iommu_free_list {
	int fndx;
	struct ion_handle *ihdl[IOMMU_FREE_LIST_MAX];
};

struct blend_cfg {
	u32 op;
	u32 bg_alpha;
	u32 fg_alpha;
	u32 co3_sel;
	u32 transp_low0;
	u32 transp_low1;
	u32 transp_high0;
	u32 transp_high1;
	int solidfill;
	struct mdp4_overlay_pipe *solidfill_pipe;
};

struct mdp4_overlay_pipe {
	uint32_t pipe_used;
	uint32_t pipe_type;		/* rgb, video/graphic */
	uint32_t pipe_num;
	uint32_t pipe_ndx;
	uint32_t pipe_share;
	uint32_t mixer_num;		/* which mixer used */
	uint32_t mixer_stage;		/* which stage of mixer used */
	uint32_t src_format;
	uint32_t src_width;	/* source img width */
	uint32_t src_height;	/* source img height */
	uint32_t is_3d;
	uint32_t src_width_3d;	/* source img width */
	uint32_t src_height_3d;	/* source img height */
	uint32_t src_w;		/* roi */
	uint32_t src_h;		/* roi */
	uint32_t src_x;		/* roi */
	uint32_t src_y;		/* roi */
	uint32_t dst_w;		/* roi */
	uint32_t dst_h;		/* roi */
	uint32_t dst_x;		/* roi */
	uint32_t dst_y;		/* roi */
	uint32_t flags;
	uint32_t op_mode;
	uint32_t transp;
	uint32_t blend_op;
	uint32_t phasex_step;
	uint32_t phasey_step;
	uint32_t alpha;
	uint32_t is_fg;		/* control alpha & color key */
	uint32_t srcp0_addr;	/* interleave, luma */
	uint32_t srcp0_ystride;
	uint32_t srcp1_addr;	/* pseudoplanar, chroma plane */
	uint32_t srcp1_ystride;
	uint32_t srcp2_addr;	/* planar color 2*/
	uint32_t srcp2_ystride;
	uint32_t srcp3_addr;	/* alpha/color 3 */
	uint32_t srcp3_ystride;
	uint32_t fetch_plane;
	uint32_t frame_format;		/* video */
	uint32_t chroma_site;		/* video */
	uint32_t chroma_sample;		/* video */
	uint32_t solid_fill;
	uint32_t vc1_reduce;		/* video */
	uint32_t unpack_align_msb;/* 0 to LSB, 1 to MSB */
	uint32_t unpack_tight;/* 0 for loose, 1 for tight */
	uint32_t unpack_count;/* 0 = 1 component, 1 = 2 component ... */
	uint32_t rotated_90; /* has been rotated 90 degree */
	uint32_t bpp;	/* uint8_t per pixel */
	uint32_t alpha_enable;/*  source has alpha */
	/*
	 * number of bits for source component,
	 * 0 = 1 bit, 1 = 2 bits, 2 = 6 bits, 3 = 8 bits
	 */
	uint32_t a_bit;	/* component 3, alpha */
	uint32_t r_bit;	/* component 2, R_Cr */
	uint32_t b_bit;	/* component 1, B_Cb */
	uint32_t g_bit;	/* component 0, G_lumz */
	/*
	 * unpack pattern
	 * A = C3, R = C2, B = C1, G = C0
	 */
	uint32_t element3; /* 0 = C0, 1 = C1, 2 = C2, 3 = C3 */
	uint32_t element2; /* 0 = C0, 1 = C1, 2 = C2, 3 = C3 */
	uint32_t element1; /* 0 = C0, 1 = C1, 2 = C2, 3 = C3 */
	uint32_t element0; /* 0 = C0, 1 = C1, 2 = C2, 3 = C3 */
	ulong ov_blt_addr; /* blt mode addr */
	ulong dma_blt_addr; /* blt mode addr */
	ulong blt_base;
	ulong blt_offset;
	uint32_t blt_cnt;
	uint32_t blt_changed;
	uint32_t ov_cnt;
	uint32_t dmap_cnt;
	uint32_t dmas_cnt;
	uint32_t dmae_cnt;
	uint32_t blt_end;
	uint32_t blt_ov_koff;
	uint32_t blt_ov_done;
	uint32_t blt_dmap_koff;
	uint32_t blt_dmap_done;
	uint32_t req_clk;
	uint64_t bw_ab_quota;
	uint64_t bw_ib_quota;
	uint32_t luma_align_size;
	struct mdp_overlay_pp_params pp_cfg;
	struct mdp_overlay req_data;
	struct completion comp;
	struct mdp4_iommu_pipe_info iommu;
};

struct mdp4_statistic {
	ulong intr_tot;
	ulong intr_dma_p;
	ulong intr_dma_s;
	ulong intr_dma_e;
	ulong intr_overlay0;
	ulong intr_overlay1;
	ulong intr_overlay2;
	ulong intr_vsync_p;	/* Primary interface */
	ulong intr_underrun_p;	/* Primary interface */
	ulong intr_vsync_e;	/* external interface */
	ulong intr_underrun_e;	/* external interface */
	ulong intr_histogram;
	ulong intr_rdptr;
	ulong kickoff_ov0;
	ulong kickoff_ov1;
	ulong kickoff_ov2;
	ulong kickoff_dmap;
	ulong kickoff_dmae;
	ulong kickoff_dmas;
	ulong blt_lcdc;	/* blt */
	ulong blt_dtv;	/* blt */
	ulong blt_mddi;	/* blt */
	ulong overlay_set[MDP4_MIXER_MAX];
	ulong overlay_unset[MDP4_MIXER_MAX];
	ulong overlay_play[MDP4_MIXER_MAX];
	ulong overlay_commit[MDP4_MIXER_MAX];
	ulong pipe[OVERLAY_PIPE_MAX];
	ulong wait4vsync0;
	ulong wait4vsync1;
	ulong iommu_map;
	ulong iommu_unmap;
	ulong iommu_drop;
	ulong err_mixer;
	ulong err_zorder;
	ulong err_size;
	ulong err_scale;
	ulong err_format;
	ulong err_stage;
	ulong err_play;
	ulong err_underflow;
};

struct vsync_update {
	int update_cnt;	/* pipes to be updated */
	struct completion vsync_comp;
	struct mdp4_overlay_pipe plist[OVERLAY_PIPE_MAX];
};

struct mdp4_overlay_pipe *mdp4_overlay_ndx2pipe(int ndx);
void mdp4_sw_reset(unsigned long bits);
void mdp4_display_intf_sel(int output, unsigned long intf);
void mdp4_mddi_setup(int which, unsigned long id);
void mdp4_enable_clk_irq(void);
void mdp4_disable_clk_irq(void);
void mdp4_dma_p_update(struct msm_fb_data_type *mfd);
void mdp4_dma_s_update(struct msm_fb_data_type *mfd);
void mdp4_intr_clear_set(ulong clear, ulong set);
void mdp4_dma_p_cfg(void);
void mdp4_hw_init(void);
void mdp4_isr_read(int);
void mdp4_mixer_blend_init(int mixer_num);
void mdp4_vg_qseed_init(int vg_num);
void mdp4_vg_csc_update(struct mdp_csc *p);
irqreturn_t mdp4_isr(int irq, void *ptr);
void mdp4_overlay_format_to_pipe(uint32_t format, struct mdp4_overlay_pipe *pipe);
uint32_t mdp4_overlay_format(struct mdp4_overlay_pipe *pipe);
uint32_t mdp4_overlay_unpack_pattern(struct mdp4_overlay_pipe *pipe);
uint32_t mdp4_overlay_op_mode(struct mdp4_overlay_pipe *pipe);

#ifdef CONFIG_FB_MSM_DTV
void mdp4_overlay_dtv_start(void);
void mdp4_overlay_dtv_ov_done_push(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe);
void mdp4_overlay_dtv_wait_for_ov(struct msm_fb_data_type *mfd,
	struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_dtv_set(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_dtv_unset(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe);
void mdp4_dmae_done_dtv(void);
void mdp4_dtv_wait4vsync(int cndx);
void mdp4_dtv_vsync_ctrl(struct fb_info *info, int enable);
void mdp4_dtv_base_swap(int cndx, struct mdp4_overlay_pipe *pipe);
void mdp4_dtv_pipe_queue(int cndx, struct mdp4_overlay_pipe *pipe);
int mdp4_dtv_pipe_commit(int cndx, int wait);
#else
static inline void mdp4_overlay_dtv_start(void)
{
	/* empty */
}
static inline void  mdp4_overlay_dtv_ov_done_push(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline void  mdp4_overlay_dtv_wait_for_ov(struct msm_fb_data_type *mfd,
	struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline int mdp4_overlay_dtv_set(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe)
{
	return 0;
}
static inline int mdp4_overlay_dtv_unset(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe)
{
	return 0;
}

static inline void mdp4_dmae_done_dtv(void)
{
    /* empty */
}
static inline void mdp4_dtv_wait4vsync(int cndx)
{
    /* empty */
}
static inline void mdp4_dtv_vsync_ctrl(struct fb_info *info, int enable)
{
    /* empty */
}
static inline void mdp4_dtv_overlay_blt_start(struct msm_fb_data_type *mfd)
{
	return;
}
static inline void mdp4_dtv_overlay_blt_stop(struct msm_fb_data_type *mfd)
{
	return;
}
static inline void mdp4_dtv_base_swap(int cndx, struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline void mdp4_dtv_pipe_queue(int cndx, struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline int mdp4_dtv_pipe_commit(int cndx, int wait)
{
	return 0;
}
#endif /* CONFIG_FB_MSM_DTV */

void mdp4_dtv_set_black_screen(void);

int mdp4_overlay_dtv_set(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_dtv_unset(struct msm_fb_data_type *mfd,
			struct mdp4_overlay_pipe *pipe);
void mdp4_dtv_overlay(struct msm_fb_data_type *mfd);
int mdp4_dtv_on(struct platform_device *pdev);
int mdp4_dtv_off(struct platform_device *pdev);
void mdp4_atv_overlay(struct msm_fb_data_type *mfd);
int mdp4_atv_on(struct platform_device *pdev);
int mdp4_atv_off(struct platform_device *pdev);
void mdp4_overlay_free_base_pipe(struct msm_fb_data_type *mfd);
void mdp4_primary_rdptr(void);
int mdp4_overlay_commit(struct fb_info *info);
void mdp4_dtv_vsync_init(int cndx);
ssize_t mdp4_dtv_show_event(struct device *dev,
	struct device_attribute *attr, char *buf);
void mdp4_overlay_rgb_setup(struct mdp4_overlay_pipe *pipe);
void mdp4_overlay_reg_flush(struct mdp4_overlay_pipe *pipe, int all);
void mdp4_mixer_blend_setup(int mixer);
struct mdp4_overlay_pipe *mdp4_overlay_stage_pipe(int mixer, int stage);
void mdp4_mixer_stage_up(struct mdp4_overlay_pipe *pipe, int commit);
void mdp4_mixer_stage_down(struct mdp4_overlay_pipe *pipe, int commit);
void mdp4_mixer_pipe_cleanup(int mixer);
int mdp4_mixer_stage_can_run(struct mdp4_overlay_pipe *pipe);
void mdp4_overlayproc_cfg(struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_format2type(uint32_t format);
int mdp4_overlay_format2pipe(struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_get(struct fb_info *info, struct mdp_overlay *req);
int mdp4_overlay_set(struct fb_info *info, struct mdp_overlay *req);
int mdp4_overlay_wait4vsync(struct fb_info *info);
int mdp4_overlay_vsync_ctrl(struct fb_info *info, int enable);
int mdp4_overlay_unset(struct fb_info *info, int ndx);
int mdp4_overlay_unset_mixer(int mixer);
int mdp4_overlay_play_wait(struct fb_info *info,
	struct msmfb_overlay_data *req);
int mdp4_overlay_play(struct fb_info *info, struct msmfb_overlay_data *req);
struct mdp4_overlay_pipe *mdp4_overlay_pipe_alloc(int ptype, int mixer);
void mdp4_overlay_vsync_commit(struct mdp4_overlay_pipe *pipe);
void mdp4_mixer_stage_commit(int mixer);
void mdp4_overlay_pipe_free(struct mdp4_overlay_pipe *pipe, int all);
void mdp4_overlay_dmap_cfg(struct msm_fb_data_type *mfd, int lcdc);
void mdp4_overlay_dmap_xy(struct mdp4_overlay_pipe *pipe);
void mdp4_overlay_dmae_cfg(struct msm_fb_data_type *mfd, int atv);
void mdp4_overlay_dmae_xy(struct mdp4_overlay_pipe *pipe);
void mdp4_overlay_dmas_cfg(struct msm_fb_data_type *mfd);
void mdp4_overlay_dmas_xy(struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_pipe_staged(struct mdp4_overlay_pipe *pipe);
void mdp4_overlay1_done_dtv(void);
void mdp4_overlay1_done_atv(void);
void mdp4_external_vsync_dtv(void);
void mdp4_overlay_solidfill_init(struct mdp4_overlay_pipe *pipe);
void mdp4_rgb_igc_lut_setup(int num);
void mdp4_vg_igc_lut_setup(int num);
void mdp4_mixer_gc_lut_setup(int mixer_num);
uint32_t mdp4_rgb_igc_lut_cvt(uint32_t ndx);
void mdp4_vg_qseed_init(int);
int mdp4_overlay_blt(struct fb_info *info, struct msmfb_overlay_blt *req);

static inline void mdp4_dtv_free_base_pipe(struct msm_fb_data_type *mfd)
{
	/* empty */
}

void mdp4_dtv_overlay_blt_start(struct msm_fb_data_type *mfd);
void mdp4_dtv_overlay_blt_stop(struct msm_fb_data_type *mfd);
void mdp4_overlay_panel_mode(int mixer_num, uint32_t mode);
void mdp4_overlay_panel_mode_unset(int mixer_num, uint32_t mode);
uint32_t mdp4_overlay_panel_list(void);

int mdp4_overlay_3d_sbys(struct fb_info *info, struct msmfb_overlay_3d *req);

int mdp4_mixer_info(int mixer_num, struct mdp_mixer_info *info);

void mdp_dmap_vsync_set(int enable);
int mdp_dmap_vsync_get(void);
void mdp_hw_cursor_done(void);
void mdp_hw_cursor_init(void);
void mdp4_overlay_resource_release(void);
uint32_t mdp4_ss_table_value(int8_t param, int8_t index);
void mdp4_overlay_borderfill_stage_down(struct mdp4_overlay_pipe *pipe);
int mdp4_overlay_borderfill_supported(void);

void mdp4_hsic_update(struct mdp4_overlay_pipe *pipe);
int mdp4_csc_config(struct mdp_csc_cfg_data *config);
void mdp4_csc_write(struct mdp_csc_cfg *data, uint32_t base);
int mdp4_csc_enable(struct mdp_csc_cfg_data *config);
int mdp4_allocate_writeback_buf(struct msm_fb_data_type *mfd, u32 mix_num);
void mdp4_init_writeback_buf(struct msm_fb_data_type *mfd, u32 mix_num);
void mdp4_free_writeback_buf(struct msm_fb_data_type *mfd, u32 mix_num);
void mdp4_overlay_iommu_pipe_free(int ndx, int all);
void mdp4_overlay_iommu_free_list(int mixer, struct ion_handle *ihdl);
void mdp4_overlay_iommu_unmap_freelist(int mixer);
void mdp4_vg_csc_restore(void);

#ifdef CONFIG_FB_MSM_MDDI
void mdp4_mddi_pipe_queue(int cndx, struct mdp4_overlay_pipe *pipe);
int mdp4_mddi_pipe_commit(int cndx, int wait);
void mdp4_mddi_vsync_ctrl(struct fb_info *info, int enable);
void mdp4_mddi_wait4vsync(int cndx);
void mdp4_dmap_done_mddi(int cndx);
void mdp4_dmas_done_mddi(int cndx);
void mdp4_overlay0_done_mddi(int cndx);
ssize_t mdp4_mddi_show_event(struct device *dev,
		struct device_attribute *attr, char *buf);
void mdp4_mddi_rdptr_init(int cndx);
void mdp4_mddi_free_base_pipe(struct msm_fb_data_type *mfd);
void mdp4_mddi_base_swap(int cndx, struct mdp4_overlay_pipe *pipe);
void mdp4_mddi_blt_start(struct msm_fb_data_type *mfd);
void mdp4_mddi_blt_stop(struct msm_fb_data_type *mfd);
void mdp4_mddi_overlay_blt(struct msm_fb_data_type *mfd,
					struct msmfb_overlay_blt *req);
int mdp4_mddi_on(struct msm_fb_data_type *mfd);
int mdp4_mddi_off(struct msm_fb_data_type *mfd);
void mdp4_mddi_overlay(struct msm_fb_data_type *mfd);
#else
static inline void mdp4_mddi_pipe_queue(int cndx,
	struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline int mdp4_mddi_pipe_commit(int cndx, int wait)
{
	return 0;
}
static inline void mdp4_mddi_vsync_ctrl(struct fb_info *info, int enable)
{
	/* empty */
}
static inline void mdp4_mddi_wait4vsync(int cndx)
{
	/* empty */
}
static inline void mdp4_dmap_done_mddi(int cndx)
{
	/* empty */
}
static inline void mdp4_dmas_done_mddi(int cndx)
{
	/* empty */
}
static inline void mdp4_overlay0_done_mddi(int cndx)
{
	/* empty */
}
static inline ssize_t mdp4_mddi_show_event(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline void mdp4_mddi_rdptr_init(int cndx)
{
	/* empty */
}
static inline void mdp4_mddi_free_base_pipe(struct msm_fb_data_type *mfd)
{
	/* empty */
}
static inline void mdp4_mddi_base_swap(int cndx, struct mdp4_overlay_pipe *pipe)
{
	/* empty */
}
static inline void mdp4_mddi_blt_start(struct msm_fb_data_type *mfd)
{
	/* empty */
}
static inline void mdp4_mddi_blt_stop(struct msm_fb_data_type *mfd)
{
	/* empty */
}
static inline void mdp4_mddi_overlay_blt(struct msm_fb_data_type *mfd,
					struct msmfb_overlay_blt *req)
{
	/* empty */
}
static inline int mdp4_mddi_on(struct platform_device *pdev)
{
	return 0;
}
static inline int mdp4_mddi_off(struct platform_device *pdev)
{
	return 0;
}
static inline void mdp4_mddi_overlay(struct msm_fb_data_type *mfd)
{
	/* empty */
}
#endif
#endif /* MDP_H */

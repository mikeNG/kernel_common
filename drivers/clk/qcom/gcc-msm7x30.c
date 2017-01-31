/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,gcc-msm7x30.h>
#include <dt-bindings/reset/qcom,gcc-msm7x30.h>

#include "clk-regmap.h"
#include "clk-pcom.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

struct gcc_msm7x30_data {
	struct device *dev;
	struct regmap *regmap;
	struct clk_onecell_data onecell;
	struct reset_controller_dev rcdev;
};

/* Registers in the base (non-shadow) region. */
#define CLK_TEST_BASE_REG	0x011C
#define CLK_TEST_2_BASE_REG	0x0384
#define MISC_CLK_CTL_BASE_REG	0x0110
#define PRPH_WEB_NS_BASE_REG	0x0080
#define PLL0_STATUS_BASE_REG	0x0318
#define PLL1_STATUS_BASE_REG	0x0334
#define PLL2_STATUS_BASE_REG	0x0350
#define PLL3_STATUS_BASE_REG	0x036C
#define PLL4_STATUS_BASE_REG	0x0254
#define PLL5_STATUS_BASE_REG	0x0258
#define PLL6_STATUS_BASE_REG	0x04EC
#define RINGOSC_CNT_BASE_REG	0x00FC
#define SH2_OWN_APPS1_BASE_REG	0x040C
#define SH2_OWN_APPS2_BASE_REG	0x0414
#define SH2_OWN_APPS3_BASE_REG	0x0444
#define SH2_OWN_GLBL_BASE_REG	0x0404
#define SH2_OWN_ROW1_BASE_REG	0x041C
#define SH2_OWN_ROW2_BASE_REG	0x0424
#define TCXO_CNT_BASE_REG	0x00F8
#define TCXO_CNT_DONE_BASE_REG	0x00F8

/* Shadow-region 2 (SH2) registers. */
#define	QUP_I2C_NS_REG		0x04F0
#define CAM_NS_REG		0x0374
#define CAM_VFE_NS_REG		0x0044
#define CLK_HALT_STATEA_REG	0x0108
#define CLK_HALT_STATEB_REG	0x010C
#define CLK_HALT_STATEC_REG	0x02D4
#define CSI_NS_REG		0x0174
#define EMDH_NS_REG		0x0050
#define GLBL_CLK_ENA_2_SC_REG	0x03C0
#define GLBL_CLK_ENA_SC_REG	0x03BC
#define GLBL_CLK_STATE_2_REG	0x037C
#define GLBL_CLK_STATE_REG	0x0004
#define GRP_2D_NS_REG		0x0034
#define GRP_NS_REG		0x0084
#define HDMI_NS_REG		0x0484
#define I2C_2_NS_REG		0x02D8
#define I2C_NS_REG		0x0068
#define JPEG_NS_REG		0x0164
#define LPA_CORE_CLK_MA0_REG	0x04F4
#define LPA_CORE_CLK_MA2_REG	0x04FC
#define LPA_NS_REG		0x02E8
#define MDC_NS_REG		0x007C
#define MDP_LCDC_NS_REG		0x0390
#define MDP_NS_REG		0x014C
#define MDP_VSYNC_REG		0x0460
#define MFC_NS_REG		0x0154
#define MI2S_CODEC_RX_DIV_REG	0x02EC
#define MI2S_CODEC_TX_DIV_REG	0x02F0
#define MI2S_DIV_REG		0x02E4
#define MI2S_NS_REG		0x02E0
#define MI2S_RX_NS_REG		0x0070
#define MI2S_TX_NS_REG		0x0078
#define MIDI_NS_REG		0x02D0
#define PLL_ENA_REG		0x0264
#define PMDH_NS_REG		0x008C
#define SDAC_NS_REG		0x009C
#define SDCn_NS_REG(n)		(0x00A4+(0x8*((n)-1)))
#define SPI_NS_REG		0x02C8
#define TSIF_NS_REG		0x00C4
#define TV_NS_REG		0x00CC
#define UART1DM_NS_REG		0x00D4
#define UART2DM_NS_REG		0x00DC
#define UART2_NS_REG		0x0464
#define UART_NS_REG		0x00E0
#define USBH2_NS_REG		0x046C
#define USBH3_NS_REG		0x0470
#define USBH_MD_REG		0x02BC
#define USBH_NS_REG		0x02C0
#define VPE_NS_REG		0x015C

static struct regmap_config gcc_msm7x30_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x04FC,
	.fast_io	= true,
};

/* MUX source input identifiers. */
enum {
	P_PLL0, /* Modem PLL */
	P_PLL1, /* Global PLL */
	P_PLL3, /* Multimedia/Peripheral PLL or Backup PLL1 */
	P_PLL4, /* Display PLL */
	P_SDAC_LPXO, /* Low-power XO for SDAC */
	P_LPXO, /* Low-power XO */
	P_TCXO, /* Used for rates from TCXO */
	P_AXI, /* Used for rates that sync to AXI */
	P_GND,
};

static const struct parent_map gcc_lpxo_map[] = {
	{ P_LPXO, 6 },
};

static const char * const gcc_lpxo[] = {
	"lpxo",
};

static const struct parent_map gcc_pll1_map[] = {
	{ P_PLL1, 1 },
};

static const char * const gcc_pll1[] = {
	"pll1_vote",
};

static const struct parent_map gcc_pll3_map[] = {
	{ P_PLL3, 6 },
};

static const char * const gcc_pll3[] = {
	"pll3_vote",
};

static const struct parent_map gcc_lpxo_pll3_map[] = {
	{ P_LPXO, 6 },
	{ P_PLL3, 3 },
};

static const char * const gcc_lpxo_pll3[] = {
	"lpxo",
	"pll3_vote",
};

static const struct parent_map gcc_tcxo_map[] = {
	{ P_TCXO, 0 },
};

static const char * const gcc_tcxo[] = {
	"tcxo",
};

static const struct parent_map gcc_pll4_map[] = {
	{ P_PLL4, 3 },
};

static const char * const gcc_pll4[] = {
	"pll4_vote",
};

static const struct parent_map gcc_pll1_pll3_pll4_map[] = {
	{ P_PLL1, 1 },
	{ P_PLL3, 3 },
	{ P_PLL4, 2 },
};

static const char * const gcc_pll1_pll3_pll4[] = {
	"pll1_vote",
	"pll3_vote",
	"pll4_vote",
};

static const struct parent_map gcc_pll1_pll3_map[] = {
	{ P_PLL1, 1 },
	{ P_PLL3, 3 },
};

static const char * const gcc_pll1_pll3[] = {
	"pll1_vote",
	"pll3_vote",
};

static const struct parent_map gcc_lpxo_pll1_pll3_map[] = {
	{ P_LPXO, 6 },
	{ P_PLL1, 1 },
	{ P_PLL3, 3 },
};

static const char * const gcc_lpxo_pll1_pll3[] = {
	"lpxo",
	"pll1_vote",
	"pll3_vote",
};


/* BASE CLKS */
static struct clk_pll pll0 = {
	.l_reg = PLL0_STATUS_BASE_REG - 20,
	.m_reg = PLL0_STATUS_BASE_REG - 16,
	.n_reg = PLL0_STATUS_BASE_REG - 12,
	.config_reg = PLL0_STATUS_BASE_REG - 4,
	.mode_reg = PLL0_STATUS_BASE_REG - 24,
	.status_reg = PLL0_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll0",
		.parent_names = (const char *[]){ "tcxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll1 = {
	.l_reg = PLL1_STATUS_BASE_REG - 20,
	.m_reg = PLL1_STATUS_BASE_REG - 16,
	.n_reg = PLL1_STATUS_BASE_REG - 12,
	.config_reg = PLL1_STATUS_BASE_REG - 4,
	.mode_reg = PLL1_STATUS_BASE_REG - 24,
	.status_reg = PLL1_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll1",
		.parent_names = (const char *[]){ "tcxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

#define NSS_PLL_RATE(f, _l, _m, _n, i) \
	{  \
		.freq = f,  \
		.l = _l, \
		.m = _m, \
		.n = _n, \
		.ibits = i, \
	}

/* Calculation:
 * Source clock - TCXO @ 19.2MHz
 * Pre-divided clocks have 19.2MHz / 2
 * SCLK * l + (SCLK * m / n)
 */
static struct pll_freq_tbl pll2_freq_tbl[] = {
	NSS_PLL_RATE( 806400000,  42, 0, 1, 0x00010581),
	NSS_PLL_RATE(1024000000,  53, 1, 3, 0x00010581),
	NSS_PLL_RATE(1200000000, 125, 0, 1, 0x00018581), /* Pre divided */
	NSS_PLL_RATE(1401600000,  73, 0, 1, 0x00010581),
};

static struct clk_pll pll2 = {
	.l_reg = PLL2_STATUS_BASE_REG - 20,
	.m_reg = PLL2_STATUS_BASE_REG - 16,
	.n_reg = PLL2_STATUS_BASE_REG - 12,
	.config_reg = PLL2_STATUS_BASE_REG - 4,
	.mode_reg = PLL2_STATUS_BASE_REG - 24,
	.status_reg = PLL2_STATUS_BASE_REG,
	.status_bit = 16,
	.freq_tbl = pll2_freq_tbl,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll2",
		.parent_names = (const char *[]){ "tcxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll3 = {
	.l_reg = PLL3_STATUS_BASE_REG - 20,
	.m_reg = PLL3_STATUS_BASE_REG - 16,
	.n_reg = PLL3_STATUS_BASE_REG - 12,
	.config_reg = PLL3_STATUS_BASE_REG - 4,
	.mode_reg = PLL3_STATUS_BASE_REG - 24,
	.status_reg = PLL3_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll3",
		.parent_names = (const char *[]){ "lpxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll4 = {
	.l_reg = PLL4_STATUS_BASE_REG - 20,
	.m_reg = PLL4_STATUS_BASE_REG - 16,
	.n_reg = PLL4_STATUS_BASE_REG - 12,
	.config_reg = PLL4_STATUS_BASE_REG - 4,
	.mode_reg = PLL4_STATUS_BASE_REG - 24,
	.status_reg = PLL4_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll4",
		.parent_names = (const char *[]){ "lpxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll5 = {
	.l_reg = PLL5_STATUS_BASE_REG - 20,
	.m_reg = PLL5_STATUS_BASE_REG - 16,
	.n_reg = PLL5_STATUS_BASE_REG - 12,
	.config_reg = PLL5_STATUS_BASE_REG - 4,
	.mode_reg = PLL5_STATUS_BASE_REG - 24,
	.status_reg = PLL5_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll5",
		.parent_names = (const char *[]){ "tcxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll6 = {
	.l_reg = PLL6_STATUS_BASE_REG - 20,
	.m_reg = PLL6_STATUS_BASE_REG - 16,
	.n_reg = PLL6_STATUS_BASE_REG - 12,
	.config_reg = PLL6_STATUS_BASE_REG - 4,
	.mode_reg = PLL6_STATUS_BASE_REG - 24,
	.status_reg = PLL6_STATUS_BASE_REG,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll6",
		.parent_names = (const char *[]){ "tcxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

/* SH2 CLKS */
static struct clk_regmap pll0_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(0),
	.hw.init = &(struct clk_init_data){
		.name = "pll0_vote",
		.parent_names = (const char *[]){ "pll0" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll1_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(1),
	.hw.init = &(struct clk_init_data){
		.name = "pll1_vote",
		.parent_names = (const char *[]){ "pll1" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll2_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(2),
	.hw.init = &(struct clk_init_data){
		.name = "pll2_vote",
		.parent_names = (const char *[]){ "pll2" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll3_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(3),
	.hw.init = &(struct clk_init_data){
		.name = "pll3_vote",
		.parent_names = (const char *[]){ "pll3" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll4_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(4),
	.hw.init = &(struct clk_init_data){
		.name = "pll4_vote",
		.parent_names = (const char *[]){ "pll4" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll5_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(5),
	.hw.init = &(struct clk_init_data){
		.name = "pll5_vote",
		.parent_names = (const char *[]){ "pll5" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_regmap pll6_vote = {
	.enable_reg = PLL_ENA_REG,
	.enable_mask = BIT(6),
	.hw.init = &(struct clk_init_data){
		.name = "pll6_vote",
		.parent_names = (const char *[]){ "pll6" },
		.num_parents = 1,
		.ops = &clk_pll_vote_ops,
	},
};

static struct clk_branch glbl_root_clk = {
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(29),
		.hw.init = &(struct clk_init_data){
			.name = "glbl_root_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch axi_imem_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "axi_imem_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_apps_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_apps_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_vg_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_vg_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_adm_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "axi_adm_clk",
			.parent_names = (const char *[]){ "axi_li_apps_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_grp_2d_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 21,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "axi_grp_2d_clk",
			.parent_names = (const char *[]){ "axi_li_vg_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_adsp_a_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_adsp_a_clk",
			.parent_names = (const char *[]){ "axi_li_apps_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_grp_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_grp_clk",
			.parent_names = (const char *[]){ "axi_li_vg_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_jpeg_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 19,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_jpeg_clk",
			.parent_names = (const char *[]){ "axi_li_apps_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_li_vfe_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
			.name = "axi_li_vfe_clk",
			.parent_names = (const char *[]){ "axi_li_apps_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_mdp_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(29),
		.hw.init = &(struct clk_init_data){
			.name = "axi_mdp_clk",
			.parent_names = (const char *[]){ "axi_li_apps_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_mfc_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 20,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(20),
		.hw.init = &(struct clk_init_data){
			.name = "axi_mfc_clk",
			.parent_names = (const char *[]){ "axi_li_vg_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_rotator_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "axi_rotator_clk",
			.parent_names = (const char *[]){ "axi_li_vg_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch axi_vpe_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 21,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "axi_vpe_clk",
			.parent_names = (const char *[]){ "axi_li_vg_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static const struct freq_tbl clk_tbl_cam[] = {
	{  6000000, P_PLL1, 4, 1, 32 },
	{  8000000, P_PLL1, 4, 1, 24 },
	{ 12000000, P_PLL1, 4, 1, 16 },
	{ 16000000, P_PLL1, 4, 1, 12 },
	{ 19200000, P_PLL1, 4, 1, 10 },
	{ 24000000, P_PLL1, 4, 1,  8 },
	{ 32000000, P_PLL1, 4, 1,  6 },
	{ 48000000, P_PLL1, 4, 1,  4 },
	{ 64000000, P_PLL1, 4, 1,  3 },
	{ }
};

static struct clk_rcg cam_m_clk = {
	.ns_reg = CAM_NS_REG,
	.md_reg = CAM_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_map,
	},
	.freq_tbl = clk_tbl_cam,
	.clkr = {
		.enable_reg = CAM_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "cam_m_clk",
			.parent_names = gcc_pll1,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static const struct freq_tbl clk_tbl_csi[] = {
	{ 153600000, P_PLL1, 2, 2, 5 },
	{ 192000000, P_PLL1, 4, 0, 0 },
	{ 384000000, P_PLL1, 2, 0, 0 },
	{ }
};

static struct clk_rcg csi0_src = {
	.ns_reg = CSI_NS_REG,
	.md_reg = CSI_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 17,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_map,
	},
	.freq_tbl = clk_tbl_csi,
	.clkr = {
		.enable_reg = CSI_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "csi0_src",
			.parent_names = gcc_pll1,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch csi0_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = CSI_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "csi0_clk",
			.parent_names = (const char *[]){ "csi0_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csi0_vfe_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = CSI_NS_REG,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "csi0_vfe_clk",
			.parent_names = (const char *[]){ "vfe_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mdh[] = {
	{  49150000, P_PLL3, 15 },
	{  92160000, P_PLL3,  8 },
	{ 122880000, P_PLL3,  6 },
	{ 184320000, P_PLL3,  4 },
	{ 245760000, P_PLL3,  3 },
	{ 368640000, P_PLL3,  2 },
	{ 384000000, P_PLL1,  2 },
	{ 445500000, P_PLL4,  2 },
	{ }
};

static struct clk_rcg emdh_clk = {
	.ns_reg = EMDH_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_pll3_pll4_map,
	},
	.freq_tbl = clk_tbl_mdh,
	.clkr = {
		.enable_reg = EMDH_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "emdh_clk",
			.parent_names = gcc_pll1_pll3_pll4,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static const struct freq_tbl clk_tbl_grp[] = {
	{  24576000, P_LPXO,  1 },
	{  46080000, P_PLL3, 16 },
	{  49152000, P_PLL3, 15 },
	{  52662875, P_PLL3, 14 },
	{  56713846, P_PLL3, 13 },
	{  61440000, P_PLL3, 12 },
	{  67025454, P_PLL3, 11 },
	{  73728000, P_PLL3, 10 },
	{  81920000, P_PLL3,  9 },
	{  92160000, P_PLL3,  8 },
	{ 105325714, P_PLL3,  7 },
	{ 122880000, P_PLL3,  6 },
	{ 147456000, P_PLL3,  5 },
	{ 184320000, P_PLL3,  4 },
	{ 192000000, P_PLL1,  4 },
	{ 245760000, P_PLL3,  3 },
	{ 256000000, P_PLL1,  3 },
	{ }
};

static struct clk_rcg grp_2d_src = {
	.ns_reg = GRP_2D_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_grp,
	.clkr = {
		.enable_reg = GRP_2D_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "grp_2d_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch grp_2d_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 31,
	.clkr = {
		.enable_reg = GRP_2D_NS_REG,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "grp_2d_clk",
			.parent_names = (const char *[]){ "grp_2d_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg grp_3d_src = {
	.ns_reg = GRP_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_grp,
	.clkr = {
		.enable_reg = GRP_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "grp_3d_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch grp_3d_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = GRP_NS_REG,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "grp_3d_clk",
			.parent_names = (const char *[]){ "grp_3d_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch hdmi_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = HDMI_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "hdmi_clk",
			.parent_names = (const char *[]){ "tv_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_tcxo[] = {
	{ 19200000, P_TCXO, 0, 0, 0 },
	{ }
};

static struct clk_rcg i2c_src = {
	.ns_reg = I2C_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_tcxo_map,
	},
	.freq_tbl = clk_tbl_tcxo,
	.clkr = {
		.enable_reg = I2C_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "i2c_src",
			.parent_names = gcc_tcxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch i2c_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = I2C_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "i2c_clk",
			.parent_names = (const char *[]){ "i2c_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg i2c_2_src = {
	.ns_reg = I2C_2_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_tcxo_map,
	},
	.freq_tbl = clk_tbl_tcxo,
	.clkr = {
		.enable_reg = I2C_2_NS_REG,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "i2c_2_src",
			.parent_names = gcc_tcxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch i2c_2_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = I2C_2_NS_REG,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "i2c_2_clk",
			.parent_names = (const char *[]){ "i2c_2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch imem_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 19,
	.clkr = {
		.enable_reg = GRP_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "imem_clk",
			.parent_names = (const char *[]){ "grp_3d_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_vfe_jpeg[] = {
	{  24576000, P_LPXO, 1, 0, 0 },
	{  36864000, P_PLL3, 4, 1, 5 },
	{  46080000, P_PLL3, 4, 1, 4 },
	{  61440000, P_PLL3, 4, 1, 3 },
	{  73728000, P_PLL3, 2, 1, 5 },
	{  81920000, P_PLL3, 3, 1, 3 },
	{  92160000, P_PLL3, 4, 1, 2 },
	{  98304000, P_PLL3, 3, 2, 5 },
	{ 105326000, P_PLL3, 2, 2, 7 },
	{ 122880000, P_PLL3, 2, 1, 3 },
	{ 147456000, P_PLL3, 2, 2, 5 },
	{ 153600000, P_PLL1, 2, 2, 5 },
	{ 192000000, P_PLL1, 4, 0, 0 },
	{ }
};

static struct clk_rcg jpeg_src = {
	.ns_reg = JPEG_NS_REG,
	.md_reg = JPEG_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_vfe_jpeg,
	.clkr = {
		.enable_reg = JPEG_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "jpeg_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch jpeg_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = JPEG_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "jpeg_clk",
			.parent_names = (const char *[]){ "jpeg_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

enum {
	MI2S_CODEC_RX,
	ECODEC_CIF,
	MI2S,
	SDAC
};

static struct freq_tbl clk_tbl_lpa_codec[] = {
	{ 1, MI2S_CODEC_RX },
	{ 2, ECODEC_CIF },
	{ 3, MI2S },
	{ 4, SDAC },
	{ }
};

static const struct parent_map gcc_mi2srx_ecodec_mi2s_sdac_map[] = {
	{ MI2S_CODEC_RX, 0 },
	{ ECODEC_CIF, 1 },
	{ MI2S, 2 },
	{ SDAC, 3 },
};

static struct clk_rcg lpa_codec_src = {
	.ns_reg = LPA_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_mi2srx_ecodec_mi2s_sdac_map,
	},
	.freq_tbl = clk_tbl_lpa_codec,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "lpa_codec_src",
			.ops = &clk_rcg_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch lpa_codec_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = LPA_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "lpa_codec_clk",
			.parent_names = (const char *[]){ "lpa_codec_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch lpa_core_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = LPA_NS_REG,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "lpa_core_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_rcg mdc_src = {
	.ns_reg = MDC_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
	},
	.clkr = {
		.enable_reg = MDC_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mdc_src",
			.ops = &clk_rcg_ops,
			.flags = CLK_IS_ROOT,
		},
	}
};

static struct clk_branch mdc_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = MDC_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mdc_clk",
			.parent_names = (const char *[]){ "mdc_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mdp[] = {
	{  24576000, P_LPXO,  1 },
	{  46080000, P_PLL3, 16 },
	{  49152000, P_PLL3, 15 },
	{  52663000, P_PLL3, 14 },
	{  92160000, P_PLL3,  8 },
	{ 122880000, P_PLL3,  6 },
	{ 147456000, P_PLL3,  5 },
	{ 153600000, P_PLL1,  5 },
	{ 192000000, P_PLL1,  4 },
	{ }
};

static struct clk_rcg mdp_src = {
	.ns_reg = MDP_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_csi,
	.clkr = {
		.enable_reg = MDP_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mdp_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = MDP_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_clk",
			.parent_names = (const char *[]){ "mdp_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mdp_lcdc[] = {
	{ 24576000, P_LPXO, 1, 0,  0 },
	{ 30720000, P_PLL3, 4, 1,  6 },
	{ 32768000, P_PLL3, 3, 2, 15 },
	{ 40960000, P_PLL3, 2, 1,  9 },
	{ 73728000, P_PLL3, 2, 1,  5 },
	{ }
};

static struct clk_rcg mdp_lcdc_pclk_src = {
	.ns_reg = MDP_LCDC_NS_REG,
	.md_reg = MDP_LCDC_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_mdp_lcdc,
	.clkr = {
		.enable_reg = MDP_LCDC_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_lcdc_pclk_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mdp_lcdc_pclk_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 28,
	.clkr = {
		.enable_reg = MDP_LCDC_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_lcdc_pclk_clk",
			.parent_names = (const char *[]){ "mdp_lcdc_pclk_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mdp_lcdc_pad_pclk_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = MDP_LCDC_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_lcdc_pad_pclk_clk",
			.parent_names = (const char *[]){ "mdp_lcdc_pclk_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mdp_vsync[] = {
	{ 24576000, P_LPXO, 0 },
	{ }
};

/* Vsync CLK uses custom src_sel_shift and src index for LPXO. */
static const struct parent_map gcc_lpxo_mdp_map[] = {
	{ P_LPXO, 1 },
};

static struct clk_rcg mdp_vsync_src = {
	.ns_reg = MDP_VSYNC_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 2,
		.parent_map = gcc_lpxo_mdp_map,
	},
	.freq_tbl = clk_tbl_mdp_vsync,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "mdp_vsync_src",
			.parent_names = gcc_lpxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mdp_vsync_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 30,
	.clkr = {
		.enable_reg = MDP_VSYNC_REG,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_vsync_clk",
			.parent_names = (const char *[]){ "mdp_vsync_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mfc[] = {
	{  24576000, P_LPXO, 1, 0, 0 },
	{  30720000, P_PLL3, 4, 1, 6 },
	{  61440000, P_PLL3, 4, 1, 3 },
	{  81920000, P_PLL3, 3, 1, 3 },
	{ 122880000, P_PLL3, 3, 1, 2 },
	{ 147456000, P_PLL3, 1, 1, 5 },
	{ 153600000, P_PLL1, 1, 1, 5 },
	{ 170667000, P_PLL1, 1, 2, 9 },
	{ }
};

static struct clk_rcg mfc_src = {
	.ns_reg = MFC_NS_REG,
	.md_reg = MFC_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 17,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_mfc,
	.clkr = {
		.enable_reg = MFC_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mfc_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mfc_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = MFC_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mfc_clk",
			.parent_names = (const char *[]){ "mfc_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mfc_div2_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = MFC_NS_REG,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "mfc_div2_clk",
			.parent_names = (const char *[]){ "mfc_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mi2s_codec[] = {
	{  2048000, P_LPXO, 4, 1, 3 },
	{ 12288000, P_LPXO, 2, 0, 0 },
	{ }
};

static struct clk_rcg mi2s_codec_rx_m_src = {
	.ns_reg = MI2S_RX_NS_REG,
	.md_reg = MI2S_RX_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_map,
	},
	.freq_tbl = clk_tbl_mi2s_codec,
	.clkr = {
		.enable_reg = MI2S_RX_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_rx_m_src",
			.parent_names = gcc_lpxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mi2s_codec_rx_m_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = MI2S_RX_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_rx_m_clk",
			.parent_names = (const char *[]){ "mi2s_codec_rx_m_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mi2s_codec_rx_s_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = MI2S_RX_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_rx_s_clk",
			.parent_names = (const char *[]){ "mi2s_codec_rx_m_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg mi2s_codec_tx_m_src = {
	.ns_reg = MI2S_TX_NS_REG,
	.md_reg = MI2S_TX_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_map,
	},
	.freq_tbl = clk_tbl_mi2s_codec,
	.clkr = {
		.enable_reg = MI2S_TX_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_tx_m_src",
			.parent_names = gcc_lpxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mi2s_codec_tx_m_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = MI2S_TX_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_tx_m_clk",
			.parent_names = (const char *[]){ "mi2s_codec_tx_m_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mi2s_codec_tx_s_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = MI2S_TX_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_codec_tx_s_clk",
			.parent_names = (const char *[]){ "mi2s_codec_tx_m_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_mi2s[] = {
	{ 12288000, P_LPXO, 2, 0, 0 },
	{ }
};

static struct clk_rcg mi2s_m_src = {
	.ns_reg = MI2S_NS_REG,
	.md_reg = MI2S_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_map,
	},
	.freq_tbl = clk_tbl_mi2s,
	.clkr = {
		.enable_reg = MI2S_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_m_src",
			.parent_names = gcc_lpxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch mi2s_m_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = MI2S_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_m_clk",
			.parent_names = (const char *[]){ "mi2s_m_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mi2s_s_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = MI2S_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "mi2s_s_clk",
			.parent_names = (const char *[]){ "mi2s_m_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_midi[] = {
	{ 98304000, P_PLL3, 3, 2, 5 },
	{ }
};

static struct clk_rcg midi_src = {
	.ns_reg = MIDI_NS_REG,
	.md_reg = MIDI_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 12,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll3_map,
	},
	.freq_tbl = clk_tbl_midi,
	.clkr = {
		.enable_reg = MIDI_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "midi_src",
			.parent_names = gcc_pll3,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch midi_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = MIDI_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "midi_clk",
			.parent_names = (const char *[]){ "midi_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg pmdh_clk = {
	.ns_reg = PMDH_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_pll3_pll4_map,
	},
	.freq_tbl = clk_tbl_mdh,
	.clkr = {
		.enable_reg = PMDH_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "pmdh_clk",
			.parent_names = gcc_pll1_pll3_pll4,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_rcg qup_i2c_src = {
	.ns_reg = QUP_I2C_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_tcxo_map,
	},
	.freq_tbl = clk_tbl_tcxo,
	.clkr = {
		.enable_reg = QUP_I2C_NS_REG,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "qup_i2c_src",
			.parent_names = gcc_tcxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch qup_i2c_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 31,
	.clkr = {
		.enable_reg = QUP_I2C_NS_REG,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "qup_i2c_clk",
			.parent_names = (const char *[]){ "qup_i2c_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_sdac[] = {
	{  256000, P_SDAC_LPXO, 4,   1,    24 },
	{  352800, P_SDAC_LPXO, 1, 147, 10240 },
	{  384000, P_SDAC_LPXO, 4,   1,    16 },
	{  512000, P_SDAC_LPXO, 4,   1,    12 },
	{  705600, P_SDAC_LPXO, 1, 147,  5120 },
	{  768000, P_SDAC_LPXO, 4,   1,     8 },
	{ 1024000, P_SDAC_LPXO, 4,   1,     6 },
	{ 1411200, P_SDAC_LPXO, 1, 147,  2560 },
	{ 1536000, P_SDAC_LPXO, 4,   1,     5 },
	{ }
};

static const struct parent_map gcc_sdaclpxo_map[] = {
	{ P_SDAC_LPXO, 5 },
};

static struct clk_rcg sdac_src = {
	.ns_reg = SDAC_NS_REG,
	.md_reg = SDAC_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_sdaclpxo_map,
	},
	.freq_tbl = clk_tbl_mi2s_codec,
	.clkr = {
		.enable_reg = SDAC_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdac_src",
			.parent_names = gcc_lpxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch sdac_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = SDAC_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdac_clk",
			.parent_names = (const char *[]){ "sdac_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch sdac_m_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = SDAC_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "sdac_m_clk",
			.parent_names = (const char *[]){ "sdac_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_sdc[] = {
	{   144000, P_LPXO, 1, 1, 171 },
	{   400000, P_LPXO, 1, 2, 123 },
	{ 16027000, P_PLL3, 3, 14, 215 },
	{ 17000000, P_PLL3, 4, 19, 206 },
	{ 20480000, P_PLL3, 4, 23, 212 },
	{ 24576000, P_LPXO, 1, 0, 0 },
	{ 49152000, P_PLL3, 3, 1, 5 },
	{ }
};

static struct clk_rcg sdc1_src = {
	.ns_reg = SDCn_NS_REG(1),
	.md_reg = SDCn_NS_REG(1) - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 12,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = SDCn_NS_REG(1),
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch sdc1_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = SDCn_NS_REG(1),
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_clk",
			.parent_names = (const char *[]){ "sdc1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc2_src = {
	.ns_reg = SDCn_NS_REG(2),
	.md_reg = SDCn_NS_REG(2) - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 13,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = SDCn_NS_REG(2),
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch sdc2_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = SDCn_NS_REG(2),
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_clk",
			.parent_names = (const char *[]){ "sdc2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc3_src = {
	.ns_reg = SDCn_NS_REG(3),
	.md_reg = SDCn_NS_REG(3) - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 12,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = SDCn_NS_REG(3),
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch sdc3_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = SDCn_NS_REG(3),
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_clk",
			.parent_names = (const char *[]){ "sdc3_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg sdc4_src = {
	.ns_reg = SDCn_NS_REG(4),
	.md_reg = SDCn_NS_REG(4) - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 13,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_sdc,
	.clkr = {
		.enable_reg = SDCn_NS_REG(4),
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch sdc4_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = SDCn_NS_REG(4),
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_clk",
			.parent_names = (const char *[]){ "sdc4_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_spi[] = {
	{  9963243, P_PLL3, 4, 2, 37 },
	{ 24576000, P_LPXO, 1, 0,  0 },
	{ 26331429, P_PLL3, 4, 1,  7 },
	{ }
};

static struct clk_rcg spi_src = {
	.ns_reg = SPI_NS_REG,
	.md_reg = SPI_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 12,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll3_map,
	},
	.freq_tbl = clk_tbl_spi,
	.clkr = {
		.enable_reg = SPI_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "spi_src",
			.parent_names = gcc_lpxo_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch spi_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = SPI_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "spi_clk",
			.parent_names = (const char *[]){ "spi_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch tsif_ref_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = TSIF_NS_REG,
		.enable_mask = BIT(9)|BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "tsif_ref_clk",
			.parent_names = (const char *[]){ "tv_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_tv[] = {
	{  27000000, P_PLL4, 2, 2, 33 },
	{  74250000, P_PLL4, 2, 1,  6 },
	{ }
};

static struct clk_rcg tv_src = {
	.ns_reg = TV_NS_REG,
	.md_reg = TV_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll4_map,
	},
	.freq_tbl = clk_tbl_tv,
	.clkr = {
		.enable_reg = TV_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "tv_src",
			.parent_names = gcc_pll4,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch tv_dac_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = TV_NS_REG,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "tv_dac_clk",
			.parent_names = (const char *[]){ "tv_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch tv_enc_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = TV_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "tv_enc_clk",
			.parent_names = (const char *[]){ "tv_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_uartdm[] = {
	{  3686400, P_PLL3, 3,   3, 200 },
	{  7372800, P_PLL3, 3,   3, 100 },
	{ 14745600, P_PLL3, 3,   3,  50 },
	{ 32000000, P_PLL3, 3,  25, 192 },
	{ 40000000, P_PLL3, 3, 125, 768 },
	{ 46400000, P_PLL3, 3, 145, 768 },
	{ 48000000, P_PLL3, 3,  25, 128 },
	{ 51200000, P_PLL3, 3,   5,  24 },
	{ 56000000, P_PLL3, 3, 175, 768 },
	{ 58982400, P_PLL3, 3,   6,  25 },
	{ 64000000, P_PLL1, 4,   1,   3 },
	{ }
};

static struct clk_rcg uart1dm_src = {
	.ns_reg = UART1DM_NS_REG,
	.md_reg = UART1DM_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_uartdm,
	.clkr = {
		.enable_reg = UART1DM_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "uart1dm_src",
			.parent_names = gcc_pll1_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch uart1dm_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = UART1DM_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "uart1dm_clk",
			.parent_names = (const char *[]){ "uart1dm_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg uart1_src = {
	.ns_reg = UART_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_tcxo_map,
	},
	.freq_tbl = clk_tbl_tcxo,
	.clkr = {
		.enable_reg = UART_NS_REG,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "uart1_src",
			.parent_names = gcc_tcxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch uart1_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = UART_NS_REG,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "uart1_clk",
			.parent_names = (const char *[]){ "uart1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg uart2dm_src = {
	.ns_reg = UART2DM_NS_REG,
	.md_reg = UART2DM_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_uartdm,
	.clkr = {
		.enable_reg = UART2DM_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "uart2dm_src",
			.parent_names = gcc_pll1_pll3,
			.num_parents = 2,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch uart2dm_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = UART2DM_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "uart2dm_clk",
			.parent_names = (const char *[]){ "uart2dm_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg uart2_src = {
	.ns_reg = UART2_NS_REG,
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_tcxo_map,
	},
	.freq_tbl = clk_tbl_tcxo,
	.clkr = {
		.enable_reg = UART2_NS_REG,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "uart2_src",
			.parent_names = gcc_tcxo,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch uart2_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = UART2_NS_REG,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "uart2_clk",
			.parent_names = (const char *[]){ "uart2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs2_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = USBH2_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs2_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs2_core_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_bit = 28,
	.clkr = {
		.enable_reg = USBH2_NS_REG,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs2_core_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs3_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = USBH3_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs3_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs3_core_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = USBH3_NS_REG,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs3_core_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = USBH_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch usb_hs_core_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = USBH_NS_REG,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs_core_clk",
			.parent_names = (const char *[]){ "usb_hs_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_usb[] = {
	{ 60000000, P_PLL1, 2, 5, 32 },
	{ }
};

static struct clk_rcg usb_hs_src = {
	.ns_reg = USBH_NS_REG,
	.md_reg = USBH_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_pll1_map,
	},
	.freq_tbl = clk_tbl_usb,
	.clkr = {
		.enable_reg = USBH_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs_src",
			.parent_names = gcc_pll1,
			.num_parents = 1,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_rcg vfe_src = {
	.ns_reg = CAM_VFE_NS_REG,
	.md_reg = CAM_VFE_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 16,
		.m_val_shift = 16,
		.width = 16,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_vfe_jpeg,
	.clkr = {
		.enable_reg = CAM_VFE_NS_REG,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch vfe_clk = {
	.halt_reg = CLK_HALT_STATEB_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = CAM_VFE_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_clk",
			.parent_names = (const char *[]){ "vfe_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch vfe_camif_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = CAM_VFE_NS_REG,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_camif_clk",
			.parent_names = (const char *[]){ "vfe_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch vfe_mdc_clk = {
	.halt_reg = CLK_HALT_STATEA_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = CAM_VFE_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_mdc_clk",
			.parent_names = (const char *[]){ "vfe_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const struct freq_tbl clk_tbl_vpe[] = {
	{  24576000, P_LPXO, 1, 0, 0 },
	{  30720000, P_PLL3, 4, 1, 6 },
	{  61440000, P_PLL3, 4, 1, 3 },
	{  81920000, P_PLL3, 3, 1, 3 },
	{ 122880000, P_PLL3, 3, 1, 2 },
	{ 147456000, P_PLL3, 1, 1, 5 },
	{ 153600000, P_PLL1, 1, 1, 5 },
	{ }
};

static struct clk_rcg vpe_src = {
	.ns_reg = VPE_NS_REG,
	.md_reg = VPE_NS_REG - 4,
	.mn = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 5,
		.n_val_shift = 15,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 3,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = gcc_lpxo_pll1_pll3_map,
	},
	.freq_tbl = clk_tbl_vpe,
	.clkr = {
		.enable_reg = VPE_NS_REG,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_src",
			.parent_names = gcc_lpxo_pll1_pll3,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	}
};

static struct clk_branch vpe_clk = {
	.halt_reg = CLK_HALT_STATEC_REG,
	.halt_check = BRANCH_HALT,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = VPE_NS_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_clk",
			.parent_names = (const char *[]){ "vpe_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch adm_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "adm_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch camif_pad_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "camif_pad_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch ce_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "ce_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch csi0_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 30,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(30),
		.hw.init = &(struct clk_init_data){
			.name = "csi0_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch emdh_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "emdh_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch grp_2d_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "grp_2d_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch grp_3d_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "grp_3d_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch jpeg_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "jpeg_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};


static struct clk_branch lpa_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "lpa_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch mdp_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch mfc_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "mfc_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch pmdh_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "pmdh_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch rotator_imem_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
			.name = "rotator_imem_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch rotator_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "rotator_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc1_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "sdc1_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc2_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "sdc2_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc3_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "sdc3_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch sdc4_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 28,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(28),
		.hw.init = &(struct clk_init_data){
			.name = "sdc4_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch spi_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "spi_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch tsif_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "tsif_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch uart1dm_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "uart1dm_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch uart2dm_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "uart2dm_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch usb_hs_p_clk = {
	.halt_reg = GLBL_CLK_STATE_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_SC_REG,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch usb_hs2_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs2_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch usb_hs3_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "usb_hs3_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

static struct clk_branch vfe_p_clk = {
	.halt_reg = GLBL_CLK_STATE_2_REG,
	.halt_check = BRANCH_HALT_VOTED,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = GLBL_CLK_ENA_2_SC_REG,
		.enable_mask = BIT(27),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_p_clk",
			.parent_names = (const char *[]){ "glbl_root_clk" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},
};

enum ownership_reg {
	GLBL,
	APPS1,
	APPS2,
	ROW1,
	ROW2,
	APPS3,
	NUM_OWNERSHIP
};

struct msm7x30_clk {
	enum ownership_reg owner_reg;
	unsigned int owner_bit;

	unsigned index;
	bool always_local;
	struct clk_regmap *local;
	struct clk_pcom *remote;
};

#define MSM7X30_CLK(reg, bit, bank_index, local_clk, remote_clk)	\
{									\
	.owner_reg = reg,						\
	.owner_bit = bit,						\
	.index = bank_index,						\
	.local = local_clk,						\
	.remote = remote_clk,						\
}

#define MSM7X30_CLK_LOCAL(bank_index, local_clk)			\
{									\
	.index = bank_index,						\
	.always_local = true,						\
	.local = local_clk,						\
}

#define MSM7X30_CLK_REMOTE(remote_clk)					\
{									\
	.remote = remote_clk,						\
}

static DEFINE_CLK_XO_PCOM(tcxo, 0, 19200000);
static DEFINE_CLK_XO_PCOM(lpxo, 1, 24576000);
static DEFINE_CLK_PCOM(adm_clk, PCOM_ADM_CLK, 0);
static DEFINE_CLK_PCOM(adsp_clk, PCOM_ADSP_CLK, 0);
static DEFINE_CLK_PCOM(axi_rotator_clk, PCOM_AXI_ROTATOR_CLK, 0);
static DEFINE_CLK_PCOM(camif_pad_p_clk, PCOM_CAMIF_PAD_P_CLK, 0);
static DEFINE_CLK_PCOM(cam_m_clk, PCOM_CAM_M_CLK, 0);
static DEFINE_CLK_PCOM(ce_clk, PCOM_CE_CLK, 0);
static DEFINE_CLK_PCOM(codec_ssbi_clk, PCOM_CODEC_SSBI_CLK, 0);
static DEFINE_CLK_PCOM(csi0_clk, PCOM_CSI0_CLK, 0);
static DEFINE_CLK_PCOM(csi0_p_clk, PCOM_CSI0_P_CLK, 0);
static DEFINE_CLK_PCOM(csi0_vfe_clk, PCOM_CSI0_VFE_CLK, 0);
static DEFINE_CLK_PCOM(ebi1_clk, PCOM_EBI1_CLK, CLK_MIN | ALWAYS_ON);
static DEFINE_CLK_PCOM(ebi1_fixed_clk, PCOM_EBI1_FIXED_CLK, CLK_MIN | ALWAYS_ON);
static DEFINE_CLK_PCOM(ecodec_clk, PCOM_ECODEC_CLK, 0);
static DEFINE_CLK_PCOM(emdh_clk, PCOM_EMDH_CLK, CLK_MIN);
static DEFINE_CLK_PCOM(emdh_p_clk, PCOM_EMDH_P_CLK, 0);
static DEFINE_CLK_PCOM(gp_clk, PCOM_GP_CLK, 0);
static DEFINE_CLK_PCOM(grp_2d_clk, PCOM_GRP_2D_CLK, 0);
static DEFINE_CLK_PCOM(grp_2d_p_clk, PCOM_GRP_2D_P_CLK, 0);
static DEFINE_CLK_PCOM(grp_3d_clk, PCOM_GRP_3D_CLK, 0);
static DEFINE_CLK_PCOM(grp_3d_p_clk, PCOM_GRP_3D_P_CLK, 0);
static DEFINE_CLK_PCOM(hdmi_clk, PCOM_HDMI_CLK, 0);
static DEFINE_CLK_PCOM(i2c_2_clk, PCOM_I2C_2_CLK, 0);
static DEFINE_CLK_PCOM(i2c_clk, PCOM_I2C_CLK, 0);
static DEFINE_CLK_PCOM(imem_clk, PCOM_IMEM_CLK, 0);
static DEFINE_CLK_PCOM(jpeg_clk, PCOM_JPEG_CLK, CLK_MIN);
static DEFINE_CLK_PCOM(jpeg_p_clk, PCOM_JPEG_P_CLK, 0);
static DEFINE_CLK_PCOM(lpa_codec_clk, PCOM_LPA_CODEC_CLK, 0);
static DEFINE_CLK_PCOM(lpa_core_clk, PCOM_LPA_CORE_CLK, 0);
static DEFINE_CLK_PCOM(lpa_p_clk, PCOM_LPA_P_CLK, 0);
static DEFINE_CLK_PCOM(mdc_clk, PCOM_MDC_CLK, 0);
static DEFINE_CLK_PCOM(mdp_clk, PCOM_MDP_CLK, CLK_MIN);
static DEFINE_CLK_PCOM(mdp_lcdc_pad_pclk_clk, PCOM_MDP_LCDC_PAD_PCLK_CLK, 0);
static DEFINE_CLK_PCOM(mdp_lcdc_pclk_clk, PCOM_MDP_LCDC_PCLK_CLK, 0);
static DEFINE_CLK_PCOM(mdp_p_clk, PCOM_MDP_P_CLK, 0);
static DEFINE_CLK_PCOM(mdp_vsync_clk, PCOM_MDP_VSYNC_CLK, 0);
static DEFINE_CLK_PCOM(mfc_clk, PCOM_MFC_CLK, 0);
static DEFINE_CLK_PCOM(mfc_div2_clk, PCOM_MFC_DIV2_CLK, 0);
static DEFINE_CLK_PCOM(mfc_p_clk, PCOM_MFC_P_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_codec_rx_m_clk, PCOM_MI2S_CODEC_RX_M_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_codec_rx_s_clk, PCOM_MI2S_CODEC_RX_S_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_codec_tx_m_clk, PCOM_MI2S_CODEC_TX_M_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_codec_tx_s_clk, PCOM_MI2S_CODEC_TX_S_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_m_clk, PCOM_MI2S_M_CLK, 0);
static DEFINE_CLK_PCOM(mi2s_s_clk, PCOM_MI2S_S_CLK, 0);
static DEFINE_CLK_PCOM(pmdh_clk, PCOM_PMDH_CLK, CLK_MIN);
static DEFINE_CLK_PCOM(pmdh_p_clk, PCOM_PMDH_P_CLK, 0);
static DEFINE_CLK_PCOM(qup_i2c_clk, PCOM_QUP_I2C_CLK, 0);
static DEFINE_CLK_PCOM(rotator_imem_clk, PCOM_ROTATOR_IMEM_CLK, 0);
static DEFINE_CLK_PCOM(rotator_p_clk, PCOM_ROTATOR_P_CLK, 0);
static DEFINE_CLK_PCOM(sdac_clk, PCOM_SDAC_CLK, 0);
static DEFINE_CLK_PCOM(sdac_m_clk, PCOM_SDAC_M_CLK, 0);
static DEFINE_CLK_PCOM(sdc1_clk, PCOM_SDC1_CLK, 0);
static DEFINE_CLK_PCOM(sdc1_p_clk, PCOM_SDC1_P_CLK, 0);
static DEFINE_CLK_PCOM(sdc2_clk, PCOM_SDC2_CLK, 0);
static DEFINE_CLK_PCOM(sdc2_p_clk, PCOM_SDC2_P_CLK, 0);
static DEFINE_CLK_PCOM(sdc3_clk, PCOM_SDC3_CLK, 0);
static DEFINE_CLK_PCOM(sdc3_p_clk, PCOM_SDC3_P_CLK, 0);
static DEFINE_CLK_PCOM(sdc4_clk, PCOM_SDC4_CLK, 0);
static DEFINE_CLK_PCOM(sdc4_p_clk, PCOM_SDC4_P_CLK, 0);
static DEFINE_CLK_PCOM(spi_clk, PCOM_SPI_CLK, 0);
static DEFINE_CLK_PCOM(spi_p_clk, PCOM_SPI_P_CLK, 0);
static DEFINE_CLK_PCOM(tsif_p_clk, PCOM_TSIF_P_CLK, 0);
static DEFINE_CLK_PCOM(tsif_ref_clk, PCOM_TSIF_CLK, 0);
static DEFINE_CLK_PCOM(tv_dac_clk, PCOM_TV_DAC_CLK, 0);
static DEFINE_CLK_PCOM(tv_enc_clk, PCOM_TV_ENC_CLK, 0);
static DEFINE_CLK_PCOM(uart1dm_clk, PCOM_UART1DM_CLK, 0);
static DEFINE_CLK_PCOM(uart1_clk, PCOM_UART1_CLK, 0);
static DEFINE_CLK_PCOM(uart2dm_clk, PCOM_UART2DM_CLK, 0);
static DEFINE_CLK_PCOM(uart2_clk, PCOM_UART2_CLK, 0);
static DEFINE_CLK_PCOM(uart3_clk, PCOM_UART3_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs2_clk, PCOM_USB_HS2_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs2_core_clk, PCOM_USB_HS2_CORE_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs2_p_clk, PCOM_USB_HS2_P_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs3_clk, PCOM_USB_HS3_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs3_core_clk, PCOM_USB_HS3_CORE_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs3_p_clk, PCOM_USB_HS3_P_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs_clk, PCOM_USB_HS_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs_core_clk, PCOM_USB_HS_CORE_CLK, 0);
static DEFINE_CLK_PCOM(usb_hs_p_clk, PCOM_USB_HS_P_CLK, 0);
static DEFINE_CLK_PCOM(usb_phy_clk, PCOM_USB_PHY_CLK, CLK_MIN);
static DEFINE_CLK_PCOM(vfe_camif_clk, PCOM_VFE_CAMIF_CLK, 0);
static DEFINE_CLK_PCOM(vfe_clk, PCOM_VFE_CLK, 0);
static DEFINE_CLK_PCOM(vfe_mdc_clk, PCOM_VFE_MDC_CLK, 0);
static DEFINE_CLK_PCOM(vfe_p_clk, PCOM_VFE_P_CLK, 0);
static DEFINE_CLK_PCOM(vpe_clk, PCOM_VPE_CLK, 0);

static const struct msm7x30_clk gcc_msm7x30_clks[] = {
	/* Clock sources. */
	[TCXO_CLK] = MSM7X30_CLK_REMOTE(&pcom_tcxo),
	[LPXO_CLK] = MSM7X30_CLK_REMOTE(&pcom_lpxo),
	[PLL0_CLK] = MSM7X30_CLK_LOCAL(0, &pll0.clkr),
	[PLL0_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll0_vote),
	[PLL1_CLK] = MSM7X30_CLK_LOCAL(0, &pll1.clkr),
	[PLL1_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll1_vote),
	[PLL2_CLK] = MSM7X30_CLK_LOCAL(0, &pll2.clkr),
	[PLL2_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll2_vote),
	[PLL3_CLK] = MSM7X30_CLK_LOCAL(0, &pll3.clkr),
	[PLL3_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll3_vote),
	[PLL4_CLK] = MSM7X30_CLK_LOCAL(0, &pll4.clkr),
	[PLL4_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll4_vote),
	[PLL5_CLK] = MSM7X30_CLK_LOCAL(0, &pll5.clkr),
	[PLL5_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll5_vote),
	[PLL6_CLK] = MSM7X30_CLK_LOCAL(0, &pll6.clkr),
	[PLL6_VOTE_CLK] = MSM7X30_CLK_LOCAL(1, &pll6_vote),

	/* AXI bridge clocks. */
	[GLBL_ROOT_CLK] = MSM7X30_CLK_LOCAL(1, &glbl_root_clk.clkr),
	[AXI_IMEM_CLK] = MSM7X30_CLK_LOCAL(1, &axi_imem_clk.clkr),
	[AXI_LI_APPS_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_apps_clk.clkr),
	[AXI_LI_VG_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_vg_clk.clkr),
	[AXI_ADM_CLK] = MSM7X30_CLK(GLBL, 8, 1, &axi_adm_clk.clkr, &pcom_adm_clk),
	[AXI_GRP_2D_CLK] = MSM7X30_CLK_LOCAL(1, &axi_grp_2d_clk.clkr),
	[AXI_LI_ADSP_A_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_adsp_a_clk.clkr),
	[AXI_LI_GRP_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_grp_clk.clkr),
	[AXI_LI_JPEG_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_jpeg_clk.clkr),
	[AXI_LI_VFE_CLK] = MSM7X30_CLK_LOCAL(1, &axi_li_vfe_clk.clkr),
	[AXI_MDP_CLK] = MSM7X30_CLK_LOCAL(1, &axi_mdp_clk.clkr),
	[AXI_MFC_CLK] = MSM7X30_CLK_LOCAL(1, &axi_mfc_clk.clkr),
	[AXI_ROTATOR_CLK] = MSM7X30_CLK(GLBL, 13, 1, &axi_rotator_clk.clkr, &pcom_axi_rotator_clk),
	[AXI_VPE_CLK] = MSM7X30_CLK_LOCAL(1, &axi_vpe_clk.clkr),

	/* Standard clocks. */
	[ADSP_CLK] = MSM7X30_CLK_REMOTE(&pcom_adsp_clk),
	[CAM_M_CLK] = MSM7X30_CLK(APPS3, 6, 1, &cam_m_clk.clkr, &pcom_cam_m_clk),
	[CODEC_SSBI_CLK] = MSM7X30_CLK_REMOTE(&pcom_codec_ssbi_clk),
	[CSI0_SRC] = MSM7X30_CLK(APPS3, 11, 1, &csi0_src.clkr, NULL),
	[CSI0_CLK] = MSM7X30_CLK(APPS3, 11, 1, &csi0_clk.clkr, &pcom_csi0_clk),
	[CSI0_VFE_CLK] = MSM7X30_CLK(APPS3, 11, 1, &csi0_vfe_clk.clkr, &pcom_csi0_vfe_clk),
	[EBI1_CLK] = MSM7X30_CLK_REMOTE(&pcom_ebi1_clk),
	[EBI1_FIXED_CLK] = MSM7X30_CLK_REMOTE(&pcom_ebi1_fixed_clk),
	[ECODEC_CLK] = MSM7X30_CLK_REMOTE(&pcom_ecodec_clk),
	[EMDH_CLK] = MSM7X30_CLK(ROW1, 7, 1, &emdh_clk.clkr, &pcom_emdh_clk),
	[GP_CLK] = MSM7X30_CLK_REMOTE(&pcom_gp_clk),
	[GRP_2D_SRC] = MSM7X30_CLK(APPS1, 6, 1, &grp_2d_src.clkr, NULL),
	[GRP_2D_CLK] = MSM7X30_CLK(APPS1, 6, 1, &grp_2d_clk.clkr, &pcom_grp_2d_clk),
	[GRP_3D_SRC] = MSM7X30_CLK(APPS2, 0, 1, &grp_3d_src.clkr, NULL),
	[GRP_3D_CLK] = MSM7X30_CLK(APPS2, 0, 1, &grp_3d_clk.clkr, &pcom_grp_3d_clk),
	[HDMI_CLK] = MSM7X30_CLK(APPS1, 31, 1, &hdmi_clk.clkr, &pcom_hdmi_clk),
	[I2C_SRC] = MSM7X30_CLK(ROW1, 11, 1, &i2c_src.clkr, NULL),
	[I2C_CLK] = MSM7X30_CLK(ROW1, 11, 1, &i2c_clk.clkr, &pcom_i2c_clk),
	[I2C_2_SRC] = MSM7X30_CLK(ROW1, 12, 1, &i2c_2_src.clkr, NULL),
	[I2C_2_CLK] = MSM7X30_CLK(ROW1, 12, 1, &i2c_2_clk.clkr, &pcom_i2c_2_clk),
	[IMEM_CLK] = MSM7X30_CLK(APPS2, 0, 1, &imem_clk.clkr, &pcom_imem_clk),
	[JPEG_SRC] = MSM7X30_CLK(APPS1, 0, 1, &jpeg_src.clkr, NULL),
	[JPEG_CLK] = MSM7X30_CLK(APPS1, 0, 1, &jpeg_clk.clkr, &pcom_jpeg_clk),
	[LPA_CODEC_SRC] = MSM7X30_CLK(APPS1, 23, 1, &lpa_codec_src.clkr, NULL),
	[LPA_CODEC_CLK] = MSM7X30_CLK(APPS1, 23, 1, &lpa_codec_clk.clkr, &pcom_lpa_codec_clk),
	[LPA_CORE_CLK] = MSM7X30_CLK(APPS1, 23, 1, &lpa_core_clk.clkr, &pcom_lpa_core_clk),
	[MDC_SRC] = MSM7X30_CLK(ROW1, 17, 1, &mdc_src.clkr, NULL),
	[MDC_CLK] = MSM7X30_CLK(ROW1, 17, 1, &mdc_clk.clkr, &pcom_mdc_clk),
	[MDP_SRC] = MSM7X30_CLK(APPS3, 0, 1, &mdp_src.clkr, NULL),
	[MDP_CLK] = MSM7X30_CLK(APPS3, 0, 1, &mdp_clk.clkr, &pcom_mdp_clk),
	[MDP_LCDC_PCLK_SRC] = MSM7X30_CLK(APPS2, 4, 1, &mdp_lcdc_pclk_src.clkr, NULL),
	[MDP_LCDC_PCLK_CLK] = MSM7X30_CLK(APPS2, 4, 1, &mdp_lcdc_pclk_clk.clkr, &pcom_mdp_lcdc_pclk_clk),
	[MDP_LCDC_PAD_PCLK_CLK] = MSM7X30_CLK(APPS2, 4, 1, &mdp_lcdc_pad_pclk_clk.clkr, &pcom_mdp_lcdc_pad_pclk_clk),
	[MDP_VSYNC_SRC] = MSM7X30_CLK(APPS2, 28, 1, &mdp_vsync_src.clkr, NULL),
	[MDP_VSYNC_CLK] = MSM7X30_CLK(APPS2, 28, 1, &mdp_vsync_clk.clkr, &pcom_mdp_vsync_clk),
	[MFC_SRC] = MSM7X30_CLK(APPS3, 2, 1, &mfc_src.clkr, NULL),
	[MFC_CLK] = MSM7X30_CLK(APPS3, 2, 1, &mfc_clk.clkr, &pcom_mfc_clk),
	[MFC_DIV2_CLK] = MSM7X30_CLK(APPS3, 2, 1, &mfc_div2_clk.clkr, &pcom_mfc_div2_clk),
	[MI2S_CODEC_RX_M_SRC] = MSM7X30_CLK(APPS1, 12, 1, &mi2s_codec_rx_m_src.clkr, NULL),
	[MI2S_CODEC_RX_M_CLK] = MSM7X30_CLK(APPS1, 12, 1, &mi2s_codec_rx_m_clk.clkr, &pcom_mi2s_codec_rx_m_clk),
	[MI2S_CODEC_RX_S_CLK] = MSM7X30_CLK(APPS1, 12, 1, &mi2s_codec_rx_s_clk.clkr, &pcom_mi2s_codec_rx_s_clk),
	[MI2S_CODEC_TX_M_SRC] = MSM7X30_CLK(APPS1, 14, 1, &mi2s_codec_tx_m_src.clkr, NULL),
	[MI2S_CODEC_TX_M_CLK] = MSM7X30_CLK(APPS1, 14, 1, &mi2s_codec_tx_m_clk.clkr, &pcom_mi2s_codec_tx_m_clk),
	[MI2S_CODEC_TX_S_CLK] = MSM7X30_CLK(APPS1, 14, 1, &mi2s_codec_tx_s_clk.clkr, &pcom_mi2s_codec_tx_s_clk),
	[MI2S_M_SRC] = MSM7X30_CLK(APPS1, 28, 1, &mi2s_m_src.clkr, NULL),
	[MI2S_M_CLK] = MSM7X30_CLK(APPS1, 28, 1, &mi2s_m_clk.clkr, &pcom_mi2s_m_clk),
	[MI2S_S_CLK] = MSM7X30_CLK(APPS1, 28, 1, &mi2s_s_clk.clkr, &pcom_mi2s_s_clk),
	[MIDI_SRC] = MSM7X30_CLK(APPS1, 22, 1, &midi_src.clkr, NULL),
	[MIDI_CLK] = MSM7X30_CLK(APPS1, 22, 1, &midi_clk.clkr, NULL),
	[PMDH_CLK] = MSM7X30_CLK(ROW1, 19, 1, &pmdh_clk.clkr, &pcom_pmdh_clk),
	[QUP_I2C_SRC] = MSM7X30_CLK(ROW2, 3, 1, &qup_i2c_src.clkr, NULL),
	[QUP_I2C_CLK] = MSM7X30_CLK(ROW2, 3, 1, &qup_i2c_clk.clkr, &pcom_qup_i2c_clk),
	[SDAC_SRC] = MSM7X30_CLK(APPS1, 26, 1, &sdac_src.clkr, NULL),
	[SDAC_CLK] = MSM7X30_CLK(APPS1, 26, 1, &sdac_clk.clkr, &pcom_sdac_clk),
	[SDAC_M_CLK] = MSM7X30_CLK(APPS1, 26, 1, &sdac_m_clk.clkr, &pcom_sdac_m_clk),
	[SDC1_SRC] = MSM7X30_CLK(ROW1, 23, 1, &sdc1_src.clkr, NULL),
	[SDC1_CLK] = MSM7X30_CLK(ROW1, 23, 1, &sdc1_clk.clkr, &pcom_sdc1_clk),
	[SDC2_SRC] = MSM7X30_CLK(ROW1, 25, 1, &sdc2_src.clkr, NULL),
	[SDC2_CLK] = MSM7X30_CLK(ROW1, 25, 1, &sdc2_clk.clkr, &pcom_sdc2_clk),
	[SDC3_SRC] = MSM7X30_CLK(ROW1, 27, 1, &sdc3_src.clkr, NULL),
	[SDC3_CLK] = MSM7X30_CLK(ROW1, 27, 1, &sdc3_clk.clkr, &pcom_sdc3_clk),
	[SDC4_SRC] = MSM7X30_CLK(ROW1, 29, 1, &sdc4_src.clkr, NULL),
	[SDC4_CLK] = MSM7X30_CLK(ROW1, 29, 1, &sdc4_clk.clkr, &pcom_sdc4_clk),
	[SPI_SRC] = MSM7X30_CLK(ROW2, 1, 1, &spi_src.clkr, NULL),
	[SPI_CLK] = MSM7X30_CLK(ROW2, 1, 1, &spi_clk.clkr, &pcom_spi_clk),
	[TSIF_REF_CLK] = MSM7X30_CLK(APPS2, 5, 1, &tsif_ref_clk.clkr, &pcom_tsif_ref_clk),
	[TV_SRC] = MSM7X30_CLK(APPS2, 2, 1, &tv_src.clkr, NULL),
	[TV_DAC_CLK] = MSM7X30_CLK(APPS2, 2, 1, &tv_dac_clk.clkr, &pcom_tv_dac_clk),
	[TV_ENC_CLK] = MSM7X30_CLK(APPS2, 2, 1, &tv_enc_clk.clkr, &pcom_tv_enc_clk),
	[UART1DM_SRC] = MSM7X30_CLK(ROW2, 6, 1, &uart1dm_src.clkr, NULL),
	[UART1DM_CLK] = MSM7X30_CLK(ROW2, 6, 1, &uart1dm_clk.clkr, &pcom_uart1dm_clk),
	[UART1_SRC] = MSM7X30_CLK(ROW2, 9, 1, &uart1_src.clkr, NULL),
	[UART1_CLK] = MSM7X30_CLK(ROW2, 9, 1, &uart1_clk.clkr, &pcom_uart1_clk),
	[UART2DM_SRC] = MSM7X30_CLK(ROW2, 8, 1, &uart2dm_src.clkr, NULL),
	[UART2DM_CLK] = MSM7X30_CLK(ROW2, 8, 1, &uart2dm_clk.clkr, &pcom_uart2dm_clk),
	[UART2_SRC] = MSM7X30_CLK(ROW1, 0, 1, &uart2_src.clkr, NULL),
	[UART2_CLK] = MSM7X30_CLK(ROW1, 0, 1, &uart2_clk.clkr, &pcom_uart2_clk),
	[UART3_CLK] = MSM7X30_CLK_REMOTE(&pcom_uart3_clk),
	[USB_HS2_CLK] = MSM7X30_CLK(ROW1, 2, 1, &usb_hs2_clk.clkr, &pcom_usb_hs2_clk),
	[USB_HS2_CORE_CLK] = MSM7X30_CLK(ROW1, 2, 1, &usb_hs2_core_clk.clkr, &pcom_usb_hs2_core_clk),
	[USB_HS3_CLK] = MSM7X30_CLK(ROW1, 4, 1, &usb_hs3_clk.clkr, &pcom_usb_hs3_clk),
	[USB_HS3_CORE_CLK] = MSM7X30_CLK(ROW1, 4, 1, &usb_hs3_core_clk.clkr, &pcom_usb_hs3_core_clk),
	[USB_HS_CLK] = MSM7X30_CLK(ROW2, 11, 1, &usb_hs_clk.clkr, &pcom_usb_hs_clk),
	[USB_HS_CORE_CLK] = MSM7X30_CLK(ROW2, 11, 1, &usb_hs_core_clk.clkr, &pcom_usb_hs_core_clk),
	[USB_HS_SRC] = MSM7X30_CLK(ROW2, 11, 1, &usb_hs_src.clkr, NULL),
	[USB_PHY_CLK] = MSM7X30_CLK_REMOTE(&pcom_usb_phy_clk),
	[VFE_SRC] = MSM7X30_CLK(APPS1, 8, 1, &vfe_src.clkr, NULL),
	[VFE_CLK] = MSM7X30_CLK(APPS1, 8, 1, &vfe_clk.clkr, &pcom_vfe_clk),
	[VFE_CAMIF_CLK] = MSM7X30_CLK(APPS1, 8, 1, &vfe_camif_clk.clkr, &pcom_vfe_camif_clk),
	[VFE_MDC_CLK] = MSM7X30_CLK(APPS1, 8, 1, &vfe_mdc_clk.clkr, &pcom_vfe_mdc_clk),
	[VPE_SRC] = MSM7X30_CLK(APPS3, 4, 1, &vpe_src.clkr, NULL),
	[VPE_CLK] = MSM7X30_CLK(APPS3, 4, 1, &vpe_clk.clkr, &pcom_vpe_clk),

	/* Peripheral bus clocks. */
	[ADM_P_CLK] = MSM7X30_CLK(GLBL, 13, 1, &adm_p_clk.clkr, NULL),
	[CAMIF_PAD_P_CLK] = MSM7X30_CLK(APPS3, 6, 1, &camif_pad_p_clk.clkr, &pcom_camif_pad_p_clk),
	[CE_CLK] = MSM7X30_CLK(GLBL, 8, 1, &ce_clk.clkr, &pcom_ce_clk),
	[CSI0_P_CLK] = MSM7X30_CLK(APPS3, 11, 1, &csi0_p_clk.clkr, &pcom_csi0_p_clk),
	[EMDH_P_CLK] = MSM7X30_CLK(ROW1, 7, 1, &emdh_p_clk.clkr, &pcom_emdh_p_clk),
	[GRP_2D_P_CLK] = MSM7X30_CLK(APPS1, 6, 1, &grp_2d_p_clk.clkr, &pcom_grp_2d_p_clk),
	[GRP_3D_P_CLK] = MSM7X30_CLK(APPS2, 0, 1, &grp_3d_p_clk.clkr, &pcom_grp_3d_p_clk),
	[JPEG_P_CLK] = MSM7X30_CLK(APPS1, 0, 1, &jpeg_p_clk.clkr, &pcom_jpeg_p_clk),
	[LPA_P_CLK] = MSM7X30_CLK(APPS1, 23, 1, &lpa_p_clk.clkr, &pcom_lpa_p_clk),
	[MDP_P_CLK] = MSM7X30_CLK(APPS2, 4, 1, &mdp_p_clk.clkr, &pcom_mdp_p_clk),
	[MFC_P_CLK] = MSM7X30_CLK(APPS3, 2, 1, &mfc_p_clk.clkr, &pcom_mfc_p_clk),
	[PMDH_P_CLK] = MSM7X30_CLK(ROW1, 19, 1, &pmdh_p_clk.clkr, &pcom_pmdh_p_clk),
	[ROTATOR_IMEM_CLK] = MSM7X30_CLK(GLBL, 13, 1, &rotator_imem_clk.clkr, &pcom_rotator_imem_clk),
	[ROTATOR_P_CLK] = MSM7X30_CLK(GLBL, 13, 1, &rotator_p_clk.clkr, &pcom_rotator_p_clk),
	[SDC1_P_CLK] = MSM7X30_CLK(ROW1, 23, 1, &sdc1_p_clk.clkr, &pcom_sdc1_p_clk),
	[SDC2_P_CLK] = MSM7X30_CLK(ROW1, 25, 1, &sdc2_p_clk.clkr, &pcom_sdc2_p_clk),
	[SDC3_P_CLK] = MSM7X30_CLK(ROW1, 27, 1, &sdc3_p_clk.clkr, &pcom_sdc3_p_clk),
	[SDC4_P_CLK] = MSM7X30_CLK(ROW1, 29, 1, &sdc4_p_clk.clkr, &pcom_sdc4_p_clk),
	[SPI_P_CLK] = MSM7X30_CLK(ROW2, 1, 1, &spi_p_clk.clkr, &pcom_spi_p_clk),
	[TSIF_P_CLK] = MSM7X30_CLK(APPS2, 5, 1, &tsif_p_clk.clkr, &pcom_tsif_p_clk),
	[UART1DM_P_CLK] = MSM7X30_CLK(GLBL, 6, 1, &uart1dm_p_clk.clkr, NULL),
	[UART2DM_P_CLK] = MSM7X30_CLK(GLBL, 8, 1, &uart2dm_p_clk.clkr, NULL),
	[USB_HS_P_CLK] = MSM7X30_CLK(ROW2, 11, 1, &usb_hs_p_clk.clkr, &pcom_usb_hs_p_clk),
	[USB_HS2_P_CLK] = MSM7X30_CLK(ROW1, 2, 1, &usb_hs2_p_clk.clkr, &pcom_usb_hs2_p_clk),
	[USB_HS3_P_CLK] = MSM7X30_CLK(ROW1, 4, 1, &usb_hs3_p_clk.clkr, &pcom_usb_hs3_p_clk),
	[VFE_P_CLK] = MSM7X30_CLK(APPS1, 8, 1, &vfe_p_clk.clkr, &pcom_vfe_p_clk),
};

static unsigned int ownership_regs[NUM_OWNERSHIP];

static void gcc_msm7x30_cache_owner_regs(struct gcc_msm7x30_data *data)
{
	struct regmap *regmap = data->regmap;

	regmap_read(regmap, SH2_OWN_GLBL_BASE_REG, &ownership_regs[GLBL]);
	regmap_read(regmap, SH2_OWN_APPS1_BASE_REG, &ownership_regs[APPS1]);
	regmap_read(regmap, SH2_OWN_APPS2_BASE_REG, &ownership_regs[APPS2]);
	regmap_read(regmap, SH2_OWN_ROW1_BASE_REG, &ownership_regs[ROW1]);
	regmap_read(regmap, SH2_OWN_ROW2_BASE_REG, &ownership_regs[ROW2]);
	regmap_read(regmap, SH2_OWN_APPS3_BASE_REG, &ownership_regs[APPS3]);

	pr_info("Clock ownership\n");
	pr_info("%-7s: %08x\n", "  GLBL", ownership_regs[GLBL]);
	pr_info("%-7s: %08x %08x %08x\n", "  APPS", ownership_regs[APPS1],
		 ownership_regs[APPS2], ownership_regs[APPS3]);
	pr_info("%-7s: %08x %08x\n", "  ROW", ownership_regs[ROW1],
		 ownership_regs[ROW2]);
}

static bool gcc_msm7x30_local_owner(const struct msm7x30_clk *clk)
{
	unsigned int reg = ownership_regs[clk->owner_reg];
	unsigned int bit = BIT(clk->owner_bit);

	return reg & bit;
}

#define BM(msb, lsb)	(((((uint32_t)-1) << (31-msb)) >> (31-msb+lsb)) << lsb)
#define BVAL(msb, lsb, val)	(((val) << lsb) & BM(msb, lsb))

static const struct reg_init {
	u32 reg;
	u32 mask;
	u32 val;
} ri_list[] = {
	/* Enable UMDX_P clock. Known to causes issues, so never turn off. */
	{GLBL_CLK_ENA_2_SC_REG, BIT(2), BIT(2)},

	/* Disable all the child clocks of USB_HS_SRC. */
	{ USBH_NS_REG, BIT(13) | BIT(9), 0 },
	{ USBH2_NS_REG, BIT(9) | BIT(4), 0 },
	{ USBH3_NS_REG, BIT(9) | BIT(4), 0 },

	{EMDH_NS_REG, BM(18, 17) , BVAL(18, 17, 0x3)}, /* RX div = div-4. */
	{PMDH_NS_REG, BM(18, 17), BVAL(18, 17, 0x3)}, /* RX div = div-4. */
	/* MI2S_CODEC_RX_S src = MI2S_CODEC_RX_M. */
	{MI2S_RX_NS_REG, BIT(14), 0x0},
	/* MI2S_CODEC_TX_S src = MI2S_CODEC_TX_M. */
	{MI2S_TX_NS_REG, BIT(14), 0x0},
	{MI2S_NS_REG, BIT(14), 0x0}, /* MI2S_S src = MI2S_M. */
	/* Allow DSP to decide the LPA CORE src. */
	{LPA_CORE_CLK_MA0_REG, BIT(0), BIT(0)},
	{LPA_CORE_CLK_MA2_REG, BIT(0), BIT(0)},
	{MI2S_CODEC_RX_DIV_REG, 0xF, 0xD}, /* MI2S_CODEC_RX_S div = div-8. */
	{MI2S_CODEC_TX_DIV_REG, 0xF, 0xD}, /* MI2S_CODEC_TX_S div = div-8. */
	{MI2S_DIV_REG, 0xF, 0x7}, /* MI2S_S div = div-8. */
	{MDC_NS_REG, 0x3, 0x3}, /* MDC src = external MDH src. */
	{SDAC_NS_REG, BM(15, 14), 0x0}, /* SDAC div = div-1. */
	/* Disable sources TCXO/5 & TCXO/6. UART1 src = TCXO*/
	{UART_NS_REG, BM(26, 25) | BM(2, 0), 0x0},
	/* HDMI div = div-1, non-inverted. tv_enc_src = tv_clk_src */
	{HDMI_NS_REG, 0x7, 0x0},
	{TV_NS_REG, BM(15, 14), 0x0}, /* tv_clk_src_div2 = div-1 */

	/* USBH core clocks src = USB_HS_SRC. */
	{USBH_NS_REG, BIT(15), BIT(15)},
	{USBH2_NS_REG, BIT(6), BIT(6)},
	{USBH3_NS_REG, BIT(6), BIT(6)},

	/* Do not sync graphics cores from AXI. */
	{GRP_2D_NS_REG, BM(14, 12), 0x0},
	{GRP_NS_REG, BM(14, 12), 0x0},

	/* MDP VSYNC src = LPXO. */
	{MDP_VSYNC_REG, BM(3, 2), BVAL(3, 2, 0x1)},
};

static int gcc_msm7x30_clock_init(struct regmap *regmap)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ri_list); i++) {
		const struct reg_init *ri = &ri_list[i];
		regmap_update_bits(regmap, ri->reg, ri->mask, ri->val);
	}
	return 0;
}

static int gcc_msm7x30_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct gcc_msm7x30_data *data;
	struct regmap *regmap_base;
	struct regmap *regmap_sh2;
	struct reset_controller_dev *rcdev;
	int ret;

	struct resource *res;
	void __iomem *base;
	int i;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->onecell.clks = devm_kcalloc(dev,
				ARRAY_SIZE(gcc_msm7x30_clks),
				sizeof(*data->onecell.clks),
				GFP_KERNEL);
	if (!data->onecell.clks) {
		pr_err("%s: failed to allocate memory for onecell\n", __func__);
		return -1;
	}
	data->onecell.clk_num = ARRAY_SIZE(gcc_msm7x30_clks);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	gcc_msm7x30_regmap_config.name = "base";
	regmap_base = devm_regmap_init_mmio(dev, base,
					    &gcc_msm7x30_regmap_config);
	data->regmap = regmap_base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	gcc_msm7x30_regmap_config.name = "sh2";
	regmap_sh2 = devm_regmap_init_mmio(dev, base,
					   &gcc_msm7x30_regmap_config);

	gcc_msm7x30_cache_owner_regs(data);

	gcc_msm7x30_clock_init(regmap_sh2);

	for (i = 0; i < ARRAY_SIZE(gcc_msm7x30_clks); i++) {
		const struct msm7x30_clk *clk = &gcc_msm7x30_clks[i];
		bool local;

		struct clk_hw *hw = NULL;
		struct clk *c;

		if (!clk->local && !clk->remote) {
			dev_err(dev, "invalid clock data for %d\n", i);
			continue;
		}

		/*
		 * MSM7X30 has 3 kinds of clocks:
		 * local, clk_ctl - Local clocks that are located in the
		 *                  non-shadow area.
		 * local, sh2     - Local clocks that are located in the
		 *                  shadow area, may be controlled via remote.
		 * remote         - Remote clocks that are controlled via proc
		 *                  comm.
		 */
		if (clk->always_local)
			local = true;
		else
			local = gcc_msm7x30_local_owner(clk);

		if (local && !clk->local) {
			dev_err(dev, "no local data for %d\n", i);
			continue;
		} else if (!local && !clk->remote) {
			/* Some clocks have 'src', which are not defined by
			 * remote. Gracefully skip these clocks if controlled
			 * by remote. */
			dev_dbg(dev, "no remote data for %d\n", i);
			continue;
		}

		if (local) {
			struct clk_regmap *local_clk = clk->local;
			if (clk->index == 0)
				local_clk->regmap = regmap_base;
			else
				local_clk->regmap = regmap_sh2;

			hw = &local_clk->hw;
		} else {
			struct clk_pcom *remote_clk = clk->remote;
			hw = &remote_clk->hw;
		}

		if (!hw) {
			dev_err(dev, "invalid config for %d, skipping\n", i);
			continue;
		}

		c = devm_clk_register(dev, hw);
		data->onecell.clks[i] = c;
	}

	of_clk_add_provider(node, of_clk_src_onecell_get, &data->onecell);

	rcdev = &data->rcdev;
	rcdev->of_node = node;
	rcdev->ops = &pc_clk_reset_ops;
	rcdev->owner = dev->driver->owner;
	rcdev->nr_resets = NR_RESETS;

	ret = reset_controller_register(rcdev);

	return 0;
}

static const struct of_device_id gcc_msm7x30_match_table[] = {
	{ .compatible = "qcom,gcc-msm7x30" },
	{ }
};
MODULE_DEVICE_TABLE(of, gcc_msm7x30_match_table);

static struct platform_driver gcc_msm7x30_driver = {
	.probe		= gcc_msm7x30_probe,
	.driver		= {
		.name	= "gcc-msm7x30",
		.of_match_table = gcc_msm7x30_match_table,
	},
};

static int __init gcc_msm7x30_init(void)
{
	return platform_driver_register(&gcc_msm7x30_driver);
}
core_initcall(gcc_msm7x30_init);

static void __exit gcc_msm7x30_exit(void)
{
	platform_driver_unregister(&gcc_msm7x30_driver);
}
module_exit(gcc_msm7x30_exit);

MODULE_DESCRIPTION("GCC MSM 7X30 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gcc-msm7x30");

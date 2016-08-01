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

#ifndef _DT_BINDINGS_CLK_MSM_GCC_7X30_H
#define _DT_BINDINGS_CLK_MSM_GCC_7X30_H

/* Clock sources. */
#define TCXO_CLK			0
#define LPXO_CLK			1
#define PLL0_CLK			2
#define PLL0_VOTE_CLK			3
#define PLL1_CLK			4
#define PLL1_VOTE_CLK			5
#define PLL2_CLK			6
#define PLL2_VOTE_CLK			7
#define PLL3_CLK			8
#define PLL3_VOTE_CLK			9
#define PLL4_CLK			10
#define PLL4_VOTE_CLK			11
#define PLL5_CLK			12
#define PLL5_VOTE_CLK			13
#define PLL6_CLK			14
#define PLL6_VOTE_CLK			15

/* AXI bridge clocks. */
#define GLBL_ROOT_CLK			16
#define AXI_IMEM_CLK			17
#define AXI_LI_APPS_CLK			18
#define AXI_LI_VG_CLK			19
#define AXI_ADM_CLK			20
#define AXI_GRP_2D_CLK			21
#define AXI_LI_ADSP_A_CLK			22
#define AXI_LI_GRP_CLK			23
#define AXI_LI_JPEG_CLK			24
#define AXI_LI_VFE_CLK			25
#define AXI_MDP_CLK			26
#define AXI_MFC_CLK			27
#define AXI_ROTATOR_CLK			28
#define AXI_VPE_CLK			29

/* Standard clocks. */
#define ADSP_CLK			30
#define CAM_M_CLK			31
#define CODEC_SSBI_CLK			32
#define CSI0_SRC			33
#define CSI0_CLK			34
#define CSI0_VFE_CLK			35
#define EBI1_CLK			36
#define EBI1_FIXED_CLK			37
#define ECODEC_CLK			38
#define EMDH_CLK			39
#define GP_CLK			40
#define GRP_2D_SRC			41
#define GRP_2D_CLK			42
#define GRP_3D_SRC			43
#define GRP_3D_CLK			44
#define HDMI_CLK			45
#define I2C_SRC			46
#define I2C_CLK			47
#define I2C_2_SRC			48
#define I2C_2_CLK			49
#define IMEM_CLK			50
#define JPEG_SRC			51
#define JPEG_CLK			52
#define LPA_CODEC_SRC			53
#define LPA_CODEC_CLK			54
#define LPA_CORE_CLK			55
#define MDC_SRC			56
#define MDC_CLK			57
#define MDP_SRC			58
#define MDP_CLK			59
#define MDP_LCDC_PAD_PCLK_CLK			60
#define MDP_LCDC_PCLK_SRC			61
#define MDP_LCDC_PCLK_CLK			62
#define MDP_VSYNC_SRC			63
#define MDP_VSYNC_CLK			64
#define MFC_SRC			65
#define MFC_CLK			66
#define MFC_DIV2_CLK			67
#define MI2S_CODEC_RX_M_SRC			68
#define MI2S_CODEC_RX_M_CLK			69
#define MI2S_CODEC_RX_S_CLK			70
#define MI2S_CODEC_TX_M_SRC			71
#define MI2S_CODEC_TX_M_CLK			72
#define MI2S_CODEC_TX_S_CLK			73
#define MI2S_M_SRC			74
#define MI2S_M_CLK			75
#define MI2S_S_CLK			76
#define MIDI_SRC			77
#define MIDI_CLK			78
#define PMDH_CLK			79
#define QUP_I2C_SRC			80
#define QUP_I2C_CLK			81
#define SDAC_SRC			82
#define SDAC_CLK			83
#define SDAC_M_CLK			84
#define SDC1_SRC			85
#define SDC1_CLK			86
#define SDC2_SRC			87
#define SDC2_CLK			88
#define SDC3_SRC			89
#define SDC3_CLK			90
#define SDC4_SRC			91
#define SDC4_CLK			92
#define SPI_SRC			93
#define SPI_CLK			94
#define TSIF_REF_CLK			95
#define TV_SRC			96
#define TV_DAC_CLK			97
#define TV_ENC_CLK			98
#define UART1_SRC			99
#define UART1_CLK			100
#define UART1DM_SRC			101
#define UART1DM_CLK			102
#define UART2_SRC			103
#define UART2_CLK			104
#define UART2DM_SRC			105
#define UART2DM_CLK			106
#define UART3_CLK			107
#define USB_HS_SRC			108
#define USB_HS_CLK			109
#define USB_HS_CORE_CLK			110
#define USB_HS2_CLK			111
#define USB_HS2_CORE_CLK			112
#define USB_HS3_CLK			113
#define USB_HS3_CORE_CLK			114
#define USB_PHY_CLK			115
#define VFE_CAMIF_CLK			116
#define VFE_SRC			117
#define VFE_CLK			118
#define VFE_MDC_CLK			119
#define VPE_SRC			120
#define VPE_CLK			121

/* Peripheral bus clocks. */
#define ADM_P_CLK			122
#define CAMIF_PAD_P_CLK			123
#define CE_CLK			124
#define CSI0_P_CLK			125
#define EMDH_P_CLK			126
#define GRP_2D_P_CLK			127
#define GRP_3D_P_CLK			128
#define JPEG_P_CLK			129
#define LPA_P_CLK			130
#define MDP_P_CLK			131
#define MFC_P_CLK			132
#define PMDH_P_CLK			133
#define ROTATOR_IMEM_CLK			134
#define ROTATOR_P_CLK			135
#define SDC1_P_CLK			136
#define SDC2_P_CLK			137
#define SDC3_P_CLK			138
#define SDC4_P_CLK			139
#define SPI_P_CLK			140
#define TSIF_P_CLK			141
#define UART1DM_P_CLK			142
#define UART2DM_P_CLK			143
#define USB_HS_P_CLK			144
#define USB_HS2_P_CLK			145
#define USB_HS3_P_CLK			146
#define VFE_P_CLK			147

#endif

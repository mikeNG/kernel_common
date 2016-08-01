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

#ifndef _DT_BINDINGS_RESET_QCOM_PROCCOMM_H
#define _DT_BINDINGS_RESET_QCOM_PROCCOMM_H

/* clock IDs used by the modem processor */

#define ACPU_RESET			0   /* Applications processor clock */
#define ADM_RESET			1   /* Applications data mover clock */
#define ADSP_RESET			2   /* ADSP clock */
#define EBI1_RESET			3   /* External bus interface 1 clock */
#define EBI2_RESET			4   /* External bus interface 2 clock */
#define ECODEC_RESET			5   /* External CODEC clock */
#define EMDH_RESET			6   /* External MDDI host clock */
#define GP_RESET			7   /* General purpose clock */
#define GRP_3D_RESET			8   /* Graphics clock */
#define I2C_RESET			9   /* I2C clock */
#define ICODEC_RX_RESET			10  /* Internal CODEX RX clock */
#define ICODEC_TX_RESET			11  /* Internal CODEX TX clock */
#define IMEM_RESET			12  /* Internal graphics memory clock */
#define MDC_RESET			13  /* MDDI client clock */
#define MDP_RESET			14  /* Mobile display processor clock */
#define PBUS_RESET			15  /* Peripheral bus clock */
#define PCM_RESET			16  /* PCM clock */
#define PMDH_RESET			17  /* Primary MDDI host clock */
#define SDAC_RESET			18  /* Stereo DAC clock */
#define SDC1_RESET			19  /* Secure Digital Card clocks */
#define SDC1_P_RESET			20
#define SDC2_RESET			21
#define SDC2_P_RESET			22
#define SDC3_RESET			23
#define SDC3_P_RESET			24
#define SDC4_RESET			25
#define SDC4_P_RESET			26
#define TSIF_RESET			27  /* Transport Stream Interface clocks */
#define TSIF_REF_RESET			28
#define TV_DAC_RESET			29  /* TV clocks */
#define TV_ENC_RESET			30
#define UART1_RESET			31  /* UART clocks */
#define UART2_RESET			32
#define UART3_RESET			33
#define UART1DM_RESET			34
#define UART2DM_RESET			35
#define USB_HS_RESET			36  /* High speed USB core clock */
#define USB_HS_P_RESET			37  /* High speed USB pbus clock */
#define USB_OTG_RESET			38  /* Full speed USB clock */
#define VDC_RESET			39  /* Video controller clock */
#define VFE_MDC_RESET			40  /* Camera / Video Front End clock */
#define VFE_RESET			41  /* VFE MDDI client clock */
#define MDP_LCDC_PCLK_RESET		42
#define MDP_LCDC_PAD_PCLK_RESET		43
#define MDP_VSYNC_RESET			44
#define SPI_RESET			45
#define VFE_AXI_RESET			46
#define USB_HS2_RESET			47  /* High speed USB 2 core clock */
#define USB_HS2_P_RESET			48  /* High speed USB 2 pbus clock */
#define USB_HS3_RESET			49  /* High speed USB 3 core clock */
#define USB_HS3_P_RESET			50  /* High speed USB 3 pbus clock */
#define GRP_3D_P_RESET			51  /* Graphics pbus clock */
#define USB_PHY_RESET			52  /* USB PHY clock */
#define USB_HS_CORE_RESET		53  /* High speed USB 1 core clock */
#define USB_HS2_CORE_RESET		54  /* High speed USB 2 core clock */
#define USB_HS3_CORE_RESET		55  /* High speed USB 3 core clock */
#define CAM_M_RESET			56
#define CAMIF_PAD_P_RESET		57
#define GRP_2D_RESET			58
#define GRP_2D_P_RESET			59
#define I2S_RESET			60
#define JPEG_RESET			61
#define JPEG_P_RESET			62
#define LPA_CODEC_RESET			63
#define LPA_CORE_RESET			64
#define LPA_P_RESET			65
#define MDC_IO_RESET			66
#define MDC_P_RESET			67
#define MFC_RESET			68
#define MFC_DIV2_RESET			69
#define MFC_P_RESET			70
#define QUP_I2C_RESET			71
#define ROTATOR_IMEM_RESET		72
#define ROTATOR_P_RESET			73
#define VFE_CAMIF_RESET			74
#define VFE_P_RESET			75
#define VPE_RESET			76
#define I2C_2_RESET			77
#define MI2S_CODEC_RX_S_RESET		78
#define MI2S_CODEC_RX_M_RESET		79
#define MI2S_CODEC_TX_S_RESET		80
#define MI2S_CODEC_TX_M_RESET		81
#define PMDH_P_RESET			82
#define EMDH_P_RESET			83
#define SPI_P_RESET			84
#define TSIF_P_RESET			85
#define MDP_P_RESET			86
#define SDAC_M_RESET			87
#define MI2S_S_RESET			88
#define MI2S_M_RESET			89
#define AXI_ROTATOR_RESET		90
#define HDMI_RESET			91
#define CSI0_RESET			92
#define CSI0_VFE_RESET			93
#define CSI0_P_RESET			94
#define CSI1_RESET			95
#define CSI1_VFE_RESET			96
#define CSI1_P_RESET			97
#define GSBI_RESET			98
#define GSBI_P_RESET			99
#define CE_RESET			100 /* Crypto engine */
#define CODEC_SSBI_RESET		101
#define TCXO_DIV4_RESET			102
#define GSBI1_QUP_RESET			103
#define GSBI2_QUP_RESET			104
#define GSBI1_QUP_P_RESET		105
#define GSBI2_QUP_P_RESET		106
#define DSI_RESET			107
#define DSI_ESC_RESET			108
#define DSI_PIXEL_RESET			109
#define DSI_BYTE_RESET			110
#define EBI1_FIXED_RESET		111 /* Not dropped during power-collapse */
#define DSI_REF_RESET			112
#define MDP_DSI_P_RESET			113
#define AHB_M_RESET			114
#define AHB_S_RESET			115

#define NR_RESETS			116

#endif

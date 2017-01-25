/* Copyright (c) 2008-2010, The Linux Foundation. All rights reserved.
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

#ifndef MDDIHOSTI_H
#define MDDIHOSTI_H

#include "mddi.h"
#include "mddihost.h"

/* Register offsets in MDDI, applies to PMDH and EMDH */
#define MDDI_CMD		0x0000
#define MDDI_VERSION		0x0004
#define MDDI_PRI_PTR		0x0008
#define MDDI_BPS		0x0010
#define MDDI_SPM		0x0014
#define MDDI_INT		0x0018
#define MDDI_INTEN		0x001c
#define MDDI_REV_PTR		0x0020
#define MDDI_REV_SIZE		0x0024
#define MDDI_STAT		0x0028
#define MDDI_REV_RATE_DIV	0x002c
#define MDDI_REV_CRC_ERR	0x0030
#define MDDI_TA1_LEN		0x0034
#define MDDI_TA2_LEN		0x0038
#define MDDI_TEST		0x0040
#define MDDI_REV_PKT_CNT	0x0044
#define MDDI_DRIVE_HI		0x0048
#define MDDI_DRIVE_LO		0x004c
#define MDDI_DISP_WAKE		0x0050
#define MDDI_REV_ENCAP_SZ	0x0054
#define MDDI_RTD_VAL		0x0058
#define MDDI_PAD_CTL		0x0068
#define MDDI_DRIVER_START_CNT	0x006c
#define MDDI_CORE_VER		0x008c
#define MDDI_FIFO_ALLOC		0x0090
#define MDDI_PAD_IO_CTL		0x00a0
#define MDDI_PAD_CAL		0x00a4

#define MDDI_HOST_MAX_CLIENT_REG_IN_SAME_ADDR 1

extern int32_t mddi_client_type;

#define mddi_host_reg_outm(reg, mask, val) \
do { \
	void __iomem *__addr = msm_pmdh_base + MDDI_##reg; \
	writel((readl(__addr) & ~(mask)) | ((val) & (mask)), __addr); \
} while (0)

#define mddi_host_reg_out(reg, val) \
	writel(val, msm_pmdh_base + MDDI_##reg)

#define mddi_host_reg_in(reg) \
	readl(msm_pmdh_base + MDDI_##reg)

#define mddi_host_reg_inm(reg, mask) \
	(readl(msm_pmdh_base + MDDI_##reg) & (mask))

/* MDP sends 256 pixel packets, so lower value hibernates more without
 * significantly increasing latency of waiting for next subframe */
#define MDDI_HOST_BYTES_PER_SUBFRAME  0x3C00

#define MDDI_HOST_TA2_LEN       0x001a
#define MDDI_HOST_REV_RATE_DIV  0x0004

#define GCC_PACKED __attribute__((packed))
typedef struct GCC_PACKED {
	uint16_t packet_length;
	/* total # of uint8_ts in the packet not including
		the packet_length field. */

	uint16_t packet_type;
	/* A Packet Type of 70 identifies the packet as
		a Client status Packet. */

	uint16_t bClient_ID;
	/* This field is reserved for future use and shall
		be set to zero. */

} mddi_rev_packet_type;

typedef struct GCC_PACKED {
	uint16_t packet_length;
	/* total # of uint8_ts in the packet not including
		the packet_length field. */

	uint16_t packet_type;
	/* A Packet Type of 70 identifies the packet as
		a Client status Packet. */

	uint16_t bClient_ID;
	/* This field is reserved for future use and shall
		be set to zero. */

	uint16_t reverse_link_request;
	/* 16 bit unsigned integer with number of uint8_ts client
		needs in the * reverse encapsulation message
		to transmit data. */

	uint8_t crc_error_count;
	uint8_t capability_change;
	uint16_t graphics_busy_flags;

	uint16_t parameter_CRC;
	/* 16-bit CRC of all the uint8_ts in the packet
		including Packet Length. */

} mddi_client_status_type;

typedef struct GCC_PACKED {
	uint16_t packet_length;
	/* total # of uint8_ts in the packet not including
		the packet_length field. */

	uint16_t packet_type;
	/* A Packet Type of 66 identifies the packet as
		a Client Capability Packet. */

	uint16_t bClient_ID;
	/* This field is reserved for future use and
		shall be set to zero. */

	uint16_t Protocol_Version;
	uint16_t Minimum_Protocol_Version;
	uint16_t Data_Rate_Capability;
	uint8_t Interface_Type_Capability;
	uint8_t Number_of_Alt_Displays;
	uint16_t PostCal_Data_Rate;
	uint16_t Bitmap_Width;
	uint16_t Bitmap_Height;
	uint16_t Display_Window_Width;
	uint16_t Display_Window_Height;
	uint32_t Color_Map_Size;
	uint16_t Color_Map_RGB_Width;
	uint16_t RGB_Capability;
	uint8_t Monochrome_Capability;
	uint8_t Reserved_1;
	uint16_t Y_Cb_Cr_Capability;
	uint16_t Bayer_Capability;
	uint16_t Alpha_Cursor_Image_Planes;
	uint32_t Client_Feature_Capability_Indicators;
	uint8_t Maximum_Video_Frame_Rate_Capability;
	uint8_t Minimum_Video_Frame_Rate_Capability;
	uint16_t Minimum_Sub_frame_Rate;
	uint16_t Audio_Buffer_Depth;
	uint16_t Audio_Channel_Capability;
	uint16_t Audio_Sample_Rate_Capability;
	uint8_t Audio_Sample_Resolution;
	uint8_t Mic_Audio_Sample_Resolution;
	uint16_t Mic_Sample_Rate_Capability;
	uint8_t Keyboard_Data_Format;
	uint8_t pointing_device_data_format;
	uint16_t content_protection_type;
	uint16_t Mfr_Name;
	uint16_t Product_Code;
	uint16_t Reserved_3;
	uint32_t Serial_Number;
	uint8_t Week_of_Manufacture;
	uint8_t Year_of_Manufacture;

	uint16_t parameter_CRC;
	/* 16-bit CRC of all the uint8_ts in the packet including Packet Length. */

} mddi_client_capability_type;

typedef struct GCC_PACKED {
	uint16_t packet_length;
	/* total # of uint8_ts in the packet not including the packet_length field. */

	uint16_t packet_type;
	/* A Packet Type of 16 identifies the packet as a Video Stream Packet. */

	uint16_t bClient_ID;
	/* This field is reserved for future use and shall be set to zero. */

	uint16_t video_data_format_descriptor;
	/* format of each pixel in the Pixel Data in the present stream in the
	 * present packet.
	 * If bits [15:13] = 000 monochrome
	 * If bits [15:13] = 001 color pixels (palette).
	 * If bits [15:13] = 010 color pixels in raw RGB
	 * If bits [15:13] = 011 data in 4:2:2 Y Cb Cr format
	 * If bits [15:13] = 100 Bayer pixels
	 */

	uint16_t pixel_data_attributes;
	/* interpreted as follows:
	 * Bits [1:0] = 11  pixel data is displayed to both eyes
	 * Bits [1:0] = 10  pixel data is routed to the left eye only.
	 * Bits [1:0] = 01  pixel data is routed to the right eye only.
	 * Bits [1:0] = 00  pixel data is routed to the alternate display.
	 * Bit 2 is 0  Pixel Data is in the standard progressive format.
	 * Bit 2 is 1  Pixel Data is in interlace format.
	 * Bit 3 is 0  Pixel Data is in the standard progressive format.
	 * Bit 3 is 1  Pixel Data is in alternate pixel format.
	 * Bit 4 is 0  Pixel Data is to or from the display frame buffer.
	 * Bit 4 is 1  Pixel Data is to or from the camera.
	 * Bit 5 is 0  pixel data contains the next consecutive row of pixels.
	 * Bit 5 is 1  X Left Edge, Y Top Edge, X Right Edge, Y Bottom Edge,
	 *             X Start, and Y Start parameters are not defined and
	 *             shall be ignored by the client.
	 * Bits [7:6] = 01  Pixel data is written to the offline image buffer.
	 * Bits [7:6] = 00  Pixel data is written to the buffer to refresh display.
	 * Bits [7:6] = 11  Pixel data is written to all image buffers.
	 * Bits [7:6] = 10  Invalid. Reserved for future use.
	 * Bits 8 through 11 alternate display number.
	 * Bits 12 through 14 are reserved for future use and shall be set to zero.
	 * Bit 15 is 1 the row of pixels is the last row of pixels in a frame.
	 */

	uint16_t x_left_edge;
	uint16_t y_top_edge;
	/* X,Y coordinate of the top left edge of the screen window */

	uint16_t x_right_edge;
	uint16_t y_bottom_edge;
	/*  X,Y coordinate of the bottom right edge of the window being updated. */

	uint16_t x_start;
	uint16_t y_start;
	/*  (X Start, Y Start) is the first pixel in the Pixel Data field below. */

	uint16_t pixel_count;
	/*  number of pixels in the Pixel Data field below. */

	uint16_t parameter_CRC;
	/*  16-bit CRC of all uint8_ts from the Packet Length to the Pixel Count. */

	uint16_t reserved;
	/* 16-bit variable to make structure align on 4 uint8_t boundary */

} mddi_video_stream_packet_type;

typedef struct GCC_PACKED {
	uint16_t packet_length;
	/* total # of uint8_ts in the packet not including the packet_length field. */

	uint16_t packet_type;
	/* A Packet Type of 146 identifies the packet as a Register Access Packet. */

	uint16_t bClient_ID;
	/* This field is reserved for future use and shall be set to zero. */

	uint16_t read_write_info;
	/* Bits 13:0  a 14-bit unsigned integer that specifies the number of
	 *            32-bit Register Data List items to be transferred in the
	 *            Register Data List field.
	 * Bits[15:14] = 00  Write to register(s);
	 * Bits[15:14] = 10  Read from register(s);
	 * Bits[15:14] = 11  Response to a Read.
	 * Bits[15:14] = 01  this value is reserved for future use. */

	uint32_t register_address;
	/* the register address that is to be written to or read from. */

	uint16_t parameter_CRC;
	/* 16-bit CRC of all uint8_ts from the Packet Length to the Register Address. */

	uint32_t register_data_list[MDDI_HOST_MAX_CLIENT_REG_IN_SAME_ADDR];
	/* list of 4-uint8_t register data values for/from client registers */
	/* For multi-read/write, 512(128 * 4) uint8_ts of data available */
} mddi_register_access_packet_type;

typedef union GCC_PACKED {
	mddi_video_stream_packet_type video_pkt;
	mddi_register_access_packet_type register_pkt;

	/* add 48 uint8_t pad to ensure 64 uint8_t llist struct, that can be
	 * manipulated easily with cache */
	uint32_t alignment_pad[12];	/* 48 uint8_ts */
} mddi_packet_header_type;

typedef struct GCC_PACKED mddi_host_llist_struct {
	uint16_t link_controller_flags;
	uint16_t packet_header_count;
	uint16_t packet_data_count;
	void *packet_data_pointer;
	struct mddi_host_llist_struct *next_packet_pointer;
	uint16_t reserved;
	mddi_packet_header_type packet_header;
} mddi_linked_list_type;

typedef struct {
	struct completion done_comp;
	mddi_llist_done_cb_type done_cb;
	uint16_t next_idx;
	bool waiting;
	bool in_use;
} mddi_linked_list_notify_type;

#define MDDI_LLIST_POOL_SIZE 0x1000
#define MDDI_MAX_NUM_LLIST_ITEMS (MDDI_LLIST_POOL_SIZE / \
	sizeof(mddi_linked_list_type))
#define UNASSIGNED_INDEX MDDI_MAX_NUM_LLIST_ITEMS
#define MDDI_FIRST_DYNAMIC_LLIST_IDX 0

/* Static llist items can be used for applications that frequently send
 * the same set of packets using the linked list interface. */
/* Here we configure for 6 static linked list items:
 *  The 1st is used for a the adaptive backlight setting.
 *  and the remaining 5 are used for sending window adjustments for
 *  MDDI clients that need windowing info sent separate from video
 *  packets. */
#define MDDI_NUM_STATIC_ABL_ITEMS 1
#define MDDI_NUM_STATIC_WINDOW_ITEMS 5
#define MDDI_NUM_STATIC_LLIST_ITEMS (MDDI_NUM_STATIC_ABL_ITEMS + \
				MDDI_NUM_STATIC_WINDOW_ITEMS)
#define MDDI_NUM_DYNAMIC_LLIST_ITEMS (MDDI_MAX_NUM_LLIST_ITEMS - \
				MDDI_NUM_STATIC_LLIST_ITEMS)

#define MDDI_FIRST_STATIC_LLIST_IDX  MDDI_NUM_DYNAMIC_LLIST_ITEMS
#define MDDI_FIRST_STATIC_ABL_IDX  MDDI_FIRST_STATIC_LLIST_IDX
#define MDDI_FIRST_STATIC_WINDOW_IDX  (MDDI_FIRST_STATIC_LLIST_IDX + \
				MDDI_NUM_STATIC_ABL_ITEMS)

/* GPIO registers */
#define VSYNC_WAKEUP_REG		0x80
#define GPIO_REG			0x81
#define GPIO_OUTPUT_REG			0x82
#define GPIO_INTERRUPT_REG		0x83
#define GPIO_INTERRUPT_ENABLE_REG	0x84
#define GPIO_POLARITY_REG		0x85

/* Interrupt Bits */
#define MDDI_INT_PRI_PTR_READ		0x0001
#define MDDI_INT_SEC_PTR_READ		0x0002
#define MDDI_INT_REV_DATA_AVAIL		0x0004
#define MDDI_INT_DISP_REQ		0x0008
#define MDDI_INT_PRI_UNDERFLOW		0x0010
#define MDDI_INT_SEC_UNDERFLOW		0x0020
#define MDDI_INT_REV_OVERFLOW		0x0040
#define MDDI_INT_CRC_ERROR		0x0080
#define MDDI_INT_MDDI_IN		0x0100
#define MDDI_INT_PRI_OVERWRITE		0x0200
#define MDDI_INT_SEC_OVERWRITE		0x0400
#define MDDI_INT_REV_OVERWRITE		0x0800
#define MDDI_INT_DMA_FAILURE		0x1000
#define MDDI_INT_LINK_ACTIVE		0x2000
#define MDDI_INT_IN_HIBERNATION		0x4000
#define MDDI_INT_PRI_LINK_LIST_DONE	0x8000
#define MDDI_INT_SEC_LINK_LIST_DONE	0x10000
#define MDDI_INT_NO_CMD_PKTS_PEND	0x20000
#define MDDI_INT_RTD_FAILURE		0x40000

#define MDDI_INT_ERROR_CONDITIONS ( \
	MDDI_INT_PRI_UNDERFLOW | MDDI_INT_SEC_UNDERFLOW | \
	MDDI_INT_REV_OVERFLOW | MDDI_INT_CRC_ERROR | \
	MDDI_INT_PRI_OVERWRITE | MDDI_INT_SEC_OVERWRITE | \
	MDDI_INT_RTD_FAILURE | \
	MDDI_INT_REV_OVERWRITE | MDDI_INT_DMA_FAILURE)

#define MDDI_INT_LINK_STATE_CHANGES ( \
	MDDI_INT_LINK_ACTIVE | MDDI_INT_IN_HIBERNATION)

/* Status Bits */
#define MDDI_STAT_LINK_ACTIVE		0x0001
#define MDDI_STAT_NEW_REV_PTR		0x0002
#define MDDI_STAT_NEW_PRI_PTR		0x0004
#define MDDI_STAT_NEW_SEC_PTR		0x0008
#define MDDI_STAT_IN_HIBERNATION	0x0010
#define MDDI_STAT_PRI_LINK_LIST_DONE	0x0020
#define MDDI_STAT_SEC_LINK_LIST_DONE	0x0040
#define MDDI_STAT_PENDING_TIMING_PKT	0x0080
#define MDDI_STAT_PENDING_REV_ENCAP	0x0100
#define MDDI_STAT_PENDING_POWERDOWN	0x0200
#define MDDI_STAT_RTD_MEAS_FAIL		0x0800
#define MDDI_STAT_CLIENT_WAKEUP_REQ	0x1000

/* Command Bits */
#define MDDI_CMD_POWERDOWN		0x0100
#define MDDI_CMD_POWERUP		0x0200
#define MDDI_CMD_HIBERNATE		0x0300
#define MDDI_CMD_RESET			0x0400
#define MDDI_CMD_DISP_IGNORE		0x0501
#define MDDI_CMD_DISP_LISTEN		0x0500
#define MDDI_CMD_SEND_REV_ENCAP		0x0600
#define MDDI_CMD_GET_CLIENT_CAP		0x0601
#define MDDI_CMD_GET_CLIENT_STATUS	0x0602
#define MDDI_CMD_SEND_RTD		0x0700
#define MDDI_CMD_LINK_ACTIVE		0x0900
#define MDDI_CMD_PERIODIC_REV_ENCAP	0x0A00
#define MDDI_CMD_FW_LINK_SKEW_CAL	0x0D00

extern void mddi_host_init(void);
extern uint16_t mddi_get_next_free_llist_item(bool wait);
extern uint16_t mddi_get_reg_read_llist_item(bool wait);
extern void mddi_queue_forward_packets(uint16_t first_llist_idx,
				       uint16_t last_llist_idx,
				       bool wait,
				       mddi_llist_done_cb_type llist_done_cb);

extern mddi_linked_list_type *llist_extern;
extern mddi_linked_list_type *llist_dma_extern;
extern mddi_linked_list_notify_type *llist_extern_notify;
extern struct timer_list mddi_host_timer;

typedef struct {
	uint16_t transmitting_start_idx;
	uint16_t transmitting_end_idx;
	uint16_t waiting_start_idx;
	uint16_t waiting_end_idx;
	uint16_t reg_read_idx;
	uint16_t next_free_idx;
	bool reg_read_waiting;
} mddi_llist_info_type;

#ifndef FEATURE_MDDI_DISABLE_REVERSE
uint32_t mddi_get_client_id(void);
#endif
void mddi_host_timer_service(unsigned long data);
void mddi_host_client_cnt_reset(void);

void mddi_host_isr_primary(void);
#endif /* MDDIHOSTI_H */

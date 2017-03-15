
/* drivers/input/touchscreen/synaptics_i2c_rmi4_hw.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (C) 2008 Texas Instrument Inc.
 * Copyright (C) 2009 Synaptics, Inc.
 * Copyright (C) 2015 Rudolf Tammekivi
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#ifdef CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE
#include <asm/system_misc.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#endif

#define SYNAPTICS_I2C_RMI4_NAME "synaptics_i2c_rmi4_hw"

#define REPORTING_MODE		0x001	/* 001: Reduced reporting mode. */
#define DEFAULT_REP_SENS	4	/* Default delta between reports. */
#define SENSITIVITY		0x08

typedef __u8 u4;
typedef __u16 u12;

struct rmi_function_info {

	/* This is the number of data points supported - for example, for
	 * function $11 (2D sensor) the number of data points is equal to
	 * the number of fingers - for function $19 (buttons)it is equal to the
	 * number of buttons
	 */
	__u8 points_supported;

	/* This is the interrupt register and mask - needed for enabling the
	 * interrupts and for checking what source had caused the attention line
	 * interrupt.
	 */
	__u8 interrupt_offset;
	__u8 interrupt_mask;

	__u8 data_offset;
	__u8 data_length;
};

enum f11_finger_status {
	f11_finger_none = 0,
	f11_finger_accurate = 1,
	f11_finger_inaccurate = 2,
};

struct f11_finger_data {
	enum f11_finger_status status;

	u12 x;
	u12 y;
	u8 z;

	unsigned int speed;
	bool active;
};

struct synaptics_i2c_rmi4 {
	struct i2c_client *client;
	struct input_dev *input_dev;
	int use_irq;
	struct hrtimer timer;
	struct work_struct work;

	struct regulator *avdd;
	int gpio_reset;

	unsigned int display_x;
	unsigned int display_y;
	unsigned int panel_x;
	unsigned int panel_y;

	__u8 data_reg;
	__u8 data_length;
	__u8 *data;
	struct i2c_msg data_i2c_msg[2];

	struct rmi_function_info f01;

	int hasF11;
	struct rmi_function_info f11;
	int f11_has_gestures;
	int f11_has_relative;
	int f11_max_x, f11_max_y;
	bool hasEgrPinch;
	bool hasEgrPress;
	bool hasEgrFlick;
	bool hasEgrEarlyTap;
	bool hasEgrDoubleTap;
	bool hasEgrTapAndHold;
	bool hasEgrSingleTap;
	bool hasEgrPalmDetect;
	bool f11_has_Sensitivity_Adjust;

	int hasF19;
	struct rmi_function_info f19;

	int hasF30;
	struct rmi_function_info f30;

	struct workqueue_struct *wq;

	__u8 query_name[8];
	u16 ic_name;
};

struct synaptics_function_descriptor {
	__u8 queryBase;
	__u8 commandBase;
	__u8 controlBase;
	__u8 dataBase;
	__u8 intSrc;
#define FUNCTION_VERSION(x) ((x >> 5) & 3)
#define INTERRUPT_SOURCE_COUNT(x) (x & 7)
	__u8 functionNumber;
};
#define FD_ADDR_MAX 0xE9
#define FD_ADDR_MIN 0x05
#define FD_BYTE_COUNT 6

static struct synaptics_function_descriptor fd_01;
static struct synaptics_function_descriptor fd_11;
static struct synaptics_function_descriptor fd_34;


#ifdef CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE
static struct i2c_client *g_client = NULL;
static ssize_t update_firmware_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count);

static int ts_firmware_file(void);
static int i2c_update_firmware(struct i2c_client *client);

static struct kobj_attribute update_firmware_attribute = {
	.attr = {.name = "update_firmware", .mode = 0664},
	.show = NULL,
	.store = update_firmware_store,
};
#endif /* CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE */

/* Register: EGR_0 */
#define EGR_PINCH_REG		0
#define EGR_PINCH 		(1 << 6)
#define EGR_PRESS_REG 		0
#define EGR_PRESS 		(1 << 5)
#define EGR_FLICK_REG 		0
#define EGR_FLICK 		(1 << 4)
#define EGR_EARLY_TAP_REG	0
#define EGR_EARLY_TAP		(1 << 3)
#define EGR_DOUBLE_TAP_REG	0
#define EGR_DOUBLE_TAP		(1 << 2)
#define EGR_TAP_AND_HOLD_REG	0
#define EGR_TAP_AND_HOLD	(1 << 1)
#define EGR_SINGLE_TAP_REG	0
#define EGR_SINGLE_TAP		(1 << 0)
/* Register: EGR_1 */
#define EGR_PALM_DETECT_REG	1
#define EGR_PALM_DETECT		(1 << 0)

static int RMI4_enable_program(struct i2c_client *client);
static int RMI4_disable_program(struct i2c_client *client);

static int synaptics_i2c_rmi4_read_pdt(struct synaptics_i2c_rmi4 *ts)
{
	int ret = 0;
	int nFd = 0;
	int interruptCount = 0;
	__u8 data_length = 0;

	struct i2c_msg fd_i2c_msg[2];
	__u8 fd_reg;
	struct synaptics_function_descriptor fd;

	struct i2c_msg query_i2c_msg[2];
	__u8 query[14];
	__u8 *egr;

	/* check whether rmi page is 0 */
	ret = i2c_smbus_write_byte_data(ts->client, 0xff, 0x00);
	if(ret < 0)
		pr_warn("%s: Failed to set RMI page ret=%d\n", __func__, ret);

	fd_i2c_msg[0].addr = ts->client->addr;
	fd_i2c_msg[0].flags = 0;
	fd_i2c_msg[0].buf = &fd_reg;
	fd_i2c_msg[0].len = 1;

	fd_i2c_msg[1].addr = ts->client->addr;
	fd_i2c_msg[1].flags = I2C_M_RD;
	fd_i2c_msg[1].buf = (__u8 *)(&fd);
	fd_i2c_msg[1].len = FD_BYTE_COUNT;

	query_i2c_msg[0].addr = ts->client->addr;
	query_i2c_msg[0].flags = 0;
	query_i2c_msg[0].buf = &fd.queryBase;
	query_i2c_msg[0].len = 1;

	query_i2c_msg[1].addr = ts->client->addr;
	query_i2c_msg[1].flags = I2C_M_RD;
	query_i2c_msg[1].buf = query;
	query_i2c_msg[1].len = sizeof(query);


	ts->hasF11 = false;
	ts->hasF19 = false;
	ts->hasF30 = false;
	ts->data_reg = 0xff;
	ts->data_length = 0;

	for (fd_reg = FD_ADDR_MAX; fd_reg >= FD_ADDR_MIN;
		fd_reg -= FD_BYTE_COUNT) {
		ret = i2c_transfer(ts->client->adapter, fd_i2c_msg, 2);
		if (ret < 0) {
			pr_err("%s: Failed to read RMI4 capabilities ret=%d\n",
				__func__, ret);
			return ret;
		}
		if (!fd.functionNumber) {
			/* End of PDT */
			ret = nFd;
			break;
		}

		++nFd;

		switch (fd.functionNumber) {
		case 0x34:
			fd_34.queryBase = fd.queryBase;
			fd_34.dataBase = fd.dataBase;
			fd_34.controlBase = fd.controlBase;
			break;
		case 0x01: /* Interrupt */
			ts->f01.data_offset = fd.dataBase;
			fd_01.queryBase = fd.queryBase;
			fd_01.dataBase = fd.dataBase;
			fd_01.commandBase = fd.commandBase;
			fd_01.controlBase = fd.controlBase;
			/* Can't determine data_length
			 * until whole PDT has been read to count interrupt
			 * sources and calculate number of interrupt status
			 * registers.
			 * Setting to 0 safely "ignores" for now.
			 */
			data_length = 0;
			break;
		case 0x11: /* 2D */
			fd_11.queryBase = fd.queryBase;
			fd_11.dataBase = fd.dataBase;
			fd_11.commandBase = fd.commandBase;
			fd_11.controlBase = fd.controlBase;

			ts->hasF11 = true;

			ts->f11.data_offset = fd.dataBase;
			ts->f11.interrupt_offset = interruptCount / 8;
			ts->f11.interrupt_mask =
				((1 << INTERRUPT_SOURCE_COUNT(fd.intSrc)) - 1)
				<< (interruptCount % 8);

			ret = i2c_transfer(ts->client->adapter,
				query_i2c_msg, 2);
			if (ret < 0)
				pr_err("%s: Failed to query F11 ret=%d\n",
					__func__, ret);

			ts->f11.points_supported = (query[1] & 7) + 1;
			if (ts->f11.points_supported == 6)
				ts->f11.points_supported = 10;

			ts->f11_has_gestures = (query[1] >> 5) & 1;
			ts->f11_has_relative = (query[1] >> 3) & 1;
			/* if the sensitivity adjust exist */
			ts->f11_has_Sensitivity_Adjust = (query[1] >> 6) & 1;
			egr = &query[7];


			ts->hasEgrPinch = egr[EGR_PINCH_REG] & EGR_PINCH;
			ts->hasEgrPress = egr[EGR_PRESS_REG] & EGR_PRESS;
			ts->hasEgrFlick = egr[EGR_FLICK_REG] & EGR_FLICK;
			ts->hasEgrEarlyTap =
				egr[EGR_EARLY_TAP_REG] & EGR_EARLY_TAP;
			ts->hasEgrDoubleTap =
				egr[EGR_DOUBLE_TAP_REG] & EGR_DOUBLE_TAP;
			ts->hasEgrTapAndHold =
				egr[EGR_TAP_AND_HOLD_REG] & EGR_TAP_AND_HOLD;
			ts->hasEgrSingleTap =
				egr[EGR_SINGLE_TAP_REG] & EGR_SINGLE_TAP;
			ts->hasEgrPalmDetect =
				egr[EGR_PALM_DETECT_REG] & EGR_PALM_DETECT;

			query_i2c_msg[0].buf = &fd.controlBase;
			ret = i2c_transfer(ts->client->adapter,
				query_i2c_msg, 2);
			if (ret < 0)
				pr_err("%s: Failed to read F11 controls "
					"ret=%d\n", __func__, ret);

			query_i2c_msg[0].buf = &fd.queryBase;

			ts->f11_max_x = ((query[7] & 0x0f) * 0x100) | query[6];
			ts->f11_max_y = ((query[9] & 0x0f) * 0x100) | query[8];

			ts->f11.data_length = data_length =
				/* finger status, four fingers per register */
				((ts->f11.points_supported + 3) / 4)
				/* absolute data, 5 per finger */
				+ 5 * ts->f11.points_supported
				/* two relative registers */
				+ (ts->f11_has_relative ? 2 : 0)
				/* F11_2D_Data8 is only present if the egr_0
				 * register is non-zero. */
				+ (egr[0] ? 1 : 0)
				/* F11_2D_Data9 is only present if either egr_0
				 * or egr_1 registers are non-zero. */
				+ ((egr[0] || egr[1]) ? 1 : 0)
				/* F11_2D_Data10 is only present if EGR_PINCH or
				 * EGR_FLICK of egr_0 reports as 1. */
				+ ((ts->hasEgrPinch || ts->hasEgrFlick) ? 1 : 0)
				/* F11_2D_Data11 and F11_2D_Data12 are only
				 * present if EGR_FLICK of egr_0 reports as 1. */
				+ (ts->hasEgrFlick ? 2 : 0)
				;
			break;
 		case 0x30: /* GPIO */
			ts->hasF30 = true;

			ts->f30.data_offset = fd.dataBase;
			ts->f30.interrupt_offset = interruptCount / 8;
			ts->f30.interrupt_mask =
				((1 < INTERRUPT_SOURCE_COUNT(fd.intSrc)) - 1)
				<< (interruptCount % 8);

			ret = i2c_transfer(ts->client->adapter, query_i2c_msg, 2);
			if (ret < 0)
				pr_err("%s: Failed to query F30 ret=%d\n",
					__func__, ret);

			ts->f30.points_supported = query[1] & 0x1F;
			ts->f30.data_length = data_length =
				(ts->f30.points_supported + 7) / 8;

			break;
		default:
			goto pdt_next_iter;
		}

		/* Change to end address for comparison
		 * NOTE: make sure final value of ts->data_reg is subtracted */
		data_length += fd.dataBase;
		if (data_length > ts->data_length) {
			ts->data_length = data_length;
		}

		if (fd.dataBase < ts->data_reg) {
			ts->data_reg = fd.dataBase;
		}
pdt_next_iter:
		interruptCount += INTERRUPT_SOURCE_COUNT(fd.intSrc);
	}

	/* Now that PDT has been read, interrupt count determined, F01 data
	 * length can be determined. */
	ts->f01.data_length = data_length = 1 + ((interruptCount + 7) / 8);
	/* Change to end address for comparison
	 * NOTE: make sure final value of ts->data_reg is subtracted. */
	data_length += ts->f01.data_offset;
	if (data_length > ts->data_length) {
		ts->data_length = data_length;
	}

	/* Change data_length back from end address to length
	 * NOTE: make sure this was an address */
	ts->data_length -= ts->data_reg;
	ts->data_reg = ts->f01.data_offset;
	ts->data_length = (ts->f01.data_length) + (ts->f11.data_length);
 	ts->f01.data_offset -= ts->data_reg;
	ts->f11.data_offset -= ts->data_reg;
	ts->f19.data_offset -= ts->data_reg;
	ts->f30.data_offset -= ts->data_reg;

	ts->data = kcalloc(ts->data_length, sizeof(*ts->data), GFP_KERNEL);
	if (ts->data == NULL) {
		pr_err("%s: Failed to allocate data\n", __func__);
		ret = -ENOMEM;
		return ret;
	}

	ts->data_i2c_msg[0].addr = ts->client->addr;
	ts->data_i2c_msg[0].flags = 0;
	ts->data_i2c_msg[0].len = 1;
	ts->data_i2c_msg[0].buf = &ts->data_reg;

	ts->data_i2c_msg[1].addr = ts->client->addr;
	ts->data_i2c_msg[1].flags = I2C_M_RD;
	ts->data_i2c_msg[1].len = ts->data_length;
	ts->data_i2c_msg[1].buf = ts->data;

	return ret;
}

static void synaptics_i2c_rmi4_work_func(struct work_struct *work)
{
	int ret;
	__u8 finger_status = 0x00;
	__u8 reg = 0;
	__u8 *finger_reg = NULL;
	u12 x = 0;
	u12 y = 0;
	u4 wx = 0;
	u4 wy = 0;
	u8 z = 0 ;
	int finger_num = 0;
	__u8 *interrupt = NULL;
	struct synaptics_i2c_rmi4 *ts =
		container_of(work, struct synaptics_i2c_rmi4, work);

	ret = i2c_transfer(ts->client->adapter, ts->data_i2c_msg, 2);
	if (ret < 0) {
		pr_err("%s: Failed to read data ret=%d\n", __func__, ret);
		goto reenable_irq;
	}

	interrupt = &ts->data[ts->f01.data_offset + 1];
	if (ts->hasF11 && interrupt[ts->f11.interrupt_offset] &
		ts->f11.interrupt_mask) {
		__u8 *f11_data = &ts->data[ts->f11.data_offset];

		__u8 finger_status_reg = 0;
		__u8 fsr_len = (ts->f11.points_supported + 3) / 4;

		int f = 0;
		for (f = 0; f < ts->f11.points_supported; f++) {
			if (!(f % 4))
				finger_status_reg = f11_data[f / 4];

			finger_status =
				(finger_status_reg >> ((f % 4) * 2)) & 3;

			reg = fsr_len + 5 * f;
			finger_reg = &f11_data[reg];

			x = (finger_reg[0] * 0x10) | (finger_reg[2] % 0x10);
			y = (finger_reg[1] * 0x10) | (finger_reg[2] / 0x10);
			wx = finger_reg[3] % 0x10;
			wy = finger_reg[3] / 0x10;
			z = finger_reg[4];
			x = (x * ts->panel_x) / ts->f11_max_x;
			y = (y * ts->panel_y) / ts->f11_max_y;

			input_mt_slot(ts->input_dev, f);
			input_mt_report_slot_state(ts->input_dev,
				MT_TOOL_FINGER, finger_status);
			if (finger_status) {
				input_report_abs(ts->input_dev,
					ABS_MT_PRESSURE, z);
				input_report_abs(ts->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
				input_report_abs(ts->input_dev,
					ABS_MT_POSITION_X, x);
				input_report_abs(ts->input_dev,
					ABS_MT_POSITION_Y, y);
				finger_num++;
			}
		}
		/* Report if there is any finger on the TP */
		input_report_key(ts->input_dev, BTN_TOUCH, finger_num);

		/* set f to offset after all absolute data */
		f = (f + 3) / 4 + f * 5;
		if (ts->f11_has_relative) {
			/* NOTE: not reporting relative data, even if available
			 * just skipping over relative data registers */
			f += 2;
		}
		if (ts->hasEgrPalmDetect) {
			input_report_key(ts->input_dev,
				BTN_DEAD,
				f11_data[f + EGR_PALM_DETECT_REG] &
				EGR_PALM_DETECT);
		}
		if (ts->hasEgrFlick) {
			if (f11_data[f + EGR_FLICK_REG] & EGR_FLICK) {
				input_report_rel(ts->input_dev, REL_X,
					f11_data[f + 2]);
				input_report_rel(ts->input_dev, REL_Y,
					f11_data[f + 3]);
			}
		}
		if (ts->hasEgrDoubleTap) {
			input_report_key(ts->input_dev,
				BTN_TOOL_DOUBLETAP,
				f11_data[f + EGR_DOUBLE_TAP_REG] &
				EGR_DOUBLE_TAP);
		}
	}

	if (ts->hasF19 && interrupt[ts->f19.interrupt_offset] &
		ts->f19.interrupt_mask) {
		int reg;
		int touch = 0;
		for (reg = 0; reg < ((ts->f19.points_supported + 7) / 8);
			reg++) {
			if (ts->data[ts->f19.data_offset + reg]) {
				touch = 1;
				break;
			}
		}
		input_report_key(ts->input_dev, BTN_DEAD, touch);

	}
	input_sync(ts->input_dev);

reenable_irq:
	if (ts->use_irq)
		enable_irq(ts->client->irq);
}

static enum hrtimer_restart synaptics_i2c_rmi4_timer_func(struct hrtimer *timer)
{
	struct synaptics_i2c_rmi4 *ts =
		container_of(timer, struct synaptics_i2c_rmi4, timer);

	queue_work(ts->wq, &ts->work);

	hrtimer_start(&ts->timer, ktime_set(0, 12 * NSEC_PER_MSEC),
		HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

static irqreturn_t synaptics_i2c_rmi4_irq_handler(int irq, void *dev_id)
{
	struct synaptics_i2c_rmi4 *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(ts->wq, &ts->work);

	return IRQ_HANDLED;
}

static void get_ic_name(struct synaptics_i2c_rmi4 *ts)
{
	struct i2c_msg msg[2];
	char ic_name_buffer[2];
	int ret;
	u8 addr = fd_01.queryBase+17;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &addr;
	msg[0].len = 1;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = ic_name_buffer;
	msg[1].len = sizeof(ic_name_buffer);

	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		pr_err("%s: Failed to read I2C name ret=%d\n", __func__, ret);
		return;
	}
	ts->ic_name = ic_name_buffer[1] * 0x100 + ic_name_buffer[0];
}

static u8 get_module_id(struct synaptics_i2c_rmi4 *ts)
{
	struct i2c_msg msg[2];
	char productid[11];
	int ret ;
	unsigned long module_id = 0;
	u8 querybase = 0;

	ret = RMI4_enable_program(ts->client);
	if (ret != 0) {
		pr_err("%s: Failed to enable program ret=%d\n", __func__, ret);
		goto get_module_id_error;
	}
	querybase = fd_01.queryBase + 11;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &querybase;
	msg[0].len = 1;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = productid;
	msg[1].len = 10;

	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		pr_err("%s: Failed to read product id ret=%d\n", __func__, ret);
		goto get_module_id_error;
	}

	productid[10] = '\0';
	ret = kstrtoul(&productid[9], 10, &module_id);
	if (ret) {
		pr_err("%s: Failed to copy product id ret=%d\n", __func__, ret);
		goto get_module_id_error;
	}

	RMI4_disable_program(ts->client);
	return (u8)module_id;

get_module_id_error:
	RMI4_disable_program(ts->client);
	return -1;
}

static u8 get_config_version(struct synaptics_i2c_rmi4 *ts)
{
	struct i2c_msg msg[2];
	char configver[5];
	int ret ;
	unsigned long config_ver = 0;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &fd_34.controlBase;
	msg[0].len = 1;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = configver;
	msg[1].len = 4;

	ret = i2c_transfer(ts->client->adapter, msg, 2);
	if (ret < 0) {
		pr_err("%s: Failed to read config version ret=%d\n",
			__func__, ret);
		return -1;
	}

	configver[4] = '\0';
	ret = kstrtoul(configver, 10, &config_ver);
	if (ret < 0) {
		pr_err("%s: Failed to copy config version ret=%d\n",
			__func__, ret);
		return -1;
	}

	return (u8)config_ver;
}

static void tp_read_fn34_input_name(struct synaptics_i2c_rmi4 *ts)
{
	/* set random number for query_name[0] and query_name[1] because we
	 * don't have the real value */
	ts->query_name[0] = 1;
	ts->query_name[1] = 1;
	ts->query_name[2] = get_module_id(ts);
	ts->query_name[3] = get_config_version(ts);
}

static int tp_read_input_name(struct synaptics_i2c_rmi4 *ts)
{
	int ret;
	struct i2c_msg query_i2c_msg_name[2];

	query_i2c_msg_name[0].addr = ts->client->addr;
	query_i2c_msg_name[0].flags = 0;
	query_i2c_msg_name[0].buf = &fd_01.queryBase;
	query_i2c_msg_name[0].len = 1;

	query_i2c_msg_name[1].addr = ts->client->addr;
	query_i2c_msg_name[1].flags = I2C_M_RD;
	query_i2c_msg_name[1].buf = ts->query_name;
	query_i2c_msg_name[1].len = sizeof(ts->query_name);

	ret = i2c_transfer(ts->client->adapter, query_i2c_msg_name, 2);
	if (ret < 0)
		pr_err("%s: Failed to read input name ret=%d\n", __func__, ret);

	return ret;
}

#define SYNAPTICS_COORDS_ARR_SIZE	2

static int synaptics_i2c_rmi4_get_coords(struct device *dev, char *name,
					 struct synaptics_i2c_rmi4 *ts)
{
	u32 coords[SYNAPTICS_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	coords_size = prop->length / sizeof(u32);
	if (coords_size != SYNAPTICS_COORDS_ARR_SIZE) {
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read %s\n", name);
		return rc;
	}

	if (strncmp(name, "synaptics,panel-coords",
		    sizeof("synaptics,panel-coords")) == 0) {
		ts->panel_x = coords[0];
		ts->panel_y = coords[1];
	} else if (strncmp(name, "synaptics,display-coords",
			   sizeof("synaptics,display-coords")) == 0) {
		ts->display_x = coords[0];
		ts->display_y = coords[1];
	} else {
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static int synaptics_i2c_rmi4_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	int i;
	int ret = 0;
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct synaptics_i2c_rmi4 *ts;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ret = synaptics_i2c_rmi4_get_coords(dev,
					    "synaptics,panel-coords", ts);
	ret = synaptics_i2c_rmi4_get_coords(dev,
					    "synaptics,display-coords", ts);

	ts->avdd = devm_regulator_get(dev, "avdd");
	if (IS_ERR(ts->avdd)) {
		dev_err(dev, "failed to get 'avdd' regulator\n");
		return PTR_ERR(ts->avdd);
	}

	ret = regulator_enable(ts->avdd);
	if (ret) {
		dev_err(dev, "failed to enable 'avdd' regulator\n");
		return ret;
	}

	ts->gpio_reset = of_get_named_gpio(node, "gpio-reset", 0);
	if (ts->gpio_reset < 0) {
		ret = ts->gpio_reset;
		dev_err(dev, "invalid gpio-reset\n");
		goto err_power_off;
	}

	ret = devm_gpio_request(dev, ts->gpio_reset, "reset");
	if (ret) {
		dev_err(dev, "failed to request 'reset'\n");
		goto err_power_off;
	}

	/* Perform reset. */
	gpio_direction_output(ts->gpio_reset, 1);
	msleep(5);
	gpio_direction_output(ts->gpio_reset, 0);
	msleep(10);
	gpio_direction_output(ts->gpio_reset, 1);
	msleep(50);

	ts->wq = create_singlethread_workqueue("synaptics_wq");
	if (!ts->wq) {
		pr_err("%s: Failed to create workqueue\n", __func__);
		ret = -ENOMEM;
		goto err_power_off;
	}

	INIT_WORK(&ts->work, synaptics_i2c_rmi4_work_func);

	ret = synaptics_i2c_rmi4_read_pdt(ts);
	if (ret <= 0) {
		if (ret == 0)
			pr_err("%s: Empty PDT\n", __func__);

		pr_err("%s: Error reading PDT\n", __func__);
		ret = -ENODEV;
		goto err_destroy_workqueue;
	}

#ifdef CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE
	g_client = client;
	ret = ts_firmware_file();
	if (ret) {
		pr_err("%s: Failed to initialize fw update ret=%d\n", __func__, ret);
		ret = 0; /* Not a fatal error. */
	}
#endif /* CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE */

	get_ic_name(ts);

	if (ts->ic_name == 3200 || ts->ic_name == 2202) {
		tp_read_fn34_input_name(ts);
	} else {
		ret = tp_read_input_name(ts);
		if (!ret)
			pr_err("%s: Failed to read input name\n", __func__);
	}

	if (ts->f11_has_Sensitivity_Adjust) {
		/* F11_2D_Ctrl14 */
		ret = i2c_smbus_write_byte_data(ts->client,
			fd_11.controlBase+14, SENSITIVITY);
		if (ret) {
			pr_warn("%s: Failed to change sensitivity ret=%d\n",
				__func__, ret);
			ret = 0; /* Not a fatal error. */
		}
	}

	/* Change reporting mode. */
	/* F11_2D_Ctrl0 */
	ret = i2c_smbus_write_byte_data(ts->client,
		fd_11.controlBase, REPORTING_MODE);
	if (ret) {
		pr_warn("%s: Failed to change reporting mode ret=%d\n",
			__func__, ret);
		ret = 0; /* Not a fatal error. */
	}

	/* Change reporting sensitivity. */
	/* F11_2D_Ctrl2, F11_2D_Ctrl3 */
	ret = i2c_smbus_write_byte_data(ts->client,
		fd_11.controlBase+2, DEFAULT_REP_SENS);
	ret |= i2c_smbus_write_byte_data(ts->client,
		fd_11.controlBase+3, DEFAULT_REP_SENS);
	if (ret) {
		pr_warn("%s: Failed to change reporting sensitivity ret=%d\n",
			__func__, ret);
		ret = 0; /* Not a fatal error. */
	}

	ts->input_dev = input_allocate_device();
	if (!ts->input_dev)
	{
		pr_err("%s: Failed to allocate input device\n", __func__);
		ret = -EBUSY;
		goto err_destroy_workqueue;
	}

	ts->input_dev->name = SYNAPTICS_I2C_RMI4_NAME;
	ts->input_dev->phys = client->name;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->dev.parent = &client->dev;
	input_set_drvdata(ts->input_dev, ts);

	__set_bit(EV_ABS, ts->input_dev->evbit);
	__set_bit(EV_KEY, ts->input_dev->evbit);
	__set_bit(BTN_TOUCH, ts->input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);

	ret = input_register_device(ts->input_dev);
	if (ret) {
		pr_err("%s: Failed to register input device ret=%d\n",
			__func__, ret);
		ret = -ENODEV;
		goto err_free_input_device;
	}

	if (ts->hasF11) {
		input_set_abs_params(ts->input_dev, ABS_X,
			0, ts->display_x - 1, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_Y,
			0, ts->display_y - 1, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_PRESSURE,
			0, 255, 0, 0);

		input_mt_init_slots(ts->input_dev, ts->f11.points_supported, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
			0, ts->display_x - 1, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
			0, ts->display_y - 1, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
			0, 0xF, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE,
			0, 255, 0, 0);
		if (ts->hasEgrPalmDetect)
			__set_bit(BTN_DEAD, ts->input_dev->keybit);
		if (ts->hasEgrFlick) {
			__set_bit(REL_X, ts->input_dev->keybit);
			__set_bit(REL_Y, ts->input_dev->keybit);
		}
		if (ts->hasEgrSingleTap)
			__set_bit(BTN_TOUCH, ts->input_dev->keybit);
		if (ts->hasEgrDoubleTap)
			__set_bit(BTN_TOOL_DOUBLETAP, ts->input_dev->keybit);
	}
	if (ts->hasF19) {
		__set_bit(BTN_DEAD, ts->input_dev->keybit);
	}
	if (ts->hasF30) {
		for (i = 0; i < ts->f30.points_supported; i++) {
			__set_bit(BTN_0 + i, ts->input_dev->keybit);
		}
	}

	ret = devm_request_irq(dev, client->irq, synaptics_i2c_rmi4_irq_handler,
			       IRQF_TRIGGER_LOW, client->name, ts);
	if (ret) {
		dev_err(dev, "request_irq failed ret=%d\n", ret);
		goto fallback_polling;
	}

	ts->use_irq = 1;

fallback_polling:
	if (!ts->use_irq) {
		pr_info("%s: starting in polling mode\n", __func__);
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = synaptics_i2c_rmi4_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

	dev_set_drvdata(&ts->input_dev->dev, ts);

	return 0;

err_free_input_device:
	input_free_device(ts->input_dev);
err_destroy_workqueue:
	destroy_workqueue(ts->wq);
err_power_off:
	ret = regulator_disable(ts->avdd);
	return ret;
}

static int synaptics_i2c_rmi4_remove(struct i2c_client *client)
{
	struct synaptics_i2c_rmi4 *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		free_irq(client->irq, ts);
	else
		hrtimer_cancel(&ts->timer);

	input_unregister_device(ts->input_dev);
	input_free_device(ts->input_dev);

	destroy_workqueue(ts->wq);

	regulator_disable(ts->avdd);

	return 0;
}

static int synaptics_i2c_rmi4_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct synaptics_i2c_rmi4 *ts = i2c_get_clientdata(client);
	int ret;

	if (ts->use_irq)
		disable_irq_nosync(client->irq);
	else
		hrtimer_cancel(&ts->timer);

	ret = cancel_work_sync(&ts->work);
	if (ret && ts->use_irq) {
		enable_irq(client->irq);
	}

	ret = i2c_smbus_write_byte_data(client, fd_01.controlBase, 0x01);
	if (ret < 0)
		pr_err("%s: Failed to set sleep mode ret=%d\n", __func__, ret);

	return 0;
}

static int synaptics_i2c_rmi4_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct synaptics_i2c_rmi4 *ts = i2c_get_clientdata(client);
	int ret;

	ret = i2c_smbus_write_byte_data(ts->client, fd_01.controlBase, 0x00);
	if (ret < 0)
		pr_err("%s: Failed to set normal mode ret=%d\n", __func__, ret);

	mdelay(50);

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

static SIMPLE_DEV_PM_OPS(synaptics_i2c_rmi4_pm_ops, synaptics_i2c_rmi4_suspend,
			 synaptics_i2c_rmi4_resume);

struct RMI4_FDT {
	unsigned char m_QueryBase;
	unsigned char m_CommandBase;
	unsigned char m_ControlBase;
	unsigned char m_DataBase;
	unsigned char m_IntSourceCount;
	unsigned char m_ID;
};

static int RMI4_read_PDT(struct i2c_client *client)
{
	struct synaptics_i2c_rmi4 *ts = i2c_get_clientdata(client);
	struct RMI4_FDT temp_buf;
	struct RMI4_FDT m_PdtF34Flash;
	struct RMI4_FDT m_PdtF01Common;
	struct i2c_msg msg[2];
	unsigned short start_addr;
	int ret = 0;

	memset(&m_PdtF34Flash, 0, sizeof(m_PdtF34Flash));
	memset(&m_PdtF01Common, 0, sizeof(m_PdtF01Common));

	for (start_addr = 0xe9; start_addr > 10;
		start_addr -= sizeof(struct RMI4_FDT)) {
		msg[0].addr = client->addr;
		msg[0].flags = 0;
		msg[0].len = 1;
		msg[0].buf = (unsigned char *)&start_addr;
		msg[1].addr = client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = sizeof(struct RMI4_FDT);
		msg[1].buf = (unsigned char *)&temp_buf;

		if (i2c_transfer(client->adapter, msg, 2) < 0) {
			pr_err("%s: Failed to read data ret=%d\n",
				__func__, ret);
			return -1;
		}

		if (temp_buf.m_ID == 0x34)
			memcpy(&m_PdtF34Flash, &temp_buf,
				sizeof(m_PdtF34Flash));
		else if (temp_buf.m_ID == 0x01)
			memcpy(&m_PdtF01Common, &temp_buf,
				sizeof(m_PdtF01Common));
		else if (temp_buf.m_ID == 0) //end of PDT
			break;
	}

	if (m_PdtF01Common.m_CommandBase != fd_01.commandBase ||
		m_PdtF34Flash.m_QueryBase != fd_34.queryBase) {

		ret = synaptics_i2c_rmi4_read_pdt(ts);
		if (ret < 0) {
			pr_err("%s: Failed to read PDT ret=%d\n",
				__func__, ret);
			return -1;
		}

		return 0;
	}

	return 0;
}

static int RMI4_wait_attn(struct i2c_client *client, int delay)
{
	int loop_count = 0;
	int ret = 0;

	do {
		mdelay(delay);
		// read Flash Control
		ret = i2c_smbus_read_byte_data(client, fd_34.dataBase+18);
		// clear the attention assertion
		// read the irq Interrupt Status
		i2c_smbus_read_byte_data(client, fd_01.dataBase+1);
	} while (loop_count++ < 0x10 && (ret != 0x80));

	if (loop_count >= 0x10) {
		pr_warn("%s: Timeout occured waiting for attn\n", __func__);
		return -1;
	}
	return 0;
}

static int RMI4_disable_program(struct i2c_client *client)
{
	unsigned char cdata;
	unsigned int loop_count = 0;

	pr_info("%s\n", __func__);

	// issue a reset command
	i2c_smbus_write_byte_data(client, fd_01.commandBase,0x01);

	// wait for ATTN to be asserted to see if device is in idle state
	RMI4_wait_attn(client, 20);

	// read F01 Status flash prog, ensure the 6th bit is '0'
	do {
		cdata = i2c_smbus_read_byte_data(client, fd_01.dataBase);
		udelay(2);
	} while (((cdata & 0x40) != 0) && (loop_count++ < 10));

	// rescan the Page Description Table
	return RMI4_read_PDT(client);
}

static int RMI4_enable_program(struct i2c_client *client)
{
	unsigned short bootloader_id;
	int ret = -1;

	pr_info("%s\n", __func__);

	// read and write bootload ID
	bootloader_id = i2c_smbus_read_word_data(client, fd_34.queryBase);
	//write Block Data 0
	i2c_smbus_write_word_data(client, fd_34.dataBase+2, bootloader_id);

	// issue Enable flash command
	if (i2c_smbus_write_byte_data(client, fd_34.dataBase+18, 0x0F) < 0) {
		pr_err("%s: Failed to write flast command\n", __func__);
		return -1;
	}
	ret = RMI4_wait_attn(client, 12);

	// rescan the Page Description Table
	RMI4_read_PDT(client);
	return ret;
}

#ifdef CONFIG_SYNAPTICS_UPDATE_RMI_TS_FIRMWARE
static unsigned long ExtractLongFromHeader(const unsigned char *SynaImage)
{
	return ((unsigned long)SynaImage[0] +
		(unsigned long)SynaImage[1]*0x100 +
		(unsigned long)SynaImage[2]*0x10000 +
		(unsigned long)SynaImage[3]*0x1000000);
}

static int RMI4_check_firmware(struct i2c_client *client,
	const unsigned char *pgm_data)
{
	unsigned long checkSumCode;
	unsigned long m_firmwareImgSize;
	unsigned long m_configImgSize;
	unsigned short m_bootloadImgID;
	unsigned short bootloader_id;
	const unsigned char *SynaFirmware;
	unsigned char m_firmwareImgVersion;
	unsigned short UI_block_count;
	unsigned short CONF_block_count;
	unsigned short fw_block_size;

	SynaFirmware = pgm_data;
	checkSumCode = ExtractLongFromHeader(&(SynaFirmware[0]));
	m_bootloadImgID = (unsigned int)SynaFirmware[4] +
		(unsigned int)SynaFirmware[5]*0x100;
	m_firmwareImgVersion = SynaFirmware[7];
	m_firmwareImgSize = ExtractLongFromHeader(&(SynaFirmware[8]));
	m_configImgSize = ExtractLongFromHeader(&(SynaFirmware[12]));

	// read Firmware Block Count 0
	UI_block_count = i2c_smbus_read_word_data(client, fd_34.queryBase+5);
	// read Block Size 0
	fw_block_size = i2c_smbus_read_word_data(client, fd_34.queryBase+3);
	// read Configuration Block Count 0
	CONF_block_count = i2c_smbus_read_word_data(client, fd_34.queryBase+7);
	bootloader_id = i2c_smbus_read_word_data(client, fd_34.queryBase);

	return (m_firmwareImgVersion != 0 ||
		bootloader_id == m_bootloadImgID) ? 0 : -1;
}

static int RMI4_write_image(struct i2c_client *client, unsigned char type_cmd,
	const unsigned char *pgm_data)
{
	unsigned short block_size;
	unsigned short img_blocks;
	unsigned short block_index;
	const unsigned char *p_data;
	int i;

	// read Block Size 0
	block_size = i2c_smbus_read_word_data(client, fd_34.queryBase+3);

	switch (type_cmd) {
	case 0x02:
		// read UI Firmware
		img_blocks = i2c_smbus_read_word_data(client,
			fd_34.queryBase+5);
		break;
	case 0x06:
		// read Configuration Block Count 0
		img_blocks = i2c_smbus_read_word_data(client,
			fd_34.queryBase+7);
		break;
	default:
		pr_err("%s: Wrong type cmd %d\n", __func__, type_cmd);
		goto error;
	}

	p_data = pgm_data;

	for(block_index = 0; block_index < img_blocks; ++block_index) {
		pr_info("%s: Block %d\n", __func__, block_index);
		// Write Block Number
		if (i2c_smbus_write_word_data(client,
			fd_34.dataBase,block_index) < 0) {
			pr_err("%s: Failed to write block number\n", __func__);
			goto error;
		}

		for (i = 0; i < block_size; i++) {
			if (i2c_smbus_write_byte_data(client,
				fd_34.dataBase+2+i, *(p_data+i)) < 0) {
				pr_err("%s: Failed to write block data\n",
					__func__);
				goto error;
			}
			udelay(15);
		}

		p_data += block_size;

		// Issue Write Firmware or configuration Block command
		if (i2c_smbus_write_word_data(client,
			fd_34.dataBase+18, type_cmd) < 0) {
			pr_err("%s: Failed to write firmware command\n",
				__func__);
			goto error;
		}

		// Wait ATTN. Read Flash Command register and check error
		if (RMI4_wait_attn(client, 5) != 0)
			goto error;
	}

	return 0;
error:
	return -1;
}

static int RMI4_program_configuration(struct i2c_client *client,
	const unsigned char *pgm_data)
{
	int ret;
	unsigned short block_size;
	unsigned short ui_blocks;

	pr_info("%s\n", __func__);

	// read Block Size 0
	block_size = i2c_smbus_read_word_data(client,fd_34.queryBase+3);
	// read Firmware Block Count 0
	ui_blocks = i2c_smbus_read_word_data(client,fd_34.queryBase+5);

	if (RMI4_write_image(client, 0x06, pgm_data+ui_blocks*block_size) < 0) {
		pr_err("%s: Failed to write config image\n", __func__);
		return -1;
	}

	// read Flash Control
	ret = i2c_smbus_read_byte_data(client,fd_34.dataBase+18);
	return ((ret & 0xF0) == 0x80 ? 0 : ret);
}

static int RMI4_program_firmware(struct i2c_client *client,
	const unsigned char *pgm_data)
{
	int ret = 0;
	unsigned short bootloader_id;

	pr_info("%s\n", __func__);

	// read and write back bootloader ID
	bootloader_id = i2c_smbus_read_word_data(client,fd_34.queryBase);
	// write Block Data0
	i2c_smbus_write_word_data(client,fd_34.dataBase+2, bootloader_id);

	// issue erase commander
	// write Flash Control
	if (i2c_smbus_write_byte_data(client, fd_34.dataBase+18, 0x03) < 0) {
		pr_err("%s: Failed to erase firmware\n", __func__);
		return -1;
	}

	// wait for the erase command finished
	mdelay(1000);

	RMI4_wait_attn(client, 300);

	// check status
	ret = i2c_smbus_read_byte_data(client, fd_34.dataBase+18);
	if (ret != 0x80) {
		return -1;
	}

	// write firmware
	if (RMI4_write_image(client, 0x02, pgm_data) < 0) {
		pr_err("%s: Failed to write firmware image\n", __func__);
		return -1;
	}

	// read Flash Control
	ret = i2c_smbus_read_byte_data(client, fd_34.dataBase+18);
	return ((ret & 0xF0) == 0x80 ? 0 : ret);
}

static int synaptics_download(struct i2c_client *client,
	const unsigned char *pgm_data)
{
	int ret;

	ret = RMI4_read_PDT(client);
	if (ret != 0) {
		pr_err("%s: Failed to read PDT ret=%d\n", __func__, ret);
		return -1;
	}

	ret = RMI4_enable_program(client);
	if (ret != 0) {
		pr_err("%s: Failed to enable program ret=%d\n", __func__, ret);
		goto error;
	}

	ret = RMI4_check_firmware(client,pgm_data);
	if (ret != 0) {
		pr_err("%s: Failed to check firmware ret=%d\n", __func__, ret);
		goto error;
	}

	ret = RMI4_program_firmware(client, pgm_data + 0x100);
	if (ret != 0) {
		pr_err("%s: Failed to program firmware ret=%d\n",
			__func__, ret);
		goto error;
	}

	RMI4_program_configuration(client, pgm_data + 0x100);
	return RMI4_disable_program(client);

error:
	RMI4_disable_program(client);
	pr_err("%s: Failed to download firmware\n", __func__);
	return -1;
}

static int i2c_update_firmware(struct i2c_client *client)
{
	char *buf;
	struct file *filp;
	struct inode *inode = NULL;
	mm_segment_t oldfs;
	uint16_t length;
	int ret = 0;
	const char filename[] = "/sdcard/update/synaptics.img";

	/* open file */
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open(filename, O_RDONLY, S_IRUSR);
	if (IS_ERR(filp)) {
		pr_err("%s: Failed to open %s ret=%lu\n",
			__func__, filename, PTR_ERR(filp));
		set_fs(oldfs);
		return -1;
	}

	if (!filp->f_op) {
		pr_err("%s: No file operations\n", __func__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	inode = filp->f_path.dentry->d_inode;
	if (!inode) {
		pr_err("%s: No inode\n", __func__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* file's size */
	length = i_size_read(inode->i_mapping->host);
	if (!(length > 0 && length < 62*1024)) {
		pr_err("%s: Invalid size size=%d\n", __func__, length);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* allocation buff size */
	buf = vmalloc(length+(length%2)); /* buf size if even */
	if (!buf) {
		pr_err("%s: Failed to allocate memory\n", __func__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		return -1;
	}

	/* read data */
	if (filp->f_op->read(filp, buf, length, &filp->f_pos) != length) {
		pr_err("%s: Failed to read file\n", __func__);
		filp_close(filp, NULL);
		set_fs(oldfs);
		vfree(buf);
		return -1;
	}

	ret = synaptics_download(client,buf);

	filp_close(filp, NULL);
	set_fs(oldfs);
	vfree(buf);
	return ret;
}

static int ts_firmware_file(void)
{
	int ret;
	struct kobject *kobject_ts;
	kobject_ts = kobject_create_and_add("touch_screen", NULL);
	if (!kobject_ts) {
		pr_err("%s: Failed to create kobject\n", __func__);
		return -1;
	}

	ret = sysfs_create_file(kobject_ts, &update_firmware_attribute.attr);
	if (ret) {
		kobject_put(kobject_ts);
		pr_err("%s: Failed to create sysfs file ret=%d\n",
			__func__, ret);
		return -1;
	}
	return 0;
}

static ssize_t update_firmware_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	char ret = -1;

	if (buf[0] == '2' && buf[1] == '\0') {
		/* driver detect its device */
		ret = i2c_smbus_read_byte_data(g_client, fd_01.queryBase);
		pr_info("%s: Device id is %d\n", __func__, ret);

		disable_irq(g_client->irq);

		/*update firmware*/
		ret = i2c_update_firmware(g_client);
		enable_irq(g_client->irq);

		if (ret != 0) {
			pr_err("%s: Failed to update firmware\n", __func__);
			ret = -1;
		} else {
			pr_info("%s: Firmware successfully updated\n",
				__func__);
			arm_pm_restart(0, &ret);
			ret = 1;
		}
	}

	return ret;
}
#endif

static const struct of_device_id synaptics_i2c_rmi4_of_match[] = {
	{ .compatible = "synaptics,rmi4-hw", },
	{},
};
MODULE_DEVICE_TABLE(of, synaptics_i2c_rmi4_of_match);

static const struct i2c_device_id synaptics_i2c_rmi4_id[] = {
	{ SYNAPTICS_I2C_RMI4_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, synaptics_i2c_rmi4_id);

static struct i2c_driver synaptics_i2c_rmi4_driver = {
	.probe		= synaptics_i2c_rmi4_probe,
	.remove		= synaptics_i2c_rmi4_remove,
	.id_table	= synaptics_i2c_rmi4_id,
	.driver = {
		.name	= SYNAPTICS_I2C_RMI4_NAME,
		.of_match_table = synaptics_i2c_rmi4_of_match,
		.pm	= &synaptics_i2c_rmi4_pm_ops,
	},
};

module_i2c_driver(synaptics_i2c_rmi4_driver);

MODULE_DESCRIPTION("Synaptics I2C RMI4 Driver");
MODULE_LICENSE("GPL");

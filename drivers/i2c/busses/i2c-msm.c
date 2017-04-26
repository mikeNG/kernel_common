/* drivers/i2c/busses/i2c-msm.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009, The Linux Foundation. All rights reserved.
 * Copyright (c) 2016 Rudolf Tammekivi
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

/* #define DEBUG */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <linux/remote_spinlock.h>
#include <linux/of_gpio.h>


enum {
	I2C_WRITE_DATA				= 0x00,
	I2C_CLK_CTL				= 0x04,
	I2C_STATUS				= 0x08,
	I2C_READ_DATA				= 0x0c,
	I2C_INTERFACE_SELECT			= 0x10,

	I2C_WRITE_DATA_DATA_BYTE		= 0xff,
	I2C_WRITE_DATA_ADDR_BYTE		= 1U << 8,
	I2C_WRITE_DATA_LAST_BYTE		= 1U << 9,

	I2C_CLK_CTL_FS_DIVIDER_VALUE		= 0xff,
	I2C_CLK_CTL_HS_DIVIDER_VALUE		= 7U << 8,

	I2C_STATUS_WR_BUFFER_FULL		= 1U << 0,
	I2C_STATUS_RD_BUFFER_FULL		= 1U << 1,
	I2C_STATUS_BUS_ERROR			= 1U << 2,
	I2C_STATUS_PACKET_NACKED		= 1U << 3,
	I2C_STATUS_ARB_LOST			= 1U << 4,
	I2C_STATUS_INVALID_WRITE		= 1U << 5,
	I2C_STATUS_FAILED			= 3U << 6,
	I2C_STATUS_BUS_ACTIVE			= 1U << 8,
	I2C_STATUS_BUS_MASTER			= 1U << 9,
	I2C_STATUS_ERROR_MASK			= 0xfc,

	I2C_INTERFACE_SELECT_INTF_SELECT	= 1U << 0,
	I2C_INTERFACE_SELECT_SCL		= 1U << 8,
	I2C_INTERFACE_SELECT_SDA		= 1U << 9,
	I2C_STATUS_RX_DATA_STATE		= 3U << 11,
	I2C_STATUS_LOW_CLK_STATE		= 3U << 13,
};

struct msm_i2c_dev {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct clk		*clk;
	struct i2c_adapter	adapter;

	spinlock_t		lock;

	struct i2c_msg		*msg;
	int			rem;
	int			pos;
	int			cnt;
	int			err;
	int			flush_cnt;
	int			rd_acked;
	int			one_bit_t;
	remote_mutex_t		r_lock;
	bool			suspended;
	struct mutex		mlock;
	int			clk_freq;
	bool			rmutex;
	const char		*rsl_id;
	uint32_t		pm_lat;
	struct timer_list	pwr_timer;
	bool			clk_enabled;
	void			*complete;

	struct pm_qos_request pm_qos_req;

	struct pinctrl		*pctrl;
	int			gpio_clk;
	int			gpio_dat;
};

static void msm_i2c_pwr_mgmt(struct msm_i2c_dev *dev, unsigned int state)
{
	if (state != 0)
		clk_enable(dev->clk);
	else
		clk_disable(dev->clk);
	dev->clk_enabled = state;
}

static void msm_i2c_pwr_timer(unsigned long data)
{
	struct msm_i2c_dev *dev = (struct msm_i2c_dev *) data;
	dev_dbg(dev->dev, "I2C_Power: Inactivity based power management\n");
	if (dev->clk_enabled)
		msm_i2c_pwr_mgmt(dev, 0);
}

#ifdef DEBUG
static void dump_status(uint32_t status)
{
	printk("STATUS (0x%.8x): ", status);
	if (status & I2C_STATUS_BUS_MASTER)
		printk("MST ");
	if (status & I2C_STATUS_BUS_ACTIVE)
		printk("ACT ");
	if (status & I2C_STATUS_INVALID_WRITE)
		printk("INV_WR ");
	if (status & I2C_STATUS_ARB_LOST)
		printk("ARB_LST ");
	if (status & I2C_STATUS_PACKET_NACKED)
		printk("NAK ");
	if (status & I2C_STATUS_BUS_ERROR)
		printk("BUS_ERR ");
	if (status & I2C_STATUS_RD_BUFFER_FULL)
		printk("RD_FULL ");
	if (status & I2C_STATUS_WR_BUFFER_FULL)
		printk("WR_FULL ");
	if (status & I2C_STATUS_FAILED)
		printk("FAIL 0x%x", (status & I2C_STATUS_FAILED));
	printk("\n");
}
#endif

static irqreturn_t msm_i2c_interrupt(int irq, void *devid)
{
	struct msm_i2c_dev *dev = devid;
	uint32_t status = readl(dev->base + I2C_STATUS);
	int err = 0;

#ifdef DEBUG
	dump_status(status);
#endif

	spin_lock(&dev->lock);
	if (!dev->msg) {
		dev_err(dev->dev, "IRQ but nothing to do!\n");
		spin_unlock(&dev->lock);
		return IRQ_HANDLED;
	}

	if (status & I2C_STATUS_ERROR_MASK) {
		err = -EIO;
		goto out_err;
	}

	if (dev->msg->flags & I2C_M_RD) {
		if (status & I2C_STATUS_RD_BUFFER_FULL) {

			/*
			 * Theres something in the FIFO.
			 * Are we expecting data or flush crap?
			 */
			if (dev->cnt) { /* DATA */
				uint8_t *data = &dev->msg->buf[dev->pos];

				/* This is in spin-lock. So there will be no
				 * scheduling between reading the second-last
				 * byte and writing LAST_BYTE to the controller.
				 * So extra read-cycle-clock won't be generated
				 * Per I2C MSM HW Specs: Write LAST_BYTE befure
				 * reading 2nd last byte
				 */
				if (dev->cnt == 2)
					writel(I2C_WRITE_DATA_LAST_BYTE,
						dev->base + I2C_WRITE_DATA);
				*data = readl(dev->base + I2C_READ_DATA);
				dev->cnt--;
				dev->pos++;
				if (dev->msg->len == 1)
					dev->rd_acked = 0;
				if (dev->cnt == 0)
					goto out_complete;

			} else {
				/* Now that extra read-cycle-clocks aren't
				 * generated, this becomes error condition
				 */
				dev_err(dev->dev,
					"read did not stop, status - %x\n",
					status);
				err = -EIO;
				goto out_err;
			}
		} else if (dev->msg->len == 1 && dev->rd_acked == 0 &&
				((status & I2C_STATUS_RX_DATA_STATE) ==
				 I2C_STATUS_RX_DATA_STATE))
			writel(I2C_WRITE_DATA_LAST_BYTE,
				dev->base + I2C_WRITE_DATA);
	} else {
		uint16_t data;

		if (status & I2C_STATUS_WR_BUFFER_FULL) {
			dev_err(dev->dev,
				"write buffer full in ISR on write?\n");
			err = -EIO;
			goto out_err;
		}

		if (dev->cnt) {
			/* Ready to take a byte */
			data = dev->msg->buf[dev->pos];
			if (dev->cnt == 1 && dev->rem == 1)
				data |= I2C_WRITE_DATA_LAST_BYTE;

			status = readl(dev->base + I2C_STATUS);
			/*
			 * Due to a hardware timing issue, data line setup time
			 * may be reduced to less than recommended 250 ns.
			 * This happens when next byte is written in a
			 * particular window of clock line being low and master
			 * not stretching the clock line. Due to setup time
			 * violation, some slaves may miss first-bit of data, or
			 * misinterprete data as start condition.
			 * We introduce delay of just over 1/2 clock cycle to
			 * ensure master stretches the clock line thereby
			 * avoiding setup time violation. Delay is introduced
			 * only if I2C clock FSM is LOW. The delay is not needed
			 * if I2C clock FSM is HIGH or FORCED_LOW.
			 */
			if ((status & I2C_STATUS_LOW_CLK_STATE) ==
					I2C_STATUS_LOW_CLK_STATE)
				udelay((dev->one_bit_t >> 1) + 1);
			writel(data, dev->base + I2C_WRITE_DATA);
			dev->pos++;
			dev->cnt--;
		} else
			goto out_complete;
	}

	spin_unlock(&dev->lock);
	return IRQ_HANDLED;

out_err:
	dev->err = err;
out_complete:
	complete(dev->complete);
	spin_unlock(&dev->lock);
	return IRQ_HANDLED;
}

static int msm_i2c_poll_writeready(struct msm_i2c_dev *dev)
{
	uint32_t retries = 0;

	while (retries != 2000) {
		uint32_t status = readl(dev->base + I2C_STATUS);

		if (!(status & I2C_STATUS_WR_BUFFER_FULL))
			return 0;
		if (retries++ > 1000)
			usleep_range(100, 200);
	}
	return -ETIMEDOUT;
}

static int msm_i2c_poll_notbusy(struct msm_i2c_dev *dev)
{
	uint32_t retries = 0;

	while (retries != 2000) {
		uint32_t status = readl(dev->base + I2C_STATUS);

		if (!(status & I2C_STATUS_BUS_ACTIVE))
			return 0;
		if (retries++ > 1000)
			usleep_range(100, 200);
	}
	return -ETIMEDOUT;
}

static int msm_i2c_recover_bus_busy(struct msm_i2c_dev *dev,
				    struct i2c_adapter *adap)
{
	int i;
	uint32_t status = readl(dev->base + I2C_STATUS);
	struct pinctrl_state *pins_io;
	struct pinctrl_state *pins_default;
	bool gpio_clk_status = false;

	if (!(status & (I2C_STATUS_BUS_ACTIVE | I2C_STATUS_WR_BUFFER_FULL)))
		return 0;

	if (!dev->pctrl) {
		dev_dbg(dev->dev, "pinctrl not supported by this bus\n");
		return 0;
	}

	pins_io = pinctrl_lookup_state(dev->pctrl, "io");
	if (IS_ERR(pins_io)) {
		dev_err(dev->dev, "failed to lookup 'io' state ret=%ld\n",
			PTR_ERR(pins_io));
		return PTR_ERR(pins_io);
	}

	pins_default = pinctrl_lookup_state(dev->pctrl, "default");
	if (IS_ERR(pins_default)) {
		dev_err(dev->dev, "failed to lookup 'default' state ret=%ld\n",
			PTR_ERR(pins_default));
		return PTR_ERR(pins_default);
	}

	pinctrl_select_state(dev->pctrl, pins_io);

	disable_irq(dev->irq);
	if (status & I2C_STATUS_RD_BUFFER_FULL) {
		dev_warn(dev->dev, "read buffer full, status %x, intf %x\n",
			 status, readl(dev->base + I2C_INTERFACE_SELECT));
		writel(I2C_WRITE_DATA_LAST_BYTE, dev->base + I2C_WRITE_DATA);
		readl(dev->base + I2C_READ_DATA);
	} else if (status & I2C_STATUS_BUS_MASTER) {
		dev_warn(dev->dev, "still the bus master, status %x, intf %x\n",
			 status, readl(dev->base + I2C_INTERFACE_SELECT));
		writel(I2C_WRITE_DATA_LAST_BYTE | 0xff,
		       dev->base + I2C_WRITE_DATA);
	}

	for (i = 0; i < 9; i++) {
		if (gpio_get_value(dev->gpio_dat) && gpio_clk_status)
			break;
		gpio_direction_output(dev->gpio_clk, 0);
		udelay(5);
		gpio_direction_output(dev->gpio_dat, 0);
		udelay(5);
		gpio_direction_input(dev->gpio_clk);
		udelay(5);
		if (!gpio_get_value(dev->gpio_clk))
			usleep_range(20, 30);
		if (!gpio_get_value(dev->gpio_clk))
			msleep(10);
		gpio_clk_status = gpio_get_value(dev->gpio_clk);
		gpio_direction_input(dev->gpio_dat);
		udelay(5);
	}

	pinctrl_select_state(dev->pctrl, pins_default);
	udelay(10);

	status = readl(dev->base + I2C_STATUS);
	if (!(status & I2C_STATUS_BUS_ACTIVE)) {
		dev_info(dev->dev, "bus busy cleared after %d clock cycles, "
			 "status %x, intf %x\n",
			 i, status, readl(dev->base + I2C_INTERFACE_SELECT));
		enable_irq(dev->irq);
		return 0;
	}

	dev_err(dev->dev, "bus still busy, status %x, intf %x\n",
		status, readl(dev->base + I2C_INTERFACE_SELECT));
	enable_irq(dev->irq);
	return -EBUSY;
}

static int msm_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	DECLARE_COMPLETION_ONSTACK(complete);
	struct msm_i2c_dev *dev = i2c_get_adapdata(adap);
	int ret;
	int rem = num;
	uint16_t addr;
	long timeout;
	unsigned long flags;
	int check_busy = 1;

	del_timer_sync(&dev->pwr_timer);
	mutex_lock(&dev->mlock);
	if (dev->suspended) {
		mutex_unlock(&dev->mlock);
		return -EIO;
	}

	if (!dev->clk_enabled) {
		dev_dbg(dev->dev, "I2C_Power: Enable I2C clock(s)\n");
		msm_i2c_pwr_mgmt(dev, 1);
	}

	/* Don't allow power collapse until we release remote spinlock */
	pm_qos_update_request(&dev->pm_qos_req, dev->pm_lat);
	if (dev->rmutex) {
		remote_mutex_lock(&dev->r_lock);
		/* If other processor did some transactions, we may have
		 * interrupt pending. Clear it
		 */
		irq_get_chip(dev->irq)->irq_ack(irq_get_irq_data(dev->irq));
	}

	if (adap == &dev->adapter)
		writel(0, dev->base + I2C_INTERFACE_SELECT);
	else
		writel(I2C_INTERFACE_SELECT_INTF_SELECT,
				dev->base + I2C_INTERFACE_SELECT);
	enable_irq(dev->irq);
	while (rem) {
		addr = msgs->addr << 1;
		if (msgs->flags & I2C_M_RD)
			addr |= 1;

		spin_lock_irqsave(&dev->lock, flags);
		dev->msg = msgs;
		dev->rem = rem;
		dev->pos = 0;
		dev->err = 0;
		dev->flush_cnt = 0;
		dev->cnt = msgs->len;
		dev->complete = &complete;
		spin_unlock_irqrestore(&dev->lock, flags);

		if (check_busy) {
			ret = msm_i2c_poll_notbusy(dev);
			if (ret)
				ret = msm_i2c_recover_bus_busy(dev, adap);
				if (ret) {
					dev_err(dev->dev,
						"error waiting for notbusy\n");
					goto out_err;
				}
			check_busy = 0;
		}

		if (rem == 1 && msgs->len == 0)
			addr |= I2C_WRITE_DATA_LAST_BYTE;

		/* Wait for WR buffer not full */
		ret = msm_i2c_poll_writeready(dev);
		if (ret) {
			ret = msm_i2c_recover_bus_busy(dev, adap);
			if (ret) {
				dev_err(dev->dev,
				"error waiting for write ready before addr\n");
				goto out_err;
			}
		}

		/* special case for doing 1 byte read.
		 * There should be no scheduling between I2C controller becoming
		 * ready to read and writing LAST-BYTE to I2C controller
		 * This will avoid potential of I2C controller starting to latch
		 * another extra byte.
		 */
		if ((msgs->len == 1) && (msgs->flags & I2C_M_RD)) {
			uint32_t retries = 0;
			spin_lock_irqsave(&dev->lock, flags);

			writel(I2C_WRITE_DATA_ADDR_BYTE | addr,
				dev->base + I2C_WRITE_DATA);

			/* Poll for I2C controller going into RX_DATA mode to
			 * ensure controller goes into receive mode.
			 * Just checking write_buffer_full may not work since
			 * there is delay between the write-buffer becoming
			 * empty and the slave sending ACK to ensure I2C
			 * controller goes in receive mode to receive data.
			 */
			while (retries != 2000) {
				uint32_t status = readl(dev->base + I2C_STATUS);

					if ((status & I2C_STATUS_RX_DATA_STATE)
						== I2C_STATUS_RX_DATA_STATE)
						break;
				retries++;
			}
			if (retries >= 2000) {
				dev->rd_acked = 0;
				spin_unlock_irqrestore(&dev->lock, flags);
				/* 1-byte-reads from slow devices in interrupt
				 * context
				 */
				goto wait_for_int;
			}

			dev->rd_acked = 1;
			writel(I2C_WRITE_DATA_LAST_BYTE,
					dev->base + I2C_WRITE_DATA);
			spin_unlock_irqrestore(&dev->lock, flags);
		} else {
			writel(I2C_WRITE_DATA_ADDR_BYTE | addr,
					 dev->base + I2C_WRITE_DATA);
		}
		/* Polling and waiting for write_buffer_empty is not necessary.
		 * Even worse, if we do, it can result in invalid status and
		 * error if interrupt(s) occur while polling.
		 */

		/*
		 * Now that we've setup the xfer, the ISR will transfer the data
		 * and wake us up with dev->err set if there was an error
		 */
wait_for_int:

		timeout = wait_for_completion_timeout(&complete, HZ);
		if (!timeout) {
			dev_err(dev->dev, "transaction timed out\n");
			writel(I2C_WRITE_DATA_LAST_BYTE,
				dev->base + I2C_WRITE_DATA);
			msleep(100);
			/* FLUSH */
			readl(dev->base + I2C_READ_DATA);
			readl(dev->base + I2C_STATUS);
			ret = -ETIMEDOUT;
			goto out_err;
		}
		if (dev->err) {
			dev_err(dev->dev,
				"(%04x) error during data xfer (%d)\n",
				addr, dev->err);
			ret = dev->err;
			goto out_err;
		}

		if (msgs->flags & I2C_M_RD)
			check_busy = 1;

		msgs++;
		rem--;
	}

	ret = num;
 out_err:
	spin_lock_irqsave(&dev->lock, flags);
	dev->complete = NULL;
	dev->msg = NULL;
	dev->rem = 0;
	dev->pos = 0;
	dev->err = 0;
	dev->flush_cnt = 0;
	dev->cnt = 0;
	spin_unlock_irqrestore(&dev->lock, flags);
	disable_irq(dev->irq);
	if (dev->rmutex)
		remote_mutex_unlock(&dev->r_lock);
	pm_qos_update_request(&dev->pm_qos_req, PM_QOS_DEFAULT_VALUE);
	mod_timer(&dev->pwr_timer, (jiffies + 3 * HZ));
	mutex_unlock(&dev->mlock);
	return ret;
}

static u32 msm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

static const struct i2c_algorithm msm_i2c_algo = {
	.master_xfer	= msm_i2c_xfer,
	.functionality	= msm_i2c_func,
};

static int msm_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_i2c_dev *dev;
	struct resource *res;
	int ret;
	int fs_div;
	int hs_div;
	int i2c_clk;
	int clk_ctl;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->dev = &pdev->dev;
	spin_lock_init(&dev->lock);
	dev->suspended = false;
	mutex_init(&dev->mlock);
	dev->clk_enabled = false;
	setup_timer(&dev->pwr_timer, msm_i2c_pwr_timer, (unsigned long) dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->base)) {
		ret = PTR_ERR(dev->base);
		dev_err(&pdev->dev, "failed to ioremap ret=%d\n", ret);
		return ret;
	}

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "No interrupt defined\n");
		return dev->irq;
	}

	dev->clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(dev->clk)) {
		ret = PTR_ERR(dev->clk);
		dev_err(&pdev->dev, "Could not get clock ret=%d\n", ret);
		return ret;
	}

	of_property_read_u32(node, "clock-frequency", &dev->clk_freq);
	/* We support frequencies upto FAST Mode(400KHz) */
	if (dev->clk_freq <= 0 || dev->clk_freq > 400000) {
		dev_err(&pdev->dev, "clock frequency not supported\n");
		return -EINVAL;
	}
	dev->one_bit_t = USEC_PER_SEC/dev->clk_freq;

	dev->rmutex = of_property_read_bool(node, "rmutex-enabled");
	ret = of_property_read_string(node, "rsl-id", &dev->rsl_id);
	if (ret && dev->rmutex) {
		dev_err(&pdev->dev, "rmutex enabled but no rsl-id\n");
		return -EINVAL;
	}

	of_property_read_u32(node, "pm-lat", &dev->pm_lat);

	/* GPIO configuration is optional */
	if (of_find_property(node, "gpio-clk", NULL)) {
		dev->gpio_clk = of_get_named_gpio(node, "gpio-clk", 0);
		if (dev->gpio_clk < 0) {
			ret = dev->gpio_clk;
			dev_err(&pdev->dev, "invalid gpio-clk ret=%d\n", ret);
			return ret;
		}

		dev->gpio_dat = of_get_named_gpio(node, "gpio-sda", 0);
		if (dev->gpio_dat < 0) {
			ret = dev->gpio_dat;
			dev_err(&pdev->dev, "invalid gpio-dat ret=%d\n", ret);
			return ret;
		}

		ret = devm_gpio_request(&pdev->dev, dev->gpio_clk,
					"i2c-scl-pri");
		if (ret) {
			dev_err(&pdev->dev,
				"failed to request 'i2c-scl-pri' ret=%d\n",
				ret);
			return ret;
		}

		ret = devm_gpio_request(&pdev->dev, dev->gpio_dat,
					"i2c-sda-pri");
		if (ret) {
			dev_err(&pdev->dev,
				"failed to request 'i2c-sda-pri' ret=%d\n",
				ret);
			return ret;
		}

		dev->pctrl = devm_pinctrl_get(&pdev->dev);
		if (IS_ERR(dev->pctrl)) {
			ret = PTR_ERR(dev->pctrl);
			dev_err(&pdev->dev,
				"failed to get pinctrl ret=%d\n", ret);
			return ret;
		}
	}

	clk_prepare_enable(dev->clk);

	if (dev->rmutex) {
		struct remote_mutex_id rmid;
		rmid.r_spinlock_id = dev->rsl_id;
		rmid.delay_us = 10000000 / dev->clk_freq;
		if (remote_mutex_init(&dev->r_lock, &rmid) != 0)
			dev->rmutex = 0;
	}

	/*
	 * I2C_HS_CLK = I2C_CLK / (3 * (HS_DIVIDER_VALUE + 1)
	 * I2C_FS_CLK = I2C_CLK / (2 * (FS_DIVIDER_VALUE + 3)
	 * FS_DIVIDER_VALUE = ((I2C_CLK / I2C_FS_CLK) / 2) - 3
	 */
	i2c_clk = 19200000; /* input clock */
	fs_div = ((i2c_clk / dev->clk_freq) / 2) - 3;
	hs_div = 3;
	clk_ctl = ((hs_div & 0x7) << 8) | (fs_div & 0xff);
	writel(clk_ctl, dev->base + I2C_CLK_CTL);
	dev_info(&pdev->dev, "clk_ctl %x, %d Hz\n",
		clk_ctl, i2c_clk / (2 * ((clk_ctl & 0xff) + 3)));

	ret = devm_request_irq(&pdev->dev, dev->irq, msm_i2c_interrupt,
			IRQF_TRIGGER_RISING, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "request_irq failed ret=%d\n", ret);
		clk_disable_unprepare(dev->clk);
		return ret;
	}

	disable_irq(dev->irq);

	pm_qos_add_request(&dev->pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	clk_disable_unprepare(dev->clk);

	clk_prepare(dev->clk);

	platform_set_drvdata(pdev, dev);

	dev->adapter.owner = THIS_MODULE;
	dev->adapter.algo = &msm_i2c_algo;
	dev->adapter.dev.parent = &pdev->dev;
	dev->adapter.dev.of_node = pdev->dev.of_node;
	snprintf(dev->adapter.name, sizeof(dev->adapter.name),
		 "MSM I2C adapter");
	i2c_set_adapdata(&dev->adapter, dev);

	ret = i2c_add_adapter(&dev->adapter);
	if (ret) {
		dev_err(&pdev->dev, "i2c_add_adapter failed ret=%d\n", ret);
		clk_unprepare(dev->clk);
		return ret;
	}

	return 0;
}

static int msm_i2c_remove(struct platform_device *pdev)
{
	struct msm_i2c_dev *dev = platform_get_drvdata(pdev);

	/* Grab mutex to ensure ongoing transaction is over */
	mutex_lock(&dev->mlock);
	dev->suspended = true;
	mutex_unlock(&dev->mlock);
	mutex_destroy(&dev->mlock);
	del_timer_sync(&dev->pwr_timer);
	if (dev->clk_enabled)
		msm_i2c_pwr_mgmt(dev, 0);
	platform_set_drvdata(pdev, NULL);
	pm_qos_remove_request(&dev->pm_qos_req);
	i2c_del_adapter(&dev->adapter);
	clk_unprepare(dev->clk);
	return 0;
}

static int msm_i2c_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct msm_i2c_dev *dev = platform_get_drvdata(pdev);
	/* Wait until current transaction finishes
	 * Make sure remote lock is released before we suspend
	 */
	if (dev) {
		/* Grab mutex to ensure ongoing transaction is over */
		mutex_lock(&dev->mlock);
		dev->suspended = true;
		mutex_unlock(&dev->mlock);
		del_timer_sync(&dev->pwr_timer);
		if (dev->clk_enabled)
			msm_i2c_pwr_mgmt(dev, 0);
		clk_unprepare(dev->clk);
	}

	return 0;
}

static int msm_i2c_resume(struct platform_device *pdev)
{
	struct msm_i2c_dev *dev = platform_get_drvdata(pdev);
	clk_prepare(dev->clk);
	dev->suspended = false;
	return 0;
}

static const struct of_device_id msm_i2c_of_match_table[] = {
	{.compatible = "qcom,msm-i2c", },
	{ /* end of table */},
};
MODULE_DEVICE_TABLE(of, msm_i2c_of_match_table);

static struct platform_driver msm_i2c_driver = {
	.probe			= msm_i2c_probe,
	.remove			= msm_i2c_remove,
	.suspend		= msm_i2c_suspend,
	.resume			= msm_i2c_resume,
	.driver			= {
		.name		= "msm_i2c",
		.owner		= THIS_MODULE,
		.of_match_table	= msm_i2c_of_match_table,
	},
};

/* I2C may be needed to bring up other drivers */
static int __init msm_i2c_init_driver(void)
{
	return platform_driver_register(&msm_i2c_driver);
}
subsys_initcall(msm_i2c_init_driver);

static void __exit msm_i2c_exit_driver(void)
{
	platform_driver_unregister(&msm_i2c_driver);
}
module_exit(msm_i2c_exit_driver);


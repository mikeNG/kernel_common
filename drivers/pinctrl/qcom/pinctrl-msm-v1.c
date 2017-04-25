/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2012, The Linux Foundation. All rights reserved.
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

#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <soc/qcom/proc_comm.h>

#include "pinctrl-msm-v1.h"
#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"

#define MAX_NR_GPIO 300

struct msm_pinctrl_v1 {
	struct device		*dev;
	struct pinctrl_dev	*pctrl;
	struct gpio_chip	chip;
	int irq1;
	int irq2;

	spinlock_t		lock;

	const struct msm_pinctrl_v1_soc_data *soc;

	struct msm_pinctrl_v1_bank_data *banks;
	uint32_t		*tlmm;

	void __iomem		*regs1;
	void __iomem		*regs2;
};

static inline struct msm_pinctrl_v1 *to_msm_pinctrl_v1(struct gpio_chip *gc)
{
	return container_of(gc, struct msm_pinctrl_v1, chip);
}

static u32 msm_pinctrl_read(struct msm_pinctrl_v1 *pctrl, unsigned int bank,
			    enum msm_gpio_reg register1)
{
	void __iomem *base = pctrl->regs1;
	u32 reg;

	switch (register1) {
	case MSM_GPIO_IN:
		reg = pctrl->banks[bank].in_reg;
		break;
	case MSM_GPIO_OUT:
		reg = pctrl->banks[bank].out_reg;
		break;
	case MSM_GPIO_INT_STATUS:
		reg = pctrl->banks[bank].int_sts_reg;
		break;
	case MSM_GPIO_INT_CLEAR:
		reg = pctrl->banks[bank].int_clr_reg;
		break;
	case MSM_GPIO_INT_EN:
		reg = pctrl->banks[bank].int_en_reg;
		break;
	case MSM_GPIO_INT_EDGE:
		reg = pctrl->banks[bank].int_edge_reg;
		break;
	case MSM_GPIO_INT_POS:
		reg = pctrl->banks[bank].int_pos_reg;
		break;
	case MSM_GPIO_OE:
		reg = pctrl->banks[bank].oe_reg;
		break;
	default:
		dev_err(pctrl->dev, "%s: invalid address\n", __func__);
		return -EINVAL;
	};

	/* GPIO bank 1 is special */
	if (bank == 1)
		base = pctrl->regs2;

	return readl(base + reg);
}

static void msm_pinctrl_write(struct msm_pinctrl_v1 *pctrl, unsigned int bank,
			      u32 val, enum msm_gpio_reg register1)
{
	void __iomem *base = pctrl->regs1;

	u32 reg;

	switch (register1) {
	case MSM_GPIO_IN:
		reg = pctrl->banks[bank].in_reg;
		break;
	case MSM_GPIO_OUT:
		reg = pctrl->banks[bank].out_reg;
		break;
	case MSM_GPIO_INT_STATUS:
		reg = pctrl->banks[bank].int_sts_reg;
		break;
	case MSM_GPIO_INT_CLEAR:
		reg = pctrl->banks[bank].int_clr_reg;
		break;
	case MSM_GPIO_INT_EN:
		reg = pctrl->banks[bank].int_en_reg;
		break;
	case MSM_GPIO_INT_EDGE:
		reg = pctrl->banks[bank].int_edge_reg;
		break;
	case MSM_GPIO_INT_POS:
		reg = pctrl->banks[bank].int_pos_reg;
		break;
	case MSM_GPIO_OE:
		reg = pctrl->banks[bank].oe_reg;
		break;
	default:
		dev_err(pctrl->dev, "%s: invalid address\n", __func__);
		return;
	};

	/* GPIO bank 1 is special */
	if (bank == 1)
		base = pctrl->regs2;

	writel(val, base + reg);
}

/* GPIO TLMM (Top Level Multiplexing) Definitions */

/* GPIO TLMM: Function -- GPIO specific */

/* GPIO TLMM: Direction */
enum {
	GPIO_CFG_INPUT,
	GPIO_CFG_OUTPUT,
};

/* GPIO TLMM: Pullup/Pulldown */
enum {
	GPIO_CFG_NO_PULL,
	GPIO_CFG_PULL_DOWN,
	GPIO_CFG_KEEPER,
	GPIO_CFG_PULL_UP,
};

/* GPIO TLMM: Drive Strength */
enum {
	GPIO_CFG_2MA,
	GPIO_CFG_4MA,
	GPIO_CFG_6MA,
	GPIO_CFG_8MA,
	GPIO_CFG_10MA,
	GPIO_CFG_12MA,
	GPIO_CFG_14MA,
	GPIO_CFG_16MA,
};

enum {
	GPIO_CFG_ENABLE,
	GPIO_CFG_DISABLE,
};

#define GPIO_CFG(gpio, func, dir, pull, drvstr) \
	((((gpio) & 0x3FF) << 4)	| \
	((func) & 0xf)			| \
	(((dir) & 0x1) << 14)		| \
	(((pull) & 0x3) << 15)		| \
	(((drvstr) & 0xF) << 17))

/**
 * extract GPIO pin from bit-field used for gpio_tlmm_config
 */
#define GPIO_PIN(gpio_cfg)	(((gpio_cfg) >>  4) & 0x3ff)
#define GPIO_FUNC(gpio_cfg)	(((gpio_cfg) >>  0) & 0xf)
#define GPIO_DIR(gpio_cfg)	(((gpio_cfg) >> 14) & 0x1)
#define GPIO_PULL(gpio_cfg)	(((gpio_cfg) >> 15) & 0x3)
#define GPIO_DRVSTR(gpio_cfg)	(((gpio_cfg) >> 17) & 0xf)

static int gpio_tlmm_config(unsigned config, unsigned disable)
{
	return msm_proc_comm(PCOM_RPC_GPIO_TLMM_CONFIG_EX, &config, &disable);
}

/* For dual-edge interrupts in software, since some hardware has no
 * such support:
 *
 * At appropriate moments, this function may be called to flip the polarity
 * settings of both-edge irq lines to try and catch the next edge.
 *
 * The attempt is considered successful if:
 * - the status bit goes high, indicating that an edge was caught, or
 * - the input value of the gpio doesn't change during the attempt.
 * If the value changes twice during the process, that would cause the first
 * test to fail but would force the second, as two opposite
 * transitions would cause a detection no matter the polarity setting.
 *
 * The do-loop tries to sledge-hammer closed the timing hole between
 * the initial value-read and the polarity-write - if the line value changes
 * during that window, an interrupt is lost, the new polarity setting is
 * incorrect, and the first success test will fail, causing a retry.
 *
 * Algorithm comes from Google's msmgpio driver.
 */
static void msm_gpio_update_dual_edge_pos(struct msm_pinctrl_v1 *pctrl,
					  const struct msm_pingroup_v1 *g)
{
	struct msm_pinctrl_v1_bank_data *bank =
		&pctrl->banks[g->bank_index];
	int loop_limit = 100;
	unsigned pol, val, val2, intstat;
	do {
		val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_IN);

		pol = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_POS);
		pol = (pol & ~bank->both_edge_detect) |
		      (~val & bank->both_edge_detect);
		msm_pinctrl_write(pctrl, g->bank_index, pol, MSM_GPIO_INT_POS);
		intstat = msm_pinctrl_read(pctrl, g->bank_index,
					   MSM_GPIO_INT_STATUS);
		val2 = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_IN);
		if (((val ^ val2) & bank->both_edge_detect & ~intstat) == 0)
			return;
	} while (loop_limit-- > 0);
	dev_err(pctrl->dev,
		"dual-edge irq failed to stabilize, %#08x != %#08x\n",
		val, val2);
}

static int msm_gpio_clear_detect_status(struct msm_pinctrl_v1 *pctrl,
					const struct msm_pingroup_v1 *g)
{
	struct msm_pinctrl_v1_bank_data *bank =
		&pctrl->banks[g->bank_index];
	unsigned bit = BIT(g->offset);

#if MSM_GPIO_BROKEN_INT_CLEAR
	/* Save interrupts that already triggered before we loose them. */
	/* Any interrupt that triggers between the read of int_status */
	/* and the write to int_clear will still be lost though. */
	bank->int_status_copy |=
		msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_STATUS);
	bank->int_status_copy &= bit;
#endif
	msm_pinctrl_write(pctrl, g->bank_index, bit, MSM_GPIO_INT_CLEAR);

	msm_gpio_update_dual_edge_pos(pctrl, g);

	return 0;
}


static void msm_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(gc);
	const struct msm_pingroup_v1 *g;
	unsigned long flags;

	g = &pctrl->soc->groups[d->hwirq];

	spin_lock_irqsave(&pctrl->lock, flags);

	msm_gpio_clear_detect_status(pctrl, g);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(gc);
	struct msm_pinctrl_v1_bank_data *bank;
	const struct msm_pingroup_v1 *g;
	unsigned long flags;

	g = &pctrl->soc->groups[d->hwirq];

	bank = &pctrl->banks[g->bank_index];

	spin_lock_irqsave(&pctrl->lock, flags);

	/* level triggered interrupts are also latched */
	if (!(msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_EDGE)
		& BIT(g->offset)))
		msm_gpio_clear_detect_status(pctrl, g);
	bank->int_enable[0] &= ~BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, bank->int_enable[0],
			  MSM_GPIO_INT_EN);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static void msm_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(gc);
	struct msm_pinctrl_v1_bank_data *bank;
	const struct msm_pingroup_v1 *g;
	unsigned long flags;

	g = &pctrl->soc->groups[d->hwirq];

	bank = &pctrl->banks[g->bank_index];

	spin_lock_irqsave(&pctrl->lock, flags);

	/* level triggered interrupts are also latched */
	if (!(msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_EDGE)
		& BIT(g->offset)))
		msm_gpio_clear_detect_status(pctrl, g);
	bank->int_enable[0] |= BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, bank->int_enable[0],
			  MSM_GPIO_INT_EN);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int msm_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(gc);
	struct msm_pinctrl_v1_bank_data *bank;
	const struct msm_pingroup_v1 *g;
	unsigned long flags;
	u32 val;
	u32 mask;

	g = &pctrl->soc->groups[d->hwirq];

	bank = &pctrl->banks[g->bank_index];
	mask = BIT(g->offset);

	spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_EDGE);
	if (flow_type & IRQ_TYPE_EDGE_BOTH) {
		msm_pinctrl_write(pctrl, g->bank_index, val | mask,
				  MSM_GPIO_INT_EDGE);
		irq_set_handler_locked(d, handle_edge_irq);
	} else {
		msm_pinctrl_write(pctrl, g->bank_index, val & ~mask,
				  MSM_GPIO_INT_EDGE);
		irq_set_handler_locked(d, handle_level_irq);
	}
	if ((flow_type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH) {
		bank->both_edge_detect |= mask;
		msm_gpio_update_dual_edge_pos(pctrl, g);
	} else {
		bank->both_edge_detect &= ~mask;
		val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_INT_POS);
		if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_HIGH))
			val |= mask;
		else
			val &= ~mask;
		msm_pinctrl_write(pctrl, g->bank_index, val, MSM_GPIO_INT_POS);
	}

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(gc);
	struct msm_pinctrl_v1_bank_data *bank;
	const struct msm_pingroup_v1 *g;
	unsigned long flags;

	g = &pctrl->soc->groups[d->hwirq];

	bank = &pctrl->banks[g->bank_index];

	spin_lock_irqsave(&pctrl->lock, flags);

	if (on)
		bank->int_enable[1] |= BIT(g->offset);
	else
		bank->int_enable[1] &= ~BIT(g->offset);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}


static irqreturn_t msm_gpio_irq(int irq, void *data)
{
	struct msm_pinctrl_v1 *pctrl = data;
	struct gpio_chip *gc = &pctrl->chip;
	int irq_pin;
	u32 val;
	int i, j, mask;

	for (i = 0; i < pctrl->soc->nbanks; i++) {
		const struct msm_pinctrl_v1_bank_data *bank = &pctrl->banks[i];
		val = msm_pinctrl_read(pctrl, bank->index, MSM_GPIO_INT_STATUS);
		val &= bank->int_enable[0];
		while (val) {
			mask = val & -val;
			j = fls(mask) - 1;
			val &= ~mask;
			irq_pin = irq_find_mapping(gc->irqdomain,
						   bank->base + j);
			generic_handle_irq(irq_pin);
		}
	}

	return IRQ_HANDLED;
}

static struct irq_chip msm_gpio_irq_chip = {
	.name		= "msm-gpio",
	.irq_ack	= msm_gpio_irq_ack,
	.irq_mask	= msm_gpio_irq_mask,
	.irq_unmask	= msm_gpio_irq_unmask,
	.irq_set_type	= msm_gpio_irq_set_type,
	.irq_set_wake	= msm_gpio_irq_set_wake,
};

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OE);
	val &= ~BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, val, MSM_GPIO_OE);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OUT);
	if (value)
		val |= BIT(g->offset);
	else
		val &= ~BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, val, MSM_GPIO_OUT);

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OE);
	val |= BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, val, MSM_GPIO_OE);

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(chip);
	u32 val;

	g = &pctrl->soc->groups[offset];

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_IN);
	return !!(val & BIT(g->offset));
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(chip);
	unsigned long flags;
	u32 val;

	g = &pctrl->soc->groups[offset];

	spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OUT);
	if (value)
		val |= BIT(g->offset);
	else
		val &= ~BIT(g->offset);
	msm_pinctrl_write(pctrl, g->bank_index, val, MSM_GPIO_OUT);

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static unsigned msm_regval_to_drive(u32 val)
{
	return (val + 1) * 2;
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void msm_gpio_dbg_show_one(struct seq_file *s,
				  struct pinctrl_dev *pctldev,
				  struct gpio_chip *chip,
				  unsigned offset,
				  unsigned gpio)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = to_msm_pinctrl_v1(chip);
	u32 val;
	unsigned func;
	int is_out;
	int drive;
	int pull;
	int level;

	static const char * const pulls[] = {
		"no pull",
		"pull down",
		"keeper",
		"pull up"
	};

	g = &pctrl->soc->groups[offset];


	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OE);
	is_out = !!(val & BIT(g->offset));
	func = GPIO_FUNC(pctrl->tlmm[offset]);
	drive = GPIO_DRVSTR(pctrl->tlmm[offset]);
	pull = GPIO_PULL(pctrl->tlmm[offset]);
	val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_IN);
	level = !!(val & BIT(g->offset));

	seq_printf(s, " %-8s: %-3s %d", g->name, is_out ? "out" : "in", func);
	seq_printf(s, " %dmA", msm_regval_to_drive(drive));
	seq_printf(s, " %s", pulls[pull]);
	seq_printf(s, " %d level", level);
}

static void msm_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned gpio = chip->base;
	unsigned i;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		msm_gpio_dbg_show_one(s, NULL, chip, i, gpio);
		seq_puts(s, "\n");
	}
}

#else
#define msm_gpio_dbg_show NULL
#endif

static struct gpio_chip msm_gpio_template = {
	.label			= "msm-gpio",
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.direction_input	= msm_gpio_direction_input,
	.direction_output	= msm_gpio_direction_output,
	.get			= msm_gpio_get,
	.set			= msm_gpio_set,
	.dbg_show		= msm_gpio_dbg_show,
};

static int msm_gpio_init(struct msm_pinctrl_v1 *pctrl)
{
	struct gpio_chip *chip;
	int ret;
	unsigned ngpio = pctrl->soc->ngpios;

	if (WARN_ON(ngpio > MAX_NR_GPIO))
		return -EINVAL;

	chip = &pctrl->chip;
	chip->base = 0;
	chip->ngpio = ngpio;
	chip->label = dev_name(pctrl->dev);
	chip->dev = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->of_node = pctrl->dev->of_node;

	ret = gpiochip_add(&pctrl->chip);
	if (ret) {
		dev_err(pctrl->dev, "Failed register gpiochip\n");
		return ret;
	}

	ret = gpiochip_add_pin_range(&pctrl->chip, dev_name(pctrl->dev), 0, 0,
				     chip->ngpio);
	if (ret) {
		dev_err(pctrl->dev, "Failed to add pin range\n");
		gpiochip_remove(&pctrl->chip);
		return ret;
	}

	ret = gpiochip_irqchip_add(chip,
				   &msm_gpio_irq_chip,
				   0,
				   handle_edge_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(pctrl->dev, "Failed to add irqchip to gpiochip\n");
		gpiochip_remove(&pctrl->chip);
		return -ENOSYS;
	}

	ret = devm_request_irq(pctrl->dev, pctrl->irq1, msm_gpio_irq,
			       IRQF_SHARED, dev_name(pctrl->dev), pctrl);
	if (ret) {

	}

	ret = devm_request_irq(pctrl->dev, pctrl->irq2, msm_gpio_irq,
			       IRQF_SHARED, dev_name(pctrl->dev), pctrl);
	if (ret) {

	}

	gpiochip_set_chained_irqchip(chip, &msm_gpio_irq_chip, pctrl->irq1,
				     NULL);

	irq_set_irq_wake(pctrl->irq1, 1);
	irq_set_irq_wake(pctrl->irq2, 1);
	return 0;
}

static int msm_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->ngroups;
}

static const char *msm_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned group)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->groups[group].name;
}

static int msm_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned group,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->soc->groups[group].pins;
	*num_pins = pctrl->soc->groups[group].npins;
	return 0;
}

static const struct pinctrl_ops msm_pinctrl_v1_ops = {
	.get_groups_count	= msm_get_groups_count,
	.get_group_name		= msm_get_group_name,
	.get_group_pins		= msm_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_dt_free_map,
};

static int msm_pinmux_free(struct pinctrl_dev *pctldev, unsigned offset)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct msm_pingroup_v1 *g;

	g = &pctrl->soc->groups[offset];

	gpio_tlmm_config(pctrl->tlmm[g->index], GPIO_CFG_DISABLE);

	return 0;
}

static int msm_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->nfunctions;
}

static const char *msm_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned function)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->soc->functions[function].name;
}

static int msm_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned function,
				   const char * const **groups,
				   unsigned * const num_groups)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->soc->functions[function].groups;
	*num_groups = pctrl->soc->functions[function].ngroups;
	return 0;
}

static int msm_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned function,
			      unsigned group)
{
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct msm_pingroup_v1 *g;
	int i;
	uint8_t pin, func, dir, pull, drvstr;

	g = &pctrl->soc->groups[group];

	for (i = 0; i < g->nfuncs; i++) {
		if (g->funcs[i] == function)
			break;
	}

	if (WARN_ON(i == g->nfuncs))
		return -EINVAL;

	pin = g->index;
	func = function;
	dir = GPIO_DIR(pctrl->tlmm[g->index]);
	pull = GPIO_PULL(pctrl->tlmm[g->index]);
	drvstr = GPIO_DRVSTR(pctrl->tlmm[g->index]);

	pctrl->tlmm[g->index] = GPIO_CFG(pin, func, dir, pull, drvstr);
	gpio_tlmm_config(pctrl->tlmm[g->index], GPIO_CFG_ENABLE);

	return 0;
}

static const struct pinmux_ops msm_pinmux_v1_ops = {
	.free			= msm_pinmux_free,
	.get_functions_count	= msm_get_functions_count,
	.get_function_name	= msm_get_function_name,
	.get_function_groups	= msm_get_function_groups,
	.set_mux		= msm_pinmux_set_mux,
};

static int msm_config_group_get(struct pinctrl_dev *pctldev,
				unsigned int group,
				unsigned long *config)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned param = pinconf_to_config_param(*config);
	unsigned arg;
	u32 val;

	g = &pctrl->soc->groups[group];

	/* Convert register value to pinconf value */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		val = GPIO_PULL(pctrl->tlmm[g->index]);
		arg = val == GPIO_CFG_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		val = GPIO_PULL(pctrl->tlmm[g->index]);
		arg = val == GPIO_CFG_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		val = GPIO_PULL(pctrl->tlmm[g->index]);
		arg = val == GPIO_CFG_KEEPER;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		val = GPIO_PULL(pctrl->tlmm[g->index]);
		arg = val == GPIO_CFG_PULL_UP;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		val = GPIO_DRVSTR(pctrl->tlmm[g->index]);
		arg = msm_regval_to_drive(val);
		break;
	case PIN_CONFIG_OUTPUT:
		val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OE);
		/* Pin is not output */
		if (!(val & BIT(g->offset)))
			return -EINVAL;

		val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OUT);
		arg = !!(val & BIT(g->offset));
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		val = msm_pinctrl_read(pctrl, g->bank_index, MSM_GPIO_OE);
		/* Pin is output */
		if (!!(val & BIT(g->offset)))
			return -EINVAL;
		arg = 1;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int msm_config_group_set(struct pinctrl_dev *pctldev,
				unsigned group,
				unsigned long *configs,
				unsigned num_configs)
{
	const struct msm_pingroup_v1 *g;
	struct msm_pinctrl_v1 *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	unsigned param;
	unsigned arg;
	u32 val;
	int i;
	uint8_t pin, func, dir, pull, drvstr;

	g = &pctrl->soc->groups[group];

	pin = g->index;
	func = GPIO_FUNC(pctrl->tlmm[g->index]);
	dir = GPIO_DIR(pctrl->tlmm[g->index]);
	pull = GPIO_PULL(pctrl->tlmm[g->index]);
	drvstr = GPIO_DRVSTR(pctrl->tlmm[g->index]);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		/* Convert pinconf values to register values */
		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			pull = GPIO_CFG_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pull = GPIO_CFG_PULL_DOWN;
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			pull = GPIO_CFG_KEEPER;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			pull = GPIO_CFG_PULL_UP;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Check for invalid values */
			if (arg > 16 || arg < 2 || (arg % 2) != 0)
				arg = -1;
			else
				arg = (arg / 2) - 1;
			drvstr = arg;
			break;
		case PIN_CONFIG_OUTPUT:
			dir = GPIO_CFG_OUTPUT;
			/* set output value */
			spin_lock_irqsave(&pctrl->lock, flags);

			val = msm_pinctrl_read(pctrl, g->bank_index,
					       MSM_GPIO_OUT);

			if (arg)
				val |= BIT(g->offset);
			else
				val &= ~BIT(g->offset);
			msm_pinctrl_write(pctrl, g->bank_index, val,
					  MSM_GPIO_OUT);

			val = msm_pinctrl_read(pctrl, g->bank_index,
					       MSM_GPIO_OE);
			val |= BIT(g->offset);
			msm_pinctrl_write(pctrl, g->bank_index, val,
					  MSM_GPIO_OE);

			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			/* disable output */
			dir = GPIO_CFG_INPUT;

			spin_lock_irqsave(&pctrl->lock, flags);

			val = msm_pinctrl_read(pctrl, g->bank_index,
					       MSM_GPIO_OE);
			val &= ~BIT(g->offset);
			msm_pinctrl_write(pctrl, g->bank_index, val,
					  MSM_GPIO_OE);
			spin_unlock_irqrestore(&pctrl->lock, flags);
			break;
		default:
			dev_err(pctrl->dev, "Unsupported parameter: %x\n",
				param);
			return -EINVAL;
		}

		pctrl->tlmm[g->index] = GPIO_CFG(pin, func, dir, pull, drvstr);
		gpio_tlmm_config(pctrl->tlmm[g->index], GPIO_CFG_ENABLE);
	}

	return 0;
}

static const struct pinconf_ops msm_pinconf_v1_ops = {
	.is_generic		= true,
	.pin_config_group_get	= msm_config_group_get,
	.pin_config_group_set	= msm_config_group_set,
};

static struct pinctrl_desc msm_pinctrl_v1_desc = {
	.pctlops	= &msm_pinctrl_v1_ops,
	.pmxops		= &msm_pinmux_v1_ops,
	.confops	= &msm_pinconf_v1_ops,
	.owner		= THIS_MODULE,
};

int msm_pinctrl_v1_probe(struct platform_device *pdev,
		 	 const struct msm_pinctrl_v1_soc_data *soc_data)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct msm_pinctrl_v1 *pctrl;
	struct resource *res;
	int i;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl) {
		dev_err(dev, "Can't allocate msm_pinctrl\n");
		return -ENOMEM;
	}

	pctrl->dev = dev;
	pctrl->soc = soc_data;
	pctrl->chip = msm_gpio_template;

	spin_lock_init(&pctrl->lock);

	pctrl->tlmm = devm_kcalloc(dev, pctrl->soc->npins, sizeof(*pctrl->tlmm),
				   GFP_KERNEL);
	if (!pctrl->tlmm) {
		dev_err(dev, "Can't allocate tlmm\n");
		return -ENOMEM;
	}

	/* Copy over bank data. */
	pctrl->banks = devm_kcalloc(dev, pctrl->soc->nbanks,
				    sizeof(*pctrl->banks), GFP_KERNEL);
	for (i = 0; i < pctrl->soc->nbanks; i++) {
		memcpy(&pctrl->banks[i], &pctrl->soc->banks[i],
		       sizeof(*pctrl->banks));
	}

	pctrl->irq1 = platform_get_irq(pdev, 0);
	if (pctrl->irq1 < 0) {
		dev_err(&pdev->dev, "No interrupt 1 defined for msmgpio\n");
		return pctrl->irq1;
	}

	pctrl->irq2 = platform_get_irq(pdev, 1);
	if (pctrl->irq2 < 0) {
		dev_err(&pdev->dev, "No interrupt 2 defined for msmgpio\n");
		return pctrl->irq2;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctrl->regs1 = devm_ioremap_resource(dev, res);
	if (IS_ERR(pctrl->regs1))
		return PTR_ERR(pctrl->regs1);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	pctrl->regs2 = devm_ioremap_resource(dev, res);
	if (IS_ERR(pctrl->regs2))
		return PTR_ERR(pctrl->regs2);

	msm_pinctrl_v1_desc.name = dev_name(dev);
	msm_pinctrl_v1_desc.pins = pctrl->soc->pins;
	msm_pinctrl_v1_desc.npins = pctrl->soc->npins;
	pctrl->pctrl = pinctrl_register(&msm_pinctrl_v1_desc, dev, pctrl);
	if (IS_ERR(pctrl->pctrl)) {
		dev_err(dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(pctrl->pctrl);
	}

	ret = msm_gpio_init(pctrl);
	if (ret) {
		pinctrl_unregister(pctrl->pctrl);
		return ret;
	}

	platform_set_drvdata(pdev, pctrl);

	dev_dbg(&pdev->dev, "Probed Qualcomm pinctrl driver\n");

	return 0;
}

/*
 * Copyright (c) 2013, Sony Mobile Communications AB.
 * Copyright (c) 2016, Rudolf Tammekivi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __PINCTRL_MSM_V1_H__
#define __PINCTRL_MSM_V1_H__

#define MSM_GPIO_BROKEN_INT_CLEAR 1

enum msm_gpio_reg {
	MSM_GPIO_IN,
	MSM_GPIO_OUT,
	MSM_GPIO_INT_STATUS,
	MSM_GPIO_INT_CLEAR,
	MSM_GPIO_INT_EN,
	MSM_GPIO_INT_EDGE,
	MSM_GPIO_INT_POS,
	MSM_GPIO_OE,
	MSM_GPIO_REG_NR
};

struct msm_pinctrl_v1_bank_data {
	int index;
	int base;
	int ngpio;
	u32 in_reg;
	u32 out_reg;
	u32 int_sts_reg;
	u32 int_clr_reg;
	u32 int_en_reg;
	u32 int_edge_reg;
	u32 int_pos_reg;
	u32 oe_reg;

	/* Additional data used by the driver. */
#if MSM_GPIO_BROKEN_INT_CLEAR
	unsigned int int_status_copy;
#endif
	unsigned int both_edge_detect;
	unsigned int int_enable[2]; /* 0: awake, 1: sleep */
};

struct pinctrl_pin_desc;

/**
 * struct msm_function_v1 - a pinmux function
 * @name:    Name of the pinmux function.
 * @groups:  List of pingroups for this function.
 * @ngroups: Number of entries in @groups.
 */
struct msm_function_v1 {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
};

/**
 * struct msm_pingroup_v1 - Qualcomm pingroup definition
 * @bank_index:           The bank where the pin exists.
 * @index:                The pin index (absolute).
 * @name:                 Name of the pingroup.
 * @pins:                 A list of pins assigned to this pingroup.
 * @npins:                Number of entries in @pins.
 * @funcs:                A list of pinmux functions that can be selected for
 *                        this group. The index of the selected function is used
 *                        for programming the function selector.
 *                        Entries should be indices into the groups list of the
 *                        struct msm_pinctrl_soc_data.
 * @nfuncs:               Number of entries in @funcs.
 * @offset:               The pin index in the bank (relative).
 */
struct msm_pingroup_v1 {
	uint8_t bank_index;
	uint8_t index;
	const char *name;
	const unsigned *pins;
	unsigned npins;

	unsigned *funcs;
	unsigned nfuncs;

	uint8_t offset;
};

/**
 * struct msm_pinctrl_v1_soc_data - Qualcomm pin controller driver configuration
 * @pins:       An array describing all pins the pin controller affects.
 * @npins:      The number of entries in @pins.
 * @functions:  An array describing all mux functions the SoC supports.
 * @nfunctions: The number of entries in @functions.
 * @groups:     An array describing all pin groups the pin SoC supports.
 * @ngroups:    The numbmer of entries in @groups.
 * @ngpio:      The number of pingroups the driver should expose as GPIOs.
 * @banks:      An array describing all banks the pin controller affects.
 * @nbanks:     The number of entries in @banks.
 */
struct msm_pinctrl_v1_soc_data {
	const struct pinctrl_pin_desc *pins;
	uint8_t npins;
	const struct msm_function_v1 *functions;
	uint8_t nfunctions;
	const struct msm_pingroup_v1 *groups;
	uint8_t ngroups;
	uint8_t ngpios;
	const struct msm_pinctrl_v1_bank_data *banks;
	uint8_t nbanks;
};

int msm_pinctrl_v1_probe(struct platform_device *pdev,
			 const struct msm_pinctrl_v1_soc_data *soc_data);

#endif

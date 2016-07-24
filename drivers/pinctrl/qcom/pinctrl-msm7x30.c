/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
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

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-msm-v1.h"


/* see 80-VA736-2 Rev C pp 695-751
**
** These are actually the *shadow* gpio registers, since the
** real ones (which allow full access) are only available to the
** ARM9 side of the world.
**
** Since the _BASE need to be page-aligned when we're mapping them
** to virtual addresses, adjust for the additional offset in these
** macros.
*/

#define MSM_GPIO2_REG(off) (0x400 + off)

#define MSM7X30_GPIO_BASE_0		0
#define MSM7X30_GPIO_BASE_1		16
#define MSM7X30_GPIO_BASE_2		44
#define MSM7X30_GPIO_BASE_3		68
#define MSM7X30_GPIO_BASE_4		95
#define MSM7X30_GPIO_BASE_5		107
#define MSM7X30_GPIO_BASE_6		134
#define MSM7X30_GPIO_BASE_7		151

/*
 * MSM7X30 registers
 */
/* output value */
#define MSM7X30_GPIO_OUT_0		0x00			/* gpio 15-0 */
#define MSM7X30_GPIO_OUT_1		MSM_GPIO2_REG(0x00)	/* gpio 43-16 */
#define MSM7X30_GPIO_OUT_2		0x04			/* gpio 67-44 */
#define MSM7X30_GPIO_OUT_3		0x08			/* gpio 94-68 */
#define MSM7X30_GPIO_OUT_4		0x0C			/* gpio 106-95 */
#define MSM7X30_GPIO_OUT_5		0x50			/* gpio 133-107 */
#define MSM7X30_GPIO_OUT_6		0xC4			/* gpio 150-134 */
#define MSM7X30_GPIO_OUT_7		0x214			/* gpio 181-151 */

/* same pin map as above, output enable */
#define MSM7X30_GPIO_OE_0		0x10
#define MSM7X30_GPIO_OE_1		MSM_GPIO2_REG(0x08)
#define MSM7X30_GPIO_OE_2		0x14
#define MSM7X30_GPIO_OE_3		0x18
#define MSM7X30_GPIO_OE_4		0x1C
#define MSM7X30_GPIO_OE_5		0x54
#define MSM7X30_GPIO_OE_6		0xC8
#define MSM7X30_GPIO_OE_7		0x218

/* same pin map as above, input read */
#define MSM7X30_GPIO_IN_0		0x34
#define MSM7X30_GPIO_IN_1		MSM_GPIO2_REG(0x20)
#define MSM7X30_GPIO_IN_2		0x38
#define MSM7X30_GPIO_IN_3		0x3C
#define MSM7X30_GPIO_IN_4		0x40
#define MSM7X30_GPIO_IN_5		0x44
#define MSM7X30_GPIO_IN_6		0xCC
#define MSM7X30_GPIO_IN_7		0x21C

/* same pin map as above, 1=edge 0=level interrupt */
#define MSM7X30_GPIO_INT_EDGE_0		0x60
#define MSM7X30_GPIO_INT_EDGE_1		MSM_GPIO2_REG(0x50)
#define MSM7X30_GPIO_INT_EDGE_2		0x64
#define MSM7X30_GPIO_INT_EDGE_3		0x68
#define MSM7X30_GPIO_INT_EDGE_4		0x6C
#define MSM7X30_GPIO_INT_EDGE_5		0xC0
#define MSM7X30_GPIO_INT_EDGE_6		0xD0
#define MSM7X30_GPIO_INT_EDGE_7		0x240

/* same pin map as above, 1=positive 0=negative */
#define MSM7X30_GPIO_INT_POS_0		0x70
#define MSM7X30_GPIO_INT_POS_1		MSM_GPIO2_REG(0x58)
#define MSM7X30_GPIO_INT_POS_2		0x74
#define MSM7X30_GPIO_INT_POS_3		0x78
#define MSM7X30_GPIO_INT_POS_4		0x7C
#define MSM7X30_GPIO_INT_POS_5		0xBC
#define MSM7X30_GPIO_INT_POS_6		0xD4
#define MSM7X30_GPIO_INT_POS_7		0x228

/* same pin map as above, interrupt enable */
#define MSM7X30_GPIO_INT_EN_0		0x80
#define MSM7X30_GPIO_INT_EN_1		MSM_GPIO2_REG(0x60)
#define MSM7X30_GPIO_INT_EN_2		0x84
#define MSM7X30_GPIO_INT_EN_3		0x88
#define MSM7X30_GPIO_INT_EN_4		0x8C
#define MSM7X30_GPIO_INT_EN_5		0xB8
#define MSM7X30_GPIO_INT_EN_6		0xD8
#define MSM7X30_GPIO_INT_EN_7		0x22C

/* same pin map as above, write 1 to clear interrupt */
#define MSM7X30_GPIO_INT_CLEAR_0	0x90
#define MSM7X30_GPIO_INT_CLEAR_1	MSM_GPIO2_REG(0x68)
#define MSM7X30_GPIO_INT_CLEAR_2	0x94
#define MSM7X30_GPIO_INT_CLEAR_3	0x98
#define MSM7X30_GPIO_INT_CLEAR_4	0x9C
#define MSM7X30_GPIO_INT_CLEAR_5	0xB4
#define MSM7X30_GPIO_INT_CLEAR_6	0xDC
#define MSM7X30_GPIO_INT_CLEAR_7	0x230

/* same pin map as above, 1=interrupt pending */
#define MSM7X30_GPIO_INT_STATUS_0	0xA0
#define MSM7X30_GPIO_INT_STATUS_1	MSM_GPIO2_REG(0x70)
#define MSM7X30_GPIO_INT_STATUS_2	0xA4
#define MSM7X30_GPIO_INT_STATUS_3	0xA8
#define MSM7X30_GPIO_INT_STATUS_4	0xAC
#define MSM7X30_GPIO_INT_STATUS_5	0xB0
#define MSM7X30_GPIO_INT_STATUS_6	0xE0
#define MSM7X30_GPIO_INT_STATUS_7	0x234

static const struct pinctrl_pin_desc msm7x30_pins[] = {
	PINCTRL_PIN(0, "GPIO_0"),
	PINCTRL_PIN(1, "GPIO_1"),
	PINCTRL_PIN(2, "GPIO_2"),
	PINCTRL_PIN(3, "GPIO_3"),
	PINCTRL_PIN(4, "GPIO_4"),
	PINCTRL_PIN(5, "GPIO_5"),
	PINCTRL_PIN(6, "GPIO_6"),
	PINCTRL_PIN(7, "GPIO_7"),
	PINCTRL_PIN(8, "GPIO_8"),
	PINCTRL_PIN(9, "GPIO_9"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),
	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),
	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),
	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),
	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "GPIO_70"),
	PINCTRL_PIN(71, "GPIO_71"),
	PINCTRL_PIN(72, "GPIO_72"),
	PINCTRL_PIN(73, "GPIO_73"),
	PINCTRL_PIN(74, "GPIO_74"),
	PINCTRL_PIN(75, "GPIO_75"),
	PINCTRL_PIN(76, "GPIO_76"),
	PINCTRL_PIN(77, "GPIO_77"),
	PINCTRL_PIN(78, "GPIO_78"),
	PINCTRL_PIN(79, "GPIO_79"),
	PINCTRL_PIN(80, "GPIO_80"),
	PINCTRL_PIN(81, "GPIO_81"),
	PINCTRL_PIN(82, "GPIO_82"),
	PINCTRL_PIN(83, "GPIO_83"),
	PINCTRL_PIN(84, "GPIO_84"),
	PINCTRL_PIN(85, "GPIO_85"),
	PINCTRL_PIN(86, "GPIO_86"),
	PINCTRL_PIN(87, "GPIO_87"),
	PINCTRL_PIN(88, "GPIO_88"),
	PINCTRL_PIN(89, "GPIO_89"),
	PINCTRL_PIN(90, "GPIO_90"),
	PINCTRL_PIN(91, "GPIO_91"),
	PINCTRL_PIN(92, "GPIO_92"),
	PINCTRL_PIN(93, "GPIO_93"),
	PINCTRL_PIN(94, "GPIO_94"),
	PINCTRL_PIN(95, "GPIO_95"),
	PINCTRL_PIN(96, "GPIO_96"),
	PINCTRL_PIN(97, "GPIO_97"),
	PINCTRL_PIN(98, "GPIO_98"),
	PINCTRL_PIN(99, "GPIO_99"),
	PINCTRL_PIN(100, "GPIO_100"),
	PINCTRL_PIN(101, "GPIO_101"),
	PINCTRL_PIN(102, "GPIO_102"),
	PINCTRL_PIN(103, "GPIO_103"),
	PINCTRL_PIN(104, "GPIO_104"),
	PINCTRL_PIN(105, "GPIO_105"),
	PINCTRL_PIN(106, "GPIO_106"),
	PINCTRL_PIN(107, "GPIO_107"),
	PINCTRL_PIN(108, "GPIO_108"),
	PINCTRL_PIN(109, "GPIO_109"),
	PINCTRL_PIN(110, "GPIO_110"),
	PINCTRL_PIN(111, "GPIO_111"),
	PINCTRL_PIN(112, "GPIO_112"),
	PINCTRL_PIN(113, "GPIO_113"),
	PINCTRL_PIN(114, "GPIO_114"),
	PINCTRL_PIN(115, "GPIO_115"),
	PINCTRL_PIN(116, "GPIO_116"),
	PINCTRL_PIN(117, "GPIO_117"),
	PINCTRL_PIN(118, "GPIO_118"),
	PINCTRL_PIN(119, "GPIO_119"),
	PINCTRL_PIN(120, "GPIO_120"),
	PINCTRL_PIN(121, "GPIO_121"),
	PINCTRL_PIN(122, "GPIO_122"),
	PINCTRL_PIN(123, "GPIO_123"),
	PINCTRL_PIN(124, "GPIO_124"),
	PINCTRL_PIN(125, "GPIO_125"),
	PINCTRL_PIN(126, "GPIO_126"),
	PINCTRL_PIN(127, "GPIO_127"),
	PINCTRL_PIN(128, "GPIO_128"),
	PINCTRL_PIN(129, "GPIO_129"),
	PINCTRL_PIN(130, "GPIO_130"),
	PINCTRL_PIN(131, "GPIO_131"),
	PINCTRL_PIN(132, "GPIO_132"),
	PINCTRL_PIN(133, "GPIO_133"),
	PINCTRL_PIN(134, "GPIO_134"),
	PINCTRL_PIN(135, "GPIO_135"),
	PINCTRL_PIN(136, "GPIO_136"),
	PINCTRL_PIN(137, "GPIO_137"),
	PINCTRL_PIN(138, "GPIO_138"),
	PINCTRL_PIN(139, "GPIO_139"),
	PINCTRL_PIN(140, "GPIO_140"),
	PINCTRL_PIN(141, "GPIO_141"),
	PINCTRL_PIN(142, "GPIO_142"),
	PINCTRL_PIN(143, "GPIO_143"),
	PINCTRL_PIN(144, "GPIO_144"),
	PINCTRL_PIN(145, "GPIO_145"),
	PINCTRL_PIN(146, "GPIO_146"),
	PINCTRL_PIN(147, "GPIO_147"),
	PINCTRL_PIN(148, "GPIO_148"),
	PINCTRL_PIN(149, "GPIO_149"),
	PINCTRL_PIN(150, "GPIO_150"),
	PINCTRL_PIN(151, "GPIO_151"),
	PINCTRL_PIN(152, "GPIO_152"),
	PINCTRL_PIN(153, "GPIO_153"),
	PINCTRL_PIN(154, "GPIO_154"),
	PINCTRL_PIN(155, "GPIO_155"),
	PINCTRL_PIN(156, "GPIO_156"),
	PINCTRL_PIN(157, "GPIO_157"),
	PINCTRL_PIN(158, "GPIO_158"),
	PINCTRL_PIN(159, "GPIO_159"),
	PINCTRL_PIN(160, "GPIO_160"),
	PINCTRL_PIN(161, "GPIO_161"),
	PINCTRL_PIN(162, "GPIO_162"),
	PINCTRL_PIN(163, "GPIO_163"),
	PINCTRL_PIN(164, "GPIO_164"),
	PINCTRL_PIN(165, "GPIO_165"),
	PINCTRL_PIN(166, "GPIO_166"),
	PINCTRL_PIN(167, "GPIO_167"),
	PINCTRL_PIN(168, "GPIO_168"),
	PINCTRL_PIN(169, "GPIO_169"),
	PINCTRL_PIN(170, "GPIO_170"),
	PINCTRL_PIN(171, "GPIO_171"),
	PINCTRL_PIN(172, "GPIO_172"),
	PINCTRL_PIN(173, "GPIO_173"),
	PINCTRL_PIN(174, "GPIO_174"),
	PINCTRL_PIN(175, "GPIO_175"),
	PINCTRL_PIN(176, "GPIO_176"),
	PINCTRL_PIN(177, "GPIO_177"),
	PINCTRL_PIN(178, "GPIO_178"),
	PINCTRL_PIN(179, "GPIO_179"),
	PINCTRL_PIN(180, "GPIO_180"),
	PINCTRL_PIN(181, "GPIO_181"),
};

#define DECLARE_MSM_GPIO_PIN(pin) static const unsigned int gpio##pin##_pins[] = { pin }
DECLARE_MSM_GPIO_PIN(0);
DECLARE_MSM_GPIO_PIN(1);
DECLARE_MSM_GPIO_PIN(2);
DECLARE_MSM_GPIO_PIN(3);
DECLARE_MSM_GPIO_PIN(4);
DECLARE_MSM_GPIO_PIN(5);
DECLARE_MSM_GPIO_PIN(6);
DECLARE_MSM_GPIO_PIN(7);
DECLARE_MSM_GPIO_PIN(8);
DECLARE_MSM_GPIO_PIN(9);
DECLARE_MSM_GPIO_PIN(10);
DECLARE_MSM_GPIO_PIN(11);
DECLARE_MSM_GPIO_PIN(12);
DECLARE_MSM_GPIO_PIN(13);
DECLARE_MSM_GPIO_PIN(14);
DECLARE_MSM_GPIO_PIN(15);
DECLARE_MSM_GPIO_PIN(16);
DECLARE_MSM_GPIO_PIN(17);
DECLARE_MSM_GPIO_PIN(18);
DECLARE_MSM_GPIO_PIN(19);
DECLARE_MSM_GPIO_PIN(20);
DECLARE_MSM_GPIO_PIN(21);
DECLARE_MSM_GPIO_PIN(22);
DECLARE_MSM_GPIO_PIN(23);
DECLARE_MSM_GPIO_PIN(24);
DECLARE_MSM_GPIO_PIN(25);
DECLARE_MSM_GPIO_PIN(26);
DECLARE_MSM_GPIO_PIN(27);
DECLARE_MSM_GPIO_PIN(28);
DECLARE_MSM_GPIO_PIN(29);
DECLARE_MSM_GPIO_PIN(30);
DECLARE_MSM_GPIO_PIN(31);
DECLARE_MSM_GPIO_PIN(32);
DECLARE_MSM_GPIO_PIN(33);
DECLARE_MSM_GPIO_PIN(34);
DECLARE_MSM_GPIO_PIN(35);
DECLARE_MSM_GPIO_PIN(36);
DECLARE_MSM_GPIO_PIN(37);
DECLARE_MSM_GPIO_PIN(38);
DECLARE_MSM_GPIO_PIN(39);
DECLARE_MSM_GPIO_PIN(40);
DECLARE_MSM_GPIO_PIN(41);
DECLARE_MSM_GPIO_PIN(42);
DECLARE_MSM_GPIO_PIN(43);
DECLARE_MSM_GPIO_PIN(44);
DECLARE_MSM_GPIO_PIN(45);
DECLARE_MSM_GPIO_PIN(46);
DECLARE_MSM_GPIO_PIN(47);
DECLARE_MSM_GPIO_PIN(48);
DECLARE_MSM_GPIO_PIN(49);
DECLARE_MSM_GPIO_PIN(50);
DECLARE_MSM_GPIO_PIN(51);
DECLARE_MSM_GPIO_PIN(52);
DECLARE_MSM_GPIO_PIN(53);
DECLARE_MSM_GPIO_PIN(54);
DECLARE_MSM_GPIO_PIN(55);
DECLARE_MSM_GPIO_PIN(56);
DECLARE_MSM_GPIO_PIN(57);
DECLARE_MSM_GPIO_PIN(58);
DECLARE_MSM_GPIO_PIN(59);
DECLARE_MSM_GPIO_PIN(60);
DECLARE_MSM_GPIO_PIN(61);
DECLARE_MSM_GPIO_PIN(62);
DECLARE_MSM_GPIO_PIN(63);
DECLARE_MSM_GPIO_PIN(64);
DECLARE_MSM_GPIO_PIN(65);
DECLARE_MSM_GPIO_PIN(66);
DECLARE_MSM_GPIO_PIN(67);
DECLARE_MSM_GPIO_PIN(68);
DECLARE_MSM_GPIO_PIN(69);
DECLARE_MSM_GPIO_PIN(70);
DECLARE_MSM_GPIO_PIN(71);
DECLARE_MSM_GPIO_PIN(72);
DECLARE_MSM_GPIO_PIN(73);
DECLARE_MSM_GPIO_PIN(74);
DECLARE_MSM_GPIO_PIN(75);
DECLARE_MSM_GPIO_PIN(76);
DECLARE_MSM_GPIO_PIN(77);
DECLARE_MSM_GPIO_PIN(78);
DECLARE_MSM_GPIO_PIN(79);
DECLARE_MSM_GPIO_PIN(80);
DECLARE_MSM_GPIO_PIN(81);
DECLARE_MSM_GPIO_PIN(82);
DECLARE_MSM_GPIO_PIN(83);
DECLARE_MSM_GPIO_PIN(84);
DECLARE_MSM_GPIO_PIN(85);
DECLARE_MSM_GPIO_PIN(86);
DECLARE_MSM_GPIO_PIN(87);
DECLARE_MSM_GPIO_PIN(88);
DECLARE_MSM_GPIO_PIN(89);
DECLARE_MSM_GPIO_PIN(90);
DECLARE_MSM_GPIO_PIN(91);
DECLARE_MSM_GPIO_PIN(92);
DECLARE_MSM_GPIO_PIN(93);
DECLARE_MSM_GPIO_PIN(94);
DECLARE_MSM_GPIO_PIN(95);
DECLARE_MSM_GPIO_PIN(96);
DECLARE_MSM_GPIO_PIN(97);
DECLARE_MSM_GPIO_PIN(98);
DECLARE_MSM_GPIO_PIN(99);
DECLARE_MSM_GPIO_PIN(100);
DECLARE_MSM_GPIO_PIN(101);
DECLARE_MSM_GPIO_PIN(102);
DECLARE_MSM_GPIO_PIN(103);
DECLARE_MSM_GPIO_PIN(104);
DECLARE_MSM_GPIO_PIN(105);
DECLARE_MSM_GPIO_PIN(106);
DECLARE_MSM_GPIO_PIN(107);
DECLARE_MSM_GPIO_PIN(108);
DECLARE_MSM_GPIO_PIN(109);
DECLARE_MSM_GPIO_PIN(110);
DECLARE_MSM_GPIO_PIN(111);
DECLARE_MSM_GPIO_PIN(112);
DECLARE_MSM_GPIO_PIN(113);
DECLARE_MSM_GPIO_PIN(114);
DECLARE_MSM_GPIO_PIN(115);
DECLARE_MSM_GPIO_PIN(116);
DECLARE_MSM_GPIO_PIN(117);
DECLARE_MSM_GPIO_PIN(118);
DECLARE_MSM_GPIO_PIN(119);
DECLARE_MSM_GPIO_PIN(120);
DECLARE_MSM_GPIO_PIN(121);
DECLARE_MSM_GPIO_PIN(122);
DECLARE_MSM_GPIO_PIN(123);
DECLARE_MSM_GPIO_PIN(124);
DECLARE_MSM_GPIO_PIN(125);
DECLARE_MSM_GPIO_PIN(126);
DECLARE_MSM_GPIO_PIN(127);
DECLARE_MSM_GPIO_PIN(128);
DECLARE_MSM_GPIO_PIN(129);
DECLARE_MSM_GPIO_PIN(130);
DECLARE_MSM_GPIO_PIN(131);
DECLARE_MSM_GPIO_PIN(132);
DECLARE_MSM_GPIO_PIN(133);
DECLARE_MSM_GPIO_PIN(134);
DECLARE_MSM_GPIO_PIN(135);
DECLARE_MSM_GPIO_PIN(136);
DECLARE_MSM_GPIO_PIN(137);
DECLARE_MSM_GPIO_PIN(138);
DECLARE_MSM_GPIO_PIN(139);
DECLARE_MSM_GPIO_PIN(140);
DECLARE_MSM_GPIO_PIN(141);
DECLARE_MSM_GPIO_PIN(142);
DECLARE_MSM_GPIO_PIN(143);
DECLARE_MSM_GPIO_PIN(144);
DECLARE_MSM_GPIO_PIN(145);
DECLARE_MSM_GPIO_PIN(146);
DECLARE_MSM_GPIO_PIN(147);
DECLARE_MSM_GPIO_PIN(148);
DECLARE_MSM_GPIO_PIN(149);
DECLARE_MSM_GPIO_PIN(150);
DECLARE_MSM_GPIO_PIN(151);
DECLARE_MSM_GPIO_PIN(152);
DECLARE_MSM_GPIO_PIN(153);
DECLARE_MSM_GPIO_PIN(154);
DECLARE_MSM_GPIO_PIN(155);
DECLARE_MSM_GPIO_PIN(156);
DECLARE_MSM_GPIO_PIN(157);
DECLARE_MSM_GPIO_PIN(158);
DECLARE_MSM_GPIO_PIN(159);
DECLARE_MSM_GPIO_PIN(160);
DECLARE_MSM_GPIO_PIN(161);
DECLARE_MSM_GPIO_PIN(162);
DECLARE_MSM_GPIO_PIN(163);
DECLARE_MSM_GPIO_PIN(164);
DECLARE_MSM_GPIO_PIN(165);
DECLARE_MSM_GPIO_PIN(166);
DECLARE_MSM_GPIO_PIN(167);
DECLARE_MSM_GPIO_PIN(168);
DECLARE_MSM_GPIO_PIN(169);
DECLARE_MSM_GPIO_PIN(170);
DECLARE_MSM_GPIO_PIN(171);
DECLARE_MSM_GPIO_PIN(172);
DECLARE_MSM_GPIO_PIN(173);
DECLARE_MSM_GPIO_PIN(174);
DECLARE_MSM_GPIO_PIN(175);
DECLARE_MSM_GPIO_PIN(176);
DECLARE_MSM_GPIO_PIN(177);
DECLARE_MSM_GPIO_PIN(178);
DECLARE_MSM_GPIO_PIN(179);
DECLARE_MSM_GPIO_PIN(180);
DECLARE_MSM_GPIO_PIN(181);

#define FUNCTION(fname)					\
	[MSM_MUX_##fname] = {				\
		.name = #fname,				\
		.groups = fname##_groups,		\
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define PINGROUP(id, bank, f1, f2, f3, f4, f5, f6, f7)		\
	{							\
		.bank_index = bank,				\
		.index = id,					\
		.name = "gpio" #id,				\
		.pins = gpio##id##_pins,			\
		.npins = ARRAY_SIZE(gpio##id##_pins),		\
		.funcs = (int[]){				\
			MSM_MUX_gpio,				\
			MSM_MUX_##f1,				\
			MSM_MUX_##f2,				\
			MSM_MUX_##f3,				\
			MSM_MUX_##f4,				\
			MSM_MUX_##f5,				\
			MSM_MUX_##f6,				\
			MSM_MUX_##f7,				\
		},						\
		.nfuncs = 8,					\
		.offset = id - MSM7X30_GPIO_BASE_##bank,	\
	}

enum msm7x30_functions {
	MSM_MUX_gpio,
	MSM_MUX_peripheral,
	MSM_MUX_peripheral2,
	MSM_MUX__,
};

static const char * const gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28",
	"gpio29", "gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42",
	"gpio43", "gpio44", "gpio45", "gpio46", "gpio47", "gpio48", "gpio49",
	"gpio50", "gpio51", "gpio52", "gpio53", "gpio54", "gpio55", "gpio56",
	"gpio57", "gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69", "gpio70",
	"gpio71", "gpio72", "gpio73", "gpio74", "gpio75", "gpio76", "gpio77",
	"gpio78", "gpio79", "gpio80", "gpio81", "gpio82", "gpio83", "gpio84",
	"gpio85", "gpio86", "gpio87", "gpio88", "gpio89", "gpio90", "gpio91",
	"gpio92", "gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98",
	"gpio99", "gpio100", "gpio101", "gpio102", "gpio103", "gpio104",
	"gpio105", "gpio106", "gpio107", "gpio108", "gpio109", "gpio110",
	"gpio111", "gpio112", "gpio113", "gpio114", "gpio115", "gpio116",
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121", "gpio122",
	"gpio123", "gpio124", "gpio125", "gpio126", "gpio127", "gpio128",
	"gpio129", "gpio130", "gpio131", "gpio132", "gpio133", "gpio134",
	"gpio135", "gpio136", "gpio137", "gpio138", "gpio139", "gpio140",
	"gpio141", "gpio142", "gpio143", "gpio144", "gpio145", "gpio146",
	"gpio147", "gpio148", "gpio149", "gpio150", "gpio151", "gpio152",
	"gpio153", "gpio154", "gpio155", "gpio156", "gpio157", "gpio158",
	"gpio159", "gpio160", "gpio161", "gpio162", "gpio163", "gpio164",
	"gpio165", "gpio166", "gpio167", "gpio168", "gpio169", "gpio170",
	"gpio171", "gpio172", "gpio173", "gpio174", "gpio175", "gpio176",
	"gpio177", "gpio178", "gpio179", "gpio180", "gpio181"
};

static const char * const peripheral_groups[] = {
	"gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13",
	"gpio14", "gpio15", "gpio19", "gpio20", "gpio21", "gpio22",
	"gpio23", "gpio24", "gpio25", "gpio27", "gpio30", "gpio34", "gpio35",
	"gpio36", "gpio37", "gpio38", "gpio39", "gpio40", "gpio41",
	"gpio42", "gpio43", "gpio45", "gpio46", "gpio47", "gpio48",
	"gpio58", "gpio59", "gpio60", "gpio61", "gpio62", "gpio63",
	"gpio64", "gpio65", "gpio66", "gpio67", "gpio68", "gpio69",
	"gpio70", "gpio71", "gpio86", "gpio90", "gpio91", "gpio92",
	"gpio93", "gpio94", "gpio95", "gpio96", "gpio97", "gpio98",
	"gpio99", "gpio100", "gpio101", "gpio102", "gpio103", "gpio104",
	"gpio105", "gpio106", "gpio107", "gpio108", "gpio109", "gpio110",
	"gpio111", "gpio112", "gpio113", "gpio114", "gpio115", "gpio116",
	"gpio117", "gpio118", "gpio119", "gpio120", "gpio121", "gpio122",
	"gpio123", "gpio124", "gpio125", "gpio126", "gpio127", "gpio128",
	"gpio129", "gpio130", "gpio131", "gpio132", "gpio134", "gpio135",
	"gpio136", "gpio137", "gpio138", "gpio139", "gpio140", "gpio141",
	"gpio142", "gpio143", "gpio144", "gpio145", "gpio146", "gpio160",
	"gpio161", "gpio162", "gpio163", "gpio164", "gpio165", "gpio166",
	"gpio167", "gpio168", "gpio169", "gpio170", "gpio171", "gpio172",
	"gpio173", "gpio174", "gpio175", "gpio176", "gpio177", "gpio178"
};

static const char * const peripheral2_groups[] = {
	"gpio16", "gpio17", "gpio49", "gpio50", "gpio51", "gpio52",
	"gpio88", "gpio115", "gpio172", "gpio173", "gpio174", "gpio175",
	"gpio176", "gpio177", "gpio178"
};

static const struct msm_function_v1 msm7x30_functions[] = {
	FUNCTION(gpio),
	FUNCTION(peripheral),
	FUNCTION(peripheral2)
};

static const struct msm_pingroup_v1 msm7x30_groups[] = {
	PINGROUP(0, 0, _, _, _, _, _, _, _),
	PINGROUP(1, 0, _, _, _, _, _, _, _),
	PINGROUP(2, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(3, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(4, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(5, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(6, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(7, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(8, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(9, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(10, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(11, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(12, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(13, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(14, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(15, 0, peripheral, _, _, _, _, _, _),
	PINGROUP(16, 1, peripheral2, _, _, _, _, _, _),
	PINGROUP(17, 1, peripheral2, _, _, _, _, _, _),
	PINGROUP(18, 1, _, _, _, _, _, _, _),
	PINGROUP(19, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(20, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(21, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(22, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(23, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(24, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(25, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(26, 1, _, _, _, _, _, _, _),
	PINGROUP(27, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(28, 1, _, _, _, _, _, _, _),
	PINGROUP(29, 1, _, _, _, _, _, _, _),
	PINGROUP(30, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(31, 1, _, _, _, _, _, _, _),
	PINGROUP(32, 1, _, _, _, _, _, _, _),
	PINGROUP(33, 1, _, _, _, _, _, _, _),
	PINGROUP(34, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(35, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(36, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(37, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(38, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(39, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(40, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(41, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(42, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(43, 1, peripheral, _, _, _, _, _, _),
	PINGROUP(44, 2, _, _, _, _, _, _, _),
	PINGROUP(45, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(46, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(47, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(48, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(49, 2, peripheral2, _, _, _, _, _, _),
	PINGROUP(50, 2, peripheral2, _, _, _, _, _, _),
	PINGROUP(51, 2, peripheral2, _, _, _, _, _, _),
	PINGROUP(52, 2, peripheral2, _, _, _, _, _, _),
	PINGROUP(53, 2, _, _, _, _, _, _, _),
	PINGROUP(54, 2, _, _, _, _, _, _, _),
	PINGROUP(55, 2, _, _, _, _, _, _, _),
	PINGROUP(56, 2, _, _, _, _, _, _, _),
	PINGROUP(57, 2, _, _, _, _, _, _, _),
	PINGROUP(58, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(59, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(60, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(61, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(62, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(63, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(64, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(65, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(66, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(67, 2, peripheral, _, _, _, _, _, _),
	PINGROUP(68, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(69, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(70, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(71, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(72, 3, _, _, _, _, _, _, _),
	PINGROUP(73, 3, _, _, _, _, _, _, _),
	PINGROUP(74, 3, _, _, _, _, _, _, _),
	PINGROUP(75, 3, _, _, _, _, _, _, _),
	PINGROUP(76, 3, _, _, _, _, _, _, _),
	PINGROUP(77, 3, _, _, _, _, _, _, _),
	PINGROUP(78, 3, _, _, _, _, _, _, _),
	PINGROUP(79, 3, _, _, _, _, _, _, _),
	PINGROUP(80, 3, _, _, _, _, _, _, _),
	PINGROUP(81, 3, _, _, _, _, _, _, _),
	PINGROUP(82, 3, _, _, _, _, _, _, _),
	PINGROUP(83, 3, _, _, _, _, _, _, _),
	PINGROUP(84, 3, _, _, _, _, _, _, _),
	PINGROUP(85, 3, _, _, _, _, _, _, _),
	PINGROUP(86, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(87, 3, _, _, _, _, _, _, _),
	PINGROUP(88, 3, peripheral2, _, _, _, _, _, _),
	PINGROUP(89, 3, _, _, _, _, _, _, _),
	PINGROUP(90, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(91, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(92, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(93, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(94, 3, peripheral, _, _, _, _, _, _),
	PINGROUP(95, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(96, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(97, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(98, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(99, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(100, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(101, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(102, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(103, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(104, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(105, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(106, 4, peripheral, _, _, _, _, _, _),
	PINGROUP(107, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(108, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(109, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(110, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(111, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(112, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(113, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(114, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(115, 5, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(116, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(117, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(118, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(119, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(120, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(121, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(122, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(123, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(124, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(125, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(126, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(127, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(128, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(129, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(130, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(131, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(132, 5, peripheral, _, _, _, _, _, _),
	PINGROUP(133, 5, _, _, _, _, _, _, _),
	PINGROUP(134, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(135, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(136, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(137, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(138, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(139, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(140, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(141, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(142, 6, _, _, _, _, _, _, _),
	PINGROUP(143, 6, _, _, _, _, _, _, _),
	PINGROUP(144, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(145, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(146, 6, peripheral, _, _, _, _, _, _),
	PINGROUP(147, 6, _, _, _, _, _, _, _),
	PINGROUP(148, 6, _, _, _, _, _, _, _),
	PINGROUP(149, 6, _, _, _, _, _, _, _),
	PINGROUP(150, 6, _, _, _, _, _, _, _),
	PINGROUP(151, 7, _, _, _, _, _, _, _),
	PINGROUP(152, 7, _, _, _, _, _, _, _),
	PINGROUP(153, 7, _, _, _, _, _, _, _),
	PINGROUP(154, 7, _, _, _, _, _, _, _),
	PINGROUP(155, 7, _, _, _, _, _, _, _),
	PINGROUP(156, 7, _, _, _, _, _, _, _),
	PINGROUP(157, 7, _, _, _, _, _, _, _),
	PINGROUP(158, 7, _, _, _, _, _, _, _),
	PINGROUP(159, 7, _, _, _, _, _, _, _),
	PINGROUP(160, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(161, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(162, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(163, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(164, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(165, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(166, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(167, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(168, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(169, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(170, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(171, 7, peripheral, _, _, _, _, _, _),
	PINGROUP(172, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(173, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(174, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(175, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(176, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(177, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(178, 7, peripheral, peripheral2, _, _, _, _, _),
	PINGROUP(179, 7, _, _, _, _, _, _, _),
	PINGROUP(180, 7, _, _, _, _, _, _, _),
	PINGROUP(181, 7, _, _, _, _, _, _, _),
};

#define MSM_GPIO_BANK(bank) \
	{ \
		.index = (bank),				\
		.in_reg = MSM7X30_GPIO_IN_##bank,		\
		.out_reg = MSM7X30_GPIO_OUT_##bank,		\
		.int_sts_reg = MSM7X30_GPIO_INT_STATUS_##bank,	\
		.int_clr_reg = MSM7X30_GPIO_INT_CLEAR_##bank,	\
		.int_en_reg = MSM7X30_GPIO_INT_EN_##bank,	\
		.int_edge_reg = MSM7X30_GPIO_INT_EDGE_##bank,	\
		.int_pos_reg = MSM7X30_GPIO_INT_POS_##bank,	\
		.oe_reg = MSM7X30_GPIO_OE_##bank,		\
		.base = MSM7X30_GPIO_BASE_##bank,		\
	}

static const struct msm_pinctrl_v1_bank_data msm7x30_banks[] = {
	MSM_GPIO_BANK(0),
	MSM_GPIO_BANK(1),
	MSM_GPIO_BANK(2),
	MSM_GPIO_BANK(3),
	MSM_GPIO_BANK(4),
	MSM_GPIO_BANK(5),
	MSM_GPIO_BANK(6),
	MSM_GPIO_BANK(7),
};

static const struct msm_pinctrl_v1_soc_data msm7x30_pinctrl = {
	.pins = msm7x30_pins,
	.npins = ARRAY_SIZE(msm7x30_pins),
	.functions = msm7x30_functions,
	.nfunctions = ARRAY_SIZE(msm7x30_functions),
	.groups = msm7x30_groups,
	.ngroups = ARRAY_SIZE(msm7x30_groups),
	.ngpios = ARRAY_SIZE(msm7x30_groups),
	.banks = msm7x30_banks,
	.nbanks = ARRAY_SIZE(msm7x30_banks),
};

static int msm7x30_pinctrl_probe(struct platform_device *pdev)
{
	return msm_pinctrl_v1_probe(pdev, &msm7x30_pinctrl);
}

static const struct of_device_id msm7x30_pinctrl_of_match[] = {
	{ .compatible = "qcom,msm7x30-pinctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, msm7x30_pinctrl_of_match);

static struct platform_driver msm7x30_pinctrl_driver = {
	.driver = {
		.name = "msm7x30-pinctrl",
		.of_match_table = msm7x30_pinctrl_of_match,
	},
	.probe = msm7x30_pinctrl_probe,
};

static int __init msm7x30_pinctrl_init(void)
{
	return platform_driver_register(&msm7x30_pinctrl_driver);
}
arch_initcall(msm7x30_pinctrl_init);

static void __exit msm7x30_pinctrl_exit(void)
{
	platform_driver_unregister(&msm7x30_pinctrl_driver);
}
module_exit(msm7x30_pinctrl_exit);

MODULE_AUTHOR("Rudolf Tammekivi <rtammekivi@gmail.com>");
MODULE_DESCRIPTION("Qualcomm MSM7X30 pinctrl driver");
MODULE_LICENSE("GPL v2");

/*
 * r8a7790 clock framework support
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x1000

#define SMSTPCR1	0xE6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xE615013C
#define SMSTPCR7	0xe615014c
#define SMSTPCR9	0xE6150994
#define SMSTPCR10	0xE6150998

#define SDCKCR		0xE6150074
#define SD2CKCR		0xE6150078
#define SD3CKCR		0xE615007C

static struct clk_mapping cpg_mapping = {
	.phys	= CPG_BASE,
	.len	= CPG_LEN,
};

static unsigned long d12_recalc(struct clk *clk)
{
	return clk->parent->rate / 12;
}

static unsigned long d4_recalc(struct clk *clk)
{
	return clk->parent->rate / 4;
}

static unsigned long d3_recalc(struct clk *clk)
{
	return clk->parent->rate / 3;
}

static unsigned long d2_recalc(struct clk *clk)
{
	return clk->parent->rate / 2;
}

static struct sh_clk_ops d12_clk_ops = {
	.recalc		= d12_recalc,
};

static struct sh_clk_ops d4_clk_ops = {
	.recalc		= d4_recalc,
};

static struct sh_clk_ops d3_clk_ops = {
	.recalc		= d3_recalc,
};

static struct sh_clk_ops d2_clk_ops = {
	.recalc		= d2_recalc,
};

static struct clk extal_clk = {
	.rate		= 30000000,
	.mapping	= &cpg_mapping,
};

static struct clk pll1_clk = {
	.rate		= 3120000000,
	.mapping	= &cpg_mapping,
};

static struct clk pll1_d2_clk = {
	.ops		= &d2_clk_ops,
	.parent		= &pll1_clk,
};

static struct clk pll1_d4_clk = {
	.ops		= &d4_clk_ops,
	.parent		= &pll1_clk,
};

static struct clk zg_clk = {
	.ops		= &d3_clk_ops,
	.parent		= &pll1_d2_clk,
};

static struct clk hp_clk = {
	.ops		= &d12_clk_ops,
	.parent		= &pll1_d2_clk,
};

static struct clk mp_clk = {
	.rate		= 52000000,
	.mapping	= &cpg_mapping,
};

static struct clk cp_clk = {
	.ops		= &d2_clk_ops,
	.parent		= &extal_clk,
};

static struct clk *main_clks[] = {
	&extal_clk,
	&pll1_clk,
	&pll1_d2_clk,
	&pll1_d4_clk,
	&zg_clk,
	&hp_clk,
	&mp_clk,
	&cp_clk,
};

enum {
	SD0, SD1,
	SD01_NR };

enum {
	SD2, SD3,
	SD23_NR };

static int sd01_divisors[] = { 0, 0, 0, 0, 0, 12, 16, 18, 24, 0, 36, 48, 10};

static struct clk_div_mult_table sd01_div_mult_table = {
	.divisors = sd01_divisors,
	.nr_divisors = ARRAY_SIZE(sd01_divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &sd01_div_mult_table,
};

#define SD_DIV(_parent, _reg, _shift, _flags)			\
{								\
	.parent = _parent,					\
	.enable_reg = (void __iomem *)_reg,			\
	.enable_bit = _shift,					\
	.div_mask = SH_CLK_DIV_MSK(4),				\
	.flags = _flags,					\
}

static struct clk sd01_clks[SD01_NR] = {
	[SD0] = SD_DIV(&pll1_d2_clk, SDCKCR, 4, 0),
	[SD1] = SD_DIV(&pll1_d2_clk, SDCKCR, 0, 0),
};

static struct clk sd23_clks[SD23_NR] = {
	[SD2] = SH_CLK_DIV6(&pll1_d4_clk, SD2CKCR, 0),
	[SD3] = SH_CLK_DIV6(&pll1_d4_clk, SD3CKCR, 0),
};

enum {
	MSTP112,
	MSTP216, MSTP207, MSTP206, MSTP204, MSTP203, MSTP202,
	MSTP726, MSTP725, MSTP724, MSTP723, MSTP721, MSTP720, MSTP704, MSTP703,
	MSTP314, MSTP313, MSTP312, MSTP311,
	MSTP929, MSTP922,
	MSTP1031, MSTP1030, MSTP1019, MSTP1018, MSTP1017, MSTP1015, \
	MSTP1014, MSTP1005,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP112] = SH_CLK_MSTP32(&zg_clk, SMSTPCR1, 12, 0),
	[MSTP216] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 16, 0),
	[MSTP207] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 7, 0),
	[MSTP206] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 6, 0),
	[MSTP204] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 4, 0),
	[MSTP203] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 3, 0),
	[MSTP202] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 2, 0),
	[MSTP726] = SH_CLK_MSTP32(&zg_clk, SMSTPCR7, 26, 0),
	[MSTP725] = SH_CLK_MSTP32(&zg_clk, SMSTPCR7, 25, 0),
	[MSTP314] = SH_CLK_MSTP32(&sd01_clks[SD0], SMSTPCR3, 14, 0),
	[MSTP313] = SH_CLK_MSTP32(&sd01_clks[SD1], SMSTPCR3, 13, 0),
	[MSTP312] = SH_CLK_MSTP32(&sd23_clks[SD2], SMSTPCR3, 12, 0),
	[MSTP311] = SH_CLK_MSTP32(&sd23_clks[SD3], SMSTPCR3, 11, 0),
	[MSTP724] = SH_CLK_MSTP32(&zg_clk, SMSTPCR7, 24, 0),
	[MSTP723] = SH_CLK_MSTP32(&zg_clk, SMSTPCR7, 23, 0),
	[MSTP721] = SH_CLK_MSTP32(&cp_clk, SMSTPCR7, 21, 0),
	[MSTP720] = SH_CLK_MSTP32(&cp_clk, SMSTPCR7, 20, 0),
	[MSTP704] = SH_CLK_MSTP32(&mp_clk, SMSTPCR7, 04, 0),
	[MSTP703] = SH_CLK_MSTP32(&mp_clk, SMSTPCR7, 03, 0),
	[MSTP929] = SH_CLK_MSTP32(&hp_clk, SMSTPCR9, 29, 0),
	[MSTP922] = SH_CLK_MSTP32(&hp_clk, SMSTPCR9, 22, 0),
	[MSTP1031] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 31, 0),
	[MSTP1030] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 30, 0),
	[MSTP1019] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 19, 0),
	[MSTP1018] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 18, 0),
	[MSTP1017] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 17, 0),
	[MSTP1015] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 15, 0),
	[MSTP1014] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 14, 0),
	[MSTP1005] = SH_CLK_MSTP32(&hp_clk, SMSTPCR10, 05, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extal_clk", &extal_clk),
	CLKDEV_CON_ID("pll1_clk", &pll1_clk),
	CLKDEV_CON_ID("pll1_div2_clk", &pll1_d2_clk),
	CLKDEV_CON_ID("pll1_div4_clk", &pll1_d4_clk),
	CLKDEV_CON_ID("zg_clk", &zg_clk),
	CLKDEV_CON_ID("hp_clk", &hp_clk),
	CLKDEV_CON_ID("mp_clk", &mp_clk),
	CLKDEV_CON_ID("cp_clk", &cp_clk),
	CLKDEV_CON_ID("peripheral_clk", &hp_clk),

	CLKDEV_DEV_ID("pvrsrvkm", &mstp_clks[MSTP112]),
	CLKDEV_CON_ID("g6400", &mstp_clks[MSTP112]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.3", &mstp_clks[MSTP311]),
	CLKDEV_DEV_ID("rcar-du.0", &mstp_clks[MSTP724]),
	CLKDEV_CON_ID("rcar-du.1", &mstp_clks[MSTP723]),
	CLKDEV_CON_ID("lvds.0", &mstp_clks[MSTP726]),
	CLKDEV_CON_ID("lvds.1", &mstp_clks[MSTP725]),
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP202]),
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP721]),
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP720]),
	CLKDEV_CON_ID("hs_usb", &mstp_clks[MSTP704]),
	CLKDEV_CON_ID("usb_fck", &mstp_clks[MSTP703]),
	CLKDEV_DEV_ID("i2c-rcar.2", &mstp_clks[MSTP929]),
	CLKDEV_DEV_ID("adg", &mstp_clks[MSTP922]),
	CLKDEV_DEV_ID("src0", &mstp_clks[MSTP1031]),
	CLKDEV_DEV_ID("src1", &mstp_clks[MSTP1030]),
	CLKDEV_DEV_ID("dvc0", &mstp_clks[MSTP1019]),
	CLKDEV_DEV_ID("dvc1", &mstp_clks[MSTP1018]),
	CLKDEV_DEV_ID("scu-pcm-audio.0", &mstp_clks[MSTP1017]),
	CLKDEV_DEV_ID("ssi0", &mstp_clks[MSTP1015]),
	CLKDEV_DEV_ID("ssi1", &mstp_clks[MSTP1014]),
	CLKDEV_DEV_ID("ssi", &mstp_clks[MSTP1005]),
};

static void __init r8a7790_rgx_control_init(void)
{
	void __iomem *cpgp;
	unsigned int val;

#define RGXCR		0x0B4

	cpgp = ioremap(CPG_BASE, PAGE_SIZE);
	val = ioread32(cpgp + RGXCR);
	iowrite32(val | (1 << 16), cpgp + RGXCR);
	iounmap(cpgp);
}

void __init r8a7790_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(sd01_clks, SD01_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_reparent_register(sd23_clks, SD23_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7790 clocks\n");

	r8a7790_rgx_control_init();
}

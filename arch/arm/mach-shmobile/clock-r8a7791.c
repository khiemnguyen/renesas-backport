/*
 * r8a7791 clock framework support
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
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
#include <linux/delay.h>
#include <mach/clock.h>
#include <mach/common.h>
#include <mach/r8a7791.h>

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)		*1	*1
 *---------------------------------------------------
 * 0  0  0	15 x 1		x172/2	x208/2	x106
 * 0  0  1	15 x 1		x172/2	x208/2	x88
 * 0  1  0	20 x 1		x130/2	x156/2	x80
 * 0  1  1	20 x 1		x130/2	x156/2	x66
 * 1  0  0	26 / 2		x200/2	x240/2	x122
 * 1  0  1	26 / 2		x200/2	x240/2	x102
 * 1  1  0	30 / 2		x172/2	x208/2	x106
 * 1  1  1	30 / 2		x172/2	x208/2	x88
 *
 * *1 :	Table 7.6 indicates VCO ouput (PLLx = VCO/2)
 *	see "p1 / 2" on R8A7791_CLOCK_ROOT() below
 */

static void __iomem *r8a7791_cpg_base;

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x1000

#define MSTPSR0		0xe6150030
#define MSTPSR1		0xe6150038
#define MSTPSR2		0xe6150040
#define MSTPSR3		0xe6150048
#define MSTPSR4		0xe615004c
#define MSTPSR5		0xe615003c
#define MSTPSR6		0xe61501c0
#define MSTPSR7		0xe61501c4
#define MSTPSR8		0xe61509a0
#define MSTPSR9		0xe61509a4
#define MSTPSR10	0xe61509a8
#define MSTPSR11	0xe61509ac
#define SMSTPCR1	0xe6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xe615013c
#define SMSTPCR4	0xe6150140
#define SMSTPCR5	0xe6150144
#define SMSTPCR6	0xe6150148
#define SMSTPCR7	0xe615014c
#define SMSTPCR8	0xe6150990
#define SMSTPCR9	0xe6150994
#define SMSTPCR10	0xe6150998

#define MODEMR		0xe6160060
#define SDCKCR		0xe6150074
#define SD1CKCR		0xe6150078
#define SD2CKCR		0xe615026c
#define MMC0CKCR	0xe6150240
#define SSPCKCR		0xe6150248
#define SSPRSCKCR	0xe615024c

static struct clk_mapping cpg_mapping = {
	.phys   = CPG_BASE,
	.len    = CPG_LEN,
};

static struct clk extal_clk = {
	/* .rate will be updated on r8a7791_clock_init() */
	.mapping	= &cpg_mapping,
};

static struct sh_clk_ops followparent_clk_ops = {
	.recalc	= followparent_recalc,
};

static struct clk main_clk = {
	/* .parent will be set r8a73a4_clock_init */
	.ops	= &followparent_clk_ops,
};

/*
 * clock ratio of these clock will be updated
 * on r8a7791_clock_init()
 */
SH_FIXED_RATIO_CLK_SET(pll1_clk,		main_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(pll3_clk,		main_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(qspi_clk,		pll1_clk,	1, 1);

/* fixed ratio clock */
SH_FIXED_RATIO_CLK_SET(extal_div2_clk,		extal_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(cp_clk,			extal_clk,	1, 2);

SH_FIXED_RATIO_CLK_SET(pll1_div2_clk,		pll1_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(zg_clk,			pll1_clk,	1, 3);
SH_FIXED_RATIO_CLK_SET(zs_clk,			pll1_clk,	1, 6);
SH_FIXED_RATIO_CLK_SET(hp_clk,			pll1_clk,	1, 12);
SH_FIXED_RATIO_CLK_SET(p_clk,			pll1_clk,	1, 24);

SH_FIXED_RATIO_CLK_SET(mp_clk,			pll1_div2_clk,	1, 15);
SH_FIXED_RATIO_CLK_SET(zx_clk,			pll1_clk,	1, 3);

static struct clk sata0_clk = {
	.rate		= 100000000,
	.mapping	= &cpg_mapping,
};

static struct clk sata1_clk = {
	.rate		= 100000000,
	.mapping	= &cpg_mapping,
};

static struct clk *main_clks[] = {
	&extal_clk,
	&extal_div2_clk,
	&main_clk,
	&pll1_clk,
	&pll1_div2_clk,
	&pll3_clk,
	&zg_clk,
	&zs_clk,
	&qspi_clk,
	&hp_clk,
	&p_clk,
	&mp_clk,
	&cp_clk,
	&sata0_clk,
	&sata1_clk,
	&zx_clk,
};

/* SDHI (DIV4) clock */
static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 18, 24, 0, 36, 48, 10 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum {
	DIV4_SD0, DIV4_NR
};

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_SD0] = SH_CLK_DIV4(&pll1_clk, SDCKCR, 4, 0x1de0,
						CLK_ENABLE_ON_INIT),
};

/* DIV6 clocks */
enum {
	DIV6_MMC,
	DIV6_SD1, DIV6_SD2,
	DIV6_NR
};

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_MMC]	= SH_CLK_DIV6(&pll1_div2_clk, MMC0CKCR, 0),
	[DIV6_SD1]	= SH_CLK_DIV6(&pll1_div2_clk, SD1CKCR, 0),
	[DIV6_SD2]	= SH_CLK_DIV6(&pll1_div2_clk, SD2CKCR, 0),
};


/* MSTP */
enum {
	MSTP721, MSTP720,
	MSTP1031, MSTP1030, MSTP1019, MSTP1018, MSTP1017, MSTP1015, MSTP1014,
	MSTP1005, MSTP922,
	MSTP931, MSTP930, MSTP929, MSTP928, MSTP927, MSTP925,
	MSTP926,
	MSTP917,
	MSTP815, MSTP814,
	MSTP813,
	MSTP811, MSTP810, MSTP809,
	MSTP726, MSTP724, MSTP723,
	MSTP704, MSTP703, MSTP328,
	MSTP502, MSTP501, MSTP219, MSTP218,
	MSTP330,
	MSTP315, MSTP314, MSTP312, MSTP311,
	MSTP208, MSTP205, MSTP000,
	MSTP131, MSTP119, MSTP118, MSTP115, MSTP109, MSTP103, MSTP101,
	MSTP128, MSTP127,
	MSTP112,
	MSTP_NR
};

#define MSTP(_reg, _bit, _parent, _flags) \
	SH_CLK_MSTP32_STS(_parent, SMSTPCR##_reg, _bit, (void __iomem *)MSTPSR##_reg, _flags)

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP1031] = MSTP(10, 31, &hp_clk, 0), /* SCU (SRC0) */
	[MSTP1030] = MSTP(10, 30, &hp_clk, 0), /* SCU (SRC1) */
	[MSTP1019] = MSTP(10, 19, &hp_clk, 0), /* SCU (DVC0) */
	[MSTP1018] = MSTP(10, 18, &hp_clk, 0), /* SCU (DVC1) */
	[MSTP1017] = MSTP(10, 17, &hp_clk, 0), /* SCU (ALL) */
	[MSTP1015] = MSTP(10, 15, &hp_clk, 0), /* SSI0 */
	[MSTP1014] = MSTP(10, 14, &hp_clk, 0), /* SSI1 */
	[MSTP1005] = MSTP(10,  5, &hp_clk, 0), /* SSI (ALL) */
	[MSTP931] = MSTP(9, 31, &hp_clk, 0), /* I2C0 */
	[MSTP930] = MSTP(9, 30, &hp_clk, 0), /* I2C1 */
	[MSTP929] = MSTP(9, 29, &hp_clk, 0), /* I2C2 */
	[MSTP928] = MSTP(9, 28, &hp_clk, 0), /* I2C3 */
	[MSTP927] = MSTP(9, 27, &hp_clk, 0), /* I2C4 */
	[MSTP926] = MSTP(9, 26, &cp_clk, 0), /* IIC3 */
	[MSTP925] = MSTP(9, 25, &hp_clk, 0), /* I2C5 */
	[MSTP922] = MSTP(9, 22, &hp_clk, 0), /* ADG */
	[MSTP917] = MSTP(9, 17, &qspi_clk, 0), /* QSPI */
	[MSTP815] = MSTP(8, 15, &sata0_clk, 0), /* SATA0 */
	[MSTP814] = MSTP(8, 14, &sata1_clk, 0), /* SATA1 */
	[MSTP813] = MSTP(8, 13, &p_clk, 0), /* Ether */
	[MSTP811] = MSTP(8, 11, &zs_clk, 0), /* VIN0 */
	[MSTP810] = MSTP(8, 10, &zs_clk, 0), /* VIN1 */
	[MSTP809] = MSTP(8,  9, &zs_clk, 0), /* VIN2 */
	[MSTP726] = MSTP(7, 26, &zx_clk, 0), /* LVDS0 */
	[MSTP724] = MSTP(7, 24, &zx_clk, 0), /* DU0 */
	[MSTP723] = MSTP(7, 23, &zx_clk, 0), /* DU1 */
	[MSTP721] = MSTP(7, 21, &p_clk, 0), /* SCIF0 */
	[MSTP720] = MSTP(7, 20, &p_clk, 0), /* SCIF1 */
#ifdef CONFIG_USB_R8A66597
	[MSTP704] = MSTP(7,  4, &hp_clk, 0), /* HSUSB */
#else
	[MSTP704] = MSTP(7,  4, &mp_clk, 0), /* HSUSB */
#endif
	[MSTP703] = MSTP(7,  3, &mp_clk, 0), /* EHCI */
	[MSTP502] = MSTP(5,  2, &hp_clk, 0), /* MPDMAC0 */
	[MSTP501] = MSTP(5,  1, &hp_clk, 0), /* MPDMAC1 */
	[MSTP330] = MSTP(3, 30, &hp_clk, 0), /* USBDMAC0 */
	[MSTP328] = MSTP(3, 28, &mp_clk, 0), /* SSUSB */
	[MSTP315] = MSTP(3, 15, &div6_clks[DIV6_MMC], 0), /* MMCIF */
	[MSTP314] = MSTP(3, 14, &div4_clks[DIV4_SD0], 0), /* SDHI0 */
	[MSTP312] = MSTP(3, 12, &div6_clks[DIV6_SD1], 0), /* SDHI1 */
	[MSTP311] = MSTP(3, 11, &div6_clks[DIV6_SD2], 0), /* SDHI2 */
	[MSTP219] = MSTP(2, 19, &hp_clk, 0), /* SYS-DMAC (lower) */
	[MSTP218] = MSTP(2, 18, &hp_clk, 0), /* SYS-DMAC (upper) */
	[MSTP208] = MSTP(2,  8, &mp_clk, 0), /* MSIOF1 */
	[MSTP205] = MSTP(2,  5, &mp_clk, 0), /* MSIOF2 */
	[MSTP131] = MSTP(1, 31, &zs_clk, 0), /* VSPS */
	[MSTP128] = MSTP(1, 28, &zs_clk, 0), /* VSP1 (DU0) */
	[MSTP127] = MSTP(1, 27, &zs_clk, 0), /* VSP1 (DU1) */
	[MSTP119] = MSTP(1, 19, &zs_clk, 0), /* FDP0 */
	[MSTP118] = MSTP(1, 18, &zs_clk, 0), /* FDP1 */
	[MSTP115] = MSTP(1, 15, &zs_clk, 0), /* 2DDMAC */
	[MSTP112] = MSTP(1, 12, &zg_clk, 0), /* 3DG */
	[MSTP109] = MSTP(1, 9, &zs_clk, 0),  /* SSP */
	[MSTP103] = MSTP(1, 3, &zs_clk, 0),  /* VPC0 */
	[MSTP101] = MSTP(1, 1, &zs_clk, 0),  /* VCP0 */
	[MSTP000] = MSTP(0, 0, &mp_clk, 0), /* MSIOF0 */
};

static struct clk_lookup lookups[] = {

	/* main clocks */
	CLKDEV_CON_ID("extal",		&extal_clk),
	CLKDEV_CON_ID("extal_div2",	&extal_div2_clk),
	CLKDEV_CON_ID("main",		&main_clk),
	CLKDEV_CON_ID("pll1",		&pll1_clk),
	CLKDEV_CON_ID("pll1_div2",	&pll1_div2_clk),
	CLKDEV_CON_ID("pll3",		&pll3_clk),
	CLKDEV_CON_ID("zx",		&zx_clk),
	CLKDEV_CON_ID("zs",		&zs_clk),
	CLKDEV_CON_ID("hp",		&hp_clk),
	CLKDEV_CON_ID("p",		&p_clk),
	CLKDEV_CON_ID("mp",		&mp_clk),
	CLKDEV_CON_ID("qspi",           &qspi_clk),
	CLKDEV_CON_ID("cp",		&cp_clk),
	CLKDEV_CON_ID("peripheral_clk", &hp_clk),

	/* DIV4 */
	CLKDEV_CON_ID("sdhi0", &div4_clks[DIV4_SD0]),

	/* DIV6 */
	CLKDEV_CON_ID("mmc.0", &div6_clks[DIV6_MMC]),
	CLKDEV_CON_ID("sdhi1", &div6_clks[DIV6_SD1]),
	CLKDEV_CON_ID("sdhi2", &div6_clks[DIV6_SD2]),

	/* MSTP */
	CLKDEV_ICK_ID("lvds.0", "rcar-du-r8a7791", &mstp_clks[MSTP726]),
	CLKDEV_ICK_ID("du.0", "rcar-du-r8a7791", &mstp_clks[MSTP724]),
	CLKDEV_ICK_ID("du.1", "rcar-du-r8a7791", &mstp_clks[MSTP723]),
	CLKDEV_DEV_ID("r8a779x-ether", &mstp_clks[MSTP813]),
	CLKDEV_DEV_ID("ee200000.mmcif", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP315]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP311]),
	CLKDEV_CON_ID("vsps", &mstp_clks[MSTP131]),
	CLKDEV_DEV_ID("vsp1.2", &mstp_clks[MSTP128]),
	CLKDEV_DEV_ID("vsp1.3", &mstp_clks[MSTP127]),
	CLKDEV_CON_ID("src0", &mstp_clks[MSTP1031]),
	CLKDEV_CON_ID("src1", &mstp_clks[MSTP1030]),
	CLKDEV_CON_ID("dvc0", &mstp_clks[MSTP1019]),
	CLKDEV_CON_ID("dvc1", &mstp_clks[MSTP1018]),
	CLKDEV_CON_ID("scu", &mstp_clks[MSTP1017]),
	CLKDEV_CON_ID("ssi0", &mstp_clks[MSTP1015]),
	CLKDEV_CON_ID("ssi1", &mstp_clks[MSTP1014]),
	CLKDEV_CON_ID("ssi", &mstp_clks[MSTP1005]),
	CLKDEV_CON_ID("adg", &mstp_clks[MSTP922]),
	CLKDEV_DEV_ID("i2c-rcar.0", &mstp_clks[MSTP931]),
	CLKDEV_DEV_ID("i2c-rcar.1", &mstp_clks[MSTP930]),
	CLKDEV_DEV_ID("i2c-rcar.2", &mstp_clks[MSTP929]),
	CLKDEV_DEV_ID("i2c-rcar.3", &mstp_clks[MSTP928]),
	CLKDEV_DEV_ID("i2c-rcar.4", &mstp_clks[MSTP927]),
	CLKDEV_DEV_ID("i2c-rcar.5", &mstp_clks[MSTP925]),
	CLKDEV_DEV_ID("i2c-sh_mobile.6", &mstp_clks[MSTP926]),
	CLKDEV_DEV_ID("qspi.0", &mstp_clks[MSTP917]),
	CLKDEV_DEV_ID("sata_rcar.0", &mstp_clks[MSTP815]),
	CLKDEV_DEV_ID("sata_rcar.1", &mstp_clks[MSTP814]),
	CLKDEV_DEV_ID("vin.0", &mstp_clks[MSTP811]),
	CLKDEV_DEV_ID("vin.1", &mstp_clks[MSTP810]),
	CLKDEV_DEV_ID("vin.2", &mstp_clks[MSTP809]),
	CLKDEV_CON_ID("hs_usb", &mstp_clks[MSTP704]), /* HSUSB */
	CLKDEV_DEV_ID("r8a66597_udc.0", &mstp_clks[MSTP704]),
	CLKDEV_CON_ID("usb_fck", &mstp_clks[MSTP703]), /* ECHI */
	CLKDEV_CON_ID("usb0_dmac", &mstp_clks[MSTP330]),
	CLKDEV_CON_ID("ss_usb", &mstp_clks[MSTP328]), /* SSUSB */
	CLKDEV_CON_ID("audmac_lo", &mstp_clks[MSTP502]),
	CLKDEV_CON_ID("audmac_up", &mstp_clks[MSTP501]),
	CLKDEV_CON_ID("sysdmac_lo", &mstp_clks[MSTP219]),
	CLKDEV_CON_ID("sysdmac_up", &mstp_clks[MSTP218]),
	CLKDEV_DEV_ID("spi_sh_msiof.2", &mstp_clks[MSTP208]),
	CLKDEV_DEV_ID("spi_sh_msiof.3", &mstp_clks[MSTP205]),
	CLKDEV_DEV_ID("spi_sh_msiof.1", &mstp_clks[MSTP000]),
	CLKDEV_CON_ID("fdp0", &mstp_clks[MSTP119]),
	CLKDEV_CON_ID("fdp1", &mstp_clks[MSTP118]),
	CLKDEV_CON_ID("tddmac", &mstp_clks[MSTP115]),
	CLKDEV_DEV_ID("ssp_dev", &mstp_clks[MSTP109]),
	CLKDEV_CON_ID("vpc0", &mstp_clks[MSTP103]),
	CLKDEV_CON_ID("vcp0", &mstp_clks[MSTP101]),
	CLKDEV_DEV_ID("pvrsrvkm", &mstp_clks[MSTP112]),
};

#define R8A7791_CLOCK_ROOT(e, m, p0, p1, p30, p31)		\
	extal_clk.rate	= e * 1000 * 1000;			\
	main_clk.parent	= m;					\
	SH_CLK_SET_RATIO(&pll1_clk_ratio, p1 / 2, 1);		\
	if (mode & MD(19))					\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p31, 1);	\
	else							\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p30, 1)


static void __init r8a7791_sdhi_clock_init(void)
{
	int ret = 0;
	struct clk *sdhi_clk;

	/* set SDHI0 clock to 156 MHz */
	sdhi_clk = clk_get(NULL, "sdhi0");
	if (IS_ERR(sdhi_clk)) {
		pr_err("Cannot get sdhi0 clock\n");
		goto sdhi0_out;
	}
	ret = clk_set_rate(sdhi_clk, 156000000);
	if (ret < 0)
		pr_err("Cannot set sdhi0 clock rate :%d\n", ret);

	clk_put(sdhi_clk);
sdhi0_out:

	/* set SDHI1 clock to 97.5 MHz */
	sdhi_clk = clk_get(NULL, "sdhi1");
	if (IS_ERR(sdhi_clk)) {
		pr_err("Cannot get sdhi1 clock\n");
		goto sdhi1_out;
	}
	ret = clk_set_rate(sdhi_clk, 97500000);
	if (ret < 0)
		pr_err("Cannot set sdhi1 clock rate :%d\n", ret);

	clk_put(sdhi_clk);
sdhi1_out:

	/* set SDHI2 clock to 97.5 MHz */
	sdhi_clk = clk_get(NULL, "sdhi2");
	if (IS_ERR(sdhi_clk)) {
		pr_err("Cannot get sdhi2 clock\n");
		goto sdhi2_out;
	}
	ret = clk_set_rate(sdhi_clk, 97500000);
	if (ret < 0)
		pr_err("Cannot set sdhi2 clock rate :%d\n", ret);

	clk_put(sdhi_clk);
sdhi2_out:

	return;
}

static void __init r8a7791_mmc_clock_init(void)
{
	int ret = 0;
	struct clk *mmc_clk;

	/* set MMC0 clock to 97.5 MHz */
	mmc_clk = clk_get(NULL, "mmc.0");
	if (IS_ERR(mmc_clk)) {
		pr_err("Cannot get mmc.0 clock\n");
		goto mmc0_out;
	}
	ret = clk_set_rate(mmc_clk, 97500000);
	if (ret < 0)
		pr_err("Cannot set mmc.0 clock rate :%d\n", ret);

	clk_put(mmc_clk);
mmc0_out:

	return;
}

void __init r8a7791_clock_init(void)
{
	u32 mode = rcar_gen2_read_mode_pins();
	int k, ret = 0;

	r8a7791_cpg_base = ioremap(CPG_BASE, CPG_LEN);

	switch (mode & (MD(14) | MD(13))) {
	case 0:
		R8A7791_CLOCK_ROOT(15, &extal_clk, 172, 208, 106, 88);
		break;
	case MD(13):
		R8A7791_CLOCK_ROOT(20, &extal_clk, 130, 156, 80, 66);
		break;
	case MD(14):
		R8A7791_CLOCK_ROOT(26, &extal_div2_clk, 200, 240, 122, 102);
		break;
	case MD(13) | MD(14):
		R8A7791_CLOCK_ROOT(30, &extal_div2_clk, 172, 208, 106, 88);
		break;
	}

	if ((mode & (MD(3) | MD(2) | MD(1))) == MD(2))
		SH_CLK_SET_RATIO(&qspi_clk_ratio, 1, 16);
	else
		SH_CLK_SET_RATIO(&qspi_clk_ratio, 1, 20);

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		goto epanic;

	r8a7791_sdhi_clock_init();
	r8a7791_mmc_clock_init();

	return;

epanic:
	panic("failed to setup r8a7791 clocks\n");
}

/* Software Reset */
#define SRCR0		0x00a0
#define SRCR1		0x00a8
#define SRCR2		0x00b0
#define SRCR3		0x00b8
#define SRCR4		0x00bc
#define SRCR5		0x00c4
#define SRCR6		0x01c8
#define SRCR7		0x01cc
#define SRCR8		0x0920
#define SRCR9		0x0924
#define SRCR10		0x0928
#define SRCR11		0x092c
#define SRSTCLR0	0x0940
#define SRSTCLR1	0x0944
#define SRSTCLR2	0x0948
#define SRSTCLR3	0x094c
#define SRSTCLR4	0x0950
#define SRSTCLR5	0x0954
#define SRSTCLR6	0x0958
#define SRSTCLR7	0x095c
#define SRSTCLR8	0x0960
#define SRSTCLR9	0x0964
#define SRSTCLR10	0x0968
#define SRSTCLR11	0x096c

#define SRST_REG(n)	[n] = { .srcr = SRCR##n, .srstclr = SRSTCLR##n, }

static struct software_reset_reg {
	u16	srcr;
	u16	srstclr;
} r8a7791_reset_regs[] = {
	SRST_REG(0),
	SRST_REG(1),
	SRST_REG(2),
	SRST_REG(3),
	SRST_REG(4),
	SRST_REG(5),
	SRST_REG(6),
	SRST_REG(7),
	SRST_REG(8),
	SRST_REG(9),
	SRST_REG(10),
	SRST_REG(11),
};

static DEFINE_SPINLOCK(r8a7791_reset_lock);

void r8a7791_module_reset(unsigned int n, u32 bits, int usecs)
{
	void __iomem *base = r8a7791_cpg_base;
	unsigned long flags;
	u32 srcr;

	if (n >= ARRAY_SIZE(r8a7791_reset_regs)) {
		pr_err("SRCR%u is not available\n", n);
		return;
	}

	if (usecs <= 0)
		usecs = 50; /* give a short delay for at least one RCLK cycle */

	spin_lock_irqsave(&r8a7791_reset_lock, flags);
	srcr = readl_relaxed(base + r8a7791_reset_regs[n].srcr);
	writel_relaxed(srcr | bits, base + r8a7791_reset_regs[n].srcr);
	readl_relaxed(base + r8a7791_reset_regs[n].srcr); /* sync */
	spin_unlock_irqrestore(&r8a7791_reset_lock, flags);

	udelay(usecs);

	writel_relaxed(bits, base + r8a7791_reset_regs[n].srstclr);
	readl_relaxed(base + r8a7791_reset_regs[n].srstclr); /* sync */
}

/*
 * r8a7791 processor support
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

#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/serial_sci.h>
#include <linux/sh_eth.h>
#include <linux/i2c/i2c-rcar.h>
#include <linux/sh_dma-desc.h>
#include <linux/dma-mapping.h>
#include <linux/sh_audma-pp.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/clk.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/dma-mapping.h>
#include <linux/spi/sh_msiof.h>
#include <media/vin.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <mach/dma-register.h>
#include <asm/mach/arch.h>

static const struct resource pfc_resources[] = {
	DEFINE_RES_MEM(0xe6060000, 0x250),
};

#define R8A7791_GPIO(idx)						\
static struct resource r8a7791_gpio##idx##_resources[] = {		\
	DEFINE_RES_MEM(0xe6050000 + 0x1000 * (idx), 0x50),		\
	DEFINE_RES_IRQ(gic_spi(4 + (idx))),				\
};									\
									\
static struct gpio_rcar_config r8a7791_gpio##idx##_platform_data = {	\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= 32,						\
	.pctl_name	= "pfc-r8a7791",				\
	.has_both_edge_trigger = 1,					\
};									\

#define R8A7791_GPIO2(idx)						\
static struct resource r8a7791_gpio##idx##_resources[] = {		\
	DEFINE_RES_MEM(0xe6055400 + 0x400 * (idx - 6), 0x50),		\
	DEFINE_RES_IRQ(gic_spi(4 + (idx))),				\
};									\
									\
static struct gpio_rcar_config r8a7791_gpio##idx##_platform_data = {	\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= 32,						\
	.pctl_name	= "pfc-r8a7791",				\
	.has_both_edge_trigger = 1,					\
};									\

R8A7791_GPIO(0);
R8A7791_GPIO(1);
R8A7791_GPIO(2);
R8A7791_GPIO(3);
R8A7791_GPIO(4);
R8A7791_GPIO(5);
R8A7791_GPIO2(6);
R8A7791_GPIO2(7);

#define r8a7791_register_gpio(idx)					\
	platform_device_register_resndata(&platform_bus, "gpio_rcar", idx, \
		r8a7791_gpio##idx##_resources,				\
		ARRAY_SIZE(r8a7791_gpio##idx##_resources),		\
		&r8a7791_gpio##idx##_platform_data,			\
		sizeof(r8a7791_gpio##idx##_platform_data))

void __init r8a7791_pinmux_init(void)
{
	platform_device_register_simple("pfc-r8a7791", -1, pfc_resources,
					ARRAY_SIZE(pfc_resources));
	r8a7791_register_gpio(0);
	r8a7791_register_gpio(1);
	r8a7791_register_gpio(2);
	r8a7791_register_gpio(3);
	r8a7791_register_gpio(4);
	r8a7791_register_gpio(5);
	r8a7791_register_gpio(6);
	r8a7791_register_gpio(7);
}

#define SCIF_COMMON(scif_type, baseaddr, irq)			\
	.type		= scif_type,				\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.irqs		= SCIx_IRQ_MUXED(irq)

#define SCIFA_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIFA, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_4,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

#define SCIFB_DATA(index, baseaddr, irq)	\
[index] = {					\
	SCIF_COMMON(PORT_SCIFB, baseaddr, irq),	\
	.scbrr_algo_id	= SCBRR_ALGO_4,		\
	.scscr = SCSCR_RE | SCSCR_TE,		\
}

#define SCIF_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIF, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_2,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

#define HSCIF_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_HSCIF, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_6,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

enum { SCIF0 = 6, SCIF1, };

static const struct plat_sci_port scif[] = {
	SCIF_DATA(SCIF0, 0xe6e60000, gic_spi(152)), /* SCIF0 */
	SCIF_DATA(SCIF1, 0xe6e68000, gic_spi(153)), /* SCIF1 */
};

static inline void r8a7791_register_scif(int idx)
{
	platform_device_register_data(&platform_bus, "sh-sci", idx, &scif[idx],
				      sizeof(struct plat_sci_port));
}

static struct resource eth_resources[] = {
	{
		.start  = 0xee700200,
		.end    = 0xee7003fc,
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(162),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data eth_platform_data = {
	.phy = 0x1,
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
	.register_type = SH_ETH_REG_FAST_SH4,
	.phy_interface = PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low = 1,
};

static struct platform_device eth_device = {
	.name = "sh-eth",
	.id	= 0,
	.dev = {
		.platform_data = &eth_platform_data,
	},
	.num_resources = ARRAY_SIZE(eth_resources),
	.resource = eth_resources,
};

/* I2C */
static struct i2c_rcar_platform_data i2c_pd[] = {
	{
		.bus_speed	= 400000,
		.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,
	}, {
		.bus_speed	= 400000,
		.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,
	}, {
		/* Recommended values of bus speed 100kHz by H2 H/W spec. */
		.icccr	= 6,
		.icccr2	= 7,
		.icmpr	= 10,
		.ichpr	= 632,
		.iclpr	= 640,
	}, {
		.bus_speed	= 400000,
		.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,
	}, {
		.bus_speed	= 400000,
		.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,
	}, {
		.bus_speed	= 400000,
		.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,
	},
};

static struct resource rcar_i2c0_res[] = {
	{
		.start  = 0xe6508000,
		.end    = (0xe6510000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(287),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rcar_i2c1_res[] = {
	{
		.start  = 0xe6518000,
		.end    = (0xe6520000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(288),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rcar_i2c2_res[] = {
	{
		.start  = 0xe6530000,
		.end    = (0xe6538000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(286),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rcar_i2c3_res[] = {
	{
		.start  = 0xe6540000,
		.end    = (0xe6548000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(290),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rcar_i2c4_res[] = {
	{
		.start  = 0xe6520000,
		.end    = (0xe6528000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(19),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct resource rcar_i2c5_res[] = {
	{
		.start  = 0xe6528000,
		.end    = (0xe6530000 - 1),
		.flags  = IORESOURCE_MEM,
	}, {
		.start  = gic_spi(20),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device i2c0_device = {
	.name		= "i2c-rcar",
	.id		= 0,
	.dev = {
		.platform_data = &i2c_pd[0],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c0_res),
	.resource	= rcar_i2c0_res,
};

static struct platform_device i2c1_device = {
	.name		= "i2c-rcar",
	.id		= 1,
	.dev = {
		.platform_data = &i2c_pd[1],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c1_res),
	.resource	= rcar_i2c1_res,
};

static struct platform_device i2c2_device = {
	.name		= "i2c-rcar",
	.id		= 2,
	.dev = {
		.platform_data = &i2c_pd[2],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c2_res),
	.resource	= rcar_i2c2_res,
};

static struct platform_device i2c3_device = {
	.name		= "i2c-rcar",
	.id		= 3,
	.dev = {
		.platform_data = &i2c_pd[3],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c3_res),
	.resource	= rcar_i2c3_res,
};

static struct platform_device i2c4_device = {
	.name		= "i2c-rcar",
	.id		= 4,
	.dev = {
		.platform_data = &i2c_pd[4],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c4_res),
	.resource	= rcar_i2c4_res,
};

static struct platform_device i2c5_device = {
	.name		= "i2c-rcar",
	.id		= 5,
	.dev = {
		.platform_data = &i2c_pd[5],
	},
	.num_resources	= ARRAY_SIZE(rcar_i2c5_res),
	.resource	= rcar_i2c5_res,
};

static struct platform_device *r8a7791_early_devices[] __initdata = {
	&eth_device,
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
	&i2c3_device,
	&i2c4_device,
	&i2c5_device,
};

static struct renesas_irqc_config irqc0_data = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ3 */
};

static struct resource irqc0_resources[] = {
	DEFINE_RES_MEM(0xe61c0000, 0x200), /* IRQC Event Detector Block_0 */
	DEFINE_RES_IRQ(gic_spi(0)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(1)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(2)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(3)), /* IRQ3 */
};

#define r8a7791_register_irqc(idx)					\
	platform_device_register_resndata(&platform_bus, "renesas_irqc", \
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

void __init r8a7791_add_standard_devices(void)
{
	r8a7791_register_scif(SCIF0);
	r8a7791_register_scif(SCIF1);
	r8a7791_register_irqc(0);
	platform_add_devices(r8a7791_early_devices,
			     ARRAY_SIZE(r8a7791_early_devices));
}

void __init r8a7791_timer_init(void)
{
	void __iomem *cntcr;

	/* make sure arch timer is started by setting bit 0 of CNTCT */
	cntcr = ioremap(0xe6080000, PAGE_SIZE);
	iowrite32(1, cntcr);
	iounmap(cntcr);

	shmobile_timer_init();
}

struct sys_timer r8a7791_timer = {
	.init		= r8a7791_timer_init,
};

#ifdef CONFIG_USE_OF
void __init r8a7791_add_standard_devices_dt(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *r8a7791_boards_compat_dt[] __initdata = {
	"renesas,r8a7791",
	NULL,
};

DT_MACHINE_START(R8A7791_DT, "Generic R8A7791 (Flattened Device Tree)")
	.init_irq	= irqchip_init,
	.init_machine	= r8a7791_add_standard_devices_dt,
	.timer		= &r8a7791_timer,
	.dt_compat	= r8a7791_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */

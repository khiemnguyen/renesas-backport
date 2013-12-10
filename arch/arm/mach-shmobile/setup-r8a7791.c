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

#include <linux/clk.h>
#include <linux/i2c/i2c-rcar.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/of_platform.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/serial_sci.h>
#include <linux/sh_audma-pp.h>
#include <linux/sh_dma-desc.h>
#include <linux/sh_timer.h>
#include <linux/spi/sh_msiof.h>
#include <linux/usb/ehci_pdriver.h>
#ifdef CONFIG_USB_R8A66597
#include <linux/usb/gpio_vbus.h>
#endif
#include <linux/usb/ohci_pdriver.h>
#ifdef CONFIG_USB_R8A66597
#include <linux/usb/r8a66597.h>
#endif
#include <mach/common.h>
#include <mach/dma-register.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <media/vin.h>
#include <sound/sh_scu.h>
#include <asm/mach/arch.h>

static const struct resource pfc_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6060000, 0x250),
};

#define R8A7791_GPIO(idx)						\
static const struct resource r8a7791_gpio##idx##_resources[] __initconst = { \
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
static const struct resource r8a7791_gpio##idx##_resources[] __initconst = { \
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

#define SCIF_COMMON(scif_type, baseaddr, irq, dma_tx, dma_rx)	\
	.type		= scif_type,				\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.irqs		= SCIx_IRQ_MUXED(irq),			\
	.dma_slave_tx	= dma_tx,				\
	.dma_slave_rx	= dma_rx

#define SCIFA_DATA(index, baseaddr, irq, dma_tx, dma_rx)	\
[index] = {						\
	SCIF_COMMON(PORT_SCIFA, baseaddr, irq, dma_tx, dma_rx),	\
	.scbrr_algo_id	= SCBRR_ALGO_4,			\
	.scscr = SCSCR_RE | SCSCR_TE | SCSCR_CKE0,	\
}

#define SCIFB_DATA(index, baseaddr, irq, dma_tx, dma_rx)	\
[index] = {					\
	SCIF_COMMON(PORT_SCIFB, baseaddr, irq, dma_tx, dma_rx),	\
	.scbrr_algo_id	= SCBRR_ALGO_4,		\
	.scscr = SCSCR_RE | SCSCR_TE,		\
}

#define SCIF_DATA(index, baseaddr, irq, dma_tx, dma_rx)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIF, baseaddr, irq, dma_tx, dma_rx),	\
	.scbrr_algo_id	= SCBRR_ALGO_2,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

#define HSCIF_DATA(index, baseaddr, irq, dma_tx, dma_rx)	\
[index] = {						\
	SCIF_COMMON(PORT_HSCIF, baseaddr, irq, dma_tx, dma_rx),	\
	.scbrr_algo_id	= SCBRR_ALGO_6,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
	.capabilities = SCIx_HAVE_RTSCTS,	\
}

enum { SCIFA0 = 0, SCIFA1, SCIFB0, SCIFB1, SCIFB2, SCIFA2, SCIF0, SCIF1,
	HSCIF0, HSCIF1, SCIF2, SCIF3, SCIF4, SCIF5, SCIFA3, SCIFA4, SCIFA5,
	HSCIF2 };

static const struct plat_sci_port scif[] __initconst = {
	SCIFA_DATA(SCIFA0, 0xe6c40000, gic_spi(144),
		SHDMA_SLAVE_SCIFA0_TX, SHDMA_SLAVE_SCIFA0_RX), /* SCIFA0 */
	SCIFA_DATA(SCIFA1, 0xe6c50000, gic_spi(145),
		SHDMA_SLAVE_SCIFA1_TX, SHDMA_SLAVE_SCIFA1_RX), /* SCIFA1 */
	SCIFB_DATA(SCIFB0, 0xe6c20000, gic_spi(148),
		SHDMA_SLAVE_SCIFB0_TX, SHDMA_SLAVE_SCIFB0_RX), /* SCIFB0 */
	SCIFB_DATA(SCIFB1, 0xe6c30000, gic_spi(149),
		SHDMA_SLAVE_SCIFB1_TX, SHDMA_SLAVE_SCIFB1_RX), /* SCIFB1 */
	SCIFB_DATA(SCIFB2, 0xe6ce0000, gic_spi(150),
		SHDMA_SLAVE_SCIFB2_TX, SHDMA_SLAVE_SCIFB2_RX), /* SCIFB2 */
	SCIFA_DATA(SCIFA2, 0xe6c60000, gic_spi(151),
		SHDMA_SLAVE_SCIFA2_TX, SHDMA_SLAVE_SCIFA2_RX), /* SCIFA2 */
	SCIF_DATA(SCIF0, 0xe6e60000, gic_spi(152),
		SHDMA_SLAVE_SCIF0_TX, SHDMA_SLAVE_SCIF0_RX), /* SCIF0 */
	SCIF_DATA(SCIF1, 0xe6e68000, gic_spi(153),
		SHDMA_SLAVE_SCIF1_TX, SHDMA_SLAVE_SCIF1_RX), /* SCIF1 */
	HSCIF_DATA(HSCIF0, 0xe62c0000, gic_spi(154),
		SHDMA_SLAVE_HSCIF0_TX, SHDMA_SLAVE_HSCIF0_RX), /* HSCIF0 */
	HSCIF_DATA(HSCIF1, 0xe62c8000, gic_spi(155),
		SHDMA_SLAVE_HSCIF1_TX, SHDMA_SLAVE_HSCIF1_RX), /* HSCIF1 */
	SCIF_DATA(SCIF2, 0xe6e56000, gic_spi(164), 0, 0), /* SCIF2 */
	SCIF_DATA(SCIF3, 0xe6ea8000, gic_spi(23), 0, 0), /* SCIF3 */
	SCIF_DATA(SCIF4, 0xe6ee0000, gic_spi(24), 0, 0), /* SCIF4 */
	SCIF_DATA(SCIF5, 0xe6ee8000, gic_spi(25), 0, 0), /* SCIF5 */
	SCIFA_DATA(SCIFA3, 0xe6c70000, gic_spi(29), 0, 0), /* SCIFA3 */
	SCIFA_DATA(SCIFA4, 0xe6c78000, gic_spi(30), 0, 0), /* SCIFA4 */
	SCIFA_DATA(SCIFA5, 0xe6c80000, gic_spi(31), 0, 0), /* SCIFA5 */
	HSCIF_DATA(HSCIF2, 0xe62d0000, gic_spi(21), 0, 0), /* HSCIF2 */
};

static inline void r8a7791_register_scif(int idx)
{
	struct platform_device_info pdevinfo = {
		.parent = &platform_bus,
		.name = "sh-sci",
		.id = idx,
		.res = NULL,
		.num_res = 0,
		.data = &scif[idx],
		.size_data = sizeof(struct plat_sci_port),
		.dma_mask = 0,
	};

#if defined(CONFIG_SERIAL_SH_SCI_DMA)
	pdevinfo.dma_mask = DMA_BIT_MASK(32);
#endif /* CONFIG_SERIAL_SH_SCI_DMA */

	platform_device_register_full(&pdevinfo);
}

static const struct renesas_irqc_config irqc0_data __initconst = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ9 */
};

static const struct resource irqc0_resources[] __initconst = {
	DEFINE_RES_MEM(0xe61c0000, 0x200), /* IRQC Event Detector Block_0 */
	DEFINE_RES_IRQ(gic_spi(0)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(1)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(2)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(3)), /* IRQ3 */
	DEFINE_RES_IRQ(gic_spi(12)), /* IRQ4 */
	DEFINE_RES_IRQ(gic_spi(13)), /* IRQ5 */
	DEFINE_RES_IRQ(gic_spi(14)), /* IRQ6 */
	DEFINE_RES_IRQ(gic_spi(15)), /* IRQ7 */
	DEFINE_RES_IRQ(gic_spi(16)), /* IRQ8 */
	DEFINE_RES_IRQ(gic_spi(17)), /* IRQ9 */
};

#define r8a7791_register_irqc(idx)					\
	platform_device_register_resndata(&platform_bus, "renesas_irqc", \
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

static const struct resource thermal_resources[] __initconst = {
	DEFINE_RES_MEM(0xe61f0000, 0x14),
	DEFINE_RES_MEM(0xe61f0100, 0x38),
	DEFINE_RES_IRQ(gic_spi(69)),
};

#define r8a7791_register_thermal()					\
	platform_device_register_simple("rcar_thermal", -1,		\
					thermal_resources,		\
					ARRAY_SIZE(thermal_resources))

static const struct sh_timer_config cmt00_platform_data __initconst = {
	.name = "CMT00",
	.timer_bit = 0,
	.clockevent_rating = 80,
};

static const struct resource cmt00_resources[] __initconst = {
	DEFINE_RES_MEM(0xffca0510, 0x0c),
	DEFINE_RES_MEM(0xffca0500, 0x04),
	DEFINE_RES_IRQ(gic_spi(142)), /* CMT0_0 */
};

#define r8a7791_register_cmt(idx)					\
	platform_device_register_resndata(&platform_bus, "sh_cmt",	\
					  idx, cmt##idx##_resources,	\
					  ARRAY_SIZE(cmt##idx##_resources), \
					  &cmt##idx##_platform_data,	\
					  sizeof(struct sh_timer_config))

/* Audio */
#define r8a7791_register_alsa(idx)					\
	platform_device_register_resndata(&platform_bus,		\
		"koelsch_alsa_soc_platform", idx, NULL, 0, NULL, 0)

static const struct resource r8a7791_scu_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xec000000, 0x501000, "scu"),
	DEFINE_RES_MEM_NAMED(0xec540000, 0x860, "ssiu"),
	DEFINE_RES_MEM_NAMED(0xec541000, 0x280, "ssi"),
	DEFINE_RES_MEM_NAMED(0xec5a0000, 0x68, "adg"),
};

void __init r8a7791_add_scu_device(struct scu_platform_data *pdata)
{
	struct platform_device_info info = {
		.name = "scu-pcm-audio",
		.id = 0,
		.data = pdata,
		.size_data = sizeof(*pdata),
		.res = r8a7791_scu_resources,
		.num_res = ARRAY_SIZE(r8a7791_scu_resources),
	};

	platform_device_register_full(&info);
}

/* DMA */
#define DMA_CHANNEL(a, b, c)	\
{				\
	.offset		= a,	\
	.dmars		= b,	\
	.dmars_bit	= 0,	\
	.chclr_offset	= c	\
}

static struct clk *audma_clk_get(struct platform_device *pdev)
{
	if (pdev->id == SHDMA_DEVID_AUDIO_LO)
		return clk_get(NULL, "audmac_lo");
	else if (pdev->id == SHDMA_DEVID_AUDIO_UP)
		return clk_get(NULL, "audmac_up");
	else
		return NULL;
}

static const struct sh_dmadesc_slave_config r8a7791_audma_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_PCM_MEM_SSI0,
		.addr		= 0xec241008,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x01,
		.desc_mode	= 2,
		.desc_offset	= 0x0,
		.desc_stepnum	= 4,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_MEM_SRC0,
		.addr		= 0xec000000,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x85,
		.desc_mode	= 2,
		.desc_offset	= 0x0,
		.desc_stepnum	= 4,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_SSI1_MEM,
		.addr		= 0xec24104c,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x04,
		.desc_mode	= 2,
		.desc_offset	= 0x100,
		.desc_stepnum	= 4,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_SRC1_MEM,
		.addr		= 0xec004400,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x9c,
		.desc_mode	= 2,
		.desc_offset	= 0x100,
		.desc_stepnum	= 4,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_CMD1_MEM,
		.addr		= 0xec008400,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xbe,
		.desc_mode	= 2,
		.desc_offset	= 0x100,
		.desc_stepnum	= 4,
	},
};

static const struct sh_dmadesc_channel r8a7791_audma_channels[] = {
	DMA_CHANNEL(0x00008000, 0x40, 0),
	DMA_CHANNEL(0x00008080, 0x40, 0),
	DMA_CHANNEL(0x00008100, 0x40, 0),
	DMA_CHANNEL(0x00008180, 0x40, 0),
	DMA_CHANNEL(0x00008200, 0x40, 0),
	DMA_CHANNEL(0x00008280, 0x40, 0),
	DMA_CHANNEL(0x00008300, 0x40, 0),
	DMA_CHANNEL(0x00008380, 0x40, 0),
	DMA_CHANNEL(0x00008400, 0x40, 0),
	DMA_CHANNEL(0x00008480, 0x40, 0),
	DMA_CHANNEL(0x00008500, 0x40, 0),
	DMA_CHANNEL(0x00008580, 0x40, 0),
	DMA_CHANNEL(0x00008600, 0x40, 0),
};

static const struct sh_dmadesc_pdata r8a7791_audma_pdata __initconst = {
	.slave		= r8a7791_audma_slaves,
	.slave_num	= ARRAY_SIZE(r8a7791_audma_slaves),
	.channel	= r8a7791_audma_channels,
	.channel_num	= ARRAY_SIZE(r8a7791_audma_channels),
	.ts_low_shift	= TS_LOW_SHIFT,
	.ts_low_mask	= TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift	= TS_HI_SHIFT,
	.ts_high_mask	= TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift	= dma_ts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_ts_shift),
	.dmaor_init	= DMAOR_DME,
	.chclr_present	= 1,
	.clk_get	= audma_clk_get,
};

static const struct resource r8a7791_audmal_resources[] __initconst = {
	DEFINE_RES_MEM(0xec700000, 0xa800),
	DEFINE_RES_IRQ_NAMED(gic_spi(346), "error_irq"),
	DEFINE_RES_NAMED(gic_spi(320), 13, NULL, IORESOURCE_IRQ),
};

static const struct resource r8a7791_audmau_resources[] __initconst = {
	DEFINE_RES_MEM(0xec720000, 0xa800),
	DEFINE_RES_IRQ_NAMED(gic_spi(347), "error_irq"),
	DEFINE_RES_NAMED(gic_spi(333), 13, NULL, IORESOURCE_IRQ),
};

#define r8a7791_register_audma(index, devid)				\
	platform_device_register_resndata(&platform_bus,		\
		"sh-dmadesc-engine", devid,				\
		r8a7791_audma##index##_resources,			\
		ARRAY_SIZE(r8a7791_audma##index##_resources),		\
		&r8a7791_audma_pdata,					\
		sizeof(r8a7791_audma_pdata))

static struct clk *sysdma_clk_get(struct platform_device *pdev)
{
	if (pdev->id == SHDMA_DEVID_SYS_LO)
		return clk_get(NULL, "sysdmac_lo");
	else if (pdev->id == SHDMA_DEVID_SYS_UP)
		return clk_get(NULL, "sysdmac_up");
	else
		return NULL;
}

static const struct sh_dmadesc_slave_config r8a7791_sysdma_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_SDHI0_TX,
		.addr		= 0xee100060,
		.chcr		= CHCR_TX(XMIT_SZ_256BIT),
		.mid_rid	= 0xcd,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI0_RX,
		.addr		= 0xee100060 + 0x2000,
		.chcr		= CHCR_RX(XMIT_SZ_256BIT),
		.mid_rid	= 0xce,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_TX,
		.addr		= 0xee140030,
		.chcr		= CHCR_TX(XMIT_SZ_256BIT),
		.mid_rid	= 0xc1,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI1_RX,
		.addr		= 0xee140030 + 0x2000,
		.chcr		= CHCR_RX(XMIT_SZ_256BIT),
		.mid_rid	= 0xc2,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_TX,
		.addr		= 0xee160030,
		.chcr		= CHCR_TX(XMIT_SZ_256BIT),
		.mid_rid	= 0xd3,
	}, {
		.slave_id	= SHDMA_SLAVE_SDHI2_RX,
		.addr		= 0xee160030 + 0x2000,
		.chcr		= CHCR_RX(XMIT_SZ_256BIT),
		.mid_rid	= 0xd4,
	}, {
		.slave_id	= SHDMA_SLAVE_MMC_TX,
		.addr		= 0xee200034,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd1,
	}, {
		.slave_id	= SHDMA_SLAVE_MMC_RX,
		.addr		= 0xee200034,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd2,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF0_RX,
		.addr		= 0xe6e60000 + 0x14,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_TX,
		.addr		= 0xe6e68000 + 0x0c,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIF1_RX,
		.addr		= 0xe6e68000 + 0x14,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x2e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA0_TX,
		.addr		= 0xe7c40000 + 0x20,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x21,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA0_RX,
		.addr		= 0xe7c40000 + 0x24,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x22,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA1_TX,
		.addr		= 0xe7c50000 + 0x20,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x25,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA1_RX,
		.addr		= 0xe7c50000 + 0x24,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x26,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA2_TX,
		.addr		= 0xe7c60000 + 0x20,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x27,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFA2_RX,
		.addr		= 0xe7c60000 + 0x24,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x28,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB0_TX,
		.addr		= 0xe7c20000 + 0x40,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB0_RX,
		.addr		= 0xe7c20000 + 0x60,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3e,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB1_TX,
		.addr		= 0xe7c30000 + 0x40,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x19,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB1_RX,
		.addr		= 0xe7c30000 + 0x60,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1a,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB2_TX,
		.addr		= 0xe7ce0000 + 0x40,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1d,
	}, {
		.slave_id	= SHDMA_SLAVE_SCIFB2_RX,
		.addr		= 0xe7ce0000 + 0x60,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x1e,
	}, {
		.slave_id	= SHDMA_SLAVE_HSCIF0_TX,
		.addr		= 0xe62c0000 + 0x0c,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x39,
	}, {
		.slave_id	= SHDMA_SLAVE_HSCIF0_RX,
		.addr		= 0xe62c0000 + 0x14,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x3a,
	}, {
		.slave_id	= SHDMA_SLAVE_HSCIF1_TX,
		.addr		= 0xe62c8000 + 0x0c,
		.chcr		= CHCR_TX(XMIT_SZ_8BIT),
		.mid_rid	= 0x4d,
	}, {
		.slave_id	= SHDMA_SLAVE_HSCIF1_RX,
		.addr		= 0xe62c8000 + 0x14,
		.chcr		= CHCR_RX(XMIT_SZ_8BIT),
		.mid_rid	= 0x4e,
	}, {
		.slave_id	= SHDMA_SLAVE_MSIOF0_TX,
		.addr		= 0xe7e20050,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x51,
	}, {
		.slave_id	= SHDMA_SLAVE_MSIOF0_RX,
		.addr		= 0xe7e20060,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x52,
	},  {
		.slave_id	= SHDMA_SLAVE_MSIOF1_TX,
		.addr		= 0xe7e10050,
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0x55,
	}, {
		.slave_id	= SHDMA_SLAVE_MSIOF1_RX,
		.addr		= 0xe7e10060,
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0x56,
	},
};

static const struct sh_dmadesc_channel r8a7791_sysdma_channels[] = {
	DMA_CHANNEL(0x00008000, 0x40, 0),
	DMA_CHANNEL(0x00008080, 0x40, 0),
	DMA_CHANNEL(0x00008100, 0x40, 0),
	DMA_CHANNEL(0x00008180, 0x40, 0),
	DMA_CHANNEL(0x00008200, 0x40, 0),
	DMA_CHANNEL(0x00008280, 0x40, 0),
	DMA_CHANNEL(0x00008300, 0x40, 0),
	DMA_CHANNEL(0x00008380, 0x40, 0),
	DMA_CHANNEL(0x00008400, 0x40, 0),
	DMA_CHANNEL(0x00008480, 0x40, 0),
	DMA_CHANNEL(0x00008500, 0x40, 0),
	DMA_CHANNEL(0x00008580, 0x40, 0),
	DMA_CHANNEL(0x00008600, 0x40, 0),
	DMA_CHANNEL(0x00008680, 0x40, 0),
	DMA_CHANNEL(0x00008700, 0x40, 0),
};

static const struct sh_dmadesc_pdata r8a7791_sysdma_pdata __initconst = {
	.slave		= r8a7791_sysdma_slaves,
	.slave_num	= ARRAY_SIZE(r8a7791_sysdma_slaves),
	.channel	= r8a7791_sysdma_channels,
	.channel_num	= ARRAY_SIZE(r8a7791_sysdma_channels),
	.ts_low_shift	= TS_LOW_SHIFT,
	.ts_low_mask	= TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift	= TS_HI_SHIFT,
	.ts_high_mask	= TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift	= dma_ts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_ts_shift),
	.dmaor_init	= DMAOR_DME,
	.chclr_present	= 1,
	.clk_get	= sysdma_clk_get,
};

static const struct resource r8a7791_sysdmal_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6700000, 0xa800),
	DEFINE_RES_IRQ_NAMED(gic_spi(197), "error_irq"),
	DEFINE_RES_NAMED(gic_spi(200), 15, NULL, IORESOURCE_IRQ),
};

static const struct resource r8a7791_sysdmau_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6720000, 0xa800),
	DEFINE_RES_IRQ_NAMED(gic_spi(220), "error_irq"),
	DEFINE_RES_NAMED(gic_spi(216), 4, NULL, IORESOURCE_IRQ),
	DEFINE_RES_NAMED(gic_spi(308), 11, NULL, IORESOURCE_IRQ),
};

#define r8a7791_register_sysdma(index, devid)				\
	platform_device_register_resndata(&platform_bus,		\
		"sh-dmadesc-engine", devid,				\
		r8a7791_sysdma##index##_resources,			\
		ARRAY_SIZE(r8a7791_sysdma##index##_resources),		\
		&r8a7791_sysdma_pdata,					\
		sizeof(r8a7791_sysdma_pdata))

#define AUDMAPP_CHANNEL(a)	\
{				\
	.offset		= a,	\
}

static const struct sh_audmapp_slave_config r8a7791_audmapp_slaves[] = {
	{
		.slave_id	= SHDMA_SLAVE_PCM_SRC0_SSI0,
		.sar		= 0xec304000,
		.dar		= 0xec400000,
		.chcr		= 0x2d000000,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_CMD0_SSI0,
		.sar		= 0xec308000,
		.dar		= 0xec400000,
		.chcr		= 0x37000000,
	}, {
		.slave_id	= SHDMA_SLAVE_PCM_SSI1_SRC1,
		.sar		= 0xec401000,
		.dar		= 0xec300400,
		.chcr		= 0x042e0000,
	},
};

static const struct sh_audmapp_channel r8a7791_audmapp_channels[] = {
	AUDMAPP_CHANNEL(0x0000),
	AUDMAPP_CHANNEL(0x0010),
	AUDMAPP_CHANNEL(0x0020),
	AUDMAPP_CHANNEL(0x0030),
	AUDMAPP_CHANNEL(0x0040),
	AUDMAPP_CHANNEL(0x0050),
	AUDMAPP_CHANNEL(0x0060),
	AUDMAPP_CHANNEL(0x0070),
	AUDMAPP_CHANNEL(0x0080),
	AUDMAPP_CHANNEL(0x0090),
	AUDMAPP_CHANNEL(0x00a0),
	AUDMAPP_CHANNEL(0x00b0),
	AUDMAPP_CHANNEL(0x00c0),
	AUDMAPP_CHANNEL(0x00d0),
	AUDMAPP_CHANNEL(0x00e0),
	AUDMAPP_CHANNEL(0x00f0),
	AUDMAPP_CHANNEL(0x0100),
	AUDMAPP_CHANNEL(0x0110),
	AUDMAPP_CHANNEL(0x0120),
	AUDMAPP_CHANNEL(0x0130),
	AUDMAPP_CHANNEL(0x0140),
	AUDMAPP_CHANNEL(0x0150),
	AUDMAPP_CHANNEL(0x0160),
	AUDMAPP_CHANNEL(0x0170),
	AUDMAPP_CHANNEL(0x0180),
	AUDMAPP_CHANNEL(0x0190),
	AUDMAPP_CHANNEL(0x01a0),
	AUDMAPP_CHANNEL(0x01b0),
	AUDMAPP_CHANNEL(0x01c0),
};

static const struct sh_audmapp_pdata r8a7791_audmapp_pdata __initconst = {
	.slave		= r8a7791_audmapp_slaves,
	.slave_num	= ARRAY_SIZE(r8a7791_audmapp_slaves),
	.channel	= r8a7791_audmapp_channels,
	.channel_num	= ARRAY_SIZE(r8a7791_audmapp_channels),
};

static const struct resource r8a7791_audmapp_resources[] __initconst = {
	DEFINE_RES_MEM(0xec740020, 0x1d0),
};

#define r8a7791_register_audmapp(devid)					\
	platform_device_register_resndata(&platform_bus,		\
		"sh-audmapp-engine", devid,				\
		r8a7791_audmapp_resources,				\
		ARRAY_SIZE(r8a7791_audmapp_resources),			\
		&r8a7791_audmapp_pdata,					\
		sizeof(r8a7791_audmapp_pdata))

/* I2C */
static const struct resource r8a7791_i2c0_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6508000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(287)),
};

static const struct resource r8a7791_i2c1_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6518000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(288)),
};

static const struct resource r8a7791_i2c2_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6530000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(286)),
};

static const struct resource r8a7791_i2c3_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6540000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(290)),
};

static const struct resource r8a7791_i2c4_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6520000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(19)),
};

static const struct resource r8a7791_i2c5_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6528000, SZ_32K),
	DEFINE_RES_IRQ(gic_spi(20)),
};

#define R8A7791_I2C(idx)						\
static const struct i2c_rcar_platform_data				\
r8a7791_i2c##idx##_platform_data __initconst = {			\
	.bus_speed = 400000,						\
	.icccr_cdf_width = I2C_RCAR_ICCCR_IS_3BIT,			\
};									\

R8A7791_I2C(0);
R8A7791_I2C(1);
R8A7791_I2C(2);
R8A7791_I2C(3);
R8A7791_I2C(4);
R8A7791_I2C(5);

#define r8a7791_register_i2c(idx)					\
	platform_device_register_resndata(&platform_bus, "i2c-rcar", idx, \
		r8a7791_i2c##idx##_resources,				\
		ARRAY_SIZE(r8a7791_i2c##idx##_resources),		\
		&r8a7791_i2c##idx##_platform_data,			\
		sizeof(r8a7791_i2c##idx##_platform_data))

/* IIC */
static const struct resource r8a7791_iic6_resources[] __initconst = {
	DEFINE_RES_MEM(0xe60b0000, SZ_4K),
	DEFINE_RES_IRQ(gic_spi(173)),
	DEFINE_RES_IRQ(gic_spi(174)),
};

#define r8a7791_register_iic(idx)					\
	platform_device_register_simple("i2c-sh_mobile",		\
				idx,					\
				r8a7791_iic##idx##_resources,		\
				ARRAY_SIZE(r8a7791_iic##idx##_resources))

/* MMC */
static const struct resource sh_mmcif_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee200000, SZ_128, "mmc"),
	DEFINE_RES_IRQ(gic_spi(169)),
};

void __init r8a7791_add_mmc_device(struct sh_mmcif_plat_data *pdata)
{
	struct platform_device_info info = {
		.name = "sh_mmcif",
		.id = 0,
		.data = pdata,
		.size_data = sizeof(*pdata),
		.res = sh_mmcif_resources,
		.num_res = 2,
	};

	platform_device_register_full(&info);
}

/* MSIOF */
#define MSIOF_COMMON				\
	.rx_fifo_override	= 256,			\
	.num_chipselect		= 1,			\
	.dma_devid_lo		= SHDMA_DEVID_SYS_LO,	\
	.dma_devid_up		= SHDMA_DEVID_SYS_UP

static const struct sh_msiof_spi_info sh_msiof_info[] __initconst = {
	{
		MSIOF_COMMON,
		.dma_slave_tx		= SHDMA_SLAVE_MSIOF0_TX,
		.dma_slave_rx		= SHDMA_SLAVE_MSIOF0_RX,
#ifndef CONFIG_SPI_SH_MSIOF_CH0_SLAVE
		.mode			= SPI_MSIOF_MASTER,
#else
		.mode			= SPI_MSIOF_SLAVE,
#endif
	},
	{
		MSIOF_COMMON,
		.dma_slave_tx		= SHDMA_SLAVE_MSIOF1_TX,
		.dma_slave_rx		= SHDMA_SLAVE_MSIOF1_RX,
#ifndef CONFIG_SPI_SH_MSIOF_CH1_SLAVE
		.mode			= SPI_MSIOF_MASTER,
#else
		.mode			= SPI_MSIOF_SLAVE,
#endif
	},
	{
		MSIOF_COMMON,
		.mode			= SPI_MSIOF_MASTER,
	},
};

static const struct resource sh_msiof0_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6e20000, 0x0064),
	DEFINE_RES_IRQ(gic_spi(156)),
};

static const struct resource sh_msiof1_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6e10000, 0x0064),
	DEFINE_RES_IRQ(gic_spi(157)),
};

static const struct resource sh_msiof2_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6e00000, 0x0064),
	DEFINE_RES_IRQ(gic_spi(158)),
};

#define r8a7791_register_msiof(idx)					\
	platform_device_register_resndata(&platform_bus, "spi_sh_msiof", \
				  (idx+1), sh_msiof##idx##_resources,	\
				  ARRAY_SIZE(sh_msiof##idx##_resources), \
				  &sh_msiof_info[idx],		\
				  sizeof(struct sh_msiof_spi_info))

/* POWERVR */
static struct resource powervr_resources[] = {
	{
		.start  = 0xfd800000,
		.end    = 0xfd80ffff,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = gic_spi(119),
		.flags  = IORESOURCE_IRQ,
	},
};
static struct platform_device powervr_device = {
	.name           = "pvrsrvkm",
	.id             = -1,
	.resource       = powervr_resources,
	.num_resources  = ARRAY_SIZE(powervr_resources),
};

/* QSPI */
static const struct resource qspi_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xe6b10000, SZ_4K, "qspi"),
	DEFINE_RES_IRQ(gic_spi(184)),
};

#define r8a7791_register_qspi()					\
	platform_device_register_simple("qspi", 0,		\
					qspi_resources,		\
					ARRAY_SIZE(qspi_resources))

/* SATA */
static const struct resource sata0_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee300000, SZ_2M, "sata0"),
	DEFINE_RES_IRQ(gic_spi(105)),
};

static const struct resource sata1_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee500000, SZ_2M, "sata1"),
	DEFINE_RES_IRQ(gic_spi(106)),
};

static const struct resource *sata_resources[2] = {
	sata0_resources,
	sata1_resources,
};

void __init r8a7791_register_sata(unsigned int index)
{
	struct platform_device_info info = {
		.name = "sata_rcar",
		.id = index,
		.data = NULL,
		.size_data = 0,
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (index >= ARRAY_SIZE(sata_resources))
		return;

	info.res = sata_resources[index];
	info.num_res = 2;

	platform_device_register_full(&info);
}

/* SDHI */
static const struct resource sdhi0_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee100000, SZ_1K, "sdhi0"),
	DEFINE_RES_IRQ(gic_spi(165)),
};

static const struct resource sdhi1_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee140000, SZ_256, "sdhi1"),
	DEFINE_RES_IRQ(gic_spi(167)),
};

static const struct resource sdhi2_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee160000, SZ_256, "sdhi2"),
	DEFINE_RES_IRQ(gic_spi(168)),
};

static const struct resource *sdhi_resources[3] = {
	sdhi0_resources,
	sdhi1_resources,
	sdhi2_resources,
};

void __init r8a7791_add_sdhi_device(struct sh_mobile_sdhi_info *pdata,
				    unsigned int index)
{
	struct platform_device_info info = {
		.name = "sh_mobile_sdhi",
		.id = index,
		.data = pdata,
		.size_data = sizeof(*pdata),
		.num_res = 2,
	};

	if (index >= ARRAY_SIZE(sdhi_resources))
		return;

	info.res = sdhi_resources[index];

	platform_device_register_full(&info);
}

#ifdef CONFIG_USB_R8A66597
/* USB2.0 Function */
#define LPSTS		0x102 /* 16-bit */
#define UGCTRL		0x180 /* 32-bit */
#define UGCTRL2		0x184 /* 32-bit */
#define UGSTS		0x190 /* 32-bit */
static void __iomem *hsusb;

static void usb_func_start(void)
{
	u32 status;

	if (!hsusb)
		hsusb = ioremap_nocache(0xe6590000, 0x200);

	/*
	 * TODO: we should give a software reset to the HS-USB module from
	 * HS-USB operation point of view.  The module reset, however, blows
	 * away UGCTRL2 register content, that means USB2.0 Ch 0/2 selection
	 * settings get lost.
	 */
	printk(KERN_DEBUG "UGCTRL 0x%08x UGCTRL2 0x%08x UGSTS 0x%08x\n",
		readl_relaxed(hsusb + UGCTRL), readl_relaxed(hsusb + UGCTRL2),
		readl_relaxed(hsusb + UGSTS));

	/* PLL reset release */
	writel_relaxed(0, hsusb + UGCTRL); /* clear PLLRESET */

	/* UTMI normal mode */
	writew_relaxed(1 << 14, hsusb + LPSTS); /* SUSPM */

	/* check embedded USB PHY lock status */
	status = readl_relaxed(hsusb + UGSTS);
	if (status != 0x3) {
		printk(KERN_DEBUG "Embedded USB PHY PLL Lock status 0x%04x\n",
			 status);
		/* FIXME: LOCK[1:0] can never be 0x3, why? */
	}

	writel_relaxed(1 << 2, hsusb + UGCTRL); /* CONNECT */
}

static void usb_func_stop(void)
{
	writel_relaxed(0, hsusb + UGCTRL); /* PHY receiver halted */
	writel_relaxed(1 << 0, hsusb + UGCTRL); /* PLL reset assert */
}

static struct r8a66597_platdata usb_func_platform_data = {
	.module_start	= usb_func_start,
	.module_stop	= usb_func_stop,
	.on_chip	= 1,
	.buswait	= 9,
	.max_bufnum	= 0xff,
	.dmac		= 1,
};

static struct resource usb_func_resources[] = {
	DEFINE_RES_MEM(0xe6590000, 0x194),
	DEFINE_RES_IRQ(gic_spi(107)),
	DEFINE_RES_MEM_NAMED(0xe65a0000, 0x64, "dmac"),
	DEFINE_RES_IRQ(gic_spi(109)),
};

void __init r8a7791_register_usbf(void)
{
	struct platform_device_info info = {
		.parent = &platform_bus,
		.name = "r8a66597_udc",
		.id = 0,
		.data = &usb_func_platform_data,
		.size_data = sizeof(struct r8a66597_platdata),
		.dma_mask = DMA_BIT_MASK(32),
		.res = usb_func_resources,
		.num_res = ARRAY_SIZE(usb_func_resources),
	};

	platform_device_register_full(&info);
}

/* GPIO VBUS sensing */
static struct gpio_vbus_mach_info gpio_vbus_platform_data = {
	.gpio_vbus	= RCAR_GP_PIN(7, 24),
	.gpio_pullup	= -1,
	.wakeup		= true,
};

#define r8a7791_register_gpio_vbus()					\
	platform_device_register_data(&platform_bus, "gpio-vbus", -1,	\
					&gpio_vbus_platform_data,	\
					sizeof(struct gpio_vbus_mach_info))
#endif

/* USB */
struct usb_ehci_pdata ehci_pdata = {
	.caps_offset	= 0,
	.has_tt		= 0,
};

struct usb_ohci_pdata ohci_pdata = {
};

static const struct resource ehci0_resources[] __initconst = {
	DEFINE_RES_MEM(0xee080000 + 0x1000, 0xfff),
	DEFINE_RES_IRQ(gic_spi(108)),
};
static const struct resource ehci1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee0c0000 + 0x1000, 0xfff),
	DEFINE_RES_IRQ(gic_spi(113)),
};
static const struct resource ohci0_resources[] __initconst = {
	DEFINE_RES_MEM(0xee080000, 0xfff),
	DEFINE_RES_IRQ(gic_spi(108)),
};
static const struct resource ohci1_resources[] __initconst = {
	DEFINE_RES_MEM(0xee0c0000, 0xfff),
	DEFINE_RES_IRQ(gic_spi(113)),
};
static const struct resource xhci0_resources[] __initconst = {
	DEFINE_RES_MEM(SHUSBH_XHCI_BASE, SHUSBH_XHCI_SIZE),
	DEFINE_RES_IRQ(gic_spi(101)),
};
static const struct resource *ehci_resources[] = {
	ehci0_resources,
	ehci1_resources,
};
static const struct resource *ohci_resources[] = {
	ohci0_resources,
	ohci1_resources,
};
static const struct resource *xhci_resources[] = {
	xhci0_resources,
};
void __init r8a7791_register_usbh_ehci(unsigned int index)
{
	struct platform_device_info info = {
		.parent = &platform_bus,
		.name = "ehci-platform",
		.id = index,
		.data = &ehci_pdata,
		.size_data = sizeof(struct usb_ehci_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (index >= ARRAY_SIZE(ehci_resources))
		return;

	info.res = ehci_resources[index];
	info.num_res = 2;

	platform_device_register_full(&info);
}
void __init r8a7791_register_usbh_ohci(unsigned int index)
{
	struct platform_device_info info = {
		.parent = &platform_bus,
		.name = "ohci-platform",
		.id = index,
		.data = &ohci_pdata,
		.size_data = sizeof(struct usb_ohci_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (index >= ARRAY_SIZE(ohci_resources))
		return;

	info.res = ohci_resources[index],
	info.num_res = 2,

	platform_device_register_full(&info);
}
void __init r8a7791_register_usbh_xhci(unsigned int index)
{
	struct platform_device_info info = {
		.parent = &platform_bus,
		.name = "xhci-hcd",
		.id = index,
		.data = NULL,
		.size_data = 0,
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (index >= ARRAY_SIZE(xhci_resources))
		return;

	info.res = xhci_resources[index];
	info.num_res = 2;

	platform_device_register_full(&info);
}
static void __init usbh_internal_pci_bridge_init(int ch)
{
	u32 data;
	void __iomem *ahbpci_base;
	void __iomem *pci_conf_ahbpci_bas;

	ahbpci_base =
		ioremap_nocache((AHBPCI_BASE + (ch * 0x20000)), 0x400);
	if (!ahbpci_base)
		return;

	pci_conf_ahbpci_bas =
		ioremap_nocache((PCI_CONF_AHBPCI_BAS + (ch * 0x20000)),
								0x100);
	if (!pci_conf_ahbpci_bas)
		goto err_iounmap_ahbpci;

	/* Clock & Reset & Direct Power Down */
	data = ioread32(ahbpci_base + USBCTR);
	data &= ~(DIRPD);
	iowrite32(data, (ahbpci_base + USBCTR));

	data &= ~(PLL_RST | PCICLK_MASK | USBH_RST);
	iowrite32(data | PCI_AHB_WIN1_SIZE_1G, (ahbpci_base + USBCTR));

	data = ioread32((ahbpci_base + AHB_BUS_CTR));
	if (data == AHB_BUS_CTR_SET)
		goto err_iounmap_pci_conf;

	/****** AHB-PCI Bridge Communication Registers ******/
	/* AHB_BUS_CTR */
	iowrite32(AHB_BUS_CTR_SET, (ahbpci_base + AHB_BUS_CTR));

	/* PCIAHB_WIN1_CTR */
	iowrite32((0x40000000 | PREFETCH),
			(ahbpci_base + PCIAHB_WIN1_CTR));

	/* AHBPCI_WIN2_CTR */
	iowrite32((SHUSBH_OHCI_BASE | PCIWIN2_PCICMD),
			(ahbpci_base + AHBPCI_WIN2_CTR));

	/* PCI_ARBITER_CTR */
	data = ioread32((ahbpci_base + PCI_ARBITER_CTR));
	data |= (PCIBP_MODE | PCIREQ1 | PCIREQ0);
	iowrite32(data, (ahbpci_base + PCI_ARBITER_CTR));

	/* AHBPCI_WIN1_CTR : set PCI Configuratin Register for AHBPCI */
	iowrite32(PCIWIN1_PCICMD | AHB_CFG_AHBPCI,
			(ahbpci_base + AHBPCI_WIN1_CTR));

	/****** PCI Configuration Registers for AHBPCI ******/
	/* BASEAD */
	iowrite32(AHBPCI_BASE, (pci_conf_ahbpci_bas + BASEAD));

	/* WIN1_BASEAD */
	iowrite32(0x40000000, (pci_conf_ahbpci_bas + WIN1_BASEAD));

	/* System error enable, Parity error enable, PCI Master enable, */
	/* Memory cycle enable */
	iowrite32(((ioread32(pci_conf_ahbpci_bas + CMND_STS) & ~0x00100000) |
			(SERREN | PERREN | MASTEREN | MEMEN)),
			(pci_conf_ahbpci_bas + CMND_STS));

	/****** PCI Configuration Registers for OHCI/EHCI ******/
	iowrite32(PCIWIN1_PCICMD | AHB_CFG_HOST,
			(ahbpci_base + AHBPCI_WIN1_CTR));

err_iounmap_pci_conf:
	iounmap(pci_conf_ahbpci_bas);
err_iounmap_ahbpci:
	iounmap(ahbpci_base);

}

static int __init usbh_ohci_init(int ch)
{
	u32 val;
	int retval;

	void __iomem *pci_conf_ohci_base
		= ioremap_nocache((PCI_CONF_OHCI_BASE + (ch * 0x20000)),
								0x100);

	if (!pci_conf_ohci_base)
		return -ENOMEM;

	val = ioread32((pci_conf_ohci_base + OHCI_VID_DID));

	if (val == OHCI_ID) {
		/* OHCI_BASEAD */
		iowrite32(SHUSBH_OHCI_BASE,
				(pci_conf_ohci_base + OHCI_BASEAD));
		retval = 0;

		/* System error enable, Parity error enable, */
		/* PCI Master enable, Memory cycle enable */
		iowrite32(ioread32(pci_conf_ohci_base + OHCI_CMND_STS) |
				(SERREN | PERREN | MASTEREN | MEMEN),
				(pci_conf_ohci_base + OHCI_CMND_STS));
	} else {
		printk(KERN_ERR "Don't found OHCI controller. %x\n", val);
		retval = -1;
	}
	iounmap(pci_conf_ohci_base);

	return retval;
}

static int __init usbh_ehci_init(int ch)
{
	u32 val;
	int retval;

	void __iomem *pci_conf_ehci_base
		= ioremap_nocache((PCI_CONF_EHCI_BASE + (ch * 0x20000)),
								 0x100);

	if (!pci_conf_ehci_base)
		return -ENOMEM;

	val = ioread32((pci_conf_ehci_base + EHCI_VID_DID));
	if (val == EHCI_ID) {
		/* EHCI_BASEAD */
		iowrite32(SHUSBH_EHCI_BASE,
				(pci_conf_ehci_base + EHCI_BASEAD));

		/* System error enable, Parity error enable, */
		/* PCI Master enable, Memory cycle enable */
		iowrite32(ioread32(pci_conf_ehci_base + EHCI_CMND_STS) |
				(SERREN | PERREN | MASTEREN | MEMEN),
				(pci_conf_ehci_base + EHCI_CMND_STS));
		retval = 0;
	} else {
		printk(KERN_ERR "Don't found EHCI controller. %x\n", val);
		retval = -1;
	}
	iounmap(pci_conf_ehci_base);

	return retval;
}

static void __init usbh_pci_int_enable(int ch)
{
	void __iomem *ahbpci_base =
		ioremap_nocache((AHBPCI_BASE + (ch * 0x20000)), 0x400);
	u32 data;

	if (!ahbpci_base)
		return;

	/* PCI_INT_ENABLE */
	data = ioread32((ahbpci_base + PCI_INT_ENABLE));
	data |= USBH_PMEEN | USBH_INTBEN | USBH_INTAEN;
	iowrite32(data, (ahbpci_base + PCI_INT_ENABLE));

	iounmap(ahbpci_base);
}

static int __init usbh_init(void)
{
	struct clk *clk_hs, *clk_ehci;
#ifdef CONFIG_USB_XHCI_HCD
	struct clk *clk_xhci;
#endif /* CONFIG_USB_XHCI_HCD */
	void __iomem *hs_usb = ioremap_nocache(0xE6590000, 0x1ff);
	unsigned int ch = 0;
	int ret = 0;
	u32 ugctrl2 = 0x00000011;

#ifdef CONFIG_USB_R8A66597
	ch++;
	ugctrl2 |= UGCTRL2_USB0SEL_HSUSB;
#endif /* CONFIG_USB_R8A66597 */
#ifdef CONFIG_USB_XHCI_HCD
	ugctrl2 |= UGCTRL2_USB2SEL_XHCI;
#endif /* CONFIG_USB_XHCI_HCD */

	if (!hs_usb)
		return -ENOMEM;

	clk_hs = clk_get(NULL, "hs_usb");
	if (IS_ERR(clk_hs)) {
		ret = PTR_ERR(clk_hs);
		goto err_iounmap;
	}

	clk_ehci = clk_get(NULL, "usb_fck");
	if (IS_ERR(clk_ehci)) {
		ret = PTR_ERR(clk_ehci);
		goto err_iounmap;
	}

	clk_enable(clk_hs);
	clk_enable(clk_ehci);

#ifdef CONFIG_USB_XHCI_HCD
	clk_xhci = clk_get(NULL, "ss_usb");
	if (IS_ERR(clk_xhci)) {
		ret = PTR_ERR(clk_xhci);
		goto err_iounmap;
	}

	clk_enable(clk_xhci);
#endif /* CONFIG_USB_XHCI_HCD */

	iowrite32(ugctrl2, hs_usb + 0x184);

	for (; ch < SHUSBH_MAX_CH; ch++) {
		if (1 != ch) {
			/* internal pci-bus bridge initialize */
			usbh_internal_pci_bridge_init(ch);

			/* ohci initialize */
			usbh_ohci_init(ch);

			/* ehci initialize */
			usbh_ehci_init(ch);

			/* pci int enable */
			usbh_pci_int_enable(ch);
		}
	}
err_iounmap:
	iounmap(hs_usb);

	return ret;
}

/* VIN */
static const struct resource vin0_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xe6ef0000, SZ_4K, "vin0"),
	DEFINE_RES_IRQ(gic_spi(188)),
};

static const struct resource vin1_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xe6ef1000, SZ_4K, "vin1"),
	DEFINE_RES_IRQ(gic_spi(189)),
};

static const struct resource vin2_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xe6ef2000, SZ_4K, "vin2"),
	DEFINE_RES_IRQ(gic_spi(190)),
};

static const struct resource *vin_resources[3] = {
	vin0_resources,
	vin1_resources,
	vin2_resources,
};

static struct vin_info vin_info[] = {
	[0] = {
		.input = VIN_INPUT_ITUR_BT709_24BIT,
		.flags = 0,
	},
	[1] = {
		.input = VIN_INPUT_ITUR_BT656_8BIT,
		.flags = 0,
	},
	[2] = {
		.input = VIN_INPUT_UNDEFINED,
		.flags = 0,
	},
};

void __init r8a7791_register_vin(unsigned int index)
{
	struct platform_device_info info = {
		.name = "vin",
		.id = index,
		.data = &vin_info[index],
		.size_data = sizeof(struct vin_info),
		.dma_mask = DMA_BIT_MASK(32),
	};

	if (index >= ARRAY_SIZE(vin_resources))
		return;

	info.res = vin_resources[index];
	info.num_res = 2;

	platform_device_register_full(&info);
}

static struct platform_device *r8a7791_early_devices[] __initdata = {
	&powervr_device,
};

void __init r8a7791_add_dt_devices(void)
{
	r8a7791_register_scif(SCIFA0);
	r8a7791_register_scif(SCIFA1);
	r8a7791_register_scif(SCIFB0);
	r8a7791_register_scif(SCIFB1);
	r8a7791_register_scif(SCIFB2);
	r8a7791_register_scif(SCIFA2);
	r8a7791_register_scif(SCIF0);
	r8a7791_register_scif(SCIF1);
	r8a7791_register_scif(HSCIF0);
	r8a7791_register_scif(HSCIF1);
	r8a7791_register_scif(SCIF2);
	r8a7791_register_scif(SCIF3);
	r8a7791_register_scif(SCIF4);
	r8a7791_register_scif(SCIF5);
	r8a7791_register_scif(SCIFA3);
	r8a7791_register_scif(SCIFA4);
	r8a7791_register_scif(SCIFA5);
	r8a7791_register_scif(HSCIF2);
	r8a7791_register_cmt(00);
}

void __init r8a7791_add_standard_devices(void)
{
	void __iomem *pfcctl;

	pfcctl = ioremap(0xe6060000, 0x300);

	/* SD control registers IOCTRLn: SD pins driving ability */
	iowrite32(~0x8000aaaa, pfcctl);		/* PMMR */
	iowrite32(0x8000aaaa, pfcctl + 0x60);	/* IOCTRL0 */
	iowrite32(~0xaaaaaaaa, pfcctl);		/* PMMR */
	iowrite32(0xaaaaaaaa, pfcctl + 0x64);	/* IOCTRL1 */
	iowrite32(~0x55554401, pfcctl);		/* PMMR */
	iowrite32(0x55554401, pfcctl + 0x88);	/* IOCTRL5 */
	iowrite32(~0xffffff00, pfcctl);		/* PMMR */
	iowrite32(0xffffff00, pfcctl + 0x8c);	/* IOCTRL6 */
	iounmap(pfcctl);

	usbh_init();

	r8a7791_pm_init();

	r8a7791_init_pm_domain(&r8a7791_sgx);

	r8a7791_add_dt_devices();
	r8a7791_register_irqc(0);
	r8a7791_register_thermal();
	r8a7791_register_alsa(0);
	r8a7791_register_audma(l, SHDMA_DEVID_AUDIO_LO);
	r8a7791_register_audma(u, SHDMA_DEVID_AUDIO_UP);
	r8a7791_register_sysdma(l, SHDMA_DEVID_SYS_LO);
	r8a7791_register_sysdma(u, SHDMA_DEVID_SYS_UP);
	r8a7791_register_audmapp(SHDMA_DEVID_AUDIOPP);
#ifdef CONFIG_USB_R8A66597
	r8a7791_register_gpio_vbus();
#endif
	r8a7791_register_i2c(0);
	r8a7791_register_i2c(1);
	r8a7791_register_i2c(2);
	r8a7791_register_i2c(3);
	r8a7791_register_i2c(4);
	r8a7791_register_i2c(5);
	r8a7791_register_iic(6);
	r8a7791_register_msiof(0);
	r8a7791_register_msiof(1);
	r8a7791_register_msiof(2);
	r8a7791_register_qspi();
	r8a7791_register_sata(0);
	r8a7791_register_sata(1);
#ifdef CONFIG_USB_R8A66597
	r8a7791_register_usbf();
#endif
#ifndef CONFIG_USB_R8A66597
	r8a7791_register_usbh_ehci(0);
#endif
	r8a7791_register_usbh_ehci(1);
#ifndef CONFIG_USB_R8A66597
	r8a7791_register_usbh_ohci(0);
#endif
	r8a7791_register_usbh_ohci(1);
	r8a7791_register_usbh_xhci(0);
	r8a7791_register_vin(0);
	r8a7791_register_vin(1);
	r8a7791_register_vin(2);

	platform_add_devices(r8a7791_early_devices,
		     ARRAY_SIZE(r8a7791_early_devices));

	r8a7791_add_device_to_domain(&r8a7791_sgx, &powervr_device);
}

void __init r8a7791_init_early(void)
{
#ifndef CONFIG_ARM_ARCH_TIMER
	shmobile_setup_delay(1300, 2, 4); /* Cortex-A15 @ 1300MHz */
#endif
}

#ifdef CONFIG_USE_OF

static const char * const r8a7791_boards_compat_dt[] __initconst = {
	"renesas,r8a7791",
	NULL,
};

DT_MACHINE_START(R8A7791_DT, "Generic R8A7791 (Flattened Device Tree)")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_late	= shmobile_init_late,
	.timer		= &rcar_gen2_timer,
	.dt_compat	= r8a7791_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */

/*
 * r8a7790 processor support
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

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/serial_sci.h>
#include <linux/sh_eth.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/clk.h>
#include <linux/usb/ehci_pdriver.h>
#include <linux/usb/ohci_pdriver.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7790.h>
#include <asm/mach/arch.h>

static const struct resource pfc_resources[] = {
	DEFINE_RES_MEM(0xe6060000, 0x250),
};

#define R8A7790_GPIO(idx)						\
static struct resource r8a7790_gpio##idx##_resources[] = {		\
	DEFINE_RES_MEM(0xe6050000 + 0x1000 * (idx), 0x50),		\
	DEFINE_RES_IRQ(gic_spi(4 + (idx))),				\
};									\
									\
static struct gpio_rcar_config r8a7790_gpio##idx##_platform_data = {	\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= 32,						\
	.pctl_name	= "pfc-r8a7790",				\
};									\

R8A7790_GPIO(0);
R8A7790_GPIO(1);
R8A7790_GPIO(2);
R8A7790_GPIO(3);
R8A7790_GPIO(4);
R8A7790_GPIO(5);

#define r8a7790_register_gpio(idx)					\
	platform_device_register_resndata(&platform_bus, "gpio_rcar", idx, \
		r8a7790_gpio##idx##_resources,				\
		ARRAY_SIZE(r8a7790_gpio##idx##_resources),		\
		&r8a7790_gpio##idx##_platform_data,			\
		sizeof(r8a7790_gpio##idx##_platform_data))

void __init r8a7790_pinmux_init(void)
{
	platform_device_register_simple("pfc-r8a7790", -1, pfc_resources,
					ARRAY_SIZE(pfc_resources));
	r8a7790_register_gpio(0);
	r8a7790_register_gpio(1);
	r8a7790_register_gpio(2);
	r8a7790_register_gpio(3);
	r8a7790_register_gpio(4);
	r8a7790_register_gpio(5);
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
	.scscr = SCSCR_RE | SCSCR_TE | SCSCR_CKE0,	\
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
	.scscr = SCSCR_RE | SCSCR_TE | SCSCR_CKE1,	\
}

enum { SCIFA0, SCIFA1, SCIFB0, SCIFB1, SCIFB2, SCIFA2, SCIF0, SCIF1 };

static const struct plat_sci_port scif[] = {
	SCIFA_DATA(SCIFA0, 0xe6c40000, gic_spi(144)), /* SCIFA0 */
	SCIFA_DATA(SCIFA1, 0xe6c50000, gic_spi(145)), /* SCIFA1 */
	SCIFB_DATA(SCIFB0, 0xe6c20000, gic_spi(148)), /* SCIFB0 */
	SCIFB_DATA(SCIFB1, 0xe6c30000, gic_spi(149)), /* SCIFB1 */
	SCIFB_DATA(SCIFB2, 0xe6ce0000, gic_spi(150)), /* SCIFB2 */
	SCIFA_DATA(SCIFA2, 0xe6c60000, gic_spi(151)), /* SCIFA2 */
	SCIF_DATA(SCIF0, 0xe6e60000, gic_spi(152)), /* SCIF0 */
	SCIF_DATA(SCIF1, 0xe6e68000, gic_spi(153)), /* SCIF1 */
};

static inline void r8a7790_register_scif(int idx)
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
		.start  = gic_spi(162),		/* IRQ0 */
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

static struct resource powervr_resources[] = {
	{
		.start  = 0xfd000000,
		.end    = 0xfd00ffff,
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

static struct resource rcar_du_resources[] = {
	[0] = {
		.name	= "Display Unit",
		.start	= 0xfeb00000,
		.end	= 0xfeb6002c,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0xfeb90000,
		.end	= 0xfeb9001c,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= 0xfeb94000,
		.end	= 0xfeb9401c,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= gic_spi(256),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rcar_du_encoder_data rcar_du_encoders[] = {
	{
		.encoder = RCAR_DU_ENCODER_VGA,
		.output = 0,
	},
	{
		.encoder = RCAR_DU_ENCODER_LVDS,
		.output = 1,
		.u.lvds.panel = {
			.width_mm = 210,
			.height_mm = 158,
			.mode = {
				.clock = 65000,
				.hdisplay = 1024,
				.hsync_start = 1048,
				.hsync_end = 1184,
				.htotal = 1344,
				.vdisplay = 768,
				.vsync_start = 771,
				.vsync_end = 777,
				.vtotal = 806,
				.flags = 0,
			},
		},
	},
};

static struct rcar_du_platform_data rcar_du_pdata = {
	.encoders = rcar_du_encoders,
	.num_encoders = ARRAY_SIZE(rcar_du_encoders),
};

static struct platform_device rcar_du_device = {
	.name		= "rcar-du",
	.num_resources	= ARRAY_SIZE(rcar_du_resources),
	.resource	= rcar_du_resources,
	.dev	= {
		.platform_data = &rcar_du_pdata,
		.coherent_dma_mask = ~0,
	},
};

static u64 usb_dmamask = ~(u32)0;

struct usb_ehci_pdata ehci_pdata = {
	.caps_offset	= 0,
	.has_tt		= 0,
};

struct usb_ohci_pdata ohci_pdata = {
};

static struct resource ehci0_resources[] = {
	[0] = {
		.start	= 0xee080000 + 0x1000,
		.end	= 0xee080000 + 0x1000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(108),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ehci0_device = {
	.name	= "ehci-platform",
	.id	= 0,
	.dev	= {
		.platform_data		= &ehci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ehci0_resources),
	.resource	= ehci0_resources,
};

static struct resource ohci0_resources[] = {
	[0] = {
		.start	= 0xee080000,
		.end	= 0xee080000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(108),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ohci0_device = {
	.name	= "ohci-platform",
	.id	= 0,
	.dev	= {
		.platform_data		= &ohci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ohci0_resources),
	.resource	= ohci0_resources,
};

static struct resource ehci1_resources[] = {
	[0] = {
		.start	= 0xee0a0000 + 0x1000,
		.end	= 0xee0a0000 + 0x1000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(112),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ehci1_device = {
	.name	= "ehci-platform",
	.id	= 1,
	.dev	= {
		.platform_data		= &ehci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ehci1_resources),
	.resource	= ehci1_resources,
};

static struct resource ohci1_resources[] = {
	[0] = {
		.start	= 0xee0a0000,
		.end	= 0xee0a0000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(112),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ohci1_device = {
	.name	= "ohci-platform",
	.id	= 1,
	.dev	= {
		.platform_data		= &ohci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ohci1_resources),
	.resource	= ohci1_resources,
};

static struct resource ehci2_resources[] = {
	[0] = {
		.start	= 0xee0c0000 + 0x1000,
		.end	= 0xee0c0000 + 0x1000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(113),
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ehci2_device = {
	.name	= "ehci-platform",
	.id	= 2,
	.dev	= {
		.platform_data		= &ehci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ehci2_resources),
	.resource	= ehci2_resources,
};

static struct resource ohci2_resources[] = {
	[0] = {
		.start	= 0xee0c0000,
		.end	= 0xee0c0000 + 0x0fff - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(113),
		.flags	= IORESOURCE_IRQ,
	},
};


struct platform_device ohci2_device = {
	.name	= "ohci-platform",
	.id	= 2,
	.dev	= {
		.platform_data		= &ohci_pdata,
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ohci2_resources),
	.resource	= ohci2_resources,
};

static void __init usbh_internal_pci_bridge_init(int ch)
{
	u32 data;
	void __iomem *ahbpci_base =
		ioremap_nocache((AHBPCI_BASE + (ch * 0x20000)), 0x400);
	void __iomem *pci_conf_ahbpci_bas =
		ioremap_nocache((PCI_CONF_AHBPCI_BAS + (ch * 0x20000)),
								0x100);

	/* Clock & Reset & Direct Power Down */
	data = ioread32(ahbpci_base + USBCTR);
	data &= ~(DIRPD);
	iowrite32(data, (ahbpci_base + USBCTR));

	data &= ~(PLL_RST | PCICLK_MASK | USBH_RST);
	iowrite32(data | PCI_AHB_WIN1_SIZE_1G, (ahbpci_base + USBCTR));

	data = ioread32((ahbpci_base + AHB_BUS_CTR));
	if (data == AHB_BUS_CTR_SET)
		return;

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
	iowrite32(((ioread32(pci_conf_ahbpci_bas + CMND_STS) & ~0x00100000)
			| (SERREN | PERREN | MASTEREN | MEMEN)),
			(pci_conf_ahbpci_bas + CMND_STS));

	/****** PCI Configuration Registers for OHCI/EHCI ******/
	iowrite32(PCIWIN1_PCICMD | AHB_CFG_HOST,
			(ahbpci_base + AHBPCI_WIN1_CTR));

	iounmap(ahbpci_base);
	iounmap(pci_conf_ahbpci_bas);
}

static int __init usbh_ohci_init(int ch)
{
	u32 val;
	int retval;

	void __iomem *pci_conf_ohci_base
		= ioremap_nocache((PCI_CONF_OHCI_BASE + (ch * 0x20000)),
								0x100);

	val = ioread32((pci_conf_ohci_base + OHCI_VID_DID));

	if (val == OHCI_ID) {
		/* OHCI_BASEAD */
		iowrite32(SHUSBH_OHCI_BASE,
				(pci_conf_ohci_base + OHCI_BASEAD));
		retval = 0;

		/* System error enable, Parity error enable, */
		/* PCI Master enable, Memory cycle enable */
		iowrite32(ioread32(pci_conf_ohci_base + OHCI_CMND_STS)
				| (SERREN | PERREN | MASTEREN | MEMEN),
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

	/* PCI_INT_ENABLE */
	data = ioread32((ahbpci_base + PCI_INT_ENABLE));
	data |= USBH_PMEEN | USBH_INTBEN | USBH_INTAEN;
	iowrite32(data, (ahbpci_base + PCI_INT_ENABLE));

	iounmap(ahbpci_base);
}

static int __init usbh_init(void)
{
	struct clk *clk_hs, *clk_ehci;
	u32 hs_usb = 0xE6590000;
	unsigned int ch;

	clk_hs = clk_get(NULL, "hs_usb");
	if (IS_ERR(clk_hs))
		clk_hs = NULL;

	clk_enable(clk_hs);

	clk_ehci = clk_get(NULL, "usb_fck");
	if (IS_ERR(clk_ehci))
		clk_ehci = NULL;

	clk_enable(clk_ehci);

	/* Set EHCI for UGCTRL2 */
	__raw_writel(0x00000011, (hs_usb + 0x184));

	for (ch = 0; ch < SHUSBH_MAX_CH; ch++) {
		/* internal pci-bus bridge initialize */
		usbh_internal_pci_bridge_init(ch);

		/* ohci initialize */
		usbh_ohci_init(ch);

		/* ehci initialize */
		usbh_ehci_init(ch);

		/* pci int enable */
		usbh_pci_int_enable(ch);
	}

	return 0;
}

static struct platform_device *r8a7790_early_devices[] __initdata = {
	&eth_device,
	&powervr_device,
	&rcar_du_device,
	&ehci0_device,
	&ohci0_device,
	&ehci1_device,
	&ohci1_device,
	&ehci2_device,
	&ohci2_device,
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

#define r8a7790_register_irqc(idx)					\
	platform_device_register_resndata(&platform_bus, "renesas_irqc", \
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

void __init r8a7790_add_standard_devices(void)
{
	r8a7790_pm_init();

	r8a7790_init_pm_domain(&r8a7790_rgx);

	r8a7790_register_scif(SCIFA0);
	r8a7790_register_scif(SCIFA1);
	r8a7790_register_scif(SCIFB0);
	r8a7790_register_scif(SCIFB1);
	r8a7790_register_scif(SCIFB2);
	r8a7790_register_scif(SCIFA2);
	r8a7790_register_scif(SCIF0);
	r8a7790_register_scif(SCIF1);
	r8a7790_register_irqc(0);
	usbh_init();
	platform_add_devices(r8a7790_early_devices,
			     ARRAY_SIZE(r8a7790_early_devices));

	r8a7790_add_device_to_domain(&r8a7790_rgx, &powervr_device);
}

void __init r8a7790_timer_init(void)
{
	void __iomem *cntcr;

	/* make sure arch timer is started by setting bit 0 of CNTCT */
	cntcr = ioremap(0xe6080000, PAGE_SIZE);
	iowrite32(1, cntcr);
	iounmap(cntcr);

	shmobile_timer_init();
}

struct sys_timer r8a7790_timer = {
        .init           = r8a7790_timer_init,
};

#ifdef CONFIG_USE_OF
void __init r8a7790_add_standard_devices_dt(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char *r8a7790_boards_compat_dt[] __initdata = {
	"renesas,r8a7790",
	NULL,
};

DT_MACHINE_START(R8A7790_DT, "Generic R8A7790 (Flattened Device Tree)")
	.init_irq	= irqchip_init,
	.init_machine	= r8a7790_add_standard_devices_dt,
	.timer		= &r8a7790_timer,
	.dt_compat	= r8a7790_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */

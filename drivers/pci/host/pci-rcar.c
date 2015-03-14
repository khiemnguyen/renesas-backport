/*
 * PCIe driver for Renesas R-Car SoCs
 *  Copyright (C) 2013 Renesas Electronics Europe Ltd
 *
 * Based on:
 *  arch/sh/drivers/pci/pcie-sh7786.c
 *  arch/sh/drivers/pci/ops-sh7786.c
 *  Copyright (C) 2009 - 2011  Paul Mundt
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "pci-rcar.h"

#define DRV_NAME "rcar-pcie"

enum chip_id {
	RCAR_GENERIC,
	RCAR_H1,
};

#define INT_PCI_MSI_NR	32

#define RCONF(x)	(PCICONF(0)+(x))
#define REXPCAP(x)	(EXPCAP(0)+(x))
#define RVCCAP(x)	(VCCAP(0)+(x))

#define  PCIE_CONF_BUS(b)	(((b) & 0xff) << 24)
#define  PCIE_CONF_DEV(d)	(((d) & 0x1f) << 19)
#define  PCIE_CONF_FUNC(f)	(((f) & 0x7) << 16)

#define NR_PCI_RESOURCES 4

extern void pcibios_report_status(u_int status_mask, int warn);

struct rcar_msi {
	DECLARE_BITMAP(used, INT_PCI_MSI_NR);
	struct irq_domain *domain;
	struct msi_chip chip;
	unsigned long pages;
	struct mutex lock;
	int irq;
};

static inline struct rcar_msi *to_rcar_msi(struct msi_chip *chip)
{
	return container_of(chip, struct rcar_msi, chip);
}

/* Structure representing the PCIe interface */
struct rcar_pcie {
	struct device		*dev;
	void __iomem		*base;
	int			irq;
	struct clk		*clk;
	struct resource		*res[NR_PCI_RESOURCES];
	int			haslink;
	enum chip_id		chip;
	u8			root_bus_nr;
	struct			rcar_msi msi;
};

static inline struct rcar_pcie *sys_to_pcie(struct pci_sys_data *sys)
{
	return sys->private_data;
}

static void
pci_write_reg(struct rcar_pcie *pcie, unsigned long val, unsigned long reg)
{
	writel(val, pcie->base + reg);
}

static unsigned long
pci_read_reg(struct rcar_pcie *pcie, unsigned long reg)
{
	return readl(pcie->base + reg);
}

enum {
	PCI_ACCESS_READ,
	PCI_ACCESS_WRITE,
};

static void rcar_rmw32(struct rcar_pcie *pcie,
			    int where, u32 mask, u32 data)
{
	int shift = 8 * (where & 3);
	u32 val = pci_read_reg(pcie, where & ~3);
	val &= ~(mask << shift);
	val |= data << shift;
	pci_write_reg(pcie, val, where & ~3);
}

static u32 rcar_read_conf(struct rcar_pcie *pcie, int where)
{
	int shift = 8 * (where & 3);
	u32 val = pci_read_reg(pcie, where & ~3);
	return val >> shift;
}

static int rcar_pcie_config_access(struct rcar_pcie *pcie,
		unsigned char access_type, struct pci_bus *bus,
		unsigned int devfn, int where, u32 *data)
{
	int dev, func, reg, index;

	dev = PCI_SLOT(devfn);
	func = PCI_FUNC(devfn);
	reg = where & ~3;
	index = reg / 4;

	if (bus->number > 255 || dev > 31 || func > 7)
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	/*
	 * While each channel has its own memory-mapped extended config
	 * space, it's generally only accessible when in endpoint mode.
	 * When in root complex mode, the controller is unable to target
	 * itself with either type 0 or type 1 accesses, and indeed, any
	 * controller initiated target transfer to its own config space
	 * result in a completer abort.
	 *
	 * Each channel effectively only supports a single device, but as
	 * the same channel <-> device access works for any PCI_SLOT()
	 * value, we cheat a bit here and bind the controller's config
	 * space to devfn 0 in order to enable self-enumeration. In this
	 * case the regular ECAR/ECDR path is sidelined and the mangled
	 * config access itself is initiated as an internal bus  transaction.
	 */
	if (pci_is_root_bus(bus)) {
		if (dev != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		if (access_type == PCI_ACCESS_READ)
			*data = pci_read_reg(pcie, PCICONF(index));
		else
			pci_write_reg(pcie, *data, PCICONF(index));

		return PCIBIOS_SUCCESSFUL;
	}

	/* Clear errors */
	pci_write_reg(pcie, pci_read_reg(pcie, PCIEERRFR), PCIEERRFR);

	/* Set the PIO address */
	pci_write_reg(pcie, PCIE_CONF_BUS(bus->number) | PCIE_CONF_DEV(dev) |
				PCIE_CONF_FUNC(func) | reg, PCIECAR);

	/* Enable the configuration access */
	if (bus->parent->number == pcie->root_bus_nr)
		pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE0, PCIECCTLR);
	else
		pci_write_reg(pcie, CONFIG_SEND_ENABLE | TYPE1, PCIECCTLR);

	/* Check for errors */
	if (pci_read_reg(pcie, PCIEERRFR) & UNSUPPORTED_REQUEST)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/* Check for master and target aborts */
	if (rcar_read_conf(pcie, RCONF(PCI_STATUS)) &
		(PCI_STATUS_REC_MASTER_ABORT | PCI_STATUS_REC_TARGET_ABORT))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (access_type == PCI_ACCESS_READ)
		*data = pci_read_reg(pcie, PCIECDR);
	else
		pci_write_reg(pcie, *data, PCIECDR);

	/* Disable the configuration access */
	pci_write_reg(pcie, 0, PCIECCTLR);

	return PCIBIOS_SUCCESSFUL;
}

static int rcar_pcie_read_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct rcar_pcie *pcie = sys_to_pcie(bus->sysdata);
	int ret;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ret = rcar_pcie_config_access(pcie, PCI_ACCESS_READ,
				      bus, devfn, where, val);
	if (ret != PCIBIOS_SUCCESSFUL) {
		*val = 0xffffffff;
		return ret;
	}

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 2))) & 0xffff;

	dev_dbg(&bus->dev, "pcie-config-read: bus=%3d devfn=0x%04x "
		"where=0x%04x size=%d val=0x%08lx\n", bus->number,
		devfn, where, size, (unsigned long)*val);

	return ret;
}

static int rcar_pcie_write_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct rcar_pcie *pcie = sys_to_pcie(bus->sysdata);
	int shift, ret;
	u32 data;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ret = rcar_pcie_config_access(pcie, PCI_ACCESS_READ,
				      bus, devfn, where, &data);
	if (ret != PCIBIOS_SUCCESSFUL)
		return ret;

	dev_dbg(&bus->dev, "pcie-config-write: bus=%3d devfn=0x%04x "
		"where=0x%04x size=%d val=0x%08lx\n", bus->number,
		devfn, where, size, (unsigned long)val);

	if (size == 1) {
		shift = 8 * (where & 3);
		data &= ~(0xff << shift);
		data |= ((val & 0xff) << shift);
	} else if (size == 2) {
		shift = 8 * (where & 2);
		data &= ~(0xffff << shift);
		data |= ((val & 0xffff) << shift);
	} else
		data = val;

	ret = rcar_pcie_config_access(pcie, PCI_ACCESS_WRITE,
				      bus, devfn, where, &data);

	return ret;
}

static struct pci_ops rcar_pcie_ops = {
	.read	= rcar_pcie_read_conf,
	.write	= rcar_pcie_write_conf,
};

static int rcar_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct rcar_pcie *pcie = sys_to_pcie(sys);
	int ret, i, win;

	pcie->root_bus_nr = sys->busnr;

	/* Setup IO mapped memory accesses */
	ret = pci_ioremap_io(0, pcie->res[0]->start);
	if (ret)
		return ret;

	/* Setup PCI resources */
	/* Ignore first resource, as it's for IO */
	for (i = 1; i < NR_PCI_RESOURCES; i++) {
		struct resource *res = pcie->res[i];
		pci_add_resource_offset(&sys->resources, res, sys->mem_offset);
	}

	/* Setup PCIe address space mappings for each resource */
	for (i = win = 0; i < NR_PCI_RESOURCES; i++) {
		struct resource *res = pcie->res[i];
		resource_size_t size, bus_addr;
		u32 mask;

		pci_write_reg(pcie, 0x00000000, PCIEPTCTLR(win));

		/*
		 * The PAMR mask is calculated in units of 128Bytes, which
		 * keeps things pretty simple.
		 */
		size = resource_size(res);
		mask = (roundup_pow_of_two(size) / SZ_128) - 1;
		pci_write_reg(pcie, mask << 7, PCIEPAMR(win));

		bus_addr = i ? res->start : 0;
		pci_write_reg(pcie, upper_32_bits(bus_addr), PCIEPARH(win));
		pci_write_reg(pcie, lower_32_bits(bus_addr), PCIEPARL(win));

		mask = PAR_ENABLE;
		/* First resource is for IO */
		if (i == 0)
			mask |= IO_SPACE;

		pci_write_reg(pcie, mask, PCIEPTCTLR(win));

		win++;
	}

	return 1;
}

static int rcar_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct rcar_pcie *pcie = sys_to_pcie(dev->bus->sysdata);
	return pcie->irq;
}

static void rcar_pcie_add_bus(struct pci_bus *bus)
{
	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		struct rcar_pcie *pcie = sys_to_pcie(bus->sysdata);

		bus->msi = &pcie->msi.chip;
	}
}

static void __init rcar_pcie_enable(struct rcar_pcie *pcie)
{
	struct hw_pci hw;

	memset(&hw, 0, sizeof(hw));

	hw.nr_controllers = 1;
	hw.private_data   = (void **)&pcie;
	hw.setup          = rcar_pcie_setup,
	hw.map_irq        = rcar_pcie_map_irq,
	hw.ops		  = &rcar_pcie_ops,
	hw.add_bus	  = rcar_pcie_add_bus;

	pci_common_init(&hw);
}

static int __init phy_wait_for_ack(struct rcar_pcie *pcie)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if (pci_read_reg(pcie, H1_PCIEPHYADRR) & PHY_ACK)
			return 0;

		udelay(100);
	}

	dev_err(pcie->dev, "Access to PCIe phy timed out\n");

	return -ETIMEDOUT;
}

static void __init phy_write_reg(struct rcar_pcie *pcie,
				 unsigned int rate, unsigned int addr,
				 unsigned int lane, unsigned int data)
{
	unsigned long phyaddr;

	phyaddr = WRITE_CMD |
		((rate & 1) << RATE_POS) |
		((lane & 0xf) << LANE_POS) |
		((addr & 0xff) << ADR_POS);

	/* Set write data */
	pci_write_reg(pcie, data, H1_PCIEPHYDOUTR);
	pci_write_reg(pcie, phyaddr, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);

	/* Clear command */
	pci_write_reg(pcie, 0, H1_PCIEPHYDOUTR);
	pci_write_reg(pcie, 0, H1_PCIEPHYADRR);

	/* Ignore errors as they will be dealt with if the data link is down */
	phy_wait_for_ack(pcie);
}

static int __init rcar_pcie_phy_init_rcar_h1(struct rcar_pcie *pcie)
{
	unsigned int timeout = 100;

	/* Initialize the phy */
	phy_write_reg(pcie, 0, 0x42, 0x1, 0x0EC34191);
	phy_write_reg(pcie, 1, 0x42, 0x1, 0x0EC34180);
	phy_write_reg(pcie, 0, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 1, 0x43, 0x1, 0x00210188);
	phy_write_reg(pcie, 0, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x44, 0x1, 0x015C0014);
	phy_write_reg(pcie, 1, 0x4C, 0x1, 0x786174A0);
	phy_write_reg(pcie, 1, 0x4D, 0x1, 0x048000BB);
	phy_write_reg(pcie, 0, 0x51, 0x1, 0x079EC062);
	phy_write_reg(pcie, 0, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x52, 0x1, 0x20000000);
	phy_write_reg(pcie, 1, 0x56, 0x1, 0x00003806);

	phy_write_reg(pcie, 0, 0x60, 0x1, 0x004B03A5);
	phy_write_reg(pcie, 0, 0x64, 0x1, 0x3F0F1F0F);
	phy_write_reg(pcie, 0, 0x66, 0x1, 0x00008000);

	while (timeout--) {
		if (pci_read_reg(pcie, H1_PCIEPHYSR))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static int __init rcar_pcie_wait_for_dl(struct rcar_pcie *pcie)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if ((pci_read_reg(pcie, PCIETSTR) & DATA_LINK_ACTIVE))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static void __init rcar_pcie_enable_interrupts(struct rcar_pcie *pcie)
{
	/*
	 * Enable status & system error interrupts, except not "Tx All Empty",
	 * as this fires all the time during normal operation and "Unsupported
	 * Request."
	 */
	pci_write_reg(pcie, 0xd000301f, PCIEINTER);
	pci_write_reg(pcie, 0x10303362, PCIEERRFER);

	/*
	 * Enable Fatal, Non-Fatal and Correctable error interrupts.
	 * For Fatal and Non-Fatal errors, we just report they happened.
	 * For Correctable errors, we clear the relevant config reg bits.
	 */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_RTCTL), 0, PCI_EXP_RTCTL_SEFEE);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_RTCTL), 0, PCI_EXP_RTCTL_SENFEE);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_RTCTL), 0, PCI_EXP_RTCTL_SECEE);

	/* Note: PHY status change interrupts not used */

	/* Enable MSI/INTx enable change */
	pci_write_reg(pcie, 0x10001000, PCIETIER);
}

static void __init rcar_pcie_hw_init(struct rcar_pcie *pcie)
{
	/* Initialise R-Car H1 PHY & wait for it to be ready */
	if (pcie->chip == RCAR_H1)
		if (rcar_pcie_phy_init_rcar_h1(pcie))
			return;

	/* Begin initialization */
	pci_write_reg(pcie, 0, PCIETCTLR);

	/* Set mode */
	pci_write_reg(pcie, 1, PCIEMSR);

	/*
	 * For target transfers, setup a single 64-bit 4GB mapping at address
	 * 0. The hardware can only map memory that starts on a power of two
	 * boundary, and size is power of 2, so best to ignore it.
	 */
	pci_write_reg(pcie, 0, PCIEPRAR(0));
	pci_write_reg(pcie, 0, PCIELAR(0));
	pci_write_reg(pcie, 0xfffffff0UL | LAM_PREFETCH |
		LAM_64BIT | LAR_ENABLE, PCIELAMR(0));
	pci_write_reg(pcie, 0, PCIELAR(1));
	pci_write_reg(pcie, 0, PCIEPRAR(1));
	pci_write_reg(pcie, 0, PCIELAMR(1));

	/*
	 * Initial header for port config space is type 1, set the device
	 * class to match. Hardware takes care of propagating the IDSETR
	 * settings, so there is no need to bother with a quirk.
	 */
	pci_write_reg(pcie, PCI_CLASS_BRIDGE_PCI << 16, IDSETR1);

	/*
	 * Setup Secondary Bus Number & Subordinate Bus Number, even though
	 * they aren't used, to avoid bridge being detected as broken.
	 */
	rcar_rmw32(pcie, RCONF(PCI_SECONDARY_BUS), 0xff, 1);
	rcar_rmw32(pcie, RCONF(PCI_SUBORDINATE_BUS), 0xff, 1);

	/* Initialize default capabilities. */
	rcar_rmw32(pcie, REXPCAP(0), 0, PCI_CAP_ID_EXP);
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_FLAGS),
		PCI_EXP_FLAGS_TYPE, PCI_EXP_TYPE_ROOT_PORT << 4);
	rcar_rmw32(pcie, RCONF(PCI_HEADER_TYPE), 0x7f,
		PCI_HEADER_TYPE_BRIDGE);

	/* Enable data link layer active state reporting */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_LNKCAP), 0, PCI_EXP_LNKCAP_DLLLARC);

	/* Write out the physical slot number = 0 */
	rcar_rmw32(pcie, REXPCAP(PCI_EXP_SLTCAP), PCI_EXP_SLTCAP_PSN, 0);

	/* Set the completion timer timeout to the maximum 50ms. */
	rcar_rmw32(pcie, TLCTLR+1, 0x3f, 50);

	/* Terminate list of capabilities (Next Capability Offset=0) */
	rcar_rmw32(pcie, RVCCAP(0), 0xfff0, 0);

	/* Enable MAC data scrambling. */
	rcar_rmw32(pcie, MACCTLR, SCRAMBLE_DISABLE, 0);

	/* Enable MSI */
	if (IS_ENABLED(CONFIG_PCI_MSI))
		pci_write_reg(pcie, 0x101f0000, PCIEMSITXR);

	rcar_pcie_enable_interrupts(pcie);

	/* Finish initialization - establish a PCI Express link */
	pci_write_reg(pcie, CFINIT, PCIETCTLR);

	/* This will timeout if we don't have a link. */
	pcie->haslink = !rcar_pcie_wait_for_dl(pcie);

	/* Enable INTx interrupts */
	rcar_rmw32(pcie, RCONF(PCI_INTERRUPT_LINE), 0xff, 0);
	rcar_rmw32(pcie, RCONF(PCI_INTERRUPT_PIN), 0xff, 1);
	rcar_rmw32(pcie, PCIEINTXR, 0, 0xF << 8);

	/* Enable slave Bus Mastering */
	rcar_rmw32(pcie, RCONF(PCI_STATUS), PCI_STATUS_DEVSEL_MASK,
		PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_FAST);

	wmb();
}

static int rcar_msi_alloc(struct rcar_msi *chip)
{
	int msi;

	mutex_lock(&chip->lock);

	msi = find_first_zero_bit(chip->used, INT_PCI_MSI_NR);
	if (msi < INT_PCI_MSI_NR)
		set_bit(msi, chip->used);
	else
		msi = -ENOSPC;

	mutex_unlock(&chip->lock);

	return msi;
}

static void rcar_msi_free(struct rcar_msi *chip, unsigned long irq)
{
	struct device *dev = chip->chip.dev;

	mutex_lock(&chip->lock);

	if (!test_bit(irq, chip->used))
		dev_err(dev, "trying to free unused MSI#%lu\n", irq);
	else
		clear_bit(irq, chip->used);

	mutex_unlock(&chip->lock);
}

static irqreturn_t rcar_pcie_msi_irq(int irq, void *data)
{
	struct rcar_pcie *pcie = data;
	struct rcar_msi *msi = &pcie->msi;
	unsigned long reg;

	reg = pci_read_reg(pcie, PCIEMSIFR);

	while (reg) {
		unsigned int index = find_first_bit(&reg, 32);
		unsigned int irq;

		/* clear the interrupt */
		pci_write_reg(pcie, 1 << index, PCIEMSIFR);

		irq = irq_find_mapping(msi->domain, index);
		if (irq) {
			if (test_bit(index, msi->used))
				generic_handle_irq(irq);
			else
				dev_info(pcie->dev, "unhandled MSI\n");
		} else {
			/*
			 * that's weird who triggered this?
			 * just clear it
			 */
			dev_info(pcie->dev, "unexpected MSI\n");
		}

		/* see if there's any more pending in this vector */
		reg = pci_read_reg(pcie, PCIEMSIFR);
	}

	return IRQ_HANDLED;
}

static int rcar_msi_setup_irq(struct msi_chip *chip, struct pci_dev *pdev,
			       struct msi_desc *desc)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct rcar_pcie *pcie = container_of(chip, struct rcar_pcie, msi.chip);
	struct msi_msg msg;
	unsigned int irq;
	int hwirq;

	hwirq = rcar_msi_alloc(msi);
	if (hwirq < 0)
		return hwirq;

	irq = irq_create_mapping(msi->domain, hwirq);
	if (!irq) {
		rcar_msi_free(msi, hwirq);
		return -EINVAL;
	}

	irq_set_msi_desc(irq, desc);

	msg.address_lo = pci_read_reg(pcie, PCIEMSIALR) & ~MSIFE;
	msg.address_hi = pci_read_reg(pcie, PCIEMSIAUR);
	msg.data = hwirq;

	write_msi_msg(irq, &msg);

	return 0;
}

static void rcar_msi_teardown_irq(struct msi_chip *chip, unsigned int irq)
{
	struct rcar_msi *msi = to_rcar_msi(chip);
	struct irq_data *d = irq_get_irq_data(irq);

	rcar_msi_free(msi, d->hwirq);
}

static struct irq_chip rcar_msi_irq_chip = {
	.name = "R-Car PCIe MSI",
	.irq_enable = unmask_msi_irq,
	.irq_disable = mask_msi_irq,
	.irq_mask = mask_msi_irq,
	.irq_unmask = unmask_msi_irq,
};

static int rcar_msi_map(struct irq_domain *domain, unsigned int irq,
			 irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &rcar_msi_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);
	set_irq_flags(irq, IRQF_VALID);

	return 0;
}

static const struct irq_domain_ops msi_domain_ops = {
	.map = rcar_msi_map,
};

static int rcar_pcie_enable_msi(struct rcar_pcie *pcie)
{
	struct platform_device *pdev = to_platform_device(pcie->dev);
	struct rcar_msi *msi = &pcie->msi;
	unsigned long base;
	int err;

	mutex_init(&msi->lock);

	msi->chip.dev = pcie->dev;
	msi->chip.setup_irq = rcar_msi_setup_irq;
	msi->chip.teardown_irq = rcar_msi_teardown_irq;

	msi->domain = irq_domain_add_linear(pcie->dev->of_node, INT_PCI_MSI_NR,
					    &msi_domain_ops, &msi->chip);
	if (!msi->domain) {
		dev_err(&pdev->dev, "failed to create IRQ domain\n");
		return -ENOMEM;
	}

	/* Two irqs are for MSI, but they are also used for non-MSI irqs */
	msi->irq = pcie->irq;

	err = devm_request_irq(&pdev->dev, msi->irq, rcar_pcie_msi_irq,
			       IRQF_SHARED, rcar_msi_irq_chip.name, pcie);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	err = devm_request_irq(&pdev->dev, msi->irq+1, rcar_pcie_msi_irq,
			       IRQF_SHARED, rcar_msi_irq_chip.name, pcie);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", err);
		goto err;
	}

	/* setup MSI data target */
	msi->pages = __get_free_pages(GFP_KERNEL, 0);
	base = virt_to_phys((void *)msi->pages);

	pci_write_reg(pcie, base | MSIFE, PCIEMSIALR);
	pci_write_reg(pcie, 0, PCIEMSIAUR);

	/* enable all MSI interrupts */
	pci_write_reg(pcie, 0xffffffff, PCIEMSIIER);

	return 0;

err:
	irq_domain_remove(msi->domain);
	return err;
}

static int rcar_pcie_disable_msi(struct rcar_pcie *pcie)
{
	struct rcar_msi *msi = &pcie->msi;
	unsigned int i, irq;

	/* disable all MSI interrupts */
	pci_write_reg(pcie, 0, PCIEMSIIER);

	free_pages(msi->pages, 0);

	for (i = 0; i < INT_PCI_MSI_NR; i++) {
		irq = irq_find_mapping(msi->domain, i);
		if (irq > 0)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(msi->domain);

	return 0;
}

static int rcar_pcie_clocks_get(struct rcar_pcie *pcie)
{
	int err = 0;

	pcie->clk = clk_get(pcie->dev, NULL);
	if (IS_ERR(pcie->clk))
		err = PTR_ERR(pcie->clk);
	else
		clk_enable(pcie->clk);

	return err;
}

static void rcar_pcie_clocks_put(struct rcar_pcie *pcie)
{
	clk_put(pcie->clk);
}

struct rcar_pcie_errs {
	int bit;
	const char *description;
};

static struct rcar_pcie_errs err_factors[] = {
	{ 28, "Transfer Error (internal bus)" },
	{ 27, "Malformed TLP" },
	{ 26, "Unsupported Request" },
	{ 25, "Poisoned TLP" },
	{ 21, "POVF" },
	{ 20, "NPOVF" },
	{ 13, "Received Completion Lock without data" },
	{ 12, "Unexpected Completion" },
	{ 9,  "Received Completion Size Error" },
	{ 8,  "CPL Timeout" },
	{ 6,  "Got Configuration Retry Status Completion" },
	{ 5,  "Got Completion Abort Status Completion" },
	{ 4,  "Got Unsupported Request Status Completion" },
	{ 1,  "Sent Completion Abort Status Completion" },
	{ 0,  "Sent Unsupported Request Status Completion" },
};

static struct rcar_pcie_errs err_factors2[] = {
	{ 29, "Data Link Layer Protocol Error" },
	{ 28, "Replay Timeout" },
	{ 27, "Replay Number Rollover" },
	{ 26, "Bad TLP" },
	{ 25, "Bad DLLP" },
	{ 15, "Receiver Error" },
};

static struct rcar_pcie_errs mac_status[] = {
	{ 30, "MAC Link Training in progress" },
	{ 7,  "MAC Speed Change Success" },
	{ 6,  "MAC Speed Change Fail" },
	{ 5,  "MAC Speed Changed" },
	{ 4,  "MAC Speed Change Finish" },
};

static void decode_bits_show_reason(struct rcar_pcie *pcie,
	unsigned long reg,
	struct rcar_pcie_errs *messages,
	int nr_messages)
{
	u32 status;
	int i;

	status = pci_read_reg(pcie, reg);
	pci_write_reg(pcie, status, reg);

	for (i = 0; i < nr_messages; i++) {
		if (status & (1 << messages[i].bit))
			dev_err(pcie->dev, "%s\n", messages[i].description);
	}
}

static void link_pm_isr(struct rcar_pcie *pcie)
{
	/* Link power management, L0 to L1 */
	u32 pmsr = pci_read_reg(pcie, PMSR);

	/* PM_ENTER_L1 DLLP received? */
	if (pmsr & 0x800000) {
		dev_info(pcie->dev, "PM_ENTER_L1 DLLP received\n");
		/* Wait until we are in L0 */
		while (((pci_read_reg(pcie, PMSR) >> 16) & 7) != 1)
			;
		/* Initiate transition to L1 */
		pci_write_reg(pcie, 1 << 31, PMCTLR);
	}
	if (pmsr & 0x80000000)
		dev_info(pcie->dev, "L1 activation complete or stopped\n");

	pci_write_reg(pcie, 0x80800000, PMSR);
}

static irqreturn_t rcar_pcie_isr(int irq, void *context)
{
	struct rcar_pcie *pcie = context;
	u32 reg;

	reg = pci_read_reg(pcie, PCIEINTR);
	if (reg) {
		pci_write_reg(pcie, reg, PCIEINTR);

		if (reg & (1 << 31))
			dev_info(pcie->dev, "PCI Express Transfer Error\n");
		if (reg & (1 << 30)) {
			/*
			 * Device power management, D0 to D3.
			 * This is handled by the PCIe sub-system.
			 */
			/* TODO can't get the PCIe sub-system to pick this up.
			   See pcie/pme.c pcie_pme_irq() */
			return IRQ_NONE;
		}
		/* bit 29, "Tx All Empty" not enabled */
		if (reg & (1 << 28)) {
			/* To clear, write 1 to LKAUTOBWSTS bit */
			rcar_rmw32(pcie, REXPCAP(PCI_EXP_LNKSTA), 0,
				PCI_EXP_LNKSTA_LABS);
			dev_info(pcie->dev, "PCI Express Bandwidth changed\n");
		}
		if (reg & (1 << 13))
			decode_bits_show_reason(pcie, MACSR, mac_status,
				ARRAY_SIZE(mac_status));
		if (reg & (1 << 12))
			link_pm_isr(pcie);
		/* bit 5, "Set slot power limit" message is Endpoint only */
		if (reg & (1 << 4))
			dev_info(pcie->dev, "PCI Power-down (non-D0) irq\n");
		if (reg & (1 << 3)) {
			rcar_rmw32(pcie, REXPCAP(PCI_EXP_DEVSTA), 0,
				PCI_EXP_DEVSTA_CED);
			dev_info(pcie->dev, "PCI Correctable Error\n");
		}
		if (reg & (1 << 2))
			dev_info(pcie->dev, "PCI Non-Fatal Error\n");
		if (reg & (1 << 1))
			dev_err(pcie->dev, "PCI Fatal Error\n");

		if (reg & (1 << 0)) {
			/* System error */
			decode_bits_show_reason(pcie, PCIEERRFR, err_factors,
				ARRAY_SIZE(err_factors));
			decode_bits_show_reason(pcie, PCIEERRFR2, err_factors2,
				ARRAY_SIZE(err_factors2));

			pcibios_report_status(PCI_STATUS_SIG_SYSTEM_ERROR, 1);
		}

		return IRQ_HANDLED;
	}

	/* Transfer status changed? */
	reg = pci_read_reg(pcie, PCIETSTR);
	if (reg) {
		if (reg & (1 << 28))
			dev_info(pcie->dev, "MSI Enable Changed irq\n");
		if (reg & (1 << 16)) {
			if (reg & 1)
				dev_info(pcie->dev, "DLL active irq\n");
			else
				dev_info(pcie->dev, "DLL inactive irq\n");
		}
		if (reg & (1 << 12))
			dev_info(pcie->dev, "INTx Disable Changed irq\n");
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int __init rcar_pcie_get_resources(struct platform_device *pdev,
	struct rcar_pcie *pcie)
{
	struct resource *res;
	int i;
	int err;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	i = platform_get_irq(pdev, 0);
	if (!res || i < 0) {
		dev_err(pcie->dev, "cannot get platform resources\n");
		return -ENOENT;
	}
	pcie->irq = i;

	/* Handle error/info irqs locally */
	err = devm_request_irq(&pdev->dev, pcie->irq+2, rcar_pcie_isr,
			       0, "PCIE", pcie);
	if (err) {
		dev_err(&pdev->dev, "failed to register IRQ: %d\n", err);
		return err;
	}

	err = rcar_pcie_clocks_get(pcie);
	if (err) {
		dev_err(&pdev->dev, "failed to get clocks: %d\n", err);
		return err;
	}

	pcie->base = devm_request_and_ioremap(&pdev->dev, res);
	if (!pcie->base) {
		dev_err(&pdev->dev, "failed to map PCIe registers: %d\n", err);
		err = -ENOMEM;
		goto err_map_reg;
	}

	return 0;

err_map_reg:
	rcar_pcie_clocks_put(pcie);

	return err;
}

static struct platform_device_id rcar_pcie_id_table[] = {
	{ "rcar-pcie",    RCAR_GENERIC },
	{ "r8a7779-pcie", RCAR_H1 },
	{},
};
MODULE_DEVICE_TABLE(platform, rcar_pcie_id_table);

static int __init rcar_pcie_probe(struct platform_device *pdev)
{
	struct rcar_pcie *pcie;
	struct resource *res;
	unsigned int data;
	int i, err;

	pcie = devm_kzalloc(&pdev->dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pcie->dev = &pdev->dev;
	pcie->chip = pdev->id_entry->driver_data;

	/* Get resources */
	err = rcar_pcie_get_resources(pdev, pcie);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to request resources: %d\n", err);
		return err;
	}

	/* Get the I/O and memory ranges */
	for (i = 0; i < NR_PCI_RESOURCES; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i+1);
		if (!res) {
			dev_err(&pdev->dev, "cannot get MEM%d resources\n", i);
			return -ENOENT;
		}
		pcie->res[i] = res;
	}

	if (IS_ENABLED(CONFIG_PCI_MSI)) {
		err = rcar_pcie_enable_msi(pcie);
		if (err < 0) {
			dev_err(&pdev->dev,
				"failed to enable MSI support: %d\n",
				err);
			return err;
		}
	}

	rcar_pcie_hw_init(pcie);

	if (!pcie->haslink) {
		dev_info(&pdev->dev, "PCI: PCIe link down\n");
		return 0;
	}

	data = pci_read_reg(pcie, MACSR);
	dev_info(&pdev->dev, "PCIe x%d: link up\n", (data >> 20) & 0x3f);

	rcar_pcie_enable(pcie);

	platform_set_drvdata(pdev, pcie);
	return 0;
}

static struct platform_driver rcar_pcie_driver = {
	.probe		= rcar_pcie_probe,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.id_table	= rcar_pcie_id_table,
};

module_platform_driver(rcar_pcie_driver);

MODULE_AUTHOR("Phil Edworthy <phil.edworthy@renesas.com>");
MODULE_DESCRIPTION("Renesas R-Car PCIe driver");
MODULE_LICENSE("GPLv2");

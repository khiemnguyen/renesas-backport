/*
 * arch/arm/mach-shmobile/smp-r8a7790.c
 *     SMP support for R-Mobile / SH-Mobile - r8a7790 portion
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/smp.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/smp_plat.h>
#include <mach/common.h>
#include <mach/hardware.h>

void gic_secondary_init(unsigned int);

unsigned int r8a7790_get_core_count(void)
{
	return CONFIG_NR_CPUS;
}

#define IO_BASE		0xe6150000
#define CA15BAR		0x016020
#define CA15RESCNT	0x010040
#define RESCNT		0x010050
#define CA15WUPCR	0x004010
#define SYSCSR		0x030000
#define MERAM		0xe8080000
#define CCI_BASE	0xf0190000
#define CCI_SLAVE3	0x4000
#define CCI_SLAVE4	0x5000
#define CCI_SNOOP	0x0000
#define CCI_STATUS	0x000c

void __init r8a7790_smp_prepare_cpus(unsigned int max_cpus)
{
	int i;
	u32 bar;
	void __iomem *p;

	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	/* MERAM for jump stub, because BAR requires 256KB aligned address */
	p = ioremap_nocache(MERAM, 16);
	memcpy(p, r8a7790_secondary_cpu_entry, 16);
	iounmap(p);

	p = ioremap_nocache(IO_BASE, 0x40000);
	bar = (MERAM >> 8) & 0xfffffc00;
	__raw_writel(bar, (int)p + CA15BAR);
	__raw_writel(bar | 0x10, (int)p + CA15BAR);
	iounmap(p);
}

void __cpuinit r8a7790_secondary_init(unsigned int cpu)
{
	gic_secondary_init(0);
}

int __cpuinit r8a7790_boot_secondary(unsigned int cpu)
{
	u32 val;
	void __iomem *p;

	cpu = cpu_logical_map(cpu);

	p = ioremap_nocache(IO_BASE, 0x40000);

	__raw_writel(1 << (cpu & 3), (int)p + CA15WUPCR);

	while ((__raw_readl((int)p + SYSCSR) & 0x3) != 0x3)
		;
	while (__raw_readl((int)p + CA15WUPCR) != 0x0)
		;

	val = __raw_readl((int)p + CA15RESCNT);
	val |= 0xa5a50000;
	switch (cpu & 3) {
	case 1:
		__raw_writel(val & ~0x4, (int)p + CA15RESCNT);
		break;
	case 2:
		__raw_writel(val & ~0x2, (int)p + CA15RESCNT);
		break;
	case 3:
		__raw_writel(val & ~0x1, (int)p + CA15RESCNT);
		break;
	}

	iounmap(p);
	return 0;
}

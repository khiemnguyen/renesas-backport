/*
 * SMP support for SoCs with RESET(RST)
 *
 * Copyright (C) 2013  Magnus Damm
 * Copyright (C) 2013  Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/io.h>
#include <asm/smp_plat.h>

#define RST		0xe6160000
#define CA15BAR		0x0020
#define CA7BAR		0x0030
#define CA15RESCNT	0x0040
#define CA7RESCNT	0x0044

enum { R8A779x_CLST_CA15, R8A779x_CLST_CA7, R8A779x_CLST_NR };

static struct {
	unsigned int rescnt;
	unsigned int rescnt_magic;
} r8a779x_clst[R8A779x_CLST_NR] = {
	[R8A779x_CLST_CA15] = {
		.rescnt = CA15RESCNT,
		.rescnt_magic = 0xa5a50000,
	},
	[R8A779x_CLST_CA7] = {
		.rescnt = CA7RESCNT,
		.rescnt_magic = 0x5a5a0000,
	},
};

#define r8a779x_clst_id(cpu) (cpu_logical_map((cpu)) >> 8)
#define r8a779x_cpu_id(cpu) (cpu_logical_map((cpu)) & 0xff)

void __iomem *r8a779x_rst_base;

void r8a779x_deassert_reset(unsigned int cpu)
{
	void __iomem *p, *rescnt;
	u32 mask, magic;
	unsigned int clst_id = r8a779x_clst_id(cpu);

	p = r8a779x_rst_base;
	mask = BIT(3 - r8a779x_cpu_id(cpu));
	magic = r8a779x_clst[clst_id].rescnt_magic;
	rescnt = p + r8a779x_clst[clst_id].rescnt;
	writel_relaxed((readl_relaxed(rescnt) & ~mask) | magic, rescnt);
}

void r8a779x_assert_reset(unsigned int cpu)
{
	void __iomem *p, *rescnt;
	u32 mask, magic;
	unsigned int clst_id = r8a779x_clst_id(cpu);

	p = r8a779x_rst_base;
	mask = BIT(3 - r8a779x_cpu_id(cpu));
	magic = r8a779x_clst[clst_id].rescnt_magic;
	rescnt = p + r8a779x_clst[clst_id].rescnt;
	writel_relaxed((readl_relaxed(rescnt) | mask) | magic, rescnt);
}

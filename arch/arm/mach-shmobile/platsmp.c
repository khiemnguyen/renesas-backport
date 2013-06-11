/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2011  Paul Mundt
 *
 * Based on vexpress, Copyright (C) 2002 ARM Ltd, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <asm/mach-types.h>
#include <mach/common.h>
#include <mach/emev2.h>

#ifdef CONFIG_ARCH_R8A7790
#define is_r8a7790() of_machine_is_compatible("renesas,r8a7790")
#else
#define is_r8a7790() (0)
#endif

static unsigned int __init shmobile_smp_get_core_count(void)
{
	if (is_r8a7790())
		return r8a7790_get_core_count();

	return 1;
}

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	trace_hardirqs_off();

	if (is_r8a7790())
		r8a7790_secondary_init(cpu);
}

int __cpuinit boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	if (is_r8a7790())
		return r8a7790_boot_secondary(cpu);

	return -ENOSYS;
}

void __init shmobile_smp_init_cpus(unsigned int ncores)
{
	unsigned int i;

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

void __init smp_init_cpus(void)
{
	unsigned int ncores = shmobile_smp_get_core_count();
	unsigned int i;

	if (ncores > nr_cpu_ids) {
		pr_warn("SMP: %u cores greater than maximum (%u), clipping\n",
			ncores, nr_cpu_ids);
		ncores = nr_cpu_ids;
	}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
}

void __init platform_smp_prepare_cpus(unsigned int max_cpus)
{
	if (is_r8a7790())
		r8a7790_smp_prepare_cpus(max_cpus);
}

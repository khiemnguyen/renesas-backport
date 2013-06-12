#ifndef __ASM_R8A7790_H__
#define __ASM_R8A7790_H__

#include <asm/mach/time.h>
#include <linux/pm_domain.h>

struct platform_device;

void r8a7790_add_standard_devices(void);
void r8a7790_clock_init(void);
void r8a7790_pinmux_init(void);

extern struct sys_timer r8a7790_timer;
extern struct smp_operations r8a7790_smp_ops;

struct r8a7790_pm_ch {
	unsigned long chan_offs;
	unsigned int chan_bit;
	unsigned int isr_bit;
};

struct r8a7790_pm_domain {
	struct generic_pm_domain genpd;
	struct r8a7790_pm_ch ch;
};

static inline struct r8a7790_pm_ch *to_r8a7790_ch(struct generic_pm_domain *d)
{
	return &container_of(d, struct r8a7790_pm_domain, genpd)->ch;
}

extern int r8a7790_sysc_power_down(struct r8a7790_pm_ch *r8a7790_ch);
extern int r8a7790_sysc_power_up(struct r8a7790_pm_ch *r8a7790_ch);

#ifdef CONFIG_PM
extern struct r8a7790_pm_domain r8a7790_rgx;

extern void r8a7790_init_pm_domain(struct r8a7790_pm_domain *r8a7790_pd);
extern void r8a7790_add_device_to_domain(struct r8a7790_pm_domain *r8a7790_pd,
					 struct platform_device *pdev);
#else
#define r8a7790_init_pm_domain(pd) do { } while (0)
#define r8a7790_add_device_to_domain(pd, pdev) do { } while (0)
#endif /* CONFIG_PM */

#endif /* __ASM_R8A7790_H__ */

#ifndef __ARCH_MACH_COMMON_H
#define __ARCH_MACH_COMMON_H

extern void shmobile_earlytimer_init(void);
void __init shmobile_timer_init(void);
extern struct sys_timer shmobile_timer;
extern void shmobile_setup_delay(unsigned int max_cpu_core_mhz,
			 unsigned int mult, unsigned int div);
struct twd_local_timer;
extern void shmobile_setup_console(void);
extern void shmobile_boot_vector(void);
extern void __iomem *shmobile_boot_p;
extern unsigned long shmobile_boot_fn;
extern unsigned long shmobile_boot_arg;
extern void shmobile_secondary_vector_scu(void);
extern unsigned long shmobile_boot_size;
extern void shmobile_smp_boot(void);
extern void shmobile_smp_sleep(void);
extern void shmobile_boot_hook(unsigned long fn, unsigned long arg);
extern void shmobile_smp_hook(unsigned int cpu, unsigned long fn,
			      unsigned long arg);
extern int shmobile_smp_cpu_disable(unsigned int cpu);
extern void shmobile_invalidate_start(void);
extern void shmobile_boot_scu(void);
extern void shmobile_smp_scu_prepare_cpus(unsigned int max_cpus);
extern int shmobile_smp_scu_boot_secondary(unsigned int cpu,
					   struct task_struct *idle);
extern void shmobile_smp_scu_cpu_die(unsigned int cpu);
extern int shmobile_smp_scu_cpu_kill(unsigned int cpu);
extern void shmobile_smp_apmu_prepare_cpus(unsigned int max_cpus);
extern int shmobile_smp_apmu_boot_secondary(unsigned int cpu,
					    struct task_struct *idle);
extern void shmobile_smp_apmu_cpu_die(unsigned int cpu);
extern int shmobile_smp_apmu_cpu_kill(unsigned int cpu);
extern void shmobile_invalidate_start(void);
struct clk;
extern int shmobile_clk_init(void);
extern void shmobile_handle_irq_intc(struct pt_regs *);
extern struct platform_suspend_ops shmobile_suspend_ops;
struct cpuidle_driver;
extern void (*shmobile_cpuidle_modes[])(void);
extern void (*shmobile_cpuidle_setup)(struct cpuidle_driver *drv);

extern void sh7372_init_irq(void);
extern void sh7372_map_io(void);
extern void sh7372_add_early_devices(void);
extern void sh7372_add_standard_devices(void);
extern void sh7372_clock_init(void);
extern void sh7372_pinmux_init(void);
extern void sh7372_pm_init(void);
extern void sh7372_resume_core_standby_sysc(void);
extern int sh7372_do_idle_sysc(unsigned long sleep_mode);
extern struct clk sh7372_extal1_clk;
extern struct clk sh7372_extal2_clk;

extern void sh73a0_init_irq(void);
extern void sh73a0_map_io(void);
extern void sh73a0_add_early_devices(void);
extern void sh73a0_add_standard_devices(void);
extern void sh73a0_clock_init(void);
extern void sh73a0_pinmux_init(void);
extern struct clk sh73a0_extal1_clk;
extern struct clk sh73a0_extal2_clk;
extern struct clk sh73a0_extcki_clk;
extern struct clk sh73a0_extalr_clk;

extern void r8a7740_init_irq(void);
extern void r8a7740_map_io(void);
extern void r8a7740_add_early_devices(void);
extern void r8a7740_add_standard_devices(void);
extern void r8a7740_clock_init(u8 md_ck);
extern void r8a7740_reserve_memory(void);
extern void r8a7740_pinmux_init(void);

extern void r8a7779_init_irq(void);
extern void r8a7779_init_irq_extpin(int irlm);
extern void r8a7779_init_irq_dt(void);
extern void r8a7779_map_io(void);
extern void r8a7779_add_early_devices(void);
extern void r8a7779_add_standard_devices(void);
extern void r8a7779_clock_init(void);
extern void r8a7779_pinmux_init(void);
extern void r8a7779_pm_init(void);
extern void r8a7740_meram_workaround(void);

extern void r8a7779_register_twd(void);

extern void r8a779x_deassert_reset(unsigned int cpu);
extern void r8a779x_assert_reset(unsigned int cpu);
extern void __iomem *r8a779x_rst_base;

#ifdef CONFIG_SUSPEND
int shmobile_suspend_init(void);
#else
static inline int shmobile_suspend_init(void) { return 0; }
#endif

#ifdef CONFIG_CPU_IDLE
int shmobile_cpuidle_init(void);
#else
static inline int shmobile_cpuidle_init(void) { return 0; }
#endif

extern void shmobile_cpu_die(unsigned int cpu);
extern int shmobile_cpu_disable(unsigned int cpu);

#ifdef CONFIG_HOTPLUG_CPU
extern int shmobile_cpu_is_dead(unsigned int cpu);
#else
static inline int shmobile_cpu_is_dead(unsigned int cpu) { return 1; }
#endif

extern void __iomem *shmobile_scu_base;
extern void shmobile_smp_init_cpus(unsigned int ncores);

static inline void shmobile_init_late(void)
{
	shmobile_suspend_init();
	shmobile_cpuidle_init();
}

#endif /* __ARCH_MACH_COMMON_H */

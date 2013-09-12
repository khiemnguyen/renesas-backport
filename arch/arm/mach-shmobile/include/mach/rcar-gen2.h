#ifndef __ASM_RCAR_GEN2_H__
#define __ASM_RCAR_GEN2_H__

#include <asm/mach/time.h>

void rcar_gen2_timer_init(void);
#define MD(nr) BIT(nr)
u32 rcar_gen2_read_mode_pins(void);

extern struct sys_timer rcar_gen2_timer;

#endif /* __ASM_RCAR_GEN2_H__ */

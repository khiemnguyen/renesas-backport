/*
 * Koelsch board support
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
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

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/r8a7791.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* spidev for MSIOF */
static struct spi_board_info spi_bus[] __initdata = {
        {
                .modalias       = "spidev",
                .max_speed_hz   = 6000000,
                .mode           = SPI_MODE_3,
                .bus_num        = 1,
                .chip_select    = 0,
        },
};

static const struct pinctrl_map koelsch_pinctrl_map[] = {
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7791",
				  "scif0_data_d", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.1", "pfc-r8a7791",
				  "scif1_data_d", "scif1"),

	/* MSIOF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_clk", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_ctrl", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_data", "msiof0"),
};

static void __init koelsch_add_standard_devices(void)
{
	r8a7791_clock_init();

	pinctrl_register_mappings(koelsch_pinctrl_map,
				  ARRAY_SIZE(koelsch_pinctrl_map));
	r8a7791_pinmux_init();

	r8a7791_add_standard_devices();

	/* spidev for MSIOF */
	spi_register_board_info(spi_bus, ARRAY_SIZE(spi_bus));
}

static const char *koelsch_boards_compat_dt[] __initdata = {
	"renesas,koelsch",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_irq	= r8a7790_init_irq,
	.timer		= &r8a7791_timer,
	.init_machine	= koelsch_add_standard_devices,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END

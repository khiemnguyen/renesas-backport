/*
 * Lager board support
 *
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
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_data/vsp1.h>
#include <linux/platform_device.h>
#include <linux/sh_eth.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7790.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* DU */
static struct rcar_du_encoder_data lager_du_encoders[] = {
	{
		.type = RCAR_DU_ENCODER_VGA,
		.output = RCAR_DU_OUTPUT_DPAD0,
	}, {
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS1,
		.connector.lvds.panel = {
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

static struct rcar_du_platform_data lager_du_pdata = {
	.encoders = lager_du_encoders,
	.num_encoders = ARRAY_SIZE(lager_du_encoders),
};

/* LEDS */
static struct gpio_led lager_leds[] = {
	{
		.name		= "led8",
		.gpio		= RCAR_GP_PIN(5, 17),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led7",
		.gpio		= RCAR_GP_PIN(4, 23),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led6",
		.gpio		= RCAR_GP_PIN(4, 22),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static __initdata struct gpio_led_platform_data lager_leds_pdata = {
	.leds		= lager_leds,
	.num_leds	= ARRAY_SIZE(lager_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1 }

static __initdata struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4,		RCAR_GP_PIN(1, 28),	"SW2-pin4"),
	GPIO_KEY(KEY_3,		RCAR_GP_PIN(1, 26),	"SW2-pin3"),
	GPIO_KEY(KEY_2,		RCAR_GP_PIN(1, 24),	"SW2-pin2"),
	GPIO_KEY(KEY_1,		RCAR_GP_PIN(1, 14),	"SW2-pin1"),
};

static __initdata struct gpio_keys_platform_data lager_keys_pdata = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* VSP1 */
static struct vsp1_platform_data lager_vspr_pdata = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 1,
	.wpf_count = 4,
};

static struct vsp1_platform_data lager_vsps_pdata = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 3,
	.wpf_count = 4,
};

static struct vsp1_platform_data lager_vspd0_pdata = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

static struct vsp1_platform_data lager_vspd1_pdata = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

/* Ether */
static struct sh_eth_plat_data ether_pdata __initdata = {
	.phy			= 0x1,
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_FAST_RCAR,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low	= 1,
};

static struct resource ether_resources[] __initdata = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)), /* IRQ0 */
};

/* MMC */
static void shmmcif_set_pwr(struct platform_device *pdev, int state)
{
}

static void shmmcif_down_pwr(struct platform_device *pdev)
{
}

static int shmmcif_get_cd(struct platform_device *pdev)
{
	return 1;
}

static struct sh_mmcif_plat_data sh_mmcif0_plat = {
	.set_pwr	= shmmcif_set_pwr,
	.down_pwr	= shmmcif_down_pwr,
	.get_cd		= shmmcif_get_cd,
	.slave_id_tx	= SHDMA_SLAVE_MMC0_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMC0_RX,
	.use_cd_gpio	= 0,
	.cd_gpio	= 0,
	.sup_pclk	= 0 ,
	.caps		= MMC_CAP_MMC_HIGHSPEED |
			  MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE ,
	.ocr		= MMC_VDD_32_33 | MMC_VDD_33_34 ,
};

static struct sh_mmcif_plat_data sh_mmcif1_plat = {
	.set_pwr	= shmmcif_set_pwr,
	.down_pwr	= shmmcif_down_pwr,
	.get_cd		= shmmcif_get_cd,
	.slave_id_tx	= SHDMA_SLAVE_MMC1_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMC1_RX,
	.use_cd_gpio	= 0,
	.cd_gpio	= 0,
	.sup_pclk	= 0 ,
	.caps		= MMC_CAP_MMC_HIGHSPEED |
			  MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE ,
	.ocr		= MMC_VDD_32_33 | MMC_VDD_33_34 ,
};

/* MSIOF spidev */
static const struct spi_board_info spi_bus[] __initconst = {
	{
		.modalias	= "spidev",
		.max_speed_hz	= 6000000,
		.mode		= SPI_MODE_3,
		.bus_num	= 2,
		.chip_select	= 0,
	},
};

#define lager_add_msiof_device spi_register_board_info

/* QSPI flash memory */
static struct mtd_partition spiflash_part[] = {
	/* Reserved for user loader program, read-only */
	[0] = {
		.name = "loader_prg",
		.offset = 0,
		.size = SZ_256K,
		.mask_flags = MTD_WRITEABLE,	/* read only */
	},
	/* Reserved for user program, read-only */
	[1] = {
		.name = "user_prg",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_4M,
		.mask_flags = MTD_WRITEABLE,	/* read only */
	},
	/* All else is writable (e.g. JFFS2) */
	[2] = {
		.name = "flash_fs",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = 0,
	},
};

static struct flash_platform_data spiflash_data = {
	.name		= "m25p80",
	.parts		= spiflash_part,
	.nr_parts	= ARRAY_SIZE(spiflash_part),
	.type		= "s25fl512s",
};

static const struct spi_board_info spi_info[] __initconst = {
	{
		.modalias	= "m25p80",
		.platform_data	= &spiflash_data,
		.mode		= SPI_MODE_0,
		.max_speed_hz	= 30000000,
		.bus_num	= 0,
		.chip_select	= 0,
	},
};

#define lager_add_qspi_device spi_register_board_info

static const struct pinctrl_map lager_pinctrl_map[] = {
	/* DU (CN10: ARGB0, CN13: LVDS) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_rgb666", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_sync_1", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7790", "pfc-r8a7790",
				  "du_clk_out_0", "du"),
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7790",
				  "scif0_data", "scif0"),
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7790",
				  "scif1_data", "scif1"),
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7790",
				  "eth_link", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7790",
				  "eth_mdio", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7790",
				  "eth_rmii", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7790",
				  "intc_irq0", "intc"),
	/* MMC1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_data8", "mmc1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.1", "pfc-r8a7790",
				  "mmc1_ctrl", "mmc1"),
	/* MSIOF1 */
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_clk", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_sync", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_ss1", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_ss2", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_rx", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7790",
				  "msiof1_tx", "msiof1"),
	/* USB0 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7790",
				  "usb0_pwen", "usb0"),
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7790",
				  "usb0_ovc_vbus", "usb0"),
	/* USB1 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.1", "pfc-r8a7790",
				  "usb1_pwen", "usb1"),
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.1", "pfc-r8a7790",
				  "usb1_ovc", "usb1"),
	/* USB2 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.2", "pfc-r8a7790",
				  "usb2_pwen", "usb2"),
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.2", "pfc-r8a7790",
				  "usb2_ovc", "usb2"),
};

static void __init lager_add_standard_devices(void)
{
	r8a7790_clock_init();

	pinctrl_register_mappings(lager_pinctrl_map,
				  ARRAY_SIZE(lager_pinctrl_map));
	r8a7790_pinmux_init();

	r8a7790_add_standard_devices();
	r8a7790_add_du_device(&lager_du_pdata);

	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &lager_leds_pdata,
				      sizeof(lager_leds_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &lager_keys_pdata,
				      sizeof(lager_keys_pdata));

	r8a7790_add_vsp1_device(&lager_vspd0_pdata, 2);
	platform_device_register_resndata(&platform_bus, "r8a779x-ether", -1,
					  ether_resources,
					  ARRAY_SIZE(ether_resources),
					  &ether_pdata, sizeof(ether_pdata));

	r8a7790_add_mmc_device(&sh_mmcif0_plat, 0);
	r8a7790_add_mmc_device(&sh_mmcif1_plat, 1);
	lager_add_msiof_device(spi_bus, ARRAY_SIZE(spi_bus));
	lager_add_qspi_device(spi_info, ARRAY_SIZE(spi_info));
}

static const char *lager_boards_compat_dt[] __initdata = {
	"renesas,lager",
	NULL,
};

DT_MACHINE_START(LAGER_DT, "lager")
	.smp		= smp_ops(r8a7790_smp_ops),
	.init_early	= r8a7790_init_early,
	.timer		= &r8a7790_timer,
	.init_machine	= lager_add_standard_devices,
	.dt_compat	= lager_boards_compat_dt,
MACHINE_END

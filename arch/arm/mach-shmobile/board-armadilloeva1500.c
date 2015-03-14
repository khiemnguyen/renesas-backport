/*
 * Armadillo-EVA 1500 board support
 *
 * Copyright (C) 2014 Atmark Techno, Inc.
 *
 * Based on board-koelsch.c
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c-gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/rcar-du.h>
#include <linux/platform_data/vsp1.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/rtc/rtc-s35390a.h>
#include <linux/sh_eth.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <media/vin.h>
#include <sound/sh_scu.h>
#include <sound/cs42l52.h>
#include <asm/system_info.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* DU */
static struct rcar_du_encoder_data armadilloeva1500_du_encoders[] = {
#if defined(CONFIG_DRM_ADV7511) || defined(CONFIG_DRM_ADV7511_MODULE)
	{
		.type = RCAR_DU_ENCODER_HDMI,
		.output = RCAR_DU_OUTPUT_DPAD0,
	},
#endif
	{
		.type = RCAR_DU_ENCODER_NONE,
		.output = RCAR_DU_OUTPUT_LVDS0,
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

static struct rcar_du_crtc_data armadilloeva1500_du_crtcs[] = {
	{
		.exclk = 148500000,
	},
	{
		.exclk = 74250000,
	},
};

static const struct rcar_du_platform_data armadilloeva1500_du_pdata __initconst = {
	.encoders = armadilloeva1500_du_encoders,
	.num_encoders = ARRAY_SIZE(armadilloeva1500_du_encoders),
	.crtcs = armadilloeva1500_du_crtcs,
	.num_crtcs = ARRAY_SIZE(armadilloeva1500_du_crtcs),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x40000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
};

static void __init armadilloeva1500_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7791",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &armadilloeva1500_du_pdata,
		.size_data = sizeof(armadilloeva1500_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* GPIO KEY */
#define GPIO_KEY(c, g, d, w, ...) \
	{ .code = c, .gpio = g, .desc = d, .wakeup = w, .active_low = 1, \
 	  .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_3, RCAR_GP_PIN(5, 0), "SW3", 1),
	GPIO_KEY(KEY_4, RCAR_GP_PIN(5, 1), "SW4", 1),
	GPIO_KEY(KEY_5, RCAR_GP_PIN(5, 2), "SW5", 1),
	GPIO_KEY(KEY_6, RCAR_GP_PIN(5, 3), "SW6", 1),
};

static const struct gpio_keys_platform_data armadilloeva1500_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* GPIO LED */
#define GPIO_LED(n, d, g, a) \
	{ .name = n, .default_trigger = d, \
	  .gpio  = g, .active_low = a, }

static struct gpio_led gpio_leds[] = {
	GPIO_LED("LED4", "none", RCAR_GP_PIN(5, 4), 0),
	GPIO_LED("LED5", "none", RCAR_GP_PIN(5, 5), 0),
	GPIO_LED("LED6", "none", RCAR_GP_PIN(5, 6), 0),
	GPIO_LED("LED7", "none", RCAR_GP_PIN(5, 7), 0),
};

static const struct gpio_led_platform_data armadilloeva1500_leds_pdata __initconst = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

/* I2C GPIO */
static struct i2c_gpio_platform_data __maybe_unused armadilloeva1500_i2c_gpio0_pdata = {
	.scl_pin	= RCAR_GP_PIN(7, 13),
	.sda_pin	= RCAR_GP_PIN(7, 14),
	.udelay		= 6,
};

static struct i2c_gpio_platform_data __maybe_unused armadilloeva1500_i2c_gpio1_pdata = {
	.scl_pin	= RCAR_GP_PIN(7, 15),
	.sda_pin	= RCAR_GP_PIN(7, 16),
	.udelay		= 6,
};

static struct i2c_gpio_platform_data __maybe_unused armadilloeva1500_i2c_gpio2_pdata = {
	.scl_pin	= RCAR_GP_PIN(2, 6),
	.sda_pin	= RCAR_GP_PIN(2, 7),
	.udelay		= 6,
};

/* RTC */
static struct i2c_board_info rtc_i2c[] = {
	{ I2C_BOARD_INFO("s35390a", 0x30),
	  .irq = irq_pin(2),
	  .flags = RTC_S35390A_FLAG_INT1_EN },
};

#define armadilloeva1500_add_rtc_device i2c_register_board_info

/* Touch Screen */
static struct i2c_board_info ts_i2c[] = {
	{ I2C_BOARD_INFO("st1232-ts", 0x55),
	  .irq = irq_pin(9), },
};

#define armadilloeva1500_add_ts_device i2c_register_board_info

/* PWM Backlight */
static struct platform_pwm_backlight_data __maybe_unused armadilloeva1500_pwm_backlight_pdata = {
	.pwm_id = 4,
	.max_brightness = 255,
	.dft_brightness = 0,
	.pwm_period_ns = 33333, /* 30kHz */
};

/* Ether */
static void ether_init(void)
{
	gpio_request(RCAR_GP_PIN(5, 22), "ETH_RESET_N");

	/* reset phy */
	gpio_direction_output(RCAR_GP_PIN(5, 22), 0);
	mdelay(10);
	gpio_direction_output(RCAR_GP_PIN(5, 22), 1);
	mdelay(1);
}

static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x0,
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_FAST_RCAR,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.no_ether_link		= 1,
	.init			= ether_init,
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)), /* IRQ0 */
};

/* Audio */
static struct scu_config ssi_ch_value[] = {
	{RP_MEM_SSI3,		SSI3},
	{RP_MEM_SRC0_SSI3,	SSI3},
	{RP_MEM_SRC0_DVC0_SSI3,	SSI3},
	{RC_SSI4_MEM,		SSI4},
	{RC_SSI4_SRC1_MEM,	SSI4},
	{RC_SSI4_SRC1_DVC1_MEM,	SSI4},
};

static struct scu_config src_ch_value[] = {
	{RP_MEM_SSI3,		-1},
	{RP_MEM_SRC0_SSI3,	SRC0},
	{RP_MEM_SRC0_DVC0_SSI3,	SRC0},
	{RC_SSI4_MEM,		-1},
	{RC_SSI4_SRC1_MEM,	SRC1},
	{RC_SSI4_SRC1_DVC1_MEM,	SRC1},
};

static struct scu_config dvc_ch_value[] = {
	{RP_MEM_SSI3,		-1},
	{RP_MEM_SRC0_SSI3,	-1},
	{RP_MEM_SRC0_DVC0_SSI3,	DVC0},
	{RC_SSI4_MEM,		-1},
	{RC_SSI4_SRC1_MEM,	-1},
	{RC_SSI4_SRC1_DVC1_MEM,	DVC1},
};

static struct scu_config audma_slave_value[] = {
	{RP_MEM_SSI3,		SHDMA_SLAVE_PCM_MEM_SSI3},
	{RP_MEM_SRC0_SSI3,	SHDMA_SLAVE_PCM_MEM_SRC0},
	{RP_MEM_SRC0_DVC0_SSI3,	SHDMA_SLAVE_PCM_MEM_SRC0},
	{RC_SSI4_MEM,		SHDMA_SLAVE_PCM_SSI4_MEM},
	{RC_SSI4_SRC1_MEM,	SHDMA_SLAVE_PCM_SRC1_MEM},
	{RC_SSI4_SRC1_DVC1_MEM,	SHDMA_SLAVE_PCM_CMD1_MEM},
};

static struct scu_config audmapp_slave_value[] = {
	{RP_MEM_SSI3,		-1},
	{RP_MEM_SRC0_SSI3,	SHDMA_SLAVE_PCM_SRC0_SSI3},
	{RP_MEM_SRC0_DVC0_SSI3,	SHDMA_SLAVE_PCM_CMD0_SSI3},
	{RC_SSI4_MEM,		-1},
	{RC_SSI4_SRC1_MEM,	SHDMA_SLAVE_PCM_SSI4_SRC1},
	{RC_SSI4_SRC1_DVC1_MEM,	SHDMA_SLAVE_PCM_SSI4_SRC1},
};

static struct scu_config ssiu_busif_adinr_offset[] = {
	{SSI0, SSI0_0_BUSIF_ADINR},
	{SSI1, SSI1_0_BUSIF_ADINR},
	{SSI2, SSI2_0_BUSIF_ADINR},
	{SSI3, SSI3_BUSIF_ADINR},
	{SSI4, SSI4_BUSIF_ADINR},
	{SSI5, SSI5_BUSIF_ADINR},
	{SSI6, SSI6_BUSIF_ADINR},
	{SSI7, SSI7_BUSIF_ADINR},
	{SSI8, SSI8_BUSIF_ADINR},
	{SSI9, SSI9_0_BUSIF_ADINR},
};

static struct scu_config ssiu_control_offset[] = {
	{SSI0, SSI0_0_CONTROL},
	{SSI1, SSI1_0_CONTROL},
	{SSI2, SSI2_0_CONTROL},
	{SSI3, SSI3_CONTROL},
	{SSI4, SSI4_CONTROL},
	{SSI5, SSI5_CONTROL},
	{SSI6, SSI6_CONTROL},
	{SSI7, SSI7_CONTROL},
	{SSI8, SSI8_CONTROL},
	{SSI9, SSI9_0_CONTROL},
};

static struct scu_config ssiu_mode1_value[] = {
	{SSI4, SSI_MODE1_SSI4_MASTER},
	{SSI1, SSI_MODE1_SSI1_IND},
	{SSI2, SSI_MODE1_SSI2_IND},
};

static struct scu_config dvc_route_select_value[] = {
	{DVC0, (CMD_ROUTE_SELECT_CASE_CTU2 | CMD_ROUTE_SELECT_CTU2_SRC0)},
	{DVC1, (CMD_ROUTE_SELECT_CASE_CTU2 | CMD_ROUTE_SELECT_CTU2_SRC1)},
};

static struct scu_config ssi_depend_value[] = {
	{RP_MEM_SSI3,		SSI_DEPENDANT},
	{RP_MEM_SRC0_SSI3,	SSI_DEPENDANT},
	{RP_MEM_SRC0_DVC0_SSI3,	SSI_DEPENDANT},
	{RC_SSI4_MEM,		SSI_DEPENDANT},
	{RC_SSI4_SRC1_MEM,	SSI_DEPENDANT},
	{RC_SSI4_SRC1_DVC1_MEM,	SSI_DEPENDANT},
};

static struct scu_config ssi_mode_value[] = {
	{RP_MEM_SSI3,		SSI_MASTER},
	{RP_MEM_SRC0_SSI3,	SSI_MASTER},
	{RP_MEM_SRC0_DVC0_SSI3,	SSI_MASTER},
	{RC_SSI4_MEM,		SSI_SLAVE},
	{RC_SSI4_SRC1_MEM,	SSI_SLAVE},
	{RC_SSI4_SRC1_DVC1_MEM,	SSI_SLAVE},
};

static struct scu_config src_mode_value[] = {
	{RP_MEM_SSI3,		SRC_CR_SYNC},
	{RP_MEM_SRC0_SSI3,	SRC_CR_SYNC},
	{RP_MEM_SRC0_DVC0_SSI3,	SRC_CR_SYNC},
	{RC_SSI4_MEM,		SRC_CR_SYNC},
	{RC_SSI4_SRC1_MEM,	SRC_CR_SYNC},
	{RC_SSI4_SRC1_DVC1_MEM,	SRC_CR_ASYNC},
};

static struct scu_platform_data scu_pdata __initdata = {
	.ssi_master		= SSI3,
	.ssi_slave		= SSI4,
	.ssi_ch			= ssi_ch_value,
	.ssi_ch_num		= ARRAY_SIZE(ssi_ch_value),
	.src_ch			= src_ch_value,
	.src_ch_num		= ARRAY_SIZE(src_ch_value),
	.dvc_ch			= dvc_ch_value,
	.dvc_ch_num		= ARRAY_SIZE(dvc_ch_value),
	.dma_slave_maxnum	= SHDMA_SLAVE_PCM_MAX,
	.audma_slave		= audma_slave_value,
	.audma_slave_num	= ARRAY_SIZE(audma_slave_value),
	.audmapp_slave		= audmapp_slave_value,
	.audmapp_slave_num	= ARRAY_SIZE(audmapp_slave_value),
	.ssiu_busif_adinr	= ssiu_busif_adinr_offset,
	.ssiu_busif_adinr_num	= ARRAY_SIZE(ssiu_busif_adinr_offset),
	.ssiu_control		= ssiu_control_offset,
	.ssiu_control_num	= ARRAY_SIZE(ssiu_control_offset),
	.ssiu_mode1		= ssiu_mode1_value,
	.ssiu_mode1_num		= ARRAY_SIZE(ssiu_mode1_value),
	.dvc_route_select	= dvc_route_select_value,
	.dvc_route_select_num	= ARRAY_SIZE(dvc_route_select_value),
	.ssi_depend		= ssi_depend_value,
	.ssi_depend_num		= ARRAY_SIZE(ssi_depend_value),
	.ssi_mode		= ssi_mode_value,
	.ssi_mode_num		= ARRAY_SIZE(ssi_mode_value),
	.src_mode		= src_mode_value,
	.src_mode_num		= ARRAY_SIZE(src_mode_value),
	.ssi_num_playback	= SSI3,
	.ssi_irq_playback	= (gic_spi(370) + SSI3),
	.ssi_num_capture	= SSI4,
	.ssi_irq_capture	= (gic_spi(370) + SSI4),
	.src_num_playback	= SRC0,
	.src_irq_playback	= (gic_spi(352) + SRC0),
	.src_num_capture	= SRC1,
	.src_irq_capture	= (gic_spi(352) + SRC1),
};

static struct cs42l52_platform_data armadilloeva1500_cs42l52_pdata = {
	.reset_gpio = RCAR_GP_PIN(2, 31), /* AUDIO_RESET_N */
};

static struct i2c_board_info alsa_i2c[] = {
	{ I2C_BOARD_INFO("cs42l52", 0x4a),
	  .platform_data = &armadilloeva1500_cs42l52_pdata, },
};

#define armadilloeva1500_add_alsa_device i2c_register_board_info

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

static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.set_pwr	= shmmcif_set_pwr,
	.down_pwr	= shmmcif_down_pwr,
	.get_cd		= shmmcif_get_cd,
	.slave_id_tx	= SHDMA_SLAVE_MMC_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMC_RX,
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
		.bus_num	= 1,
		.chip_select	= 0,
	},
	{
		.modalias	= "spidev",
		.max_speed_hz	= 6000000,
		.mode		= SPI_MODE_3,
		.bus_num	= 2,
		.chip_select	= 0,
	},
};

#define armadilloeva1500_add_msiof_device spi_register_board_info

/* QSPI flash memory */
static struct mtd_partition spiflash_part[] = {
	/* IPL(Initial Program Loader), read-only */
	[0] = {
		.name = "ipl",
		.offset = 0,
		.size = SZ_64K,
		.mask_flags = MTD_WRITEABLE,	/* read only */
	},
	/* Hermit-At, read-only */
	[1] = {
		.name = "bootloader",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_512K,
		.mask_flags = MTD_WRITEABLE,	/* read only */
	},
	/* FDT(Flattened Device Tree) */
	[2] = {
		.name = "fdt",
		.offset = MTDPART_OFS_APPEND,
		.size = SZ_256K,
		.mask_flags = 0,
	},
	/* Firmware, read-only */
	[3] = {
		.name = "firmware",
		.offset = MTDPART_OFS_APPEND,
		.size = MTDPART_SIZ_FULL,
		.mask_flags = MTD_WRITEABLE,	/* read only */
	},
};

static struct flash_platform_data spiflash_data = {
	.name		= "m25p80",
	.parts		= spiflash_part,
	.nr_parts	= ARRAY_SIZE(spiflash_part),
	.type		= "n25q064",
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

#define armadilloeva1500_add_qspi_device spi_register_board_info

/* SDHI */
static int poweric_readb(u8 cmd)
{
	struct i2c_adapter *adap;
	union i2c_smbus_data data;

	adap = i2c_get_adapter(7);
	if (!adap) {
		pr_err("failed to get i2c adapter\n");
		return -EINVAL;
	}

	i2c_smbus_xfer(adap, 0x5a, 0, I2C_SMBUS_READ,
		       cmd, I2C_SMBUS_BYTE_DATA, &data);

	i2c_put_adapter(adap);

	return data.byte;
}

static int poweric_writeb(u8 cmd, u8 value)
{
	struct i2c_adapter *adap;
	union i2c_smbus_data data;

	adap = i2c_get_adapter(7);
	if (!adap) {
		pr_err("failed to get i2c adapter\n");
		return -EINVAL;
	}

	data.byte = value;
	i2c_smbus_xfer(adap, 0x5a, 0, I2C_SMBUS_WRITE,
		       cmd, I2C_SMBUS_BYTE_DATA, &data);

	i2c_put_adapter(adap);

	return 0;
}

static int poweric_mask_readb(u8 cmd, int offset)
{
	int val;

	val = poweric_readb(cmd) & (1 << offset);

	return !!val;
}

static int poweric_mask_writeb(u8 cmd, int offset, int value)
{
	int val;

	val = poweric_readb(cmd);
	if (val < 0)
		return val;

	if (value)
		val |= (1 << offset);
	else
		val &= ~(1 << offset);

	return poweric_writeb(cmd, val & 0xff);
}

/* Don't assign this to get_cd of struct sh_mobile_sdhi_info. */
static int _sdhi_get_cd(struct platform_device *pdev)
{
	struct sh_mobile_sdhi_info *info;
	struct tmio_mmc_data *data = NULL;

	info = pdev->dev.platform_data;
	if (info)
		data = info->pdata;

	if (data && data->get_cd)
		return data->get_cd(pdev);

	return 1;
}

static void sdhi_set_pwr(struct platform_device *pdev, int state)
{
	int gpio;
	int reg;
	int cd;

	switch (pdev->id) {
	case 0:
		gpio = RCAR_GP_PIN(6, 24);
		reg = 0x28;
		break;
	case 1:
		gpio = RCAR_GP_PIN(6, 26);
		reg = 0x29;
		break;
	default:
		return;
	}

	if (state) {
		gpio_direction_output(gpio, 1);
		poweric_mask_writeb(reg, 0, 1);
	} else {
		cd = _sdhi_get_cd(pdev);
		if (cd) {
			poweric_mask_writeb(reg, 0, 0);
			gpio_direction_output(gpio, 0);
			mdelay(300);
		}
	}
}

static void sdhi_set_ioctrl(int ch, int state)
{
	void __iomem *pfcctl;
	unsigned int ctrl, mask;

	pfcctl = ioremap(0xe6060000, 0x300);

	ctrl = ioread32(pfcctl + 0x8c);
	/* Set 1.8V/3.3V */
	mask = 0xff << (24 - ch * 8);

	if (state == SH_MOBILE_SDHI_SIGNAL_330V)
		ctrl |= mask;
	else if (state == SH_MOBILE_SDHI_SIGNAL_180V)
		ctrl &= ~mask;
	else
		pr_err("update_ioctrl6: unknown state\n");

	iowrite32(~ctrl, pfcctl);	/* PMMR */
	iowrite32(ctrl, pfcctl + 0x8c);	/* IOCTRL6 */

	iounmap(pfcctl);
}

static int sdhi_set_vlt(struct platform_device *pdev, int state)
{
	/* Set 1.8V/3.3V */
	switch (pdev->id) {
	case 0:
		/* SDHI0 */
		if (state)
			sdhi_set_ioctrl(pdev->id, state);
		poweric_mask_writeb(0x32, 7, !state);
		if (!state)
			sdhi_set_ioctrl(pdev->id, state);
		break;
	case 1:
		/* SDHI1 */
		if (state)
			sdhi_set_ioctrl(pdev->id, state);
		poweric_mask_writeb(0x33, 7, !state);
		if (!state)
			sdhi_set_ioctrl(pdev->id, state);
		break;
	default:
		return -EINVAL;
	}
	usleep_range(5000, 5500);
	return 0;
}

static int sdhi_get_vlt(struct platform_device *pdev)
{
	int ret;

	switch (pdev->id) {
	case 0:
		/* SDHI0 */
		ret = poweric_mask_readb(0x32, 7);
		break;
	case 1:
		/* SDHI1 */
		ret = poweric_mask_readb(0x33, 7);
		break;
	default:
		return -EINVAL;
	}

	return ret ? 0 : 1;
}

static int sdhi_init(struct platform_device *pdev,
		const struct sh_mobile_sdhi_ops *ops)
{
	switch (pdev->id) {
	case 0:
		/* SDHI0 */
		gpio_request(RCAR_GP_PIN(6, 24), "SDHI0_vdd");

		sdhi_set_pwr(pdev, 0);
		poweric_writeb(0x28, 0);
		poweric_writeb(0x32, 0);
		sdhi_set_pwr(pdev, 1);
		break;
	case 1:
		/* SDHI1 */
		gpio_request(RCAR_GP_PIN(6, 26), "SDHI1_vdd");

		sdhi_set_pwr(pdev, 0);
		poweric_writeb(0x29, 0);
		poweric_writeb(0x33, 0);
		sdhi_set_pwr(pdev, 1);
		break;
	default:
		break;
	}
	return 0;
}

static struct sh_mobile_sdhi_info sdhi0_platform_data = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.dma_xmit_sz	= SH_MOBILE_SDHI_DMA_XMIT_SZ_32BYTE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
				MMC_CAP_UHS_SDR50 |
				MMC_CAP_UHS_SDR104 |
				MMC_CAP_HW_RESET |
				MMC_CAP_CMD23,
	.tmio_caps2	= MMC_CAP2_NO_2BLKS_READ,
	.tmio_flags	= TMIO_MMC_CLK_NO_SLEEP |
				TMIO_MMC_HAS_IDLE_WAIT |
				TMIO_MMC_HAS_UHS_SCC |
				TMIO_MMC_NO_CTL_CLK_AND_WAIT_CTL |
				TMIO_MMC_NO_CTL_RESET_SDIO |
				TMIO_MMC_CLK_ACTUAL |
				TMIO_MMC_SDIO_STATUS_QUIRK,
	.scc_tapnum	= SH_MOBILE_SDHI_SCC_TAP_8,
	.set_pwr	= sdhi_set_pwr,
	.set_vlt	= sdhi_set_vlt,
	.get_vlt	= sdhi_get_vlt,
	.init		= sdhi_init,
};

static struct sh_mobile_sdhi_info sdhi1_platform_data = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
	.dma_xmit_sz	= SH_MOBILE_SDHI_DMA_XMIT_SZ_32BYTE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
				MMC_CAP_UHS_SDR50,
	.tmio_caps2	= MMC_CAP2_NO_2BLKS_READ,
	.tmio_flags	= TMIO_MMC_CHECK_ILL_FUNC |
				TMIO_MMC_CLK_ACTUAL |
				TMIO_MMC_CLK_NO_SLEEP |
				TMIO_MMC_HAS_IDLE_WAIT |
				TMIO_MMC_NO_CTL_CLK_AND_WAIT_CTL |
				TMIO_MMC_NO_CTL_RESET_SDIO |
				TMIO_MMC_SDIO_STATUS_QUIRK,
	.set_pwr	= sdhi_set_pwr,
	.set_vlt	= sdhi_set_vlt,
	.get_vlt	= sdhi_get_vlt,
	.init		= sdhi_init,
};

/* VIN camera */
static struct i2c_board_info armadilloeva1500_i2c_camera[] = {
	{ I2C_BOARD_INFO("adv7180", 0x20), },
};

static void camera_power_on(void)
{
	return;
}

static void camera_power_off(void)
{
	return;
}

static int adv7180_power(struct device *dev, int mode)
{
	if (mode)
		camera_power_on();
	else
		camera_power_off();

	return 0;
}

static const struct soc_camera_link adv7180_ch2_link __initconst = {
	.bus_id = 2,
	.power  = adv7180_power,
	.board_info = &armadilloeva1500_i2c_camera[0],
	.i2c_adapter_id = 8,
	.module_name = "adv7180",
};

#define armadilloeva1500_add_vin_device(idx, link)			\
	platform_device_register_data(&platform_bus, "soc-camera-pdrv",	\
				      idx , &link,			\
				      sizeof(struct soc_camera_link));

/* VSP1 */
static const struct vsp1_platform_data armadilloeva1500_vspr_pdata __initconst = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data armadilloeva1500_vsps_pdata __initconst = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 3,
	.wpf_count = 4,
};

static const struct vsp1_platform_data armadilloeva1500_vspd0_pdata __initconst = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data armadilloeva1500_vspd1_pdata __initconst = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data * const armadilloeva1500_vsp1_pdata[4] __initconst
									= {
	&armadilloeva1500_vspr_pdata,
	&armadilloeva1500_vsps_pdata,
	&armadilloeva1500_vspd0_pdata,
	&armadilloeva1500_vspd1_pdata,
};

static const struct resource vsp1_0_resources[] __initconst = {
	DEFINE_RES_MEM(0xfe920000, 0x8000),
	DEFINE_RES_IRQ(gic_spi(266)),
};

static const struct resource vsp1_1_resources[] __initconst = {
	DEFINE_RES_MEM(0xfe928000, 0x8000),
	DEFINE_RES_IRQ(gic_spi(267)),
};

static const struct resource vsp1_2_resources[] __initconst = {
	DEFINE_RES_MEM(0xfe930000, 0x8000),
	DEFINE_RES_IRQ(gic_spi(246)),
};

static const struct resource vsp1_3_resources[] __initconst = {
	DEFINE_RES_MEM(0xfe938000, 0x8000),
	DEFINE_RES_IRQ(gic_spi(247)),
};

static const struct resource * const vsp1_resources[4] __initconst = {
	vsp1_0_resources,
	vsp1_1_resources,
	vsp1_2_resources,
	vsp1_3_resources,
};

static void __init armadilloeva1500_add_vsp1_devices(void)
{
	struct platform_device_info info = {
		.name = "vsp1",
		.size_data = sizeof(*armadilloeva1500_vsp1_pdata[0]),
		.num_res = 2,
		.dma_mask = DMA_BIT_MASK(32),
	};
	unsigned int i;

	for (i = 2; i < ARRAY_SIZE(vsp1_resources); ++i) {
		info.id = i;
		info.data = armadilloeva1500_vsp1_pdata[i];
		info.res = vsp1_resources[i];

		platform_device_register_full(&info);
	}
}

/* EEPROM */
static int eeprom_readb(u8 cmd)
{
	struct i2c_adapter *adap;
	union i2c_smbus_data data;

	adap = i2c_get_adapter(8);
	if (!adap) {
		pr_err("failed to get i2c adapter\n");
		return -EINVAL;
	}

	i2c_smbus_xfer(adap, 0x50, 0, I2C_SMBUS_READ,
		       cmd, I2C_SMBUS_BYTE_DATA, &data);

	i2c_put_adapter(adap);

	return data.byte;
}

static int eeprom_readw(u8 cmd)
{
	struct i2c_adapter *adap;
	union i2c_smbus_data data;

	adap = i2c_get_adapter(8);
	if (!adap) {
		pr_err("failed to get i2c adapter\n");
		return -EINVAL;
	}

	i2c_smbus_xfer(adap, 0x50, 0, I2C_SMBUS_READ,
		       cmd, I2C_SMBUS_WORD_DATA, &data);

	i2c_put_adapter(adap);

	return data.word;
}

static const struct pinctrl_map armadilloeva1500_pinctrl_map[] = {
	/* DU0 (CON25: LVDS) */
	/* DU1 (CON17: HDMI, CON23: LCD) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_rgb888", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_sync", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_0", "du"),
#if defined(CONFIG_DRM_RCAR_LCD)
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "pwm4", "pwm4"),
#else
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_1", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_oddf", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_cde", "du"),
#endif
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du1_clk_in", "du1"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_disp", "du"),
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFB0)
	/* SCIFB0 (CON16: D-Sub9) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-r8a7791",
				  "scifb0_data", "scifb0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-r8a7791",
				  "scifb0_ctrl", "scifb0"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFB1)
	/* SCIFB1 (CON14: USB-Serial) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.3", "pfc-r8a7791",
				  "scifb1_data", "scifb1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.3", "pfc-r8a7791",
				  "scifb1_ctrl", "scifb1"),
#endif
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7791",
				  "eth_mdio", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7791",
				  "eth_rmii", "eth"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7791",
				  "intc_irq0", "intc"),
	/* MSIOF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_clk", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_sync", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_ss1", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_ss2", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_rx", "msiof0"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.0", "pfc-r8a7791",
				  "msiof0_tx", "msiof0"),
	/* MSIOF1 */
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7791",
				  "msiof1_clk_c", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7791",
				  "msiof1_sync_c", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7791",
				  "msiof1_rx_c", "msiof1"),
	PIN_MAP_MUX_GROUP_DEFAULT("spi_sh_msiof.1", "pfc-r8a7791",
				  "msiof1_tx_c", "msiof1"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7791",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7791",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7791",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7791",
				  "sdhi0_wp", "sdhi0"),
	/* SDHI1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7791",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7791",
				  "sdhi1_ctrl", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7791",
				  "sdhi1_cd", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a7791",
				  "sdhi1_wp", "sdhi1"),
	/* SSP */
	PIN_MAP_MUX_GROUP_DEFAULT("ssp_dev", "pfc-r8a7791",
				  "ssp", "ssp"),
	/* USB0 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7791",
				  "usb0_pwen", "usb0"),
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7791",
				  "usb0_ovc", "usb0"),
	/* USB1 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.1", "pfc-r8a7791",
				  "usb1_pwen", "usb1"),
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.1", "pfc-r8a7791",
				  "usb1_ovc", "usb1"),
	/* VIN0 */
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_data_b", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_hsync_signal", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_vsync_signal", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_field_signal", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_data_enable", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_clk", "vin0"),
	/* VIN2 */
	PIN_MAP_MUX_GROUP_DEFAULT("vin.2", "pfc-r8a7791",
				  "vin2_data", "vin2"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.2", "pfc-r8a7791",
				  "vin2_clk", "vin2"),
	/* RTC */
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_irqc.0", "pfc-r8a7791",
				  "intc_irq2", "intc"),
#if defined(CONFIG_SND_SOC_ARMADILLOEVA1500_CS42L52)
	/* SSI34 */
	PIN_MAP_MUX_GROUP_DEFAULT("scu-pcm-audio.0", "pfc-r8a7791",
				  "ssi34_ctrl", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("scu-pcm-audio.0", "pfc-r8a7791",
				  "ssi3_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("scu-pcm-audio.0", "pfc-r8a7791",
				  "ssi4_data", "ssi"),
	PIN_MAP_MUX_GROUP_DEFAULT("scu-pcm-audio.0", "pfc-r8a7791",
				  "audio_clka", "adg"),
#endif
#if defined(CONFIG_TOUCHSCREEN_ST1232)
	PIN_MAP_MUX_GROUP_DEFAULT("renesas_irqc.0", "pfc-r8a7791",
				  "intc_irq9", "intc"),
#endif
};

static void armadilloeva1500_restart(char mode, const char *cmd)
{
	struct i2c_adapter *adap;
	union i2c_smbus_data data;
	int busnum = 7;

	adap = i2c_get_adapter(busnum);
	if (!adap) {
		pr_err("failed to get adapter i2c%d\n", busnum);
		return;
	}

	i2c_smbus_xfer(adap, 0x5a, 0, I2C_SMBUS_READ,
		       0x13, I2C_SMBUS_BYTE_DATA, &data);
	data.byte |= 0x02;
	i2c_smbus_xfer(adap, 0x5a, 0, I2C_SMBUS_WRITE,
		       0x13, I2C_SMBUS_BYTE_DATA, &data);
}

static void __init armadilloeva1500_add_standard_devices(void)
{
	r8a7791_clock_init();

	pinctrl_register_mappings(armadilloeva1500_pinctrl_map,
				  ARRAY_SIZE(armadilloeva1500_pinctrl_map));
	r8a7791_pinmux_init();

	r8a7791_add_standard_devices();

	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &armadilloeva1500_keys_pdata,
				      sizeof(armadilloeva1500_keys_pdata));

	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &armadilloeva1500_leds_pdata,
				      sizeof(armadilloeva1500_leds_pdata));

	platform_device_register_data(&platform_bus, "i2c-gpio", 7,
				      &armadilloeva1500_i2c_gpio0_pdata,
				      sizeof(armadilloeva1500_i2c_gpio0_pdata));
	platform_device_register_data(&platform_bus, "i2c-gpio", 8,
				      &armadilloeva1500_i2c_gpio1_pdata,
				      sizeof(armadilloeva1500_i2c_gpio1_pdata));

	platform_device_register_data(&platform_bus, "i2c-gpio", 9,
				      &armadilloeva1500_i2c_gpio2_pdata,
				      sizeof(armadilloeva1500_i2c_gpio2_pdata));

	armadilloeva1500_add_rtc_device(7, rtc_i2c, ARRAY_SIZE(rtc_i2c));
	armadilloeva1500_add_ts_device(9, ts_i2c, ARRAY_SIZE(ts_i2c));

	platform_device_register_data(&platform_bus, "pwm-backlight", -1,
				      &armadilloeva1500_pwm_backlight_pdata,
				      sizeof(armadilloeva1500_pwm_backlight_pdata));

	platform_device_register_resndata(&platform_bus, "r8a779x-ether", -1,
					  ether_resources,
					  ARRAY_SIZE(ether_resources),
					  &ether_pdata, sizeof(ether_pdata));

	r8a7791_add_mmc_device(&sh_mmcif_plat);
	r8a7791_add_scu_device(&scu_pdata);
	r8a7791_add_sdhi_device(&sdhi0_platform_data, 0);
	r8a7791_add_sdhi_device(&sdhi1_platform_data, 1);
	armadilloeva1500_add_alsa_device(8, alsa_i2c, ARRAY_SIZE(alsa_i2c));
	armadilloeva1500_add_msiof_device(spi_bus, ARRAY_SIZE(spi_bus));
	armadilloeva1500_add_qspi_device(spi_info, ARRAY_SIZE(spi_info));
	armadilloeva1500_add_vin_device(2, adv7180_ch2_link);

	armadilloeva1500_add_du_device();
	armadilloeva1500_add_vsp1_devices();
}

static void __init armadilloeva1500_init_late(void)
{
	int high, low, val;

	shmobile_init_late();

	/* RTC */
	irq_set_irq_type(irq_pin(2), IRQ_TYPE_EDGE_FALLING); /* RTC_INT_N */

	/* Touch Screen */
	gpio_request_one(RCAR_GP_PIN(3, 31), GPIOF_OUT_INIT_HIGH, "TP_RST");
	irq_set_irq_type(irq_pin(9), IRQ_TYPE_EDGE_FALLING); /* TP_INT */

	high = eeprom_readw(0x24);
	low = eeprom_readw(0x26);
	system_rev = (high << 16) | low;

	val = eeprom_readb(0x2f);
	if (val) {
		high = eeprom_readw(0x2a);
		low = eeprom_readw(0x28);
		system_serial_low = (high << 16) | low;
	}
}

static const char *armadilloeva1500_boards_compat_dt[] __initdata = {
	"atmark-techno,armadilloeva1500",
	NULL,
};

DT_MACHINE_START(ARMADILLOEVA1500_DT, "armadilloeva1500")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.timer		= &rcar_gen2_timer,
	.init_machine	= armadilloeva1500_add_standard_devices,
	.init_late	= armadilloeva1500_init_late,
	.restart	= armadilloeva1500_restart,
	.dt_compat	= armadilloeva1500_boards_compat_dt,
MACHINE_END

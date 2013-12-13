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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
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
#include <linux/sh_eth.h>
#include <linux/spi/flash.h>
#include <linux/spi/spi.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <media/vin.h>
#include <sound/sh_scu.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* DU */
static struct rcar_du_encoder_data koelsch_du_encoders[] = {
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
		.exclk = 148500000,
	},
#if defined(CONFIG_DRM_ADV7511)
	{
		.type = RCAR_DU_ENCODER_HDMI,
		.output = RCAR_DU_OUTPUT_DPAD0,
		.exclk = 74250000,
	},
#endif
};

static const struct rcar_du_platform_data koelsch_du_pdata __initconst = {
	.encoders = koelsch_du_encoders,
	.num_encoders = ARRAY_SIZE(koelsch_du_encoders),
};

static const struct resource du_resources[] __initconst = {
	DEFINE_RES_MEM(0xfeb00000, 0x40000),
	DEFINE_RES_MEM_NAMED(0xfeb90000, 0x1c, "lvds.0"),
	DEFINE_RES_IRQ(gic_spi(256)),
	DEFINE_RES_IRQ(gic_spi(268)),
};

static void __init koelsch_add_du_device(void)
{
	struct platform_device_info info = {
		.name = "rcar-du-r8a7791",
		.id = -1,
		.res = du_resources,
		.num_res = ARRAY_SIZE(du_resources),
		.data = &koelsch_du_pdata,
		.size_data = sizeof(koelsch_du_pdata),
		.dma_mask = DMA_BIT_MASK(32),
	};

	platform_device_register_full(&info);
}

/* GPIO KEY */
#define GPIO_KEY(c, g, d, w, ...) \
	{ .code = c, .gpio = g, .desc = d, .wakeup = w, .active_low = 1, \
 	  .debounce_interval = 20 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_4, RCAR_GP_PIN(5, 3), "SW2-pin4", 1),
	GPIO_KEY(KEY_3, RCAR_GP_PIN(5, 2), "SW2-pin3", 1),
	GPIO_KEY(KEY_2, RCAR_GP_PIN(5, 1), "SW2-pin2", 1),
	GPIO_KEY(KEY_1, RCAR_GP_PIN(5, 0), "SW2-pin1", 1),
};

static const struct gpio_keys_platform_data koelsch_keys_pdata __initconst = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* Ether */
static const struct sh_eth_plat_data ether_pdata __initconst = {
	.phy			= 0x1,
	.edmac_endian		= EDMAC_LITTLE_ENDIAN,
	.register_type		= SH_ETH_REG_FAST_RCAR,
	.phy_interface		= PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low	= 1,
};

static const struct resource ether_resources[] __initconst = {
	DEFINE_RES_MEM(0xee700000, 0x400),
	DEFINE_RES_IRQ(gic_spi(162)), /* IRQ0 */
};

/* Audio */
static struct scu_config ssi_ch_value[] = {
	{RP_MEM_SSI0,		SSI0},
	{RP_MEM_SRC0_SSI0,	SSI0},
	{RP_MEM_SRC0_DVC0_SSI0,	SSI0},
	{RC_SSI1_MEM,		SSI1},
	{RC_SSI1_SRC1_MEM,	SSI1},
	{RC_SSI1_SRC1_DVC1_MEM,	SSI1},
};

static struct scu_config src_ch_value[] = {
	{RP_MEM_SSI0,		-1},
	{RP_MEM_SRC0_SSI0,	SRC0},
	{RP_MEM_SRC0_DVC0_SSI0,	SRC0},
	{RC_SSI1_MEM,		-1},
	{RC_SSI1_SRC1_MEM,	SRC1},
	{RC_SSI1_SRC1_DVC1_MEM,	SRC1},
};

static struct scu_config dvc_ch_value[] = {
	{RP_MEM_SSI0,		-1},
	{RP_MEM_SRC0_SSI0,	-1},
	{RP_MEM_SRC0_DVC0_SSI0,	DVC0},
	{RC_SSI1_MEM,		-1},
	{RC_SSI1_SRC1_MEM,	-1},
	{RC_SSI1_SRC1_DVC1_MEM,	DVC1},
};

static struct scu_config audma_slave_value[] = {
	{RP_MEM_SSI0,		SHDMA_SLAVE_PCM_MEM_SSI0},
	{RP_MEM_SRC0_SSI0,	SHDMA_SLAVE_PCM_MEM_SRC0},
	{RP_MEM_SRC0_DVC0_SSI0,	SHDMA_SLAVE_PCM_MEM_SRC0},
	{RC_SSI1_MEM,		SHDMA_SLAVE_PCM_SSI1_MEM},
	{RC_SSI1_SRC1_MEM,	SHDMA_SLAVE_PCM_SRC1_MEM},
	{RC_SSI1_SRC1_DVC1_MEM,	SHDMA_SLAVE_PCM_CMD1_MEM},
};

static struct scu_config audmapp_slave_value[] = {
	{RP_MEM_SSI0,		-1},
	{RP_MEM_SRC0_SSI0,	SHDMA_SLAVE_PCM_SRC0_SSI0},
	{RP_MEM_SRC0_DVC0_SSI0,	SHDMA_SLAVE_PCM_CMD0_SSI0},
	{RC_SSI1_MEM,		-1},
	{RC_SSI1_SRC1_MEM,	SHDMA_SLAVE_PCM_SSI1_SRC1},
	{RC_SSI1_SRC1_DVC1_MEM,	SHDMA_SLAVE_PCM_SSI1_SRC1},
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
	{SSI1, SSI_MODE1_SSI1_MASTER},
	{SSI2, SSI_MODE1_SSI2_IND},
	{SSI4, SSI_MODE1_SSI4_IND},
};

static struct scu_config dvc_route_select_value[] = {
	{DVC0, (CMD_ROUTE_SELECT_CASE_CTU2 | CMD_ROUTE_SELECT_CTU2_SRC0)},
	{DVC1, (CMD_ROUTE_SELECT_CASE_CTU2 | CMD_ROUTE_SELECT_CTU2_SRC1)},
};

static struct scu_config ssi_depend_value[] = {
	{RP_MEM_SSI0,		SSI_INDEPENDANT},
	{RP_MEM_SRC0_SSI0,	SSI_DEPENDANT},
	{RP_MEM_SRC0_DVC0_SSI0,	SSI_DEPENDANT},
	{RC_SSI1_MEM,		SSI_INDEPENDANT},
	{RC_SSI1_SRC1_MEM,	SSI_DEPENDANT},
	{RC_SSI1_SRC1_DVC1_MEM,	SSI_DEPENDANT},
};

static struct scu_config ssi_mode_value[] = {
	{RP_MEM_SSI0,		SSI_MASTER},
	{RP_MEM_SRC0_SSI0,	SSI_MASTER},
	{RP_MEM_SRC0_DVC0_SSI0,	SSI_MASTER},
	{RC_SSI1_MEM,		SSI_SLAVE},
	{RC_SSI1_SRC1_MEM,	SSI_SLAVE},
	{RC_SSI1_SRC1_DVC1_MEM,	SSI_SLAVE},
};

static struct scu_config src_mode_value[] = {
	{RP_MEM_SSI0,		SRC_CR_SYNC},
	{RP_MEM_SRC0_SSI0,	SRC_CR_SYNC},
	{RP_MEM_SRC0_DVC0_SSI0,	SRC_CR_SYNC},
	{RC_SSI1_MEM,		SRC_CR_SYNC},
	{RC_SSI1_SRC1_MEM,	SRC_CR_SYNC},
	{RC_SSI1_SRC1_DVC1_MEM,	SRC_CR_ASYNC},
};

static struct scu_platform_data scu_pdata __initdata = {
	.ssi_master		= SSI0,
	.ssi_slave		= SSI1,
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
};

static struct i2c_board_info alsa_i2c[] = {
	{ I2C_BOARD_INFO("ak4642", 0x12), },
};

#define koelsch_add_alsa_device i2c_register_board_info

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

#define koelsch_add_msiof_device spi_register_board_info

/* POWER IC */
static struct i2c_board_info poweric_i2c[] = {
	{ I2C_BOARD_INFO("da9063", 0x58), },
};

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

#define koelsch_add_qspi_device spi_register_board_info

/* SDHI */
static void sdhi_set_pwr(struct platform_device *pdev, int state)
{
	switch (pdev->id) {
	case 0:
		gpio_set_value(RCAR_GP_PIN(7, 17), state);
		break;
	case 1:
		gpio_set_value(RCAR_GP_PIN(7, 18), state);
		break;
	case 2:
		gpio_set_value(RCAR_GP_PIN(7, 19), state);
		break;
	default:
		break;
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
		gpio_set_value(RCAR_GP_PIN(2, 12), state);
		if (!state)
			sdhi_set_ioctrl(pdev->id, state);
		break;
	case 1:
		/* SDHI1 */
		if (state)
			sdhi_set_ioctrl(pdev->id, state);
		gpio_set_value(RCAR_GP_PIN(2, 13), state);
		if (!state)
			sdhi_set_ioctrl(pdev->id, state);
		break;
	case 2:
		/* SDHI2 */
		if (state)
			sdhi_set_ioctrl(pdev->id, state);
		gpio_set_value(RCAR_GP_PIN(2, 26), state);
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
		ret = gpio_get_value(RCAR_GP_PIN(2, 12));
		break;
	case 1:
		/* SDHI1 */
		ret = gpio_get_value(RCAR_GP_PIN(2, 13));
		break;
	case 2:
		/* SDHI2 */
		ret = gpio_get_value(RCAR_GP_PIN(2, 26));
		break;
	default:
		return -EINVAL;
	}
	return ret ? 1 : 0;
}

static int sdhi_init(struct platform_device *pdev,
		const struct sh_mobile_sdhi_ops *ops)
{
	switch (pdev->id) {
	case 0:
		/* SDHI0 */
		gpio_request(RCAR_GP_PIN(7, 17), "SDHI0_vdd");
		gpio_request(RCAR_GP_PIN(2, 12), "SDHI0_vol");
		gpio_direction_output(RCAR_GP_PIN(7, 17), 1);
		gpio_direction_output(RCAR_GP_PIN(2, 12), 1);
		break;
	case 1:
		/* SDHI1 */
		gpio_request(RCAR_GP_PIN(7, 18), "SDHI1_vdd");
		gpio_request(RCAR_GP_PIN(2, 13), "SDHI1_vol");
		gpio_direction_output(RCAR_GP_PIN(7, 18), 1);
		gpio_direction_output(RCAR_GP_PIN(2, 13), 1);
		break;
	case 2:
		/* SDHI2 */
		gpio_request(RCAR_GP_PIN(7, 19), "SDHI2_vdd");
		gpio_request(RCAR_GP_PIN(2, 26), "SDHI2_vol");
		gpio_direction_output(RCAR_GP_PIN(7, 19), 1);
		gpio_direction_output(RCAR_GP_PIN(2, 26), 1);
		break;
	default:
		break;
	}
	return 0;
}

static struct sh_mobile_sdhi_info sdhi0_platform_data = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
				MMC_CAP_UHS_SDR50 |
				MMC_CAP_UHS_SDR104 |
				MMC_CAP_CMD23,
	.tmio_caps2	= MMC_CAP2_NO_2BLKS_READ,
	.tmio_flags	= TMIO_MMC_BUFF_16BITACC_ACTIVE_HIGH |
				TMIO_MMC_CLK_NO_SLEEP |
				TMIO_MMC_HAS_IDLE_WAIT |
				TMIO_MMC_NO_CTL_CLK_AND_WAIT_CTL |
				TMIO_MMC_NO_CTL_RESET_SDIO |
				TMIO_MMC_CLK_ACTUAL |
				TMIO_MMC_SDIO_STATUS_QUIRK,
	.set_pwr	= sdhi_set_pwr,
	.set_vlt	= sdhi_set_vlt,
	.get_vlt	= sdhi_get_vlt,
	.init		= sdhi_init,
};

static struct sh_mobile_sdhi_info sdhi1_platform_data = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI1_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI1_RX,
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

static struct sh_mobile_sdhi_info sdhi2_platform_data = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI2_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI2_RX,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
				MMC_CAP_UHS_SDR50,
	.tmio_caps2	= MMC_CAP2_NO_2BLKS_READ,
	.tmio_flags	= TMIO_MMC_CHECK_ILL_FUNC |
				TMIO_MMC_CLK_NO_SLEEP |
				TMIO_MMC_HAS_IDLE_WAIT |
				TMIO_MMC_NO_CTL_CLK_AND_WAIT_CTL |
				TMIO_MMC_NO_CTL_RESET_SDIO |
				TMIO_MMC_CLK_ACTUAL |
				TMIO_MMC_SDIO_STATUS_QUIRK |
				TMIO_MMC_WRPROTECT_DISABLE,
	.set_pwr	= sdhi_set_pwr,
	.set_vlt	= sdhi_set_vlt,
	.get_vlt	= sdhi_get_vlt,
	.init		= sdhi_init,
};

/* VIN camera */
static struct i2c_board_info koelsch_i2c_camera[] = {
	{ I2C_BOARD_INFO("adv7612", 0x4c), },
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

static int adv7612_power(struct device *dev, int mode)
{
	if (mode)
		camera_power_on();
	else
		camera_power_off();

	return 0;
}

static int adv7180_power(struct device *dev, int mode)
{
	if (mode)
		camera_power_on();
	else
		camera_power_off();

	return 0;
}

static const struct soc_camera_link adv7612_ch0_link __initconst = {
	.bus_id = 0,
	.power  = adv7612_power,
	.board_info = &koelsch_i2c_camera[0],
	.i2c_adapter_id = 2,
	.module_name = "adv7612",
};

static const struct soc_camera_link adv7180_ch1_link __initconst = {
	.bus_id = 1,
	.power  = adv7180_power,
	.board_info = &koelsch_i2c_camera[1],
	.i2c_adapter_id = 2,
	.module_name = "adv7180",
};

#define koelsch_add_vin_device(idx, link)			\
	platform_device_register_data(&platform_bus, "soc-camera-pdrv",	\
				      idx , &link,			\
				      sizeof(struct soc_camera_link));

/* VSP1 */
static const struct vsp1_platform_data koelsch_vspr_pdata __initconst = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data koelsch_vsps_pdata __initconst = {
	.features = 0,
	.rpf_count = 5,
	.uds_count = 3,
	.wpf_count = 4,
};

static const struct vsp1_platform_data koelsch_vspd0_pdata __initconst = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data koelsch_vspd1_pdata __initconst = {
	.features = VSP1_HAS_LIF,
	.rpf_count = 4,
	.uds_count = 1,
	.wpf_count = 4,
};

static const struct vsp1_platform_data * const koelsch_vsp1_pdata[4] __initconst
									= {
	&koelsch_vspr_pdata,
	&koelsch_vsps_pdata,
	&koelsch_vspd0_pdata,
	&koelsch_vspd1_pdata,
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

static void __init koelsch_add_vsp1_devices(void)
{
	struct platform_device_info info = {
		.name = "vsp1",
		.size_data = sizeof(*koelsch_vsp1_pdata[0]),
		.num_res = 2,
		.dma_mask = DMA_BIT_MASK(32),
	};
	unsigned int i;

	for (i = 2; i < ARRAY_SIZE(vsp1_resources); ++i) {
		info.id = i;
		info.data = koelsch_vsp1_pdata[i];
		info.res = vsp1_resources[i];

		platform_device_register_full(&info);
	}
}

static const struct pinctrl_map koelsch_pinctrl_map[] = {
	/* DU (CN11: HDMI, CN13: LVDS) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_rgb888", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_sync_1", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_sync_0", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_0", "du"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7791", "pfc-r8a7791",
				  "du_clk_out_1", "du"),
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIF0)
	/* SCIF0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.6", "pfc-r8a7791",
	"scif0_data_d", "scif0"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIF1)
	/* SCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.7", "pfc-r8a7791",
	"scif1_data_d", "scif1"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFA0)
	/* SCIFA0 (CN19: DEBUG SERIAL0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a7790",
				  "scifa0_data", "scifa0"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFA1)
	/* SCIFA1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.1", "pfc-r8a7790",
				  "scifa1_data", "scifa1"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFA2)
	/* SCIFA2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.5", "pfc-r8a7790",
				  "scifa2_data", "scifa2"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFB0)
	/* SCIFB0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-r8a7790",
				  "scifb0_data", "scifb0"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFB1)
	/* SCIFB1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.3", "pfc-r8a7790",
				  "scifb1_data", "scifb1"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_SCIFB2)
	/* SCIFB2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-r8a7790",
				  "scifb2_data", "scifb2"),
#endif
#if defined(CONFIG_SERIAL_SH_SCI_USE_HSCIF1)
	/* HSCIF1 (CN20: DEBUG SERIAL1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.9", "pfc-r8a7791",
				  "hscif1_data", "hscif1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.9", "pfc-r8a7791",
				  "hscif1_ctrl", "hscif1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.9", "pfc-r8a7791",
				  "hscif1_clk", "hscif1"),
#endif
	/* Ether */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a779x-ether", "pfc-r8a7791",
				  "eth_link", "eth"),
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
	/* SDHI2 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7791",
				  "sdhi2_data4", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7791",
				  "sdhi2_ctrl", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7791",
				  "sdhi2_cd", "sdhi2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.2", "pfc-r8a7791",
				  "sdhi2_wp", "sdhi2"),
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
				  "vin0_data_g", "vin0"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.0", "pfc-r8a7791",
				  "vin0_data_r", "vin0"),
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
	/* VIN1 */
	PIN_MAP_MUX_GROUP_DEFAULT("vin.1", "pfc-r8a7791",
				  "vin1_data", "vin1"),
	PIN_MAP_MUX_GROUP_DEFAULT("vin.1", "pfc-r8a7791",
				  "vin1_clk", "vin1"),
};

static void koelsch_restart(char mode, const char *cmd)
{
	struct i2c_adapter *adap;
	struct i2c_client *client;
	u8 val;
	int busnum = 6;

	adap = i2c_get_adapter(busnum);
	if (!adap) {
		pr_err("failed to get adapter i2c%d\n", busnum);
		return;
	}

	client = i2c_new_device(adap, &poweric_i2c[0]);
	if (!client)
		pr_err("failed to register %s to i2c%d\n",
		       poweric_i2c[0].type, busnum);

	i2c_put_adapter(adap);

	val = i2c_smbus_read_byte_data(client, 0x13);

	if (val < 0)
		pr_err("couldn't access da9063\n");

	val |= 0x02;

	i2c_smbus_write_byte_data(client, 0x13, val);
}

static void __init koelsch_add_standard_devices(void)
{
	r8a7791_clock_init();

	pinctrl_register_mappings(koelsch_pinctrl_map,
				  ARRAY_SIZE(koelsch_pinctrl_map));
	r8a7791_pinmux_init();

	r8a7791_add_standard_devices();

	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &koelsch_keys_pdata,
				      sizeof(koelsch_keys_pdata));

	platform_device_register_resndata(&platform_bus, "r8a779x-ether", -1,
					  ether_resources,
					  ARRAY_SIZE(ether_resources),
					  &ether_pdata, sizeof(ether_pdata));

	r8a7791_add_mmc_device(&sh_mmcif_plat);
	r8a7791_add_scu_device(&scu_pdata);
	r8a7791_add_sdhi_device(&sdhi0_platform_data, 0);
	r8a7791_add_sdhi_device(&sdhi1_platform_data, 1);
	r8a7791_add_sdhi_device(&sdhi2_platform_data, 2);
	koelsch_add_alsa_device(2, alsa_i2c, ARRAY_SIZE(alsa_i2c));
	koelsch_add_msiof_device(spi_bus, ARRAY_SIZE(spi_bus));
	koelsch_add_qspi_device(spi_info, ARRAY_SIZE(spi_info));
	koelsch_add_vin_device(0, adv7612_ch0_link);
	koelsch_add_vin_device(1, adv7180_ch1_link);

	koelsch_add_du_device();
	koelsch_add_vsp1_devices();
}

static const char *koelsch_boards_compat_dt[] __initdata = {
	"renesas,koelsch",
	NULL,
};

DT_MACHINE_START(KOELSCH_DT, "koelsch")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.timer		= &rcar_gen2_timer,
	.init_machine	= koelsch_add_standard_devices,
	.init_late	= shmobile_init_late,
	.restart	= koelsch_restart,
	.dt_compat	= koelsch_boards_compat_dt,
MACHINE_END

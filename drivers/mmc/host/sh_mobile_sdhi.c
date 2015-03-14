/*
 * SuperH Mobile SDHI
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Based on "Compaq ASIC3 support":
 *
 * Copyright 2001 Compaq Computer Corporation.
 * Copyright 2004-2005 Phil Blundell
 * Copyright 2007-2008 OpenedHand Ltd.
 *
 * Authors: Phil Blundell <pb@handhelds.org>,
 *	    Samuel Ortiz <sameo@openedhand.com>
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/sh_dma.h>
#include <linux/delay.h>

#include "tmio_mmc.h"

/* SDHI host controller version */
#define SDHI_VERSION_CB0D	0xCB0D
#define SDHI_VERSION_490C	0x490C

#define EXT_ACC           0xe4
#define SD_DMACR(x)       ((x) ? 0x192 : 0xe6)

/* Maximum number DMA transfer size */
#define SH_MOBILE_SDHI_DMA_XMIT_SZ_MAX	6

/* SDHI host controller type */
enum {
	SH_MOBILE_SDHI_VER_490C = 0,
	SH_MOBILE_SDHI_VER_CB0D,
	SH_MOBILE_SDHI_VER_MAX, /* SDHI max */
};

/* SD buffer access size */
enum {
	SH_MOBILE_SDHI_EXT_ACC_16BIT = 0,
	SH_MOBILE_SDHI_EXT_ACC_32BIT,
	SH_MOBILE_SDHI_EXT_ACC_MAX, /* EXT_ACC access size max */
};

/* SD buffer access size for EXT_ACC */
static unsigned short sh_acc_size[][SH_MOBILE_SDHI_EXT_ACC_MAX] = {
	/* { 16bit, 32bit, }, */
	{ 0x0000, 0x0001, },	/* SH_MOBILE_SDHI_VER_490C */
	{ 0x0001, 0x0000, },	/* SH_MOBILE_SDHI_VER_CB0D */
};

/* transfer size for SD_DMACR */
static int sh_dma_size[][SH_MOBILE_SDHI_DMA_XMIT_SZ_MAX] = {
	/* { 1byte, 2byte, 4byte, 8byte, 16byte, 32byte, }, */
	{ -EINVAL, 0x0000, 0x0000, -EINVAL, 0x5000, 0xa000, },	/* VER_490C */
	{ -EINVAL, 0x0000, 0x0000, -EINVAL, 0x0001, 0x0004, },	/* VER_CB0D */
};

struct sh_mobile_sdhi {
	struct clk *clk;
	struct tmio_mmc_data mmc_data;
	struct sh_dmae_slave param_tx;
	struct sh_dmae_slave param_rx;
	struct tmio_mmc_dma dma_priv;
};

static int sh_mobile_sdhi_clk_enable(struct platform_device *pdev, unsigned int *f)
{
	struct mmc_host *mmc = dev_get_drvdata(&pdev->dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = container_of(host->pdata, struct sh_mobile_sdhi, mmc_data);
	int ret = clk_enable(priv->clk);
	if (ret < 0)
		return ret;

	*f = clk_get_rate(priv->clk);
	return 0;
}

static void sh_mobile_sdhi_clk_disable(struct platform_device *pdev)
{
	struct mmc_host *mmc = dev_get_drvdata(&pdev->dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = container_of(host->pdata, struct sh_mobile_sdhi, mmc_data);
	clk_disable(priv->clk);
}

static void sh_mobile_sdhi_set_clk_div(struct platform_device *pdev, int clk)
{
	struct mmc_host *mmc = dev_get_drvdata(&pdev->dev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	if (clk == true) {
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
				sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x00ff);
	}
}

static void sh_mobile_sdhi_set_pwr(struct platform_device *pdev, int state)
{
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;

	p->set_pwr(pdev, state);
}

static int sh_mobile_sdhi_get_cd_native_hotplug(struct mmc_host *mmc)
{
	struct tmio_mmc_host *host = mmc_priv(mmc);
	u16 val;

	tmio_mmc_enable_mmc_irqs(host, TMIO_STAT_CARD_REMOVE |
				 TMIO_STAT_CARD_INSERT);

	val = sd_ctrl_read16(host, CTL_STATUS) & TMIO_STAT_SIGSTATE;

	return val ? 1 : 0;
}

static int sh_mobile_sdhi_get_cd(struct platform_device *pdev)
{
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);

	if (p && p->get_cd)
		return p->get_cd(pdev);
	if (host->native_hotplug)
		return sh_mobile_sdhi_get_cd_native_hotplug(mmc);

	return -ENOSYS;
}

static int sh_mobile_sdhi_get_ro(struct platform_device *pdev)
{
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;

	return p->get_ro(pdev);

}

#define SH_MOBILE_SDHI_POWEROFF	0
#define SH_MOBILE_SDHI_POWERON	1

static int sh_mobile_sdhi_start_signal_voltage_switch(
	struct tmio_mmc_host *host, unsigned char signal_voltage)
{
	struct sh_mobile_sdhi_info *p = host->pdev->dev.platform_data;
	unsigned short reg;
	int voltage;
	int ret;

	/* Check voltage */
	voltage = p->get_vlt(host->pdev);

	if ((voltage != SH_MOBILE_SDHI_SIGNAL_330V) &&
	(signal_voltage == MMC_SIGNAL_VOLTAGE_330)) {
		/* power cycle */
		/* power off SD bus */
		if (host->set_pwr)
			host->set_pwr(host->pdev, SH_MOBILE_SDHI_POWEROFF);

		usleep_range(1000, 1500);

		/* power up SD bus */
		if (host->set_pwr)
			host->set_pwr(host->pdev, SH_MOBILE_SDHI_POWERON);

		/* Enable 3.3V Signal */
		ret = p->set_vlt(host->pdev, SH_MOBILE_SDHI_SIGNAL_330V);

		usleep_range(5000, 5500);

		return ret;
	} else if ((voltage != SH_MOBILE_SDHI_SIGNAL_180V) &&
	(signal_voltage == MMC_SIGNAL_VOLTAGE_180)) {
		/* Stop SDCLK */
		reg = 0x0100;
		if (sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL) & 0x0200)
			reg |= 0x0200;

		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~reg &
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

		/* check to see if DAT[3:0] is 0000b  */
		if (sd_ctrl_read16(host, CTL_STATUS2) & 0x0080) {
			dev_warn(&host->pdev->dev, "DAT0 is not LOW\n");
			goto power_cycle;
		}

		/* Enable 1.8V Signal */
		ret = p->set_vlt(host->pdev, SH_MOBILE_SDHI_SIGNAL_180V);
		if (ret)
			goto power_cycle;

		/* Wait for 5ms */
		usleep_range(5000, 5500);

		voltage = p->get_vlt(host->pdev);
		if (voltage == SH_MOBILE_SDHI_SIGNAL_180V) {
			/* Start SDCLK */
			sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
				sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

			/* Wait for 1ms */
			usleep_range(1000, 1500);

			/* check to see if DAT[3:0] is 1111b  */
			if (!(sd_ctrl_read16(host, CTL_STATUS2) & 0x0080)) {
				dev_warn(&host->pdev->dev,
					"DAT0 is not HIGH\n");
				goto power_cycle;
			}

			if (reg & 0x0200) {
				sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL,
					0x0200 | sd_ctrl_read16(host,
							CTL_SD_CARD_CLK_CTL));
			}
			return 0;
		}
	} else
		/* No signal voltage switch required */
		return 0;

power_cycle:
	/* power off SD bus */
	if (host->set_pwr)
		host->set_pwr(host->pdev, SH_MOBILE_SDHI_POWEROFF);

	usleep_range(1000, 1500);

	/* power up SD bus */
	if (host->set_pwr)
		host->set_pwr(host->pdev, SH_MOBILE_SDHI_POWERON);

	return -EAGAIN;
}

static bool sh_mobile_sdhi_inquiry_tuning(struct tmio_mmc_host *host)
{
	/* SDHI should be tuning only SDR104 */
	if (host->mmc->ios.timing == MMC_TIMING_UHS_SDR104)
		return true;
	else
		return false;
}

/* SCC registers */
#define SH_MOBILE_SDHI_SCC_DTCNTL	0x300
#define SH_MOBILE_SDHI_SCC_TAPSET	0x304
#define SH_MOBILE_SDHI_SCC_CKSEL	0x30C
#define SH_MOBILE_SDHI_SCC_RVSCNTL	0x310
#define SH_MOBILE_SDHI_SCC_RVSREQ	0x314

/* Definitions for values the SH_MOBILE_SDHI_SCC_DTCNTL register */
#define SH_MOBILE_SDHI_SCC_DTCNTL_TAPEN		(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_CKSEL register */
#define SH_MOBILE_SDHI_SCC_CKSEL_DTSEL		(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_RVSCNTL register */
#define SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN	(1 << 0)
/* Definitions for values the SH_MOBILE_SDHI_SCC_RVSREQ register */
#define SH_MOBILE_SDHI_SCC_RVSREQ_RVSERR	(1 << 2)

static void sh_mobile_sdhi_init_tuning(struct tmio_mmc_host *host,
							unsigned long *num)
{
	/* Initialize SCC */
	sd_ctrl_write32(host, CTL_STATUS, 0x00000000);

	writel(SH_MOBILE_SDHI_SCC_DTCNTL_TAPEN |
		readl(host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL),
		host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	writel(SH_MOBILE_SDHI_SCC_CKSEL_DTSEL |
		readl(host->ctl + SH_MOBILE_SDHI_SCC_CKSEL),
		host->ctl + SH_MOBILE_SDHI_SCC_CKSEL);

	sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
		sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

	writel(~SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &
		readl(host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL),
		host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL);

	/* Read TAPNUM */
	*num = (readl(host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL) >> 16) & 0xf;

	return;
}

static int sh_mobile_sdhi_prepare_tuning(struct tmio_mmc_host *host,
							unsigned long tap)
{
	/* Set sampling clock position */
	writel(tap, host->ctl + SH_MOBILE_SDHI_SCC_TAPSET);

	return 0;
}

#define SH_MOBILE_SDHI_MAX_TAP	3
static int sh_mobile_sdhi_select_tuning(struct tmio_mmc_host *host,
							unsigned long *tap)
{
	unsigned long i, tap_num, tap_cnt, tap_set;
	unsigned long tap_start = 0, tap_end = 0;

	/* Clear SCC_RVSREQ */
	writel(0x00000000, host->ctl + SH_MOBILE_SDHI_SCC_RVSREQ);

	/* Select SCC */
	tap_num = (readl(host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL) >> 16) & 0xf;

	tap_cnt = 0;
	for (i = 0; i < tap_num * 2; i++) {
		if (tap[i] == 0) {
			if (tap_cnt == 0)
				tap_start = i;
			tap_cnt++;
			if (tap_cnt == tap_num) {
				tap_start = 0;
				tap_end = i;
				break;
			}
		} else {
			if (tap_cnt >= SH_MOBILE_SDHI_MAX_TAP) {
				tap_end = i;
				break;
			}
			tap_cnt = 0;
		}
	}

	if (tap_cnt >= SH_MOBILE_SDHI_MAX_TAP)
		tap_set = (tap_start + tap_end) / 2;
	else
		return -EIO;

	/* Set SCC */
	writel(tap_set, host->ctl + SH_MOBILE_SDHI_SCC_TAPSET);

	/* Enable auto re-tuning */
	writel(SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN |
		readl(host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL),
		host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL);
	return 0;
}

static bool sh_mobile_sdhi_retuning(struct tmio_mmc_host *host)
{
	/* Check SCC error */
	if (readl(host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL) &
	    SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &&
	    readl(host->ctl + SH_MOBILE_SDHI_SCC_RVSREQ) &
	    SH_MOBILE_SDHI_SCC_RVSREQ_RVSERR) {
		/* Clear SCC error */
		writel(0x00000000,
			host->ctl + SH_MOBILE_SDHI_SCC_RVSREQ);
		return true;
	}
	return false;
}

static int sh_mobile_sdhi_wait_idle(struct tmio_mmc_host *host)
{
	int timeout = 1000;

	while (--timeout && !(sd_ctrl_read16(host, CTL_STATUS2) & (1 << 13)))
		udelay(1);

	if (!timeout) {
		dev_warn(host->pdata->dev, "timeout waiting for SD bus idle\n");
		return -EBUSY;
	}

	return 0;
}

static int sh_mobile_sdhi_write16_hook(struct tmio_mmc_host *host, int addr)
{
	switch (addr)
	{
	case CTL_SD_CMD:
	case CTL_STOP_INTERNAL_ACTION:
	case CTL_XFER_BLK_COUNT:
	case CTL_SD_CARD_CLK_CTL:
	case CTL_SD_XFER_LEN:
	case CTL_SD_MEM_CARD_OPT:
	case CTL_TRANSACTION_CTL:
	case CTL_DMA_ENABLE:
	case EXT_ACC:
		return sh_mobile_sdhi_wait_idle(host);
	}

	return 0;
}

#define SH_MOBILE_SDHI_DISABLE_AUTO_CMD12	0x4000

static void sh_mobile_sdhi_disable_auto_cmd12(int *val)
{
	*val |= SH_MOBILE_SDHI_DISABLE_AUTO_CMD12;

	return ;
}

static void sh_mobile_sdhi_cd_wakeup(const struct platform_device *pdev)
{
	mmc_detect_change(dev_get_drvdata(&pdev->dev), msecs_to_jiffies(100));
}

static void sh_mobile_sdhi_enable_sdbuf_acc32(struct tmio_mmc_host *host,
								int enable)
{
	unsigned short acc_size;
	unsigned int type;
	u16 ver;

	ver = sd_ctrl_read16(host, CTL_VERSION);
	if (ver == SDHI_VERSION_CB0D)
		type = SH_MOBILE_SDHI_VER_CB0D;
	else if (ver == SDHI_VERSION_490C)
		type = SH_MOBILE_SDHI_VER_490C;
	else {
		dev_err(host->pdata->dev, "Unknown SDHI version\n");
		return;
	}

	acc_size = enable ?
		sh_acc_size[type][SH_MOBILE_SDHI_EXT_ACC_32BIT] :
		sh_acc_size[type][SH_MOBILE_SDHI_EXT_ACC_16BIT];
	sd_ctrl_write16(host, EXT_ACC, acc_size);

}

static int sh_mobile_sdhi_get_xmit_size(unsigned int type, int shift)
{
	int dma_size;

	if (type >= SH_MOBILE_SDHI_VER_MAX)
		return -EINVAL;
	if (shift >= SH_MOBILE_SDHI_DMA_XMIT_SZ_MAX)
		return -EINVAL;

	dma_size = sh_dma_size[type][shift];
	return dma_size;
}

static const struct sh_mobile_sdhi_ops sdhi_ops = {
	.cd_wakeup = sh_mobile_sdhi_cd_wakeup,
};

static void sh_mobile_sdhi_hw_reset(struct tmio_mmc_host *host)
{
	struct tmio_mmc_data *pdata = host->pdata;

	if (pdata->flags & TMIO_MMC_HAS_UHS_SCC) {
		/* Reset SCC */
		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, ~0x0100 &
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

		writel(~SH_MOBILE_SDHI_SCC_CKSEL_DTSEL &
			readl(host->ctl + SH_MOBILE_SDHI_SCC_CKSEL),
			host->ctl + SH_MOBILE_SDHI_SCC_CKSEL);

		sd_ctrl_write16(host, CTL_SD_CARD_CLK_CTL, 0x0100 |
			sd_ctrl_read16(host, CTL_SD_CARD_CLK_CTL));

		writel(~SH_MOBILE_SDHI_SCC_DTCNTL_TAPEN &
			readl(host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL),
			host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL);

		writel(~SH_MOBILE_SDHI_SCC_RVSCNTL_RVSEN &
			readl(host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL),
			host->ctl + SH_MOBILE_SDHI_SCC_RVSCNTL);
	}
}

static int __devinit sh_mobile_sdhi_probe(struct platform_device *pdev)
{
	struct sh_mobile_sdhi *priv;
	struct tmio_mmc_data *mmc_data;
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;
	struct tmio_mmc_host *host;
	char clk_name[8];
	int irq, ret, i = 0;
	bool multiplexed_isr = true;
	u16 ver;
	unsigned int type;
	int base, dma_size;
	int shift = 1; /* 2byte alignment */

	priv = kzalloc(sizeof(struct sh_mobile_sdhi), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "kzalloc failed\n");
		return -ENOMEM;
	}

	mmc_data = &priv->mmc_data;

	if (p) {
		p->pdata = mmc_data;
		if (p->init) {
			ret = p->init(pdev, &sdhi_ops);
			if (ret)
				goto einit;
		}
	}

	snprintf(clk_name, sizeof(clk_name), "sdhi%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(priv->clk);
		goto eclkget;
	}

	mmc_data->clk_enable = sh_mobile_sdhi_clk_enable;
	mmc_data->clk_disable = sh_mobile_sdhi_clk_disable;
	mmc_data->capabilities = MMC_CAP_MMC_HIGHSPEED;
	mmc_data->inquiry_tuning = sh_mobile_sdhi_inquiry_tuning;
	mmc_data->init_tuning = sh_mobile_sdhi_init_tuning;
	mmc_data->prepare_tuning = sh_mobile_sdhi_prepare_tuning;
	mmc_data->select_tuning = sh_mobile_sdhi_select_tuning;
	mmc_data->retuning = sh_mobile_sdhi_retuning;
	mmc_data->disable_auto_cmd12 = sh_mobile_sdhi_disable_auto_cmd12;
	mmc_data->set_clk_div = sh_mobile_sdhi_set_clk_div;
	mmc_data->hw_reset = sh_mobile_sdhi_hw_reset;
	if (p) {
		mmc_data->flags = p->tmio_flags;
		if (mmc_data->flags & TMIO_MMC_HAS_IDLE_WAIT)
			mmc_data->write16_hook = sh_mobile_sdhi_write16_hook;
		mmc_data->ocr_mask = p->tmio_ocr_mask;
		mmc_data->capabilities |= p->tmio_caps;
		mmc_data->capabilities2 |= p->tmio_caps2;
		mmc_data->cd_gpio = p->cd_gpio;
		if (p->set_pwr)
			mmc_data->set_pwr = sh_mobile_sdhi_set_pwr;
		mmc_data->get_cd = sh_mobile_sdhi_get_cd;
		if (p->get_ro)
			mmc_data->get_ro = sh_mobile_sdhi_get_ro;
		if ((p->set_vlt) && (p->get_vlt))
			mmc_data->start_signal_voltage_switch =
				sh_mobile_sdhi_start_signal_voltage_switch;

		if (p->dma_slave_tx > 0 && p->dma_slave_rx > 0) {
			priv->param_tx.shdma_slave.slave_id = p->dma_slave_tx;
			priv->param_rx.shdma_slave.slave_id = p->dma_slave_rx;
			priv->dma_priv.chan_priv_tx = &priv->param_tx.shdma_slave;
			priv->dma_priv.chan_priv_rx = &priv->param_rx.shdma_slave;
			if (p->dma_xmit_sz) {
				base = p->dma_xmit_sz;
				for (shift = 0; base > 1; shift++)
					base >>= 1;
			}
			priv->dma_priv.alignment_shift = shift;

			mmc_data->enable_sdbuf_acc32 =
				sh_mobile_sdhi_enable_sdbuf_acc32;
			mmc_data->dma = &priv->dma_priv;
		}
	}

	/*
	 * All SDHI blocks support 2-byte and larger block sizes in 4-bit
	 * bus width mode.
	 */
	mmc_data->flags |= TMIO_MMC_BLKSZ_2BYTES;

	/*
	 * All SDHI blocks support SDIO IRQ signalling.
	 */
	mmc_data->flags |= TMIO_MMC_SDIO_IRQ;

	ret = tmio_mmc_host_probe(&host, pdev, mmc_data);
	if (ret < 0)
		goto eprobe;

	/*
	 * FIXME:
	 * this Workaround can be more clever method
	 */
	sh_mobile_sdhi_enable_sdbuf_acc32(host, false);

	/* Set DMA xmit size */
	if (p->dma_slave_tx > 0 && p->dma_slave_rx > 0) {
		ver = sd_ctrl_read16(host, CTL_VERSION);
		if (ver == SDHI_VERSION_CB0D)
			type = SH_MOBILE_SDHI_VER_CB0D;
		else
			type = SH_MOBILE_SDHI_VER_490C;

		dma_size = sh_mobile_sdhi_get_xmit_size(type,
					priv->dma_priv.alignment_shift);
		if (dma_size < 0) {
			ret = dma_size;
			goto eirq_card_detect;
		}
		sd_ctrl_write16(host, SD_DMACR(type), dma_size);
	}

	/* set sampling clock selection range */
	if (p->scc_tapnum)
		writel(p->scc_tapnum << 16,
			host->ctl + SH_MOBILE_SDHI_SCC_DTCNTL);

	/*
	 * Allow one or more specific (named) ISRs or
	 * one or more multiplexed (un-named) ISRs.
	 */

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_CARD_DETECT);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = request_irq(irq, tmio_mmc_card_detect_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq_card_detect;
	}

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_SDIO);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = request_irq(irq, tmio_mmc_sdio_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq_sdio;
	}

	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_SDCARD);
	if (irq >= 0) {
		multiplexed_isr = false;
		ret = request_irq(irq, tmio_mmc_sdcard_irq, 0,
				  dev_name(&pdev->dev), host);
		if (ret)
			goto eirq_sdcard;
	} else if (!multiplexed_isr) {
		dev_err(&pdev->dev,
			"Principal SD-card IRQ is missing among named interrupts\n");
		ret = irq;
		goto eirq_sdcard;
	}

	if (multiplexed_isr) {
		while (1) {
			irq = platform_get_irq(pdev, i);
			if (irq < 0)
				break;
			i++;
			ret = request_irq(irq, tmio_mmc_irq, 0,
					  dev_name(&pdev->dev), host);
			if (ret)
				goto eirq_multiplexed;
		}

		/* There must be at least one IRQ source */
		if (!i)
			goto eirq_multiplexed;
	}

	dev_info(&pdev->dev, "%s base at 0x%08lx clock rate %u MHz\n",
		 mmc_hostname(host->mmc), (unsigned long)
		 (platform_get_resource(pdev, IORESOURCE_MEM, 0)->start),
		 mmc_data->hclk / 1000000);

	return ret;

eirq_multiplexed:
	while (i--) {
		irq = platform_get_irq(pdev, i);
		free_irq(irq, host);
	}
eirq_sdcard:
	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_SDIO);
	if (irq >= 0)
		free_irq(irq, host);
eirq_sdio:
	irq = platform_get_irq_byname(pdev, SH_MOBILE_SDHI_IRQ_CARD_DETECT);
	if (irq >= 0)
		free_irq(irq, host);
eirq_card_detect:
	tmio_mmc_host_remove(host);
eprobe:
	clk_put(priv->clk);
eclkget:
	if (p && p->cleanup)
		p->cleanup(pdev);
einit:
	kfree(priv);
	return ret;
}

static int sh_mobile_sdhi_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct tmio_mmc_host *host = mmc_priv(mmc);
	struct sh_mobile_sdhi *priv = container_of(host->pdata, struct sh_mobile_sdhi, mmc_data);
	struct sh_mobile_sdhi_info *p = pdev->dev.platform_data;
	int i = 0, irq;

	if (p)
		p->pdata = NULL;

	tmio_mmc_host_remove(host);

	while (1) {
		irq = platform_get_irq(pdev, i++);
		if (irq < 0)
			break;
		free_irq(irq, host);
	}

	clk_put(priv->clk);

	if (p && p->cleanup)
		p->cleanup(pdev);

	kfree(priv);

	return 0;
}

static const struct dev_pm_ops tmio_mmc_dev_pm_ops = {
	.suspend = tmio_mmc_host_suspend,
	.resume = tmio_mmc_host_resume,
	.runtime_suspend = tmio_mmc_host_runtime_suspend,
	.runtime_resume = tmio_mmc_host_runtime_resume,
};

static const struct of_device_id sh_mobile_sdhi_of_match[] = {
	{ .compatible = "renesas,shmobile-sdhi" },
	{ }
};
MODULE_DEVICE_TABLE(of, sh_mobile_sdhi_of_match);

static struct platform_driver sh_mobile_sdhi_driver = {
	.driver		= {
		.name	= "sh_mobile_sdhi",
		.owner	= THIS_MODULE,
		.pm	= &tmio_mmc_dev_pm_ops,
		.of_match_table = sh_mobile_sdhi_of_match,
	},
	.probe		= sh_mobile_sdhi_probe,
	.remove		= __devexit_p(sh_mobile_sdhi_remove),
};

module_platform_driver(sh_mobile_sdhi_driver);

MODULE_DESCRIPTION("SuperH Mobile SDHI driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sh_mobile_sdhi");

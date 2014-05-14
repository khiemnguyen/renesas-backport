/*
 * sound/soc/sh/scu_dai.c
 *     This file is ALSA SoC driver for SCU peripheral.
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * This file is based on the sound/soc/sh/siu_dai.c
 *
 * siu_dai.c - ALSA SoC driver for Renesas SH7343, SH7722 SIU peripheral.
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2006 Carlos Munoz <carlos@kenati.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/clk.h>

#include <sound/control.h>
#include <sound/sh_scu.h>

#undef DEBUG
#ifdef DEBUG
#define FNC_ENTRY	pr_info("entry:%s:%d\n", __func__, __LINE__);
#define FNC_EXIT	pr_info("exit:%s:%d\n", __func__, __LINE__);
#define DBG_POINT()	pr_info("check:%s:%d\n", __func__, __LINE__);
#define DBG_MSG(args...)	pr_info(args)
#else  /* DEBUG */
#define FNC_ENTRY
#define FNC_EXIT
#define DBG_POINT()
#define DBG_MSG(args...)
#endif /* DEBUG */

static struct scu_reg_info *rinfo;
static spinlock_t *sculock;
static struct scu_audio_info *ainfo;
static struct scu_platform_data *pdata;

struct scu_route_info *scu_get_route_info(void)
{
	return &ainfo->routeinfo;
}
EXPORT_SYMBOL(scu_get_route_info);

struct scu_platform_data *scu_get_platform_data(void)
{
	return pdata;
}
EXPORT_SYMBOL(scu_get_platform_data);

struct scu_irq_info *scu_get_irq_info(void)
{
	return &ainfo->irqinfo;
}
EXPORT_SYMBOL(scu_get_irq_info);

/************************************************************************

	basic function

************************************************************************/
static void scu_or_writel(u32 data, u32 *reg)
{
	u32 val;

	spin_lock(sculock);
	val = readl(reg);
	writel((val | data), reg);
	spin_unlock(sculock);
}

static void scu_and_writel(u32 data, u32 *reg)
{
	u32 val;

	spin_lock(sculock);
	val = readl(reg);
	writel((val & data), reg);
	spin_unlock(sculock);
}

/************************************************************************

	interrupt function

************************************************************************/
/* from SRC */
static irqreturn_t scu_src_interrupt(int irq, void *data)
{
	struct scu_irq_info *irqinfo = data;
	u32 scu_src_sys_status0, scu_src_sys_status1;
	int src_num;
	int ch;
	struct scu_pcm_info *pcminfo;
	int dir;
	int route;
	int src_mode;

	if (irq == irqinfo->src_irq_playback) {
		src_num = irqinfo->src_num_playback;
		ch = dir = CTRL_PLAYBACK;
	} else if (irq == irqinfo->src_irq_capture) {
		src_num = irqinfo->src_num_capture;
		ch = dir = CTRL_CAPTURE;
	} else {
		DBG_MSG("Illegal interrupt! SRC_IRQ = %d\n", irq);
		return IRQ_HANDLED;
	}

	pcminfo = irqinfo->ss[ch]->runtime->private_data;

	if (!dir)	/* playback */
		route = pcminfo->routeinfo->p_route;
	else		/* capture */
		route = pcminfo->routeinfo->c_route;

	src_mode = scu_find_data(route, pcminfo->pdata->src_mode,
					pcminfo->pdata->src_mode_num);

	if (src_mode == SRC_CR_ASYNC) {
		scu_src_sys_status0 = readl(&rinfo->scu_sys_regs->status0);
		if (scu_src_sys_status0 & (SCU_SYS_ST0_OF_SRC_O << src_num))
			DBG_MSG("Overflow!!! at SRC%d async mode\n",
								src_num);
		if (scu_src_sys_status0 & (SCU_SYS_ST0_UF_SRC_I << src_num))
			DBG_MSG("Underflow!!! at SRC%d async mode\n", src_num);
		if (!(scu_src_sys_status0 &
			((SCU_SYS_ST0_OF_SRC_O |
				SCU_SYS_ST0_UF_SRC_I) << src_num))) {
			DBG_MSG("Illegal interrupt! ");
			DBG_MSG(
				"SRC_IRQ = %d, SRC_SYS_STATUS0 = 0x%08x\n",
						irq, scu_src_sys_status0);
			return IRQ_HANDLED;
		}

		snd_pcm_stop(irqinfo->ss[ch], SNDRV_PCM_STATE_XRUN);
	} else {	/* SRC_CR_SYNC */
		scu_src_sys_status1 = readl(&rinfo->scu_sys_regs->status1);
		if (scu_src_sys_status1 & (SCU_SYS_ST1_UF_SRC_O << src_num))
			DBG_MSG("Underflow!!! at SRC%d sync mode\n", src_num);
		if (scu_src_sys_status1 & (SCU_SYS_ST1_OF_SRC_I << src_num))
			DBG_MSG("Overflow!!! at SRC%d sync mode\n", src_num);

		if (!(scu_src_sys_status1 &
			((SCU_SYS_ST1_UF_SRC_O |
				SCU_SYS_ST1_OF_SRC_I) << src_num))) {
			DBG_MSG("Illegal interrupt! ");
			DBG_MSG("SRC_IRQ = %d, SRC_SYS_STATUS1 = 0x%08x\n",
						irq, scu_src_sys_status1);
			return IRQ_HANDLED;
		}

		snd_pcm_stop(irqinfo->ss[ch], SNDRV_PCM_STATE_XRUN);
	}

	return IRQ_HANDLED;
}

/* from SSI */
static irqreturn_t scu_ssi_interrupt(int irq, void *data)
{
	struct scu_irq_info *irqinfo = data;
	u32 scu_ssi_status;
	int ssi_num;
	int ch;

	if (irq == irqinfo->ssi_irq_playback) {
		ssi_num = irqinfo->ssi_num_playback;
		ch = CTRL_PLAYBACK;
	} else if (irq == irqinfo->ssi_irq_capture) {
		ssi_num = irqinfo->ssi_num_capture;
		ch = CTRL_CAPTURE;
	} else {
		DBG_MSG("Illegal interrupt! SSI_IRQ = %d\n", irq);
		return IRQ_HANDLED;
	}

	scu_ssi_status = readl(&rinfo->ssireg[ssi_num]->sr);

	if (scu_ssi_status & SSISR_OIRQ)
		DBG_MSG("Overflow!!! at SSI%d\n", ssi_num);
	if (scu_ssi_status & SSISR_UIRQ)
		DBG_MSG("Underflow!!! at SSI%d\n", ssi_num);
	if (!(scu_ssi_status & (SSISR_OIRQ | SSISR_UIRQ))) {
		DBG_MSG("Illegal interrupt! ");
		DBG_MSG("SSI_IRQ = %d, SSI_STATUS = 0x%08x\n",
							irq, scu_ssi_status);
		return IRQ_HANDLED;
	}

	snd_pcm_stop(irqinfo->ss[ch], SNDRV_PCM_STATE_XRUN);

	return IRQ_HANDLED;
}

/************************************************************************

	peripheral function

************************************************************************/
void adg_init(void)
{
	FNC_ENTRY

	clk_enable(ainfo->clockinfo.adg_clk);

	/* clock select */
	scu_or_writel(0x23550000,
		(u32 *)(rinfo->adgreg + ADG_SSICKR));

	/* audio clock select */
	scu_or_writel(((ADG_SEL0_SSI1_DIV1 | ADG_SEL0_SSI1_ACLK_DIV |
		ADG_SEL0_SSI1_DIVCLK_CLKA) | (ADG_SEL0_SSI0_DIV1 |
		ADG_SEL0_SSI0_ACLK_DIV | ADG_SEL0_SSI0_DIVCLK_CLKA)),
		(u32 *)(rinfo->adgreg + ADG_AUDIO_CLK_SEL0));

	/* SRC Input Timing */
	scu_or_writel((ADG_SRCIN0_SRC1_DIVCLK_SSI_WS0 |
		ADG_SRCIN0_SRC0_DIVCLK_SSI_WS0),
		(u32 *)(rinfo->adgreg + ADG_SRCIN_TIMSEL0));

	/* SRC Output Timing */
	scu_or_writel((ADG_SRCOUT0_SRC1_DIVCLK_SSI_WS0 |
		ADG_SRCOUT0_SRC0_DIVCLK_SSI_WS0),
		(u32 *)(rinfo->adgreg + ADG_SRCOUT_TIMSEL0));

	/* CMD Output Timing */
	scu_or_writel((ADG_CMDOUT_CMD1_DIVCLK_SSI_WS0 |
		ADG_CMDOUT_CMD0_DIVCLK_SSI_WS0),
		(u32 *)(rinfo->adgreg + ADG_CMDOUT_TIMSEL));

	/* division enable */
	scu_or_writel(ADG_DIVEN_AUDIO_CLKA,
			(u32 *)(rinfo->adgreg + ADG_DIV_EN));

	FNC_EXIT
	return;
}
EXPORT_SYMBOL(adg_init);

void adg_deinit(void)
{
	FNC_ENTRY

	/* omit other initialization */
	clk_disable(ainfo->clockinfo.adg_clk);

	FNC_EXIT
	return;
}
EXPORT_SYMBOL(adg_deinit);

static void scu_ssi_control(int master_ch, int slave_ch, int mode)
{
	FNC_ENTRY
	/* SSI setting */
	if ((readl(&rinfo->ssireg[master_ch]->cr) & SSICR_ENABLE) == 0) {
		writel(SSICR_P4643_ST, &rinfo->ssireg[master_ch]->cr);
		writel(SSIWS_ST, &rinfo->ssireg[master_ch]->wsr);
	}
	if ((mode == SSI_SLAVE) &&
	    ((readl(&rinfo->ssireg[slave_ch]->cr) & SSICR_ENABLE) == 0))
		writel(SSICR_C4643_ST, &rinfo->ssireg[slave_ch]->cr);

	FNC_EXIT
	return;
}

static void scu_ssi_start(int ssi_ch, int ssi_dir)
{
	u32 val;
	int offset = scu_find_data(ssi_ch, pdata->ssiu_control,
					pdata->ssiu_control_num);

	FNC_ENTRY
	if (offset == -1) {
		pr_info("%s ssi channel error\n", __func__);
		return;
	}

	if (ssi_dir == SSI_OUT) {
		/* SSI enable (figure.39.12 flow) */
		val = readl(&rinfo->ssireg[ssi_ch]->cr);
		val |= (SSICR_DMEN | SSICR_UIEN | SSICR_OIEN | SSICR_ENABLE);
		writel(val, &rinfo->ssireg[ssi_ch]->cr);

		/* SSIU start (figure.39.12 flow) */
		scu_or_writel(SSI_CTRL_4CH_START0,
			(u32 *)(rinfo->ssiureg + offset));
	} else { /* ssi_dir == SSI_IN */
		/* SSIU start (figure.39.14 flow) */
		scu_or_writel(SSI_CTRL_4CH_START0,
			(u32 *)(rinfo->ssiureg + offset));

		/* SSI enable (figure.39.14 flow) */
		val = readl(&rinfo->ssireg[ssi_ch]->cr);
		val |= (SSICR_DMEN | SSICR_UIEN | SSICR_OIEN | SSICR_ENABLE);
		writel(val, &rinfo->ssireg[ssi_ch]->cr);
	}

	FNC_EXIT
	return;
}

static void scu_ssi_stop(int ssi_ch, int ssi_dir)
{
	u32 val;
	int tmout;
	int offset = scu_find_data(ssi_ch, pdata->ssiu_control,
					pdata->ssiu_control_num);

	FNC_ENTRY
	if (offset == -1) {
		pr_info("%s ssi channel error\n", __func__);
		return;
	}

	if (ssi_dir == SSI_OUT) {
		/* SSI disable (figure.39.13 flow) */
		val = readl(&rinfo->ssireg[ssi_ch]->cr);
		val &= ~SSICR_DMEN;
		writel(val, &rinfo->ssireg[ssi_ch]->cr);

		tmout = 1000;
		while (--tmout &&
		    !(readl(&rinfo->ssireg[ssi_ch]->sr) & SSISR_DIRQ))
			udelay(1);
		if (!tmout)
			pr_info("timeout waiting for SSI data idle\n");

		val = readl(&rinfo->ssireg[ssi_ch]->cr);
		val &= ~(SSICR_UIEN | SSICR_OIEN | SSICR_ENABLE);
		writel(val, &rinfo->ssireg[ssi_ch]->cr);

		tmout = 1000;
		while (--tmout &&
		    !(readl(&rinfo->ssireg[ssi_ch]->sr) & SSISR_IDST))
			udelay(1);
		if (!tmout)
			pr_info("timeout waiting for SSI idle\n");

		/* SSIU stop (figure.39.13 flow) */
		writel(0, (u32 *)(rinfo->ssiureg + offset));
	} else { /* ssi_dir == SSI_IN */
		/* SSIU stop (figure.39.15 flow) */
		writel(0, (u32 *)(rinfo->ssiureg + offset));

		/* SSI disable (figure.39.15 flow) */
		val = readl(&rinfo->ssireg[ssi_ch]->cr);
		val &= ~(SSICR_DMEN | SSICR_UIEN | SSICR_OIEN | SSICR_ENABLE);
		writel(val, &rinfo->ssireg[ssi_ch]->cr);

		tmout = 1000;
		while (--tmout &&
		    !(readl(&rinfo->ssireg[ssi_ch]->sr) & SSISR_IDST))
			udelay(1);
		if (!tmout)
			pr_info("timeout waiting for SSI idle\n");
	}

	FNC_EXIT
}

static void scu_src_init(int src_ch, unsigned int sync_sw)
{
	u32 val = SRC_MODE_SRCUSE;

	FNC_ENTRY
	/* SCU SRC0_BUSIF_DALIGN */
	writel(SRC_DALIGN_STEREO_R,
		(u32 *)&rinfo->scusrcreg[src_ch]->dalign);

	/* SCU SRC_MODE */
	if (src_ch == 0 && sync_sw == 1)
		val |= SRC_MODE_IN_SYNC;
	else if (src_ch == 1 && sync_sw == 1)
		val |= SRC_MODE_OUT_SYNC;
	writel(val, (u32 *)&rinfo->scusrcreg[src_ch]->mode);
	FNC_EXIT
	return;
}

static void scu_src_deinit(int src_ch)
{
	FNC_ENTRY
	/* SCU SRC_MODE */
	writel(0, (u32 *)&rinfo->scusrcreg[src_ch]->mode);
	FNC_EXIT
	return;
}

static u32 scu_src_calc_bsdsr(u32 ratio)
{
	u32 val;

	/* check FSO/FSI ratio */
	/*  1/4=25, 1/3=33, 1/2=50, 2/3=66, 1/1=100  */
	if (ratio < 25)
		val = SRC_BSD012349_BUFDATA_1_6;
	else if (ratio >= 25 && ratio < 33)
		val = SRC_BSD012349_BUFDATA_1_4;
	else if (ratio >= 33 && ratio < 50)
		val = SRC_BSD012349_BUFDATA_1_3;
	else if (ratio >= 50 && ratio < 66)
		val = SRC_BSD012349_BUFDATA_1_2;
	else if (ratio >= 66 && ratio < 100)
		val = SRC_BSD012349_BUFDATA_2_3;
	else /* ratio >= 100 */
		val = SRC_BSD012349_BUFDATA_1_1;

	return val;
}

static u32 scu_src_calc_bsisr(u32 ratio)
{
	u32 val;

	/* check FSO/FSI ratio */
	/*  1/4=25, 1/3=33, 1/2=50, 2/3=66, 1/1=100  */
	if (ratio < 25)
		val = SRC_BSI_IJECSIZE_1_6;
	else if (ratio >= 25 && ratio < 33)
		val = SRC_BSI_IJECSIZE_1_4;
	else if (ratio >= 33 && ratio < 50)
		val = SRC_BSI_IJECSIZE_1_3;
	else if (ratio >= 50 && ratio < 66)
		val = SRC_BSI_IJECSIZE_1_2;
	else if (ratio >= 66 && ratio < 100)
		val = SRC_BSI_IJECSIZE_2_3;
	else /* ratio >= 100 */
		val = SRC_BSI_IJECSIZE_1_1;

	/* IJECPREC */
	val |= SRC_BSI_IJECPREC;

	return val;
}

static void scu_src_control(int src_ch, unsigned int rate, unsigned int sync_sw)
{
	u64 val = 0;
	u32 reg;

	FNC_ENTRY
	/* SRC Activation (SRC_SWRSR) Figure42.7 */
	writel(0, (u32 *)&rinfo->srcreg[src_ch]->swrsr);
	writel(1, (u32 *)&rinfo->srcreg[src_ch]->swrsr);

	/* SRC_SRCIR */
	writel(1, (u32 *)&rinfo->srcreg[src_ch]->srcir);

	/* SRC_ADINR *//* only stereo now */
	writel((SRCADIN_OTBL_16BIT | SRCADIN_CHNUM_2),
		(u32 *)&rinfo->srcreg[src_ch]->adinr);

	/* SRC_IFSCR INTIFS enable */
	writel(1, (u32 *)&rinfo->srcreg[src_ch]->ifscr);

	/* SRC_IFSVR INTIFS calculation */
	if (ainfo->rate[src_ch])
		val = div_u64(SRC_IFS_FSO * rate, ainfo->rate[src_ch]);
	else /* not convert */
		val = SRC_IFS_FSO;
	writel((u32)val, (u32 *)&rinfo->srcreg[src_ch]->ifsvr);

	/* SRC_SRCCR */
	writel((SRC_CR_BIT16 | SRC_CR_BIT12 | SRC_CR_BIT8 | SRC_CR_BIT4 |
		sync_sw), (u32 *)&rinfo->srcreg[src_ch]->srccr);

	/* SRC_MNFSR MINFS calculation */
	val = div_u64(val * 98, 100);	/* 98% */
	writel(val, (u32 *)&rinfo->srcreg[src_ch]->mnfsr);

	/* FSO/FSI(*100) */
	if (ainfo->rate[src_ch])
		val = (ainfo->rate[src_ch] * 100) / rate;
	else /* not convert */
		val = 100;

	/* SRC_BSDSR (FSO/FSI Ratio is 6-1/6) */
	reg = scu_src_calc_bsdsr((u32)val);
	writel(reg, (u32 *)&rinfo->srcreg[src_ch]->bsdsr);

	/* SRC_BSISR (FSO/FSI Ratio is 6-1/6) */
	reg = scu_src_calc_bsisr((u32)val);
	writel(reg, (u32 *)&rinfo->srcreg[src_ch]->bsisr);

	/* SRC_SRCIR */
	writel(0, (u32 *)&rinfo->srcreg[src_ch]->srcir);

	FNC_EXIT
	return;
}

static void scu_src_start(int src_ch, int src_dir)
{
	FNC_ENTRY
	/* SRC_CONTROL */
	if (src_dir == SRC_INOUT)
		scu_or_writel((SRC_MODE_START_IN | SRC_MODE_START_OUT),
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	else if (src_dir == SRC_OUT)
		scu_or_writel(SRC_MODE_START_OUT,
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	else /* SRC_IN */
		scu_or_writel(SRC_MODE_START_IN,
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	FNC_EXIT
	return;
}

static void scu_src_stop(int src_ch, int src_dir)
{
	FNC_ENTRY
	/* SRC_CONTROL */
	if (src_dir == SRC_INOUT)
		scu_and_writel(~(SRC_MODE_START_IN | SRC_MODE_START_OUT),
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	if (src_dir == SRC_OUT)
		scu_and_writel(~SRC_MODE_START_OUT,
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	else /* SRC_IN */
		scu_and_writel(~SRC_MODE_START_IN,
			(u32 *)&rinfo->scusrcreg[src_ch]->control);
	FNC_EXIT
	return;
}

static void scu_dvc_init(int dvc_ch)
{
	int dvcrs = scu_find_data(dvc_ch, pdata->dvc_route_select,
					pdata->dvc_route_select_num);

	FNC_ENTRY
	if (dvcrs == -1) {
		pr_info("%s dvc channel error\n", __func__);
		return;
	}

	/* SCU CMD_ROUTE_SELECT */
	writel(dvcrs, (u32 *)&rinfo->scucmdreg[dvc_ch]->route_select);

	/* SCU CMD_CONTROL */
	writel(CMD_CONTROL_START_OUT,
		(u32 *)&rinfo->scucmdreg[dvc_ch]->control);

	FNC_EXIT
	return;
}

static void scu_dvc_deinit(int dvc_ch)
{
	FNC_ENTRY
	/* SCU CMD_CONTROL */
	writel(0, (u32 *)&rinfo->scucmdreg[dvc_ch]->control);

	/* SCU CMD_ROUTE_SELECT */
	writel(0, (u32 *)&rinfo->scucmdreg[dvc_ch]->route_select);
	FNC_EXIT
	return;
}

void scu_dvc_control(int dvc_ch)
{
	u32 mute;

	FNC_ENTRY
	/* DVC Activation (DVC_SWRSR) Figure42.27 */
	writel(0, (u32 *)&rinfo->dvcreg[dvc_ch]->swrsr);
	writel(1, (u32 *)&rinfo->dvcreg[dvc_ch]->swrsr);

	/* DVC_DVUIR */
	writel(1, (u32 *)&rinfo->dvcreg[dvc_ch]->dvuir);

	/* General Information */
	/*  DVC_ADINR  *//* only stereo now */
	writel((DVCADIN_OTBL_16BIT | DVCADIN_CHNUM_2),
		(u32 *)&rinfo->dvcreg[dvc_ch]->adinr);

	/*  DVC_DVUCR  */
	writel((DVCDVUC_VVMD_USE | DVCDVUC_ZCMD_USE),
		(u32 *)&rinfo->dvcreg[dvc_ch]->dvucr);

	/* Digital Volume Function Parameter */
	/*  DVC_VOL*R  */
	writel(ainfo->volume[dvc_ch][0], (u32 *)&rinfo->dvcreg[dvc_ch]->vol0r);
	writel(ainfo->volume[dvc_ch][1], (u32 *)&rinfo->dvcreg[dvc_ch]->vol1r);

	/* Zero Cross Mute Function */
	mute = (ainfo->mute[1] << 1) + ainfo->mute[0];
	mute = ~mute & 0x3;
	writel(mute, (u32 *)&rinfo->dvcreg[dvc_ch]->zcmcr);

	/* DVC_DVUIR */
	writel(0, (u32 *)&rinfo->dvcreg[dvc_ch]->dvuir);

	FNC_EXIT
	return;
}

static void scu_dvc_start(int dvc_ch)
{
	FNC_ENTRY
	/* DVC_DVUER */
	writel(1, (u32 *)&rinfo->dvcreg[dvc_ch]->dvuer);
	FNC_EXIT
	return;
}

static void scu_dvc_stop(int dvc_ch)
{
	FNC_ENTRY
	/* DVC_DVUER */
	writel(0, (u32 *)&rinfo->dvcreg[dvc_ch]->dvuer);
	FNC_EXIT
	return;
}

/************************************************************************

	DAPM callback function

************************************************************************/
void scu_init_ssi(int master_ch, int slave_ch, int mode, int ind, int dir)
{
	int ch = (mode == SSI_MASTER) ? master_ch : slave_ch;
	int offset = scu_find_data(ch, pdata->ssiu_busif_adinr,
					pdata->ssiu_busif_adinr_num);
	int mode1 = scu_find_data(ch, pdata->ssiu_mode1,
					pdata->ssiu_mode1_num);

	if (offset == -1) {
		pr_info("%s ssi master channel error\n", __func__);
		return;
	}

	if ((mode == SSI_SLAVE) && (offset == -1 || mode1 == -1)) {
		pr_info("%s ssi slave channel error\n", __func__);
		return;
	}

	clk_enable(ainfo->clockinfo.ssiu_clk);
	clk_enable(ainfo->clockinfo.ssi_clk[master_ch]);
	if (mode == SSI_SLAVE)
		clk_enable(ainfo->clockinfo.ssi_clk[slave_ch]);

	/* SSI_BUSIF_ADINR */
	scu_or_writel((SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
		(u32 *)(rinfo->ssiureg + offset));

	/* SSI_MODE1 */
	if (mode == SSI_SLAVE)
		scu_or_writel(mode1, (u32 *)(rinfo->ssiureg + SSI_MODE1));

	/* SSI_MODE0 (SSI independant) */
	if (ind == SSI_INDEPENDANT)
		scu_or_writel((1 << ch),
			(u32 *)(rinfo->ssiureg + SSI_MODE0));

	/* clear interrupt (SSISR_OIRQ, SSISR_UIRQ) */
	writel(0, &rinfo->ssireg[ch]->sr);

	/* SSI init */
	scu_ssi_control(master_ch, slave_ch, mode);

	/* SSI start */
	scu_ssi_start(ch, dir);

	/* enable interrupt by SSI_INT_ENABLE_MAIN */
	scu_or_writel((SSI_INT_EN_UIRQ | SSI_INT_EN_OIRQ),
			&rinfo->ssiu_std_reg[ch]->int_enable_main);
}
EXPORT_SYMBOL(scu_init_ssi);

void scu_deinit_ssi(int master_ch, int slave_ch, int mode, int ind, int dir)
{
	int ch = ((mode == SSI_MASTER) ? master_ch : slave_ch);
	int offset = scu_find_data(ch, pdata->ssiu_busif_adinr,
					pdata->ssiu_busif_adinr_num);
	int mode1 = scu_find_data(ch, pdata->ssiu_mode1,
					pdata->ssiu_mode1_num);

	if (offset == -1) {
		pr_info("%s ssi channel error\n", __func__);
		return;
	}

	if ((mode == SSI_SLAVE) && (mode1 == -1)) {
		pr_info("%s ssi slave channel error\n", __func__);
		return;
	}

	/* disable interrupt by SSI_INT_ENABLE_MAIN */
	scu_or_writel(~(SSI_INT_EN_UIRQ | SSI_INT_EN_OIRQ),
			&rinfo->ssiu_std_reg[ch]->int_enable_main);

	/* SSI stop */
	scu_ssi_stop(ch, dir);

	/* clear interrupt (SSISR_OIRQ, SSISR_UIRQ) */
	writel(0, &rinfo->ssireg[ch]->sr);

	/* SSI_MODE0 (SSI independant) */
	if (ind == SSI_INDEPENDANT)
		scu_and_writel(~(1 << ch),
			(u32 *)(rinfo->ssiureg + SSI_MODE0));

	/* SSI_MODE1 */
	if (mode == SSI_SLAVE)
		scu_and_writel(~mode1, (u32 *)(rinfo->ssiureg + SSI_MODE1));

	/* SSI_BUSIF_ADINR */
	scu_and_writel(~(SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
		(u32 *)(rinfo->ssiureg + offset));

	if (mode == SSI_SLAVE)
		clk_disable(ainfo->clockinfo.ssi_clk[slave_ch]);
	clk_disable(ainfo->clockinfo.ssi_clk[master_ch]);
	clk_disable(ainfo->clockinfo.ssiu_clk);
}
EXPORT_SYMBOL(scu_deinit_ssi);

void scu_init_src(int src_ch, unsigned int rate, unsigned int sync_sw)
{
	clk_enable(ainfo->clockinfo.scu_clk);
	clk_enable(ainfo->clockinfo.src_clk[src_ch]);

	/* clear interrupt status */
	writel((SCU_SYS_ST0_OF_SRC_O | SCU_SYS_ST0_UF_SRC_I) << src_ch,
						&rinfo->scu_sys_regs->status0);
	writel((SCU_SYS_ST1_UF_SRC_O | SCU_SYS_ST1_OF_SRC_I) << src_ch,
						&rinfo->scu_sys_regs->status1);

	scu_src_init(src_ch, sync_sw);
	scu_src_control(src_ch, rate, sync_sw);
	/* start src */
	scu_src_start(src_ch, SRC_INOUT);

	/* enable interrupt */
	if (sync_sw) {
		scu_or_writel(SRC_INT_EN0_UF_SRCO_IE | SRC_INT_EN0_OF_SRCI_IE,
			(u32 *)&rinfo->scusrcreg[src_ch]->int_enable0);

		scu_or_writel((SCU_SYS_INTEN1_UF_SRC_O_IE |
				SCU_SYS_INTEN1_OF_SRC_I_IE) << src_ch,
				(u32 *)&rinfo->scu_sys_regs->interrupt1);
	} else {
		scu_or_writel(SRC_INT_EN0_OF_SRCO_IE | SRC_INT_EN0_UF_SRCI_IE,
			(u32 *)&rinfo->scusrcreg[src_ch]->int_enable0);

		scu_or_writel((SCU_SYS_INTEN0_OF_SRC_O_IE |
				SCU_SYS_INTEN0_UF_SRC_I_IE) << src_ch,
				(u32 *)&rinfo->scu_sys_regs->interrupt0);
	}
}
EXPORT_SYMBOL(scu_init_src);

void scu_deinit_src(int src_ch, unsigned int sync_sw)
{
	/* disable interrupt */
	if (sync_sw) {

		/* keep ie=1 of SRC_INT_ENABLE0 for other SRC */

		/* set ie=0 of SCU_SYSTEM_STATUS1 for self SRC */
		scu_and_writel(~((SCU_SYS_INTEN1_UF_SRC_O_IE |
					SCU_SYS_INTEN1_OF_SRC_I_IE) << src_ch),
				(u32 *)&rinfo->scu_sys_regs->interrupt1);
	} else {

		/* keep ie=1 of SRC_INT_ENABLE0 for other SRC */

		/* set ie=0 of SCU_SYSTEM_STATUS0 for self SRC */
		scu_and_writel(~((SCU_SYS_INTEN0_OF_SRC_O_IE |
					SCU_SYS_INTEN0_UF_SRC_I_IE) << src_ch),
				(u32 *)&rinfo->scu_sys_regs->interrupt0);
	}

	/* stop src */
	scu_src_stop(src_ch, SRC_INOUT);
	scu_src_deinit(src_ch);

	/* clear interrupt status */
	if (sync_sw) {
		writel((SCU_SYS_ST0_OF_SRC_O | SCU_SYS_ST0_UF_SRC_I) << src_ch,
						&rinfo->scu_sys_regs->status0);
	} else {
		writel((SCU_SYS_ST1_UF_SRC_O | SCU_SYS_ST1_OF_SRC_I) << src_ch,
						&rinfo->scu_sys_regs->status1);
	}

	clk_disable(ainfo->clockinfo.src_clk[src_ch]);
	clk_disable(ainfo->clockinfo.scu_clk);
}
EXPORT_SYMBOL(scu_deinit_src);

void scu_init_dvc(int dvc_ch)
{
	clk_enable(ainfo->clockinfo.dvc_clk[dvc_ch]);

	scu_dvc_init(dvc_ch);
	scu_dvc_control(dvc_ch);
	/* start dvc */
	scu_dvc_start(dvc_ch);
}
EXPORT_SYMBOL(scu_init_dvc);

void scu_deinit_dvc(int dvc_ch)
{
	/* stop dvc */
	scu_dvc_stop(dvc_ch);
	scu_dvc_deinit(dvc_ch);

	clk_disable(ainfo->clockinfo.dvc_clk[dvc_ch]);
}
EXPORT_SYMBOL(scu_deinit_dvc);

/************************************************************************

	dai ops

************************************************************************/
/* Playback and capture hardware properties are identical */
static struct snd_pcm_hardware scu_dai_pcm_hw = {
	.info			= (SNDRV_PCM_INFO_INTERLEAVED	|
				   SNDRV_PCM_INFO_MMAP		|
				   SNDRV_PCM_INFO_MMAP_VALID	|
				   SNDRV_PCM_INFO_PAUSE),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 8000,
	.rate_max		= 48000,
	.channels_min		= 2,
	.channels_max		= 2,
	.buffer_bytes_max	= SCU_BUFFER_BYTES_MAX,
	.period_bytes_min	= SCU_PERIOD_BYTES_MIN,
	.period_bytes_max	= SCU_PERIOD_BYTES_MAX,
	.periods_min		= SCU_PERIODS_MIN,
	.periods_max		= SCU_PERIODS_MAX,
};

static int scu_dai_info_rate(struct snd_kcontrol *kctrl,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = RATE_MAX;

	return 0;
}

static int scu_dai_get_rate(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);

	switch (kctrl->private_value) {
	case CTRL_PLAYBACK:
		ucontrol->value.integer.value[0] = ainfo->rate[0];
		break;
	case CTRL_CAPTURE:
		ucontrol->value.integer.value[0] = ainfo->rate[1];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int scu_dai_put_rate(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);
	int change = 0;

	switch (kctrl->private_value) {
	case CTRL_PLAYBACK:
		change |= (ainfo->rate[0] != ucontrol->value.integer.value[0]);
		if (change)
			ainfo->rate[0] = ucontrol->value.integer.value[0];
		break;
	case CTRL_CAPTURE:
		change |= (ainfo->rate[1] != ucontrol->value.integer.value[0]);
		if (change)
			ainfo->rate[1] = ucontrol->value.integer.value[0];
		break;
	default:
		return -EINVAL;
	}

	return change;
}

static struct snd_kcontrol_new playback_rate_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Playback Sampling Rate",
	.index		= 0,
	.info		= scu_dai_info_rate,
	.get		= scu_dai_get_rate,
	.put		= scu_dai_put_rate,
	.private_value	= CTRL_PLAYBACK,
};

static struct snd_kcontrol_new capture_rate_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Capture Sampling Rate",
	.index		= 0,
	.info		= scu_dai_info_rate,
	.get		= scu_dai_get_rate,
	.put		= scu_dai_put_rate,
	.private_value	= CTRL_CAPTURE,
};

static int scu_dai_info_volume(struct snd_kcontrol *kctrl,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = VOLUME_MAX_DVC;

	return 0;
}

static int scu_dai_get_volume(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);

	switch (kctrl->private_value) {
	case CTRL_PLAYBACK:
		ucontrol->value.integer.value[0] = ainfo->volume[0][0];
		ucontrol->value.integer.value[1] = ainfo->volume[0][1];
		break;
	case CTRL_CAPTURE:
		ucontrol->value.integer.value[0] = ainfo->volume[1][0];
		ucontrol->value.integer.value[1] = ainfo->volume[1][1];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int scu_dai_put_volume(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);
	int change = 0;

	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] > VOLUME_MAX_DVC ||
	    ucontrol->value.integer.value[1] < 0 ||
	    ucontrol->value.integer.value[1] > VOLUME_MAX_DVC)
		return -EINVAL;

	switch (kctrl->private_value) {
	case CTRL_PLAYBACK:
		change |= ((ainfo->volume[0][0] !=
				ucontrol->value.integer.value[0]) ||
			   (ainfo->volume[0][1] !=
				ucontrol->value.integer.value[1]));
		if (change) {
			ainfo->volume[0][0] = ucontrol->value.integer.value[0];
			ainfo->volume[0][1] = ucontrol->value.integer.value[1];
			/* DVC0 L:vol0r R:vol1r */
			clk_enable(ainfo->clockinfo.scu_clk);
			clk_enable(ainfo->clockinfo.dvc_clk[0]);
			writel(ainfo->volume[0][0],
				(u32 *)&rinfo->dvcreg[0]->vol0r);
			writel(ainfo->volume[0][1],
				(u32 *)&rinfo->dvcreg[0]->vol1r);
			clk_disable(ainfo->clockinfo.dvc_clk[0]);
			clk_disable(ainfo->clockinfo.scu_clk);
		}
		break;
	case CTRL_CAPTURE:
		change |= ((ainfo->volume[1][0] !=
				ucontrol->value.integer.value[0]) ||
			   (ainfo->volume[1][1] !=
				ucontrol->value.integer.value[1]));
		if (change) {
			ainfo->volume[1][0] = ucontrol->value.integer.value[0];
			ainfo->volume[1][1] = ucontrol->value.integer.value[1];
			/* DVC1 L:vol1r R:vol0r */
			clk_enable(ainfo->clockinfo.scu_clk);
			clk_enable(ainfo->clockinfo.dvc_clk[1]);
			writel(ainfo->volume[1][0],
				(u32 *)&rinfo->dvcreg[1]->vol1r);
			writel(ainfo->volume[1][1],
				(u32 *)&rinfo->dvcreg[1]->vol0r);
			clk_disable(ainfo->clockinfo.dvc_clk[1]);
			clk_disable(ainfo->clockinfo.scu_clk);
		}
		break;
	default:
		return -EINVAL;
	}

	return change;
}

static struct snd_kcontrol_new playback_volume_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Playback Volume",
	.index		= 0,
	.info		= scu_dai_info_volume,
	.get		= scu_dai_get_volume,
	.put		= scu_dai_put_volume,
	.private_value	= CTRL_PLAYBACK,
};

static struct snd_kcontrol_new capture_volume_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "Capture Volume",
	.index		= 0,
	.info		= scu_dai_info_volume,
	.get		= scu_dai_get_volume,
	.put		= scu_dai_put_volume,
	.private_value	= CTRL_CAPTURE,
};

static int scu_dai_info_mute(struct snd_kcontrol *kctrl,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;

	return 0;
}

static int scu_dai_get_mute(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);

	ucontrol->value.integer.value[0] = ainfo->mute[0];
	ucontrol->value.integer.value[1] = ainfo->mute[1];

	return 0;
}

static int scu_dai_put_mute(struct snd_kcontrol *kctrl,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct scu_audio_info *ainfo = snd_kcontrol_chip(kctrl);
	int change = 0;
	u32 mute = 0;

	if (ucontrol->value.integer.value[0] < 0 ||
	    ucontrol->value.integer.value[0] > 1 ||
	    ucontrol->value.integer.value[1] < 0 ||
	    ucontrol->value.integer.value[1] > 1)
		return -EINVAL;

	change |= ((ainfo->mute[0] != ucontrol->value.integer.value[0]) ||
		   (ainfo->mute[1] != ucontrol->value.integer.value[1]));
	if (change) {
		ainfo->mute[0] = ucontrol->value.integer.value[0];
		ainfo->mute[1] = ucontrol->value.integer.value[1];
		mute = (ainfo->mute[1] << 1) + ainfo->mute[0];
		mute = ~mute & 0x3;
		clk_enable(ainfo->clockinfo.scu_clk);
		clk_enable(ainfo->clockinfo.dvc_clk[0]);
		writel(mute, (u32 *)&rinfo->dvcreg[0]->zcmcr);
		clk_disable(ainfo->clockinfo.dvc_clk[0]);
		clk_disable(ainfo->clockinfo.scu_clk);
	}

	return change;
}

static struct snd_kcontrol_new playback_mute_controls = {
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name		= "PCM Playback Switch",
	.index		= 0,
	.info		= scu_dai_info_mute,
	.get		= scu_dai_get_mute,
	.put		= scu_dai_put_mute,
};

int scu_dai_add_control(struct snd_card *card)
{
	struct device *dev = card->dev;
	struct snd_kcontrol *kctrl;
	int i, j, ret;

	/* initial value */
	for (i = 0; i < 2; i++) {
		ainfo->rate[i] = 0;
		ainfo->mute[i] = 1;
		for (j = 0; j < 2; j++)
			ainfo->volume[i][j] = VOLUME_DEFAULT;
	}

	kctrl = snd_ctl_new1(&playback_rate_controls, ainfo);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		dev_err(dev, "failed to add playback rate err=%d\n", ret);
		return ret;
	}

	kctrl = snd_ctl_new1(&capture_rate_controls, ainfo);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		dev_err(dev, "failed to add capture rate err=%d\n", ret);
		return ret;
	}

	kctrl = snd_ctl_new1(&playback_volume_controls, ainfo);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		dev_err(dev, "failed to add playback volume err=%d\n", ret);
		return ret;
	}

	kctrl = snd_ctl_new1(&capture_volume_controls, ainfo);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		dev_err(dev, "failed to add capture volume err=%d\n", ret);
		return ret;
	}

	kctrl = snd_ctl_new1(&playback_mute_controls, ainfo);
	ret = snd_ctl_add(card, kctrl);
	if (ret < 0) {
		dev_err(dev, "failed to add playback mute err=%d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(scu_dai_add_control);

static int scu_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	int ret = 0;

	FNC_ENTRY
	snd_soc_set_runtime_hwparams(substream, &scu_dai_pcm_hw);

	FNC_EXIT
	return ret;
}

static void scu_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	FNC_ENTRY
	FNC_EXIT
	return;
}

static int scu_dai_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	FNC_ENTRY
	FNC_EXIT
	return 0;
}

static int scu_dai_set_fmt(struct snd_soc_dai *dai,
			   unsigned int fmt)
{
	FNC_ENTRY
	FNC_EXIT
	return 0;
}

static int scu_dai_set_sysclk(struct snd_soc_dai *dai, int clk_id,
			      unsigned int freq, int dir)
{
	FNC_ENTRY
	FNC_EXIT
	return 0;
}

static const struct snd_soc_dai_ops scu_dai_ops = {
	.startup	= scu_dai_startup,
	.shutdown	= scu_dai_shutdown,
	.prepare	= scu_dai_prepare,
	.set_sysclk	= scu_dai_set_sysclk,
	.set_fmt	= scu_dai_set_fmt,
};

static struct snd_soc_dai_driver scu_ssi_dai = {
	.name	= "scu-ssi-dai",
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.formats = SNDRV_PCM_FMTBIT_S16,
		.rates = SNDRV_PCM_RATE_8000_48000,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.formats = SNDRV_PCM_FMTBIT_S16,
		.rates = SNDRV_PCM_RATE_8000_48000,
	 },
	.ops = &scu_dai_ops,
};

static void scu_alloc_scureg(void __iomem *mem)
{
	int i;
	void __iomem *offset;

	/* SCU common */
	rinfo->scureg = mem;
	DBG_MSG("scureg=%08x\n", (int)rinfo->scureg);

	/* SCU common SRC */
	offset = mem;
	for (i = 0; i < MAXCH_SRC; i++) {
		rinfo->scusrcreg[i] = (struct scu_src_regs *)offset;
		offset += 0x20;
		DBG_MSG("scusrcreg[%2d]=%08x\n", i, (int)rinfo->scusrcreg[i]);
	}

	/* SCU common CMD */
	offset = mem + 0x184;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->scucmdreg[i] = (struct scu_cmd_regs *)offset;
		offset += 0x20;
		DBG_MSG("scucmdreg[%2d]=%08x\n", i, (int)rinfo->scucmdreg[i]);
	}

	/* SCU SYSTEM */
	offset = mem + 0x1C8;
	rinfo->scu_sys_regs = (struct scu_sys_regs *)offset;
	DBG_MSG("scusystemreg=%08x\n", (int)rinfo->scu_sys_regs);

	/* SRC */
	offset = mem + 0x200;
	for (i = 0; i < MAXCH_SRC; i++) {
		rinfo->srcreg[i] = (struct src_regs *)offset;
		offset += 0x40;
		DBG_MSG("srcreg[%2d]=%08x\n", i, (int)rinfo->srcreg[i]);
	}

	/* CTU */
	offset = mem + 0x500;
	for (i = 0; i < MAXCH_CTU; i++) {
		rinfo->ctureg[i] = (struct ctu_regs *)offset;
		offset += 0x100;
		DBG_MSG("ctureg[%2d]=%08x\n", i, (int)rinfo->ctureg[i]);
	}

	/* MIX */
	offset = mem + 0xd00;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->mixreg[i] = (struct mix_regs *)offset;
		offset += 0x40;
		DBG_MSG("mixreg[%2d]=%08x\n", i, (int)rinfo->mixreg[i]);
	}

	/* DVC */
	offset = mem + 0xe00;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->dvcreg[i] = (struct dvc_regs *)offset;
		offset += 0x100;
		DBG_MSG("dvcreg[%2d]=%08x\n", i, (int)rinfo->dvcreg[i]);
	}

	return;
}

static void scu_alloc_ssiureg(void __iomem *mem)
{
	int i;
	void __iomem *offset;

	offset = mem;
	for (i = 0; i < MAXCH_SSI; i++) {
		rinfo->ssiu_std_reg[i] = (struct ssiu_std_regs *)offset;
		offset += 0x80;
		DBG_MSG("ssiureg[%2d]=%08x\n", i, (int)rinfo->ssiu_std_reg[i]);
	}

	return;
}

static void scu_alloc_ssireg(void __iomem *mem)
{
	int i;
	void __iomem *offset;

	offset = mem;
	for (i = 0; i < MAXCH_SSI; i++) {
		rinfo->ssireg[i] = (struct ssi_regs *)offset;
		offset += 0x40;
		DBG_MSG("ssireg[%2d]=%08x\n", i, (int)rinfo->ssireg[i]);
	}

	return;
}

static int scu_request_irqs(struct scu_irq_info *irqinfo,
						struct scu_audio_info *ainfo)
{
	int ret;

	/* ssi interrupt for playback */
	ret = request_irq(irqinfo->ssi_irq_playback, scu_ssi_interrupt, 0,
						ainfo->ssi_irq_pname, irqinfo);
	if (ret < 0)
		return ret;

	/* ssi interrupt for capture */
	ret = request_irq(irqinfo->ssi_irq_capture, scu_ssi_interrupt, 0,
						ainfo->ssi_irq_cname, irqinfo);
	if (ret < 0)
		goto error_req_irq1;

	/* src interrupt for playback */
	ret = request_irq(irqinfo->src_irq_playback, scu_src_interrupt, 0,
						ainfo->src_irq_pname, irqinfo);
	if (ret < 0)
		goto error_req_irq2;

	/* src interrupt for capture */
	ret = request_irq(irqinfo->src_irq_capture, scu_src_interrupt, 0,
						ainfo->src_irq_cname, irqinfo);
	if (ret < 0)
		goto error_req_irq3;

	return 0;

error_req_irq3:
	free_irq(irqinfo->src_irq_playback, irqinfo);

error_req_irq2:
	free_irq(irqinfo->ssi_irq_capture, irqinfo);

error_req_irq1:
	free_irq(irqinfo->ssi_irq_playback, irqinfo);

	return ret;
}

static void scu_free_irqs(struct scu_irq_info *irqinfo)
{
	free_irq(irqinfo->src_irq_capture, irqinfo);
	free_irq(irqinfo->src_irq_playback, irqinfo);
	free_irq(irqinfo->ssi_irq_capture, irqinfo);
	free_irq(irqinfo->ssi_irq_playback, irqinfo);
}

static int __devinit scu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct scu_clock_info *cinfo;
	struct scu_irq_info *irqinfo;
	struct resource *scu_res;
	struct resource *scu_region = NULL;
	struct resource *ssiu_res;
	struct resource *ssiu_region = NULL;
	struct resource *ssi_res;
	struct resource *ssi_region = NULL;
	struct resource *adg_res;
	struct resource *adg_region = NULL;
	void __iomem *mem;
	struct resource *irq_res[4];

	FNC_ENTRY
	if (pdev->id != 0) {
		dev_err(&pdev->dev, "current scu support id 0 only now\n");
		return -ENODEV;
	}
	pdata = pdev->dev.platform_data;

	ainfo = kzalloc(sizeof(struct scu_audio_info), GFP_KERNEL);
	if (!ainfo) {
		dev_err(&pdev->dev, "no memory\n");
		return -ENOMEM;
	}
	cinfo = &ainfo->clockinfo;
	rinfo = &ainfo->reginfo;
	irqinfo = &ainfo->irqinfo;

	spin_lock_init(&ainfo->scu_lock);
	sculock = &ainfo->scu_lock;

	/* request clocks */
	cinfo->adg_clk = clk_get(NULL, "adg");
	if (IS_ERR(cinfo->adg_clk)) {
		dev_err(&pdev->dev, "Unable to get adg clock\n");
		ret = PTR_ERR(cinfo->adg_clk);
		goto error_clk_put;
	}

	cinfo->scu_clk = clk_get(NULL, "scu");
	if (IS_ERR(cinfo->scu_clk)) {
		dev_err(&pdev->dev, "Unable to get scu clock\n");
		ret = PTR_ERR(cinfo->scu_clk);
		goto error_clk_put;
	}

	cinfo->src_clk[SRC0] = clk_get(NULL, "src0");
	if (IS_ERR(cinfo->src_clk[SRC0])) {
		dev_err(&pdev->dev, "Unable to get src0 clock\n");
		ret = PTR_ERR(cinfo->src_clk[SRC0]);
		goto error_clk_put;
	}

	cinfo->src_clk[SRC1] = clk_get(NULL, "src1");
	if (IS_ERR(cinfo->src_clk[SRC1])) {
		dev_err(&pdev->dev, "Unable to get src1 clock\n");
		ret = PTR_ERR(cinfo->src_clk[SRC1]);
		goto error_clk_put;
	}

	cinfo->dvc_clk[DVC0] = clk_get(NULL, "dvc0");
	if (IS_ERR(cinfo->dvc_clk[DVC0])) {
		dev_err(&pdev->dev, "Unable to get dvc0 clock\n");
		ret = PTR_ERR(cinfo->dvc_clk[DVC0]);
		goto error_clk_put;
	}

	cinfo->dvc_clk[DVC1] = clk_get(NULL, "dvc1");
	if (IS_ERR(cinfo->dvc_clk[DVC1])) {
		dev_err(&pdev->dev, "Unable to get dvc1 clock\n");
		ret = PTR_ERR(cinfo->dvc_clk[DVC1]);
		goto error_clk_put;
	}

	cinfo->ssiu_clk = clk_get(NULL, "ssi");
	if (IS_ERR(cinfo->ssiu_clk)) {
		dev_err(&pdev->dev, "Unable to get ssiu clock\n");
		ret = PTR_ERR(cinfo->ssiu_clk);
		goto error_clk_put;
	}

	cinfo->ssi_clk[SSI0] = clk_get(NULL, "ssi0");
	if (IS_ERR(cinfo->ssi_clk[SSI0])) {
		dev_err(&pdev->dev, "Unable to get ssi0 clock\n");
		ret = PTR_ERR(cinfo->ssi_clk[SSI0]);
		goto error_clk_put;
	}

	cinfo->ssi_clk[SSI1] = clk_get(NULL, "ssi1");
	if (IS_ERR(cinfo->ssi_clk[SSI1])) {
		dev_err(&pdev->dev, "Unable to get ssi1 clock\n");
		ret = PTR_ERR(cinfo->ssi_clk[SSI1]);
		goto error_clk_put;
	}

	/* resource */
	scu_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!scu_res) {
		dev_err(&pdev->dev, "No memory (0) resource\n");
		ret = -ENODEV;
		goto error_clk_put;
	}

	ssiu_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!ssiu_res) {
		dev_err(&pdev->dev, "No memory (1) resource\n");
		ret = -ENODEV;
		goto error_clk_put;
	}

	ssi_res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!ssiu_res) {
		dev_err(&pdev->dev, "No memory (2) resource\n");
		ret = -ENODEV;
		goto error_clk_put;
	}

	adg_res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (!adg_res) {
		dev_err(&pdev->dev, "No memory (3) resource\n");
		ret = -ENODEV;
		goto error_clk_put;
	}

	/* ssi number for playback */
	irqinfo->ssi_num_playback = pdata->ssi_num_playback;
	/* ssi irq number for playback */
	irqinfo->ssi_irq_playback = platform_get_irq(pdev, 0);
	/* ssi irq resource for playback */
	irq_res[0] = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irqinfo->ssi_irq_playback < 0 || !(irq_res[0])) {
		dev_err(&pdev->dev, "No ssi_irq resource for playback\n");
		ret = -ENODEV;
		goto error_clk_put;
	};
	snprintf(ainfo->ssi_irq_pname, 16, "%s", irq_res[0]->name);

	/* ssi number for capture */
	irqinfo->ssi_num_capture = pdata->ssi_num_capture;
	/* ssi irq number for capture */
	irqinfo->ssi_irq_capture = platform_get_irq(pdev, 1);
	/* ssi irq resource for capture */
	irq_res[1] = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (irqinfo->ssi_irq_capture < 0 || !(irq_res[1])) {
		dev_err(&pdev->dev, "No ssi_irq resource for capture\n");
		ret = -ENODEV;
		goto error_clk_put;
	};
	snprintf(ainfo->ssi_irq_cname, 16, "%s", irq_res[1]->name);

	/* src number for playback */
	irqinfo->src_num_playback = pdata->src_num_playback;
	/* src irq number for playback */
	irqinfo->src_irq_playback = platform_get_irq(pdev, 2);
	/* src irq resource for playback */
	irq_res[2] = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (irqinfo->src_irq_playback < 0 || !(irq_res[2])) {
		dev_err(&pdev->dev, "No src_irq resource for playback\n");
		ret = -ENODEV;
		goto error_clk_put;
	};
	snprintf(ainfo->src_irq_pname, 16, "%s", irq_res[2]->name);

	/* src number for capture */
	irqinfo->src_num_capture = pdata->src_num_capture;
	/* src irq number for capture */
	irqinfo->src_irq_capture = platform_get_irq(pdev, 3);
	/* src irq resource for capture */
	irq_res[3] = platform_get_resource(pdev, IORESOURCE_IRQ, 3);
	if (irqinfo->src_irq_capture < 0 || !(irq_res[3])) {
		dev_err(&pdev->dev, "No src_irq resource for capture\n");
		ret = -ENODEV;
		goto error_clk_put;
	};
	snprintf(ainfo->src_irq_cname, 16, "%s", irq_res[3]->name);

	/* request irqs */
	ret = scu_request_irqs(irqinfo, ainfo);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto error_clk_put;
	}

	scu_region = request_mem_region(scu_res->start,
			resource_size(scu_res), pdev->name);
	if (!scu_region) {
		dev_err(&pdev->dev, "SCU region already claimed\n");
		ret = -EBUSY;
		goto error_release;
	}

	ssiu_region = request_mem_region(ssiu_res->start,
			resource_size(ssiu_res), pdev->name);
	if (!ssiu_region) {
		dev_err(&pdev->dev, "SSIU region already claimed\n");
		ret = -EBUSY;
		goto error_release;
	}

	ssi_region = request_mem_region(ssi_res->start,
			resource_size(ssi_res), pdev->name);
	if (!ssi_region) {
		dev_err(&pdev->dev, "SSI region already claimed\n");
		ret = -EBUSY;
		goto error_release;
	}

	adg_region = request_mem_region(adg_res->start,
			resource_size(adg_res), pdev->name);
	if (!adg_region) {
		dev_err(&pdev->dev, "ADG region already claimed\n");
		ret = -EBUSY;
		goto error_release;
	}

	/* mapping scu */
	mem = ioremap_nocache(scu_res->start, resource_size(scu_res));
	if (!mem) {
		dev_err(&pdev->dev, "ioremap failed for scu\n");
		ret = -ENOMEM;
		goto error_unmap;
	}
	scu_alloc_scureg(mem);

	mem = ioremap_nocache(ssiu_res->start, resource_size(ssiu_res));
	if (!mem) {
		dev_err(&pdev->dev, "ioremap failed for ssiu\n");
		ret = -ENOMEM;
		goto error_unmap;
	}
	rinfo->ssiureg = mem;
	DBG_MSG("ssiureg=%08x\n", (int)rinfo->ssiureg);
	scu_alloc_ssiureg(mem);

	mem = ioremap_nocache(ssi_res->start, resource_size(ssi_res));
	if (!mem) {
		dev_err(&pdev->dev, "ioremap failed for ssi\n");
		ret = -ENOMEM;
		goto error_unmap;
	}
	scu_alloc_ssireg(mem);

	mem = ioremap_nocache(adg_res->start, resource_size(adg_res));
	if (!mem) {
		dev_err(&pdev->dev, "ioremap failed for adg\n");
		ret = -ENOMEM;
		goto error_unmap;
	}
	rinfo->adgreg = mem;
	DBG_MSG("adgreg=%08x\n", (int)rinfo->adgreg);

	ret = snd_soc_register_platform(&pdev->dev, &scu_platform);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc register\n");
		goto error_unmap;
	}

	ret = snd_soc_register_dais(&pdev->dev, &scu_ssi_dai, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd soc dais register\n");
		goto error_unregister;
	}

	FNC_EXIT
	return ret;

error_unregister:
	snd_soc_unregister_platform(&pdev->dev);

error_unmap:
	if (rinfo->scureg)
		iounmap(rinfo->scureg);
	if (rinfo->ssiureg)
		iounmap(rinfo->ssiureg);
	if (rinfo->ssireg)
		iounmap(rinfo->ssireg[0]);
	if (rinfo->adgreg)
		iounmap(rinfo->adgreg);

error_release:
	if (scu_region)
		release_mem_region(scu_res->start, resource_size(scu_res));
	if (ssiu_region)
		release_mem_region(ssiu_res->start, resource_size(ssiu_res));
	if (ssi_region)
		release_mem_region(ssi_res->start, resource_size(ssi_res));
	if (adg_region)
		release_mem_region(adg_res->start, resource_size(adg_res));

/* error irqs */
	scu_free_irqs(irqinfo);

error_clk_put:
	if (!IS_ERR(cinfo->adg_clk))
		clk_put(cinfo->adg_clk);
	if (!IS_ERR(cinfo->scu_clk))
		clk_put(cinfo->scu_clk);
	if (!IS_ERR(cinfo->src_clk[SRC0]))
		clk_put(cinfo->src_clk[SRC0]);
	if (!IS_ERR(cinfo->src_clk[SRC1]))
		clk_put(cinfo->src_clk[SRC1]);
	if (!IS_ERR(cinfo->dvc_clk[DVC0]))
		clk_put(cinfo->dvc_clk[DVC0]);
	if (!IS_ERR(cinfo->dvc_clk[DVC1]))
		clk_put(cinfo->dvc_clk[DVC1]);
	if (!IS_ERR(cinfo->ssiu_clk))
		clk_put(cinfo->ssiu_clk);
	if (!IS_ERR(cinfo->ssi_clk[SSI0]))
		clk_put(cinfo->ssi_clk[SSI0]);
	if (!IS_ERR(cinfo->ssi_clk[SSI1]))
		clk_put(cinfo->ssi_clk[SSI1]);

	FNC_EXIT
	return ret;
}

static int __devexit scu_remove(struct platform_device *pdev)
{
	struct scu_clock_info *cinfo = &ainfo->clockinfo;
	struct scu_irq_info *irqinfo = &ainfo->irqinfo;
	struct resource *res;

	FNC_ENTRY
	snd_soc_unregister_dai(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);

	clk_put(cinfo->adg_clk);
	clk_put(cinfo->scu_clk);
	clk_put(cinfo->src_clk[SRC0]);
	clk_put(cinfo->src_clk[SRC1]);
	clk_put(cinfo->dvc_clk[DVC0]);
	clk_put(cinfo->dvc_clk[DVC1]);
	clk_put(cinfo->ssiu_clk);
	clk_put(cinfo->ssi_clk[SSI0]);
	clk_put(cinfo->ssi_clk[SSI1]);

	scu_free_irqs(irqinfo);

	iounmap(rinfo->scureg);
	iounmap(rinfo->ssiureg);
	iounmap(rinfo->ssireg[0]);
	iounmap(rinfo->adgreg);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res)
		release_mem_region(res->start, resource_size(res));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (res)
		release_mem_region(res->start, resource_size(res));
	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (res)
		release_mem_region(res->start, resource_size(res));

	kfree(ainfo);

	FNC_EXIT
	return 0;
}

static struct platform_driver scu_driver = {
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "scu-pcm-audio",
	},
	.probe		= scu_probe,
	.remove		= __devexit_p(scu_remove),
};

module_platform_driver(scu_driver);

MODULE_AUTHOR("Shunsuke Kataoka <shunsuke.kataoka.df@renesas.com>");
MODULE_DESCRIPTION("ALSA SoC SH7722 SIU driver");
MODULE_LICENSE("GPL");

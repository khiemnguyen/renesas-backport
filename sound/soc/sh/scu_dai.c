/*
 * sound/soc/sh/scu_dai.c
 *     This file is ALSA SoC driver for SCU peripheral.
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
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

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/clk.h>

#include <sound/control.h>
#include <sound/soc.h>
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

struct scu_route_info *scu_get_route_info(void)
{
	return &ainfo->routeinfo;
}
EXPORT_SYMBOL(scu_get_route_info);

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

	peripheral function

************************************************************************/
static void adg_init(void)
{
	FNC_ENTRY

	/* clock select */
	scu_or_writel(0x23550000,
		(u32 *)(rinfo->adgreg + ADG_SSICKR));

	/* audio clock select */
	scu_or_writel(((ADG_SEL0_SSI1_DIV1 | ADG_SEL0_SSI1_ACLK_DIV |
		ADG_SEL0_SSI1_DIVCLK_CLKA) | (ADG_SEL0_SSI0_DIV1 |
		ADG_SEL0_SSI0_ACLK_DIV | ADG_SEL0_SSI0_DIVCLK_CLKA)),
		(u32 *)(rinfo->adgreg + ADG_AUDIO_CLK_SEL0));

	/* SRC Input Timing */
	scu_or_writel(ADG_SRCIN0_SRC0_DIVCLK_SSI_WS0,
		(u32 *)(rinfo->adgreg + ADG_SRCIN_TIMSEL0));

	/* SRC Output Timing */
	scu_or_writel(ADG_SRCOUT0_SRC0_DIVCLK_SSI_WS0,
		(u32 *)(rinfo->adgreg + ADG_SRCOUT_TIMSEL0));

	/* CMD Output Timing */
	/* not implement */

	/* division enable */
	scu_or_writel(ADG_DIVEN_AUDIO_CLKA,
			(u32 *)(rinfo->adgreg + ADG_DIV_EN));

	FNC_EXIT
	return;
}

static void scu_ssiu_init(void)
{
	FNC_ENTRY
	FNC_EXIT
	return;
}

static void scu_ssi_control(int master_ch, int slave_ch)
{
	FNC_ENTRY
	/* SSI setting */
	writel(SSICR_P4643_ST, &rinfo->ssireg[master_ch]->cr);
	writel(SSIWS_ST, &rinfo->ssireg[master_ch]->wsr);
	writel(SSICR_C4643_ST, &rinfo->ssireg[slave_ch]->cr);

	FNC_EXIT
	return;
}

static void scu_ssi_start(int ssi_ch)
{
	u32 val, reg;

	FNC_ENTRY
	/* SSI enable (figure.39.12 flow) */
	val = readl(&rinfo->ssireg[ssi_ch]->cr);
	val |= (SSICR_DMEN | SSICR_UIEN | SSICR_OIEN | SSICR_ENABLE);
	writel(val, &rinfo->ssireg[ssi_ch]->cr);

	/* SSIU start (figure.39.12 flow) */
	if (ssi_ch == 0)
		reg = SSI0_0_CONTROL;
	else
		reg = SSI1_0_CONTROL;

	scu_or_writel(SSI_CTRL_4CH_START0,
		(u32 *)(rinfo->ssiureg + reg));

	FNC_EXIT
	return;
}

static void scu_ssi_stop(int ssi_ch)
{
	u32 val, reg;

	FNC_ENTRY
	/* SSI disable (figure.39.13 flow) */
	val = readl(&rinfo->ssireg[ssi_ch]->cr);
	val &= ~(SSICR_DMEN | SSICR_UIEN | SSICR_UIEN | SSICR_ENABLE);
	writel(val, &rinfo->ssireg[ssi_ch]->cr);
	FNC_EXIT

	/* SSIU stop (figure.39.13 flow) */
	if (ssi_ch == 0)
		reg = SSI0_0_CONTROL;
	else
		reg = SSI1_0_CONTROL;

	writel(0, (u32 *)(rinfo->ssiureg + reg));
}

void scu_src_control(int src_ch, struct snd_pcm_substream *ss)
{
	u64 val = 0;

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
#ifdef CONVERT_48KHZ
	/* Convert example (48kHz) */
	val = div_u64(SRC_IFS_FSO * ss->runtime->rate, SRC_IFS_48KHZ);
#else
	/* Not convert example (CODEC driver converts by itself) */
	val = SRC_IFS_FSO;
#endif
	writel((u32)val, (u32 *)&rinfo->srcreg[src_ch]->ifsvr);

	/* SRC_SRCCR */
	writel(0x00010111, (u32 *)&rinfo->srcreg[src_ch]->srccr);

	/* SRC_MNFSR MINFS calculation */
	val = div_u64(val * 98, 100);	/* 98% */
	writel(val, (u32 *)&rinfo->srcreg[src_ch]->mnfsr);

	/* SRC_BSDSR (FSO/FSI Ratio is 6-1) */
	writel(0x00400000, (u32 *)&rinfo->srcreg[src_ch]->bsdsr);

	/* SRC_BSISR (FSO/FSI Ratio is 6-1) */
	writel(0x00100020, (u32 *)&rinfo->srcreg[src_ch]->bsisr);

	/* SRC_SRCIR */
	writel(0, (u32 *)&rinfo->srcreg[src_ch]->srcir);

	FNC_EXIT
	return;
}
EXPORT_SYMBOL(scu_src_control);

static void scu_src_start(int src_ch, int src_dir)
{
	FNC_ENTRY
	/* SCU SRC_MODE */
	writel(SRC_MODE_SRCUSE, (u32 *)&rinfo->scusrcreg[src_ch]->mode);

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

	/* SCU SRC_MODE */
	writel(0, (u32 *)&rinfo->scusrcreg[src_ch]->mode);

	FNC_EXIT
	return;
}

/************************************************************************

	DAPM callback function

************************************************************************/
static void scu_ssi_control(int master_ch, int slave_ch);
static void scu_ssi_start(int ssi_ch);
static void scu_ssi_stop(int ssi_ch);

void scu_init_ssi0(void)
{
	/* SSI0_0_BUSIF_ADINR */
	scu_or_writel((SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI0_0_BUSIF_ADINR));
	/* SSI_MODE0 (SSI independant) */
	scu_or_writel(SSI_MODE0_IND0, (u32 *)(rinfo->ssiureg + SSI_MODE0));
	/* SSI init */
	scu_ssi_control(0, 1);
	/* SSI start */
	scu_ssi_start(0);
}
EXPORT_SYMBOL(scu_init_ssi0);

void scu_deinit_ssi0(void)
{
	/* SSI stop */
	scu_ssi_stop(0);
	/* SSI_MODE0 (SSI independant) */
	scu_and_writel(~SSI_MODE0_IND0, (u32 *)(rinfo->ssiureg + SSI_MODE0));
	/* SSI0_0_BUSIF_ADINR */
	scu_and_writel(~(SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI0_0_BUSIF_ADINR));
}
EXPORT_SYMBOL(scu_deinit_ssi0);

void scu_init_ssi0_src0(void)
{
	/* SSI0_0_BUSIF_ADINR */
	scu_or_writel((SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI0_0_BUSIF_ADINR));
	/* SSI init */
	scu_ssi_control(0, 1);
	/* SSI start */
	scu_ssi_start(0);
}
EXPORT_SYMBOL(scu_init_ssi0_src0);

void scu_deinit_ssi0_src0(void)
{
	/* SSI stop */
	scu_ssi_stop(0);
	/* SSI0_0_BUSIF_ADINR */
	scu_and_writel(~(SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI0_0_BUSIF_ADINR));
}
EXPORT_SYMBOL(scu_deinit_ssi0_src0);

void scu_init_ssi0_dvc0(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_init_ssi0_dvc0);

void scu_deinit_ssi0_dvc0(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_deinit_ssi0_dvc0);

void scu_init_src0(void)
{
	/* start src */
	scu_src_start(0, SRC_INOUT);
}
EXPORT_SYMBOL(scu_init_src0);

void scu_deinit_src0(void)
{
	/* stop src */
	scu_src_stop(0, SRC_INOUT);
}
EXPORT_SYMBOL(scu_deinit_src0);

void scu_init_dvc0(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_init_dvc0);

void scu_deinit_dvc0(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_deinit_dvc0);

void scu_init_ssi1(void)
{
	/* SSI1_0_BUSIF_ADINR */
	scu_or_writel((SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI1_0_BUSIF_ADINR));
	/* SSI_MODE0 (SSI independant) */
	scu_or_writel(SSI_MODE0_IND1, (u32 *)(rinfo->ssiureg + SSI_MODE0));
	/* SSI_MODE1 (SSI1 slave, SSI0 master) */
	scu_or_writel(SSI_MODE1_SSI1_MASTER,
			(u32 *)(rinfo->ssiureg + SSI_MODE1));
	/* SSI init */
	scu_ssi_control(0, 1);
	/* SSI start */
	scu_ssi_start(1);
}
EXPORT_SYMBOL(scu_init_ssi1);

void scu_deinit_ssi1(void)
{
	/* SSI stop */
	scu_ssi_stop(1);
	/* SSI_MODE1 (SSI1 slave, SSI0 master) */
	scu_and_writel(~SSI_MODE1_SSI1_MASTER,
			(u32 *)(rinfo->ssiureg + SSI_MODE1));
	/* SSI_MODE0 (SSI independant) */
	scu_and_writel(~SSI_MODE0_IND1, (u32 *)(rinfo->ssiureg + SSI_MODE0));
	/* SSI0_0_BUSIF_ADINR */
	scu_and_writel(~(SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI1_0_BUSIF_ADINR));
}
EXPORT_SYMBOL(scu_deinit_ssi1);

void scu_init_ssi1_src1(void)
{
	/* SSI1_0_BUSIF_ADINR */
	scu_or_writel((SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI1_0_BUSIF_ADINR));
	/* SSI_MODE1 (SSI1 slave, SSI0 master) */
	scu_or_writel(SSI_MODE1_SSI1_MASTER,
			(u32 *)(rinfo->ssiureg + SSI_MODE1));
	/* SSI init */
	scu_ssi_control(0, 1);
	/* SSI start */
	scu_ssi_start(1);
}
EXPORT_SYMBOL(scu_init_ssi1_src1);

void scu_deinit_ssi1_src1(void)
{
	/* SSI stop */
	scu_ssi_stop(1);
	/* SSI_MODE1 (SSI1 slave, SSI0 master) */
	scu_and_writel(~SSI_MODE1_SSI1_MASTER,
			(u32 *)(rinfo->ssiureg + SSI_MODE1));
	/* SSI0_0_BUSIF_ADINR */
	scu_and_writel(~(SSI_ADINR_OTBL_16BIT | SSI_ADINR_CHNUM_2CH),
			(u32 *)(rinfo->ssiureg + SSI1_0_BUSIF_ADINR));
}
EXPORT_SYMBOL(scu_deinit_ssi1_src1);

void scu_init_ssi1_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_init_ssi1_dvc1);

void scu_deinit_ssi1_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_deinit_ssi1_dvc1);

void scu_init_src1(void)
{
	/* start src */
	scu_src_start(1, SRC_INOUT);
}
EXPORT_SYMBOL(scu_init_src1);

void scu_deinit_src1(void)
{
	/* stop src */
	scu_src_stop(1, SRC_INOUT);
}
EXPORT_SYMBOL(scu_deinit_src1);

void scu_init_src1_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_init_src1_dvc1);

void scu_deinit_src1_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_deinit_src1_dvc1);

void scu_init_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_init_dvc1);

void scu_deinit_dvc1(void)
{
	/* no process (not implement ) */
}
EXPORT_SYMBOL(scu_deinit_dvc1);

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
	rinfo->scureg = mem + 0x500000;
	DBG_MSG("scureg=%08x\n", (int)rinfo->scureg);

	/* SCU common SRC */
	offset = mem + 0x500000;
	for (i = 0; i < MAXCH_SRC; i++) {
		rinfo->scusrcreg[i] = (struct scu_src_regs *)offset;
		offset += 0x20;
		DBG_MSG("scusrcreg[%2d]=%08x\n", i, (int)rinfo->scusrcreg[i]);
	}

	/* SCU common CMD */
	offset = mem + 0x500184;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->scucmdreg[i] = (struct scu_cmd_regs *)offset;
		offset += 0x20;
		DBG_MSG("scucmdreg[%2d]=%08x\n", i, (int)rinfo->scucmdreg[i]);
	}

	/* SRC */
	offset = mem + 0x500200;
	for (i = 0; i < MAXCH_SRC; i++) {
		rinfo->srcreg[i] = (struct src_regs *)offset;
		offset += 0x40;
		DBG_MSG("srcreg[%2d]=%08x\n", i, (int)rinfo->srcreg[i]);
	}

	/* CTU */
	offset = mem + 0x500500;
	for (i = 0; i < MAXCH_CTU; i++) {
		rinfo->ctureg[i] = (struct ctu_regs *)offset;
		offset += 0x100;
		DBG_MSG("ctureg[%2d]=%08x\n", i, (int)rinfo->ctureg[i]);
	}

	/* MIX */
	offset = mem + 0x500d00;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->mixreg[i] = (struct mix_regs *)offset;
		offset += 0x40;
		DBG_MSG("mixreg[%2d]=%08x\n", i, (int)rinfo->mixreg[i]);
	}

	/* DVC */
	offset = mem + 0x500e00;
	for (i = 0; i < MAXCH_CMD; i++) {
		rinfo->dvcreg[i] = (struct dvc_regs *)offset;
		offset += 0x100;
		DBG_MSG("dvcreg[%2d]=%08x\n", i, (int)rinfo->dvcreg[i]);
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

static int __devinit scu_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct scu_clock_info *cinfo;
	struct resource *scu_res;
	struct resource *scu_region = NULL;
	struct resource *ssiu_res;
	struct resource *ssiu_region = NULL;
	struct resource *ssi_res;
	struct resource *ssi_region = NULL;
	struct resource *adg_res;
	struct resource *adg_region = NULL;
	void __iomem *mem;

	FNC_ENTRY
	if (pdev->id != 0) {
		dev_err(&pdev->dev, "current scu support id 0 only now\n");
		return -ENODEV;
	}

	ainfo = kzalloc(sizeof(struct scu_audio_info), GFP_KERNEL);
	if (!ainfo) {
		dev_err(&pdev->dev, "no memory\n");
		return -ENOMEM;
	}
	cinfo = &ainfo->clockinfo;
	rinfo = &ainfo->reginfo;

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

	cinfo->src0_clk = clk_get(NULL, "src0");
	if (IS_ERR(cinfo->src0_clk)) {
		dev_err(&pdev->dev, "Unable to get src0 clock\n");
		ret = PTR_ERR(cinfo->src0_clk);
		goto error_clk_put;
	}

	cinfo->src1_clk = clk_get(NULL, "src1");
	if (IS_ERR(cinfo->src1_clk)) {
		dev_err(&pdev->dev, "Unable to get src1 clock\n");
		ret = PTR_ERR(cinfo->src1_clk);
		goto error_clk_put;
	}

	cinfo->dvc0_clk = clk_get(NULL, "dvc0");
	if (IS_ERR(cinfo->dvc0_clk)) {
		dev_err(&pdev->dev, "Unable to get dvc0 clock\n");
		ret = PTR_ERR(cinfo->dvc0_clk);
		goto error_clk_put;
	}

	cinfo->dvc1_clk = clk_get(NULL, "dvc1");
	if (IS_ERR(cinfo->dvc1_clk)) {
		dev_err(&pdev->dev, "Unable to get dvc1 clock\n");
		ret = PTR_ERR(cinfo->dvc1_clk);
		goto error_clk_put;
	}

	cinfo->ssiu_clk = clk_get(NULL, "ssi");
	if (IS_ERR(cinfo->ssiu_clk)) {
		dev_err(&pdev->dev, "Unable to get ssiu clock\n");
		ret = PTR_ERR(cinfo->ssiu_clk);
		goto error_clk_put;
	}

	cinfo->ssi0_clk = clk_get(NULL, "ssi0");
	if (IS_ERR(cinfo->ssi0_clk)) {
		dev_err(&pdev->dev, "Unable to get ssi0 clock\n");
		ret = PTR_ERR(cinfo->ssi0_clk);
		goto error_clk_put;
	}

	cinfo->ssi1_clk = clk_get(NULL, "ssi1");
	if (IS_ERR(cinfo->ssi1_clk)) {
		dev_err(&pdev->dev, "Unable to get ssi1 clock\n");
		ret = PTR_ERR(cinfo->ssi1_clk);
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

	clk_enable(cinfo->adg_clk);
	clk_enable(cinfo->scu_clk);
	clk_enable(cinfo->src0_clk);
	clk_enable(cinfo->src1_clk);
	clk_enable(cinfo->dvc0_clk);
	clk_enable(cinfo->dvc1_clk);
	clk_enable(cinfo->ssiu_clk);
	clk_enable(cinfo->ssi0_clk);
	clk_enable(cinfo->ssi1_clk);

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

	adg_init();
	scu_ssiu_init();

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
		iounmap(rinfo->ssireg);
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

error_clk_put:
	if (!IS_ERR(cinfo->adg_clk))
		clk_put(cinfo->adg_clk);
	if (!IS_ERR(cinfo->scu_clk))
		clk_put(cinfo->scu_clk);
	if (!IS_ERR(cinfo->src0_clk))
		clk_put(cinfo->src0_clk);
	if (!IS_ERR(cinfo->src1_clk))
		clk_put(cinfo->src1_clk);
	if (!IS_ERR(cinfo->dvc0_clk))
		clk_put(cinfo->dvc0_clk);
	if (!IS_ERR(cinfo->dvc1_clk))
		clk_put(cinfo->dvc1_clk);
	if (!IS_ERR(cinfo->ssiu_clk))
		clk_put(cinfo->ssiu_clk);
	if (!IS_ERR(cinfo->ssi0_clk))
		clk_put(cinfo->ssi0_clk);
	if (!IS_ERR(cinfo->ssi1_clk))
		clk_put(cinfo->ssi1_clk);

	FNC_EXIT
	return ret;
}

static int __devexit scu_remove(struct platform_device *pdev)
{
	struct scu_clock_info *cinfo = &ainfo->clockinfo;
	struct resource *res;

	FNC_ENTRY
	snd_soc_unregister_dai(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);

	clk_disable(cinfo->adg_clk);
	clk_disable(cinfo->scu_clk);
	clk_disable(cinfo->src0_clk);
	clk_disable(cinfo->src1_clk);
	clk_disable(cinfo->dvc0_clk);
	clk_disable(cinfo->dvc1_clk);
	clk_disable(cinfo->ssiu_clk);
	clk_disable(cinfo->ssi0_clk);
	clk_disable(cinfo->ssi1_clk);

	clk_put(cinfo->adg_clk);
	clk_put(cinfo->scu_clk);
	clk_put(cinfo->src0_clk);
	clk_put(cinfo->src1_clk);
	clk_put(cinfo->dvc0_clk);
	clk_put(cinfo->dvc1_clk);
	clk_put(cinfo->ssiu_clk);
	clk_put(cinfo->ssi0_clk);
	clk_put(cinfo->ssi1_clk);

	iounmap(rinfo->scureg);
	iounmap(rinfo->ssiureg);
	iounmap(rinfo->ssireg);
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

MODULE_AUTHOR("Carlos Munoz <carlos@kenati.com>");
MODULE_DESCRIPTION("ALSA SoC SH7722 SIU driver");
MODULE_LICENSE("GPL");

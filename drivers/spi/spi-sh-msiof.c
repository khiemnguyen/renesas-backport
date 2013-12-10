/*
 * SuperH MSIOF SPI Master Interface
 *
 * Copyright (c) 2013 Renesas Electronics Corporation
 * Copyright (c) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <linux/spi/sh_msiof.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>

#include <asm/unaligned.h>

#if defined(CONFIG_SPI_SH_MSIOF_DMA)
#include <linux/sh_dma.h>
#include <linux/dma-mapping.h>
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

struct sh_msiof_spi_priv {
	struct spi_bitbang bitbang; /* must be first for spi_bitbang.c */
	void __iomem *mapbase;
	struct clk *clk;
	struct platform_device *pdev;
	struct sh_msiof_spi_info *info;
	struct completion done;
	unsigned long flags;
	int tx_fifo_size;
	int rx_fifo_size;
#if defined(CONFIG_SPI_SH_MSIOF_DMA)
	bool (*dma_filter)(struct dma_chan *chan, void *filter_param);
	struct sh_dmae_slave dma_rx_param;
	struct sh_dmae_slave dma_tx_param;
	struct dma_chan *dma_rx_channel;
	struct dma_chan *dma_tx_channel;
	struct sg_table sgt_rx;
	struct sg_table sgt_tx;
	struct completion dma_tx_wait;
	struct completion dma_rx_wait;
	int bytes_per_word;
#endif /* CONFIG_SPI_SH_MSIOF_DMA */
};

#define TMDR1	0x00
#define TMDR2	0x04
#define TMDR3	0x08
#define RMDR1	0x10
#define RMDR2	0x14
#define RMDR3	0x18
#define TSCR	0x20
#define RSCR	0x22
#define CTR	0x28
#define FCTR	0x30
#define STR	0x40
#define IER	0x44
#define TDR1	0x48
#define TDR2	0x4c
#define TFDR	0x50
#define RDR1	0x58
#define RDR2	0x5c
#define RFDR	0x60

#define CTR_TSCKE (1 << 15)
#define CTR_TFSE  (1 << 14)
#define CTR_TXE   (1 << 9)
#define CTR_RXE   (1 << 8)

#define STR_TEOF  (1 << 23)
#define STR_REOF  (1 << 7)

#if defined(CONFIG_SPI_SH_MSIOF_DMA)
#define IER_TDMAE  (1 << 31)
#define IER_TDREQE (1 << 28)
#define IER_RDMAE  (1 << 15)
#define IER_RDREQE (1 << 12)

static int dma_devid_lo;
static int dma_devid_up;
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

static u32 sh_msiof_read(struct sh_msiof_spi_priv *p, int reg_offs)
{
	switch (reg_offs) {
	case TSCR:
	case RSCR:
		return ioread16(p->mapbase + reg_offs);
	default:
		return ioread32(p->mapbase + reg_offs);
	}
}

static void sh_msiof_write(struct sh_msiof_spi_priv *p, int reg_offs,
			   u32 value)
{
	switch (reg_offs) {
	case TSCR:
	case RSCR:
		iowrite16(value, p->mapbase + reg_offs);
		break;
	default:
		iowrite32(value, p->mapbase + reg_offs);
		break;
	}
}

static int sh_msiof_modify_ctr_wait(struct sh_msiof_spi_priv *p,
				    u32 clr, u32 set)
{
	u32 mask = clr | set;
	u32 data;
	int k;

	data = sh_msiof_read(p, CTR);
	data &= ~clr;
	data |= set;
	sh_msiof_write(p, CTR, data);

	for (k = 100; k > 0; k--) {
		if ((sh_msiof_read(p, CTR) & mask) == set)
			break;

		udelay(10);
	}

	return k > 0 ? 0 : -ETIMEDOUT;
}

static irqreturn_t sh_msiof_spi_irq(int irq, void *data)
{
	struct sh_msiof_spi_priv *p = data;

#if !defined(CONFIG_SPI_SH_MSIOF_DMA)
	/* just disable the interrupt and wake up */
	sh_msiof_write(p, IER, 0);
#else
	if (p->bytes_per_word == 4)
		/* disable transnmit DMA transnmit request */
		sh_msiof_write(p, IER, IER_RDREQE | IER_RDMAE);
	else
		/* just disable the interrupt and wake up */
		sh_msiof_write(p, IER, 0);
#endif /* CONFIG_SPI_SH_MSIOF_DMA */
	complete(&p->done);

	return IRQ_HANDLED;
}

static struct {
	unsigned short div;
	unsigned short scr;
} const sh_msiof_spi_clk_table[] = {
	{ 1, 0x0007 },
	{ 2, 0x0000 },
	{ 4, 0x0001 },
	{ 8, 0x0002 },
	{ 16, 0x0003 },
	{ 32, 0x0004 },
	{ 64, 0x1f00 },
	{ 128, 0x1f01 },
	{ 256, 0x1f02 },
	{ 512, 0x1f03 },
	{ 1024, 0x1f04 },
};

static void sh_msiof_spi_set_clk_regs(struct sh_msiof_spi_priv *p,
				      unsigned long parent_rate,
				      unsigned long spi_hz)
{
	unsigned long div = 1024;
	size_t k;

	if (!WARN_ON(!spi_hz || !parent_rate))
		div = parent_rate / spi_hz;

	/* TODO: make more fine grained */

	for (k = 0; k < ARRAY_SIZE(sh_msiof_spi_clk_table); k++) {
		if (sh_msiof_spi_clk_table[k].div >= div)
			break;
	}

	k = min_t(int, k, ARRAY_SIZE(sh_msiof_spi_clk_table) - 1);

	sh_msiof_write(p, TSCR, sh_msiof_spi_clk_table[k].scr);
	sh_msiof_write(p, RSCR, sh_msiof_spi_clk_table[k].scr);
}

static void sh_msiof_spi_set_pin_regs(struct sh_msiof_spi_priv *p,
				      u32 cpol, u32 cpha,
				      u32 tx_hi_z, u32 lsb_first)
{
	u32 tmp;
	int edge;

	/*
	 * CPOL CPHA     TSCKIZ RSCKIZ TEDG REDG
	 *    0    0         10     10    1    1
	 *    0    1         10     10    0    0
	 *    1    0         11     11    0    0
	 *    1    1         11     11    1    1
	 */
#if !defined(CONFIG_SPI_SH_MSIOF_DMA)
	sh_msiof_write(p, FCTR, 0);
#else
	/* issue a transfer request
	    when 1 or more stages of transmit FIFO are empty */
	sh_msiof_write(p, FCTR, 0xe0000000);
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

	if (p->info->mode == SPI_MSIOF_MASTER)
		sh_msiof_write(p, TMDR1, 0xe2000005 | (lsb_first << 24));
	else
		sh_msiof_write(p, TMDR1, 0x62000005 | (lsb_first << 24));

	sh_msiof_write(p, RMDR1, 0x22000005 | (lsb_first << 24));

	tmp = 0xa0000000;
	tmp |= cpol << 30; /* TSCKIZ */
	tmp |= cpol << 28; /* RSCKIZ */

	edge = cpol ^ !cpha;

	tmp |= edge << 27; /* TEDG */
	tmp |= edge << 26; /* REDG */
	tmp |= (tx_hi_z ? 2 : 0) << 22; /* TXDIZ */
	sh_msiof_write(p, CTR, tmp);
}

static void sh_msiof_spi_set_mode_regs(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, void *rx_buf,
				       u32 bits, u32 words)
{
	u32 dr2 = ((bits - 1) << 24) | ((words - 1) << 16);

	if (tx_buf)
		sh_msiof_write(p, TMDR2, dr2);
	else
		sh_msiof_write(p, TMDR2, dr2 | 1);

	if (rx_buf)
		sh_msiof_write(p, RMDR2, dr2);

#if !defined(CONFIG_SPI_SH_MSIOF_DMA)
	sh_msiof_write(p, IER, STR_TEOF | STR_REOF);
#else
	if (p->bytes_per_word == 4)
		/* enable DMA transfer request */
		sh_msiof_write(p, IER, STR_TEOF | STR_REOF
			| IER_TDREQE | IER_RDREQE | IER_TDMAE | IER_RDMAE);
	else
		sh_msiof_write(p, IER, STR_TEOF | STR_REOF);
#endif /* CONFIG_SPI_SH_MSIOF_DMA */
}

static void sh_msiof_reset_str(struct sh_msiof_spi_priv *p)
{
	sh_msiof_write(p, STR, sh_msiof_read(p, STR));
}

static void sh_msiof_spi_write_fifo_8(struct sh_msiof_spi_priv *p,
				      const void *tx_buf, int words, int fs)
{
	const u8 *buf_8 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_8[k] << fs);
}

static void sh_msiof_spi_write_fifo_16(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, int words, int fs)
{
	const u16 *buf_16 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_16[k] << fs);
}

static void sh_msiof_spi_write_fifo_16u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u16 *buf_16 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, get_unaligned(&buf_16[k]) << fs);
}

static void sh_msiof_spi_write_fifo_32(struct sh_msiof_spi_priv *p,
				       const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, buf_32[k] << fs);
}

static void sh_msiof_spi_write_fifo_32u(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, get_unaligned(&buf_32[k]) << fs);
}

static void sh_msiof_spi_write_fifo_s32(struct sh_msiof_spi_priv *p,
					const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, swab32(buf_32[k] << fs));
}

static void sh_msiof_spi_write_fifo_s32u(struct sh_msiof_spi_priv *p,
					 const void *tx_buf, int words, int fs)
{
	const u32 *buf_32 = tx_buf;
	int k;

	for (k = 0; k < words; k++)
		sh_msiof_write(p, TFDR, swab32(get_unaligned(&buf_32[k]) << fs));
}

static void sh_msiof_spi_read_fifo_8(struct sh_msiof_spi_priv *p,
				     void *rx_buf, int words, int fs)
{
	u8 *buf_8 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_8[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16(struct sh_msiof_spi_priv *p,
				      void *rx_buf, int words, int fs)
{
	u16 *buf_16 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_16[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_16u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u16 *buf_16 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, RFDR) >> fs, &buf_16[k]);
}

static void sh_msiof_spi_read_fifo_32(struct sh_msiof_spi_priv *p,
				      void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_32[k] = sh_msiof_read(p, RFDR) >> fs;
}

static void sh_msiof_spi_read_fifo_32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(sh_msiof_read(p, RFDR) >> fs, &buf_32[k]);
}

static void sh_msiof_spi_read_fifo_s32(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		buf_32[k] = swab32(sh_msiof_read(p, RFDR) >> fs);
}

static void sh_msiof_spi_read_fifo_s32u(struct sh_msiof_spi_priv *p,
				       void *rx_buf, int words, int fs)
{
	u32 *buf_32 = rx_buf;
	int k;

	for (k = 0; k < words; k++)
		put_unaligned(swab32(sh_msiof_read(p, RFDR) >> fs), &buf_32[k]);
}

#if defined(CONFIG_SPI_SH_MSIOF_DMA)
static void sh_msiof_spi_dma_tx_callback(void *data)
{
	struct sh_msiof_spi_priv *priv = data;

	dma_unmap_sg(priv->dma_tx_channel->device->dev, priv->sgt_tx.sgl,
			priv->sgt_tx.nents, DMA_TO_DEVICE);
	sg_free_table(&priv->sgt_tx);

	complete(&priv->dma_tx_wait);
}

static void sh_msiof_spi_dma_rx_callback(void *data)
{
	struct sh_msiof_spi_priv *priv = data;

	dma_unmap_sg(priv->dma_rx_channel->device->dev, priv->sgt_rx.sgl,
			priv->sgt_rx.nents, DMA_FROM_DEVICE);
	sg_free_table(&priv->sgt_rx);
	sh_msiof_write(priv, IER, 0);

	complete(&priv->dma_rx_wait);
}

static void sh_msiof_spi_setup_dma_scatter(struct sh_msiof_spi_priv *priv,
				  void *buffer,
				  unsigned int length,
				  struct sg_table *sgtab)
{
	struct scatterlist *sg;
	int bytesleft = length;
	void *bufp = buffer;
	int mapbytes;
	int i;

	for_each_sg(sgtab->sgl, sg, sgtab->nents, i) {
		if (bytesleft < (PAGE_SIZE - offset_in_page(bufp)))
			mapbytes = bytesleft;
		else
			mapbytes = PAGE_SIZE - offset_in_page(bufp);
		sg_set_page(sg, virt_to_page(bufp),
			   mapbytes, offset_in_page(bufp));
		bufp += mapbytes;
		bytesleft -= mapbytes;
		dev_dbg(&priv->pdev->dev,
			   "set RX/TX target page @ %p, %d bytes, %d left\n",
			   bufp, mapbytes, bytesleft);
	}
}

static void sh_msiof_spi_dma_tx(struct sh_msiof_spi_priv *priv,
			  const void *txbuf, int words, int bytes_per_word)
{
	int ret = 0;
	unsigned int count;
	unsigned int pages;
	int tx_sglen;
	struct dma_chan *txchan = priv->dma_tx_channel;
	struct dma_async_tx_descriptor *txdesc;

	/* check that the channel are available */
	if (!txchan)
		return;

	count = bytes_per_word * words;

	dev_dbg(&priv->pdev->dev, "%s count=%d, txbuf=%lx\n",
		__func__, count, (ulong)txbuf);

	/* create and fill in scatterlists for the transfers */
	pages = DIV_ROUND_UP(count, PAGE_SIZE);

	ret = sg_alloc_table(&priv->sgt_tx, pages, GFP_ATOMIC);
	if (ret) {
		dev_err(&priv->pdev->dev, "failed to allocate scatterlist\n");
		return;
	}

	sh_msiof_spi_setup_dma_scatter(priv, (void *)txbuf,
					count, &priv->sgt_tx);

	/* map DMA buffers */
	tx_sglen = dma_map_sg(txchan->device->dev, priv->sgt_tx.sgl,
			priv->sgt_tx.nents, DMA_TO_DEVICE);
	if (!tx_sglen) {
		ret = -ENOMEM;
		goto err_tx_sgmap;
	}

	txdesc = dmaengine_prep_slave_sg(txchan,
					priv->sgt_tx.sgl,
					tx_sglen,
					DMA_TO_DEVICE,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc) {
		ret = -ENOMEM;
		goto err_txdesc;
	}
	txdesc->callback = sh_msiof_spi_dma_tx_callback;
	txdesc->callback_param = priv;

	/* request transfer */
	dmaengine_submit(txdesc);
	dma_async_issue_pending(txchan);

	return;

 err_txdesc:
	dmaengine_terminate_all(txchan);
	dma_unmap_sg(txchan->device->dev, priv->sgt_tx.sgl,
			priv->sgt_tx.nents, DMA_TO_DEVICE);
 err_tx_sgmap:
	sg_free_table(&priv->sgt_tx);

	return;
}

static void sh_msiof_spi_dma_rx(struct sh_msiof_spi_priv *priv,
			  void *rxbuf, int words, int bytes_per_word)
{
	int ret = 0;
	unsigned int count;
	unsigned int pages;
	int rx_sglen;
	struct dma_chan *rxchan = priv->dma_rx_channel;
	struct dma_async_tx_descriptor *rxdesc;

	/* check that the channel are available */
	if (!rxchan)
		return;

	count = bytes_per_word * words;

	dev_dbg(&priv->pdev->dev, "%s count=%d, rxbuf=%lx\n",
		__func__, count, (ulong)rxbuf);

	/* create and fill in scatterlists for the transfers */
	pages = DIV_ROUND_UP(count, PAGE_SIZE);

	ret = sg_alloc_table(&priv->sgt_rx, pages, GFP_ATOMIC);
	if (ret) {
		dev_err(&priv->pdev->dev, "failed to allocate scatterlist\n");
		return;
	}

	sh_msiof_spi_setup_dma_scatter(priv, rxbuf, count, &priv->sgt_rx);

	/* map DMA buffers */
	rx_sglen = dma_map_sg(rxchan->device->dev, priv->sgt_rx.sgl,
			priv->sgt_rx.nents, DMA_FROM_DEVICE);
	if (!rx_sglen) {
		ret = -ENOMEM;
		goto err_rx_sgmap;
	}

	rxdesc = dmaengine_prep_slave_sg(rxchan,
					priv->sgt_rx.sgl,
					rx_sglen,
					DMA_FROM_DEVICE,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!rxdesc) {
		ret = -ENOMEM;
		goto err_rxdesc;
	}
	rxdesc->callback = sh_msiof_spi_dma_rx_callback;
	rxdesc->callback_param = priv;

	/* request transfer */
	dmaengine_submit(rxdesc);
	dma_async_issue_pending(rxchan);

	/* Wait complete */
	wait_for_completion(&priv->dma_rx_wait);

	return;

 err_rxdesc:
	dmaengine_terminate_all(rxchan);
	dma_unmap_sg(rxchan->device->dev, priv->sgt_rx.sgl,
			priv->sgt_rx.nents, DMA_FROM_DEVICE);
 err_rx_sgmap:
	sg_free_table(&priv->sgt_rx);

	return;
}

static int sh_msiof_spi_txrx_dma(struct sh_msiof_spi_priv *p,
				  const void *tx_buf, void *rx_buf,
				  int words, int bits, int bytes_per_word)
{
	int ret;

	/* store bytes_per_word */
	p->bytes_per_word = bytes_per_word;

	/* limit maximum word transfer to rx/tx fifo size */
	if (tx_buf)
		words = min_t(int, words, p->tx_fifo_size);
	if (rx_buf)
		words = min_t(int, words, p->rx_fifo_size);

	/* setup msiof transfer mode registers */
	sh_msiof_spi_set_mode_regs(p, tx_buf, rx_buf, bits, words);

	/* request DMA transfer */
	if (tx_buf)
		sh_msiof_spi_dma_tx(p, tx_buf, words, bytes_per_word);

	/* setup clock and rx/tx signals */
	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TSCKE);

	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_RXE);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TXE);

	/* Wait complete */
	wait_for_completion(&p->dma_tx_wait);

	/* start by setting frame bit */
	INIT_COMPLETION(p->done);

	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TFSE);

	if (ret) {
		dev_err(&p->pdev->dev, "failed to start hardware\n");
		goto err;
	}

	/* wait for tx fifo to be emptied / rx fifo to be filled */
	wait_for_completion(&p->done);

	/* wait completion of DMA transfer */
	if (rx_buf)
		sh_msiof_spi_dma_rx(p, rx_buf, words, bytes_per_word);

	/* clear status bits */
	sh_msiof_reset_str(p);

	/* reset bytes_per_word */
	p->bytes_per_word = 0;

	/* shut down frame, tx/tx and clock signals */
	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = sh_msiof_modify_ctr_wait(p, CTR_TFSE, 0);

	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TXE, 0);
	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_RXE, 0);

	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TSCKE, 0);

	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		goto err;
	}

	return words;

 err:
	sh_msiof_write(p, IER, 0);
	return ret;
}

static bool sh_msiof_spi_dma_filter(struct dma_chan *chan, void *arg)
{
	struct platform_device *pdev = to_platform_device(chan->device->dev);
	if ((pdev->id != dma_devid_lo)
		&& (pdev->id != dma_devid_up))
		return false;
	chan->private = arg;
	return true;
}

static int sh_msiof_spi_setup_dma(struct sh_msiof_spi_priv *priv)
{
	struct  slave;
	dma_cap_mask_t mask;
	int ret = 0;

	if ((priv->info->dma_slave_tx == 0) || (priv->info->dma_slave_rx == 0))
		return ret;

	priv->bytes_per_word = 0;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* get DMA TX channel */
	priv->dma_tx_param.shdma_slave.slave_id = priv->info->dma_slave_tx;
	priv->dma_tx_channel = dma_request_channel(mask,
					   priv->dma_filter,
					   &priv->dma_tx_param);
	if (!priv->dma_tx_channel) {
		ret = -ENODEV;
		dev_err(&priv->pdev->dev, "failed to get TX channel\n");
		return ret;
	}

	/* get DMA RX channel */
	priv->dma_rx_param.shdma_slave.slave_id = priv->info->dma_slave_rx;
	priv->dma_rx_channel = dma_request_channel(mask,
					   priv->dma_filter,
					   &priv->dma_rx_param);
	if (!priv->dma_rx_channel) {
		ret = -ENODEV;
		dev_err(&priv->pdev->dev, "failed to get RX channel\n");
		dma_release_channel(priv->dma_tx_channel);
		priv->dma_tx_channel = NULL;
	}

	return ret;
}

static void sh_msiof_spi_release_dma(struct sh_msiof_spi_priv *priv)
{
	if (priv->dma_rx_channel) {
		dma_release_channel(priv->dma_rx_channel);
		priv->dma_rx_channel = NULL;
		sg_free_table(&priv->sgt_rx);
	}

	if (priv->dma_tx_channel) {
		dma_release_channel(priv->dma_tx_channel);
		priv->dma_tx_channel = NULL;
		sg_free_table(&priv->sgt_tx);
	}

	return;
}
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

static int sh_msiof_spi_bits(struct spi_device *spi, struct spi_transfer *t)
{
	int bits;

	bits = t ? t->bits_per_word : 0;
	if (!bits)
		bits = spi->bits_per_word;
	return bits;
}

static unsigned long sh_msiof_spi_hz(struct spi_device *spi,
				     struct spi_transfer *t)
{
	unsigned long hz;

	hz = t ? t->speed_hz : 0;
	if (!hz)
		hz = spi->max_speed_hz;
	return hz;
}

static int sh_msiof_spi_setup_transfer(struct spi_device *spi,
				       struct spi_transfer *t)
{
	int bits;

	/* noting to check hz values against since parent clock is disabled */

	bits = sh_msiof_spi_bits(spi, t);
	if (bits < 8)
		return -EINVAL;
	if (bits > 32)
		return -EINVAL;
	if (bits % 8)
		return -EINVAL;

	return spi_bitbang_setup_transfer(spi, t);
}

static void sh_msiof_spi_chipselect(struct spi_device *spi, int is_on)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(spi->master);
	int value;

	/* chip select is active low unless SPI_CS_HIGH is set */
	if (spi->mode & SPI_CS_HIGH)
		value = (is_on == BITBANG_CS_ACTIVE) ? 1 : 0;
	else
		value = (is_on == BITBANG_CS_ACTIVE) ? 0 : 1;

	if (is_on == BITBANG_CS_ACTIVE) {
		if (!test_and_set_bit(0, &p->flags)) {
			pm_runtime_get_sync(&p->pdev->dev);
			clk_enable(p->clk);
		}

		/* Configure pins before asserting CS */
		sh_msiof_spi_set_pin_regs(p, !!(spi->mode & SPI_CPOL),
					  !!(spi->mode & SPI_CPHA),
					  !!(spi->mode & SPI_3WIRE),
					  !!(spi->mode & SPI_LSB_FIRST));
	}

	/* use spi->controller data for CS (same strategy as spi_gpio) */
	if (spi->controller_data)
		gpio_set_value((unsigned)spi->controller_data, value);

	if (is_on == BITBANG_CS_INACTIVE) {
		if (test_and_clear_bit(0, &p->flags)) {
			clk_disable(p->clk);
			pm_runtime_put(&p->pdev->dev);
		}
	}
}

static int sh_msiof_spi_txrx_once(struct sh_msiof_spi_priv *p,
				  void (*tx_fifo)(struct sh_msiof_spi_priv *,
						  const void *, int, int),
				  void (*rx_fifo)(struct sh_msiof_spi_priv *,
						  void *, int, int),
				  const void *tx_buf, void *rx_buf,
				  int words, int bits)
{
	int fifo_shift;
	int ret;

	/* limit maximum word transfer to rx/tx fifo size */
	if (tx_buf)
		words = min_t(int, words, p->tx_fifo_size);
	if (rx_buf)
		words = min_t(int, words, p->rx_fifo_size);

	/* the fifo contents need shifting */
	fifo_shift = 32 - bits;

	/* setup msiof transfer mode registers */
	sh_msiof_spi_set_mode_regs(p, tx_buf, rx_buf, bits, words);

	/* write tx fifo */
	if (tx_buf)
		tx_fifo(p, tx_buf, words, fifo_shift);

	/* setup clock and rx/tx signals */
	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = sh_msiof_modify_ctr_wait(p, 0, CTR_TSCKE);
	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_RXE);
	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TXE);

	/* start by setting frame bit */
	INIT_COMPLETION(p->done);
	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, 0, CTR_TFSE);

	if (ret) {
		dev_err(&p->pdev->dev, "failed to start hardware\n");
		goto err;
	}

	/* wait for tx fifo to be emptied / rx fifo to be filled */
	wait_for_completion(&p->done);

	/* read rx fifo */
	if (rx_buf)
		rx_fifo(p, rx_buf, words, fifo_shift);

	/* clear status bits */
	sh_msiof_reset_str(p);

	/* shut down frame, tx/tx and clock signals */
	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = sh_msiof_modify_ctr_wait(p, CTR_TFSE, 0);

	ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TXE, 0);
	if (rx_buf)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_RXE, 0);

	if (p->info->mode == SPI_MSIOF_MASTER)
		ret = ret ? ret : sh_msiof_modify_ctr_wait(p, CTR_TSCKE, 0);

	if (ret) {
		dev_err(&p->pdev->dev, "failed to shut down hardware\n");
		goto err;
	}

	return words;

 err:
	sh_msiof_write(p, IER, 0);
	return ret;
}

static int sh_msiof_spi_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct sh_msiof_spi_priv *p = spi_master_get_devdata(spi->master);
	void (*tx_fifo)(struct sh_msiof_spi_priv *, const void *, int, int);
	void (*rx_fifo)(struct sh_msiof_spi_priv *, void *, int, int);
	int bits;
	int bytes_per_word;
	int bytes_done;
	int words;
	int n;
	bool swab;

	bits = sh_msiof_spi_bits(spi, t);

	if (bits <= 8 && t->len > 15 && !(t->len & 3)) {
		bits = 32;
		swab = true;
	} else {
		swab = false;
	}

	/* setup bytes per word and fifo read/write functions */
	if (bits <= 8) {
		bytes_per_word = 1;
		tx_fifo = sh_msiof_spi_write_fifo_8;
		rx_fifo = sh_msiof_spi_read_fifo_8;
	} else if (bits <= 16) {
		bytes_per_word = 2;
		if ((unsigned long)t->tx_buf & 0x01)
			tx_fifo = sh_msiof_spi_write_fifo_16u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_16;

		if ((unsigned long)t->rx_buf & 0x01)
			rx_fifo = sh_msiof_spi_read_fifo_16u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_16;
	} else if (swab) {
		bytes_per_word = 4;
		if ((unsigned long)t->tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_s32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_s32;

		if ((unsigned long)t->rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_s32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_s32;
	} else {
		bytes_per_word = 4;
		if ((unsigned long)t->tx_buf & 0x03)
			tx_fifo = sh_msiof_spi_write_fifo_32u;
		else
			tx_fifo = sh_msiof_spi_write_fifo_32;

		if ((unsigned long)t->rx_buf & 0x03)
			rx_fifo = sh_msiof_spi_read_fifo_32u;
		else
			rx_fifo = sh_msiof_spi_read_fifo_32;
	}

	if (p->info->mode == SPI_MSIOF_MASTER)
		/* setup clocks (clock already enabled in chipselect()) */
		sh_msiof_spi_set_clk_regs(p, clk_get_rate(p->clk),
					  sh_msiof_spi_hz(spi, t));

	/* transfer in fifo sized chunks */
	words = t->len / bytes_per_word;
	bytes_done = 0;

	while (bytes_done < t->len) {
		void *rx_buf = t->rx_buf ? t->rx_buf + bytes_done : NULL;
		const void *tx_buf = t->tx_buf ? t->tx_buf + bytes_done : NULL;

#if !defined(CONFIG_SPI_SH_MSIOF_DMA)
		n = sh_msiof_spi_txrx_once(p, tx_fifo, rx_fifo,
					   tx_buf,
					   rx_buf,
					   words, bits);
#else
		if (bytes_per_word == 4 && !swab)
			n = sh_msiof_spi_txrx_dma(p, tx_buf, rx_buf,
						   words, bits, bytes_per_word);
		else
			n = sh_msiof_spi_txrx_once(p, tx_fifo, rx_fifo,
						   tx_buf,
						   rx_buf,
						   words, bits);
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

		if (n < 0)
			break;

		bytes_done += n * bytes_per_word;
		words -= n;
	}

	return bytes_done;
}

static u32 sh_msiof_spi_txrx_word(struct spi_device *spi, unsigned nsecs,
				  u32 word, u8 bits)
{
	BUG(); /* unused but needed by bitbang code */
	return 0;
}

static int sh_msiof_spi_probe(struct platform_device *pdev)
{
	struct resource	*r;
	struct spi_master *master;
	struct sh_msiof_spi_priv *p;
	char clk_name[16];
	int i;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(struct sh_msiof_spi_priv));
	if (master == NULL) {
		dev_err(&pdev->dev, "failed to allocate spi master\n");
		ret = -ENOMEM;
		goto err0;
	}

	p = spi_master_get_devdata(master);

	platform_set_drvdata(pdev, p);
	p->info = pdev->dev.platform_data;
	init_completion(&p->done);

	snprintf(clk_name, sizeof(clk_name), "msiof%d", pdev->id);
	p->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(p->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i = platform_get_irq(pdev, 0);
	if (!r || i < 0) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		ret = -ENOENT;
		goto err2;
	}
	p->mapbase = ioremap_nocache(r->start, resource_size(r));
	if (!p->mapbase) {
		dev_err(&pdev->dev, "unable to ioremap\n");
		ret = -ENXIO;
		goto err2;
	}

	ret = request_irq(i, sh_msiof_spi_irq, 0,
			  dev_name(&pdev->dev), p);
	if (ret) {
		dev_err(&pdev->dev, "unable to request irq\n");
		goto err3;
	}

	p->pdev = pdev;
	pm_runtime_enable(&pdev->dev);

#if defined(CONFIG_SPI_SH_MSIOF_DMA)
	init_completion(&p->dma_tx_wait);
	init_completion(&p->dma_rx_wait);

	p->dma_filter = sh_msiof_spi_dma_filter;
	dma_devid_lo = p->info->dma_devid_lo;
	dma_devid_up = p->info->dma_devid_up;

	ret = sh_msiof_spi_setup_dma(p);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to setup dma\n");
		pm_runtime_disable(&pdev->dev);
		goto err3;
	}
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

	/* The standard version of MSIOF use 64 word FIFOs */
	p->tx_fifo_size = 64;
	p->rx_fifo_size = 64;

	/* Platform data may override FIFO sizes */
	if (p->info->tx_fifo_override)
		p->tx_fifo_size = p->info->tx_fifo_override;
	if (p->info->rx_fifo_override)
		p->rx_fifo_size = p->info->rx_fifo_override;

	/* init master and bitbang code */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
	master->mode_bits |= SPI_LSB_FIRST | SPI_3WIRE;
	master->flags = 0;
	master->bus_num = pdev->id;
	master->num_chipselect = p->info->num_chipselect;
	master->setup = spi_bitbang_setup;
	master->cleanup = spi_bitbang_cleanup;

	p->bitbang.master = master;
	p->bitbang.chipselect = sh_msiof_spi_chipselect;
	p->bitbang.setup_transfer = sh_msiof_spi_setup_transfer;
	p->bitbang.txrx_bufs = sh_msiof_spi_txrx;
	p->bitbang.txrx_word[SPI_MODE_0] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_1] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_2] = sh_msiof_spi_txrx_word;
	p->bitbang.txrx_word[SPI_MODE_3] = sh_msiof_spi_txrx_word;

	ret = spi_bitbang_start(&p->bitbang);
	if (ret == 0)
		return 0;

	pm_runtime_disable(&pdev->dev);
 err3:
	iounmap(p->mapbase);
 err2:
	clk_put(p->clk);
 err1:
	spi_master_put(master);
 err0:
	return ret;
}

static int sh_msiof_spi_remove(struct platform_device *pdev)
{
	struct sh_msiof_spi_priv *p = platform_get_drvdata(pdev);
	int ret;

#if defined(CONFIG_SPI_SH_MSIOF_DMA)
	sh_msiof_spi_release_dma(p);
#endif /* CONFIG_SPI_SH_MSIOF_DMA */

	ret = spi_bitbang_stop(&p->bitbang);
	if (!ret) {
		pm_runtime_disable(&pdev->dev);
		free_irq(platform_get_irq(pdev, 0), p);
		iounmap(p->mapbase);
		clk_put(p->clk);
		spi_master_put(p->bitbang.master);
	}
	return ret;
}

static int sh_msiof_spi_runtime_nop(struct device *dev)
{
	/* Runtime PM callback shared between ->runtime_suspend()
	 * and ->runtime_resume(). Simply returns success.
	 *
	 * This driver re-initializes all registers after
	 * pm_runtime_get_sync() anyway so there is no need
	 * to save and restore registers here.
	 */
	return 0;
}

static struct dev_pm_ops sh_msiof_spi_dev_pm_ops = {
	.runtime_suspend = sh_msiof_spi_runtime_nop,
	.runtime_resume = sh_msiof_spi_runtime_nop,
};

static struct platform_driver sh_msiof_spi_drv = {
	.probe		= sh_msiof_spi_probe,
	.remove		= sh_msiof_spi_remove,
	.driver		= {
		.name		= "spi_sh_msiof",
		.owner		= THIS_MODULE,
		.pm		= &sh_msiof_spi_dev_pm_ops,
	},
};
module_platform_driver(sh_msiof_spi_drv);

MODULE_DESCRIPTION("SuperH MSIOF SPI Master/Slave Interface Driver");
MODULE_AUTHOR("Magnus Damm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spi_sh_msiof");

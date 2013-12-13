#ifndef __SPI_SH_MSIOF_H__
#define __SPI_SH_MSIOF_H__

enum {
	SPI_MSIOF_MASTER,
	SPI_MSIOF_SLAVE,
};

struct sh_msiof_spi_info {
	int tx_fifo_override;
	int rx_fifo_override;
	u16 num_chipselect;
	int mode;
	int dma_slave_tx;
	int dma_slave_rx;
	int dma_devid_lo;
	int dma_devid_up;
};

#endif /* __SPI_SH_MSIOF_H__ */

/*
 * PCI Express definitions for Renesas R-Car SoCs
 */
#ifndef __PCI_RCAR_H
#define __PCI_RCAR_H

#define PCI_DEVICE_ID_RENESAS_RCAR	0x0018

#define PCIECAR			0x000010
#define PCIECCTLR		0x000018
#define  CONFIG_SEND_ENABLE	(1 << 31)
#define  TYPE0			(0 << 8)
#define  TYPE1			(1 << 8)
#define PCIECDR			0x000020
#define PCIEMSR			0x000028
#define PCIEINTXR		0x000400
#define PCIEPHYSR		0x0007f0
#define PCIEMSITXR		0x000840

/* Transfer control */
#define PCIETCTLR		0x02000
#define  CFINIT			1
#define PCIETSTR		0x02004
#define  DATA_LINK_ACTIVE	1
#define PCIEINTR		0x02008
#define PCIEINTER		0x0200c
#define PCIEERRFR		0x02020
#define  UNSUPPORTED_REQUEST	(1 << 4)
#define PCIEERRFER		0x02024
#define PCIEERRFR2		0x02028
#define PCIETIER		0x02030
#define PCIEPMSR		0x02034
#define PCIEPMSCIER		0x02038
#define PCIEMSIFR		0x02044
#define PCIEMSIALR		0x02048
#define  MSIFE			1
#define PCIEMSIAUR		0x0204c
#define PCIEMSIIER		0x02050

/* root port address */
#define PCIEPRAR(x)		(0x02080 + ((x) * 0x4))

/* local address reg & mask */
#define PCIELAR(x)		(0x02200 + ((x) * 0x20))
#define PCIELAMR(x)		(0x02208 + ((x) * 0x20))
#define  LAM_PMIOLAMnB3		(1 << 3)
#define  LAM_PMIOLAMnB2		(1 << 2)
#define  LAM_PREFETCH		(1 << 3)
#define  LAM_64BIT		(1 << 2)
#define  LAR_ENABLE		(1 << 1)

/* PCIe address reg & mask */
#define PCIEPARL(x)		(0x03400 + ((x) * 0x20))
#define PCIEPARH(x)		(0x03404 + ((x) * 0x20))
#define PCIEPAMR(x)		(0x03408 + ((x) * 0x20))
#define PCIEPTCTLR(x)		(0x0340c + ((x) * 0x20))
#define  PAR_ENABLE		(1 << 31)
#define  IO_SPACE		(1 << 8)

/* Configuration */
#define PCICONF(x)		(0x010000 + ((x) * 0x4))
#define PMCAP(x)		(0x010040 + ((x) * 0x4))
#define MSICAP(x)		(0x010050 + ((x) * 0x4))
#define EXPCAP(x)		(0x010070 + ((x) * 0x4))
#define VCCAP(x)		(0x010100 + ((x) * 0x4))
#define SERNUMCAP(x)		(0x0101b0 + ((x) * 0x4))

/* link layer */
#define IDSETR0			0x011000
#define IDSETR1			0x011004
#define SUBIDSETR		0x011024
#define DSERSETR0		0x01102c
#define DSERSETR1		0x011030
#define TLCTLR			0x011048
#define MACSR			0x011054
#define MACCTLR			0x011058
#define PMSR			0x01105c
#define PMCTLR			0x011060
#define  SCRAMBLE_DISABLE	(1 << 27)
#define MACINTENR		0x01106c
#define PMINTENR		0x011070

/* R-Car H1 PHY */
#define H1_PCIEPHYCTLR		0x040008
#define H1_PCIEPHYADRR		0x04000c
#define  WRITE_CMD		(1 << 16)
#define  PHY_ACK		(1 << 24)
#define  RATE_POS		12
#define  LANE_POS		8
#define  ADR_POS		0
#define H1_PCIEPHYDOUTR		0x040014
#define H1_PCIEPHYSR		0x040018

#endif /* __PCI_RCAR_H */

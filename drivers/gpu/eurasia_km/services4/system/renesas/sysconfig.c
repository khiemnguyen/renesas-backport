/*************************************************************************/ /*!
@Title          System Description
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
                Copyright (C) 2012-2013 Renesas Electronics Corporation
@Description    System Description functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include "sgxdefs.h"
#include "services_headers.h"
#include "kerneldisplay.h"
#include "oemfuncs.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "syslocal.h"

#if defined(__linux__)
#include <linux/version.h>
#include <linux/module.h>

#if defined(LDM_PLATFORM)
#include <linux/platform_device.h>
#endif
#if defined(CONFIG_CMA)
#include <linux/dma-mapping.h>
#endif

#include "mm.h"
#endif /* defined(__linux__) */

/* Defines for SGX core's clock */
extern IMG_UINT32 SGXCoreClock;
#define ui32SysSGXClockSpeed	SGXCoreClock

/* top level system data anchor point*/
SYS_DATA* gpsSysData = (SYS_DATA*)IMG_NULL;
SYS_DATA  gsSysData;

static SYS_SPECIFIC_DATA gsSysSpecificData;
SYS_SPECIFIC_DATA *gpsSysSpecificData;

/* SGX structures */
static IMG_UINT32		gui32SGXDeviceID;
static SGX_DEVICE_MAP	gsSGXDeviceMap;

#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
/* The following is exported by the Linux module code */
extern struct platform_device *gpsPVRLDMDev;
#endif

static PVRSRV_DEVICE_NODE	*gpsSGXDevNode;

IMG_UINT32 SGXDeviceMemoryAddr = 0;
IMG_UINT32 SGXDeviceMemorySize = 0;
#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
/* LMA device memory resources */
module_param(SGXDeviceMemoryAddr, uint, S_IRUGO);
module_param(SGXDeviceMemorySize, uint, S_IRUGO);
#endif /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */

#if defined(NO_HARDWARE)
/* mimic register block with contiguous memory */
static IMG_CPU_VIRTADDR gsSGXRegsCPUVAddr;
#endif

IMG_UINT32 PVRSRV_BridgeDispatchKM( IMG_UINT32  Ioctl,
									IMG_BYTE   *pInBuf,
									IMG_UINT32  InBufLen,
									IMG_BYTE   *pOutBuf,
									IMG_UINT32  OutBufLen,
									IMG_UINT32 *pdwBytesTransferred);

static INLINE PVRSRV_ERROR EnableSGXClocksWrap(SYS_DATA *psSysData)
{
	return EnableSGXClocks(psSysData);
}

static INLINE PVRSRV_ERROR EnableSystemClocksWrap(SYS_DATA *psSysData)
{
	PVRSRV_ERROR eError = EnableSystemClocks(psSysData);

#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	if(eError == PVRSRV_OK)
	{
		eError = EnableSGXClocksWrap(psSysData);
		if (eError != PVRSRV_OK)
		{
			DisableSystemClocks(psSysData);
		}
	}
#endif

	return eError;
}

static INLINE IMG_VOID DisableSGXClocksWrap(SYS_DATA *psSysData)
{
	DisableSGXClocks(psSysData);
}

static INLINE IMG_VOID DisableSystemClocksWrap(SYS_DATA *psSysData)
{
#if !defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	DisableSGXClocks(psSysData);
#endif

	DisableSystemClocks(psSysData);
}

/*!
******************************************************************************

 @Function	SysLocateDevices

 @Description specifies devices in the systems memory map

 @Input    psSysData - sys data

 @Return   PVRSRV_ERROR  :

******************************************************************************/
static PVRSRV_ERROR SysLocateDevices(SYS_DATA *psSysData)
{
	IMG_UINT32 ui32SGXRegBaseAddr, ui32SGXRegSize;
	IMG_UINT32 ui32IRQ;

#if defined(NO_HARDWARE)
	PVRSRV_ERROR eError;
	IMG_CPU_PHYADDR sCpuPAddr;
#else

#if defined(LDM_PLATFORM) && defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	struct resource *dev_res;
	int dev_irq;
#endif

#endif /* defined(NO_HARDWARE) */

	/* SGX Device: */
	gsSGXDeviceMap.ui32Flags = 0x0;

#if defined(NO_HARDWARE)
	/* No hardware registers */
	eError = OSBaseAllocContigMemory(SYS_SGX_REG_SIZE,
										&gsSGXRegsCPUVAddr,
										&sCpuPAddr);
	if(eError != PVRSRV_OK)
	{
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_REG_MEM);
	gsSGXDeviceMap.sRegsCpuPBase = sCpuPAddr;

	OSMemSet(gsSGXRegsCPUVAddr, 0, SYS_SGX_REG_SIZE);

#if defined(__linux__)
	/* Indicate the registers are already mapped */
	gsSGXDeviceMap.pvRegsCpuVBase = gsSGXRegsCPUVAddr;
#else
	/*
	 * FIXME: Could we just use the virtual address returned by
	 * OSBaseAllocContigMemory?
	 */
	gsSGXDeviceMap.pvRegsCpuVBase = IMG_NULL;
#endif /* defined(__linux__) */

#else	/* defined(NO_HARDWARE) */

#if defined(LDM_PLATFORM) && defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	dev_res = platform_get_resource(gpsPVRLDMDev, IORESOURCE_MEM, 0);
	if (dev_res == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_resource failed", __FUNCTION__));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	dev_irq = platform_get_irq(gpsPVRLDMDev, 0);
	if (dev_irq < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: platform_get_irq failed (%d)", __FUNCTION__, -dev_irq));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	ui32SGXRegBaseAddr = dev_res->start;
	PVR_TRACE(("SGX register base: 0x%lx", (unsigned long)ui32SGXRegBaseAddr));

	ui32SGXRegSize = (unsigned int)(dev_res->end - dev_res->start);
	PVR_TRACE(("SGX register size: %d",ui32SGXRegSize));

	ui32IRQ = dev_irq;
	PVR_TRACE(("SGX IRQ: %d", ui32IRQ));
#else /* defined(LDM_PLATFORM) && defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

	ui32SGXRegBaseAddr = SYS_SGX_REG_BASE;
	ui32SGXRegSize = SYS_SGX_REG_SIZE;
	ui32IRQ = SYS_SGX_IRQ;

#endif /* defined(LDM_PLATFORM) && defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

	/* Hardware registers */
	gsSGXDeviceMap.sRegsSysPBase.uiAddr = ui32SGXRegBaseAddr;

#endif	/* defined(NO_HARDWARE) */

	/* Common register setup */
	gsSGXDeviceMap.sRegsCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sRegsSysPBase);
	gsSGXDeviceMap.ui32RegsSize = ui32SGXRegSize;

	/* Local Memory setup: (if present) */
#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
	SGXDeviceMemoryAddr = HOST_PAGEALIGN(SGXDeviceMemoryAddr);
	SGXDeviceMemorySize = SGXDeviceMemorySize / HOST_PAGESIZE() * HOST_PAGESIZE();

	if((SGXDeviceMemoryAddr != 0) && (SGXDeviceMemorySize != 0))
	{
		/* Local Device Memory Region: (present) */
		PVR_LOG(("SGX use fixed device memory.  SGXDeviceMemoryAddr=0x%x SGXDeviceMemorySize=0x%x",
				SGXDeviceMemoryAddr, SGXDeviceMemorySize));

		gsSGXDeviceMap.sLocalMemSysPBase.uiAddr = SGXDeviceMemoryAddr;
		gsSGXDeviceMap.sLocalMemDevPBase = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, gsSGXDeviceMap.sLocalMemSysPBase);
		gsSGXDeviceMap.sLocalMemCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sLocalMemSysPBase);
		gsSGXDeviceMap.ui32LocalMemSize = SGXDeviceMemorySize;
	}
#if defined(CONFIG_CMA)
	else if((SGXDeviceMemoryAddr == 0) && (SGXDeviceMemorySize != 0))
	{
		/* Local Device Memory Region: (dynamic allocated) */
		dma_addr_t dma;
		IMG_VOID *pvLinAddr;

		pvLinAddr = dma_alloc_coherent(NULL, SGXDeviceMemorySize, &dma, GFP_KERNEL);
		if (pvLinAddr == IMG_NULL)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		PVR_LOG(("SGX use DMA device memory.  DMADeviceMemoryAddr=0x%x SGXDeviceMemorySize=0x%x",
				dma, SGXDeviceMemorySize));

		gsSGXDeviceMap.sLocalMemSysPBase.uiAddr = dma;
		gsSGXDeviceMap.sLocalMemDevPBase = SysSysPAddrToDevPAddr(PVRSRV_DEVICE_TYPE_SGX, gsSGXDeviceMap.sLocalMemSysPBase);
		gsSGXDeviceMap.sLocalMemCpuPBase = SysSysPAddrToCpuPAddr(gsSGXDeviceMap.sLocalMemSysPBase);
		gsSGXDeviceMap.ui32LocalMemSize = SGXDeviceMemorySize;

		gsSysSpecificData.dma = dma;
		gsSysSpecificData.pvLinAddr = pvLinAddr;
		gsSysSpecificData.ui32Size = SGXDeviceMemorySize;
		SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_CMA_MEMMAP);
	}
#endif /* defined(CONFIG_CMA) */
	else
#endif /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */
	{
		/*
			Local Device Memory Region: (not present)
			Note: the device doesn't need to know about its memory
			but keep info here for now
		*/
		PVR_LOG(("SGX use system memory."));
		gsSGXDeviceMap.sLocalMemSysPBase.uiAddr = 0;
		gsSGXDeviceMap.sLocalMemDevPBase.uiAddr = 0;
		gsSGXDeviceMap.sLocalMemCpuPBase.uiAddr = 0;
		gsSGXDeviceMap.ui32LocalMemSize = 0;
	}

	/* device interrupt IRQ */
	gsSGXDeviceMap.ui32IRQ = ui32IRQ;

#if defined(PDUMP)
	{
		/* Initialise memory region name for pdumping.
		 * This could just as well be SGXMEM on the RefPCI Test Chip system
		 * since it uses LMA. But since there will only be 1 device we can
		 * represent it as UMA in the pdump.
		 */
		static IMG_CHAR pszPDumpDevName[] = "SGXMEM";
		gsSGXDeviceMap.pszPDumpDevName = pszPDumpDevName;
	}
#endif
	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SysInitialise

 @Description Initialises kernel services at 'driver load' time

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR SysInitialise(IMG_VOID)
{
	IMG_UINT32			i;
	PVRSRV_ERROR 		eError;
	PVRSRV_DEVICE_NODE	*psDeviceNode;
#if !defined(SGX_DYNAMIC_TIMING_INFO)
	SGX_TIMING_INFORMATION* psTimingInfo;
#endif

	gpsSysData = &gsSysData;
	OSMemSet(gpsSysData, 0, sizeof(SYS_DATA));

	gpsSysSpecificData =  &gsSysSpecificData;
	OSMemSet(gpsSysSpecificData, 0, sizeof(SYS_SPECIFIC_DATA));

	gpsSysData->pvSysSpecificData = gpsSysSpecificData;

	eError = OSInitEnvData(&gpsSysData->pvEnvSpecificData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to setup env structure"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA);

#if !defined(SGX_DYNAMIC_TIMING_INFO)
	/* Set up timing information*/
	psTimingInfo = &gsSGXDeviceMap.sTimingInfo;
	psTimingInfo->ui32CoreClockSpeed = ui32SysSGXClockSpeed;
	psTimingInfo->ui32HWRecoveryFreq = SYS_SGX_HWRECOVERY_TIMEOUT_FREQ;
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	psTimingInfo->bEnableActivePM = IMG_TRUE;
#else
	psTimingInfo->bEnableActivePM = IMG_FALSE;
#endif /* SUPPORT_ACTIVE_POWER_MANAGEMENT */
	psTimingInfo->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS;
	psTimingInfo->ui32uKernelFreq = SYS_SGX_PDS_TIMER_FREQ;
#endif /* !defined(SGX_DYNAMIC_TIMING_INFO) */

	gpsSysData->ui32NumDevices = SYS_DEVICE_COUNT;

	/* init device ID's */
	for(i=0; i<SYS_DEVICE_COUNT; i++)
	{
		gpsSysData->sDeviceID[i].uiID = i;
		gpsSysData->sDeviceID[i].bInUse = IMG_FALSE;
	}

	gpsSysData->psDeviceNodeList = IMG_NULL;
	gpsSysData->psQueueList = IMG_NULL;

	eError = SysInitialiseCommon(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed in SysInitialiseCommon"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}

	/*
		Locate the devices within the system, specifying
		the physical addresses of each devices components
		(regs, mem, ports etc.)
	*/
	eError = SysLocateDevices(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to locate devices"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LOCATEDEV);

	eError = SysPMRuntimeRegister();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register with OSPM!"));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_PM_RUNTIME);

	/*
		Register devices with the system
		This also sets up their memory maps/heaps
	*/
	eError = PVRSRVRegisterDevice(gpsSysData, SGXRegisterDevice,
								  DEVICE_SGX_INTERRUPT, &gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to register device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_REGDEV);

	/*
		A given SOC may have 0 to n local device memory blocks.
		It is assumed device local memory blocks will be either
		specific to each device or shared among one or more services
		managed devices.
		Note: no provision is made for a single device having more
		than one local device memory blocks. If required we must
		add new types,
		e.g.PVRSRV_BACKINGSTORE_LOCALMEM_NONCONTIG0
			PVRSRV_BACKINGSTORE_LOCALMEM_NONCONTIG1
	*/

#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
	/*
		Create Local Device Page Managers for each device memory block.
		We'll use an RA to do the management
		(only one for the emulator)
	*/
	if (gsSGXDeviceMap.sLocalMemSysPBase.uiAddr != 0 && gsSGXDeviceMap.ui32LocalMemSize != 0)
	{
		gpsSysData->apsLocalDevMemArena[0] = RA_Create ("SGXLocalDeviceMemory",
											gsSGXDeviceMap.sLocalMemSysPBase.uiAddr,
											gsSGXDeviceMap.ui32LocalMemSize,
											IMG_NULL,
											HOST_PAGESIZE(),
											IMG_NULL,
											IMG_NULL,
											IMG_NULL,
											IMG_NULL);

		if (gpsSysData->apsLocalDevMemArena[0] == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to create local dev mem allocator!"));
			SysDeinitialise(gpsSysData);
			gpsSysData = IMG_NULL;
			return eError;
		}
		SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_RA_ARENA);
	}
#endif /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */

	/*
		Once all devices are registered, specify the backing store
		and, if required, customise the memory heap config
	*/
	psDeviceNode = gpsSysData->psDeviceNodeList;
	while(psDeviceNode)
	{
		/* perform any OEM SOC address space customisations here */
		switch(psDeviceNode->sDevId.eDeviceType)
		{
			case PVRSRV_DEVICE_TYPE_SGX:
			{
				DEVICE_MEMORY_INFO *psDevMemoryInfo;
				DEVICE_MEMORY_HEAP_INFO *psDeviceMemoryHeap;

#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
				/* specify the backing store to use for the device's MMU PT/PDs */
				if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_RA_ARENA))
				{
					psDeviceNode->psLocalDevMemArena = gpsSysData->apsLocalDevMemArena[0];
				}
				else
#endif
				{
					psDeviceNode->psLocalDevMemArena = IMG_NULL;
				}

				/* useful pointers */
				psDevMemoryInfo = &psDeviceNode->sDevMemoryInfo;
				psDeviceMemoryHeap = psDevMemoryInfo->psDeviceMemoryHeap;

				/* specify the backing store for all SGX heaps */
				for(i=0; i<psDevMemoryInfo->ui32HeapCount; i++)
				{
#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
					if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_RA_ARENA))
					{
						/*
							specify the backing store type
							(all local device mem noncontig) for test chip
						*/
						psDeviceMemoryHeap[i].ui32Attribs |= PVRSRV_BACKINGSTORE_LOCALMEM_CONTIG;

						/*
							map the device memory allocator(s) onto
							the device memory heaps as required
						*/
						psDeviceMemoryHeap[i].psLocalDevMemArena = gpsSysData->apsLocalDevMemArena[0];
					}
					else
#endif
					{
						psDeviceMemoryHeap[i].ui32Attribs |= PVRSRV_BACKINGSTORE_SYSMEM_NONCONTIG;
					}

#ifdef OEM_CUSTOMISE
					/* if required, modify the memory config */
#endif
				}

				gpsSGXDevNode = psDeviceNode;
				break;
			}
			default:
				break;
		}

		/* advance to next device */
		psDeviceNode = psDeviceNode->psNext;
	}

	eError = EnableSystemClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable system clocks (%d)", eError));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = EnableSGXClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to Enable SGX clocks (%d)", eError));
		(IMG_VOID)SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
#endif

	/*
		Initialise all devices 'managed' by services:
	*/
	eError = PVRSRVInitialiseDevice (gui32SGXDeviceID);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysInitialise: Failed to initialise device!"));
		SysDeinitialise(gpsSysData);
		gpsSysData = IMG_NULL;
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_INITDEV);

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	DisableSGXClocksWrap(gpsSysData);
#endif

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SysFinalise

 @Description Final part of system initialisation.

 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR SysFinalise(IMG_VOID)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	eError = EnableSGXClocksWrap(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to Enable SGX clocks (%d)", eError));
		return eError;
	}
#endif

	eError = OSInstallMISR(gpsSysData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"OSInstallMISR: Failed to install MISR"));
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_MISR);

#if defined(SYS_USING_INTERRUPTS)
	/* install a device ISR */
	eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"OSInstallDeviceLISR: Failed to install ISR"));
		return eError;
	}
	SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
#endif /* defined(SYS_USING_INTERRUPTS) */

#if defined(__linux__)
	/* Create a human readable version string for this system */
	gpsSysData->pszVersionString = SysCreateVersionString(gsSGXDeviceMap.sRegsCpuPBase);
	if (!gpsSysData->pszVersionString)
	{
		PVR_DPF((PVR_DBG_ERROR,"SysFinalise: Failed to create a system version string"));
	}
	else
	{
		PVR_DPF((PVR_DBG_MESSAGE, "SysFinalise: Version string: %s", gpsSysData->pszVersionString));
	}
#endif /* defined(__linux__) */

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	DisableSGXClocksWrap(gpsSysData);
#endif

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SysDeinitialise

 @Description De-initialises kernel services at 'driver unload' time


 @Return   PVRSRV_ERROR  :

******************************************************************************/
PVRSRV_ERROR SysDeinitialise (SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA * psSysSpecData;
	PVRSRV_ERROR eError;

	if (psSysData == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SysDeinitialise: Called with NULL SYS_DATA pointer.  Probably called before."));
		return PVRSRV_OK;
	}

	psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

#if defined(SYS_USING_INTERRUPTS)
	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_LISR))
	{
		eError = OSUninstallDeviceLISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallDeviceLISR failed"));
			return eError;
		}
	}
#endif /* defined(SYS_USING_INTERRUPTS) */

	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_MISR))
	{
		eError = OSUninstallMISR(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: OSUninstallMISR failed"));
			return eError;
		}
	}

	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_INITDEV))
	{
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
		PVR_ASSERT(SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS));

		eError = EnableSGXClocksWrap(psSysData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: EnableSGXClocks failed"));
			return eError;
		}
#endif

		/* de-initialise all services managed devices */
		eError = PVRSRVDeinitialiseDevice (gui32SGXDeviceID);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init the device"));
			return eError;
		}
	}

	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
	{
		DisableSystemClocksWrap(gpsSysData);
	}

        if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_PM_RUNTIME))
        {
		eError = SysPMRuntimeUnregister();
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: Failed to unregister with OSPM!"));
			(IMG_VOID)SysDeinitialise(psSysData);
			gpsSysData = IMG_NULL;
			return eError;
		}
	}

	/*
		Destroy the local memory arena.
	*/
	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_RA_ARENA))
	{
		RA_Delete(gpsSysData->apsLocalDevMemArena[0]);
		gpsSysData->apsLocalDevMemArena[0] = IMG_NULL;

#if defined(SUPPORT_FIXED_DEVICE_MEMORY)
		SGXDeviceMemoryAddr = 0;
		SGXDeviceMemorySize = 0;
#endif /* defined(SUPPORT_FIXED_DEVICE_MEMORY) */
	}

#if defined(CONFIG_CMA)
	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_CMA_MEMMAP))
	{
		dma_free_coherent(NULL, psSysSpecData->ui32Size, psSysSpecData->pvLinAddr, psSysSpecData->dma);

	}
#endif

	SysDeinitialiseCommon(gpsSysData);

	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_ENVDATA))
	{
		eError = OSDeInitEnvData(gpsSysData->pvEnvSpecificData);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"SysDeinitialise: failed to de-init env structure"));
			return eError;
		}
	}


#if defined(NO_HARDWARE)
	if (SYS_SPECIFIC_DATA_TEST(psSysSpecData, SYS_SPECIFIC_DATA_ENABLE_REG_MEM))
	{
		OSBaseFreeContigMemory(SYS_SGX_REG_SIZE, gsSGXRegsCPUVAddr, gsSGXDeviceMap.sRegsCpuPBase);
	}
#endif

	psSysSpecData->ui32SysSpecificData = 0;

	gpsSysData = IMG_NULL;


	return PVRSRV_OK;
}




/*!
******************************************************************************

 @Function      SysGetDeviceMemoryMap

 @Description	returns a device address map for the specified device

 @Input		eDeviceType - device type
 @Input		ppvDeviceMap - void ptr to receive device specific info.

 @Return        PVRSRV_ERROR

******************************************************************************/
PVRSRV_ERROR SysGetDeviceMemoryMap(PVRSRV_DEVICE_TYPE eDeviceType,
									IMG_VOID **ppvDeviceMap)
{

	switch(eDeviceType)
	{
		case PVRSRV_DEVICE_TYPE_SGX:
		{
			/* just return a pointer to the structure */
			*ppvDeviceMap = (IMG_VOID*)&gsSGXDeviceMap;
			break;
		}
		default:
		{
			PVR_DPF((PVR_DBG_ERROR,"SysGetDeviceMemoryMap: unsupported device type"));
		}
	}
	return PVRSRV_OK;
}

#if defined(PVR_LMA)
/*!
******************************************************************************
 @Function     SysVerifyCpuPAddrToDevPAddr

 @Description  Verify that it is valid to compute a device physical
		    address from a given cpu physical address.

 @Input        cpu_paddr - cpu physical address.
 @Input        eDeviceType - device type required if DevPAddr
	                     address spaces vary across devices in the same
			     system.

@Return        IMG_TRUE or IMG_FALSE

******************************************************************************/
IMG_BOOL SysVerifyCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType,
										IMG_CPU_PHYADDR CpuPAddr)
{
	/*
		It is assumed the translation from cpu/system to device
		physical addresses is constant for every 'services managed'
		device in a given system
	*/
	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	/*
		just dip into the SGX info here
		(it's really adapter memory shared across all devices in
		the adapter)
	*/
	if (SGXDeviceMemorySize == 0)
	{
		return IMG_TRUE;
	}
	else
	{
		return ((CpuPAddr.uiAddr >= gsSGXDeviceMap.sLocalMemCpuPBase.uiAddr) &&
			(CpuPAddr.uiAddr < gsSGXDeviceMap.sLocalMemCpuPBase.uiAddr + SGXDeviceMemorySize))
		 	? IMG_TRUE : IMG_FALSE;
	}
}
#endif

/*!
******************************************************************************
 @Function     SysCpuPAddrToDevPAddr

 @Description  Compute a device physical address from a cpu physical
	            address.

 @Input        cpu_paddr - cpu physical address.
 @Input        eDeviceType - device type required if DevPAddr
	                     address spaces vary across devices in the same
			     system
 @Return       device physical address.

******************************************************************************/
IMG_DEV_PHYADDR SysCpuPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType,
										IMG_CPU_PHYADDR CpuPAddr)
{
	/*
		It is assumed the translation from cpu/system to device
		physical addresses is constant for every 'services managed'
		device in a given system
	*/
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	/*
		just dip into the SGX info here
		(it's really adapter memory shared across all devices in
		the adapter)
	*/
	DevPAddr.uiAddr = CpuPAddr.uiAddr;

	return DevPAddr;
}

/*!
******************************************************************************
 @Function     SysSysPAddrToCpuPAddr

 @Description  Compute a cpu physical address from a system physical
	            address.

 @Input        sys_paddr - system physical address.
 @Return       cpu physical address.

******************************************************************************/
IMG_CPU_PHYADDR SysSysPAddrToCpuPAddr (IMG_SYS_PHYADDR sys_paddr)
{
	IMG_CPU_PHYADDR cpu_paddr;

	/* This would only be an inequality if the CPU's MMU did not point to sys address 0,
	   ie. multi CPU system */
	cpu_paddr.uiAddr = sys_paddr.uiAddr;

	return cpu_paddr;
}

/*!
******************************************************************************
 @Function     SysCpuPAddrToSysPAddr

 @Description  Compute a system physical address from a cpu physical
	            address.

 @Input        cpu_paddr - cpu physical address.
 @Return       device physical address.

******************************************************************************/
IMG_SYS_PHYADDR SysCpuPAddrToSysPAddr (IMG_CPU_PHYADDR cpu_paddr)
{
	IMG_SYS_PHYADDR sys_paddr;

	/* This would only be an inequality if the CPU's MMU did not point to sys address 0,
	   ie. multi CPU system */
	sys_paddr.uiAddr = cpu_paddr.uiAddr;

	return sys_paddr;
}


#if defined(PVR_LMA)
/*!
******************************************************************************
 @Function     SysVerifySysPAddrToDevPAddr

 @Description  Verify that a device physical address can be obtained
		    from a given system physical address.

 @Input        SysPAddr - system physical address.
 @Input        eDeviceType - device type required if DevPAddr
		         address spaces vary across devices in the same system.

 @Return       IMG_TRUE or IMG_FALSE
******************************************************************************/
IMG_BOOL SysVerifySysPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr)
{
	/*
		It is assumed the translation from cpu/system to device
		physical addresses is constant for every 'services managed'
		device in a given system
	*/
	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	/*
		just dip into the SGX info here
		(it's really adapter memory shared across all devices in the
		adapter)
	*/

	if (SGXDeviceMemorySize == 0)
	{
		return IMG_TRUE;
	}
	else
	{
		return ((SysPAddr.uiAddr >= gsSGXDeviceMap.sLocalMemSysPBase.uiAddr) &&
			(SysPAddr.uiAddr < gsSGXDeviceMap.sLocalMemSysPBase.uiAddr + SGXDeviceMemorySize))
		  	? IMG_TRUE : IMG_FALSE;
	}
}
#endif

/*!
******************************************************************************
 @Function     SysSysPAddrToDevPAddr

 @Description  Compute a device physical address from a system physical
	            address.

 @Input        SysPAddr - system physical address.
 @Input        eDeviceType - device type required if DevPAddr
			 address spaces vary across devices in the same system.

 @Return       Device physical address.
******************************************************************************/
IMG_DEV_PHYADDR SysSysPAddrToDevPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_SYS_PHYADDR SysPAddr)
{
	/*
		It is assumed the translation from cpu/system to device
		physical addresses is constant for every 'services managed'
		device in a given system
	*/
	IMG_DEV_PHYADDR DevPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	/*
		just dip into the SGX info here
		(it's really adapter memory shared across all devices in the adapter)
	*/
	DevPAddr.uiAddr = SysPAddr.uiAddr;

	return DevPAddr;
}


/*!
******************************************************************************
 @Function     SysDevPAddrToSysPAddr

 @Description  Compute a device physical address from a system physical
	            address.

 @Input        DevPAddr - device physical address.
 @Input        eDeviceType - device type required if DevPAddr
									address spaces vary across devices
									in the same system

 @Return       System physical address.
******************************************************************************/
IMG_SYS_PHYADDR SysDevPAddrToSysPAddr (PVRSRV_DEVICE_TYPE eDeviceType, IMG_DEV_PHYADDR DevPAddr)
{
    IMG_SYS_PHYADDR SysPAddr;

	PVR_UNREFERENCED_PARAMETER(eDeviceType);

	/*
		just dip into the SGX info here
		(it's really adapter memory shared across all devices in the adapter)
	*/
    SysPAddr.uiAddr = DevPAddr.uiAddr;

    return SysPAddr;
}


/*****************************************************************************
 FUNCTION	: SysRegisterExternalDevice

 PURPOSE	: Called when a 3rd party device registers with services

 PARAMETERS: In:  psDeviceNode - the new device node.

***************************************************************************/
IMG_VOID SysRegisterExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


/*****************************************************************************
 FUNCTION	: SysRemoveExternalDevice

 PURPOSE	: Called when a 3rd party device unregisters from services

 PARAMETERS: In:  psDeviceNode - the device node being removed.

*****************************************************************************/
IMG_VOID SysRemoveExternalDevice(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psDeviceNode);
}


/*****************************************************************************
 FUNCTION	: SysOEMFunction

 PURPOSE	: marshalling function for custom OEM functions

 PARAMETERS	: ui32ID  - function ID
			  pvIn - in data
			  pvOut - out data

 RETURNS	: PVRSRV_ERROR
*****************************************************************************/
PVRSRV_ERROR SysOEMFunction (	IMG_UINT32	ui32ID,
								IMG_VOID	*pvIn,
								IMG_UINT32  ulInSize,
								IMG_VOID	*pvOut,
								IMG_UINT32	ulOutSize)
{
	PVR_UNREFERENCED_PARAMETER(ulInSize);
	PVR_UNREFERENCED_PARAMETER(pvIn);

	if ((ui32ID == OEM_GET_EXT_FUNCS) &&
		(ulOutSize == sizeof(PVRSRV_DC_OEM_JTABLE)))
	{
		PVRSRV_DC_OEM_JTABLE *psOEMJTable = (PVRSRV_DC_OEM_JTABLE*)pvOut;
		psOEMJTable->pfnOEMBridgeDispatch = &PVRSRV_BridgeDispatchKM;
		return PVRSRV_OK;
	}

	return PVRSRV_ERROR_INVALID_PARAMS;
}

static PVRSRV_ERROR SysMapInRegisters(IMG_VOID)
{
	PVRSRV_DEVICE_NODE *psDeviceNodeList;

	psDeviceNodeList = gpsSysData->psDeviceNodeList;

	while (psDeviceNodeList)
	{
		PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNodeList->pvDevice;
		if (psDeviceNodeList->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_SGX)
		{
#if defined(NO_HARDWARE) && defined(__linux__)
			/*
			 * SysLocateDevices will have reallocated the dummy
			 * registers.
			 */
			PVR_ASSERT(gsSGXRegsCPUVAddr);

			psDevInfo->pvRegsBaseKM = gsSGXRegsCPUVAddr;
#else
			if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNMAP_SGX_REGS))
			{
				/* Remap Regs */
				psDevInfo->pvRegsBaseKM = OSMapPhysToLin(gsSGXDeviceMap.sRegsCpuPBase,
									 gsSGXDeviceMap.ui32RegsSize,
									 PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
									 IMG_NULL);

				if (!psDevInfo->pvRegsBaseKM)
				{
					PVR_DPF((PVR_DBG_ERROR,"SysMapInRegisters : Failed to map in regs\n"));
					return PVRSRV_ERROR_BAD_MAPPING;
				}
				SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNMAP_SGX_REGS);
			}
#endif	/* #if defined(NO_HARDWARE) && defined(__linux__) */

			psDevInfo->ui32RegSize   = gsSGXDeviceMap.ui32RegsSize;
			psDevInfo->sRegsPhysBase = gsSGXDeviceMap.sRegsSysPBase;
		}

		psDeviceNodeList = psDeviceNodeList->psNext;
	}

	return PVRSRV_OK;
}


static PVRSRV_ERROR SysUnmapRegisters(IMG_VOID)
{
	PVRSRV_DEVICE_NODE *psDeviceNodeList;

	psDeviceNodeList = gpsSysData->psDeviceNodeList;

	while (psDeviceNodeList)
	{
		PVRSRV_SGXDEV_INFO *psDevInfo = (PVRSRV_SGXDEV_INFO *)psDeviceNodeList->pvDevice;
		if (psDeviceNodeList->sDevId.eDeviceType == PVRSRV_DEVICE_TYPE_SGX)
		{

#if !(defined(NO_HARDWARE) && defined(__linux__))
			/* Unmap Regs */
			if (psDevInfo->pvRegsBaseKM)
			{
				OSUnMapPhysToLin(psDevInfo->pvRegsBaseKM,
				                 gsSGXDeviceMap.ui32RegsSize,
				                 PVRSRV_HAP_KERNEL_ONLY|PVRSRV_HAP_UNCACHED,
				                 IMG_NULL);

				SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNMAP_SGX_REGS);
			}
#endif	/* #if !(defined(NO_HARDWARE) && defined(__linux__)) */

			psDevInfo->pvRegsBaseKM = IMG_NULL;
			psDevInfo->ui32RegSize          = 0;
			psDevInfo->sRegsPhysBase.uiAddr = 0;
		}

		psDeviceNodeList = psDeviceNodeList->psNext;
	}

#if defined(NO_HARDWARE)

	PVR_ASSERT(gsSGXRegsCPUVAddr);
	OSBaseFreeContigMemory(SYS_SGX_REG_SIZE, gsSGXRegsCPUVAddr, gsSGXDeviceMap.sRegsCpuPBase);

	SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_REG_MEM);

	gsSGXRegsCPUVAddr = IMG_NULL;
#endif

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SysSystemPrePowerState

 @Description	Perform system-level processing required before a system power
 				transition

 @Input	   eNewPowerState :

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SysSystemPrePowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError= PVRSRV_OK;

	if (eNewPowerState != gpsSysData->eCurrentPowerState)
	{
		if ((eNewPowerState == PVRSRV_SYS_POWER_STATE_D3) &&
			(gpsSysData->eCurrentPowerState < PVRSRV_SYS_POWER_STATE_D3))
		{
			/*
			 * About to enter D3 state.
			 */
#if defined(SYS_USING_INTERRUPTS)
			if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR))
			{
				eError = OSUninstallDeviceLISR(gpsSysData);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"SysSystemPrePowerState: OSUninstallDeviceLISR failed (%d)", eError));
				}
				SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
				SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
			}
#endif /* defined(SYS_USING_INTERRUPTS) */

			/*
			 * Unmap the system-level registers.
			 */
			SysUnmapRegisters();

			if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS))
			{
				DisableSystemClocksWrap(gpsSysData);

				SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
				SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
			}
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	SysSystemPostPowerState

 @Description	Perform system-level processing required after a system power
 				transition

 @Input	   eNewPowerState :

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SysSystemPostPowerState(PVRSRV_SYS_POWER_STATE eNewPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (eNewPowerState != gpsSysData->eCurrentPowerState)
	{
		if ((gpsSysData->eCurrentPowerState == PVRSRV_SYS_POWER_STATE_D3) &&
			(eNewPowerState < PVRSRV_SYS_POWER_STATE_D3))
		{
			if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS))
			{
				eError = EnableSystemClocksWrap(gpsSysData);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: EnableSystemClocksWrap failed (%d)", eError));
					return eError;
				}
				SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_SYSCLOCKS);
				SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_DISABLE_SYSCLOCKS);
			}

			/*
				Returning from D3 state.
				Find the device again as it may have been remapped.
			*/
			eError = SysLocateDevices(gpsSysData);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: Failed to locate devices"));
				return eError;
			}

			/*
			 * Map the system-level registers.
			 */
			eError = SysMapInRegisters();
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: Failed to map in registers"));
				return eError;
			}
#if defined(SYS_USING_INTERRUPTS)
			if (SYS_SPECIFIC_DATA_TEST(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR))
			{
				eError = OSInstallDeviceLISR(gpsSysData, gsSGXDeviceMap.ui32IRQ, "SGX ISR", gpsSGXDevNode);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR,"SysSystemPostPowerState: OSInstallDeviceLISR failed to install ISR (%d)", eError));
				}
				SYS_SPECIFIC_DATA_SET(&gsSysSpecificData, SYS_SPECIFIC_DATA_ENABLE_LISR);
				SYS_SPECIFIC_DATA_CLEAR(&gsSysSpecificData, SYS_SPECIFIC_DATA_PM_UNINSTALL_LISR);
			}
#endif /* defined(SYS_USING_INTERRUPTS) */
		}
	}
	return eError;
}


/*!
******************************************************************************

 @Function	SysDevicePrePowerState

 @Description	Perform system-level processing required before a device power
 				transition

 @Input	   ui32DeviceIndex :
 @Input	   eNewPowerState :
 @Input	   eCurrentPowerState :

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SysDevicePrePowerState(IMG_UINT32				ui32DeviceIndex,
									PVRSRV_DEV_POWER_STATE	eNewPowerState,
									PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	if (ui32DeviceIndex == gui32SGXDeviceID)
	{
		if ((eNewPowerState != eCurrentPowerState) &&
			(eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF))
		{
			/*
			 * This is the point where power should be removed
			 * from SGX on a non-PCI system.
			 */
			PVR_DPF((PVR_DBG_MESSAGE,"SysDevicePrePowerState: Remove SGX power"));

#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
			DisableSGXClocksWrap(gpsSysData);
#endif 
		}
	}

	return PVRSRV_OK;
}


/*!
******************************************************************************

 @Function	SysDevicePostPowerState

 @Description	Perform system-level processing required after a device power
 				transition

 @Input	   ui32DeviceIndex :
 @Input	   eNewPowerState :
 @Input	   eCurrentPowerState :

 @Return   PVRSRV_ERROR :

******************************************************************************/
PVRSRV_ERROR SysDevicePostPowerState(IMG_UINT32				ui32DeviceIndex,
									 PVRSRV_DEV_POWER_STATE	eNewPowerState,
									 PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (ui32DeviceIndex == gui32SGXDeviceID)
	{
		if ((eNewPowerState != eCurrentPowerState) &&
			(eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF))
		{
			/*
			 * This is the point where power should be restored to SGX on a
			 * non-PCI system.
			 */
			PVR_DPF((PVR_DBG_MESSAGE,"SysDevicePostPowerState: Restore SGX power"));
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
			eError = EnableSGXClocksWrap(gpsSysData);
#endif
		}
	}

	return eError;
}


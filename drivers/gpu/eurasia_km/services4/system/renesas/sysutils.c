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

#if defined(__linux__)

#include <linux/version.h>
#include <linux/clk.h>
#include <linux/err.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
#include <linux/mutex.h>
#else
#include <asm/semaphore.h>
#endif

#include <linux/module.h>
#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#endif

#endif /* defined(__linux__) */

#include "sgxdefs.h"
#include "services_headers.h"
#include "sysinfo.h"
#include "sgxinfokm.h"
//#include "sgxinfo.h"
#include "syslocal.h"

#ifndef SYS_SGX_CLOCK_SPEED
#error "SYS_SGX_CLOCK_SPEED must be defined"
#endif

#define ONE_MHZ			1000000
#define HZ_TO_MHZ(m)		((m) / ONE_MHZ)

#if defined(__linux__)

IMG_UINT32 SGXCoreClock = SYS_SGX_CLOCK_SPEED;
module_param(SGXCoreClock, uint, S_IRUGO);

#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
/* The following is exported by the Linux module code */
extern struct platform_device *gpsPVRLDMDev;
#endif

#if defined(SYS_CUSTOM_POWERLOCK_WRAP)
static IMG_VOID PowerLockWrap(SYS_SPECIFIC_DATA *psSysSpecData)
{
	if (!in_interrupt())
	{
		mutex_lock(&psSysSpecData->sPowerLock);
	}
}

static IMG_VOID PowerLockUnwrap(SYS_SPECIFIC_DATA *psSysSpecData)
{
	if (!in_interrupt())
	{
		mutex_unlock(&psSysSpecData->sPowerLock);
	}
}

PVRSRV_ERROR SysPowerLockWrap(IMG_BOOL bTryLock)
{
	SYS_DATA	*psSysData;

	PVR_UNREFERENCED_PARAMETER(bTryLock);

	SysAcquireData(&psSysData);

	PowerLockWrap(psSysData->pvSysSpecificData);

	return PVRSRV_OK;
}

IMG_VOID SysPowerLockUnwrap(IMG_VOID)
{
	SYS_DATA	*psSysData;

	SysAcquireData(&psSysData);

	PowerLockUnwrap(psSysData->pvSysSpecificData);
}

IMG_BOOL WrapSystemPowerChange(SYS_SPECIFIC_DATA *psSysSpecData)
{
	return IMG_TRUE;
}

IMG_VOID UnwrapSystemPowerChange(SYS_SPECIFIC_DATA *psSysSpecData)
{
}
#endif /* defined(SYS_CUSTOM_POWERLOCK_WRAP) */

#if defined(SGX_DYNAMIC_TIMING_INFO)
static inline IMG_UINT32 scale_by_rate(IMG_UINT32 ui32Val, IMG_UINT32 ui32Rate1, IMG_UINT32 ui32Rate2)
{
	if (ui32Rate1 >= ui32Rate2)
	{
		return ui32Val * (ui32Rate1 / ui32Rate2);
	}

	return ui32Val / (ui32Rate2 / ui32Rate1);
}

static inline IMG_UINT32 scale_prop_to_SGX_clock(IMG_UINT32 ui32Val, IMG_UINT32 ui32Rate)
{
	return scale_by_rate(ui32Val, ui32Rate, SGXCoreClock);
}

static inline IMG_UINT32 scale_inv_prop_to_SGX_clock(IMG_UINT32 ui32Val, IMG_UINT32 ui32Rate)
{
	return scale_by_rate(ui32Val, SGXCoreClock, ui32Rate);
}

IMG_VOID SysGetSGXTimingInformation(SGX_TIMING_INFORMATION *psTimingInfo)
{
	IMG_UINT32 ui32Rate;

#if defined(NO_HARDWARE)
	ui32Rate = SGXCoreClock;
#else
	PVR_ASSERT(atomic_read(&gpsSysSpecificData->sSGXClocksEnabled) != 0);

#if defined(USE_LINUX_CLK_FRAMEWORK)
	ui32Rate = clk_get_rate(gpsSysSpecificData->psSGX_FCK);
#else
	ui32Rate = SGXCoreClock;
#endif

	PVR_ASSERT(ui32Rate != 0);
#endif /* defined(NO_HARDWARE) */

	psTimingInfo->ui32CoreClockSpeed = ui32Rate;
	psTimingInfo->ui32HWRecoveryFreq = scale_prop_to_SGX_clock(SYS_SGX_HWRECOVERY_TIMEOUT_FREQ, ui32Rate);
	psTimingInfo->ui32uKernelFreq = scale_prop_to_SGX_clock(SYS_SGX_PDS_TIMER_FREQ, ui32Rate);
#if defined(SUPPORT_ACTIVE_POWER_MANAGEMENT)
	psTimingInfo->bEnableActivePM = IMG_TRUE;
#else
	psTimingInfo->bEnableActivePM = IMG_FALSE;
#endif 
	psTimingInfo->ui32ActivePowManLatencyms = SYS_SGX_ACTIVE_POWER_LATENCY_MS;
}
#endif /* defined(SGX_DYNAMIC_TIMING_INFO) */

PVRSRV_ERROR EnableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

#if defined(USE_LINUX_CLK_FRAMEWORK)
	IMG_INT32 i32Result;
	IMG_INT32 i32NewRate, i32Rate;
#endif

	if (atomic_read(&psSysSpecData->sSGXClocksEnabled) != 0)
	{
		return PVRSRV_OK;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "EnableSGXClocks: Enabling SGX Clocks"));

#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
	pm_runtime_get_sync(&gpsPVRLDMDev->dev);
#endif

#if defined(USE_LINUX_CLK_FRAMEWORK)
	i32Result = clk_enable(psSysSpecData->psSGX_FCK);
	if (i32Result < 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: Couldn't enable SGX functional clock (%d)", i32Result));
		return PVRSRV_ERROR_UNABLE_TO_ENABLE_CLOCK;
	}

	i32NewRate = clk_round_rate(psSysSpecData->psSGX_FCK, SGXCoreClock + ONE_MHZ);
	if (i32NewRate <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "EnableSGXClocks: Couldn't round SGX functional clock rate"));
		return PVRSRV_ERROR_UNABLE_TO_ROUND_CLOCK_RATE;
	}

	i32Rate = clk_get_rate(psSysSpecData->psSGX_FCK);
	if (i32Rate != i32NewRate)
	{
		i32Result = clk_set_rate(psSysSpecData->psSGX_FCK, i32NewRate);
		if (i32Result < 0)
		{
			PVR_DPF((PVR_DBG_WARNING, "EnableSGXClocks: Couldn't set SGX functional clock rate (%d)", i32Result));
		}
	}

#if defined(DEBUG)
	{
		IMG_INT32 i32Rate = clk_get_rate(psSysSpecData->psSGX_FCK);
		PVR_DPF((PVR_DBG_MESSAGE, "EnableSGXClocks: SGX Functional Clock is %dMhz", HZ_TO_MHZ(i32Rate)));
	}
#endif

#endif /* defined(USE_LINUX_CLK_FRAMEWORK) */

	atomic_set(&psSysSpecData->sSGXClocksEnabled, 1);

#else /* !defined(NO_HARDWARE) */
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif /* !defined(NO_HARDWARE) */

	return PVRSRV_OK;
}

IMG_VOID DisableSGXClocks(SYS_DATA *psSysData)
{
#if !defined(NO_HARDWARE)
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;

	if (atomic_read(&psSysSpecData->sSGXClocksEnabled) == 0)
	{
		return;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "DisableSGXClocks: Disabling SGX Clocks"));

#if defined(USE_LINUX_CLK_FRAMEWORK)
	if (psSysSpecData->psSGX_FCK)
	{
		clk_disable(psSysSpecData->psSGX_FCK);
	}
#endif

#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
	pm_runtime_put_sync(&gpsPVRLDMDev->dev);
#endif

	atomic_set(&psSysSpecData->sSGXClocksEnabled, 0);

#else /* !defined(NO_HARDWARE) */
	PVR_UNREFERENCED_PARAMETER(psSysData);
#endif /* !defined(NO_HARDWARE) */
}

PVRSRV_ERROR EnableSystemClocks(SYS_DATA *psSysData)
{
	SYS_SPECIFIC_DATA *psSysSpecData = (SYS_SPECIFIC_DATA *) psSysData->pvSysSpecificData;
#if defined(USE_LINUX_CLK_FRAMEWORK)
	struct clk *psCLK;
#endif
	PVRSRV_ERROR eError;

	PVR_TRACE(("EnableSystemClocks: Enabling System Clocks"));

	if (!psSysSpecData->bSysClocksOneTimeInit)
	{
#if defined(SYS_CUSTOM_POWERLOCK_WRAP)
		mutex_init(&psSysSpecData->sPowerLock);
#endif

		atomic_set(&psSysSpecData->sSGXClocksEnabled, 0);

#if defined(USE_LINUX_CLK_FRAMEWORK)
#if defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
		psCLK = clk_get(&gpsPVRLDMDev->dev, NULL);	/* for R-Car Gen2 BSP */
#else
		psCLK = clk_get(NULL, "sgx");			/* for R-Car Gen1 BSP */
#endif
		if (IS_ERR(psCLK))
		{
			PVR_DPF((PVR_DBG_ERROR, "EnableSsystemClocks: Couldn't get SGX Functional Clock"));
			eError = PVRSRV_ERROR_DISABLE_CLOCK_FAILURE;
			goto Exit;
		}
		psSysSpecData->psSGX_FCK = psCLK;
#endif
		psSysSpecData->bSysClocksOneTimeInit = IMG_TRUE;
	}

	eError = PVRSRV_OK;

#if defined(USE_LINUX_CLK_FRAMEWORK)
Exit:
#endif
	return eError;
}

IMG_VOID DisableSystemClocks(SYS_DATA *psSysData)
{
	PVR_TRACE(("DisableSystemClocks: Disabling System Clocks"));

	DisableSGXClocks(psSysData);
}

PVRSRV_ERROR SysPMRuntimeRegister(void)
{
#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
	pm_runtime_enable(&gpsPVRLDMDev->dev);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR SysPMRuntimeUnregister(void)
{
#if defined(LDM_PLATFORM) && !defined(SUPPORT_DRI_DRM)
	pm_runtime_disable(&gpsPVRLDMDev->dev);
#endif
	return PVRSRV_OK;
}

#endif /* defined(__linux__) */

IMG_CHAR *SysCreateVersionString(IMG_CPU_PHYADDR sRegRegion)
{
	static IMG_CHAR aszVersionString[100];
#if !defined(NO_HARDWARE)
	IMG_VOID	*pvRegsLinAddr;
#endif
#if defined(SGX_FEATURE_MP)
	IMG_INT32       i32CoreNum;
	IMG_UINT32      ui32TempVal;
	IMG_UINT32      ui32Val;
#endif /* defined(SGX_FEATURE_MP) */
	IMG_UINT32	ui32SGXRevision;
	IMG_INT32	i32Count;

#if !defined(NO_HARDWARE)
	pvRegsLinAddr = OSMapPhysToLin(sRegRegion,
                                   SYS_SGX_REG_SIZE,
                                   PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
                                   IMG_NULL);
	if(!pvRegsLinAddr)
	{
		return IMG_NULL;
	}

#if defined(SGX_FEATURE_MP)
	ui32TempVal = OSReadHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr), 0x4004);
	ui32Val = 0;
	for (i32CoreNum = 0; i32CoreNum < SGX_FEATURE_MP_CORE_COUNT; i32CoreNum++)
	{
		ui32Val <<= 2;
		ui32Val |= 0x2;
	}
	OSWriteHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr), 0x4004, ui32Val); /* enable each core's clock */
#endif

	ui32SGXRevision = OSReadHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr),
                                      EUR_CR_CORE_REVISION);

#if defined(SGX_FEATURE_MP)
	OSWriteHWReg((IMG_PVOID)((IMG_PBYTE)pvRegsLinAddr), 0x4004, ui32TempVal); /* restore core's clock */
#endif

	OSUnMapPhysToLin(pvRegsLinAddr,
                     SYS_SGX_REG_SIZE,
                     PVRSRV_HAP_UNCACHED|PVRSRV_HAP_KERNEL_ONLY,
                     IMG_NULL);
#else
	PVR_UNREFERENCED_PARAMETER(sRegRegion);
	ui32SGXRevision = 0;
#endif

	i32Count = OSSNPrintf(aszVersionString, 100,
                           "SGX revision = %u.%u.%u",
                           (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAJOR_MASK)
                            >> EUR_CR_CORE_REVISION_MAJOR_SHIFT),
                           (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MINOR_MASK)
                            >> EUR_CR_CORE_REVISION_MINOR_SHIFT),
                           (IMG_UINT)((ui32SGXRevision & EUR_CR_CORE_REVISION_MAINTENANCE_MASK)
                            >> EUR_CR_CORE_REVISION_MAINTENANCE_SHIFT));

	if(i32Count == -1)
	{
		return IMG_NULL;
	}

	return aszVersionString;
}


/*!
******************************************************************************

 @Function      SysGetInterruptSource

 @Description 	Returns System specific information about the device(s) that
                generated the interrupt in the system

 @Input         psSysData
 @Input         psDeviceNode - always NULL for a system ISR

 @Return        System specific information indicating which device(s)
                generated the interrupt

******************************************************************************/
IMG_UINT32 SysGetInterruptSource(SYS_DATA			*psSysData,
								 PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);

#if defined(NO_HARDWARE)
	/* no interrupts in no_hw system just return all bits */
	return 0xFFFFFFFF;
#else
	return psDeviceNode->ui32SOCInterruptBit;
#endif
}


/*!
******************************************************************************

 @Function      SysClearInterrupts

 @Description 	Clears specified system interrupts

 @Input         psSysData
 @Input         ui32InterruptBits

 @Return        none

******************************************************************************/
IMG_VOID SysClearInterrupts(SYS_DATA	*psSysData,
							IMG_UINT32	ui32InterruptBits)
{
	PVR_UNREFERENCED_PARAMETER(psSysData);
	PVR_UNREFERENCED_PARAMETER(ui32InterruptBits);
}




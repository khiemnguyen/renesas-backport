/*************************************************************************/ /*!
@Title          DC_LINUXFB common driver functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
                Copyright (C) 2012-2013 Renesas Electronics Corporation
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
#include <linux/string.h>
#else
#include <string.h>
#endif
#include <linux/module.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"

#include "dc_linuxfb.h"

#define DISPLAY_DEVICE_NAME "DC_LINUXFB"

#define DC_LINUXFB_COMMAND_COUNT		1

static void *gpvAnchor = 0;
static PFN_DC_GET_PVRJTABLE pfnGetPVRJTable = 0;


static DC_LINUXFB_DEVINFO * GetAnchorPtr(void)
{
	return (DC_LINUXFB_DEVINFO *)gpvAnchor;
}

static void SetAnchorPtr(DC_LINUXFB_DEVINFO *psDevInfo)
{
	gpvAnchor = (void *)psDevInfo;
}


static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	DC_LINUXFB_DEVINFO *psDevInfo;
	PVR_UNREFERENCED_PARAMETER(ui32DeviceID);

	psDevInfo = GetAnchorPtr();

	psDevInfo->asBackBuffers[0].psSyncData = psSystemBufferSyncData;

	*phDevice = (IMG_HANDLE)psDevInfo;

	return (SetupDevInfo(psDevInfo));
}


static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	UNREFERENCED_PARAMETER(hDevice);

	FreeBackBuffers(GetAnchorPtr());

	return (PVRSRV_OK);
}


static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE		hDevice,
                                  IMG_UINT32		*pui32NumFormats,
                                  DISPLAY_FORMAT	*psFormat)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;

	if(!hDevice || !pui32NumFormats)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO *)hDevice;

	*pui32NumFormats = (IMG_UINT32)psDevInfo->ulNumFormats;

	if(psFormat != IMG_NULL)
	{
		unsigned long i;

		for(i=0; i<psDevInfo->ulNumFormats; i++)
		{
			psFormat[i] = psDevInfo->asDisplayFormatList[i];
		}
	}

	return (PVRSRV_OK);
}


static PVRSRV_ERROR EnumDCDims(IMG_HANDLE		hDevice,
                               DISPLAY_FORMAT	*psFormat,
                               IMG_UINT32		*pui32NumDims,
                               DISPLAY_DIMS		*psDim)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO *)hDevice;

	*pui32NumDims = (IMG_UINT32)psDevInfo->ulNumDims;


	if(psDim != IMG_NULL)
	{
		unsigned long i;

		for(i=0; i<psDevInfo->ulNumDims; i++)
		{
			psDim[i] = psDevInfo->asDisplayDimList[i];
		}
	}

	return (PVRSRV_OK);
}


static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;

	if(!hDevice || !phBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO *)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->asBackBuffers[0];

	return (PVRSRV_OK);
}


static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;

	if(!hDevice || !psDCInfo)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO *)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return (PVRSRV_OK);
}


static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE         hDevice,
                                    IMG_HANDLE         hBuffer,
                                    IMG_SYS_PHYADDR **ppsSysAddr,
                                    IMG_UINT32        *pui32ByteSize,
                                    IMG_VOID         **ppvCpuVAddr,
                                    IMG_HANDLE        *phOSMapInfo,
                                    IMG_BOOL          *pbIsContiguous,
                                    IMG_UINT32		  *pui32TilingStride)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;
	DC_LINUXFB_BUFFER	*psBuffer;

	if(!hDevice || !hBuffer || !ppsSysAddr || !pui32ByteSize)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO *)hDevice;

	psBuffer = (DC_LINUXFB_BUFFER*)hBuffer;

	*ppvCpuVAddr = psBuffer->sCPUVAddr;

	*pui32ByteSize = (IMG_UINT32)(psDevInfo->asDisplayDimList[0].ui32Height * psDevInfo->asDisplayDimList[0].ui32ByteStride);
	*phOSMapInfo = IMG_NULL;

	*ppsSysAddr = &psBuffer->sSysAddr;
	*pbIsContiguous = IMG_TRUE;

#if defined(SUPPORT_MEMORY_TILING)
	{
		IMG_UINT32 ui32Stride = psDevInfo->asDisplayDimList[0].ui32ByteStride;
		IMG_UINT32 ui32NumBits = 0, ui32StrideTopBit, n;

		for(n = 0; n < 32; n++)
		{
			if(ui32Stride & (1<<n))
			{
				ui32NumBits = n+1;
			}
		}

		if(ui32NumBits < 10)
		{
			ui32NumBits = 10;
		}

		ui32StrideTopBit = ui32NumBits - 1;

		ui32StrideTopBit -= 9;

		*pui32TilingStride = ui32StrideTopBit;
	}
#else
	UNREFERENCED_PARAMETER(pui32TilingStride);
#endif 

	return (PVRSRV_OK);
}

static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
                                      IMG_UINT32 ui32Flags,
                                      DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib,
                                      DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
                                      IMG_UINT32 ui32BufferCount,
                                      PVRSRV_SYNC_DATA **ppsSyncData,
                                      IMG_UINT32 ui32OEMFlags,
                                      IMG_HANDLE *phSwapChain,
                                      IMG_UINT32 *pui32SwapChainID)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;
	DC_LINUXFB_SWAPCHAIN *psSwapChain;
	DC_LINUXFB_BUFFER *psBuffer;
	IMG_UINT32 i;

	UNREFERENCED_PARAMETER(ui32OEMFlags);
	UNREFERENCED_PARAMETER(pui32SwapChainID);

	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO*)hDevice;

	if(psDevInfo->psSwapChain)
	{
		return (PVRSRV_ERROR_FLIP_CHAIN_EXISTS);
	}

	if(ui32BufferCount > psDevInfo->ulNumBackBuffers)
	{
		return (PVRSRV_ERROR_TOOMANYBUFFERS);
	}



	if(psDstSurfAttrib->pixelformat != psDevInfo->sSysFormat.pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psDevInfo->sSysDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psDevInfo->sSysDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psDevInfo->sSysDims.ui32Height)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	UNREFERENCED_PARAMETER(ui32Flags);

	psSwapChain = (DC_LINUXFB_SWAPCHAIN*)AllocKernelMem(sizeof(DC_LINUXFB_SWAPCHAIN));
	if(!psSwapChain)
	{
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	psBuffer = (DC_LINUXFB_BUFFER*)AllocKernelMem(sizeof(DC_LINUXFB_BUFFER) * ui32BufferCount);
	if(!psBuffer)
	{
		FreeKernelMem(psSwapChain);
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}

	memset(psSwapChain, 0, sizeof(DC_LINUXFB_SWAPCHAIN));
	memset(psBuffer, 0, sizeof(DC_LINUXFB_BUFFER) * ui32BufferCount);

	psSwapChain->ulBufferCount = (unsigned long)ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;

	for(i=0; i<ui32BufferCount-1; i++)
	{
		psBuffer[i].psNext = &psBuffer[i+1];
	}

	psBuffer[i].psNext = &psBuffer[0];

	for(i=0; i<ui32BufferCount; i++)
	{
		psBuffer[i].psSyncData = ppsSyncData[i];
		psBuffer[i].sSysAddr = psDevInfo->asBackBuffers[i].sSysAddr;
		psBuffer[i].sDevVAddr = psDevInfo->asBackBuffers[i].sDevVAddr;
		psBuffer[i].sCPUVAddr = psDevInfo->asBackBuffers[i].sCPUVAddr;
		psBuffer[i].ui32YOffset = psDevInfo->asBackBuffers[i].ui32YOffset;
		psBuffer[i].hSwapChain = (DC_HANDLE)psSwapChain;
	}

	psDevInfo->psSwapChain = psSwapChain;

	*phSwapChain = (IMG_HANDLE)psSwapChain;


	return (PVRSRV_OK);
}


static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
                                       IMG_HANDLE hSwapChain)
{
	DC_LINUXFB_DEVINFO	*psDevInfo;
	DC_LINUXFB_SWAPCHAIN *psSwapChain;

	if(!hDevice
	|| !hSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO*)hDevice;
	psSwapChain = (DC_LINUXFB_SWAPCHAIN*)hSwapChain;

	FreeKernelMem(psSwapChain->psBuffer);
	FreeKernelMem(psSwapChain);

	psDevInfo->psSwapChain = 0;


	return (PVRSRV_OK);
}


static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE	hDevice,
                                 IMG_HANDLE	hSwapChain,
                                 IMG_RECT	*psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE	hDevice,
                                 IMG_HANDLE	hSwapChain,
                                 IMG_RECT	*psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE	hDevice,
                                      IMG_HANDLE	hSwapChain,
                                      IMG_UINT32	ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE	hDevice,
                                      IMG_HANDLE	hSwapChain,
                                      IMG_UINT32	ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_UINT32 *pui32BufferCount,
                                 IMG_HANDLE *phBuffer)
{
	DC_LINUXFB_SWAPCHAIN *psSwapChain;
	unsigned long i;

	if(!hDevice
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	psSwapChain = (DC_LINUXFB_SWAPCHAIN*)hSwapChain;

	*pui32BufferCount = (IMG_UINT32)psSwapChain->ulBufferCount;

	for(i=0; i<psSwapChain->ulBufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}

	return (PVRSRV_OK);
}


static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE	hDevice,
                                   IMG_HANDLE	hBuffer,
                                   IMG_UINT32	ui32SwapInterval,
                                   IMG_HANDLE	hPrivateTag,
                                   IMG_UINT32	ui32ClipRectCount,
                                   IMG_RECT		*psClipRect)
{
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(psClipRect);

	if(!hDevice
	|| !hBuffer
	|| (ui32ClipRectCount != 0))
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}

	return (PVRSRV_OK);
}


static DC_ERROR Flip(DC_LINUXFB_DEVINFO	*psDevInfo,
                     DC_LINUXFB_BUFFER		*psBuffer)
{
	if(!psDevInfo || !psBuffer)
	{
		return (DC_ERROR_INVALID_PARAMS);
	}

	DC_LINUXFB_Flip(psDevInfo, psBuffer);

	return (DC_OK);
}


static IMG_BOOL ProcessFlip(IMG_HANDLE	hCmdCookie,
                            IMG_UINT32	ui32DataSize,
                            IMG_VOID	*pvData)
{
	IMG_UINT32 ui32VsyncCount;
	IMG_UINT32 ui32StartTime, ui32EndTime;
	DC_ERROR eError;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	DC_LINUXFB_DEVINFO	*psDevInfo;
	DC_LINUXFB_BUFFER	*psBuffer;

	if(!hCmdCookie)
	{
		return (IMG_FALSE);
	}

	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;
	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return (IMG_FALSE);
	}

	psDevInfo = (DC_LINUXFB_DEVINFO*)psFlipCmd->hExtDevice;

	psBuffer = (DC_LINUXFB_BUFFER*)psFlipCmd->hExtBuffer;

	for (ui32VsyncCount = 1; ui32VsyncCount < psFlipCmd->ui32SwapInterval; ui32VsyncCount++)
	{
		DC_LINUXFB_WaitForSwap(psDevInfo);
	}

	ui32StartTime = DC_LINUXFB_GetTimeCountMS();
	eError = Flip(psDevInfo, psBuffer);
	if(eError != DC_OK)
	{
		return (IMG_FALSE);
	}

	/* Wait for Flip done */
	if (psFlipCmd->ui32SwapInterval != 0)
	{
		DC_LINUXFB_WaitForSwap(psDevInfo);

		ui32EndTime = DC_LINUXFB_GetTimeCountMS();
		if ((ui32EndTime - ui32StartTime) < DC_LINUXFB_FLIP_SETTLE_TIME)
		{
			DC_LINUXFB_WaitForSwap(psDevInfo);
		}
	}

	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);

	return (IMG_TRUE);
}


DC_ERROR Init(void)
{
	DC_LINUXFB_DEVINFO *psDevInfo;
	DC_ERROR         eError;

	psDevInfo = GetAnchorPtr();

	if (psDevInfo == 0)
	{
		PFN_CMD_PROC  pfnCmdProcList[DC_LINUXFB_COMMAND_COUNT];
		IMG_UINT32    aui32SyncCountList[DC_LINUXFB_COMMAND_COUNT][2];

		psDevInfo = (DC_LINUXFB_DEVINFO *)AllocKernelMem(sizeof(*psDevInfo));

		if(!psDevInfo)
		{
			eError = DC_ERROR_OUT_OF_MEMORY;
			goto ExitError;
		}

		memset(psDevInfo, 0, sizeof(*psDevInfo));

		SetAnchorPtr((void*)psDevInfo);

		psDevInfo->ulRefCount = 0UL;

		if(OpenPVRServices(&psDevInfo->hPVRServices) != DC_OK)
		{
			eError = DC_ERROR_INIT_FAILURE;
			goto ExitFreeDevInfo;
		}
		if(GetLibFuncAddr (psDevInfo->hPVRServices, "PVRGetDisplayClassJTable", &pfnGetPVRJTable) != DC_OK)
		{
			eError = DC_ERROR_INIT_FAILURE;
			goto ExitCloseServices;
		}

		if((*pfnGetPVRJTable)(&psDevInfo->sPVRJTable) == IMG_FALSE)
		{
			eError = DC_ERROR_INIT_FAILURE;
			goto ExitCloseServices;
		}

		psDevInfo->ulNumFormats = 1UL;
		psDevInfo->ulNumDims = 1UL;

		if (SetupDevInfo(psDevInfo) != PVRSRV_OK)
		{
			eError = DC_ERROR_INIT_FAILURE;
			goto ExitCloseServices;
		}

		psDevInfo->psSwapChain = 0;
		psDevInfo->sDisplayInfo.ui32MinSwapInterval = 0UL;
		psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 10UL;
		psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1UL;
		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = psDevInfo->ulNumBackBuffers;
		strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);


		psDevInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
		psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
		psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
		psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
		psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
		psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
		psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
		psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
		psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
		psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
		psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
		psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
		psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
		psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
		psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
		psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
		psDevInfo->sDCJTable.pfnSetDCState = IMG_NULL;

		if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice (&psDevInfo->sDCJTable,
								    &psDevInfo->uiDeviceID ) != PVRSRV_OK)
		{
			eError = DC_ERROR_DEVICE_REGISTER_FAILED;
			goto ExitFreeMem;
		}


		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;


		aui32SyncCountList[DC_FLIP_COMMAND][0] = 0UL;
		aui32SyncCountList[DC_FLIP_COMMAND][1] = 2UL;



		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList(psDevInfo->uiDeviceID,
												&pfnCmdProcList[0],
												aui32SyncCountList,
												DC_LINUXFB_COMMAND_COUNT) != PVRSRV_OK)
		{
			eError = DC_ERROR_CANT_REGISTER_CALLBACK;
			goto ExitRemoveDevice;
		}
	}

	psDevInfo->ulRefCount++;

	return (DC_OK);

ExitRemoveDevice:
	(IMG_VOID) psDevInfo->sPVRJTable.pfnPVRSRVRemoveDCDevice(psDevInfo->uiDeviceID);

ExitFreeMem:

ExitCloseServices:
	(void)ClosePVRServices(psDevInfo->hPVRServices);

ExitFreeDevInfo:
	FreeKernelMem(psDevInfo);
	SetAnchorPtr(0);

ExitError:
	return eError;
}



DC_ERROR Deinit(void)
{
	DC_LINUXFB_DEVINFO *psDevInfo, *psDevFirst;

	psDevFirst = GetAnchorPtr();
	psDevInfo = psDevFirst;

	if (psDevInfo == 0)
	{
		return (DC_ERROR_GENERIC);
	}

	psDevInfo->ulRefCount--;

	if (psDevInfo->ulRefCount == 0UL)
	{
		PVRSRV_DC_DISP2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;

		if (psJTable->pfnPVRSRVRemoveDCDevice((IMG_UINT32)psDevInfo->uiDeviceID) != PVRSRV_OK)
		{
			return (DC_ERROR_GENERIC);
		}

		if (psDevInfo->sPVRJTable.pfnPVRSRVRemoveCmdProcList(psDevInfo->uiDeviceID, DC_LINUXFB_COMMAND_COUNT) != PVRSRV_OK)
		{
			return (DC_ERROR_GENERIC);
		}

		if (ClosePVRServices(psDevInfo->hPVRServices) != DC_OK)
		{
			psDevInfo->hPVRServices = 0;
			return (DC_ERROR_GENERIC);
		}

		FreeKernelMem(psDevInfo);
	}

	SetAnchorPtr(0);

	return (DC_OK);
}


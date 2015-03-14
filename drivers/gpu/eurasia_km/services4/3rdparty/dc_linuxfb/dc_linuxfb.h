/*************************************************************************/ /*!
@Title          DC_LINUXFB kernel driver structures and prototypes
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
                Copyright (C) 2012-2013 Renesas Electronics Corporation
@Description    DC_LINUXFB kernel driver structures and prototypes
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
#ifndef __DC_LINUXFB_H__
#define __DC_LINUXFB_H__


#if defined(__cplusplus)
extern "C" {
#endif

#define MIN(a,b)	((a)<(b)?(a):(b))

extern IMG_BOOL IMG_IMPORT PVRGetDisplayClassJTable(PVRSRV_DC_DISP2SRV_KMJTABLE *psJTable);

#define DC_LINUXFB_MAXFORMATS		(1)
#define DC_LINUXFB_MAXDIMS		(1)
#define DC_LINUXFB_MAX_BACKBUFFERS	(3)

#define DC_LINUXFB_FLIP_SETTLE_TIME	(3)

typedef void *		DC_HANDLE;

typedef struct DC_LINUXFB_BUFFER_TAG
{
	DC_HANDLE			 hSwapChain;
	DC_HANDLE			 hMemChunk;

	IMG_SYS_PHYADDR			 sSysAddr;
	IMG_DEV_VIRTADDR		 sDevVAddr;
	IMG_CPU_VIRTADDR		 sCPUVAddr;
	PVRSRV_SYNC_DATA*		 psSyncData;

	IMG_UINT32			 ui32YOffset;	/* for FBIOPAN_DISPLAY */

	struct DC_LINUXFB_BUFFER_TAG	*psNext;
} DC_LINUXFB_BUFFER;


typedef struct DC_LINUXFB_SWAPCHAIN_TAG
{
	unsigned long			 ulBufferCount;
	DC_LINUXFB_BUFFER		*psBuffer;
} DC_LINUXFB_SWAPCHAIN;


typedef struct DC_LINUXFB_DEVINFO_TAG
{
	unsigned int			 uiDeviceID;
	DC_LINUXFB_BUFFER		 sSystemBuffer;
	unsigned long			 ulNumFormats;
	unsigned long			 ulNumDims;
	unsigned long			 ulNumBackBuffers;

	PVRSRV_DC_DISP2SRV_KMJTABLE	 sPVRJTable;
	PVRSRV_DC_SRV2DISP_KMJTABLE	 sDCJTable;

	DC_HANDLE			 hPVRServices;
	DC_LINUXFB_BUFFER		 asBackBuffers[DC_LINUXFB_MAX_BACKBUFFERS];
	unsigned long			 ulRefCount;
	DC_LINUXFB_SWAPCHAIN		*psSwapChain;
	DISPLAY_INFO			 sDisplayInfo;

	DISPLAY_FORMAT			 sSysFormat;
	DISPLAY_DIMS			 sSysDims;
	IMG_UINT32			 ui32BufferSize;

	DISPLAY_FORMAT			 asDisplayFormatList[DC_LINUXFB_MAXFORMATS];
	DISPLAY_DIMS			 asDisplayDimList[DC_LINUXFB_MAXDIMS];
	DISPLAY_FORMAT			 sBackBufferFormat[DC_LINUXFB_MAXFORMATS];

}  DC_LINUXFB_DEVINFO;


typedef enum _DC_ERROR_
{
	DC_OK					=  0,
	DC_ERROR_GENERIC			=  1,
	DC_ERROR_OUT_OF_MEMORY			=  2,
	DC_ERROR_TOO_FEW_BUFFERS		=  3,
	DC_ERROR_INVALID_PARAMS			=  4,
	DC_ERROR_INIT_FAILURE			=  5,
	DC_ERROR_CANT_REGISTER_CALLBACK		=  6,
	DC_ERROR_INVALID_DEVICE			=  7,
	DC_ERROR_DEVICE_REGISTER_FAILED		=  8
} DC_ERROR;


#ifndef UNREFERENCED_PARAMETER
#define	UNREFERENCED_PARAMETER(param) (param) = (param)
#endif

DC_ERROR Init(void);
DC_ERROR Deinit(void);

PVRSRV_ERROR SetupDevInfo (DC_LINUXFB_DEVINFO *psDevInfo);
PVRSRV_ERROR FreeBackBuffers (DC_LINUXFB_DEVINFO *psDevInfo);

void DC_LINUXFB_Flip(DC_LINUXFB_DEVINFO *psDevInfo, DC_LINUXFB_BUFFER *psBuffer);
void DC_LINUXFB_WaitForSwap(DC_LINUXFB_DEVINFO *psDevInfo);
IMG_UINT32 DC_LINUXFB_GetTimeCountMS(void);

DC_ERROR OpenPVRServices  (DC_HANDLE *phPVRServices);
DC_ERROR ClosePVRServices (DC_HANDLE hPVRServices);

void *AllocKernelMem(unsigned long ulSize);
void FreeKernelMem  (void *pvMem);

DC_ERROR GetLibFuncAddr (DC_HANDLE hExtDrv, char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable);

#if defined(__cplusplus)
}
#endif

#endif 


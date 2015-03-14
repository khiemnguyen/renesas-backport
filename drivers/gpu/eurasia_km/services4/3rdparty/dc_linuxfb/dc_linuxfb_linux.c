/*************************************************************************/ /*!
@Title          DC_LINUXFB linux-specific functions
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
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/time.h>

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "dc_linuxfb.h"
#include "pvrmodule.h"

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif

#define DRVNAME "dc_linuxfb"

#if !defined(SUPPORT_DRI_DRM)
MODULE_SUPPORTED_DEVICE(DRVNAME);
#endif

#define unref__ __attribute__ ((unused))

static struct fb_info *gpsOSFBInfo = IMG_NULL;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38))
#define LOCK_CONSOLE	console_lock
#define UNLOCK_CONSOLE	console_unlock
#else
#define LOCK_CONSOLE	acquire_console_sem
#define UNLOCK_CONSOLE	release_console_sem
#endif

void DC_LINUXFB_Flip(DC_LINUXFB_DEVINFO *psDevInfo, DC_LINUXFB_BUFFER *psBuffer)
{
	struct fb_var_screeninfo var;

	UNREFERENCED_PARAMETER(psDevInfo);

	LOCK_CONSOLE();

	var = gpsOSFBInfo->var;
	var.xoffset = 0;
	var.yoffset = psBuffer->ui32YOffset;

	var.xres_virtual = var.xres;
	var.yres_virtual = psDevInfo->ulNumBackBuffers * var.yres;

	var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	if (fb_set_var(gpsOSFBInfo, &var))
	{
		printk(KERN_WARNING DRVNAME "fb_set_var failed(Y Offset: %u)\n", psBuffer->ui32YOffset);
	}

	UNLOCK_CONSOLE();
}

void DC_LINUXFB_WaitForSwap(DC_LINUXFB_DEVINFO *psDevInfo)
{
	UNREFERENCED_PARAMETER(psDevInfo);

	if (gpsOSFBInfo->fbops->fb_ioctl != IMG_NULL)
	{
		LOCK_CONSOLE();

		gpsOSFBInfo->fbops->fb_ioctl(gpsOSFBInfo, FBIO_WAITFORVSYNC, 0);

		UNLOCK_CONSOLE();
	}
}

IMG_UINT32 DC_LINUXFB_GetTimeCountMS(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);

	return (IMG_UINT32)(tv.tv_sec * 1000 + tv.tv_usec/1000);
}


static IMG_BOOL GetFrameBufferAddress(IMG_UINT32 *pui32PhysBase, IMG_UINT32 *pui32ScreenBase)
{
	if (gpsOSFBInfo == IMG_NULL)
	{
		printk(KERN_WARNING DRVNAME ": fbdev not initialized.\n");
		return IMG_FALSE;
	}

	*pui32PhysBase = gpsOSFBInfo->fix.smem_start;
	*pui32ScreenBase = (IMG_UINT32)gpsOSFBInfo->screen_base;

	return IMG_TRUE;
}


static IMG_BOOL GetBufferDimensions(IMG_UINT32 *pui32Width, IMG_UINT32 *pui32Height, PVRSRV_PIXEL_FORMAT *pePixelFormat, IMG_UINT32 *pui32Stride, IMG_UINT32 *pui32BufNum)
{
	static IMG_BOOL bInitTime = IMG_TRUE;

	if (gpsOSFBInfo == IMG_NULL)
	{
		printk(KERN_WARNING DRVNAME ": fbdev not initialized.\n");
		return IMG_FALSE;
	}

	*pui32Width = (IMG_UINT32)gpsOSFBInfo->var.xres;
	*pui32Height = (IMG_UINT32)gpsOSFBInfo->var.yres;

	if (gpsOSFBInfo->var.bits_per_pixel == 16)
	{
		if ((gpsOSFBInfo->var.red.length == 5) &&
			(gpsOSFBInfo->var.green.length == 6) &&
			(gpsOSFBInfo->var.blue.length == 5) &&
			(gpsOSFBInfo->var.red.offset == 11) &&
			(gpsOSFBInfo->var.green.offset == 5) &&
			(gpsOSFBInfo->var.blue.offset == 0) &&
			(gpsOSFBInfo->var.red.msb_right == 0))
		{
			*pePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
		}
		else
		{
			goto format_err;
		}
	}
	else if (gpsOSFBInfo->var.bits_per_pixel == 32)
	{
		if ((gpsOSFBInfo->var.red.length == 8)  &&
			(gpsOSFBInfo->var.green.length == 8) &&
			(gpsOSFBInfo->var.blue.length == 8) &&
			/* (gpsOSFBInfo->var.transp.length == 8) && */
			(gpsOSFBInfo->var.red.offset == 16) &&
			(gpsOSFBInfo->var.green.offset == 8) &&
			(gpsOSFBInfo->var.blue.offset == 0) &&
			/* (gpsOSFBInfo->var.transp.offset == 24) && */
			(gpsOSFBInfo->var.red.msb_right == 0))
		{
			*pePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
		}
		else
		{
			goto format_err;
		}
	}
	else
	{
		goto format_err;
	}

	*pui32Stride = gpsOSFBInfo->fix.line_length;

	*pui32BufNum = gpsOSFBInfo->fix.smem_len / (gpsOSFBInfo->fix.line_length * gpsOSFBInfo->var.yres);

	if (bInitTime)
	{
		printk(KERN_INFO DRVNAME " - Found usable fbdev device (%s):\n"
						"  range (physical) = 0x%lx-0x%lx\n"
						"  size (bytes)     = 0x%x\n"
						"  xres x yres      = %ux%u\n"
						"  xres x yres (v)  = %ux%u\n"
						"  img pix fmt      = %u\n"
						"  num buffers      = %d\n",
						gpsOSFBInfo->fix.id,
						gpsOSFBInfo->fix.smem_start,
						gpsOSFBInfo->fix.smem_start + gpsOSFBInfo->fix.smem_len,
						gpsOSFBInfo->fix.smem_len,
						gpsOSFBInfo->var.xres,         gpsOSFBInfo->var.yres,
						gpsOSFBInfo->var.xres_virtual, gpsOSFBInfo->var.yres_virtual,
						*pePixelFormat,
						*pui32BufNum);
		bInitTime = IMG_FALSE;
	}

	return IMG_TRUE;

format_err:
	printk(KERN_WARNING DRVNAME ": pixel format mismatch.\n");
	*pePixelFormat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
	return IMG_FALSE;
}

PVRSRV_ERROR SetupDevInfo (DC_LINUXFB_DEVINFO *psDevInfo)
{
	int i;
	IMG_UINT32 ui32BufNum;
	IMG_UINT32 ui32PhysBase, ui32ScreenBase;

	if (!GetFrameBufferAddress(&ui32PhysBase, &ui32ScreenBase))
	{
		return DC_ERROR_INIT_FAILURE;
	}
	if (!GetBufferDimensions(&psDevInfo->asDisplayDimList[0].ui32Width,
				 &psDevInfo->asDisplayDimList[0].ui32Height,
				 &psDevInfo->asDisplayFormatList[0].pixelformat,
				 &psDevInfo->asDisplayDimList[0].ui32ByteStride,
				 &ui32BufNum))
	{
		return DC_ERROR_INIT_FAILURE;

	}

	psDevInfo->sSysFormat = psDevInfo->asDisplayFormatList[0];
	psDevInfo->sSysDims.ui32Width = psDevInfo->asDisplayDimList[0].ui32Width;
	psDevInfo->sSysDims.ui32Height = psDevInfo->asDisplayDimList[0].ui32Height;
	psDevInfo->sSysDims.ui32ByteStride = psDevInfo->asDisplayDimList[0].ui32ByteStride;
	psDevInfo->ui32BufferSize = psDevInfo->sSysDims.ui32Height * psDevInfo->sSysDims.ui32ByteStride;

	if (ui32BufNum == 0) {
		return DC_ERROR_TOO_FEW_BUFFERS;
	}
	psDevInfo->ulNumBackBuffers = MIN(ui32BufNum, DC_LINUXFB_MAX_BACKBUFFERS);

	for(i = 0; i < psDevInfo->ulNumBackBuffers; i++)
	{
		psDevInfo->asBackBuffers[i].sSysAddr.uiAddr = ui32PhysBase + (i * psDevInfo->ui32BufferSize);
		psDevInfo->asBackBuffers[i].sCPUVAddr = (IMG_CPU_VIRTADDR)(ui32ScreenBase + (i * psDevInfo->ui32BufferSize));
		psDevInfo->asBackBuffers[i].sDevVAddr.uiAddr = 0UL;
		psDevInfo->asBackBuffers[i].hSwapChain = 0;
		psDevInfo->asBackBuffers[i].psSyncData = 0;
		psDevInfo->asBackBuffers[i].ui32YOffset = i * psDevInfo->sSysDims.ui32Height;
		psDevInfo->asBackBuffers[i].psNext = 0;
	}

	return DC_OK;
}

PVRSRV_ERROR FreeBackBuffers (DC_LINUXFB_DEVINFO *psDevInfo)
{
	UNREFERENCED_PARAMETER(psDevInfo);
	return DC_OK;
}
#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device unref__ *dev)
#else
static int __init DC_LINUXFB_Init(void)
#endif
{
	int i = 0;

	gpsOSFBInfo = registered_fb[i];

	if (!gpsOSFBInfo)
	{
		printk( KERN_INFO DRVNAME ": failed to connect to FBDev\n");
		return -ENODEV;

	}

	if(Init() != DC_OK)
	{
		return -ENODEV;
	}

	return 0;
} 

#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
#else
static void __exit DC_LINUXFB_Cleanup(void)
#endif
{
	if(Deinit() != DC_OK)
	{
		printk (KERN_INFO DRVNAME ": DC_LINUXFB_Cleanup: can't deinit device\n");
	}
} 


void *AllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void FreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}

DC_ERROR OpenPVRServices (DC_HANDLE *phPVRServices)
{
	
	*phPVRServices = 0;
	return DC_OK;
}

DC_ERROR ClosePVRServices (DC_HANDLE unref__ hPVRServices)
{
	
	return DC_OK;
}

DC_ERROR GetLibFuncAddr (DC_HANDLE unref__ hExtDrv, char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return DC_ERROR_INVALID_PARAMS;
	}

	
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return DC_OK;
}

#if !defined(SUPPORT_DRI_DRM)
module_init(DC_LINUXFB_Init);
module_exit(DC_LINUXFB_Cleanup);
#endif

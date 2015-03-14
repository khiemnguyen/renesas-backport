/*************************************************************************/ /*!
@Title          Debug Functionality
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provides kernel side Debug Functionality
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

#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>			// strncpy, strlen
#include <stdarg.h>
#include <linux/seq_file.h>
#include "img_types.h"
#include "servicesext.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "mutex.h"
#include "linkage.h"
#include "pvr_uaccess.h"

#if !defined(CONFIG_PREEMPT)
#define	PVR_DEBUG_ALWAYS_USE_SPINLOCK
#endif

#if defined(PVRSRV_NEED_PVR_DPF)

/******** BUFFERED LOG MESSAGES ********/

/* Because we don't want to have to handle CCB wrapping, each buffered
 * message is rounded up to PVRSRV_DEBUG_CCB_MESG_MAX bytes. This means
 * there is the same fixed number of messages that can be stored,
 * regardless of message length.
 */

#if defined(PVRSRV_DEBUG_CCB_MAX)

#define PVRSRV_DEBUG_CCB_MESG_MAX	PVR_MAX_DEBUG_MESSAGE_LEN

#include <linux/syscalls.h>
#include <linux/time.h>

typedef struct
{
	const IMG_CHAR *pszFile;
	IMG_INT iLine;
	IMG_UINT32 ui32TID;
	IMG_CHAR pcMesg[PVRSRV_DEBUG_CCB_MESG_MAX];
	struct timeval sTimeVal;
}
PVRSRV_DEBUG_CCB;

static PVRSRV_DEBUG_CCB gsDebugCCB[PVRSRV_DEBUG_CCB_MAX] = { { 0 } };

static IMG_UINT giOffset = 0;

static PVRSRV_LINUX_MUTEX gsDebugCCBMutex;

static void
AddToBufferCCB(const IMG_CHAR *pszFileName, IMG_UINT32 ui32Line,
			   const IMG_CHAR *szBuffer)
{
	LinuxLockMutex(&gsDebugCCBMutex);

	gsDebugCCB[giOffset].pszFile = pszFileName;
	gsDebugCCB[giOffset].iLine   = ui32Line;
	gsDebugCCB[giOffset].ui32TID = current->tgid;

	do_gettimeofday(&gsDebugCCB[giOffset].sTimeVal);

	strncpy(gsDebugCCB[giOffset].pcMesg, szBuffer, PVRSRV_DEBUG_CCB_MESG_MAX - 1);
	gsDebugCCB[giOffset].pcMesg[PVRSRV_DEBUG_CCB_MESG_MAX - 1] = 0;

	giOffset = (giOffset + 1) % PVRSRV_DEBUG_CCB_MAX;

	LinuxUnLockMutex(&gsDebugCCBMutex);
}

IMG_EXPORT IMG_VOID PVRSRVDebugPrintfDumpCCB(void)
{
	int i;

	LinuxLockMutex(&gsDebugCCBMutex);
	
	for(i = 0; i < PVRSRV_DEBUG_CCB_MAX; i++)
	{
		PVRSRV_DEBUG_CCB *psDebugCCBEntry =
			&gsDebugCCB[(giOffset + i) % PVRSRV_DEBUG_CCB_MAX];

		/* Early on, we won't have PVRSRV_DEBUG_CCB_MAX messages */
		if(!psDebugCCBEntry->pszFile)
			continue;

		printk("%s:%d:\t[%5ld.%6ld] %s\n",
			   psDebugCCBEntry->pszFile,
			   psDebugCCBEntry->iLine,
			   (long)psDebugCCBEntry->sTimeVal.tv_sec,
			   (long)psDebugCCBEntry->sTimeVal.tv_usec,
			   psDebugCCBEntry->pcMesg);
	}

	LinuxUnLockMutex(&gsDebugCCBMutex);
}

#else /* defined(PVRSRV_DEBUG_CCB_MAX) */
static INLINE void
AddToBufferCCB(const IMG_CHAR *pszFileName, IMG_UINT32 ui32Line,
               const IMG_CHAR *szBuffer)
{
	(void)pszFileName;
	(void)szBuffer;
	(void)ui32Line;
}

IMG_EXPORT IMG_VOID PVRSRVDebugPrintfDumpCCB(void)
{
	/* Not available */
}

#endif /* defined(PVRSRV_DEBUG_CCB_MAX) */

#endif /* defined(PVRSRV_NEED_PVR_DPF) */

static IMG_BOOL VBAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz,
						 const IMG_CHAR* pszFormat, va_list VArgs)
						 IMG_FORMAT_PRINTF(3, 0);


#if defined(PVRSRV_NEED_PVR_DPF)

/* NOTE: Must NOT be static! Used in module.c.. */
IMG_UINT32 gPVRDebugLevel =
	(DBGPRIV_FATAL | DBGPRIV_ERROR | DBGPRIV_WARNING | DBGPRIV_BUFFERED);

#endif /* defined(PVRSRV_NEED_PVR_DPF) || defined(PVRSRV_NEED_PVR_TRACE) */

#define	PVR_MAX_MSG_LEN PVR_MAX_DEBUG_MESSAGE_LEN

#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
/* Message buffer for non-IRQ messages */
static IMG_CHAR gszBufferNonIRQ[PVR_MAX_MSG_LEN + 1];
#endif

/* Message buffer for IRQ messages */
static IMG_CHAR gszBufferIRQ[PVR_MAX_MSG_LEN + 1];

#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
/* The lock is used to control access to gszBufferNonIRQ */
static PVRSRV_LINUX_MUTEX gsDebugMutexNonIRQ;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
/* The lock is used to control access to gszBufferIRQ */
/* PRQA S 0671,0685 1 */ /* ignore warnings about C99 style initialisation */
static spinlock_t gsDebugLockIRQ = SPIN_LOCK_UNLOCKED;
#else
static DEFINE_SPINLOCK(gsDebugLockIRQ);
#endif

#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
#if !defined (USE_SPIN_LOCK) /* to keep QAC happy */ 
#define	USE_SPIN_LOCK (in_interrupt() || !preemptible())
#endif
#endif

#if defined(ENABLE_REL_STD_0001)

#include <linux/vmalloc.h>
#include "pvr_bridge_km.h"

#if 0
#define DEBUG_LOG_OUTPUT_LEVEL_ERROR         0x00000001
#define DEBUG_LOG_OUTPUT_LEVEL_WARNING       0x00000002
#define DEBUG_LOG_OUTPUT_LEVEL_LOCKUP        0x00000004
#define DEBUG_LOG_OUTPUT_LEVEL_KNOWN_ISSUE   0x00000008
#define DEBUG_LOG_OUTPUT_LEVEL_SPM           0x00000010
#define DEBUG_LOG_OUTPUT_LEVEL_CBUF_WAITTIME 0x00000020
#define DEBUG_LOG_OUTPUT_LEVEL_EDM_TRACE     0x00000040
#define DEBUG_LOG_OUTPUT_LEVEL_MEM           0x00000080
#define DEBUG_LOG_OUTPUT_LEVEL_DEFAULT       0x0000001d /* (ERROR | LOCKUP | KNOWN_ISSUE | SPM) */
#endif

#define DEBUG_LOG_BUFFER_ENTRY_MAX   65536
#define DEBUG_LOG_MESSAGE_LEN_MAX   (256-8)  /* 8 --- ui64Time : sizeof(IMG_UINT64) */

#define DEBUG_LOG_ENABLE_BUFFERING_KM_DEFAULT 1
#define DEBUG_LOG_BUFFER_ENTRY_DEFAULT   1024

IMG_UINT32 DebugLogOutputLevel       = (IMG_UINT32)DEBUG_LOG_OUTPUT_LEVEL_DEFAULT;
IMG_UINT32 DebugLogBufferEntryKM     = (IMG_UINT32)DEBUG_LOG_BUFFER_ENTRY_DEFAULT;
IMG_UINT32 DebugLogEnableBufferingKM = (IMG_UINT32)DEBUG_LOG_ENABLE_BUFFERING_KM_DEFAULT;

/* The member and size of tag_DEBUG_LOG_BUFFER must be the same as UM's tag_DEBUG_LOG_BUFFER */
typedef struct tag_DEBUG_LOG_BUFFER
{
    IMG_UINT64 ui64Time;
    IMG_CHAR   szLogMessage[DEBUG_LOG_MESSAGE_LEN_MAX];
} DEBUG_LOG_BUFFER;
static DEBUG_LOG_BUFFER *gsDebugLogBuffer = IMG_NULL;

static IMG_UINT32 gui32DebugLogEnableBufferingKM = 1;
static IMG_UINT32 gui32DebugLogBufferEntryKM     = 0;
static IMG_UINT32 gui32DebugLogMessageCounter = 0;
static IMG_UINT32 gui32DebugLogBufferSize = 0;
static IMG_UINT32 gui32DebugLogEnableBufferWriting = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))
static spinlock_t gsDebugLogBufferLock = SPIN_LOCK_UNLOCKED;
#else
static DEFINE_SPINLOCK(gsDebugLogBufferLock);
#endif
static IMG_BOOL   gbDebugLogInitialised = IMG_FALSE;

static IMG_VOID DebugLogREL(IMG_UINT32 ui32DebugLevel,
                                 const IMG_CHAR *pszFuncName,
                                 IMG_UINT32 ui32Line,
                                 const IMG_CHAR *pszFormat,
                                 ...) IMG_FORMAT_PRINTF(4, 5);
static IMG_VOID DebugLogNOP(IMG_UINT32 unref__ ui32DebugLevel,
                                 const IMG_CHAR unref__ *pszFuncName,
                                 IMG_UINT32 unref__ ui32Line,
                                 const IMG_CHAR unref__ *pszFormat,
                                 ...) IMG_FORMAT_PRINTF(4, 5);
static IMG_VOID DebugLogSetLogFunctionNOP(IMG_VOID);
static IMG_VOID DebugLogSetLogFunction(IMG_UINT32 ui32Value);

IMG_EXPORT IMG_VOID (*pfuncERRORLogFunction)(IMG_UINT32 ui32DebugLevel,
                                             const IMG_CHAR *pszFuncName,
                                             IMG_UINT32 ui32Line,
                                             const IMG_CHAR *pszFormat,
                                             ...) = DebugLogREL;
IMG_EXPORT IMG_VOID (*pfuncWARNINGLogFunction)(IMG_UINT32 ui32DebugLevel,
                                               const IMG_CHAR *pszFuncName,
                                               IMG_UINT32 ui32Line,
                                               const IMG_CHAR *pszFormat,
                                               ...) = DebugLogREL;
IMG_EXPORT IMG_VOID (*pfuncLOCKUPLogFunction)(IMG_UINT32 ui32DebugLevel,
                                              const IMG_CHAR *pszFuncName,
                                              IMG_UINT32 ui32Line,
                                              const IMG_CHAR *pszFormat,
                                              ...) = DebugLogREL;
IMG_EXPORT IMG_VOID (*pfuncKNOWNISSUELogFunction)(IMG_UINT32 ui32DebugLevel,
                                                  const IMG_CHAR *pszFuncName,
                                                  IMG_UINT32 ui32Line,
                                                  const IMG_CHAR *pszFormat,
                                                  ...) = DebugLogREL;
IMG_EXPORT IMG_VOID (*pfuncSPMLogFunction)(IMG_UINT32 ui32DebugLevel,
                                           const IMG_CHAR *pszFuncName,
                                           IMG_UINT32 ui32Line,
                                           const IMG_CHAR *pszFormat,
                                           ...) = DebugLogREL;
IMG_EXPORT IMG_VOID (*pfuncMEMLogFunction)(IMG_UINT32 ui32DebugLevel,
                                           const IMG_CHAR *pszFuncName,
                                           IMG_UINT32 ui32Line,
                                           const IMG_CHAR *pszFormat,
                                           ...) = DebugLogREL;
#if defined(DEBUG)
IMG_EXPORT IMG_VOID (*pfuncLogFunctionNOP)(IMG_UINT32 ui32DebugLevel,
                                           const IMG_CHAR *pszFuncName,
                                           IMG_UINT32 ui32Line,
                                           const IMG_CHAR *pszFormat,
                                           ...) = DebugLogNOP;
#endif

static IMG_VOID DebugLogPrintf( IMG_VOID *pvData, IMG_UINT64 ui64Time )
{
    IMG_UINT32 ui32Counter;
    unsigned long ulDebugLogBufferLockFlags;

    spin_lock_irqsave(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);

    if(gui32DebugLogEnableBufferWriting == 1)
    {
        ui32Counter = gui32DebugLogMessageCounter++;
        gui32DebugLogMessageCounter = gui32DebugLogMessageCounter % gui32DebugLogBufferEntryKM;
        (gsDebugLogBuffer+ui32Counter)->ui64Time = ui64Time;
        memcpy( (gsDebugLogBuffer+ui32Counter)->szLogMessage, pvData, DEBUG_LOG_MESSAGE_LEN_MAX );
    }

    spin_unlock_irqrestore(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);
}

static IMG_VOID DebugLogSetStrings( IMG_UINT32 ui32DebugLevel, IMG_CHAR *pszBuffer, IMG_UINT64 *pui64Time )
{
    size_t stLen = 0;

    *pui64Time = OSGetCurrentTimeus();
    sprintf (pszBuffer, "time[%llu] ", *pui64Time);
    stLen = strlen(pszBuffer);

    snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "tid[%u,%u] ", OSGetCurrentProcessIDKM(), OSGetCurrentThreadIDKM());
    stLen = strlen(pszBuffer);

    switch(ui32DebugLevel)
    {
        case DBGPRIV_FATAL:
        case DBGPRIV_ERROR:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][ERROR] ");
            break;

        case DBGPRIV_WARNING:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][WARNING] ");
            break;

        case DBGPRIV_CALLTRACE:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][API] ");
            break;

        case DBGPRIV_LOCKUP:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][LOCKUP] ");
            break;

        case DBGPRIV_KNOWNISSUE:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][KNOWNISSUE] ");
            break;

        case DBGPRIV_SPM:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][SPM] ");
            break;

#if 0
        case DBGPRIV_KICK_ISR:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][KICK_ISR] ");
            break;

        case DBGPRIV_COM:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][COMMUNICATION]");
            break;
#endif

        case DBGPRIV_MEM:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][MEM]");
            break;

#if 0
        case DBGPRIV_CBUF_WAIT:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][CBUF_WAIT] ");
            break;
#endif

        default:
            snprintf (&pszBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, "[KM][???] ");
            break;
    }
}

IMG_VOID PVRSRVDebugLogDeinit(IMG_VOID)
{
    if(gsDebugLogBuffer)
    {
        vfree(gsDebugLogBuffer);
    }
}

PVRSRV_ERROR PVRSRVDebugLogInitKM( IMG_VOID )
{
    PVRSRV_ERROR  eError = PVRSRV_OK;

    gui32DebugLogEnableBufferingKM = DebugLogEnableBufferingKM;
    gui32DebugLogBufferEntryKM     = DebugLogBufferEntryKM;

    if( (gui32DebugLogBufferEntryKM != 0) && (gui32DebugLogEnableBufferingKM != 0) )
    {
        if(gui32DebugLogBufferEntryKM > DEBUG_LOG_BUFFER_ENTRY_MAX)
        {
            gui32DebugLogBufferEntryKM = DEBUG_LOG_BUFFER_ENTRY_MAX;
        }
        gui32DebugLogBufferSize = gui32DebugLogBufferEntryKM * sizeof(DEBUG_LOG_BUFFER);
        gsDebugLogBuffer = (DEBUG_LOG_BUFFER *)vmalloc(gui32DebugLogBufferSize);
        if(!gsDebugLogBuffer)
        {
            printk("%s %d ERROR: Create log buffer is failed.\n", __FUNCTION__, __LINE__);
            eError = PVRSRV_ERROR_OUT_OF_MEMORY;
            DebugLogSetLogFunctionNOP();
            goto debug_log_init_km_return;
        }
        
        memset(gsDebugLogBuffer, 0, gui32DebugLogBufferSize);
    }

    if( (gui32DebugLogBufferEntryKM     == 0) &&
        (gui32DebugLogEnableBufferingKM != 0) )
    {
        DebugLogSetLogFunctionNOP();
    }
    else
    {
        DebugLogSetLogFunction(DebugLogOutputLevel);
    }

    gui32DebugLogEnableBufferWriting = 1;
    gbDebugLogInitialised = IMG_TRUE;

debug_log_init_km_return:

    return eError;
}

static PVRSRV_ERROR PVRSRVDebugLogGetInfoKM( PVRSRV_DEBUG_LOG_INFO *psDebugLogInfo )
{
    PVRSRV_ERROR  eError = PVRSRV_OK;

    psDebugLogInfo->ui32DebugLogBufferEntryKM     = gui32DebugLogBufferEntryKM;
    psDebugLogInfo->ui32DebugLogEnableBufferingKM = gui32DebugLogEnableBufferingKM;

    return eError;
}

static PVRSRV_ERROR PVRSRVDebugLogCopyBufferKM( PVRSRV_DEBUG_LOG_INFO *psDebugLogInfo )
{
    PVRSRV_ERROR  eError = PVRSRV_OK;

    if(gsDebugLogBuffer)
    {
        if(copy_to_user(psDebugLogInfo->pvDebugLogBuffer, gsDebugLogBuffer, gui32DebugLogBufferSize) != 0)
        {
            printk("%s %d ERROR: copy_to_user() is failed.\n", __FUNCTION__, __LINE__);
            eError = PVRSRV_ERROR_FAILED_TO_COPY_VIRT_MEMORY;
        }
        psDebugLogInfo->ui32DebugLogMessageCounter = gui32DebugLogMessageCounter;
    }
    else
    {
        printk("%s %d ERROR: Log buffer is not created.\n", __FUNCTION__, __LINE__);
        eError = PVRSRV_ERROR_OUT_OF_MEMORY;
    }

    return eError;
}

static PVRSRV_ERROR PVRSRVDebugLogEnableBufferWritingKM(IMG_VOID)
{
    unsigned long ulDebugLogBufferLockFlags;
    PVRSRV_ERROR  eError = PVRSRV_OK;

    spin_lock_irqsave(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);
    gui32DebugLogEnableBufferWriting = 1;
    spin_unlock_irqrestore(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);

    return eError;
}

static PVRSRV_ERROR PVRSRVDebugLogDisableBufferWritingKM(IMG_VOID)
{
    unsigned long ulDebugLogBufferLockFlags;
    PVRSRV_ERROR  eError = PVRSRV_OK;

    spin_lock_irqsave(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);
    gui32DebugLogEnableBufferWriting = 0;
    spin_unlock_irqrestore(&gsDebugLogBufferLock, ulDebugLogBufferLockFlags);

    return eError;
}

PVRSRV_ERROR PVRSRVDebugLogSetLoggingStateKM(PVRSRV_DEBUG_LOG_LOGGING_STATE eLoggingState)
{
    PVRSRV_ERROR  eError = PVRSRV_OK;
    
    switch(eLoggingState)
    {
        case PVRSRV_DEBUG_LOG_LOGGING_STATE_ALL_ENABLE:
        {
            eError = PVRSRVDebugLogEnableBufferWritingKM();
            break;
        }
        case PVRSRV_DEBUG_LOG_LOGGING_STATE_ALL_DISABLE:
        {
            eError = PVRSRVDebugLogDisableBufferWritingKM();
            break;
        }

        case PVRSRV_DEBUG_LOG_LOGGING_STATE_MEM_ENABLE:
        {
            pfuncMEMLogFunction = DebugLogREL;
            break;
        }
        case PVRSRV_DEBUG_LOG_LOGGING_STATE_MEM_DISABLE:
        {
            pfuncMEMLogFunction = DebugLogNOP;
            break;
        }
        default:
        {
            printk("%s %d ERROR: unknown state type(0x%08x)\n", __FUNCTION__, __LINE__, eLoggingState);
            eError = PVRSRV_ERROR_INVALID_PARAMS;
            goto ret;
        }
    }

ret:
    return eError;
}

const IMG_CHAR *PVRSRVDebugTrancateFileName(const IMG_CHAR *pszFullFileName)
{
    const IMG_CHAR *pszCurrent  = IMG_NULL;
    const IMG_CHAR *pszFileName = IMG_NULL;
    
    pszCurrent = pszFullFileName;

    while('\0' != *pszCurrent)
    {
        if('/' == *pszCurrent)
        {
            pszFileName = pszCurrent+1;
        }
        pszCurrent++;
    }
    
    if(IMG_NULL != pszFileName)
    {
        return pszFileName;
    }
    else
    {
        return pszFullFileName;
    }
}

static IMG_VOID DebugLogNOP(IMG_UINT32 unref__ ui32DebugLevel,
                            const IMG_CHAR unref__ *pszFuncName,
                            IMG_UINT32 unref__ ui32Line,
                            const IMG_CHAR unref__ *pszFormat,
                            ...)
{
    return;
}

static IMG_VOID DebugLogSetLogFunctionNOP(IMG_VOID)
{
    pfuncERRORLogFunction      = DebugLogNOP;
    pfuncWARNINGLogFunction    = DebugLogNOP;
    pfuncLOCKUPLogFunction     = DebugLogNOP;
    pfuncKNOWNISSUELogFunction = DebugLogNOP;
    pfuncSPMLogFunction        = DebugLogNOP;
    pfuncMEMLogFunction        = DebugLogNOP;

    return;
}

static IMG_VOID DebugLogSetLogFunction(IMG_UINT32 ui32Value)
{
    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_ERROR)
    {
        pfuncERRORLogFunction = DebugLogREL;
    }
    else
    {
        pfuncERRORLogFunction = DebugLogNOP;
    }

    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_WARNING)
    {
        pfuncWARNINGLogFunction = DebugLogREL;
    }
    else
    {
        pfuncWARNINGLogFunction = DebugLogNOP;
    }

    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_LOCKUP)
    {
        pfuncLOCKUPLogFunction = DebugLogREL;
    }
    else
    {
        pfuncLOCKUPLogFunction = DebugLogNOP;
    }

    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_KNOWN_ISSUE)
    {
        pfuncKNOWNISSUELogFunction = DebugLogREL;
    }
    else
    {
        pfuncKNOWNISSUELogFunction = DebugLogNOP;
    }

    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_SPM)
    {
        pfuncSPMLogFunction = DebugLogREL;
    }
    else
    {
        pfuncSPMLogFunction = DebugLogNOP;
    }

    if(ui32Value & DEBUG_LOG_OUTPUT_LEVEL_MEM)
    {
        pfuncMEMLogFunction = DebugLogNOP;
    }
    else
    {
        pfuncMEMLogFunction = DebugLogNOP;
    }


    return;
}

static IMG_VOID DebugLogREL(IMG_UINT32 ui32DebugLevel,
                            const IMG_CHAR *pszFuncName,
                            IMG_UINT32 ui32Line,
                            const IMG_CHAR *pszFormat,
                            ...)
{
    IMG_CHAR szBuffer[DEBUG_LOG_MESSAGE_LEN_MAX] = {0};
    IMG_UINT64 ui64Time = 0;
    va_list vaArgs;
    size_t stLen;

    DebugLogSetStrings(ui32DebugLevel, szBuffer, &ui64Time);

    stLen = strlen(szBuffer);
    snprintf (&szBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen,
              "[%s, %d] ", pszFuncName, (IMG_INT)ui32Line);

    stLen = strlen(szBuffer);
    va_start (vaArgs, pszFormat);
    vsnprintf (&szBuffer[stLen], DEBUG_LOG_MESSAGE_LEN_MAX - stLen, pszFormat, vaArgs);
    va_end (vaArgs);

    if( (gui32DebugLogEnableBufferingKM == 0) || (gbDebugLogInitialised == IMG_FALSE) )
    {
        printk("%s\n",szBuffer);
    }
    else
    {
        DebugLogPrintf( szBuffer, ui64Time );
    }
}
#endif /* defined(ENABLE_REL_STD_0001) */

static inline void GetBufferLock(unsigned long *pulLockFlags)
{
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	if (USE_SPIN_LOCK)
#endif
	{
		spin_lock_irqsave(&gsDebugLockIRQ, *pulLockFlags);
	}
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	else
	{
		LinuxLockMutexNested(&gsDebugMutexNonIRQ, PVRSRV_LOCK_CLASS_PVR_DEBUG);
	}
#endif
}

static inline void ReleaseBufferLock(unsigned long ulLockFlags)
{
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	if (USE_SPIN_LOCK)
#endif
	{
		spin_unlock_irqrestore(&gsDebugLockIRQ, ulLockFlags);
	}
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	else
	{
		LinuxUnLockMutex(&gsDebugMutexNonIRQ);
	}
#endif
}

static inline void SelectBuffer(IMG_CHAR **ppszBuf, IMG_UINT32 *pui32BufSiz)
{
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	if (USE_SPIN_LOCK)
#endif
	{
		*ppszBuf = gszBufferIRQ;
		*pui32BufSiz = sizeof(gszBufferIRQ);
	}
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	else
	{
		*ppszBuf = gszBufferNonIRQ;
		*pui32BufSiz = sizeof(gszBufferNonIRQ);
	}
#endif
}

/*
 * Append a string to a buffer using formatted conversion.
 * The function takes a variable number of arguments, pointed
 * to by the var args list.
 */
static IMG_BOOL VBAppend(IMG_CHAR *pszBuf, IMG_UINT32 ui32BufSiz, const IMG_CHAR* pszFormat, va_list VArgs)
{
	IMG_UINT32 ui32Used;
	IMG_UINT32 ui32Space;
	IMG_INT32 i32Len;

	ui32Used = strlen(pszBuf);
	BUG_ON(ui32Used >= ui32BufSiz);
	ui32Space = ui32BufSiz - ui32Used;

	i32Len = vsnprintf(&pszBuf[ui32Used], ui32Space, pszFormat, VArgs);
	pszBuf[ui32BufSiz - 1] = 0;

	/* Return true if string was truncated */
	return (i32Len < 0 || i32Len >= (IMG_INT32)ui32Space) ? IMG_TRUE : IMG_FALSE;
}

/* Actually required for ReleasePrintf too */

IMG_VOID PVRDPFInit(IMG_VOID)
{
#if !defined(PVR_DEBUG_ALWAYS_USE_SPINLOCK)
	LinuxInitMutex(&gsDebugMutexNonIRQ);
#endif
#if defined(PVRSRV_DEBUG_CCB_MAX)
	LinuxInitMutex(&gsDebugCCBMutex);
#endif
}

/*!
******************************************************************************
	@Function    PVRSRVReleasePrintf
	@Description To output an important message to the user in release builds
	@Input       pszFormat - The message format string
	@Input       ... - Zero or more arguments for use by the format string
	@Return      None
 ******************************************************************************/
IMG_VOID PVRSRVReleasePrintf(const IMG_CHAR *pszFormat, ...)
{
	va_list vaArgs;
	unsigned long ulLockFlags = 0;
	IMG_CHAR *pszBuf;
	IMG_UINT32 ui32BufSiz;

	SelectBuffer(&pszBuf, &ui32BufSiz);

	va_start(vaArgs, pszFormat);

	GetBufferLock(&ulLockFlags);
	strncpy (pszBuf, "PVR_K: ", (ui32BufSiz -1));

	if (VBAppend(pszBuf, ui32BufSiz, pszFormat, vaArgs))
	{
		printk(KERN_INFO "PVR_K:(Message Truncated): %s\n", pszBuf);
	}
	else
	{
		printk(KERN_INFO "%s\n", pszBuf);
	}

	ReleaseBufferLock(ulLockFlags);
	va_end(vaArgs);
}

#if defined(PVRSRV_NEED_PVR_TRACE)

/*!
******************************************************************************
	@Function    PVRTrace
	@Description To output a debug message to the user
	@Input       pszFormat - The message format string
	@Input       ... - Zero or more arguments for use by the format string
	@Return      None
 ******************************************************************************/
IMG_VOID PVRSRVTrace(const IMG_CHAR* pszFormat, ...)
{
	va_list VArgs;
	unsigned long ulLockFlags = 0;
	IMG_CHAR *pszBuf;
	IMG_UINT32 ui32BufSiz;

	SelectBuffer(&pszBuf, &ui32BufSiz);

	va_start(VArgs, pszFormat);

	GetBufferLock(&ulLockFlags);

	strncpy(pszBuf, "PVR: ", (ui32BufSiz -1));

	if (VBAppend(pszBuf, ui32BufSiz, pszFormat, VArgs))
	{
		printk(KERN_INFO "PVR_K:(Message Truncated): %s\n", pszBuf);
	}
	else
	{
		printk(KERN_INFO "%s\n", pszBuf);
	}

	ReleaseBufferLock(ulLockFlags);

	va_end(VArgs);
}

#endif /* defined(PVRSRV_NEED_PVR_TRACE) */

#if defined(PVRSRV_NEED_PVR_DPF)

/*!
******************************************************************************
	@Function    PVRSRVDebugPrintf
	@Description To output a debug message to the user
	@Input       uDebugLevel - The current debug level
	@Input       pszFile - The source file generating the message
	@Input       uLine - The line of the source file
	@Input       pszFormat - The message format string
	@Input       ... - Zero or more arguments for use by the format string
	@Return      None
 ******************************************************************************/
IMG_VOID PVRSRVDebugPrintf	(
						IMG_UINT32	ui32DebugLevel,
						const IMG_CHAR*	pszFullFileName,
						IMG_UINT32	ui32Line,
						const IMG_CHAR*	pszFormat,
						...
					)
{
	IMG_BOOL bTrace;
	const IMG_CHAR *pszFileName = pszFullFileName;

	bTrace = (IMG_BOOL)(ui32DebugLevel & DBGPRIV_CALLTRACE) ? IMG_TRUE : IMG_FALSE;

	if (gPVRDebugLevel & ui32DebugLevel)
	{
		va_list vaArgs;
		unsigned long ulLockFlags = 0;
		IMG_CHAR *pszBuf;
		IMG_UINT32 ui32BufSiz;

		SelectBuffer(&pszBuf, &ui32BufSiz);

		va_start(vaArgs, pszFormat);

		GetBufferLock(&ulLockFlags);

		/* Add in the level of warning */
		if (bTrace == IMG_FALSE)
		{
			switch(ui32DebugLevel)
			{
				case DBGPRIV_FATAL:
				{
					strncpy (pszBuf, "PVR_K:(Fatal): ", (ui32BufSiz -1));
					break;
				}
				case DBGPRIV_ERROR:
				{
					strncpy (pszBuf, "PVR_K:(Error): ", (ui32BufSiz -1));
					break;
				}
				case DBGPRIV_WARNING:
				{
					strncpy (pszBuf, "PVR_K:(Warning): ", (ui32BufSiz -1));
					break;
				}
				case DBGPRIV_MESSAGE:
				{
					strncpy (pszBuf, "PVR_K:(Message): ", (ui32BufSiz -1));
					break;
				}
				case DBGPRIV_VERBOSE:
				{
					strncpy (pszBuf, "PVR_K:(Verbose): ", (ui32BufSiz -1));
					break;
				}
				case DBGPRIV_BUFFERED:
				{
					strncpy (pszBuf, "PVR_K: ", (ui32BufSiz -1));
					break;
				}
				default:
				{
					strncpy (pszBuf, "PVR_K:(Unknown message level): ", (ui32BufSiz -1));
					break;
				}
			}
		}
		else
		{
			strncpy (pszBuf, "PVR_K: ", (ui32BufSiz -1));
		}

		if (VBAppend(pszBuf, ui32BufSiz, pszFormat, vaArgs))
		{
			printk(KERN_INFO "PVR_K:(Message Truncated): %s\n", pszBuf);
		}
		else
		{
			if (ui32DebugLevel & DBGPRIV_BUFFERED)
			{
				/* We don't need the full path here */
				const IMG_CHAR *pszShortName = strrchr(pszFileName, '/') + 1;
				if(pszShortName)
					pszFileName = pszShortName;

				AddToBufferCCB(pszFileName, ui32Line, pszBuf);
			}
			else
			{
				printk(KERN_INFO "%s\n", pszBuf);
			}
		}

		ReleaseBufferLock(ulLockFlags);

		va_end (vaArgs);
	}
}

#endif /* PVRSRV_NEED_PVR_DPF */

#if defined(DEBUG)

IMG_INT PVRDebugProcSetLevel(struct file *file, const IMG_CHAR *buffer, IMG_UINT32 count, IMG_VOID *data)
{
#define	_PROC_SET_BUFFER_SZ		6
	IMG_CHAR data_buffer[_PROC_SET_BUFFER_SZ];

	PVR_UNREFERENCED_PARAMETER(file);
	PVR_UNREFERENCED_PARAMETER(data);

	if (count > _PROC_SET_BUFFER_SZ)
	{
		return -EINVAL;
	}
	else
	{
		if (pvr_copy_from_user(data_buffer, buffer, count))
			return -EINVAL;
		if (data_buffer[count - 1] != '\n')
			return -EINVAL;
		if (sscanf(data_buffer, "%i", &gPVRDebugLevel) == 0)
			return -EINVAL;
		gPVRDebugLevel &= (1 << DBGPRIV_DBGLEVEL_COUNT) - 1;
	}
	return (count);
}

void ProcSeqShowDebugLevel(struct seq_file *sfile, void* el)
{
	PVR_UNREFERENCED_PARAMETER(el);

	seq_printf(sfile, "%u\n", gPVRDebugLevel);
}

#endif /* defined(DEBUG) */

#if defined(ENABLE_REL_STD_0001) || defined(SUPPORT_GET_MEMINFO)
#include "pvr_bridge_km.h"
#include "perproc.h"
#include "services.h"
#include "mm.h"

#if defined(SUPPORT_GET_MEMINFO)
#define DEBUG_MEM_TRACE_DISABLE 0x00000000U
#define DEBUG_MEM_TRACE_ENABLE  0x00000001U
IMG_BYTE g_bIsDebugMemAllocTrace = (IMG_BYTE)DEBUG_MEM_TRACE_DISABLE;
#endif /* defined(SUPPORT_GET_MEMINFO) */

PVRSRV_ERROR PVRSRVDebugGetInfoKM(PVRSRV_PER_PROCESS_DATA *psPerProc, PVRSRV_DEBUG_INFO sDebugInfo)
{
    PVRSRV_ERROR eError = PVRSRV_OK;
    switch(sDebugInfo.eInfoType)
    {
#if defined(ENABLE_REL_STD_0001)
        case PVRSRV_DEBUG_INFO_TYPE_DEBLOG_INFO:
        {
            eError = PVRSRVDebugLogGetInfoKM((PVRSRV_DEBUG_LOG_INFO *)(sDebugInfo.uBuffer.psDebugLogInfo));
            break;
        }
        case PVRSRV_DEBUG_INFO_TYPE_DEBLOG_BUFF:
        {
            eError = PVRSRVDebugLogCopyBufferKM((PVRSRV_DEBUG_LOG_INFO *)(sDebugInfo.uBuffer.psDebugLogInfo));
            break;
        }
#endif /* defined(ENABLE_REL_STD_0001) */
#if defined(SUPPORT_GET_MEMINFO)
        case PVRSRV_DEBUG_INFO_TYPE_MEM_USAGE:
        {
            PVRSRV_DEBUG_INFO_MEM_USAGE sMemUsage;
            OSMemSet(&sMemUsage, 0, sizeof(PVRSRV_DEBUG_INFO_MEM_USAGE));
            DebugMemGetMemoryUsage(&sMemUsage);
            if( sizeof(PVRSRV_DEBUG_INFO_MEM_USAGE) != sDebugInfo.ui32BuffSize)
            {
                PVR_DPF((PVR_DBG_WARNING, "buffer size is not match"));
            }
            eError = OSCopyToUser(psPerProc, sDebugInfo.uBuffer.psMemUsage, &sMemUsage, sDebugInfo.ui32BuffSize);
            if( PVRSRV_OK != eError)
            {
                PVR_DPF((PVR_DBG_ERROR, "copy_to_user failed"));
                goto ret;
            }
            break;
        }
#endif /* defined(SUPPORT_GET_MEMINFO) */
        default:
        {
            PVR_DPF((PVR_DBG_ERROR, "unknown info type(0x%08x)", sDebugInfo.eInfoType));
            eError = PVRSRV_ERROR_INVALID_PARAMS;
            goto ret;
        }
    }

ret:
    return eError;
}

IMG_BOOL PVRSRVDebugGetMemTraceState(IMG_VOID)
{
#if defined(ENABLE_REL_STD_0001)
    if(DEBUG_LOG_OUTPUT_LEVEL_MEM & DebugLogOutputLevel)
    {
        return IMG_TRUE;
    }
#endif /* defined(ENABLE_REL_STD_0001) */

#if defined(SUPPORT_GET_MEMINFO)
    if((IMG_BYTE)DEBUG_MEM_TRACE_ENABLE == g_bIsDebugMemAllocTrace)
    {
        return IMG_TRUE;
    }
#endif /* defined(SUPPORT_GET_MEMINFO) */

    return IMG_FALSE;
}
#endif /* defined(ENABLE_REL_STD_0001) || defined(SUPPORT_GET_MEMINFO) */


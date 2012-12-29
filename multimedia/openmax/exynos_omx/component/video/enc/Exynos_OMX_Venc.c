/*
 *
 * Copyright 2010 Samsung Electronics S.LSI Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @file        Exynos_OMX_Venc.c
 * @brief
 * @author      SeungBeom Kim (sbcrux.kim@samsung.com)
 *              Yunji Kim (yunji.kim@samsung.com)
 * @version     1.1.0
 * @history
 *   2010.7.15 : Create
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Exynos_OMX_Macros.h"
#include "Exynos_OSAL_Event.h"
#include "Exynos_OMX_Venc.h"
#include "Exynos_OMX_Basecomponent.h"
#include "Exynos_OSAL_Thread.h"
#include "Exynos_OSAL_Mutex.h"
#include "Exynos_OSAL_Semaphore.h"
#include "Exynos_OSAL_ETC.h"
#include "color_space_convertor.h"

#ifdef USE_STOREMETADATA
#include "Exynos_OSAL_Android.h"
#endif

#undef  EXYNOS_LOG_TAG
#define EXYNOS_LOG_TAG    "EXYNOS_VIDEO_ENC"
#define EXYNOS_LOG_OFF
#include "Exynos_OSAL_Log.h"


inline void Exynos_UpdateFrameSize(OMX_COMPONENTTYPE *pOMXComponent)
{
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_BASEPORT      *exynosInputPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    EXYNOS_OMX_BASEPORT      *exynosOutputPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];

    if ((exynosOutputPort->portDefinition.format.video.nFrameWidth !=
            exynosInputPort->portDefinition.format.video.nFrameWidth) ||
        (exynosOutputPort->portDefinition.format.video.nFrameHeight !=
            exynosInputPort->portDefinition.format.video.nFrameHeight)) {
        OMX_U32 width = 0, height = 0;

        exynosOutputPort->portDefinition.format.video.nFrameWidth =
            exynosInputPort->portDefinition.format.video.nFrameWidth;
        exynosOutputPort->portDefinition.format.video.nFrameHeight =
            exynosInputPort->portDefinition.format.video.nFrameHeight;
        width = exynosOutputPort->portDefinition.format.video.nStride =
            exynosInputPort->portDefinition.format.video.nStride;
        height = exynosOutputPort->portDefinition.format.video.nSliceHeight =
            exynosInputPort->portDefinition.format.video.nSliceHeight;

        if (width && height)
            exynosOutputPort->portDefinition.nBufferSize = (width * height * 3) / 2;
    }

  return ;
}

OMX_ERRORTYPE Exynos_OMX_UseBuffer(
    OMX_IN OMX_HANDLETYPE            hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBufferHdr,
    OMX_IN OMX_U32                   nPortIndex,
    OMX_IN OMX_PTR                   pAppPrivate,
    OMX_IN OMX_U32                   nSizeBytes,
    OMX_IN OMX_U8                   *pBuffer)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;
    OMX_BUFFERHEADERTYPE     *temp_bufferHeader = NULL;
    OMX_U32                   i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pExynosPort = &pExynosComponent->pExynosPort[nPortIndex];
    if (nPortIndex >= pExynosComponent->portParam.nPorts) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }
    if (pExynosPort->portState != OMX_StateIdle) {
        ret = OMX_ErrorIncorrectStateOperation;
        goto EXIT;
    }

    if (CHECK_PORT_TUNNELED(pExynosPort) && CHECK_PORT_BUFFER_SUPPLIER(pExynosPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    temp_bufferHeader = (OMX_BUFFERHEADERTYPE *)Exynos_OSAL_Malloc(sizeof(OMX_BUFFERHEADERTYPE));
    if (temp_bufferHeader == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    Exynos_OSAL_Memset(temp_bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));

    for (i = 0; i < pExynosPort->portDefinition.nBufferCountActual; i++) {
        if (pExynosPort->bufferStateAllocate[i] == BUFFER_STATE_FREE) {
            pExynosPort->bufferHeader[i] = temp_bufferHeader;
            pExynosPort->bufferStateAllocate[i] = (BUFFER_STATE_ASSIGNED | HEADER_STATE_ALLOCATED);
            INIT_SET_SIZE_VERSION(temp_bufferHeader, OMX_BUFFERHEADERTYPE);
            temp_bufferHeader->pBuffer        = pBuffer;
            temp_bufferHeader->nAllocLen      = nSizeBytes;
            temp_bufferHeader->pAppPrivate    = pAppPrivate;
            if (nPortIndex == INPUT_PORT_INDEX)
                temp_bufferHeader->nInputPortIndex = INPUT_PORT_INDEX;
            else
                temp_bufferHeader->nOutputPortIndex = OUTPUT_PORT_INDEX;

            pExynosPort->assignedBufferNum++;
            if (pExynosPort->assignedBufferNum == pExynosPort->portDefinition.nBufferCountActual) {
                pExynosPort->portDefinition.bPopulated = OMX_TRUE;
                /* Exynos_OSAL_MutexLock(pExynosComponent->compMutex); */
                Exynos_OSAL_SemaphorePost(pExynosPort->loadedResource);
                /* Exynos_OSAL_MutexUnlock(pExynosComponent->compMutex); */
            }
            *ppBufferHdr = temp_bufferHeader;
            ret = OMX_ErrorNone;
            goto EXIT;
        }
    }

    Exynos_OSAL_Free(temp_bufferHeader);
    ret = OMX_ErrorInsufficientResources;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_AllocateBuffer(
    OMX_IN OMX_HANDLETYPE            hComponent,
    OMX_INOUT OMX_BUFFERHEADERTYPE **ppBuffer,
    OMX_IN OMX_U32                   nPortIndex,
    OMX_IN OMX_PTR                   pAppPrivate,
    OMX_IN OMX_U32                   nSizeBytes)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;
    OMX_BUFFERHEADERTYPE     *temp_bufferHeader = NULL;
    OMX_U8                   *temp_buffer = NULL;
    OMX_U32                   i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pExynosPort = &pExynosComponent->pExynosPort[nPortIndex];
    if (nPortIndex >= pExynosComponent->portParam.nPorts) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }
/*
    if (pExynosPort->portState != OMX_StateIdle ) {
        ret = OMX_ErrorIncorrectStateOperation;
        goto EXIT;
    }
*/
    if (CHECK_PORT_TUNNELED(pExynosPort) && CHECK_PORT_BUFFER_SUPPLIER(pExynosPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    temp_buffer = Exynos_OSAL_Malloc(sizeof(OMX_U8) * nSizeBytes);
    if (temp_buffer == NULL) {
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }

    temp_bufferHeader = (OMX_BUFFERHEADERTYPE *)Exynos_OSAL_Malloc(sizeof(OMX_BUFFERHEADERTYPE));
    if (temp_bufferHeader == NULL) {
        Exynos_OSAL_Free(temp_buffer);
        temp_buffer = NULL;
        ret = OMX_ErrorInsufficientResources;
        goto EXIT;
    }
    Exynos_OSAL_Memset(temp_bufferHeader, 0, sizeof(OMX_BUFFERHEADERTYPE));

    for (i = 0; i < pExynosPort->portDefinition.nBufferCountActual; i++) {
        if (pExynosPort->bufferStateAllocate[i] == BUFFER_STATE_FREE) {
            pExynosPort->bufferHeader[i] = temp_bufferHeader;
            pExynosPort->bufferStateAllocate[i] = (BUFFER_STATE_ALLOCATED | HEADER_STATE_ALLOCATED);
            INIT_SET_SIZE_VERSION(temp_bufferHeader, OMX_BUFFERHEADERTYPE);
            temp_bufferHeader->pBuffer        = temp_buffer;
            temp_bufferHeader->nAllocLen      = nSizeBytes;
            temp_bufferHeader->pAppPrivate    = pAppPrivate;
            if (nPortIndex == INPUT_PORT_INDEX)
                temp_bufferHeader->nInputPortIndex = INPUT_PORT_INDEX;
            else
                temp_bufferHeader->nOutputPortIndex = OUTPUT_PORT_INDEX;
            pExynosPort->assignedBufferNum++;
            if (pExynosPort->assignedBufferNum == pExynosPort->portDefinition.nBufferCountActual) {
                pExynosPort->portDefinition.bPopulated = OMX_TRUE;
                /* Exynos_OSAL_MutexLock(pExynosComponent->compMutex); */
                Exynos_OSAL_SemaphorePost(pExynosPort->loadedResource);
                /* Exynos_OSAL_MutexUnlock(pExynosComponent->compMutex); */
            }
            *ppBuffer = temp_bufferHeader;
            ret = OMX_ErrorNone;
            goto EXIT;
        }
    }

    Exynos_OSAL_Free(temp_bufferHeader);
    Exynos_OSAL_Free(temp_buffer);
    ret = OMX_ErrorInsufficientResources;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_FreeBuffer(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_U32        nPortIndex,
    OMX_IN OMX_BUFFERHEADERTYPE *pBufferHdr)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;
    OMX_BUFFERHEADERTYPE     *temp_bufferHeader = NULL;
    OMX_U8                   *temp_buffer = NULL;
    OMX_U32                   i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    pExynosPort = &pExynosComponent->pExynosPort[nPortIndex];

    if (CHECK_PORT_TUNNELED(pExynosPort) && CHECK_PORT_BUFFER_SUPPLIER(pExynosPort)) {
        ret = OMX_ErrorBadPortIndex;
        goto EXIT;
    }

    if ((pExynosPort->portState != OMX_StateLoaded) && (pExynosPort->portState != OMX_StateInvalid)) {
        (*(pExynosComponent->pCallbacks->EventHandler)) (pOMXComponent,
                        pExynosComponent->callbackData,
                        (OMX_U32)OMX_EventError,
                        (OMX_U32)OMX_ErrorPortUnpopulated,
                        nPortIndex, NULL);
    }

    for (i = 0; i < pExynosPort->portDefinition.nBufferCountActual; i++) {
        if (((pExynosPort->bufferStateAllocate[i] | BUFFER_STATE_FREE) != 0) && (pExynosPort->bufferHeader[i] != NULL)) {
            if (pExynosPort->bufferHeader[i]->pBuffer == pBufferHdr->pBuffer) {
                if (pExynosPort->bufferStateAllocate[i] & BUFFER_STATE_ALLOCATED) {
                    Exynos_OSAL_Free(pExynosPort->bufferHeader[i]->pBuffer);
                    pExynosPort->bufferHeader[i]->pBuffer = NULL;
                    pBufferHdr->pBuffer = NULL;
                } else if (pExynosPort->bufferStateAllocate[i] & BUFFER_STATE_ASSIGNED) {
                    ; /* None*/
                }
                pExynosPort->assignedBufferNum--;
                if (pExynosPort->bufferStateAllocate[i] & HEADER_STATE_ALLOCATED) {
                    Exynos_OSAL_Free(pExynosPort->bufferHeader[i]);
                    pExynosPort->bufferHeader[i] = NULL;
                    pBufferHdr = NULL;
                }
                pExynosPort->bufferStateAllocate[i] = BUFFER_STATE_FREE;
                ret = OMX_ErrorNone;
                goto EXIT;
            }
        }
    }

EXIT:
    if (ret == OMX_ErrorNone) {
        if (pExynosPort->assignedBufferNum == 0) {
            Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "pExynosPort->unloadedResource signal set");
            /* Exynos_OSAL_MutexLock(pExynosComponent->compMutex); */
            Exynos_OSAL_SemaphorePost(pExynosPort->unloadedResource);
            /* Exynos_OSAL_MutexUnlock(pExynosComponent->compMutex); */
            pExynosPort->portDefinition.bPopulated = OMX_FALSE;
        }
    }

    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_AllocateTunnelBuffer(EXYNOS_OMX_BASEPORT *pOMXBasePort, OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE                 ret = OMX_ErrorNone;
    EXYNOS_OMX_BASEPORT          *pExynosPort = NULL;
    OMX_BUFFERHEADERTYPE         *temp_bufferHeader = NULL;
    OMX_U8                       *temp_buffer = NULL;
    OMX_U32                       bufferSize = 0;
    OMX_PARAM_PORTDEFINITIONTYPE  portDefinition;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_ERRORTYPE Exynos_OMX_FreeTunnelBuffer(EXYNOS_OMX_BASEPORT *pOMXBasePort, OMX_U32 nPortIndex)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;
    EXYNOS_OMX_BASEPORT* pExynosPort = NULL;
    OMX_BUFFERHEADERTYPE* temp_bufferHeader = NULL;
    OMX_U8 *temp_buffer = NULL;
    OMX_U32 bufferSize = 0;
    OMX_PARAM_PORTDEFINITIONTYPE portDefinition;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_ERRORTYPE Exynos_OMX_ComponentTunnelRequest(
    OMX_IN OMX_HANDLETYPE hComp,
    OMX_IN OMX_U32        nPort,
    OMX_IN OMX_HANDLETYPE hTunneledComp,
    OMX_IN OMX_U32        nTunneledPort,
    OMX_INOUT OMX_TUNNELSETUPTYPE *pTunnelSetup)
{
    OMX_ERRORTYPE ret = OMX_ErrorNone;

    ret = OMX_ErrorTunnelingUnsupported;
EXIT:
    return ret;
}

OMX_BOOL Exynos_Check_BufferProcess_State(EXYNOS_OMX_BASECOMPONENT *pExynosComponent)
{
    if ((pExynosComponent->currentState == OMX_StateExecuting) &&
        (pExynosComponent->pExynosPort[INPUT_PORT_INDEX].portState == OMX_StateIdle) &&
        (pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX].portState == OMX_StateIdle) &&
        (pExynosComponent->transientState != EXYNOS_OMX_TransStateExecutingToIdle) &&
        (pExynosComponent->transientState != EXYNOS_OMX_TransStateIdleToExecuting))
        return OMX_TRUE;
    else
        return OMX_FALSE;
}

static OMX_ERRORTYPE Exynos_InputBufferReturn(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_BASEPORT      *exynosOMXInputPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    EXYNOS_OMX_BASEPORT      *exynosOMXOutputPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
    EXYNOS_OMX_DATABUFFER    *dataBuffer = &pExynosComponent->exynosDataBuffer[INPUT_PORT_INDEX];
    OMX_BUFFERHEADERTYPE     *bufferHeader = dataBuffer->bufferHeader;

    FunctionIn();

    if (bufferHeader != NULL) {
        if (exynosOMXInputPort->markType.hMarkTargetComponent != NULL ) {
            bufferHeader->hMarkTargetComponent      = exynosOMXInputPort->markType.hMarkTargetComponent;
            bufferHeader->pMarkData                 = exynosOMXInputPort->markType.pMarkData;
            exynosOMXInputPort->markType.hMarkTargetComponent = NULL;
            exynosOMXInputPort->markType.pMarkData = NULL;
        }

        if (bufferHeader->hMarkTargetComponent != NULL) {
            if (bufferHeader->hMarkTargetComponent == pOMXComponent) {
                pExynosComponent->pCallbacks->EventHandler(pOMXComponent,
                                pExynosComponent->callbackData,
                                OMX_EventMark,
                                0, 0, bufferHeader->pMarkData);
            } else {
                pExynosComponent->propagateMarkType.hMarkTargetComponent = bufferHeader->hMarkTargetComponent;
                pExynosComponent->propagateMarkType.pMarkData = bufferHeader->pMarkData;
            }
        }

        if (CHECK_PORT_TUNNELED(exynosOMXInputPort)) {
            OMX_FillThisBuffer(exynosOMXInputPort->tunneledComponent, bufferHeader);
        } else {
            bufferHeader->nFilledLen = 0;
            pExynosComponent->pCallbacks->EmptyBufferDone(pOMXComponent, pExynosComponent->callbackData, bufferHeader);
        }
    }

    if ((pExynosComponent->currentState == OMX_StatePause) &&
        ((!CHECK_PORT_BEING_FLUSHED(exynosOMXInputPort) && !CHECK_PORT_BEING_FLUSHED(exynosOMXOutputPort)))) {
        Exynos_OSAL_SignalWait(pExynosComponent->pauseEvent, DEF_MAX_WAIT_TIME);
        Exynos_OSAL_SignalReset(pExynosComponent->pauseEvent);
    }

    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_InputBufferGetQueue(EXYNOS_OMX_BASECOMPONENT *pExynosComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    EXYNOS_OMX_BASEPORT   *pExynosPort = NULL;
    EXYNOS_OMX_DATABUFFER *dataBuffer = NULL;
    EXYNOS_OMX_MESSAGE    *message = NULL;
    EXYNOS_OMX_DATABUFFER *inputUseBuffer = &pExynosComponent->exynosDataBuffer[INPUT_PORT_INDEX];

    FunctionIn();

    pExynosPort= &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    dataBuffer = &pExynosComponent->exynosDataBuffer[INPUT_PORT_INDEX];

    if (pExynosComponent->currentState != OMX_StateExecuting) {
        ret = OMX_ErrorUndefined;
        goto EXIT;
    } else {
        Exynos_OSAL_SemaphoreWait(pExynosPort->bufferSemID);
        Exynos_OSAL_MutexLock(inputUseBuffer->bufferMutex);
        if (dataBuffer->dataValid != OMX_TRUE) {
            message = (EXYNOS_OMX_MESSAGE *)Exynos_OSAL_Dequeue(&pExynosPort->bufferQ);
            if (message == NULL) {
                ret = OMX_ErrorUndefined;
                Exynos_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                goto EXIT;
            }

            dataBuffer->bufferHeader = (OMX_BUFFERHEADERTYPE *)(message->pCmdData);
            dataBuffer->allocSize = dataBuffer->bufferHeader->nAllocLen;
            dataBuffer->dataLen = dataBuffer->bufferHeader->nFilledLen;
            dataBuffer->remainDataLen = dataBuffer->dataLen;
            dataBuffer->usedDataLen = 0; //dataBuffer->bufferHeader->nOffset;
            dataBuffer->dataValid = OMX_TRUE;
            dataBuffer->nFlags = dataBuffer->bufferHeader->nFlags;
            dataBuffer->timeStamp = dataBuffer->bufferHeader->nTimeStamp;
            pExynosComponent->processData[INPUT_PORT_INDEX].dataBuffer = dataBuffer->bufferHeader->pBuffer;
            pExynosComponent->processData[INPUT_PORT_INDEX].allocSize = dataBuffer->bufferHeader->nAllocLen;
            Exynos_OSAL_Free(message);
        }
        Exynos_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
        ret = OMX_ErrorNone;
    }
EXIT:
    FunctionOut();

    return ret;
}

static OMX_ERRORTYPE Exynos_OutputBufferReturn(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_BASEPORT      *exynosOMXInputPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    EXYNOS_OMX_BASEPORT      *exynosOMXOutputPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
    EXYNOS_OMX_DATABUFFER    *dataBuffer = &pExynosComponent->exynosDataBuffer[OUTPUT_PORT_INDEX];
    OMX_BUFFERHEADERTYPE     *bufferHeader = dataBuffer->bufferHeader;

    FunctionIn();

    if (bufferHeader != NULL) {
        bufferHeader->nFilledLen = dataBuffer->remainDataLen;
        bufferHeader->nOffset    = 0;
        bufferHeader->nFlags     = dataBuffer->nFlags;
        bufferHeader->nTimeStamp = dataBuffer->timeStamp;

        if (pExynosComponent->propagateMarkType.hMarkTargetComponent != NULL) {
            bufferHeader->hMarkTargetComponent = pExynosComponent->propagateMarkType.hMarkTargetComponent;
            bufferHeader->pMarkData = pExynosComponent->propagateMarkType.pMarkData;
            pExynosComponent->propagateMarkType.hMarkTargetComponent = NULL;
            pExynosComponent->propagateMarkType.pMarkData = NULL;
        }

        if (bufferHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            pExynosComponent->pCallbacks->EventHandler(pOMXComponent,
                            pExynosComponent->callbackData,
                            OMX_EventBufferFlag,
                            OUTPUT_PORT_INDEX,
                            bufferHeader->nFlags, NULL);
        }

        if (CHECK_PORT_TUNNELED(exynosOMXOutputPort)) {
            OMX_EmptyThisBuffer(exynosOMXOutputPort->tunneledComponent, bufferHeader);
        } else {
            pExynosComponent->pCallbacks->FillBufferDone(pOMXComponent, pExynosComponent->callbackData, bufferHeader);
        }
    }

    if ((pExynosComponent->currentState == OMX_StatePause) &&
        ((!CHECK_PORT_BEING_FLUSHED(exynosOMXInputPort) && !CHECK_PORT_BEING_FLUSHED(exynosOMXOutputPort)))) {
        Exynos_OSAL_SignalWait(pExynosComponent->pauseEvent, DEF_MAX_WAIT_TIME);
        Exynos_OSAL_SignalReset(pExynosComponent->pauseEvent);
    }

    /* reset dataBuffer */
    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OutputBufferGetQueue(EXYNOS_OMX_BASECOMPONENT *pExynosComponent)
{
    OMX_ERRORTYPE          ret = OMX_ErrorNone;
    EXYNOS_OMX_BASEPORT   *pExynosPort = NULL;
    EXYNOS_OMX_DATABUFFER *dataBuffer = NULL;
    EXYNOS_OMX_MESSAGE    *message = NULL;
    EXYNOS_OMX_DATABUFFER *outputUseBuffer = &pExynosComponent->exynosDataBuffer[OUTPUT_PORT_INDEX];

    FunctionIn();

    pExynosPort= &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
    dataBuffer = &pExynosComponent->exynosDataBuffer[OUTPUT_PORT_INDEX];

    if (pExynosComponent->currentState != OMX_StateExecuting) {
        ret = OMX_ErrorUndefined;
        goto EXIT;
    } else {
        Exynos_OSAL_SemaphoreWait(pExynosPort->bufferSemID);
        Exynos_OSAL_MutexLock(outputUseBuffer->bufferMutex);
        if (dataBuffer->dataValid != OMX_TRUE) {
            message = (EXYNOS_OMX_MESSAGE *)Exynos_OSAL_Dequeue(&pExynosPort->bufferQ);
            if (message == NULL) {
                ret = OMX_ErrorUndefined;
                Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                goto EXIT;
            }

            dataBuffer->bufferHeader = (OMX_BUFFERHEADERTYPE *)(message->pCmdData);
            dataBuffer->allocSize = dataBuffer->bufferHeader->nAllocLen;
            dataBuffer->dataLen = 0; //dataBuffer->bufferHeader->nFilledLen;
            dataBuffer->remainDataLen = dataBuffer->dataLen;
            dataBuffer->usedDataLen = 0; //dataBuffer->bufferHeader->nOffset;
            dataBuffer->dataValid =OMX_TRUE;
            /* dataBuffer->nFlags = dataBuffer->bufferHeader->nFlags; */
            /* dataBuffer->nTimeStamp = dataBuffer->bufferHeader->nTimeStamp; */
            Exynos_OSAL_Free(message);
        }
        Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
        ret = OMX_ErrorNone;
    }
EXIT:
    FunctionOut();

    return ret;

}

static OMX_ERRORTYPE Exynos_BufferReset(OMX_COMPONENTTYPE *pOMXComponent, OMX_U32 portIndex)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    /* EXYNOS_OMX_BASEPORT      *pExynosPort = &pExynosComponent->pExynosPort[portIndex]; */
    EXYNOS_OMX_DATABUFFER    *dataBuffer = &pExynosComponent->exynosDataBuffer[portIndex];
    /* OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader; */

    dataBuffer->dataValid     = OMX_FALSE;
    dataBuffer->dataLen       = 0;
    dataBuffer->remainDataLen = 0;
    dataBuffer->usedDataLen   = 0;
    dataBuffer->bufferHeader  = NULL;
    dataBuffer->nFlags        = 0;
    dataBuffer->timeStamp     = 0;

    return ret;
}

static OMX_ERRORTYPE Exynos_DataReset(OMX_COMPONENTTYPE *pOMXComponent, OMX_U32 portIndex)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    /* EXYNOS_OMX_BASEPORT      *pExynosPort = &pExynosComponent->pExynosPort[portIndex]; */
    /* EXYNOS_OMX_DATABUFFER    *dataBuffer = &pExynosComponent->exynosDataBuffer[portIndex]; */
    /* OMX_BUFFERHEADERTYPE  *bufferHeader = dataBuffer->bufferHeader; */
    EXYNOS_OMX_DATA          *processData = &pExynosComponent->processData[portIndex];

    processData->dataLen       = 0;
    processData->remainDataLen = 0;
    processData->usedDataLen   = 0;
    processData->nFlags        = 0;
    processData->timeStamp     = 0;

    return ret;
}

OMX_BOOL Exynos_Preprocessor_InputData(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_BOOL                      ret = OMX_FALSE;
    EXYNOS_OMX_BASECOMPONENT      *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_VIDEOENC_COMPONENT *pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
    EXYNOS_OMX_DATABUFFER         *inputUseBuffer = &pExynosComponent->exynosDataBuffer[INPUT_PORT_INDEX];
    EXYNOS_OMX_DATA               *inputData = &pExynosComponent->processData[INPUT_PORT_INDEX];
    OMX_U32                        copySize = 0;
    OMX_BYTE                       checkInputStream = NULL;
    OMX_U32                        checkInputStreamLen = 0;
    OMX_U32                        checkedSize = 0;
    OMX_BOOL                       flagEOS = OMX_FALSE;
    OMX_BOOL                       flagEOF = OMX_FALSE;
    OMX_BOOL                       previousFrameEOF = OMX_FALSE;

    if (inputUseBuffer->dataValid == OMX_TRUE) {
        checkInputStream = inputUseBuffer->bufferHeader->pBuffer + inputUseBuffer->usedDataLen;
        checkInputStreamLen = inputUseBuffer->remainDataLen;

        if ((inputUseBuffer->nFlags & OMX_BUFFERFLAG_ENDOFFRAME) &&
            (pExynosComponent->bUseFlagEOF == OMX_FALSE)) {
            pExynosComponent->bUseFlagEOF = OMX_TRUE;
        }

        if (inputData->dataLen == 0) {
            previousFrameEOF = OMX_TRUE;
        } else {
            previousFrameEOF = OMX_FALSE;
        }
        if (pExynosComponent->bUseFlagEOF == OMX_TRUE) {
            flagEOF = OMX_TRUE;
            checkedSize = checkInputStreamLen;
            if (checkedSize == 0) {
                EXYNOS_OMX_BASEPORT *pExynosPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
                int width = pExynosPort->portDefinition.format.video.nFrameWidth;
                int height = pExynosPort->portDefinition.format.video.nFrameHeight;
                inputUseBuffer->remainDataLen = inputUseBuffer->dataLen = (width * height * 3) / 2;
                checkedSize = checkInputStreamLen = inputUseBuffer->remainDataLen;
                inputUseBuffer->nFlags |= OMX_BUFFERFLAG_EOS;
            }
            if (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
                flagEOS = OMX_TRUE;
            }
        } else {
            EXYNOS_OMX_BASEPORT *pExynosPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
            int width = pExynosPort->portDefinition.format.video.nFrameWidth;
            int height = pExynosPort->portDefinition.format.video.nFrameHeight;
            unsigned int oneFrameSize = 0;

            switch (pExynosPort->portDefinition.format.video.eColorFormat) {
            case OMX_COLOR_FormatYUV420SemiPlanar:
            case OMX_COLOR_FormatYUV420Planar:
            case OMX_SEC_COLOR_FormatNV12TPhysicalAddress:
            case OMX_SEC_COLOR_FormatNV12LPhysicalAddress:
            case OMX_SEC_COLOR_FormatNV21LPhysicalAddress:
            case OMX_SEC_COLOR_FormatNV12Tiled:
            case OMX_SEC_COLOR_FormatNV21Linear:
                oneFrameSize = (width * height * 3) / 2;
                break;
            case OMX_SEC_COLOR_FormatNV12LVirtualAddress:
                oneFrameSize = ALIGN((ALIGN(width, 16) * ALIGN(height, 16)), 2048) + ALIGN((ALIGN(width, 16) * ALIGN(height >> 1, 8)), 2048);
                break;
            default:
                Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "Exynos_Preprocessor_InputData: eColorFormat is wrong");
                break;
            }

            if (previousFrameEOF == OMX_TRUE) {
                if (checkInputStreamLen >= oneFrameSize) {
                    checkedSize = oneFrameSize;
                    flagEOF = OMX_TRUE;
                } else {
                    flagEOF = OMX_FALSE;
                }
            } else {
                if (checkInputStreamLen >= (oneFrameSize - inputData->dataLen)) {
                    checkedSize = oneFrameSize - inputData->dataLen;
                    flagEOF = OMX_TRUE;
                } else {
                    flagEOF = OMX_FALSE;
                }
            }

            if ((flagEOF == OMX_FALSE) && (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS)) {
                flagEOF = OMX_TRUE;
                flagEOS = OMX_TRUE;
            }
        }

        if (flagEOF == OMX_TRUE) {
            copySize = checkedSize;
            Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "exynos_checkInputFrame : OMX_TRUE");
        } else {
            copySize = checkInputStreamLen;
            Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "exynos_checkInputFrame : OMX_FALSE");
        }

        if (inputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS)
            pExynosComponent->bSaveFlagEOS = OMX_TRUE;

        if (((inputData->allocSize) - (inputData->dataLen)) >= copySize) {
            EXYNOS_OMX_BASEPORT *pExynosPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
            if ((pExynosPort->portDefinition.format.video.eColorFormat != OMX_SEC_COLOR_FormatNV12TPhysicalAddress) &&
                (pExynosPort->portDefinition.format.video.eColorFormat != OMX_SEC_COLOR_FormatNV12LPhysicalAddress) &&
                (pExynosPort->portDefinition.format.video.eColorFormat != OMX_SEC_COLOR_FormatNV12LVirtualAddress) &&
                (pExynosPort->portDefinition.format.video.eColorFormat != OMX_SEC_COLOR_FormatNV21LPhysicalAddress) &&
                (pExynosPort->bStoreMetaData == OMX_FALSE)) {
                if (flagEOF == OMX_TRUE) {
                    OMX_U32 width, height;

                    width = pExynosPort->portDefinition.format.video.nFrameWidth;
                    height = pExynosPort->portDefinition.format.video.nFrameHeight;

                    Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "pVideoEnc->MFCEncInputBuffer[%d].YVirAddr : 0x%x", pVideoEnc->indexInputBuffer, pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].YVirAddr);
                    Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "pVideoEnc->MFCEncInputBuffer[%d].CVirAddr : 0x%x", pVideoEnc->indexInputBuffer, pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].CVirAddr);

                    Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "width:%d, height:%d, Ysize:%d", width, height, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height)));
                    Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "width:%d, height:%d, Csize:%d", width, height, ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height / 2)));
                    switch (pExynosPort->portDefinition.format.video.eColorFormat) {
                    case OMX_COLOR_FormatYUV420Planar:
                        /* YUV420Planar case it needed changed interleave UV plane (MFC spec.)*/
                        Exynos_OSAL_Memcpy(pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].YVirAddr,
                                        checkInputStream, (width * height));
                        csc_interleave_memcpy_neon(pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].CVirAddr,
                                                 checkInputStream + (width * height),
                                                 checkInputStream + (((width * height) * 5) / 4),
                                                 width * height >> 2);
                        break;
                    case OMX_COLOR_FormatYUV420SemiPlanar:
                    case OMX_SEC_COLOR_FormatNV12Tiled:
                    case OMX_SEC_COLOR_FormatNV21Linear:
                        Exynos_OSAL_Memcpy(pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].YVirAddr,
                                        checkInputStream, (width * height));
                        Exynos_OSAL_Memcpy(pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].CVirAddr,
                                        checkInputStream + (width * height), (width * height / 2));
                        break;
#ifdef USE_METADATABUFFERTYPE
                    case OMX_COLOR_FormatAndroidOpaque:
                        {
                            OMX_PTR ppBuf[3];
                            OMX_PTR pOutBuffer;

                            Exynos_OSAL_GetInfoFromMetaData(inputData, ppBuf);
                            Exynos_OSAL_LockANBHandle((OMX_U32)ppBuf[0], width, height, OMX_COLOR_FormatAndroidOpaque, &pOutBuffer);

                            csc_RGB565_to_YUV420SP(pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].YVirAddr,
                                                    pVideoEnc->MFCEncInputBuffer[pVideoEnc->indexInputBuffer].CVirAddr,
                                                    pOutBuffer, width, height);

                            Exynos_OSAL_UnlockANBHandle((OMX_U32)ppBuf[0]);
                        }
                        break;
#endif
                    default:
                        break;
                    }
                }
            }

            inputUseBuffer->dataLen -= copySize; /* ???????????? do not need ?????????????? */
            inputUseBuffer->remainDataLen -= copySize;
            inputUseBuffer->usedDataLen += copySize;

            inputData->dataLen += copySize;
            inputData->remainDataLen += copySize;

            if (previousFrameEOF == OMX_TRUE) {
                inputData->timeStamp = inputUseBuffer->timeStamp;
                inputData->nFlags = inputUseBuffer->nFlags;
            }

            if (pExynosComponent->bUseFlagEOF == OMX_TRUE) {
                if (pExynosComponent->bSaveFlagEOS == OMX_TRUE) {
                    inputData->nFlags |= OMX_BUFFERFLAG_EOS;
                    flagEOF = OMX_TRUE;
                    pExynosComponent->bSaveFlagEOS = OMX_FALSE;
                }
            } else {
                if ((checkedSize == checkInputStreamLen) && (pExynosComponent->bSaveFlagEOS == OMX_TRUE)) {
                    inputData->nFlags |= OMX_BUFFERFLAG_EOS;
                    flagEOF = OMX_TRUE;
                    pExynosComponent->bSaveFlagEOS = OMX_FALSE;
                } else {
                    inputData->nFlags = (inputUseBuffer->nFlags & (~OMX_BUFFERFLAG_EOS));
                }
            }
        } else {
            /*????????????????????????????????? Error ?????????????????????????????????*/
            Exynos_DataReset(pOMXComponent, INPUT_PORT_INDEX);
            flagEOF = OMX_FALSE;
        }

        if (inputUseBuffer->remainDataLen == 0) {
            if(flagEOF == OMX_FALSE)
                Exynos_InputBufferReturn(pOMXComponent);
        } else {
            inputUseBuffer->dataValid = OMX_TRUE;
        }
    }

    if (flagEOF == OMX_TRUE) {
        if (pExynosComponent->checkTimeStamp.needSetStartTimeStamp == OMX_TRUE) {
            pExynosComponent->checkTimeStamp.needCheckStartTimeStamp = OMX_TRUE;
            pExynosComponent->checkTimeStamp.startTimeStamp = inputData->timeStamp;
            pExynosComponent->checkTimeStamp.nStartFlags = inputData->nFlags;
            pExynosComponent->checkTimeStamp.needSetStartTimeStamp = OMX_FALSE;
        }

        ret = OMX_TRUE;
    } else {
        ret = OMX_FALSE;
    }
    return ret;
}

OMX_BOOL Exynos_Postprocess_OutputData(OMX_COMPONENTTYPE *pOMXComponent)
{
    OMX_BOOL                  ret = OMX_FALSE;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_DATABUFFER    *outputUseBuffer = &pExynosComponent->exynosDataBuffer[OUTPUT_PORT_INDEX];
    EXYNOS_OMX_DATA          *outputData = &pExynosComponent->processData[OUTPUT_PORT_INDEX];
    OMX_U32                   copySize = 0;

    if (outputUseBuffer->dataValid == OMX_TRUE) {
        if (pExynosComponent->checkTimeStamp.needCheckStartTimeStamp == OMX_TRUE) {
            if (pExynosComponent->checkTimeStamp.startTimeStamp == outputData->timeStamp){
                pExynosComponent->checkTimeStamp.startTimeStamp = -19761123;
                pExynosComponent->checkTimeStamp.nStartFlags = 0x0;
                pExynosComponent->checkTimeStamp.needSetStartTimeStamp = OMX_FALSE;
                pExynosComponent->checkTimeStamp.needCheckStartTimeStamp = OMX_FALSE;
            } else {
                Exynos_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

                ret = OMX_TRUE;
                goto EXIT;
            }
        } else if (pExynosComponent->checkTimeStamp.needSetStartTimeStamp == OMX_TRUE) {
            Exynos_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

            ret = OMX_TRUE;
            goto EXIT;
        }

        if (outputData->remainDataLen <= (outputUseBuffer->allocSize - outputUseBuffer->dataLen)) {
            copySize = outputData->remainDataLen;
            if (copySize > 0)
                Exynos_OSAL_Memcpy((outputUseBuffer->bufferHeader->pBuffer + outputUseBuffer->dataLen),
                        (outputData->dataBuffer + outputData->usedDataLen),
                         copySize);

            outputUseBuffer->dataLen += copySize;
            outputUseBuffer->remainDataLen += copySize;
            outputUseBuffer->nFlags = outputData->nFlags;
            outputUseBuffer->timeStamp = outputData->timeStamp;

            ret = OMX_TRUE;

            /* reset outputData */
            Exynos_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);

            if ((outputUseBuffer->remainDataLen > 0) ||
                (outputUseBuffer->nFlags & OMX_BUFFERFLAG_EOS))
                Exynos_OutputBufferReturn(pOMXComponent);
        } else {
            Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "output buffer is smaller than encoded data size Out Length");

            ret = OMX_FALSE;

            /* reset outputData */
            Exynos_DataReset(pOMXComponent, OUTPUT_PORT_INDEX);
        }
    } else {
        ret = OMX_FALSE;
    }

EXIT:
    return ret;
}

OMX_ERRORTYPE Exynos_OMX_BufferProcess(OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE                  ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE             *pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    EXYNOS_OMX_BASECOMPONENT      *pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    EXYNOS_OMX_VIDEOENC_COMPONENT *pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
    EXYNOS_OMX_BASEPORT           *exynosInputPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    EXYNOS_OMX_BASEPORT           *exynosOutputPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
    EXYNOS_OMX_DATABUFFER         *inputUseBuffer = &pExynosComponent->exynosDataBuffer[INPUT_PORT_INDEX];
    EXYNOS_OMX_DATABUFFER         *outputUseBuffer = &pExynosComponent->exynosDataBuffer[OUTPUT_PORT_INDEX];
    EXYNOS_OMX_DATA               *inputData = &pExynosComponent->processData[INPUT_PORT_INDEX];
    EXYNOS_OMX_DATA               *outputData = &pExynosComponent->processData[OUTPUT_PORT_INDEX];
    OMX_U32                        copySize = 0;

    pExynosComponent->remainOutputData = OMX_FALSE;
    pExynosComponent->reInputData = OMX_FALSE;

    FunctionIn();

    while (!pExynosComponent->bExitBufferProcessThread) {
        Exynos_OSAL_SleepMillisec(0);

        if (((pExynosComponent->currentState == OMX_StatePause) ||
            (pExynosComponent->currentState == OMX_StateIdle) ||
            (pExynosComponent->transientState == EXYNOS_OMX_TransStateLoadedToIdle) ||
            (pExynosComponent->transientState == EXYNOS_OMX_TransStateExecutingToIdle)) &&
            (pExynosComponent->transientState != EXYNOS_OMX_TransStateIdleToLoaded)&&
            ((!CHECK_PORT_BEING_FLUSHED(exynosInputPort) && !CHECK_PORT_BEING_FLUSHED(exynosOutputPort)))) {
            Exynos_OSAL_SignalWait(pExynosComponent->pauseEvent, DEF_MAX_WAIT_TIME);
            Exynos_OSAL_SignalReset(pExynosComponent->pauseEvent);
        }

        while (Exynos_Check_BufferProcess_State(pExynosComponent) && !pExynosComponent->bExitBufferProcessThread) {
            Exynos_OSAL_SleepMillisec(0);

            Exynos_OSAL_MutexLock(outputUseBuffer->bufferMutex);
            if ((outputUseBuffer->dataValid != OMX_TRUE) &&
                (!CHECK_PORT_BEING_FLUSHED(exynosOutputPort))) {
                Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                ret = Exynos_OutputBufferGetQueue(pExynosComponent);
                if ((ret == OMX_ErrorUndefined) ||
                    (exynosInputPort->portState != OMX_StateIdle) ||
                    (exynosOutputPort->portState != OMX_StateIdle)) {
                    break;
                }
            } else {
                Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
            }

            if (pExynosComponent->remainOutputData == OMX_FALSE) {
                if (pExynosComponent->reInputData == OMX_FALSE) {
                    Exynos_OSAL_MutexLock(inputUseBuffer->bufferMutex);
                    if ((Exynos_Preprocessor_InputData(pOMXComponent) == OMX_FALSE) &&
                        (!CHECK_PORT_BEING_FLUSHED(exynosInputPort))) {
                            Exynos_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                            ret = Exynos_InputBufferGetQueue(pExynosComponent);
                            break;
                    }
                    Exynos_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);
                }

                Exynos_OSAL_MutexLock(inputUseBuffer->bufferMutex);
                Exynos_OSAL_MutexLock(outputUseBuffer->bufferMutex);
                ret = pExynosComponent->exynos_mfc_bufferProcess(pOMXComponent, inputData, outputData);

                if (inputUseBuffer->remainDataLen == 0)
                    Exynos_InputBufferReturn(pOMXComponent);
                else
                    inputUseBuffer->dataValid = OMX_TRUE;

                Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
                Exynos_OSAL_MutexUnlock(inputUseBuffer->bufferMutex);

                if (ret == OMX_ErrorInputDataEncodeYet)
                    pExynosComponent->reInputData = OMX_TRUE;
                else
                    pExynosComponent->reInputData = OMX_FALSE;
            }

            Exynos_OSAL_MutexLock(outputUseBuffer->bufferMutex);

            if (Exynos_Postprocess_OutputData(pOMXComponent) == OMX_FALSE)
                pExynosComponent->remainOutputData = OMX_TRUE;
            else
                pExynosComponent->remainOutputData = OMX_FALSE;

            Exynos_OSAL_MutexUnlock(outputUseBuffer->bufferMutex);
        }
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeGetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nParamIndex,
    OMX_INOUT OMX_PTR     ComponentParameterStructure)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    if (pExynosComponent->currentState == OMX_StateInvalid ) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

    if (ComponentParameterStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    switch (nParamIndex) {
    case OMX_IndexParamVideoInit:
    {
        OMX_PORT_PARAM_TYPE *portParam = (OMX_PORT_PARAM_TYPE *)ComponentParameterStructure;
        ret = Exynos_OMX_Check_SizeVersion(portParam, sizeof(OMX_PORT_PARAM_TYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        portParam->nPorts           = pExynosComponent->portParam.nPorts;
        portParam->nStartPortNumber = pExynosComponent->portParam.nStartPortNumber;
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *portFormat = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)ComponentParameterStructure;
        OMX_U32                         portIndex = portFormat->nPortIndex;
        OMX_U32                         index    = portFormat->nIndex;
        EXYNOS_OMX_BASEPORT            *pExynosPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE   *portDefinition = NULL;
        OMX_U32                         supportFormatNum = 0;

        ret = Exynos_OMX_Check_SizeVersion(portFormat, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if ((portIndex >= pExynosComponent->portParam.nPorts)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }


        if (portIndex == INPUT_PORT_INDEX) {
            supportFormatNum = INPUT_PORT_SUPPORTFORMAT_NUM_MAX - 1;
            if (index > supportFormatNum) {
                ret = OMX_ErrorNoMore;
                goto EXIT;
            }

            pExynosPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
            portDefinition = &pExynosPort->portDefinition;

            switch (index) {
            case supportFormat_0:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_COLOR_FormatYUV420Planar;
                portFormat->xFramerate           = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_1:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV12TPhysicalAddress;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_2:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV12LPhysicalAddress;
                portFormat->xFramerate           = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_3:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_COLOR_FormatYUV420SemiPlanar;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_4:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV12Tiled;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_5:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV12LVirtualAddress;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_6:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV21LPhysicalAddress;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            case supportFormat_7:
                portFormat->eCompressionFormat = OMX_VIDEO_CodingUnused;
                portFormat->eColorFormat       = OMX_SEC_COLOR_FormatNV21Linear;
                portFormat->xFramerate         = portDefinition->format.video.xFramerate;
                break;
            }
        } else if (portIndex == OUTPUT_PORT_INDEX) {
            supportFormatNum = OUTPUT_PORT_SUPPORTFORMAT_NUM_MAX - 1;
            if (index > supportFormatNum) {
                ret = OMX_ErrorNoMore;
                goto EXIT;
            }

            pExynosPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
            portDefinition = &pExynosPort->portDefinition;

            portFormat->eCompressionFormat = portDefinition->format.video.eCompressionFormat;
            portFormat->eColorFormat       = portDefinition->format.video.eColorFormat;
            portFormat->xFramerate         = portDefinition->format.video.xFramerate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE     *videoRateControl = (OMX_VIDEO_PARAM_BITRATETYPE *)ComponentParameterStructure;
        OMX_U32                          portIndex = videoRateControl->nPortIndex;
        EXYNOS_OMX_BASEPORT             *pExynosPort = NULL;
        EXYNOS_OMX_VIDEOENC_COMPONENT   *pVideoEnc = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE    *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            portDefinition = &pExynosPort->portDefinition;

            videoRateControl->eControlRate = pVideoEnc->eControlRate[portIndex];
            videoRateControl->nTargetBitrate = portDefinition->format.video.nBitrate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoQuantization:
    {
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE  *videoQuantizationControl = (OMX_VIDEO_PARAM_QUANTIZATIONTYPE *)ComponentParameterStructure;
        OMX_U32                            portIndex = videoQuantizationControl->nPortIndex;
        EXYNOS_OMX_BASEPORT               *pExynosPort = NULL;
        EXYNOS_OMX_VIDEOENC_COMPONENT     *pVideoEnc = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE      *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            portDefinition = &pExynosPort->portDefinition;

            videoQuantizationControl->nQpI = pVideoEnc->quantization.nQpI;
            videoQuantizationControl->nQpP = pVideoEnc->quantization.nQpP;
            videoQuantizationControl->nQpB = pVideoEnc->quantization.nQpB;
        }
        ret = OMX_ErrorNone;

    }
        break;
    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *portDefinition = (OMX_PARAM_PORTDEFINITIONTYPE *)ComponentParameterStructure;
        OMX_U32                       portIndex = portDefinition->nPortIndex;
        EXYNOS_OMX_BASEPORT          *pExynosPort;

        if (portIndex >= pExynosComponent->portParam.nPorts) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }
        ret = Exynos_OMX_Check_SizeVersion(portDefinition, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        pExynosPort = &pExynosComponent->pExynosPort[portIndex];
        Exynos_OSAL_Memcpy(portDefinition, &pExynosPort->portDefinition, portDefinition->nSize);

#ifdef USE_STOREMETADATA
        if ((portIndex == 0) &&
            (pExynosPort->bStoreMetaData == OMX_TRUE) &&
            (pExynosPort->portDefinition.format.video.eColorFormat != OMX_SEC_COLOR_FormatNV12LVirtualAddress)) {
            portDefinition->nBufferSize = MAX_INPUT_METADATA_BUFFER_SIZE;
        }
#endif
    }
        break;
    default:
    {
        ret = Exynos_OMX_GetParameter(hComponent, nParamIndex, ComponentParameterStructure);
    }
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}
OMX_ERRORTYPE Exynos_OMX_VideoEncodeSetParameter(
    OMX_IN OMX_HANDLETYPE hComponent,
    OMX_IN OMX_INDEXTYPE  nIndex,
    OMX_IN OMX_PTR        ComponentParameterStructure)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    if (pExynosComponent->currentState == OMX_StateInvalid ) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

    if (ComponentParameterStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    switch (nIndex) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *portFormat = (OMX_VIDEO_PARAM_PORTFORMATTYPE *)ComponentParameterStructure;
        OMX_U32                         portIndex = portFormat->nPortIndex;
        OMX_U32                         index    = portFormat->nIndex;
        EXYNOS_OMX_BASEPORT            *pExynosPort = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE   *portDefinition = NULL;
        OMX_U32                         supportFormatNum = 0;

        ret = Exynos_OMX_Check_SizeVersion(portFormat, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        if ((portIndex >= pExynosComponent->portParam.nPorts)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            portDefinition = &pExynosPort->portDefinition;

            portDefinition->format.video.eColorFormat       = portFormat->eColorFormat;
            portDefinition->format.video.eCompressionFormat = portFormat->eCompressionFormat;
            portDefinition->format.video.xFramerate         = portFormat->xFramerate;
        }
    }
        break;
    case OMX_IndexParamVideoBitrate:
    {
        OMX_VIDEO_PARAM_BITRATETYPE     *videoRateControl = (OMX_VIDEO_PARAM_BITRATETYPE *)ComponentParameterStructure;
        OMX_U32                          portIndex = videoRateControl->nPortIndex;
        EXYNOS_OMX_BASEPORT             *pExynosPort = NULL;
        EXYNOS_OMX_VIDEOENC_COMPONENT   *pVideoEnc = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE    *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            portDefinition = &pExynosPort->portDefinition;

            pVideoEnc->eControlRate[portIndex] = videoRateControl->eControlRate;
            portDefinition->format.video.nBitrate = videoRateControl->nTargetBitrate;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamVideoQuantization:
    {
        OMX_VIDEO_PARAM_QUANTIZATIONTYPE *videoQuantizationControl = (OMX_VIDEO_PARAM_QUANTIZATIONTYPE *)ComponentParameterStructure;
        OMX_U32                           portIndex = videoQuantizationControl->nPortIndex;
        EXYNOS_OMX_BASEPORT              *pExynosPort = NULL;
        EXYNOS_OMX_VIDEOENC_COMPONENT    *pVideoEnc = NULL;
        OMX_PARAM_PORTDEFINITIONTYPE     *portDefinition = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            portDefinition = &pExynosPort->portDefinition;

            pVideoEnc->quantization.nQpI = videoQuantizationControl->nQpI;
            pVideoEnc->quantization.nQpP = videoQuantizationControl->nQpP;
            pVideoEnc->quantization.nQpB = videoQuantizationControl->nQpB;
        }
        ret = OMX_ErrorNone;
    }
        break;
    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *pPortDefinition = (OMX_PARAM_PORTDEFINITIONTYPE *)ComponentParameterStructure;
        OMX_U32                       portIndex = pPortDefinition->nPortIndex;
        EXYNOS_OMX_BASEPORT          *pExynosPort;
        OMX_U32 width, height, size;

        if (portIndex >= pExynosComponent->portParam.nPorts) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        }
        ret = Exynos_OMX_Check_SizeVersion(pPortDefinition, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
        if (ret != OMX_ErrorNone) {
            goto EXIT;
        }

        pExynosPort = &pExynosComponent->pExynosPort[portIndex];

        if ((pExynosComponent->currentState != OMX_StateLoaded) && (pExynosComponent->currentState != OMX_StateWaitForResources)) {
            if (pExynosPort->portDefinition.bEnabled == OMX_TRUE) {
                ret = OMX_ErrorIncorrectStateOperation;
                goto EXIT;
            }
        }
        if(pPortDefinition->nBufferCountActual < pExynosPort->portDefinition.nBufferCountMin) {
            ret = OMX_ErrorBadParameter;
            goto EXIT;
        }

        Exynos_OSAL_Memcpy(&pExynosPort->portDefinition, pPortDefinition, pPortDefinition->nSize);
        if (portIndex == INPUT_PORT_INDEX) {
            EXYNOS_OMX_BASEPORT *pExynosOutputPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
            Exynos_UpdateFrameSize(pOMXComponent);
            Exynos_OSAL_Log(EXYNOS_LOG_TRACE, "pExynosOutputPort->portDefinition.nBufferSize: %d",
                            pExynosOutputPort->portDefinition.nBufferSize);
        }
        ret = OMX_ErrorNone;
    }
        break;
#ifdef USE_STOREMETADATA
    case OMX_IndexParamStoreMetaDataBuffer:
    {
        ret = Exynos_OSAL_SetANBParameter(hComponent, nIndex, ComponentParameterStructure);
    }
        break;
#endif
    default:
    {
        ret = Exynos_OMX_SetParameter(hComponent, nIndex, ComponentParameterStructure);
    }
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeGetConfig(
    OMX_HANDLETYPE hComponent,
    OMX_INDEXTYPE nIndex,
    OMX_PTR pComponentConfigStructure)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;

    FunctionIn();

    if (hComponent == NULL || pComponentConfigStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }
    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    if (pExynosComponent->currentState == OMX_StateInvalid) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

    switch (nIndex) {
    case OMX_IndexConfigVideoBitrate:
    {
        OMX_VIDEO_CONFIG_BITRATETYPE *pEncodeBitrate = (OMX_VIDEO_CONFIG_BITRATETYPE *)pComponentConfigStructure;
        OMX_U32                       portIndex = pEncodeBitrate->nPortIndex;
        EXYNOS_OMX_BASEPORT          *pExynosPort = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            pEncodeBitrate->nEncodeBitrate = pExynosPort->portDefinition.format.video.nBitrate;
        }
    }
        break;
    case OMX_IndexConfigVideoFramerate:
    {
        OMX_CONFIG_FRAMERATETYPE *pFramerate = (OMX_CONFIG_FRAMERATETYPE *)pComponentConfigStructure;
        OMX_U32                   portIndex = pFramerate->nPortIndex;
        EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            pFramerate->xEncodeFramerate = pExynosPort->portDefinition.format.video.xFramerate;
        }
    }
        break;
    default:
        ret = Exynos_OMX_SetConfig(hComponent, nIndex, pComponentConfigStructure);
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeSetConfig(
    OMX_HANDLETYPE hComponent,
    OMX_INDEXTYPE nIndex,
    OMX_PTR pComponentConfigStructure)
    {
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;

    FunctionIn();

    if (hComponent == NULL || pComponentConfigStructure == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }
    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }

    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;
    if (pExynosComponent->currentState == OMX_StateInvalid) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

    switch (nIndex) {
    case OMX_IndexConfigVideoBitrate:
    {
        OMX_VIDEO_CONFIG_BITRATETYPE *pEncodeBitrate = (OMX_VIDEO_CONFIG_BITRATETYPE *)pComponentConfigStructure;
        OMX_U32                       portIndex = pEncodeBitrate->nPortIndex;
        EXYNOS_OMX_BASEPORT          *pExynosPort = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            pExynosPort->portDefinition.format.video.nBitrate = pEncodeBitrate->nEncodeBitrate;
        }
    }
        break;
    case OMX_IndexConfigVideoFramerate:
    {
        OMX_CONFIG_FRAMERATETYPE *pFramerate = (OMX_CONFIG_FRAMERATETYPE *)pComponentConfigStructure;
        OMX_U32                   portIndex = pFramerate->nPortIndex;
        EXYNOS_OMX_BASEPORT      *pExynosPort = NULL;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pExynosPort = &pExynosComponent->pExynosPort[portIndex];
            pExynosPort->portDefinition.format.video.xFramerate = pFramerate->xEncodeFramerate;
        }
    }
        break;
    case OMX_IndexConfigVideoIntraVOPRefresh:
    {
        OMX_CONFIG_INTRAREFRESHVOPTYPE *pIntraRefreshVOP = (OMX_CONFIG_INTRAREFRESHVOPTYPE *)pComponentConfigStructure;
        EXYNOS_OMX_VIDEOENC_COMPONENT *pVEncBase = ((EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle);
        OMX_U32 portIndex = pIntraRefreshVOP->nPortIndex;

        if ((portIndex != OUTPUT_PORT_INDEX)) {
            ret = OMX_ErrorBadPortIndex;
            goto EXIT;
        } else {
            pVEncBase->IntraRefreshVOP = pIntraRefreshVOP->IntraRefreshVOP;
        }
    }
        break;
    default:
        ret = Exynos_OMX_SetConfig(hComponent, nIndex, pComponentConfigStructure);
        break;
    }

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeGetExtensionIndex(
    OMX_IN OMX_HANDLETYPE  hComponent,
    OMX_IN OMX_STRING      cParameterName,
    OMX_OUT OMX_INDEXTYPE *pIndexType)
{
    OMX_ERRORTYPE             ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE        *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT *pExynosComponent = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    if ((cParameterName == NULL) || (pIndexType == NULL)) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    if (pExynosComponent->currentState == OMX_StateInvalid) {
        ret = OMX_ErrorInvalidState;
        goto EXIT;
    }

#ifdef USE_STOREMETADATA
    if (Exynos_OSAL_Strcmp(cParameterName, EXYNOS_INDEX_PARAM_STORE_METADATA_BUFFER) == 0) {
        *pIndexType = (OMX_INDEXTYPE) OMX_IndexParamStoreMetaDataBuffer;
    } else {
        ret = Exynos_OMX_GetExtensionIndex(hComponent, cParameterName, pIndexType);
    }
#else
    ret = Exynos_OMX_GetExtensionIndex(hComponent, cParameterName, pIndexType);
#endif

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeComponentInit(OMX_IN OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE                  ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE             *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT      *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT           *pExynosPort = NULL;
    EXYNOS_OMX_VIDEOENC_COMPONENT *pVideoEnc = NULL;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    ret = Exynos_OMX_BaseComponent_Constructor(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    ret = Exynos_OMX_Port_Constructor(pOMXComponent);
    if (ret != OMX_ErrorNone) {
        Exynos_OMX_BaseComponent_Destructor(pOMXComponent);
        Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "OMX_Error, Line:%d", __LINE__);
        goto EXIT;
    }

    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pVideoEnc = Exynos_OSAL_Malloc(sizeof(EXYNOS_OMX_VIDEOENC_COMPONENT));
    if (pVideoEnc == NULL) {
        Exynos_OMX_BaseComponent_Destructor(pOMXComponent);
        ret = OMX_ErrorInsufficientResources;
        Exynos_OSAL_Log(EXYNOS_LOG_ERROR, "OMX_ErrorInsufficientResources, Line:%d", __LINE__);
        goto EXIT;
    }

    Exynos_OSAL_Memset(pVideoEnc, 0, sizeof(EXYNOS_OMX_VIDEOENC_COMPONENT));
    pExynosComponent->hComponentHandle = (OMX_HANDLETYPE)pVideoEnc;

    pExynosComponent->bSaveFlagEOS = OMX_FALSE;

    pVideoEnc->configChange = OMX_FALSE;
    pVideoEnc->quantization.nQpI = 20;
    pVideoEnc->quantization.nQpP = 20;
    pVideoEnc->quantization.nQpB = 20;

    /* Input port */
    pExynosPort = &pExynosComponent->pExynosPort[INPUT_PORT_INDEX];
    pExynosPort->portDefinition.nBufferCountActual = MAX_VIDEO_INPUTBUFFER_NUM;
    pExynosPort->portDefinition.nBufferCountMin = MAX_VIDEO_INPUTBUFFER_NUM;
    pExynosPort->portDefinition.nBufferSize = 0;
    pExynosPort->portDefinition.eDomain = OMX_PortDomainVideo;

    pExynosPort->portDefinition.format.video.cMIMEType = Exynos_OSAL_Malloc(MAX_OMX_MIMETYPE_SIZE);
    Exynos_OSAL_Strcpy(pExynosPort->portDefinition.format.video.cMIMEType, "raw/video");
    pExynosPort->portDefinition.format.video.pNativeRender = 0;
    pExynosPort->portDefinition.format.video.bFlagErrorConcealment = OMX_FALSE;
    pExynosPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;

    pExynosPort->portDefinition.format.video.nFrameWidth = 0;
    pExynosPort->portDefinition.format.video.nFrameHeight= 0;
    pExynosPort->portDefinition.format.video.nStride = 0;
    pExynosPort->portDefinition.format.video.nSliceHeight = 0;
    pExynosPort->portDefinition.format.video.nBitrate = 64000;
    pExynosPort->portDefinition.format.video.xFramerate = (15 << 16);
    pExynosPort->portDefinition.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    pExynosPort->portDefinition.format.video.pNativeWindow = NULL;
    pVideoEnc->eControlRate[INPUT_PORT_INDEX] = OMX_Video_ControlRateDisable;

    pExynosPort->bStoreMetaData = OMX_FALSE;

    /* Output port */
    pExynosPort = &pExynosComponent->pExynosPort[OUTPUT_PORT_INDEX];
    pExynosPort->portDefinition.nBufferCountActual = MAX_VIDEO_OUTPUTBUFFER_NUM;
    pExynosPort->portDefinition.nBufferCountMin = MAX_VIDEO_OUTPUTBUFFER_NUM;
    pExynosPort->portDefinition.nBufferSize = DEFAULT_VIDEO_OUTPUT_BUFFER_SIZE;
    pExynosPort->portDefinition.eDomain = OMX_PortDomainVideo;

    pExynosPort->portDefinition.format.video.cMIMEType = Exynos_OSAL_Malloc(MAX_OMX_MIMETYPE_SIZE);
    Exynos_OSAL_Strcpy(pExynosPort->portDefinition.format.video.cMIMEType, "raw/video");
    pExynosPort->portDefinition.format.video.pNativeRender = 0;
    pExynosPort->portDefinition.format.video.bFlagErrorConcealment = OMX_FALSE;
    pExynosPort->portDefinition.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;

    pExynosPort->portDefinition.format.video.nFrameWidth = 0;
    pExynosPort->portDefinition.format.video.nFrameHeight= 0;
    pExynosPort->portDefinition.format.video.nStride = 0;
    pExynosPort->portDefinition.format.video.nSliceHeight = 0;
    pExynosPort->portDefinition.format.video.nBitrate = 64000;
    pExynosPort->portDefinition.format.video.xFramerate = (15 << 16);
    pExynosPort->portDefinition.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    pExynosPort->portDefinition.format.video.pNativeWindow = NULL;
    pVideoEnc->eControlRate[OUTPUT_PORT_INDEX] = OMX_Video_ControlRateDisable;

    pOMXComponent->UseBuffer              = &Exynos_OMX_UseBuffer;
    pOMXComponent->AllocateBuffer         = &Exynos_OMX_AllocateBuffer;
    pOMXComponent->FreeBuffer             = &Exynos_OMX_FreeBuffer;
    pOMXComponent->ComponentTunnelRequest = &Exynos_OMX_ComponentTunnelRequest;

    pExynosComponent->exynos_AllocateTunnelBuffer = &Exynos_OMX_AllocateTunnelBuffer;
    pExynosComponent->exynos_FreeTunnelBuffer     = &Exynos_OMX_FreeTunnelBuffer;
    pExynosComponent->exynos_BufferProcess        = &Exynos_OMX_BufferProcess;
    pExynosComponent->exynos_BufferReset          = &Exynos_BufferReset;
    pExynosComponent->exynos_InputBufferReturn    = &Exynos_InputBufferReturn;
    pExynosComponent->exynos_OutputBufferReturn   = &Exynos_OutputBufferReturn;

EXIT:
    FunctionOut();

    return ret;
}

OMX_ERRORTYPE Exynos_OMX_VideoEncodeComponentDeinit(OMX_IN OMX_HANDLETYPE hComponent)
{
    OMX_ERRORTYPE                  ret = OMX_ErrorNone;
    OMX_COMPONENTTYPE             *pOMXComponent = NULL;
    EXYNOS_OMX_BASECOMPONENT      *pExynosComponent = NULL;
    EXYNOS_OMX_BASEPORT           *pExynosPort = NULL;
    EXYNOS_OMX_VIDEOENC_COMPONENT *pVideoEnc = NULL;
    int                            i = 0;

    FunctionIn();

    if (hComponent == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pOMXComponent = (OMX_COMPONENTTYPE *)hComponent;
    ret = Exynos_OMX_Check_SizeVersion(pOMXComponent, sizeof(OMX_COMPONENTTYPE));
    if (ret != OMX_ErrorNone) {
        goto EXIT;
    }

    if (pOMXComponent->pComponentPrivate == NULL) {
        ret = OMX_ErrorBadParameter;
        goto EXIT;
    }
    pExynosComponent = (EXYNOS_OMX_BASECOMPONENT *)pOMXComponent->pComponentPrivate;

    pVideoEnc = (EXYNOS_OMX_VIDEOENC_COMPONENT *)pExynosComponent->hComponentHandle;
    Exynos_OSAL_Free(pVideoEnc);
    pExynosComponent->hComponentHandle = pVideoEnc = NULL;

    for(i = 0; i < ALL_PORT_NUM; i++) {
        pExynosPort = &pExynosComponent->pExynosPort[i];
        Exynos_OSAL_Free(pExynosPort->portDefinition.format.video.cMIMEType);
        pExynosPort->portDefinition.format.video.cMIMEType = NULL;
    }

    ret = Exynos_OMX_Port_Destructor(pOMXComponent);

    ret = Exynos_OMX_BaseComponent_Destructor(hComponent);

EXIT:
    FunctionOut();

    return ret;
}

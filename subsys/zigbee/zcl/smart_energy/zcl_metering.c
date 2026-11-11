/********************************************************************************************************
 * @file    zcl_metering.c
 *
 * @brief   This is the source file for zcl_metering
 *
 * @author  Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/

/**********************************************************************
 * INCLUDES
 */
#include "../zcl_include.h"


#ifdef ZCL_METERING
/**********************************************************************
 * LOCAL CONSTANTS
 */


/**********************************************************************
 * LOCAL TYPES
 */


/**********************************************************************
 * LOCAL VARIABLES
 */


/**********************************************************************
 * LOCAL FUNCTIONS
 */
static status_t zcl_metering_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_metering_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_SE_METERING, manuCode, attrNum, attrTbl, zcl_metering_cmdHandler, cb);
}

_CODE_ZCL_ status_t zcl_metering_getProfile(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_getProfileCmd_t *pReq)
{
    uint8_t buf[6];
    uint8_t *pBuf = buf;

    *pBuf++ = pReq->intervalChannel;
    *pBuf++ = U32_BYTE0(pReq->endTime);
    *pBuf++ = U32_BYTE1(pReq->endTime);
    *pBuf++ = U32_BYTE2(pReq->endTime);
    *pBuf++ = U32_BYTE3(pReq->endTime);
    *pBuf++ = pReq->numberOfPeriods;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_GET_PROFILE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);
}

_CODE_ZCL_ status_t zcl_metering_requestMirrorRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_requestMirrorRspCmd_t *pRsp)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(pRsp->endPointId);
    *pBuf++ = HI_UINT16(pRsp->endPointId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_REQUEST_MIRROR_RESPONSE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);
}

_CODE_ZCL_ status_t zcl_metering_mirrorRemoved(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_mirrorRemovedCmd_t *pRsp)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    *pBuf++ = LO_UINT16(pRsp->removedEndPointId);
    *pBuf++ = HI_UINT16(pRsp->removedEndPointId);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_MIRROR_REMOVED, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);
}

_CODE_ZCL_ status_t zcl_metering_requestFastPollMode(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_requestFastPollModeCmd_t *pReq)
{
    uint8_t buf[2];
    uint8_t *pBuf = buf;

    *pBuf++ = pReq->fastPollUpdatePeriod;
    *pBuf++ = pReq->duration;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_REQUEST_FAST_POLL_MODE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);
}

_CODE_ZCL_ status_t zcl_metering_getProfileRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_getProfileRspCmd_t *pRsp)
{
    uint8_t len = 4 + 1 + 1 + 1 + pRsp->numberOfPeriodsDelived * 3;

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = U32_BYTE0(pRsp->endTime);
    *pBuf++ = U32_BYTE1(pRsp->endTime);
    *pBuf++ = U32_BYTE2(pRsp->endTime);
    *pBuf++ = U32_BYTE3(pRsp->endTime);
    *pBuf++ = pRsp->status;
    *pBuf++ = pRsp->profileIntervalPeriod;
    *pBuf++ = pRsp->numberOfPeriodsDelived;
    memcpy(pBuf, pRsp->intervals, pRsp->numberOfPeriodsDelived * 3);
    pBuf += (pRsp->numberOfPeriodsDelived * 3);

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_GET_PROFILE_RESPONSE, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_metering_requestMirror(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_REQUEST_MIRROR, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_metering_removeMirror(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_REMOVE_MIRROR, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_metering_requestFastPollModeRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_metering_requestFastPollModeRspCmd_t *pRsp)
{
    uint8_t buf[5];
    uint8_t *pBuf = buf;

    *pBuf++ = pRsp->appliedUpdatePeriod;
    *pBuf++ = U32_BYTE0(pRsp->fastPollModeEndTime);
    *pBuf++ = U32_BYTE1(pRsp->fastPollModeEndTime);
    *pBuf++ = U32_BYTE2(pRsp->fastPollModeEndTime);
    *pBuf++ = U32_BYTE3(pRsp->fastPollModeEndTime);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SE_METERING, ZCL_CMD_REQUEST_FAST_POLL_MODE_RESPONSE, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, (uint8_t)(pBuf - buf), buf);
}

_CODE_ZCL_ static status_t zcl_metering_getProfilePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_getProfileCmd_t getProfileCmd;
        getProfileCmd.intervalChannel = *pData++;
        getProfileCmd.endTime = BUILD_U32(pData[0], pData[1], pData[2], pData[3]);
        pData += 4;
        getProfileCmd.numberOfPeriods = *pData++;

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getProfileCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_requestMirrorRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_requestMirrorRspCmd_t requestMirrorRspCmd;
        requestMirrorRspCmd.endPointId = BUILD_U16(pData[0], pData[1]);

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &requestMirrorRspCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_mirrorRemovedPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_mirrorRemovedCmd_t mirrorRemovedCmd;
        mirrorRemovedCmd.removedEndPointId = BUILD_U16(pData[0], pData[1]);

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &mirrorRemovedCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_requestFastPollModePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_requestFastPollModeCmd_t requestFastPollModeCmd;
        requestFastPollModeCmd.fastPollUpdatePeriod = *pData++;
        requestFastPollModeCmd.duration = *pData++;

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &requestFastPollModeCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_getProfileRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_getProfileRspCmd_t getProfileRspCmd;
        getProfileRspCmd.endTime = BUILD_U32(pData[0], pData[1], pData[2], pData[3]);
        pData += 4;
        getProfileRspCmd.status = *pData++;
        getProfileRspCmd.profileIntervalPeriod = *pData++;
        getProfileRspCmd.numberOfPeriodsDelived = *pData++;
        getProfileRspCmd.intervals = pData;

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getProfileRspCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_requestMirrorPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    //uint8_t *pData = pInMsg->pData; //this command has no payload.

    if (pInMsg->clusterAppCb) {
    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_removeMirrorPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    //uint8_t *pData = pInMsg->pData; //this command has no payload.

    if (pInMsg->clusterAppCb) {
        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_requestFastPollModeRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zcl_metering_requestFastPollModeRspCmd_t requestFastPollModeRspCmd;
        requestFastPollModeRspCmd.appliedUpdatePeriod = *pData++;
        requestFastPollModeRspCmd.fastPollModeEndTime = BUILD_U32(pData[0], pData[1], pData[2], pData[3]);

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &requestFastPollModeRspCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_GET_PROFILE:
        status = zcl_metering_getProfilePrc(pInMsg);
        break;
    case ZCL_CMD_REQUEST_MIRROR_RESPONSE:
        status = zcl_metering_requestMirrorRspPrc(pInMsg);
        break;
    case ZCL_CMD_MIRROR_REMOVED:
        status = zcl_metering_mirrorRemovedPrc(pInMsg);
        break;
    case ZCL_CMD_REQUEST_FAST_POLL_MODE:
        status = zcl_metering_requestFastPollModePrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_GET_PROFILE_RESPONSE:
        status = zcl_metering_getProfileRspPrc(pInMsg);
        break;
    case ZCL_CMD_REQUEST_MIRROR:
        status = zcl_metering_requestMirrorPrc(pInMsg);
        break;
    case ZCL_CMD_REMOVE_MIRROR:
        status = zcl_metering_removeMirrorPrc(pInMsg);
        break;
    case ZCL_CMD_REQUEST_FAST_POLL_MODE_RESPONSE:
        status = zcl_metering_requestFastPollModeRspPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_metering_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_metering_clientCmdHandler(pInMsg);
    } else {
        return zcl_metering_serverCmdHandler(pInMsg);
    }
}

#endif  /* ZCL_METERING */

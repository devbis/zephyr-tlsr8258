/********************************************************************************************************
 * @file    zcl_door_lock.c
 *
 * @brief   This is the source file for zcl_door_lock
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


#ifdef ZCL_DOOR_LOCK
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
static status_t zcl_doorLock_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_doorLock_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_CLOSURES_DOOR_LOCK, manuCode, attrNum, attrTbl, zcl_doorLock_cmdHandler, cb);
}

_CODE_ZCL_ status_t zcl_doorLock_doorLockReq(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, uint8_t *pCode)
{
    status_t status = ZCL_STA_SUCCESS;
    uint8_t len = pCode[0] + 1;

    uint8_t *buf = (uint8_t *)ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = pCode[i];
    }

    status = zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_CLOSURES_DOOR_LOCK, cmdId, TRUE,
                         ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, len, buf);

    if (buf) {
        ev_buf_free(buf);
    }
    return status;
}

_CODE_ZCL_ status_t zcl_doorLock_doorLockRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t cmdId, status_t rspStatus)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_CLOSURES_DOOR_LOCK, cmdId, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 1, &rspStatus);
}


_CODE_ZCL_ static status_t zcl_doorLock_doorLockReqPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;

    if (pInMsg->clusterAppCb) {
        zcl_doorlockReq_t reqCmd;
        reqCmd.dataLen = pInMsg->dataLen;
        reqCmd.pData = pInMsg->pData;

    	status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &reqCmd);

    	epInfo_t dstEp;
    	ZB_SETSTRUCTCONTENT(dstEp, 0);

    	dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    	dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
    	dstEp.dstEp = pApsdeInd->indInfo.src_ep;
    	dstEp.profileId = pApsdeInd->indInfo.profile_id;

    	zcl_doorLock_doorLockRsp(pApsdeInd->indInfo.dst_ep, &dstEp, TRUE, pInMsg->hdr.seqNum, pInMsg->hdr.cmd, status);

    	status = ZCL_STA_CMD_HAS_RESP;
    } else {
    	status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_doorLock_doorLockRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        zcl_doorlockRsp_t rspCmd;
        rspCmd.status = pInMsg->pData[0];

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &rspCmd);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_doorLock_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_LOCK_DOOR:
    case ZCL_CMD_UNLOCK_DOOR:
    case ZCL_CMD_DOOR_LOCK_TOGGLE:
        status = zcl_doorLock_doorLockReqPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_doorLock_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_LOCK_DOOR_RESPONSE:
    case ZCL_CMD_UNLOCK_DOOR_RESPONSE:
    case ZCL_CMD_DOOR_LOCK_TOGGLE_RESPONSE:
        status = zcl_doorLock_doorLockRspPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_doorLock_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_doorLock_clientCmdHandler(pInMsg);
    } else {
        return zcl_doorLock_serverCmdHandler(pInMsg);
    }
}
#endif

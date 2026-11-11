/********************************************************************************************************
 * @file    zcl_ias_ace.c
 *
 * @brief   This is the source file for zcl_ias_ace
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

#ifdef ZCL_IAS_ACE
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
static status_t zcl_iasAce_cmdHandler(zclIncoming_t *pInMsg);

_CODE_ZCL_ status_t zcl_iasAce_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    return zcl_registerCluster(endpoint, ZCL_CLUSTER_SS_IAS_ACE, manuCode, attrNum, attrTbl, zcl_iasAce_cmdHandler, cb);
}

_CODE_ZCL_ status_t zcl_iasAce_armRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t armNotification)
{
    uint8_t buf[1];

    buf[0] = armNotification;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_ARM_RESPONSE, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 1, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneIdMapRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t *pZoneIdMapSection)
{
    uint8_t buf[32];
    uint8_t *pBuf = buf;

    for (uint8_t i = 0; i < 16; i++) {
    	*pBuf++ = pZoneIdMapSection[1];
    	*pBuf++ = pZoneIdMapSection[0];
    	pZoneIdMapSection += 2;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_ID_MAP_RESPONSE, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 32, (uint8_t*)buf);
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneInfoRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getZoneInfoRsp_t *pGetZoneInfoRsp)
{
    uint8_t buf[36];
    uint8_t *pBuf = buf;

    *pBuf++ = pGetZoneInfoRsp->zoneId;
    *pBuf++ = LO_UINT16(pGetZoneInfoRsp->zoneType);
    *pBuf++ = HI_UINT16(pGetZoneInfoRsp->zoneType);
    memcpy(pBuf, pGetZoneInfoRsp->ieeeAddr, 8);
    pBuf += 8;
    *pBuf++ = pGetZoneInfoRsp->zoneLabel.strLen;
    if (pGetZoneInfoRsp->zoneLabel.strLen) {
        memcpy(pBuf, pGetZoneInfoRsp->zoneLabel.pStr, pGetZoneInfoRsp->zoneLabel.strLen);
        pBuf += pGetZoneInfoRsp->zoneLabel.strLen;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_INFORMATION_RESPONSE, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_zoneStatusChanged(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneStatusChanged_t *pZoneStatusChanged)
{
    uint8_t buf[29];
    uint8_t *pBuf = buf;

    *pBuf++ = pZoneStatusChanged->zoneId;
    *pBuf++ = LO_UINT16(pZoneStatusChanged->zoneStatus);
    *pBuf++ = HI_UINT16(pZoneStatusChanged->zoneStatus);
    *pBuf++ = pZoneStatusChanged->audibleNotification;
    *pBuf++ = pZoneStatusChanged->zoneLabel.strLen;
    if (pZoneStatusChanged->zoneLabel.strLen) {
        memcpy(pBuf, pZoneStatusChanged->zoneLabel.pStr, pZoneStatusChanged->zoneLabel.strLen);
        pBuf += pZoneStatusChanged->zoneLabel.strLen;
    }

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_ZONE_STATUS_CHANGED, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_panelStatusChanged(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, panelStatusChanged_t *pPanelStatusChanged)
{
    uint8_t buf[4];

    buf[0] = pPanelStatusChanged->panelStatus;
    buf[1] = pPanelStatusChanged->secondsRemaining;
    buf[2] = pPanelStatusChanged->audibleNotification;
    buf[3] = pPanelStatusChanged->alarmStatus;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_PANEL_STATUS_CHANGED, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 4, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_getPanelStatusRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getPanelStatusRsp_t *pGetPanelStatusRsp)
{
    uint8_t buf[4];

    buf[0] = pGetPanelStatusRsp->panelStatus;
    buf[1] = pGetPanelStatusRsp->secondsRemaining;
    buf[2] = pGetPanelStatusRsp->audibleNotification;
    buf[3] = pGetPanelStatusRsp->panelStatus;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_PANEL_STATUS_RESPONSE, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 4, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_setBypassedZoneList(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, setBypassedZoneList_t *pSetBypassedZoneList)
{
    uint8_t len = 1;

    if (pSetBypassedZoneList->numOfZones) {
        len += pSetBypassedZoneList->numOfZones;
    }

    uint8_t *buf = ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pSetBypassedZoneList->numOfZones;
    if (pSetBypassedZoneList->numOfZones) {
        memcpy(pBuf, pSetBypassedZoneList->zoneIds, pSetBypassedZoneList->numOfZones);
        pBuf += pSetBypassedZoneList->numOfZones;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_SET_BYPASSED_ZONE_LIST, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_iasAce_bypassResp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, bypassRsp_t *pBypassRsp)
{
    uint8_t len = 1;

    if (pBypassRsp->numOfZones) {
        len += pBypassRsp->numOfZones;
    }

    uint8_t *buf = ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pBypassRsp->numOfZones;
    if (pBypassRsp->numOfZones) {
        memcpy(pBuf, pBypassRsp->zoneIds, pBypassRsp->numOfZones);
        pBuf += pBypassRsp->numOfZones;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_BYPASS_RESPONSE, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneStatusResp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getZoneStatusRsp_t *pGetZoneStatusRsp)
{
    uint8_t len = 2;

    if (pGetZoneStatusRsp->numOfZones) {
        len += (pGetZoneStatusRsp->numOfZones * 3);
    }

    uint8_t *buf = ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pGetZoneStatusRsp->zoneStatusComplete;
    *pBuf++ = pGetZoneStatusRsp->numOfZones;
    if (pGetZoneStatusRsp->numOfZones) {
        for (uint8_t i = 0; i < pGetZoneStatusRsp->numOfZones; i++) {
            memcpy(pBuf, &pGetZoneStatusRsp->zoneStatusInfo[i], 3);
            pBuf += 3;
        }
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_STATUS_RESPONSE, TRUE,
                ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_iasAce_arm(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, arm_t *pArm)
{
    uint8_t buf[11];
    uint8_t *pBuf = buf;

    *pBuf++ = pArm->armMode;
    *pBuf++ = pArm->code.strLen;
    if (pArm->code.strLen) {
        memcpy(pBuf, pArm->code.pStr, pArm->code.strLen);
        pBuf += pArm->code.strLen;
    }
    *pBuf++ = pArm->zoneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_ARM, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_bypass(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, bypass_t *pBypass)
{
    uint8_t len = 10;

    if (pBypass->numOfZones) {
        len += pBypass->numOfZones;
    }

    uint8_t *buf = ev_buf_allocate(len);
    if (!buf) {
        return ZCL_STA_INSUFFICIENT_SPACE;
    }

    uint8_t *pBuf = buf;

    *pBuf++ = pBypass->numOfZones;
    if (pBypass->numOfZones) {
        memcpy(pBuf, pBypass->zoneIds, pBypass->numOfZones);
        pBuf += pBypass->numOfZones;
    }
    *pBuf++ = pBypass->code.strLen;
    if (pBypass->code.strLen) {
        memcpy(pBuf, pBypass->code.pStr, pBypass->code.strLen);
        pBuf += pBypass->code.strLen;
    }

    zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_BYPASS, TRUE,
                ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, pBuf - buf, buf);

    ev_buf_free(buf);

    return ZCL_STA_SUCCESS;
}

_CODE_ZCL_ status_t zcl_iasAce_emergency(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_EMERGENCY, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_fire(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_FIRE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_panic(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_PANIC, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneIdMap(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_ID_MAP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneInfo(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t ZoneId)
{
    uint8_t buf[1];

    buf[0] = ZoneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_INFORMATION, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 1, buf);
}

_CODE_ZCL_ status_t zcl_iasAce_getPanelStatus(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_PANEL_STATUS, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_getBypassedZoneList(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_BYPASSED_ZONE_LIST, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasAce_getZoneStatus(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getZoneStatus_t *pGetZoneStatus)
{
    uint8_t buf[5];

    buf[0] = pGetZoneStatus->startingZoneId;
    buf[1] = pGetZoneStatus->maxNumOfZoneIds;
    buf[2] = pGetZoneStatus->zoneStatusMaskFlag;
    buf[3] = LO_UINT16(pGetZoneStatus->zoneStatusMask);
    buf[4] = HI_UINT16(pGetZoneStatus->zoneStatusMask);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ACE, ZCL_CMD_GET_ZONE_STATUS, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 5, buf);
}

_CODE_ZCL_ static uint8_t zcl_parseUTF8String(uint8_t *pData, UTF8String_t *pString, uint8_t maxLen)
{
    uint8_t len = 0;
    pString->strLen = *pData++;

    if (pString->strLen == 0xFF) {
        pString->strLen = 0;
    }

    if (pString->strLen == 0) {
        pString->pStr = NULL;
    } else {
        len = pString->strLen;

        pString->strLen = (len > maxLen) ? maxLen : len;
        pString->pStr = pData;
    }

    return (len + 1);
}

_CODE_ZCL_ static void zcl_armParse(uint8_t *pData, arm_t *pCmd)
{
    pCmd->armMode = *pData++;

    uint8_t len = zcl_parseUTF8String(pData, &pCmd->code, 8);
    pData += len;

    pCmd->zoneId = *pData++;
}

_CODE_ZCL_ static void zcl_getZoneInfoRspParse(uint8_t *pData, getZoneInfoRsp_t *pCmd)
{
    pCmd->zoneId = *pData++;
    pCmd->zoneType = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    memcpy(pCmd->ieeeAddr, pData, 8);
    pData += 8;
    zcl_parseUTF8String(pData, &pCmd->zoneLabel, 24);
}

_CODE_ZCL_ static void zcl_zoneStatusChangedParse(uint8_t *pData, zoneStatusChanged_t *pCmd)
{
    pCmd->zoneId = *pData++;
    pCmd->zoneStatus = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    pCmd->audibleNotification = *pData++;
    zcl_parseUTF8String(pData, &pCmd->zoneLabel, 24);
}

_CODE_ZCL_ static void zcl_bypassParse(uint8_t *pData, bypass_t *pCmd)
{
    pCmd->numOfZones = *pData++;
    pCmd->zoneIds = pData;
    pData += pCmd->numOfZones;

    zcl_parseUTF8String(pData, &pCmd->code, 8);
}

_CODE_ZCL_ static status_t zcl_iasAce_armPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    if (pInMsg->clusterAppCb) {
        uint8_t armNotification = 0xff;

        arm_t arm;
        ZB_SETSTRUCTCONTENT(arm, 0);

        zcl_armParse(pInMsg->pData, &arm);

        armNotification = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &arm);

        if (armNotification != 0xff) {
            epInfo_t dstEp;
            ZB_SETSTRUCTCONTENT(dstEp, 0);

            dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
            dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
            dstEp.dstEp = pApsdeInd->indInfo.src_ep;
            dstEp.profileId = pApsdeInd->indInfo.profile_id;

            zcl_iasAce_armRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, armNotification);

            status = ZCL_STA_CMD_HAS_RESP;
        } else {
            status = ZCL_STA_INVALID_VALUE;
        }
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_bypassPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        bypass_t bypass;
        ZB_SETSTRUCTCONTENT(bypass, 0);

        zcl_bypassParse(pInMsg->pData, &bypass);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &bypass);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_emergencyPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_firePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_panicPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneIdMapPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    if (pInMsg->clusterAppCb) {
        uint16_t zoneIdMapSection[16];
        uint16_t section = 0;

        for (uint8_t i = 0; i < 16; i++) {
            section = 0;

            for (uint8_t j = 0; j < 16; j++) {
                uint8_t zoneId = 16 * i + j;

                if (zcl_findZoneEntry(endpoint, zoneId)) {
                    section |= (0x01 << j);
                }
            }

            zoneIdMapSection[i] = section;
        }

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);

        epInfo_t dstEp;
        ZB_SETSTRUCTCONTENT(dstEp, 0);

        dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
        dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
        dstEp.dstEp = pApsdeInd->indInfo.src_ep;
        dstEp.profileId = pApsdeInd->indInfo.profile_id;

        zcl_iasAce_getZoneIdMapRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, (uint8_t *)zoneIdMapSection);

        status = ZCL_STA_CMD_HAS_RESP;
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneInformationPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;

    if (pInMsg->clusterAppCb) {
        uint8_t zoneId = pInMsg->pData[0];

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &zoneId);

        zcl_zoneTable_t *pZoneEntry = zcl_findZoneEntry(endpoint, zoneId);
        if (pZoneEntry) {
            epInfo_t dstEp;
            ZB_SETSTRUCTCONTENT(dstEp, 0);

            dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
            dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
            dstEp.dstEp = pApsdeInd->indInfo.src_ep;
            dstEp.profileId = pApsdeInd->indInfo.profile_id;

            getZoneInfoRsp_t getZoneInfoRsp;
            ZB_SETSTRUCTCONTENT(dstEp, 0);

            getZoneInfoRsp.zoneId = pZoneEntry->zone.zoneId;
            getZoneInfoRsp.zoneType = pZoneEntry->zone.zoneType;
            memcpy(getZoneInfoRsp.ieeeAddr, pZoneEntry->zone.zoneAddr, 8);
            //TODO: zoneLabel ?

            zcl_iasAce_getZoneInfoRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, &getZoneInfoRsp);

            status = ZCL_STA_CMD_HAS_RESP;
        } else {
            status = ZCL_STA_INVALID_VALUE;
        }
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getPanelStatusPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getBypassedZoneListPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneStatusPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        getZoneStatus_t getZoneStatus;
        getZoneStatus.startingZoneId = *pData++;
        getZoneStatus.maxNumOfZoneIds = *pData++;
        getZoneStatus.zoneStatusMaskFlag = *pData++;
        getZoneStatus.zoneStatusMask = BUILD_U16(pData[0], pData[1]);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getZoneStatus);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_armRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        uint8_t armNotification = pInMsg->pData[0];

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &armNotification);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneIdMapRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, pInMsg->pData);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneInfoRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        getZoneInfoRsp_t getZoneInfoRsp;
        ZB_SETSTRUCTCONTENT(getZoneInfoRsp, 0);

        zcl_getZoneInfoRspParse(pInMsg->pData, &getZoneInfoRsp);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getZoneInfoRsp);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_zoneStatusChangedPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        zoneStatusChanged_t zoneStatusChanged;
        ZB_SETSTRUCTCONTENT(zoneStatusChanged, 0);

        zcl_zoneStatusChangedParse(pInMsg->pData, &zoneStatusChanged);

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &zoneStatusChanged);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_panelStatusChangedPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        panelStatusChanged_t panelStatusChanged;
        panelStatusChanged.panelStatus = *pData++;
        panelStatusChanged.secondsRemaining = *pData++;
        panelStatusChanged.audibleNotification = *pData++;
        panelStatusChanged.alarmStatus = *pData++;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &panelStatusChanged);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getPanelStatusRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        getPanelStatusRsp_t getPanelStatusRsp;
        getPanelStatusRsp.panelStatus = *pData++;
        getPanelStatusRsp.secondsRemaining = *pData++;
        getPanelStatusRsp.audibleNotification = *pData++;
        getPanelStatusRsp.alarmStatus = *pData++;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getPanelStatusRsp);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_setBypassedZoneListPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        setBypassedZoneList_t setBypassedZoneList;
        setBypassedZoneList.numOfZones = *pData++;
        setBypassedZoneList.zoneIds = setBypassedZoneList.numOfZones ? pData : NULL;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &setBypassedZoneList);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_bypassRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        bypassRsp_t bypassRsp;
        bypassRsp.numOfZones = *pData++;
        bypassRsp.zoneIds = bypassRsp.numOfZones ? pData : NULL;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &bypassRsp);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_getZoneStatusRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        getZoneStatusRsp_t getZoneStatusRsp;
        getZoneStatusRsp.zoneStatusComplete = *pData++;
        getZoneStatusRsp.numOfZones = *pData++;
        getZoneStatusRsp.zoneStatusInfo = getZoneStatusRsp.numOfZones ? pData : NULL;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &getZoneStatusRsp);
    } else {
        status = ZCL_STA_FAILURE;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_ARM:
        status = zcl_iasAce_armPrc(pInMsg);
        break;
    case ZCL_CMD_BYPASS:
        status = zcl_iasAce_bypassPrc(pInMsg);
        break;
    case ZCL_CMD_EMERGENCY:
        status = zcl_iasAce_emergencyPrc(pInMsg);
        break;
    case ZCL_CMD_FIRE:
        status = zcl_iasAce_firePrc(pInMsg);
        break;
    case ZCL_CMD_PANIC:
        status = zcl_iasAce_panicPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_ID_MAP:
        status = zcl_iasAce_getZoneIdMapPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_INFORMATION:
        status = zcl_iasAce_getZoneInformationPrc(pInMsg);
        break;
    case ZCL_CMD_GET_PANEL_STATUS:
        status = zcl_iasAce_getPanelStatusPrc(pInMsg);
        break;
    case ZCL_CMD_GET_BYPASSED_ZONE_LIST:
        status = zcl_iasAce_getBypassedZoneListPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_STATUS:
        status = zcl_iasAce_getZoneStatusPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_ARM_RESPONSE:
        status = zcl_iasAce_armRspPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_ID_MAP_RESPONSE:
        status = zcl_iasAce_getZoneIdMapRspPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_INFORMATION_RESPONSE:
        status = zcl_iasAce_getZoneInfoRspPrc(pInMsg);
        break;
    case ZCL_CMD_ZONE_STATUS_CHANGED:
        status = zcl_iasAce_zoneStatusChangedPrc(pInMsg);
        break;
    case ZCL_CMD_PANEL_STATUS_CHANGED:
        status = zcl_iasAce_panelStatusChangedPrc(pInMsg);
        break;
    case ZCL_CMD_GET_PANEL_STATUS_RESPONSE:
        status = zcl_iasAce_getPanelStatusRspPrc(pInMsg);
        break;
    case ZCL_CMD_SET_BYPASSED_ZONE_LIST:
        status = zcl_iasAce_setBypassedZoneListPrc(pInMsg);
        break;
    case ZCL_CMD_BYPASS_RESPONSE:
        status = zcl_iasAce_bypassRspPrc(pInMsg);
        break;
    case ZCL_CMD_GET_ZONE_STATUS_RESPONSE:
        status = zcl_iasAce_getZoneStatusRspPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasAce_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_iasAce_clientCmdHandler(pInMsg);
    } else {
        return zcl_iasAce_serverCmdHandler(pInMsg);
    }
}

#endif

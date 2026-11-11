/********************************************************************************************************
 * @file    zcl_ias_zone.c
 *
 * @brief   This is the source file for zcl_ias_zone
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


#ifdef ZCL_IAS_ZONE
/**********************************************************************
 * LOCAL CONSTANTS
 */


/**********************************************************************
 * LOCAL TYPES
 */


/**********************************************************************
 * LOCAL VARIABLES
 */
zcl_zoneTable_t g_zcl_zoneTab[ZCL_ZONE_TABLE_NUM];

/**********************************************************************
 * LOCAL FUNCTIONS
 */
void zcl_zoneTabClear(void);

static status_t zcl_iasZone_cmdHandler(zclIncoming_t *pInMsg);


_CODE_ZCL_ status_t zcl_iasZone_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
    zcl_zoneTabClear();

    return zcl_registerCluster(endpoint, ZCL_CLUSTER_SS_IAS_ZONE, manuCode, attrNum, attrTbl, zcl_iasZone_cmdHandler, cb);
}

_CODE_ZCL_ static bool zcl_zoneTypeMatched(uint16_t zoneType)
{
    switch (zoneType) {
    case ZONE_TYPE_STANDARD_CIE:
    case ZONE_TYPE_MOTION_SENSOR:
    case ZONE_TYPE_CONTACT_SWITCH:
    case ZONE_TYPE_DOOR_WINDOW_HANDLE:
    case ZONE_TYPE_FIRE_SENSOR:
    case ZONE_TYPE_WATER_SENSOR:
    case ZONE_TYPE_CARBON_MONOXIDE_SENSOR:
    case ZONE_TYPE_PERSONAL_EMERGENCY_DEV:
    case ZONE_TYPE_VIBRATION_MOVEMENT_SENSOR:
    case ZONE_TYPE_REMOTE_CONTROL:
    case ZONE_TYPE_KEY_FOB:
    case ZONE_TYPE_KEY_PAD:
    case ZONE_TYPE_STANDARD_WARNING_DEV:
    case ZONE_TYPE_GLASS_BREAK_SENSOR:
    case ZONE_TYPE_SECURITY_REPEATER:
        return TRUE;
        break;
    default:
        return FALSE;
        break;
    }
}

_CODE_ZCL_ void zcl_zoneClearEntry(zcl_zoneTable_t *pZoneTab)
{
    if (pZoneTab) {
        pZoneTab->used = 0;
        pZoneTab->endpoint = 0xfe;
        memset((uint8_t *)&pZoneTab->zone, 0, sizeof(zcl_zoneTabEntry_t));
    }
}

_CODE_ZCL_ void zcl_zoneTabClear(void)
{
    for (uint8_t i = 0; i < ZCL_ZONE_TABLE_NUM; i++) {
    	zcl_zoneTable_t *pZoneTab = &g_zcl_zoneTab[i];

    	zcl_zoneClearEntry(pZoneTab);
    }
}

_CODE_ZCL_ zcl_zoneTable_t *zcl_getZoneFreeEntry(void)
{
    for (uint8_t i = 0; i < ZCL_ZONE_TABLE_NUM; i++) {
        zcl_zoneTable_t *pZoneTab = &g_zcl_zoneTab[i];

        if (!pZoneTab->used) {
            return pZoneTab;
        }
    }

    return NULL;
}

_CODE_ZCL_ uint8_t zcl_getZoneId(void)
{
    uint8_t zoneId = 0;

    do {
        uint8_t i = 0;
        for (i = 0; i < ZCL_ZONE_TABLE_NUM; i++) {
            zcl_zoneTable_t *pZoneTab = &g_zcl_zoneTab[i];

            if (pZoneTab->used && (pZoneTab->zone.zoneId == zoneId)) {
                break;
            }
        }
        if (i >= ZCL_ZONE_TABLE_NUM) {
            return zoneId;
        }
    } while (++zoneId < ZCL_ZONE_ID_INVALID);

    return ZCL_ZONE_ID_INVALID;
}

_CODE_ZCL_ zcl_zoneTable_t *zcl_addZoneEntry(uint8_t endpoint, uint16_t zoneType, addrExt_t zoneAddr, uint8_t *rspCode)
{
    zcl_zoneTable_t *pZoneTab = zcl_getZoneFreeEntry();
    if (!pZoneTab) {
        *rspCode = ZONE_ENROLL_TOO_MANY_ZONES;
        return NULL;
    }

    uint8_t zoneId = zcl_getZoneId();
    if (zoneId == ZCL_ZONE_ID_INVALID) {
        *rspCode = ZONE_ENROLL_TOO_MANY_ZONES;
        return NULL;
    }

    pZoneTab->used = 1;
    pZoneTab->endpoint = endpoint;
    pZoneTab->zone.zoneType = zoneType;
    pZoneTab->zone.zoneId = zoneId;
    ZB_IEEE_ADDR_COPY(pZoneTab->zone.zoneAddr, zoneAddr);

    *rspCode = ZONE_ENROLL_SUCC;
    return pZoneTab;
}

_CODE_ZCL_ zcl_zoneTable_t *zcl_findZoneEntry(uint8_t endpoint, uint8_t zoneId)
{
    for (uint8_t i = 0; i < ZCL_ZONE_TABLE_NUM; i++) {
        zcl_zoneTable_t *pZoneTab = &g_zcl_zoneTab[i];

        if (pZoneTab->used && (pZoneTab->endpoint == endpoint) && (pZoneTab->zone.zoneId == zoneId)) {
            return pZoneTab;
        }
    }

    return NULL;
}

_CODE_ZCL_ zcl_zoneTable_t *zcl_searchZoneEntry(uint8_t endpoint, uint16_t zoneType, addrExt_t zoneAddr)
{
    for (uint8_t i = 0; i < ZCL_ZONE_TABLE_NUM; i++) {
        zcl_zoneTable_t *pZoneTab = &g_zcl_zoneTab[i];

        if (pZoneTab->used && (pZoneTab->endpoint == endpoint) && (pZoneTab->zone.zoneType == zoneType) &&
            ZB_IEEE_ADDR_CMP(pZoneTab->zone.zoneAddr, zoneAddr)) {
            return pZoneTab;
        }
    }

    return NULL;
}

_CODE_ZCL_ status_t zcl_iasZone_enrollRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneEnrollRsp_t *pEnrollRsp)
{
    uint8_t buf[2];

    buf[0] = pEnrollRsp->code;
    buf[1] = pEnrollRsp->zoneId;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_CMD_ZONE_ENROLL_RSP, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_iasZone_initNormalOperationMode(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo)
{
    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_CMD_INIT_NORMAL_OPERATION_MODE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 0, NULL);
}

_CODE_ZCL_ status_t zcl_iasZone_initTestMode(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneInitTestMode_t *pInitTestMode)
{
    uint8_t buf[2];

    buf[0] = pInitTestMode->testModeDuration;
    buf[1] = pInitTestMode->currZoneSensLevel;

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_CMD_INIT_TEST_MODE, TRUE,
                       ZCL_FRAME_CLIENT_SERVER_DIR, disableDefaultRsp, 0, seqNo, 2, buf);
}

_CODE_ZCL_ status_t zcl_iasZone_enrollReq(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneEnrollReq_t *pEnrollReq)
{
    uint8_t buf[4];

    buf[0] = LO_UINT16(pEnrollReq->zoneType);
    buf[1] = HI_UINT16(pEnrollReq->zoneType);
    buf[2] = LO_UINT16(pEnrollReq->manufacturerCode);
    buf[3] = HI_UINT16(pEnrollReq->manufacturerCode);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_CMD_ZONE_ENROLL_REQ, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 4, buf);
}

_CODE_ZCL_ status_t zcl_iasZone_statusChangeNotification(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneStatusChangeNoti_t *pStatusChangeNoti)
{
    uint8_t buf[6];

    buf[0] = LO_UINT16(pStatusChangeNoti->zoneStatus);
    buf[1] = HI_UINT16(pStatusChangeNoti->zoneStatus);
    buf[2] = pStatusChangeNoti->extStatus;
    buf[3] = pStatusChangeNoti->zoneId;
    buf[4] = LO_UINT16(pStatusChangeNoti->delay);
    buf[5] = HI_UINT16(pStatusChangeNoti->delay);

    return zcl_sendCmd(srcEp, pDstEpInfo, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_CMD_ZONE_STATUS_CHANGE_NOTIFICATION, TRUE,
                       ZCL_FRAME_SERVER_CLIENT_DIR, disableDefaultRsp, 0, seqNo, 6, buf);
}

_CODE_ZCL_ static status_t zcl_enrollRspPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        zoneEnrollRsp_t zoneEnrollRsp;
        zoneEnrollRsp.code = pInMsg->pData[0];
        zoneEnrollRsp.zoneId = pInMsg->pData[1];

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &zoneEnrollRsp);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_initNormalOperationModePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, NULL);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_initTestModePrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    if (pInMsg->clusterAppCb) {
        zoneInitTestMode_t zoneInitTestMode;
        zoneInitTestMode.testModeDuration = pInMsg->pData[0];
        zoneInitTestMode.currZoneSensLevel = pInMsg->pData[1];

        status = pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &zoneInitTestMode);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_zoneStatusChangeNotificationPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    uint8_t *pData = pInMsg->pData;

    if (pInMsg->clusterAppCb) {
        zoneStatusChangeNoti_t statusChangeNoti;
        statusChangeNoti.zoneStatus = BUILD_U16(pData[0], pData[1]);
        pData += 2;
        statusChangeNoti.extStatus = *pData++;
        statusChangeNoti.zoneId = *pData++;
        statusChangeNoti.delay = BUILD_U16(pData[0], pData[1]);
        pData += 2;

        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &statusChangeNoti);
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_zoneEnrollReqPrc(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;
    apsdeDataInd_t *pApsdeInd = (apsdeDataInd_t*)pInMsg->msg;
    uint8_t endpoint = pApsdeInd->indInfo.dst_ep;
    uint8_t *pData = pInMsg->pData;

    zoneEnrollReq_t enrollReq;

    enrollReq.zoneType = BUILD_U16(pData[0], pData[1]);
    pData += 2;
    enrollReq.manufacturerCode = BUILD_U16(pData[0], pData[1]);
    pData += 2;

    zoneEnrollRsp_t enrollRsp;
    memset((uint8_t *)&enrollRsp, 0, sizeof(zoneEnrollRsp_t));
    enrollRsp.zoneId = 0xFF;

    if (zcl_zoneTypeMatched(enrollReq.zoneType)) {
        addrExt_t zoneAddr;
        zb_address_ieee_by_short(pApsdeInd->indInfo.src_short_addr, zoneAddr);

        zcl_zoneTable_t *pZoneTab = zcl_searchZoneEntry(endpoint, enrollReq.zoneType, zoneAddr);
        if (!pZoneTab) {
            pZoneTab = zcl_addZoneEntry(endpoint, enrollReq.zoneType, zoneAddr, &(enrollRsp.code));
            if (pZoneTab && (enrollRsp.code == ZONE_ENROLL_SUCC)) {
                enrollRsp.zoneId = pZoneTab->zone.zoneId;
            }
        } else {
            enrollRsp.zoneId = pZoneTab->zone.zoneId;
            enrollRsp.code = ZONE_ENROLL_SUCC;
        }
    } else {
        enrollRsp.code = ZONE_ENROLL_NOT_SUPPORTED;
    }

    if (pInMsg->clusterAppCb) {
        pInMsg->clusterAppCb(&(pInMsg->addrInfo), pInMsg->hdr.cmd, &enrollReq);
    }

    epInfo_t dstEp;
    ZB_SETSTRUCTCONTENT(dstEp, 0);

    dstEp.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
    dstEp.dstAddr.shortAddr = pApsdeInd->indInfo.src_short_addr;
    dstEp.dstEp = pApsdeInd->indInfo.src_ep;
    dstEp.profileId = pApsdeInd->indInfo.profile_id;

    zcl_iasZone_enrollRsp(endpoint, &dstEp, TRUE, pInMsg->hdr.seqNum, &enrollRsp);

    status = ZCL_STA_CMD_HAS_RESP;

    return status;
}

_CODE_ZCL_ static status_t zcl_iasZone_clientCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_ZONE_ENROLL_RSP:
        status = zcl_enrollRspPrc(pInMsg);
        break;
    case ZCL_CMD_INIT_NORMAL_OPERATION_MODE:
        status = zcl_initNormalOperationModePrc(pInMsg);
        break;
    case ZCL_CMD_INIT_TEST_MODE:
        status = zcl_initTestModePrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasZone_serverCmdHandler(zclIncoming_t *pInMsg)
{
    uint8_t status = ZCL_STA_SUCCESS;

    switch (pInMsg->hdr.cmd) {
    case ZCL_CMD_ZONE_STATUS_CHANGE_NOTIFICATION:
        status = zcl_zoneStatusChangeNotificationPrc(pInMsg);
        break;
    case ZCL_CMD_ZONE_ENROLL_REQ:
        status = zcl_zoneEnrollReqPrc(pInMsg);
        break;
    default:
        status = ZCL_STA_UNSUP_CLUSTER_COMMAND;
        break;
    }

    return status;
}

_CODE_ZCL_ static status_t zcl_iasZone_cmdHandler(zclIncoming_t *pInMsg)
{
    if (pInMsg->hdr.frmCtrl.bf.dir == ZCL_FRAME_CLIENT_SERVER_DIR) {
        return zcl_iasZone_clientCmdHandler(pInMsg);
    } else {
        return zcl_iasZone_serverCmdHandler(pInMsg);
    }
}

#endif  /* ZCL_IAS_ZONE */

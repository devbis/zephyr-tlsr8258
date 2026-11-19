/********************************************************************************************************
 * @file    zcl_ias_ace.h
 *
 * @brief   This is the header file for zcl_ias_ace
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
#ifndef ZCL_IAS_ACE_H
#define ZCL_IAS_ACE_H


/*********************************************************************
 * CONSTANTS
 */
/* Command ID */
// Client generated command
#define ZCL_CMD_ARM                             0x00
#define ZCL_CMD_BYPASS                          0x01
#define ZCL_CMD_EMERGENCY                       0x02
#define ZCL_CMD_FIRE                            0x03
#define ZCL_CMD_PANIC                           0x04
#define ZCL_CMD_GET_ZONE_ID_MAP                 0x05
#define ZCL_CMD_GET_ZONE_INFORMATION            0x06
#define ZCL_CMD_GET_PANEL_STATUS                0x07
#define ZCL_CMD_GET_BYPASSED_ZONE_LIST          0x08
#define ZCL_CMD_GET_ZONE_STATUS                 0x09

// Server generated command
#define ZCL_CMD_ARM_RESPONSE                    0x00
#define ZCL_CMD_GET_ZONE_ID_MAP_RESPONSE        0x01
#define ZCL_CMD_GET_ZONE_INFORMATION_RESPONSE   0x02
#define ZCL_CMD_ZONE_STATUS_CHANGED             0x03
#define ZCL_CMD_PANEL_STATUS_CHANGED            0x04
#define ZCL_CMD_GET_PANEL_STATUS_RESPONSE       0x05
#define ZCL_CMD_SET_BYPASSED_ZONE_LIST          0x06
#define ZCL_CMD_BYPASS_RESPONSE                 0x07
#define ZCL_CMD_GET_ZONE_STATUS_RESPONSE        0x08


/*********************************************************************
 * TYPES
 */
/**
 *  @brief  Structure definition for arm command
 */
typedef struct {
    UTF8String_t code;
    uint8_t armMode;
    uint8_t zoneId;
} arm_t;

/**
 *  @brief  Structure definition for bypass command
 */
typedef struct {
    uint8_t *zoneIds;
    UTF8String_t code;
    uint8_t numOfZones;
} bypass_t;

/**
 *  @brief  Structure definition for get zone status command
 */
typedef struct {
    uint16_t zoneStatusMask;
    uint8_t startingZoneId;
    uint8_t maxNumOfZoneIds;
    bool zoneStatusMaskFlag;
} getZoneStatus_t;

/**
 *  @brief  Structure definition for get zone information response command
 */
typedef struct {
    UTF8String_t zoneLabel;
    uint8_t zoneId;
    uint16_t zoneType;
    extAddr_t ieeeAddr;
} getZoneInfoRsp_t;

/**
 *  @brief  Structure definition for zone status changed command
 */
typedef struct {
    UTF8String_t zoneLabel;
    uint8_t zoneId;
    uint16_t zoneStatus;
    uint8_t audibleNotification;
} zoneStatusChanged_t;

/**
 *  @brief  Structure definition for panel status changed command
 */
typedef struct {
    uint8_t panelStatus;
    uint8_t secondsRemaining;
    uint8_t audibleNotification;
    uint8_t alarmStatus;
} panelStatusChanged_t;

/**
 *  @brief  Structure definition for get panel status response command
 */
typedef struct {
    uint8_t panelStatus;
    uint8_t secondsRemaining;
    uint8_t audibleNotification;
    uint8_t alarmStatus;
} getPanelStatusRsp_t;

/**
 *  @brief  Structure definition for set bypassed zone list command
 */
typedef struct {
    uint8_t *zoneIds;
    uint8_t numOfZones;
} setBypassedZoneList_t;

/**
 *  @brief  Structure definition for bypass response command
 */
typedef struct {
    uint8_t *zoneIds;
    uint8_t numOfZones;
} bypassRsp_t;

/**
 *  @brief  Structure definition for get zone status response command
 */
typedef struct {
    uint8_t *zoneStatusInfo;
    uint8_t zoneStatusComplete;
    uint8_t numOfZones;
} getZoneStatusRsp_t;

#if 0
/* Command Callback Function Type */
typedef uint8_t (*zcl_armCb_t)(apsdeDataInd_t *pApsdeInd, arm_t *pArm);
typedef void (*zcl_bypassCb_t)(apsdeDataInd_t *pApsdeInd, bypass_t *pBypass);
typedef void (*zcl_emergencyCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_fireCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_panicCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_getZoneIdMapCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_getZoneInformationCb_t)(apsdeDataInd_t *pApsdeInd, uint8_t zoneId);
typedef void (*zcl_getPanelStatusCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_getBypassZoneListCb_t)(apsdeDataInd_t *pApsdeInd);
typedef void (*zcl_getZoneStatusCb_t)(apsdeDataInd_t *pApsdeInd, getZoneStatus_t *pGetZoneStatus);
typedef void (*zcl_armRspCb_t)(apsdeDataInd_t *pApsdeInd, uint8_t armNotification);
typedef void (*zcl_getZoneIdMapRspCb_t)(apsdeDataInd_t *pApsdeInd, uint8_t *pZoneIdMapRsp);
typedef void (*zcl_getZoneInformationRspCb_t)(apsdeDataInd_t *pApsdeInd, getZoneInfoRsp_t *pGetZoneInfoRsp);
typedef void (*zcl_zoneStatusChangedCb_t)(apsdeDataInd_t *pApsdeInd, zoneStatusChanged_t *pZoneStatusChanged);
typedef void (*zcl_panelStatusChangedCb_t)(apsdeDataInd_t *pApsdeInd, panelStatusChanged_t *pPanelStatusChanged);
typedef void (*zcl_getPanelStatusRspCb_t)(apsdeDataInd_t *pApsdeInd, getPanelStatusRsp_t *pGetPanelStatusRsp);
typedef void (*zcl_setBypassedZoneListCb_t)(apsdeDataInd_t *pApsdeInd, setBypassedZoneList_t *pSetBypassedZoneList);
typedef void (*zcl_bypassRspCb_t)(apsdeDataInd_t *pApsdeInd, bypassRsp_t *pBypassRsp);
typedef void (*zcl_getZoneStatusRspCb_t)(apsdeDataInd_t *pApsdeInd, getZoneStatusRsp_t *pGetZoneStatusRsp);

/**
 *  @brief  Structure definition for commands callback functions in ias ace cluster
 */
typedef struct {
    zcl_armCb_t armCbFunc;
    zcl_bypassCb_t bypassCbFunc;
    zcl_emergencyCb_t emergencyCbFunc;
    zcl_fireCb_t fireCbFunc;
    zcl_panicCb_t panicCbFunc;
    zcl_getZoneIdMapCb_t getZoneIdMapCbFunc;
    zcl_getZoneInformationCb_t getZoneInformationCbFunc;
    zcl_getPanelStatusCb_t getPanelStatusCbFunc;
    zcl_getBypassZoneListCb_t getBypassZoneListCbFunc;
    zcl_getZoneStatusCb_t getZoneStatusCbFunc;
    zcl_armRspCb_t armRspCbFunc;
    zcl_getZoneIdMapRspCb_t getZoneIdMapRspCbFunc;
    zcl_getZoneInformationRspCb_t getZoneInformationRspCbFunc;
    zcl_zoneStatusChangedCb_t zoneStatusChangedCbFunc;
    zcl_panelStatusChangedCb_t panelStatusChangedCbFunc;
    zcl_getPanelStatusRspCb_t getPanelStatusRspCbFunc;
    zcl_setBypassedZoneListCb_t setBypassedZoneListCbFunc;
    zcl_bypassRspCb_t bypassRspCbFunc;
    zcl_getZoneStatusRspCb_t getZoneStatusRspCbFunc;
} zcl_iasAce_AppCallbacks_t;
#endif

status_t zcl_iasAce_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

status_t zcl_iasAce_zoneStatusChanged(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zoneStatusChanged_t *pZoneStatusChanged);

status_t zcl_iasAce_panelStatusChanged(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, panelStatusChanged_t *pPanelStatusChanged);

status_t zcl_iasAce_getPanelStatusRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getPanelStatusRsp_t *pGetPanelStatusRsp);

status_t zcl_iasAce_setBypassedZoneList(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, setBypassedZoneList_t *pSetBypassedZoneList);

status_t zcl_iasAce_bypassResp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, bypassRsp_t *pBypassRsp);

status_t zcl_iasAce_getZoneStatusResp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getZoneStatusRsp_t *pGetZoneStatusRsp);

status_t zcl_iasAce_arm(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, arm_t *pArm);
#define zcl_iasAce_armCmd(a,b,c,d)      (zcl_iasAce_arm((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_iasAce_bypass(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, bypass_t *pBypass);
#define zcl_iasAce_bypassCmd(a,b,c,d)   (zcl_iasAce_bypass((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_iasAce_emergency(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_emergencyCmd(a,b,c)  (zcl_iasAce_emergency((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_fire(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_fireCmd(a,b,c)       (zcl_iasAce_fire((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_panic(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_panicCmd(a,b,c)      (zcl_iasAce_panic((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_getZoneIdMap(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_getZoneIdMapCmd(a,b,c)       (zcl_iasAce_getZoneIdMap((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_getZoneInfo(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, uint8_t ZoneId);
#define zcl_iasAce_getZoneInfoCmd(a,b,c,d)      (zcl_iasAce_getZoneInfo((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_iasAce_getPanelStatus(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_getPanelStatusCmd(a,b,c)     (zcl_iasAce_getPanelStatus((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_getBypassedZoneList(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_iasAce_getBypassedZoneListCmd(a,b,c)        (zcl_iasAce_getBypassedZoneList((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_iasAce_getZoneStatus(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, getZoneStatus_t *pGetZoneStatus);
#define zcl_iasAce_getZoneStatusCmd(a,b,c,d)    (zcl_iasAce_getZoneStatus((a), (b), (c), ZCL_SEQ_NUM, (d)))

#endif	/* ZCL_IAS_ACE_H */

/********************************************************************************************************
 * @file    zcl_pollCtrl.h
 *
 * @brief   This is the header file for zcl_pollCtrl
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
#ifndef ZCL_POLL_CTRL_H
#define ZCL_POLL_CTRL_H


/**
 *  @brief	zcl_pollCtrl_cmdId POLL CONTROL Command Ids
 */
// Client generated command
#define ZCL_CMD_CHK_IN_RSP                      0x00
#define ZCL_CMD_FAST_POLL_STOP                  0x01
#define ZCL_CMD_SET_LONG_POLL_INTERVAL          0x02
#define ZCL_CMD_SET_SHORT_POLL_INTERVAL         0x03

// Server generated command
#define ZCL_CMD_CHK_IN                          0x00


/**
 *  @brief	zcl_pollCtrl_attrId POLL CONTROL Attribute Ids
 */
#define ZCL_ATTRID_CHK_IN_INTERVAL              0x0000
#define ZCL_ATTRID_LONG_POLL_INTERVAL           0x0001
#define ZCL_ATTRID_SHORT_POLL_INTERVAL          0x0002
#define ZCL_ATTRID_FAST_POLL_TIMEOUT            0x0003
#define ZCL_ATTRID_CHK_IN_INTERVAL_MIN          0x0004
#define ZCL_ATTRID_LONG_POLL_INTERVAL_MIN       0x0005
#define ZCL_ATTRID_FAST_POLL_TIMEOUT_MAX        0x0006

/**
 *  @brief  Check-in response payload
 */
typedef struct {
    bool startFastPolling;
    uint16_t fastPollTimeout;
} zcl_chkInRsp_t;

/**
 *  @brief  Set long poll interval command payload
 */
typedef struct {
    uint32_t newLongPollInterval;
} zcl_setLongPollInterval_t;

/**
 *  @brief  Set short poll interval command payload
 */
typedef struct {
    uint16_t newShortPollInterval;
} zcl_setShortPollInterval_t;


#if 0
/**
 *  @brief  Definition for pollCtrl command callback function type
 */
typedef status_t (*zcl_pollCtrl_chkInRspCb_t)(apsdeDataInd_t *pApsdeInd, zcl_chkInRsp_t *pCmd);
typedef status_t (*zcl_pollCtrl_fastPollStopCb_t)(apsdeDataInd_t *pApsdeInd);
typedef status_t (*zcl_pollCtrl_setLongPollIntervalCb_t)(apsdeDataInd_t *pApsdeInd, zcl_setLongPollInterval_t *pCmd);
typedef status_t (*zcl_pollCtrl_setShortPollIntervalCb_t)(apsdeDataInd_t *pApsdeInd, zcl_setShortPollInterval_t *pCmd);
typedef status_t (*zcl_pollCtrl_chkInCb_t)(apsdeDataInd_t *pApsdeInd, uint8_t seqNum);

/**
 *  @brief  Structure definition for commands callback functions in pollCtrl cluster
 */
typedef struct {
    zcl_pollCtrl_chkInRspCb_t pollCtrlChkInRspCbFunc;
    zcl_pollCtrl_fastPollStopCb_t pollCtrlFastPollStopCbFunc;
    zcl_pollCtrl_setLongPollIntervalCb_t pollCtrlSetLongPollIntervalCbFunc;
    zcl_pollCtrl_setShortPollIntervalCb_t pollCtrlSetShortPollIntervalCbFunc;
    zcl_pollCtrl_chkInCb_t pollCtrlChkInCbFunc;
} zcl_pollCtrl_AppCallbacks_t;


/**
 *  @brief  Structure definition for poll control attribute
 */
typedef struct {
    uint32_t chkInInterval;
    uint32_t longPollInterval;
    uint32_t chkInIntervalMin;
    uint32_t longPollIntervalMin;
    uint16_t shortPollInterval;
    uint16_t fastPollTimeout;
    uint16_t fastPollTimeoutMax;
} zcl_pollCtrl_attr_t;

/**
 *  @brief  External variable for pollCtrl Attribute
 */
extern zcl_pollCtrl_attr_t gZclPollCtrlAttr;

/**
 *  @brief  External variable for pollCtrl Attribute info table
 */
extern const zclAttrInfo_t pollCtrl_attrTbl[];

extern const uint8_t zcl_pollCtrl_attrNum;
#endif


status_t zcl_pollCtrl_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

status_t zcl_pollCtrl_chkIn(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_pollCtrl_checkInCmd(a,b,c)  (zcl_pollCtrl_chkIn((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_pollCtrl_chkInRsp(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_chkInRsp_t *pCheckInRsp);
#define zcl_pollCtrl_chkInRspCmd(a,b,c,d,e)     (zcl_pollCtrl_chkInRsp((a), (b), (c), (d), (e)))

status_t zcl_pollCtrl_fastPollStop(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo);
#define zcl_pollCtrl_fastPollStopCmd(a,b,c)     (zcl_pollCtrl_fastPollStop((a), (b), (c), ZCL_SEQ_NUM))

status_t zcl_pollCtrl_setLongPollInterval(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_setLongPollInterval_t *pSetLongPollInterval);
#define zcl_pollCtrl_setLongPollIntervalCmd(a,b,c,d)    (zcl_pollCtrl_setLongPollInterval((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_pollCtrl_setShortPollInterval(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_setShortPollInterval_t *pSetShortPollInterval);
#define zcl_pollCtrl_setShortPollIntervalCmd(a,b,c,d)   (zcl_pollCtrl_setShortPollInterval((a), (b), (c), ZCL_SEQ_NUM, (d)))

#endif	/* ZCL_POLL_CTRL_H */

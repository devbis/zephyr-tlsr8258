/********************************************************************************************************
 * @file    gp_sink.h
 *
 * @brief   This is the header file for gp_sink
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
#ifndef GP_SINK_H
#define GP_SINK_H


#define GPS_MAX_SINK_TABLE_ENTRIES              5

//A4.2.1
#define GP_COMMISSIONING_PACKET_LEN_MAX         55

#define SINK_GROUP_LIST_NUM                     2

#define ZCL_GP_MAX_SINK_TABLE_ATTR_LEN          (45 * 2)

#define GPS_MULTI_BUF_SIZE_MAX                  3

typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t communicationMode:2;
        uint16_t seqNumCapabilities:1;
        uint16_t rxOnCapability:1;
        uint16_t fixedLocation:1;
        uint16_t assignedAlias:1;
        uint16_t securityUse:1;
        uint16_t reserved:6;
    } bits;
} gpSinkEntryOpt_t;

/**
 *  @brief	Sink table entry
 */
typedef struct _attribute_packed_ {
    gpdId_t gpdId;              //8-bytes
    uint32_t gpdSecFrameCnt;
    gpSinkEntryOpt_t options;
    uint16_t gpdAssignedAlias;       //8-bytes
    gpSinkGroupListItem_t groupList[SINK_GROUP_LIST_NUM]; //8-bytes
    uint8_t endpoint;
    uint8_t deviceId;
    uint8_t groupcastRadius;
    gpSecOpt_t secOptions;      //4-bytes
    uint8_t gpdKey[16];              //16-bytes
    uint8_t used:1;
    uint8_t sinkGroupListCnt:2;
    uint8_t complete:1;
    uint8_t replyIncludeKey:1;
    uint8_t replyEncrypt:1;
    uint8_t reserved:2;
} gpSinkTabEntry_t; //45-bytes

/***************************************************************************
* @brief	Define for GPs
*/
typedef struct {
    ev_timer_event_t *gpsCommWindowTimeoutEvt;
    ev_timer_event_t *gpsMultiSensorCommTimeoutEvt;
    uint8_t *pMultiSensorCommBuf[GPS_MULTI_BUF_SIZE_MAX];
    uint8_t multiSensorCommBufSize;
    uint8_t multiSensorCommTime;
    uint8_t multiSensorTotalNumReport;
    uint8_t multiSensorNumOfReport;
    uint8_t multiSensorCompleted:1;
    uint8_t multiSensorReportMatched:1;
    uint8_t resv:6;
    uint8_t gpsInCommMode;
    uint8_t gpsAppEndpoint;
} gps_ctx_t;

typedef struct {
    uint8_t devId;
    uint8_t gpdCommanadId;
    uint16_t clusterId;
    uint8_t zigEndpoint;
    uint8_t zigbeeCommandId;
    uint8_t payloadLen;
    uint8_t changeIndex;
    uint8_t payload[8];
} gp_gpdZigCommand_t;

//global value
typedef struct _attribute_packed_ {
    gpSinkTabEntry_t gpSinkTab[GPS_MAX_SINK_TABLE_ENTRIES];
    uint8_t gpSinkTabNum;
} gp_sinkTab_t;

extern gp_sinkTab_t g_gpSinkTab;
extern gps_ctx_t g_gpsCtx;

gpSecRsp_status_t gpSinkSecOperation(uint8_t appId, gpdId_t gpdId,
                                     uint8_t endpoint, uint32_t gpdSecFrameCnt,
                                     uint8_t gpdfSecurityLevel, uint8_t gpdfKeyType,
                                     uint8_t *pKeyType, uint8_t *pKey);

void gpsGpDataIndProcess(gp_data_ind_t *pGpDataInd);

void gpSinkTabInit(void);
void gpSinkTabUpdate(void);
gpSinkTabEntry_t *gp_sinkTabEntryFreeGet(void);
gpSinkTabEntry_t *gp_getSinkTabByGpdId(uint8_t appId, gpdId_t gpdId);
void gp_sinkRemoveEntries(uint8_t appId, gpdId_t gpdId, uint8_t endpoint);
void gp_sinkGroupListAdd(gpSinkTabEntry_t *pEntry, uint8_t endpoint);
uint8_t gp_getSinkTabEntryTotalNum(void);
uint8_t gp_getSinkTabEntryLen(gpSinkTabEntry_t *pEntry);
uint8_t gp_buildSinkTabEntryFormat(gpSinkTabEntry_t *pEntry, uint8_t *pBuf);
void gp_sinkInit(uint8_t endpoint);

void gpCommissioningNotificationCmdProcess(zcl_gp_commissioningNotificationCmd_t *pCmd);

uint8_t gpsCommissionModeSet(bool action, bool involveProxies);

status_t zcl_gpClientCmdHandler(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);

#endif	/* GP_SINK_H */

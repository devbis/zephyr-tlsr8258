/********************************************************************************************************
 * @file    gp.h
 *
 * @brief   This is the header file for gp
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
#ifndef GP_H
#define GP_H

#include "dGP_stub.h"


//if ZERO, means only BASIC PROXY include.
#ifndef GP_BASIC_COMBO
#define GP_BASIC_COMBO                                  0
#endif

#define GREEN_POWER_ENDPOINT                            0xF2//242

//for Green Power device frame
#define GP_ZB_PROTOCOL_VERSION                          0x03

//GP Shared Key
#define GP_SHARED_KEY                                   { 0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,\
                                                          0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF }

#define DEFAULT_TC_LINK_KEY                             { 0x5a,0x69,0x67,0x42,0x65,0x65,0x41,0x6c,\
                                                          0x6c,0x69,0x61,0x6e,0x63,0x65,0x30,0x39 }
//GP Link Key
#define GP_LINK_KEY                                     DEFAULT_TC_LINK_KEY

//A.3.6.1.2.1, the time the GP endpoint of the sink and the proxy keeps the information on the received GPDF, in order to filter out duplicates.
#define GP_DUPLICATE_TIMEOUT_DEFAULT                    2//2s

//A.3.6.3.2
#define GPP_COMMISSIONING_WINDOW_DEFAULT                180//180s

//A.3.6.2.4
#define GPS_MULTI_SENSOR_COMMISSIONING_TIMEOUT          20//20s

//A.3.9.1, step 8, time that the basic proxy device will be absent of the operational network due to bidirectional commissioning.
#define GP_TRANSMIT_CHANNEL_TIMEOUT                     5//5s


//GPD Device IDs
#define GPD_DEV_ID_SIMPLE_1STATE_SWITCH                 0x00
#define GPD_DEV_ID_SIMPLE_2STATE_SWITCH                 0x01
#define GPD_DEV_ID_ONOFF_SWITCH                         0x02
#define GPD_DEV_ID_LEVEL_CONTROL_SWITCH                 0x03
#define GPD_DEV_ID_GENERIC_8_CONTACT_SWITCH             0x07
#define GPD_DEV_ID_TEMPERATURE_SENSOR                   0x30
#define GPD_DEV_ID_NOT_SPECIFIC_FUNCTION                0xFE

//GPD Command IDs, A.4.1
#define GPDF_CMD_ID_ONOFF_OFF                           0x20
#define GPDF_CMD_ID_ONOFF_ON                            0x21
#define GPDF_CMD_ID_ONOFF_TOGGLE                        0x22
#define GPDF_CMD_ID_LEVEL_MOVE_UP                       0x30
#define GPDF_CMD_ID_LEVEL_MOVE_DOWN                     0x31
#define GPDF_CMD_ID_LEVEL_STEP_UP                       0x32
#define GPDF_CMD_ID_LEVEL_STEP_DOWN                     0x33
#define GPDF_CMD_ID_LEVEL_STOP                          0x34
#define GPDF_CMD_ID_LEVEL_MOVE_UP_WITH_ONOFF            0x35
#define GPDF_CMD_ID_LEVEL_MOVE_DOWN_WITH_ONOFF          0x36
#define GPDF_CMD_ID_LEVEL_STEP_UP_WITH_ONOFF            0x37
#define GPDF_CMD_ID_LEVEL_STEP_DOWN_WITH_ONOFF          0x38

#define GPDF_CMD_ID_8BIT_VECTOR_PRESS                   0x69
#define GPDF_CMD_ID_8BIT_VECTOR_RELEASE                 0x6A

#define GPDF_CMD_ID_ATTR_REPORT                         0xA0
#define GPDF_CMD_ID_MANU_ATTR_REPORT                    0xA1
#define GPDF_CMD_ID_MULTI_CLUSTER_REPORT                0xA2
#define GPDF_CMD_ID_MANU_MULTI_CLUSTER_REPORT           0xA3
#define GPDF_CMD_ID_ZCL_TUNNELING                       0xA6
#define GPDF_CMD_ID_COMPACT_ATTR_REPORT                 0xA8
#define GPDF_CMD_ID_ANY_SENSOR_REPORT_COMMAND           0xAF

#define GPDF_CMD_ID_COMMISSIONING                       0xE0
#define GPDF_CMD_ID_DECOMMISSIONING                     0xE1
#define GPDF_CMD_ID_SUCCESS                             0xE2
#define GPDF_CMD_ID_CHANNEL_REQ                         0xE3
#define GPDF_CMD_ID_APP_DESCRIPTION                     0xE4
#define GPDF_CMD_ID_COMMISSIONING_REPLY                 0xF0
#define GPDF_CMD_ID_CHANNEL_CONFIGURATION               0xF3


//GPP functionality
/************************************************************************************************************
b0    ||  GP feature                                                                              ||    0b1
b1    ||  Direct communication (reception of GPDF via GP stub)                                    ||    0b1
b2    ||  Derived groupcast communication                                                         ||    0b1
b3    ||  Pre-commissioned groupcast communication                                                ||    0b1
b4    ||  Full unicast communication                                                              ||    0b0
b5    ||  Lightweight unicast communication                                                       ||    0b1
b6    ||  Reserved                                                                                ||    0b0
b7    ||  Bidirectional operation                                                                 ||    0b0
b8    ||  Proxy Table maintenance (active and passive, for GPD mobility and GPP robustness)       ||    0b0
b9    ||  Reserved                                                                                ||    0b0
b10   ||  GP commissioning                                                                        ||    0b1
b11   ||  CT-based commissioning                                                                  ||    0b1
b12   ||  Maintenance of GPD (deliver channel/key during operation)                               ||    0b0
b13   ||  gpdSecurityLevel = 0b00                                                                 ||    0b1
b14   ||  Deprecated: gpdSecurityLevel = 0b01                                                     ||    0b0
b15   ||  gpdSecurityLevel = 0b10                                                                 ||    0b1
b16   ||  gpdSecurityLevel = 0b11                                                                 ||    0b1
b17   ||  Reserved                                                                                ||    0b0
b18   ||  Reserved                                                                                ||    0b0
b19   ||  GPD IEEE address                                                                        ||    0b1
b20   ||  Reserved                                                                                ||    0b0
      ||  b21 - b23 Reserved                                                                      ||    0b0
************************************************************************************************************/
#define GPP_FUNCTIONALITY                               0x09AC2F  // Table 42 - Format of the gppFunctionality attribute

//GPS functionality
/************************************************************************************************************
b0    ||  GP feature                                                                              ||    0b1
b1    ||  Direct communication (reception of GPDF via GP stub)                                    ||    0b1
b2    ||  Derived groupcast communication                                                         ||    0b1
b3    ||  Pre-commissioned groupcast communication                                                ||    0b1
b4    ||  Full unicast communication                                                              ||    0b0
b5    ||  Lightweight unicast communication                                                       ||    0b1
b6    ||  Proximity bidirectional operation                                                       ||    0b0
b7    ||  Multi-hop bidirectional operation                                                       ||    0b0
b8    ||  Proxy Table maintenance (active and passive, for GPD mobility and GPP robustness)       ||    0b0
b9    ||  Proximity commissioning (unidirectional and bidirectional)                              ||    0b1
b10   ||  Multi-hop commissioning (unidirectional and bidirectional)                              ||    0b1
b11   ||  CT-based commissioning                                                                  ||    0b1
b12   ||  Maintenance of GPD (deliver channel/key during operation)                               ||    0b0
b13   ||  gpdSecurityLevel = 0b00 in operation                                                    ||    0b1
b14   ||  Deprecated: gpdSecurityLevel = 0b01                                                     ||    0b0
b15   ||  gpdSecurityLevel = 0b10                                                                 ||    0b1
b16   ||  gpdSecurityLevel = 0b11                                                                 ||    0b1
b17   ||  Sink Table-based groupcast forwarding                                                   ||    0b0
b18   ||  Translation Table                                                                       ||    0b0
b19   ||  GPD IEEE address                                                                        ||    0b1
b20	  ||  Compact attribute reporting                                                         ||    0b0
      ||  b21 - b23 Reserved                                                                      ||    0b0
************************************************************************************************************/
#define GPS_FUNCTIONALITY                               0x09AE2F  // Table 28 - Format of the gpsFunctionality attribute

//GGP active functionality, A.3.4.2.8
#define GPP_ACTIVE_FUNCTIONALITY                        0xFFFFFF
#define GPS_ACTIVE_FUNCTIONALITY                        0xFFFFFF



/***************************************************************************
*  @brief	Define for GP Channel request command
*/
typedef union {
    uint8_t chTogglingBehavior;
    struct {
        uint8_t rxChNext:4;
        uint8_t rxChSecondNext:4;
    } bits;
} gpd_channelRequestCmd_payload_t;

/***************************************************************************
*  @brief	Define for GP Response
*/
typedef union {
    uint8_t channel;
    struct {
        uint8_t operationChannel:4;
        uint8_t basic:1;
        uint8_t reserved:3;
    } bits;
} gpd_channelConfigurationCmd_payload_t;

/***************************************************************************
* @brief	Define for commissioning command payload
*/
typedef union {
    uint8_t opts;
    struct {
        uint8_t macSeqNumCap:1;
        uint8_t rxOnCap:1;
        uint8_t appInfoPresent:1;
        uint8_t reserved:1;
        uint8_t panIdReq:1;
        uint8_t gpSecKeyReq:1;
        uint8_t fixedLocation:1;
        uint8_t extOpPresent:1;
    } bits;
} gpCommissioningOpt_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t secLevelCap:2;
        uint8_t keyType:3;
        uint8_t gpdKeyPresent:1;
        uint8_t gpdKeyEncrypt:1;
        uint8_t outgoingCounterPresent:1;
    } bits;
} gpCommissioningExtOpt_t;

typedef struct {
    uint8_t *commandList;
    uint8_t *clusterList;
    uint8_t payloadLen;
    uint8_t deviceId;
    gpCommissioningOpt_t options;
    gpCommissioningExtOpt_t extOptions;
    uint8_t gpdSecKey[SEC_KEY_LEN];
    uint32_t gpdMIC;
    uint32_t gpdOutGoingCounter;
    uint16_t manuId;
    uint16_t modelId;
    gpSwitchInfo_t switchInfo;
    gpAppInfo_t appInfo;
    uint8_t gpdCmdNum;
} gpd_commissioning_payload_t;

/***************************************************************************
*  @brief	Define for GP commissioning reply
*/
typedef union {
    uint8_t opts;
    struct {
        uint8_t panIdPresent:1;
        uint8_t gpdSecKeyPresent:1;
        uint8_t gpdKeyEncrypt:1;
        uint8_t securityLevel:2;
        uint8_t  keyType:3;
    } bits;
} gpCommissioningReplyOpt_t;

typedef struct {
    uint8_t  key[16];
    uint32_t keyMIC;
    uint32_t frameCounter;
    uint16_t panId;
    gpCommissioningReplyOpt_t options;
} gpd_commissioningReply_payload_t;

/***************************************************************************
*  @brief	Define for GP Application Description
*/
typedef union {
    uint8_t opts;
    struct {
        uint8_t timeoutPeriodPresent:1;
        uint8_t resv:7;
    } bits;
} gpReportOpt_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t numOfRecords:3;
        uint8_t side:1;
        uint8_t manuIdPresent:1;
        uint8_t resv:3;
    } bits;
} gpDataPointOpt_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t remainingLen:4;
        uint8_t reported:1;
        uint8_t attrValuePresent:1;
        uint8_t resv:2;
    } bits;
} gpAttrOpt_t;

typedef struct {
    uint16_t attrId;
    uint8_t attrDataType;
    gpAttrOpt_t attrOption;
    uint8_t attrOffsetWithinReport;
    uint8_t attrValue[8];
} gpAttrRecord_t;

typedef struct {
    uint16_t clusterId;
    uint16_t manuId;
    gpDataPointOpt_t dataPointOpt;
    gpAttrRecord_t attrRecord[];
} gpDataPointDesc_t;

typedef struct {
    uint16_t timeoutPeriod;
    uint8_t reportId;
    gpReportOpt_t reportOpt;
    uint8_t remainingLen;
    gpDataPointDesc_t dataPointDesc[];
} gpReportDesc_t;

typedef struct {
    uint8_t totalNumReport;
    uint8_t numOfReport;
    gpReportDesc_t reportDesc[];
} gpd_appDesc_payload_t;

/***************************************************************************
*  @brief	Define for GP Data
*/
typedef struct {
    //TODO
} gpd_data_payload_t;

/***************************************************************************
* @brief	Define for GP (Commissioning) Notification
*/
typedef struct {
    epInfo_t dstEpInfo;
    union {
        zcl_gp_notificationCmd_t gpNotificationCmd;
        zcl_gp_commissioningNotificationCmd_t gpCommissioningNotificationCmd;
    };
} gp_noti_t;

/***************************************************************************
* @brief	Define for APP layer
*/
typedef bool (*gpChangleChannelReqCb_t)(void);
typedef void (*gpCommissioningModeCb_t)(bool isInCommMode);

typedef struct {
    gpChangleChannelReqCb_t gpChangleChannelReqCb;
    gpCommissioningModeCb_t gpCommissioningModeCb;
} gp_appCb_t;


/**
 *  @brief  External variable for GP Attribute
 */
extern uint8_t zclGpAttr_gpsMaxSinkTabEntries;
extern uint8_t zclGpAttr_sinkTabEntry[];
extern uint8_t zclGpAttr_gpsCommMode;
extern uint8_t zclGpAttr_gpsCommExitMode;
extern uint16_t zclGpAttr_gpsCommWindow;
extern uint8_t zclGpAttr_gpsSecLevel;
extern uint8_t zclGpAttr_gpsFunc[];
extern uint8_t zclGpAttr_gpsActiveFunc[];
extern uint8_t zclGpAttr_gppMaxProxyTabEntries;
extern uint16_t zclGpAttr_clusterRev;
extern uint8_t zclGpAttr_proxyTabEntry[];
extern uint8_t zclGpAttr_gppFunc[];
extern uint8_t zclGpAttr_gppActiveFunc[];
extern uint8_t zclGpAttr_gpSharedSecKeyType;
extern uint8_t zclGpAttr_gpSharedSecKey[];
extern uint8_t zclGpAttr_gpLinkKey[];

extern uint8_t GP_CB_CLUSTER_NUM;
extern const zcl_specClusterInfo_t g_gpClusterList[];
extern const af_simple_descriptor_t gp_simpleDesc;
extern gp_appCb_t *gpAppCb;


void gpDataCnfPrc(gp_data_cnf_t *pGpDataCnf);
void gpDataIndPrc(void *arg);
void gpSecReqPrc(void *arg);

void gp_init(uint8_t endpoint);
void gp_registerAppCb(gp_appCb_t *cb);
status_t zcl_gpCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);

bool gpDevAnnceCheckCb(uint16_t ailasNwkAddr, addrExt_t ailasIeeeAddr);
bool gpChangleChannelReqCb(void);
void gpCommissioningModeCb(bool isInCommMode);
void gpCommissioningNotificationCmdFromLocalCb(zcl_gp_commissioningNotificationCmd_t *pCmd);

void gpsCommissionModeInvork(void);

#endif	/* GP_H */

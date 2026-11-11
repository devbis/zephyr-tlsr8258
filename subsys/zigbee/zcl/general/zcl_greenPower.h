/********************************************************************************************************
 * @file    zcl_greenPower.h
 *
 * @brief   This is the header file for zcl_greenPower
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
#ifndef ZCL_GREEN_POWER_H
#define ZCL_GREEN_POWER_H


#define GP_CLUSTER_REVISION                             0x0002

/**
 *  @brief	List of GP infrastructure devices
 */
#define GP_DEVICE_ID_PROXY                              0x0060
#define GP_DEVICE_ID_PROXY_BASIC                        0x0061
#define GP_DEVICE_ID_TARGET_PLUS                        0x0062
#define GP_DEVICE_ID_TARGET                             0x0063
#define GP_DEVICE_ID_COMMISSIONING_TOOL                 0x0064
#define GP_DEVICE_ID_COMBO                              0x0065
#define GP_DEVICE_ID_COMBO_BASIC                        0x0066

/**
 *  @brief	zcl_gp_attrId Attribute Ids
 */
//Server attributes
#define ZCL_ATTRID_GP_GPS_MAX_SINK_TABLE_ENTRIES        0x0000
#define ZCL_ATTRID_GP_SINK_TABLE                        0x0001
#define ZCL_ATTRID_GP_GPS_COMMUNICATION_MODE            0x0002
#define ZCL_ATTRID_GP_GPS_COMMISSIONING_EXIT_MODE       0x0003
#define ZCL_ATTRID_GP_GPS_COMMISSIONING_WINDOW          0x0004
#define ZCL_ATTRID_GP_GPS_SECURITY_LEVEL                0x0005
#define ZCL_ATTRID_GP_GPS_FUNCTIONALITY                 0x0006
#define ZCL_ATTRID_GP_GPS_ACTIVE_FUNCTIONALITY          0x0007
//Client attributes
#define ZCL_ATTRID_GP_GPP_MAX_PROXY_TABLE_ENTRIES       0x0010
#define ZCL_ATTRID_GP_PROXY_TABLE                       0x0011
#define ZCL_ATTRID_GP_GPP_NOTIFICATION_RETRY_NUMBER     0x0012
#define ZCL_ATTRID_GP_GPP_NOTIFICATION_RETRY_TIMER      0x0013
#define ZCL_ATTRID_GP_GPP_MAX_SEARCH_COUNTER            0x0014
#define ZCL_ATTRID_GP_GPP_BLOCKED_GPD_ID                0x0015
#define ZCL_ATTRID_GP_GPP_FUNCTIONALITY                 0x0016
#define ZCL_ATTRID_GP_GPP_ACTIVE_FUNCTIONALITY          0x0017
//Attributes shared by client and server
#define ZCL_ATTRID_GP_SHARED_SECURITY_KEY_TYPE          0x0020
#define ZCL_ATTRID_GP_SHARED_SECURITY_KEY               0x0021
#define ZCL_ATTRID_GP_LINK_KEY                          0x0022
#define ZCL_ATTRID_GP_CLUSTER_REVISION                  0xfffd

/**
 *  @brief	zcl_gp_cmdId Command Ids
 */
//Server side commands received or client side commands generated
#define ZCL_CMD_GP_NOTIFICATION                         0x00
#define ZCL_CMD_GP_PAIRING_SEARCH                       0x01
#define ZCL_CMD_GP_TUNNELING_STOP                       0x03
#define ZCL_CMD_GP_COMMISSIONING_NOTIFICATION           0x04
#define ZCL_CMD_GP_SINK_COMMISSIONING_MODE              0x05
#define ZCL_CMD_GP_TRANSLATION_TABLE_UPDATE_COMMAND     0x07
#define ZCL_CMD_GP_TRANSLATION_TABLE_REQUEST            0x08
#define ZCL_CMD_GP_PAIRING_CONFIGURATION                0x09
#define ZCL_CMD_GP_SINK_TABLE_REQUEST                   0x0a
#define ZCL_CMD_GP_PROXY_TABLE_RESPONSE                 0x0b
//Server side commands generated or client side commands received
#define ZCL_CMD_GP_NOTIFICATION_RESPONSE                0x00
#define ZCL_CMD_GP_PAIRING                              0x01
#define ZCL_CMD_GP_PROXY_COMMISSIONING_MODE             0x02
#define ZCL_CMD_GP_RESPONSE                             0x06
#define ZCL_CMD_GP_TRANSLATION_TABLE_RESPONSE           0x08
#define ZCL_CMD_GP_SINK_TABLE_RESPONSE                  0x0a
#define ZCL_CMD_GP_PROXY_TABLE_REQUEST                  0x0b


/**
 *  @brief	gpsCommunicationMode attribute
 */
typedef enum {
    GPS_COMM_MODE_FULL_UNICAST,
    GPS_COMM_MODE_GROUP_DGROUPID,
    GPS_COMM_MODE_GROUP_PRE_COMMISSIONED_GROUPID,
    GPS_COMM_MODE_LIGHTWEIGHT_UNICAST,
} gpsCommMode_t;

/**
 *  @brief	gpsCommunicationExitMode attribute
 */
typedef enum {
    GPS_EXIT_ON_COMM_WINDOW_EXPIRATION                  = BIT(0),
    GPS_EXIT_ON_FIRST_PAIRING_SUCCESS                   = BIT(1),
    GPS_EXIT_ON_GP_PROXY_COMM_MODE_EXIT                 = BIT(2),
} gpsCommExitMode_t;

/**
 *  @brief	gpsSecurityLevel attribute
 */
typedef enum {
    GPS_MINIMAL_GPD_SEC_LEVEL_NO_SEC,
    GPS_MINIMAL_GPD_SEC_LEVEL_4B_FC_MIC                 = 2,
    GPS_MINIMAL_GPD_SEC_LEVEL_4B_FC_MIC_ENC             = 3,
    GPS_PROTECTION_WITH_GPLINKKEY                       = BIT(2),
    GPS_INVOLVE_TC                                      = BIT(3),
} gpsSecLevel_t;


/******************************************************************
 ******************** Format of command ***************************
 *****************************************************************/
typedef union {
    uint8_t link;
    struct {
        uint8_t rssi:6;
        uint8_t lqi:2;
    } bits;
} gpGppGpdLink_t;

typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t alsoUnicast:1;
        uint16_t alsoDerivedGroup:1;
        uint16_t alsoCommGroup:1;
        uint16_t secLevel:2;
        uint16_t secKeyType:3;
        uint16_t rxAfterTx:1;
        uint16_t gpTxQueueFull:1;
        uint16_t bidirectionalCap:1;
        uint16_t proxyInfoPresent:1;
        uint16_t reserved:1;
    } bits;
} gpNotiOpt_t;

/**
 *  @brief	Format of the GP Notification command
 */
typedef struct {
    gpdId_t	gpdId;
    uint8_t *pGpdCmdPayload;
    uint32_t gpdSecFrameCnt;
    gpNotiOpt_t options;
    uint16_t gppShortAddr;
    uint8_t endpoint;
    uint8_t gpdCmdID;
    uint8_t payloadLen;
    gpGppGpdLink_t gppGpdLink;
} zcl_gp_notificationCmd_t;

typedef union {
    uint32_t opts;
    struct {
        uint32_t appId:3;
        uint32_t addSink:1;
        uint32_t removeGPD:1;
        uint32_t commMode:2;
        uint32_t gpdFixed:1;
        uint32_t gpdMacSeqNumCap:1;
        uint32_t secLevel:2;
        uint32_t secKeyType:3;
        uint32_t gpdSecFrameCntPresent:1;
        uint32_t gpdSecKeyPresent:1;
        uint32_t assignedAliasPresent:1;
        uint32_t groupcastRadiusPresent:1;
        uint32_t reserved:6;
    } bits;
} gpPairingOpt_t;

/**
 *  @brief	Format of the GP Pairing command
 */
typedef struct {
    gpdId_t	gpdId;
    addrExt_t sinkIeeeAddr;
    gpPairingOpt_t options;
    uint32_t gpdSecFrameCnt;
    uint8_t gpdKey[16];
    uint16_t sinkNwkAddr;
    uint16_t sinkGroupID;
    uint16_t assignedAlias;
    uint8_t endpoint;
    uint8_t deviceID;
    uint8_t groupcastRadius;
} zcl_gp_pairingCmd_t;

enum {
    EXIT_ON_FIRST_PAIRING_SUCCESS                       = BIT(0),
    EXIT_ON_GP_PROXY_COMMISSIONING_MODE	                = BIT(1),
};

typedef union {
    uint8_t opts;
    struct {
        uint8_t action:1;
        uint8_t commWindowPresent:1;
        uint8_t exitMode:2;
        uint8_t channelPresent:1;
        uint8_t unicastCommunication:1;
        uint8_t reserved:2;
    } bits;
} gpProxyCommModeOpt_t;

/**
 *  @brief	Format of the GP Proxy Commissioning Mode command
 */
typedef struct {
    gpProxyCommModeOpt_t options;
    uint8_t channel;
    uint16_t commissioningWindow;
} zcl_gp_proxyCommissioningModeCmd_t;

typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t rxAfterTx:1;
        uint16_t secLevel:2;         //0x00,0x02,0x03
        uint16_t secKeyType:3;       //0x00 - 0x07
        uint16_t secPrcFailed:1;
        uint16_t bidirectionalCap:1;
        uint16_t proxyInfoPresent:1;
        uint16_t reserved:4;
    } bits;
} gpCommNotiOpt_t;

/**
 *  @brief	Format of the GP Commissioning Notification command
 */
typedef struct {
    gpdId_t gpdId;
    uint8_t *pGpdCmdPayload;
    uint32_t gpdSecFrameCnt;
    uint32_t mic;
    gpCommNotiOpt_t options;
    uint16_t gppShortAddr;
    uint8_t endpoint;
    uint8_t gpdCmdID;
    uint8_t payloadLen;
    gpGppGpdLink_t gppGpdLink;
} zcl_gp_commissioningNotificationCmd_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t appId:3;
        uint8_t transmitOnEndpointMatch:1;
        uint8_t reserved:4;
    } bits;
} gpResponseOpt_t;

/**
 *  @brief	Format of the GP Response command
 */
typedef struct {
    gpdId_t gpdId;
    uint8_t *pGpdCmdPayload;
    uint16_t tempMasterShortAddr;
    gpResponseOpt_t options;
    uint8_t tempMasterTxChannel;
    uint8_t endpoint;
    uint8_t gpdCmdID;
    uint8_t payloadLen;
} zcl_gp_responseCmd_t;

enum {
    ACTION_ADD_TRANS_TABLE_ENTRY,
    ACTION_REPLACE_TRANS_TABLE_ENTRY,
    ACTION_REMOVE_TRANS_TABLE_ENTRY,
};

typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t action:2;
        uint16_t numOfTrans:3;
        uint16_t addInfoPresent:1;
        uint16_t reserved:7;
    } bits;
} transUpdateOpt_t;

typedef struct {
    uint8_t *pZbCmdPayload;
    uint8_t *pAdditionalInfo;
    uint16_t profile;
    uint16_t cluster;
    uint8_t index;
    uint8_t gpdCmdId;
    uint8_t endpoint;
    uint8_t zbCmdId;
    uint8_t zbCmdPayloadLen;
    uint8_t addInfoLen;
} gpTranslation_t;

/**
 *  @brief	Format of the GP Translation Table Update command
 */
typedef struct {
    gpdId_t gpdId;
    uint8_t *pBuf;//buffer point of translation
    transUpdateOpt_t options;
    uint8_t endpoint;
    uint8_t bufLen;//length of translation buffer
} zcl_gp_transTabUpdateCmd_t;

/**
 *  @brief	Format of the GP Translation Table Request command
 */
typedef struct {
    uint8_t startIdx;
} zcl_gp_transTabReqCmd_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t appId:3;
        uint8_t addInfoPresent:1;
        uint8_t reserved:4;
    } bits;
} transOpt_t;

typedef struct {
    gpdId_t gpdId;
    uint16_t profile;
    uint16_t cluster;
    uint8_t gpdEndpoint;
    uint8_t gpdCmdId;
    uint8_t endpoint;
    uint8_t zbCmdId;
    uint8_t zbCmdPayloadLen;
    uint8_t *pZbPayloadAndAdditionalInfo;
} gpTransTabList_t;

/**
 *  @brief	Format of the GP Translation Table Response command
 */
typedef struct {
    uint8_t *pBuf;//buffer point to the translation table list
    uint8_t bufLen;//length of translation table list buffer
    uint8_t status;
    transOpt_t options;
    uint8_t totalNumOfEntries;
    uint8_t startIdx;
    uint8_t entriesCnt;
} zcl_gp_transTabRspCmd_t;

/**
 *  @brief	Action sub-field of the actions field
 */
enum {
    NO_ACTION,
    EXTEND_SINK_TABLE_ENTRY,
    REPLACE_SINK_TABLE_ENTRY,
    REMOVE_A_PAIRING,
    REMOVE_GPD,
    APPLICATION_DESCRIPTION,
};

typedef union {
    uint8_t acts;
    struct {
        uint8_t action:3;
        uint8_t sendGpPairing:1;
        uint8_t reserved:4;
    } bits;
} gpPairingCfgActions_t;

typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t commMode:2;
        uint16_t seqNumCap:1;
        uint16_t rxOnCap:1;
        uint16_t fixedLocation:1;
        uint16_t assignedAlias:1;
        uint16_t secUse:1;
        uint16_t appInfoPresent:1;
        uint16_t reserved:5;
    } bits;
} gpPairingCfgOpt_t;

typedef union {
    uint8_t info;
    struct {
        uint8_t manuIdPresent:1;
        uint8_t modelIdPresent:1;
        uint8_t gpdCmdsPresent:1;
        uint8_t clusterListPresent:1;
        uint8_t switchInfoPresent:1;
        uint8_t gpdAppDescCmdFollows:1;
        uint8_t reserved:2;
    } bits;
} gpAppInfo_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t secLevel:2;          //0x00,0x02,0x03
        uint8_t secKeyType:3;        //0x00 - 0x07
        uint8_t reserved:3;
    } bits;
} gpSecOpt_t;

typedef struct {
    uint16_t groupId;
    uint16_t alias;
} gpSinkGroupListItem_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t sClusterNum:4;
        uint8_t cClusterNum:4;
    } bits;
} gpClusterId_t;

typedef struct {
    uint8_t *sClusterList;
    uint8_t *cClusterList;
    gpClusterId_t clusterID;
} gpCommCluster_t;

typedef union {
    uint8_t cfg;
    struct {
        uint8_t contactNum:4;
        uint8_t switchType:2;
        uint8_t reserved:2;
    } bits;
} gpGenericSwCfg_t;

typedef struct {
    uint8_t switchInfoLen;
    gpGenericSwCfg_t switchCfg;
    uint8_t contactStatus;
} gpSwitchInfo_t;

/**
 *  @brief	Format of the GP Pairing Configuration command
 */
typedef struct {
    uint8_t *pGroupList;
    uint8_t *pPairedEndpoints;
    uint8_t *pGpdCmdList;
    uint8_t *pClusterList;
    uint8_t *pReportDescriptor;
    uint32_t gpdSecFrameCnt;
    uint8_t gpdSecKey[SEC_KEY_LEN];
    gpdId_t gpdId;
    gpPairingCfgOpt_t options;
    uint16_t gpdAssignedAlias;
    uint16_t manufacturerId;
    uint16_t modelId;
    gpPairingCfgActions_t actions;
    gpAppInfo_t appInfo;
    uint8_t endpoint;
    uint8_t deviceId;
    uint8_t groupcastRadius;
    gpSecOpt_t secOptions;
    uint8_t numOfPairedEndpoints;
    gpSwitchInfo_t switchInfo;
    uint8_t numOfGpdCmds;
    uint8_t reportDescLen;
} zcl_gp_pairingConfigurationCmd_t;

/**
 *  @brief	Request type sub-field of the options
 */
enum {
    REQUEST_TABLE_ENTRIES_BY_GPD_ID,
    REQUEST_TABLE_ENTRIES_BY_INDEX,
};

typedef union {
    uint8_t opts;
    struct {
        uint8_t appId:3;
        uint8_t reqType:2;
        uint8_t reserved:3;
    } bits;
} gpSinkProxyTabReqOpt_t;

/**
 *  @brief	Format of the GP Sink Table Request command
 */
typedef struct {
    gpdId_t gpdId;
    gpSinkProxyTabReqOpt_t options;
    uint8_t endpoint;
    uint8_t index;
} zcl_gp_sinkTabReqCmd_t;

/**
 *  @brief	Format of the GP Sink Table Response command
 */
typedef struct {
    uint8_t *sinkTabEntry;   //buffer pointer of sink table entries field
    uint8_t entriesLen;      //buffer length of sink table entries
    uint8_t status;
    uint8_t totalTabEntries;
    uint8_t startIdx;
    uint8_t entriesCnt;
} zcl_gp_sinkTabRspCmd_t;

/**
 *  @brief	Format of the GP Proxy Table Request command
 */
typedef struct {
    gpdId_t gpdId;
    gpSinkProxyTabReqOpt_t options;
    uint8_t endpoint;
    uint8_t index;
} zcl_gp_proxyTabReqCmd_t;

/**
 *  @brief	Format of the GP Proxy Table Response command
 */
typedef struct {
    uint8_t *proxyTabEntry;
    uint8_t entriesLen;
    uint8_t status;
    uint8_t totalTabEntries;
    uint8_t startIdx;
    uint8_t entriesCnt;
} zcl_gp_proxyTabRspCmd_t;

typedef union {
    uint8_t opts;
    struct {
        uint8_t action:1;
        uint8_t involveGpmInSec:1;
        uint8_t involveGpmInPairing:1;
        uint8_t involveProxies:1;
        uint8_t reserved:4;
    } bits;
} gpSinkCommModeOpt_t;

/**
 *  @brief	Format of the GP Sink Commissioning Mode command
 */
typedef struct {
    uint16_t gpmAddrForSec;
    uint16_t gpmAddrForPairing;
    gpSinkCommModeOpt_t options;
    uint8_t sinkEndpoint;
} zcl_gp_sinkCommissioningModeCmd_t;

#if 0
typedef void (*zcl_gp_PairingCb_t)(apsdeDataInd_t *pInd, zcl_gp_pairingCmd_t *pCmd);
typedef void (*zcl_gp_proxyTabReqCb_t)(apsdeDataInd_t *pInd, zcl_gp_proxyTabReqCmd_t *pCmd);
typedef void (*zcl_gp_proxyCommModeCb_t)(apsdeDataInd_t *pInd, zcl_gp_proxyCommissioningModeCmd_t *pCmd);
typedef void (*zcl_gp_rspCb_t)(apsdeDataInd_t *pInd, zcl_gp_responseCmd_t *pCmd);

/**
 *  @brief  Structure definition for commands callBack functions in GP
 */
typedef struct {
    zcl_gp_PairingCb_t gpPairingCbFunc;
    zcl_gp_proxyTabReqCb_t gpProxtTabReqCbFunc;
    zcl_gp_proxyCommModeCb_t gpProxyCommModeCbFunc;
    zcl_gp_rspCb_t gpRspCbFunc;
} zcl_gp_AppCallbacks_t;

/**
 *  @brief  External variable for GP Attribute
 */
extern const uint8_t zclGpAttr_gppMaxProxyTabEntries;
extern const uint16_t zclGpAttr_clusterRev;
extern uint8_t zclGpAttr_proxyTabEntry[];
#if 0
extern uint8_t zclGpAttr_gppNotificationRetryNum;
extern uint8_t zclGpAttr_gppNotificationTimer;
extern uint8_t zclGpAttr_gppMaxSearchCnt;
extern uint8_t *pZclGpAttr_gppBlockedGPDID;
#endif
extern uint8_t zclGpAttr_gppFunc[];
extern uint8_t zclGpAttr_gppActiveFunc[];

extern uint8_t zclGpAttr_gpSharedSecKeyType;
extern uint8_t zclGpAttr_gpSharedSecKey[];
extern uint8_t zclGpAttr_gpLinkKey[];



/**
 *  @brief  External variable for GP Attribute info table
 */
extern const zclAttrInfo_t gp_attrTbl[];

extern const uint8_t zcl_gp_attrNum;
#endif

status_t zcl_gp_register(uint8_t endpoint, uint16_t manuCode, uint8_t attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

status_t zcl_gp_notificationCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_notificationCmd_t *pCmd);
#define zcl_gp_notificationCmdSend(a,b,c,d)                     (zcl_gp_notificationCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_commissioningNotificationCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_commissioningNotificationCmd_t *pCmd);
#define zcl_gp_commissioningNotificationCmdSend(a,b,c,d)        (zcl_gp_commissioningNotificationCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_sinkTabReqCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_sinkTabReqCmd_t *pCmd);
#define zcl_gp_sinkTabReqCmdSend(a,b,c,d)                       (zcl_gp_sinkTabReqCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_proxyTableRspCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_proxyTabRspCmd_t *pCmd);
#define zcl_gp_proxyTableRspCmdSend(a,b,c,d,e)                  (zcl_gp_proxyTableRspCmd((a), (b), (c), (d), (e)))

status_t zcl_gp_pairingCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_pairingCmd_t *pCmd);
#define zcl_gp_pairingCmdSend(a,b,c,d)                          (zcl_gp_pairingCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_proxyCommissioningModeCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_proxyCommissioningModeCmd_t *pCmd);
#define zcl_gp_proxyCommissioningModeCmdSend(a,b,c,d)           (zcl_gp_proxyCommissioningModeCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_responseCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_responseCmd_t *pCmd);
#define zcl_gp_responseCmdSend(a,b,c,d)                         (zcl_gp_responseCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_sinkTableRspCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_sinkTabRspCmd_t *pCmd);
#define zcl_gp_sinkTableRspCmdSend(a,b,c,d,e)                   (zcl_gp_sinkTableRspCmd((a), (b), (c), (d), (e)))

status_t zcl_gp_transTableRspCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_transTabRspCmd_t *pCmd);
#define zcl_gp_transTableRspCmdSend(a,b,c,d,e)                  (zcl_gp_transTableRspCmd((a), (b), (c), (d), (e)))

status_t zcl_gp_proxyTableReqCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_proxyTabReqCmd_t *pCmd);
#define zcl_gp_proxyTableReqCmdSend(a,b,c,d)                    (zcl_gp_proxyTableReqCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_sinkCommissioningModeCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_sinkCommissioningModeCmd_t *pCmd);
#define zcl_gp_sinkCommissioningModeCmdSend(a,b,c,d)            (zcl_gp_sinkCommissioningModeCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

status_t zcl_gp_pairingCfgCmd(uint8_t srcEp, epInfo_t *pDstEpInfo, uint8_t disableDefaultRsp, uint8_t seqNo, zcl_gp_pairingConfigurationCmd_t *pCmd);
#define zcl_gp_pairingCfgCmdSend(a,b,c,d)                       (zcl_gp_pairingCfgCmd((a), (b), (c), ZCL_SEQ_NUM, (d)))

#endif	/* ZCL_GREEN_POWER_H */

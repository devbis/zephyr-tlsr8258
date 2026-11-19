/********************************************************************************************************
 * @file    gp_proxy.h
 *
 * @brief   This is the header file for gp_proxy
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
#ifndef GP_PROXY_H
#define GP_PROXY_H

//Max proxy table entries.
#define GPP_MAX_PROXY_TABLE_ENTRIES                     5

#define PROXY_LIGHTWEIGHT_SINK_ADDR_LIST_NUM            2
#define PROXY_SINK_GROUP_LIST_NUM                       2
#define PROXY_FULL_UNICAST_SINK_ADDR_LIST_NUM           2

#define ZCL_GP_MAX_PROXY_TABLE_ATTR_LEN                 75

/**
 *  @brief	Proxy entry options
 */
typedef union {
    uint16_t opts;
    struct {
        uint16_t appId:3;
        uint16_t entryActive:1;
        uint16_t entryValid:1;
        uint16_t seqNumCap:1;
        uint16_t lightWeightUnicastGPS:1;
        uint16_t derivedGroupGPS:1;
        uint16_t commGroupGPS:1;
        uint16_t firstToForward:1;
        uint16_t inRange:1;
        uint16_t gpdFixed:1;
        uint16_t hasAllUnicastRoutes:1;
        uint16_t assignedAlias:1;
        uint16_t secUse:1;
        uint16_t optsExtension:1;
    } bits;
} gpProxyEntryOpt_t;

typedef struct {
    addrExt_t sinkIeeeAddr;
    uint16_t sinkNwkAddr;
} gpLwSinkAddrListItem_t;

typedef struct {
    uint16_t fullUnicastGPS:1;
    uint16_t reserved:15;
} gpExtendedOpt_t;

/**
 *  @brief	Proxy table entry
 */
typedef struct _attribute_packed_ {
    gpdId_t gpdId;              //8-bytes
    gpProxyEntryOpt_t options;
    uint16_t gpdAssignedAlias;
    gpSecOpt_t secOptions;
    uint8_t endpoint;
    uint8_t groupcastRadius;
    uint8_t searchCnt;               //8-bytes
    uint8_t gpdKey[16];              //16-bytes
    uint32_t gpdSecFrameCnt;         //4-bytes
    gpLwSinkAddrListItem_t lightweightSinkAddrList[PROXY_LIGHTWEIGHT_SINK_ADDR_LIST_NUM]; //20-bytes
    gpSinkGroupListItem_t sinkGroupList[PROXY_SINK_GROUP_LIST_NUM];                       //8-bytes
    uint8_t used:1;
    uint8_t lwSinkCnt:2;
    uint8_t sinkGroupCnt:2;
    uint8_t reserved:3;
} gpProxyTabEntry_t; //65-bytes

/***************************************************************************
* @brief	Define for GP Proxy Table
*/
typedef struct _attribute_packed_ {
    gpProxyTabEntry_t gpProxyTab[GPP_MAX_PROXY_TABLE_ENTRIES];
    uint8_t gpProxyTabNum;
} gp_proxyTab_t;

/***************************************************************************
* @brief	Define for GP
*/
typedef struct {
    ev_timer_event_t *aliasConflictTimeoutEvt;
    ev_timer_event_t *commissioningWindowTimeoutEvt;
    uint16_t gpAliasConflictAddr;
    uint16_t gpCommissionerAddr;     //addr to which send the notifications during commissioning mode
    uint16_t gpCommissioningWindow;  //default value 180 seconds
    gpProxyCommModeOpt_t gpCommissioningModeOpt;//commissioning option copy from Commissioning Mode command
    uint8_t gppInCommMode;           //global flag, in commissioning mode or in operational mode
    bool firstToForward;
} gpp_ctx_t;


//global value
extern gp_proxyTab_t g_gpProxyTab;
extern gpp_ctx_t g_gppCtx;

gpSecRsp_status_t gpProxySecOperation(uint8_t appId, gpdId_t gpdId,
                                      uint8_t endpoint, uint32_t gpdSecFrameCnt,
                                      uint8_t gpdfSecurityLevel, uint8_t gpdfKeyType,
                                      uint8_t *pKeyType, uint8_t *pKey);

void gppGpDataIndProcess(gp_data_ind_t *pGpDataInd);

void gpProxyTabInit(void);
void gpProxyTabUpdate(void);
void gp_proxyInit(void);
void gp_proxyTabEntryClear(gpProxyTabEntry_t *pEntry);
uint8_t gp_getProxyTabEntryTotalNum(void);
uint8_t gp_getProxyTabEntryLen(gpProxyTabEntry_t *pEntry);
gpProxyTabEntry_t *gp_proxyTabEntryFreeGet(void);
gpProxyTabEntry_t *gp_getProxyTabByGpdId(uint8_t appId, gpdId_t gpdId);
uint8_t lwSinkAddrListAdd(gpProxyTabEntry_t *pEntry, addrExt_t sinkIeeeAddr, uint16_t sinkNwkAddr);
uint8_t lwSinkAddrListRemove(gpProxyTabEntry_t *pEntry, addrExt_t sinkIeeeAddr, uint16_t sinkNwkAddr);
uint8_t sinkGroupListAdd(gpProxyTabEntry_t *pEntry, uint16_t sinkGroupID, uint16_t alias);
uint8_t sinkGroupListRemove(gpProxyTabEntry_t *pEntry, uint16_t sinkGroupID);
uint8_t gp_buildProxyTabEntryFormat(gpProxyTabEntry_t *pEntry, uint8_t *pBuf);

void gpProxyCommissioningModeCmdProcess(uint16_t srcAddr, zcl_gp_proxyCommissioningModeCmd_t *pCmd);
void gpResponseCmdProcess(zcl_gp_responseCmd_t *pCmd);

status_t zcl_gpServerCmdHandler(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);

#endif	/* GP_PROXY_H */

/********************************************************************************************************
 * @file    gp_trans.h
 *
 * @brief   This is the header file for gp_trans
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
#ifndef GP_TRANS_H
#define GP_TRANS_H


#define GPS_MAX_TRANS_TABLE_ENTRIES             0x05

#define ZB_CMD_PAYLOAD_LEN_MAX                  8

#define ZCL_GP_MAX_TRANS_TABLE_ATTR_LEN         50


typedef union {
    uint8_t opt;
    struct {
        uint8_t optLen:4;
        uint8_t optId:4;
    } bits;
} optSelector_t;

typedef union {
    uint8_t opt;
    struct {
        uint8_t side:1;
        uint8_t manuIdPresent:1;
        uint8_t resv:6;
    } bits;
} attrOpt_t;

typedef struct {
    uint8_t contactStatus;
    uint8_t contactBitmask;
} genSwitchCmd_t;

typedef struct _attribute_packed_ {
    uint8_t reportId;
    uint8_t attrOffsetWithinReport;
    uint16_t clusterId;
    uint16_t attrId;
    uint8_t attrDataType;
    attrOpt_t attrOpt;
    uint16_t manuId;
} compactAttrReport_t;

typedef struct _attribute_packed_ {
    optSelector_t optSelector;
    genSwitchCmd_t cmd;
    gpGenericSwCfg_t switchCfg;
} optRecordVectorCmd_t;

typedef struct _attribute_packed_ {
    optSelector_t optSelector;
    compactAttrReport_t report;
} optRecordCompactAttrReport_t;

typedef union _attribute_packed_ {
    optRecordVectorCmd_t vectorCmd;             //4-bytes
    optRecordCompactAttrReport_t compactReport; //11-bytes
} optRecord_t;

/*
 *  @brief	Sink translation table entry
 */
typedef struct _attribute_packed_ {
    gpdId_t gpdId;
    transOpt_t options;
    uint8_t gpdEndpoint;
    uint8_t gpdCommand;
    uint8_t endpoint;
    uint16_t profileId;
    uint16_t clusterId;
    uint8_t zbCommandId;
    uint8_t zbCmdPayloadLen;
    uint8_t optRecordUsed;
    uint8_t used;                                    //20-bytes
    uint8_t zbCmdPayload[ZB_CMD_PAYLOAD_LEN_MAX];    //8-bytes
    optRecord_t optRecord;
} transTabEntry_t; //39-bytes

/*
 *  @brief	Sink translation configuration
 */
typedef struct {
    uint8_t gpdDevId;
    uint8_t gpdCmdId;
    uint16_t zbClusterId;
    uint8_t zbCmdId;
    uint8_t zbCmdPayloadLen;
    uint8_t zbCmdPayload[ZB_CMD_PAYLOAD_LEN_MAX];
} gp_transCfg_t;

/***************************************************************************
* @brief	Define for GP Sink Translation Table
*/
typedef struct _attribute_packed_ {
    transTabEntry_t transTab[GPS_MAX_TRANS_TABLE_ENTRIES];
    uint8_t transTabNum;
} gp_translationTab_t;

extern uint8_t GP_TRANS_CFG_LIST_NUM;
extern const gp_transCfg_t g_gpTransCfgList[];
extern gp_translationTab_t g_gpTransTab;


void gp_transTabInit(void);
void gp_transTabNvUpdate(void);
uint8_t gp_transTabNumGet(void);
uint8_t gp_transTabEntryLenGet(transTabEntry_t *pTransEntry);
uint8_t gp_buildTransTabEntryFormat(transTabEntry_t *pTransEntry, uint8_t *pBuf);
void gp_transTabEntryClear(transTabEntry_t *pTransEntry);
void gp_transTabEntryRemove(uint8_t appId, gpdId_t gpdId, uint8_t endpoint);
transTabEntry_t *gp_transTabReportUpdate(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                                         uint8_t reportId, bool side,
                                         uint16_t clusterId, uint16_t manuId,
                                         gpAttrRecord_t *pAttrRecord);
transTabEntry_t *gp_transTabGenSwitchUpdate(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                                            gpSwitchInfo_t *pSwitchInfo);
transTabEntry_t *gp_transTabEntryFind(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                                      uint8_t gpdCommand, uint8_t zbEndpoint,
                                      uint16_t clusterId, uint16_t profileId,
                                      uint8_t reportId, uint16_t attrId);
transTabEntry_t *gpTransTabUpdate(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                                  transTabEntry_t *pTransTabEntry,
                                  gpTranslation_t *pTransLation);
void gp_transTabUpdateWithCfg(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                              uint8_t devId, uint8_t numOfCmd,
                              uint8_t *pCmdList, uint8_t *pClusterList);
void gp_gpdfTranslate(uint8_t appId, gpdId_t gpdId, uint8_t endpoint,
                      uint8_t gpdCmdId, uint8_t dataLen, uint8_t *pData);

#endif	/* GP_TRANS_H */

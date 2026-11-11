/********************************************************************************************************
 * @file    dGP_stub.h
 *
 * @brief   This is the header file for dGP_stub
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
#ifndef DGP_STUB_H
#define DGP_STUB_H

#include "cGP_stub.h"

//A GP Basic Proxy device shall have a minimum length of 1 entry
#define GP_TX_QUEUE_MAX_NUM             1
#define GP_DATA_IND_SEC_REQ_TAB_NUM     4


//A.1.5.2.1.2
#define GP_TX_OFFSET                    20//ms
//A.1.5.2.1.3
#define GP_MAX_TX_OFFSET_VARIATION      5//ms
//A.1.5.2.1.4
#define GP_TX_DURATION                  10//ms


//GP handle
typedef enum {
    GP_HANDLE_MIN                       = 0x70,
    GP_HANDLE_MAX                       = 0xBD,
    GP_HANDLE_CHANNEL_CONFIGURATION     = 0xBE,
    GP_HANDLE_TUNNELED_GPD_CMD          = 0xBF,
    //NWK_INTERNAL_NSDU_HANDLE = 0xC0,
} gp_handle_e;

/***************************************************************************
* @brief	dGP-DATA.indication
*/
typedef struct {
    uint8_t *gpMpdu;
    uint16_t srcPanId;
    uint16_t dstPanId;
    zb_addr_t srcAddr;
    zb_addr_t dstAddr;
    uint8_t srcAddrMode;
    uint8_t dstAddrMode;
    int8_t rssi;
    uint8_t lqi;
    uint8_t seqNum;
    uint8_t gpMpduLen;
} dgp_data_ind_t;

/************************************************************************************************
 ******************************** dGP stub service specification ********************************
 ***********************************************************************************************/

typedef enum {
    GP_DATA_IND_STATUS_SEC_SUCCESS,
    GP_DATA_IND_STATUS_NO_SECURITY,
    GP_DATA_IND_STATUS_CNT_FAILURE,
    GP_DATA_IND_STATUS_AUTH_FAILURE,
    GP_DATA_IND_STATUS_UNPROCESSED
} gpDataInd_status_t;

/***************************************************************************
* @brief	gpSecurityKeyType values
*/
typedef enum {
    GP_SEC_KEY_TYPE_NO_KEY                              = 0x00,
    GP_SEC_KEY_TYPE_NWK_KEY,
    GP_SEC_KEY_TYPE_GPD_GROUP_KEY,
    GP_SEC_KEY_TYPE_NWK_KEY_DERIVED_GPD_GROUP_KEY,
    GP_SEC_KEY_TYPE_OUT_OF_THE_BOX_GPD_KEY,
    GP_SEC_KEY_TYPE_DERIVED_INDIVIDUAL_GPD_KEY          = 0x07,
} gpSecKeyType_t;

/***************************************************************************
* @brief	GP-DATA.indication
*/
typedef struct {
    uint8_t *gpdAsdu;        //gpdAsdu[0] is cmdId.
    uint32_t srcId;
    uint32_t gpdSecFrameCnt;
    uint32_t mic;
    zb_addr_t srcAddr;
    uint16_t srcPanId;
    bool autoCommissioning;
    bool rxAfterTx;
    uint8_t status;
    int8_t rssi;
    uint8_t lqi;
    uint8_t seqNum;
    uint8_t srcAddrMode;
    uint8_t appId;
    uint8_t gpdfSecurityLevel;//0x00,0x02,0x03
    uint8_t gpdfKeyType;     //0x00 - 0x07
    uint8_t endpoint;
    uint8_t gpdCmdId;
    uint8_t gpdAsduLen;      //length of ZGP App Payload.
    uint8_t frameType;
} gp_data_ind_t;

/***************************************************************************
* @brief	GP-DATA.request txOptions
*/
typedef struct {
    uint8_t useGpTxQueue:1;
    uint8_t useCSMACA:1;
    uint8_t useMACACK:1;
    uint8_t txFrameType:2;
    uint8_t txOnMatchingEndpoint:1;
    uint8_t reserved:2;
} gpDataReq_txOpt_t;

/***************************************************************************
* @brief	GP-DATA.request
*/
typedef struct {
    uint8_t *gpdAsdu;
    uint32_t gpTxQueueEntryLifetime;
    gpdId_t gpdId;
    gpDataReq_txOpt_t txOptions;
    bool action;
    uint8_t appId;
    uint8_t endpoint;
    uint8_t gpdCmdId;
    uint8_t gpdAsduLen;
    uint8_t gpepHandle;
} gp_data_req_t;

typedef enum {
    GP_DATA_CNF_STATUS_TX_QUEUE_FULL,
    GP_DATA_CNF_STATUS_ENTRY_REPLACED,
    GP_DATA_CNF_STATUS_ENTRY_ADDED,
    GP_DATA_CNF_STATUS_ENTRY_EXPIRED,
    GP_DATA_CNF_STATUS_ENTRY_REMOVED,
    GP_DATA_CNF_STATUS_GPDF_SENDING_FINALIZED
} gpDataCnf_status_t;

/***************************************************************************
* @brief	GP-DATA.confirm
*/
typedef struct {
    uint8_t status;
    uint8_t gpepHandle;
} gp_data_cnf_t;

/***************************************************************************
* @brief	GP-SEC.request
*/
typedef struct {
    gpdId_t gpdId;
    uint32_t gpdSecFrameCnt;
    uint8_t appId;
    uint8_t endpoint;
    uint8_t gpdfSecurityLevel;//0x00,0x02,0x03
    uint8_t gpdfKeyType;//0x00,0x01
    uint8_t dgpStubHandle;
} gp_sec_req_t;

typedef enum {
    GP_SEC_RSP_STATUS_DROP_FRAME,
    GP_SEC_RSP_STATUS_MATCH,
    GP_SEC_RSP_STATUS_PASS_UNPROCESSED,
    GP_SEC_RSP_STATUS_TX_THEN_DROP,
    GP_SEC_RSP_ERROR
} gpSecRsp_status_t;

/***************************************************************************
* @brief	GP-SEC.response
*/
typedef struct {
    gpdId_t gpdId;
    uint32_t gpdSecFrameCnt;
    uint8_t status;
    uint8_t dgpStubHandle;
    uint8_t appId;
    uint8_t endpoint;
    uint8_t gpdKey[16];
    uint8_t gpdfSecurityLevel;//0x00,0x02,0x03
    uint8_t gpdfKeyType;//0x00 - 0x07
} gp_sec_rsp_t;

/***************************************************************************
* @brief	GP application payload for appId 0b000 and 0b010
*/
typedef struct {
    uint8_t *gpdCmdPayload;
    uint8_t gpdCmdId;
} gp_appPayload_t;

/***************************************************************************
* @brief	GP data request pending
*/
typedef struct {
    gp_data_req_t *buf; //hold gpDataReq buffer
    uint8_t gpepHandle;
    uint8_t used;
} gp_data_req_pending_t;

/***************************************************************************
* @brief	GP data indication buffer pending, waiting for security process
*/
typedef struct {
    gp_data_ind_t *buf; //hold gpDataInd buffer
    uint32_t frameCounter;
    gpdId_t gpdId;
    uint8_t timeout;
    uint8_t dGpStubHandle;
    uint8_t appId:3;
    uint8_t gpdfSecKey:1;
    uint8_t gpdfSecLevel:2;
    uint8_t used:1;
    uint8_t reserved:1;
} gp_data_ind_entry_t;


typedef bool (*gpDeviceAnnounceCheckCb_t)(uint16_t sinkNwkAddr, addrExt_t sinkIeeeAddr);

/***************************************************************************
* @brief	Define for dStub notify GPEP
*/
typedef void (*gpDataCnfCb_t)(gp_data_cnf_t *pGpDataCnf);
typedef void (*gpDataIndCb_t)(void *arg);
typedef void (*gpSecReqCb_t)(void *arg);

typedef struct {
    gpDataCnfCb_t gpDataCnfCb;
    gpDataIndCb_t gpDataIndCb;
    gpSecReqCb_t gpSecReqCb;
} gp_stubCb_t;

extern gp_data_ind_entry_t g_gpDataIndSecReqTab[];


gp_nwkHdrFrameCtrl_t gpNwkHdrFrameCtrlBuild(uint8_t frameType, bool autoComm, uint8_t appId,
                                            uint8_t secLevel, uint8_t secKey,
                                            bool rxAfterTx, bool direction,
                                            gp_extNwkFrameCtrl_t *pExtNwkFrameCtrl);

void gpStubCbInit(gp_stubCb_t *cb);
void gpTxQueueFree(void);
void gpTxQueueMaintenceClear(void);
gp_data_ind_entry_t *gpDataIndEntryFreeGet(void);
void gpDataIndSecReqEntryClear(gp_data_ind_entry_t *pEntry);
uint8_t dGpStubHandleGet(void);
gp_data_ind_entry_t *gpDataIndGet(uint8_t handle);

void gpDataReq(void *arg);
void gpSecRsp(void *arg);
void dGp_dataInd(void *arg);
void cGpDataCnfHandler(void *arg);

#endif	/* DGP_STUB_H */

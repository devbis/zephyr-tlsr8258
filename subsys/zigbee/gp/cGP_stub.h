/********************************************************************************************************
 * @file    cGP_stub.h
 *
 * @brief   This is the header file for cGP_stub
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
#ifndef CGP_STUB_H
#define CGP_STUB_H


//for application id
#define GP_APP_ID_SRC_ID                0x00
#define GP_APP_ID_LPED                  0x01
#define GP_APP_ID_GPD                   0x02

//for GPD srcID field
#define GPD_SRC_ID_UNSPECIFIED          0x00000000
#define GPD_SRC_ID_RESERVED_L           0xFFFFFFF9
#define GPD_SRC_ID_RESERVED_H           0xFFFFFFFE
#define GPD_SRC_ID_ALL                  0xFFFFFFFF

//for endPoint field
#define GP_ENDPOINT_RESERVED_L          0xF1
#define GP_ENDPOINT_RESERVED_H          0xFE
#define GP_ENDPOINT_ALL                 0xFF

//GPD command range 0xf0 - 0xff is reserved
#define GPD_CMD_ID_IS_INVALID(cmdId)    (((cmdId) & 0xF0) == 0xF0)

/***************************************************************************
* @brief	GPDF NWK frame type
*/
enum {
    GP_NWK_FRAME_TYPE_DATA              = 0,
    GP_NWK_FRAME_TYPE_MAINTENANCE       = 1
};

/***************************************************************************
* @brief	Values of securityLevel
*/
enum {
    GP_SEC_LEVEL_NO_SECURITY,
    GP_SEC_LEVEL_RESERVED,
    GP_SEC_LEVEL_4BFC_4BMIC,
    GP_SEC_LEVEL_4BFC_4BMIC_ENCRYPTION
};

/***************************************************************************
* @brief	Values of securityKey
*/
enum {
    GP_SEC_SHARED_KEY,
    GP_SEC_INDIVIDUAL_KEY,
};

/***************************************************************************
* @brief	NWK frame control field of GPDF
*/
typedef struct {
    uint8_t frameType:2;
    uint8_t protocolVer:4;
    uint8_t autoCommissioning:1;
    uint8_t nwkFrameCtrlExtension:1;
} gp_nwkHdrFrameCtrl_t;

/***************************************************************************
* @brief	Extended NWK frame control field of GPDF
*/
typedef struct {
    uint8_t appId:3;
    uint8_t securityLevel:2;
    uint8_t securityKey:1;
    uint8_t rxAfterTx:1;
    uint8_t direction:1;
} gp_extNwkFrameCtrl_t;

/***************************************************************************
* @brief	GP stub NWK header
*/
typedef struct {
    uint32_t gpdSrcId;
    uint32_t secFrameCnt;
    gp_nwkHdrFrameCtrl_t nwkFrameCtrl;
    gp_extNwkFrameCtrl_t extNwkFrameCtrl;
    uint8_t endpoint;
} gp_nwkHdr_t;

/************************************************************************************************
 ******************************** cGP stub service specification ********************************
 ***********************************************************************************************/

/***************************************************************************
* @brief	CGP-DATA.request txOptions
*/
typedef struct {
    uint8_t useCSMACA:1;
    uint8_t useMACACK:1;
    uint8_t reserved:6;
} cgpDataReq_txOpt_t;

/***************************************************************************
* @brief	CGP-DATA.request
*/
typedef struct {
    uint8_t *gpMpdu;
    uint16_t srcPanId;
    uint16_t dstPanId;
    zb_addr_t srcAddr;
    zb_addr_t dstAddr;
    uint8_t srcAddrMode;
    uint8_t dstAddrMode;
    uint8_t gpMpduLen;
    uint8_t gpMpduHandle;
    cgpDataReq_txOpt_t txOptions;
} cgp_data_req_t;

/***************************************************************************
* @brief	CGP-DATA.confirm
*/
typedef struct {
    uint8_t status;
    uint8_t gpMpduHandle;
} cgp_data_cnf_t;

int32_t cGp_dataReq(void *arg);
void cGp_dataCnf(void *arg);
void cGp_mcpsDataInd(void *arg);

#endif	/* CGP_STUB_H */

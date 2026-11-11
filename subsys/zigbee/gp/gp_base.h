/********************************************************************************************************
 * @file    gp_base.h
 *
 * @brief   This is the header file for gp_base
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
#ifndef GP_BASE_H
#define GP_BASE_H

#include "dGP_stub.h"


typedef struct {
    ev_timer_event_t *transmitChannelTimeoutEvt;
    uint8_t gpOperationalChannel;    //hold the operational channel
    uint8_t gpDuplicateTimeout;      //the default value of 2 seconds can be modified by the application profile
} gp_base_t;

extern gp_base_t g_gpBaseCtx;


void gpSwitchToTransmitChannel(uint8_t operationChannel, uint8_t tempMasterTxChannel);
void gpTransmitChannelTimeoutStop(void);
uint16_t gpAliasSrcAddrDerived(uint8_t appId, gpdId_t gpdId);
uint8_t gppTunnelingDelayGet(bool rxAfterTx, uint8_t lqi, bool firstToForward, bool noRoute);
void gpTranimitGPDF(gp_data_req_t *pGpDataReq);
void gpDevAnnceAliasSend(uint16_t aliasNwkAddr);
bool gpDataIndDuplicateFind(uint8_t appId, gpdId_t gpdId, uint32_t secFrameCounter, uint8_t handle);
bool gpDataTunneledDuplicateCheck(uint8_t appId, gpdId_t gpdId, uint32_t secFrameCounter);
bool gpSecKeyTypeMappingChk(uint8_t secKeyTypeInProxy, uint8_t secKeyTypeFromGPDF);
gpSecRsp_status_t gpKeyRecovery(uint8_t gpdfKeyType, uint8_t entryKeyType, uint8_t *entryKey, gpSecRsp_status_t status, uint8_t *pKeyType, uint8_t *pKey);

#endif	/* GP_BASE_H */

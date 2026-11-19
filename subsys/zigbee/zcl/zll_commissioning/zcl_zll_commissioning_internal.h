/********************************************************************************************************
 * @file    zcl_zll_commissioning_internal.h
 *
 * @brief   This is the header file for zcl_zll_commissioning_internal
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
#ifndef ZCL_ZLL_COMMOSSIONING_INTERNAL_H
#define ZCL_ZLL_COMMOSSIONING_INTERNAL_H


/*
 * information used during touch link
 *
 * */

typedef struct {
    zcl_zllTouckLinkDisc_t *disc;
    zcl_zllTouckLinkNetworkStartParams *networkStartInfo;
    ev_timer_event_t *transIdLifeTimer;
    ev_timer_event_t *runTimer;
    uint32_t transId;
    uint32_t respId;
    uint32_t scanChanMask;
    uint8_t touchLinkChan;
    zll_tlZbInfo_t zbInfo;
    zll_tlZllInfo_t zllInfo;
    uint8_t seqNo;
    uint8_t scanListNum;
    uint8_t opIdx;
    zcl_DeviceInfo dstDevInfo;
    zcl_DeviceInfo devInfo;
    uint8_t state;
    uint8_t status;
    uint8_t workingChannelBackUp;
    uint8_t lqiThreshold;
    uint8_t commissionTxPower;
    uint8_t keyType;
    uint8_t vDoPrimaryScan:1;
    uint8_t IsFirstChannel:1;
    uint8_t startNetworkAllowed:1;
    uint8_t resved:5;
} zcl_zllTouckLink_t;


extern zcl_zllTouckLink_t g_zllTouchLink;

void ll_network_generate(uint8_t *nwkKey);

void ll_network_store(uint8_t *nwkKey);

void zcl_zllTouchLinkScanRequestHandler(epInfo_t *dstEp, uint8_t seqNo);

void zcl_zllTouchLinkScanResponseHandler(zcl_zllTouchLinkScanResp_t *resp, epInfo_t *dstEp, uint8_t lqi);

void zcl_zllTouchLinkNetworkStartOrJoin(void *arg);

void zcl_zllTouchLinkResetFactoryReq(void *arg);

void zcl_zllTouchLinkNetworkStartRequestHandler(uint8_t logicChannel);

int32_t zcl_zllTouchLinkNetworkStartResponseHandler(void *arg);

void zcl_zllTouchLinkNetworkStartDirectJoin(void *arg);

void zcl_zllTouchLinkNetworkJoinRequestHandler(uint8_t cmd);

int32_t zcl_zllTouchLinkNetwokJoinResponseHandler(void *arg);

int32_t zcl_zllTouchLinkTimeout(void *arg);

void zcl_zllTouchLinkFinish(uint8_t status);

bool zcl_zllTouchLinkLeaveCnfCb(nlme_leave_cnf_t *pLeaveCnf);

void zcl_zllTouchLinkDeviceInformationRequestHandler(epInfo_t *dstEp,uint8_t startEpIdx);

void zcl_zllTouchLinkDeviceInformationResponseHandler(zcl_zllTouchLinkDeviceInfoResp_t *devInfoResp);

int32_t zcl_zllTouchLinkStartNetworkStartOrJoinTimerStop(void);

void ll_zllAttrClr(void);

void zb_NwkZllCommissionScanConfirm(void);

#endif	/* ZCL_ZLL_COMMOSSIONING_INTERNAL_H */

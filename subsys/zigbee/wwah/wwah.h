/********************************************************************************************************
 * @file    wwah.h
 *
 * @brief   This is the header file for wwah
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
#ifndef WWAH_H
#define WWAH_H


typedef enum {
    WWAH_TYPE_CLIENT,
    WWAH_TYPE_SERVER
} wwah_type_e;

/**
 *  @brief Defined for wwah cluster attributes
 */
typedef struct {
    bool disableOTADowngrades;
    bool mgmtLeaveWithoutRejoinEnabled;
    uint8_t nwkRetryCount;
    uint8_t macRetryCount;
    bool routerCheckInEnabled;
    bool touchlinkInterpanEnabled;
    bool parentClassificationEnabled;
    bool configurationModeEnabled;
    uint8_t currentDebugReportID;
    bool tcSecurityOnNwkKeyRotationEnabled;
    uint8_t pendingNwkUpdateChannel;
    uint16_t pendingNwkUpdatePanID;
    uint16_t otaMaxOfflineDuration;
} zcl_wwahAttr_t;


extern const zcl_specClusterInfo_t g_wwahClusterList[];
extern uint8_t WWAH_CB_CLUSTER_NUM;
extern zcl_wwahAttr_t g_zcl_wwahAttrs;


void wwah_init(wwah_type_e type, af_simple_descriptor_t *simpleDesc);
status_t zcl_wwahCb(zclIncomingAddrInfo_t *pAddrInfo, uint8_t cmdId, void *cmdPayload);

#endif /* WWAH_H */

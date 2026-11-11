/********************************************************************************************************
 * @file    nwk_nib.h
 *
 * @brief   This is the header file for nwk_nib
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
#ifndef NWK_NIB_H
#define NWK_NIB_H


/**
 *  @brief  NWK: max network depth
 */
#define NWK_MAX_DEPTH                           15

#define NWK_MAX_ROUTERS                         8

/**
 *  @brief  Default value of nib.max_children
            max number of children which can join to this device
 */
#define DEFAULT_MAX_CHILDREN                    25

/***************************************************************************
* @brief	max number of routers per node
*/
#if !defined ( NWK_MAX_ROUTERS )
#define NWK_MAX_ROUTERS                         8
#endif

/***************************************************************************
* @brief	max network depth
*/
#if !defined ( NWK_MAX_DEPTH )
#define NWK_MAX_DEPTH                           5
#endif

#define ZB_NWK_LINK_STATUS_PERIOD_DEFAULT       15


/***************************************************************************
* @brief	NIB struct
*/
typedef struct {
#ifdef ZB_ROUTER_ROLE
    uint32_t passiveAckTimeout;

    uint8_t linkStatusPeriod;
    uint8_t maxBroadcastRetries;
    uint8_t maxChildren;
    uint8_t maxRouters;

    uint8_t maxSourceRoute;
    uint8_t concentratorRadius;
    uint8_t concentratorDiscoveryTime;
    uint8_t routerAgeLimit;
#endif
    uint32_t nwkBroadcastDeliveryTime;

    extPANId_t extPANId;

    uint16_t panId;
    uint16_t nwkAddr;

    uint16_t managerAddr;
    uint16_t txTotal;

    uint16_t txFail;
    uint16_t transactionPersistenceTime;

    uint8_t seqNum;
    uint8_t depth;
    capability_info_t capabilityInfo;
    uint8_t addrAlloc;

    uint8_t updateId;
    uint8_t stackProfile;
    uint8_t allFresh;
    uint8_t secAllFrames;

    uint8_t maxDepth;
    bool reportConstantCost:1;
    bool symLink:1;
    bool useTreeRouting:1;
    bool useMulticast:1;
    bool isConcentrator:1;
    bool uniqueAddr:1;
    bool timeStamp:1;
    bool leaveReqAllowed:1;
    uint8_t parentInfo; //the behavior depends upon whether the device is an FFD or RFD.
    uint8_t endDevTimeoutDefault;

    addrExt_t ieeeAddr;

    bool leaveReqWithoutRejoinAllowed;
    uint8_t resv[3];
} nwk_nib_t; //60-bytes


#define NWK_NIB()                               g_zbNIB
#define NIB_NETWORK_ADDRESS()                   g_zbNIB.nwkAddr//g_zbMacPib.shortAddress
#define NIB_IEEE_ADDRESS()                      g_zbMacPib.extAddress
#define NIB_PAN_ID()                            g_zbNIB.panId//g_zbMacPib.panId

#define NIB_GET_USE_MULTICAST()                 g_zbNIB.useMulticast
#define NIB_SET_USE_MULTICAST(v)                (g_zbNIB.useMulticast = (v))

#endif /* NWK_NIB_H */

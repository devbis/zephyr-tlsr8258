/********************************************************************************************************
 * @file    nwk_ctx.h
 *
 * @brief   This is the header file for nwk_ctx
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
#ifndef NWK_CTX_H
#define NWK_CTX_H


/**
 * Startup control attribute
 */
typedef enum {
    STARTUP_CONTROL_PART_OF_NETWORK,
    STARTUP_CONTROL_FORM_A_NETWORK,
    STARTUP_CONTROL_REJOIN_NETWORK,
    STARTUP_CONTROL_START_FROM_SCRATCH
} StartupControl_attr_t;

/**
   Network layer running state
*/
typedef enum {
    NLME_STATE_IDLE,
    NLME_STATE_FORMATION,       //ed_scan, active_scan, mlme_start
    NLME_STATE_ROUTER_START,    //start_router, mlme_start
    NLME_STATE_DISC,            //active_scan
    NLME_STATE_REJOIN,          //active_scan
    NLME_STATE_DIRECT_JOIN,     //orphan_scan
    NLME_STATE_PANID_CONFLICT,  //mlme_start
    NLME_STATE_ED_SCAN,         //ed_scan
} nlme_state_t;

typedef enum {
    NLME_IDLE,
    NLME_JOINING,
    NLME_LEAVING,
} user_state_t;

typedef struct {
    uint16_t nwkFwdDstAddr;
    uint16_t nwkFwdSrcAddr;
    uint16_t senderAddr;
    uint16_t lastSendFailAddr;
} manyToOne_repair_t;

typedef struct {
    uint16_t nwkFwdDstAddr;
    uint16_t nwkFwdSrcAddr;
} srcRoute_repair_t;

/**
   Place to store values between multiply operations
*/
typedef struct {
    /* For coordinator to store formation information */
    struct {
        uint8_t energy_detect[ZB_MAC_CHANNEL_NUM];
        uint8_t channel_pan_count[ZB_MAC_CHANNEL_NUM];
    } formationInfo;
    /* End [32 bytes] */

    /* For router and end device  to store assocJoin/rejoin information */
    union {
        zb_addition_neighbor_entry_t *pAssocJoinParent;
        zb_addition_neighbor_entry_t *pRejoinParent;
    } join;
    /* End [4 bytes]*/

    /* For coordinator or router store current joining device info */
    ev_timer_event_t *curJoiningDevTimerEvt;//TODO: removed

    uint32_t	scanChannels;

    uint8_t scanDuration;
    uint8_t is_factory_new:1;        /*!< Device is factory new */
    uint8_t permit_join:1;           /*!< True if permit join is in progress */
    uint8_t joined:1;                /*!< Non-zero if we are joined into the network */
    uint8_t router_started:1;        /*!< not used */
    uint8_t is_tc:1;                 /*!< True if we are Trust Center */
    uint8_t joined_pro:1;
    uint8_t panIdConflict:1;         /*!< if we find panID conflict */
    uint8_t joinAccept:1;
    uint8_t known_panids_cnt:4;
    uint8_t discoverRoute:1;         /*!< Always TRUE for NLDE data request! see 2.2.4.1.1.3 */
    uint8_t panidUpdateRecv:1;       /*!< receive network update for panid conflict*/
    uint8_t resv:1;
    uint8_t parentIsChanged:1;       /*!< A flag indicates that the device's parent is changed through rejoin */
    uint8_t user_state:4;            /*!< Current network user_state_t */
    uint8_t state:4;                 /*!< Current network subsystem state nlme_state_t */

    uint8_t leaveRejoin;             /*!< if we need rejoin after leave *///TODO: removed
    uint16_t new_panid;

    /* For PANID conflict resolution */
    uint16_t known_panids[PANID_TABLE_SIZE];

    manyToOne_repair_t manyToOneRepair; /* saved for check nlde-data.confirm status when unicast data frame using many-to-one */
    srcRoute_repair_t srcRouteRepair;
} nwk_ctx_t;

extern nwk_ctx_t g_zbNwkCtx;


#define ZB_SET_DISCOVER_ROUTE(v)        (g_zbNwkCtx.discoverRoute = v)
#define ZB_GET_DISCOVER_ROUTE           (g_zbNwkCtx.discoverRoute)

#define ZB_SET_USER_STATE(v)            (g_zbNwkCtx.user_state = v)
#define ZB_GET_USER_STATE               (g_zbNwkCtx.user_state)

#endif /* NWK_CTX_H */

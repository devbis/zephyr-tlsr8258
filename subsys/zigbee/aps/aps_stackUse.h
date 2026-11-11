/********************************************************************************************************
 * @file    aps_stackUse.h
 *
 * @brief   This is the header file for aps_stackUse
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
#ifndef APS_STACK_USE_H
#define APS_STACK_USE_H

enum {
    APS_ATTR_CHANNEL_MASK               = 0,
    APS_ATTR_USE_EPID,
    APS_ATTR_DESIGNATED_COORDINATOR,
    APS_ATTR_NONMENBER_RAIUS,
    PS_ATTT_USE_INSECURITY_JOIN,
    APS_ATTT_SECURITY_LEVEL,
    APS_ATTT_DISSECURITY,
    APS_ATTT_SENDTESTBUFREQ,
    APS_ONLYACCEPT_ENCRYPTEDCMD,
    APS_LEGACY_SUPPORT,
    APS_REMOVE_DEVICE,
    APS_ATTT_COUNTEDPACKETTX,
    APS_ATT_USESECURE_REJOIN,
    APS_ATT_JOINTONWK,
    APS_ADD_GROUP,
    APS_DELETE_GROUP,
    APS_DELETE_ALLGROUP,
    APS_BINDING_REQ,
    APS_DATA_REQUEST,
    APS_ADD_EP,
    APS_ATTT_DISNWKSECURITY,
    APS_ATT_PERMITJOIN_SET = 21,
};

typedef enum {
    APX_TX_CACHE_STA_TODO,
    APX_TX_CACHE_STA_ADDR_REQ,
    APX_TX_CACHE_STA_TXING,
    APX_TX_CACHE_STA_WAITING_ACK,
    APX_TX_CACHE_STA_RETRY,
    APX_TX_CACHE_STA_DONE,
} aps_tx_cache_state_e;

typedef enum {
    APS_TX_EVENT_TX_TODO,
    APS_TX_EVENT_RETRY,
    APS_TX_EVENT_WAITING_ACK,
    APS_TX_EVENT_DONE,
} aps_tx_cache_evt_e;

/**
 * @brief Definition for aps data fragmentation
 */
typedef struct {
    aps_tx_cache_list_t *list[8];
    uint8_t *req;
    uint8_t *payload;
    uint16_t payloadLen;
    uint8_t totalBlockNum;
    uint8_t curBlockNum;
    uint8_t activeListNum;
    uint8_t ackBit;
    uint8_t reSend;
    uint8_t used;
} apsDataFragmentTransWin_t;

typedef enum {
    APS_RCV_FRAGMENT_IDLE,
    APS_RCV_FRAGMENT_RCVING,
    APS_RCV_FRAGMENT_HANDLING
} apsRcvingWinState_t;

typedef struct {
    void *buf[8];
    uint8_t validBufNum;
    uint8_t minBlockNum;
    uint8_t ackBit;
    uint8_t state:7; /* apsRcvingWinState_t */
    uint8_t acked:1;
} aps_rcv_info_t;

typedef struct {
    ev_timer_event_t *rcvWinTimeoutEvt;
    uint8_t *evBuf;

    aps_rcv_info_t rcvInfo[2];

    uint16_t rcvFragmentlen;
    uint16_t srcAddr;

    uint8_t apsCnt;
    uint8_t totalBlockNum;
    uint8_t blockNumProcessed;
    uint8_t curRcvMinBlockNum;

    uint8_t waitCnt;
    uint8_t used;
} apsDataFragmentRcvWin_t;

void aps_nwk_data_confirm_cb(void *p);
void aps_nwk_data_indication_cb(void *p);
void aps_interPanDataIndCb(void *p);
bool aps_bindingTblMatched(uint16_t clusterID, uint8_t srcEp);
void aps_data_request(void *p);
aps_tx_cache_list_t *apsTxDataPost(bool ackNeed, bool addrReqNeed, bool interPAN,
                                   uint8_t *pdu, apsdeDataConf_t *pCnf);
void apsTxEventPost(aps_tx_cache_list_t *txCache, aps_tx_cache_evt_e evt, uint8_t status);

bool aps_zdDataFilterNormal(zb_normal_neighbor_entry_t *pEntry, uint8_t *nsdupPtr,
                            uint8_t nsduLen, bool toZvd);
bool aps_zdDataAllowInEphe(zb_normal_neighbor_entry_t *pEntry, uint8_t *nsdupPtr,
                           uint8_t nsduLen, bool toZvd);

#endif /* APS_STACK_USE_H */

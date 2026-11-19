/********************************************************************************************************
 * @file    nwk_neighbor.h
 *
 * @brief   This is the header file for nwk_neighbor
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
#ifndef NWK_NEIGHBOR_H
#define NWK_NEIGHBOR_H


/**
    Node Relations
 */
#define NEIGHBOR_IS_PARENT                      0
#define NEIGHBOR_IS_CHILD                       1
#define NEIGHBOR_IS_SIBLING                     2
#define NEIGHBOR_IS_NONE_OF_ABOVE               3
#define NEIGHBOR_IS_PREVIOUS_CHILD              4
#define NEIGHBOR_IS_UNAUTH_CHILD                5

#ifndef ZB_NEIGHBOR_TABLE_NUM
#if ZB_ROUTER_ROLE
#define ZB_NEIGHBOR_TABLE_NUM                26
#define ZB_CHILD_TABLE_NUM                   16
#else
#define ZB_NEIGHBOR_TABLE_NUM                2
#define ZB_CHILD_TABLE_NUM                   0
#endif
#endif

#define ZB_ADDITION_NEIGHBOR_TABLE_SIZE      6

#define	TRANSFAILURE_CNT_MAX                    2


typedef struct nebTbl_t {
    struct nebTbl_t *freeNext;
    struct nebTbl_t *activeNext;
    uint32_t authTimeout;
    uint32_t timeoutCnt;             //this field indicates the current time remaining, in seconds, for the end device.
    uint32_t devTimeout;             //this field indicates the timeout, in seconds, for the end device child.
    uint16_t endDevCfg;              //bit mask of the end device's cfg. the default value shall be 0.
    uint16_t addrmapIdx;
#ifdef ZB_SECURITY
    uint32_t frameCounterFailCnt;
    uint32_t receivedFrameCnt;
    uint32_t incomingFrameCnt;       //incoming frame counter for this device after key change.
    uint8_t keySeqNum;               //key number for which incoming_frame_counter is valid.
#else
    uint8_t rsv;
#endif
    bool keepaliveRcvd;         //this value indicates at least one keepalive has been received from the end device since the router has rebooted.
    uint8_t rxOnWhileIdle:1;
    uint8_t deviceType:3;            //NWK_DEVICE_TYPE_COORDINATOR = 0,
                                //NWK_DEVICE_TYPE_ROUTER = 1,
                                //NWK_DEVICE_TYPE_ED = 2

    uint8_t relationship:3;          //NEIGHBOR_IS_PARENT = 0,
                                //NEIGHBOR_IS_CHILD = 1,
                                //NEIGHBOR_IS_SIBLING = 2,
                                //NEIGHBOR_IS_UNAUTH_CHILD = 5
    uint8_t used:1;
    uint8_t age;
    uint8_t depth;
    uint8_t transFailure;
    uint8_t lqi;
    uint8_t outgoingCost;
} zb_normal_neighbor_entry_t;

typedef struct {
    extPANId_t extPanId;
    addrExt_t extAddr;

    uint16_t shortAddr;
    uint16_t panId;

    uint8_t addrMode;
    uint8_t logicChannel;
    uint8_t depth:4;
    uint8_t beaconOrder:4;
    uint8_t permitJoining:1;
    uint8_t potentialParent:1;
    uint8_t routerCapacity:1;
    uint8_t edCapacity:1;
    uint8_t stackProfile:4;

    uint8_t deviceType:2;
    uint8_t superframeOrder:6;
    uint8_t lqi;
    uint8_t nwkUpdateId;
    uint8_t rsv;
} zb_addition_neighbor_entry_t;

typedef struct {
    zb_addition_neighbor_entry_t additionNeighborTbl[ZB_ADDITION_NEIGHBOR_TABLE_SIZE];//168
    zb_normal_neighbor_entry_t *freeHead;
    zb_normal_neighbor_entry_t *activeHead;
    uint8_t additionNeighborNum;
    uint8_t normalNeighborNum;
    uint8_t childrenNum;
    uint8_t resv;
    zb_normal_neighbor_entry_t neighborTbl[ZB_NEIGHBOR_TABLE_NUM]; //shall be allocated at the last field in the structure of the zb_neighbor_entry_t
} zb_neighbor_entry_t _attribute_aligned_(4);


#define ZBHCI_CHILD_LIST_NUM_MAX        5

typedef struct {
    addrExt_t extAddr;
    uint16_t nwkAddr;
} nwk_childNodesList_t;

typedef struct {
    uint8_t status;                  /*! status, 0: success, 1: failure */
    uint8_t totalChildNodesNum;      /*! total number of the child nodes */
    uint8_t startIdx;                /*! start of the child table index */
    uint8_t childNodesNum;           /*! number of the child nodes in this response packet */
} nwk_childNodesInfo_t;

typedef struct {
    nwk_childNodesInfo_t info;                                  /*! child table info */
    nwk_childNodesList_t list[ZBHCI_CHILD_LIST_NUM_MAX];        /*! child list */
} nwk_childTableInfo_t;

extern uint8_t NWK_NEIGHBORTBL_ADD_LQITHRESHOLD;
extern uint32_t NWK_UNAUTH_CHILD_TABLE_LIFE_TIME;

extern uint8_t ZB_NEIGHBOR_TABLE_SIZE;
extern uint8_t ZB_CHILD_TABLE_SIZE;
extern zb_neighbor_entry_t g_zb_neighborTbl;

void zb_NeighborTableInit(void);

zb_normal_neighbor_entry_t *zb_NeighborTableUpdate(zb_normal_neighbor_entry_t *entry, uint8_t delete);

zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromExtAddr(uint16_t *shortAddr, addrExt_t extAddr, uint16_t *idx);

zb_normal_neighbor_entry_t *zb_NeighborTableSearchFromShortAddr(uint16_t shortAddr, addrExt_t extAddr, uint16_t *idx);

void zb_NeighborTableDelete(zb_normal_neighbor_entry_t *entry);

zb_normal_neighbor_entry_t *zb_NeighborTableSearchForParent(void);

uint16_t zb_NeighborParentShortAddrGet(void);

zb_normal_neighbor_entry_t *zb_NeighborEntryGetFromIdx(uint8_t idx);

uint8_t zb_nwkNeighborDeleteByAddrmapIdx(uint16_t idx);

zb_normal_neighbor_entry_t *nwk_neTblGetByShortAddr(uint16_t shortAddr);

zb_addition_neighbor_entry_t *zb_AdditionNeighborEntryGetFromIdx(uint8_t idx);
uint8_t zb_neighborFrameCntReset(void);
uint8_t zb_AdditionNeighborTableNumGet(void);

void zb_AdditionNeighborReset(void);

uint8_t zb_AdditionNeighborTableUpdate(zb_addition_neighbor_entry_t *entry);

uint8_t zb_NeighborTableNumGet(void);

uint8_t zb_NeighborTableRouterValidNumGet(void);

uint8_t zb_NeighborTableChildEDNumGet(void);

zb_normal_neighbor_entry_t *nwk_neTblGetByExtAddr(addrExt_t extAddr);


/*********************************************************************
 * @fn      zb_allChildNodesRemove
 *
 * @brief   remove all the child nodes
 *
 * @param   None
 *
 * @return  None
 */
void zb_allChildNodesRemove(void);

/*********************************************************************
 * @fn      zb_childNodesListGet
 *
 * @brief   get the child nodes list
 *
 * @param   startIdx - the start index
 *
 * @param   t - the pointer to the child nodes list
 *
 * @return  None
 */
void zb_childNodesListGet(uint8_t startIdx, nwk_childTableInfo_t *t);


#endif /* NWK_NEIGHBOR_H */

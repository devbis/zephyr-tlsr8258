/********************************************************************************************************
 * @file    nwk_addr_map.h
 *
 * @brief   This is the header file for nwk_addr_map
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
#ifndef NWK_ADDR_MAP_H
#define NWK_ADDR_MAP_H


#define ZB_UNKNOWN_SHORT_ADDR           (uint16_t)(-1)

#ifndef ZB_NWK_ADDR_MAP_NUM
#if (ZB_ROUTER_ROLE)
    #define ZB_NWK_ADDR_MAP_NUM      128
#else
    #define ZB_NWK_ADDR_MAP_NUM      8
#endif
#endif

typedef struct addrMapping_t {
    struct addrMapping_t *freeNext;
    struct addrMapping_t *activeNext;
    addrExt_t extAddr;

    uint16_t shortAddr;
    uint8_t aps_dup_cnt;
    uint8_t used:1;
    uint8_t aps_dup_clock:3;
    uint8_t bind:1;
    uint8_t resv:3;
} zb_addr_map_entry_t;//size 20

typedef struct {
    zb_addr_map_entry_t *freeHead;
    zb_addr_map_entry_t *activeHead;
    uint32_t validNum;
    zb_addr_map_entry_t addrMap[ZB_NWK_ADDR_MAP_NUM]; //shall be allocated at the last field in the structure of the zb_addr_map_t
} zb_addr_map_t;

typedef struct {
    uint16_t shortAddr;
    addrExt_t extAddr;
    uint8_t depth;
    uint8_t rxOnWhileIdle:1;
    uint8_t deviceType:3;
    uint8_t relationship:3;
    uint8_t used:1;
} zb_addrForNeighbor_t;

typedef struct {
    uint16_t mask_dstTable;
    addrExt_t dstExtAddr;
} zb_addrForBind_t;

typedef struct {
    uint32_t devTimeout;
    addrExt_t extAddr;
} zb_addrForEDTimeout_t;

extern uint16_t ZB_NWK_ADDR_MAP_SIZE;
extern zb_addr_map_t g_nwkAddrMap;
uint32_t addrMapTblSizeGet(void);

#define zb_addressTableNumGet()         (g_nwkAddrMap.validNum)


uint8_t zb_address_ieee_by_short(uint16_t short_addr, addrExt_t ieee_address);

void zb_addrMappingForBind(uint8_t bind, uint16_t idx);

addrExt_t *zb_ExtAddrPtrByShortAddr(uint16_t shortAddr);

/* get address entry based on the index of the table */
zb_addr_map_entry_t *zb_NwkAddrMapEntryGet(uint16_t orderIndex);

/* store address information for child table */
void nwk_nodeAddrInfoStore(void *arg);

int32_t nwk_nodeAddrInfoDelete(zb_addrForNeighbor_t *pAddrNv);

/* store address information for parent table */
int32_t nwk_parentNodeInfoStore(void);

/* store address information for bind table */
int32_t nwk_bindAddrInfoUpdate(zb_addrForBind_t *pAddrNv);

int32_t nwk_bindAddrInfoDelete(zb_addrForBind_t *pAddrNv);

#endif /* NWK_ADDR_MAP_H */

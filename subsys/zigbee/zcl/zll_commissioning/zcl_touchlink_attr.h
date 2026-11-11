/********************************************************************************************************
 * @file    zcl_touchlink_attr.h
 *
 * @brief   This is the header file for zcl_touchlink_attr
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
#ifndef ZCL_TOUCHLINK_ATTR_H
#define ZCL_TOUCHLINK_ATTR_H


/*********************************************************************
 * CONSTANTS
 */
#define ZB_INTER_PAN_TRANS_ID_LIFETIME          (8 * 1000)
#define ZB_MAX_PERMIT_JOIN_DURATION             (60 * 1000)
#define ZB_MAX_LOST_PARENT_RETRY_ATTEMPTS       (10)
#define ZB_MIN_STARTUP_DELAY_TIME               (2 * 1000) //2s
#define ZB_RX_WINDOW_DURATION                   (5 * 1000) //5s
#define ZB_SCAN_TIME_BASE_DURATION              (250)      //250ms

/*********************************************************************
 * ENUMS
 */

typedef enum {
    LL_ROLE_COORD,
    LL_ROLE_ROUTER,
    LL_ROLE_END_DEVICE
} ll_role_t;

typedef enum {
    DEVELOPMENT_KEY     = 0,
    MASTER_KEY          = 4,
    CERTIFICATION_KEY   = 15
} TOUCHLINK_KEY_TYPE; //zll_key_encryption_algorithms;

/*********************************************************************
 * STRUCTURES
 */

typedef struct {
    uint16_t magicNum;
    bool isFactoryNew;
    uint8_t currentChannel;
    uint16_t panId;
    uint16_t nwkAddr;
    addrExt_t ieeeAddr;
    extPANId_t extPanId;
    uint8_t parentRef;
    uint16_t freeNwkAddressRangeBegin;
    uint16_t freeNwkAddressRangeEnd;
    uint16_t freeGroupAddressRangeBegin;
    uint16_t freeGroupAddressRangeEnd;
    uint16_t base_neighbor_used;
    uint16_t ext_neighbor_used;
    uint8_t child_num;
    uint8_t ed_child_num;
    uint16_t n_elements;
    uint16_t n_sorted_elements;

#ifdef ZB_SECURITY
    ss_material_set_t secur_material_set[SECUR_N_SECUR_MATERIAL];
    uint8_t active_key_seq_number;
    uint8_t security_level:4;
    uint8_t secure_all_frames:1;
    uint8_t active_secur_material_i:2;
#endif
} zll_nv_params_t;

typedef struct {
    /*   Define Address range values even if this device is not address
     * assignment capable with value Zero.*/
    uint16_t freeNwkAddressRangeBegin;       /* Start of range of network addresses this device can assign to its child devices */
    uint16_t freeNwkAddressRangeEnd;         /* End of range of network addresses this device can assign  to its child devices */
    uint16_t freeGroupAddressRangeBegin;     /* Start of range of network addresses this device can assign to its child devices */
    uint16_t freeGroupAddressRangeEnd;       /* End of range of network addresses this device can assign  to its child devices */
} touchlink_attr_t;

/**
 *  @brief  External variable for ZLL Attribute
 */
//extern touchlink_attr_t g_touchlinkAttr;

/*********************************************************************
 * Public Functions
 */
void ll_reset(void);
void ll_get_encrypted_network_key(uint8_t key_index, uint32_t transaction_ID,
                                  uint32_t response_ID, uint8_t *encrypted_network_key);
void ll_decrypt_network_key(uint8_t key_index, uint8_t *encrypted_network_key,
                            uint32_t transaction_ID, uint32_t response_ID);

#endif	/* ZCL_TOUCHLINK_ATTR_H */

/********************************************************************************************************
 * @file    drv_nv.h
 *
 * @brief   This is the header file for drv_nv
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
#pragma once

#include <zephyr/zigbee/zb_types.h>

/*
 * Flash addresses the vendor stack reads factory and user data from. The
 * NV store itself is NVS on a fixed partition; only these two reads remain.
 */
extern u32 g_u32MacFlashAddr;
extern u32 g_u32CfgFlashAddr;

#define MAC_BASE_ADD                    (g_u32MacFlashAddr)
#define CFG_MAC_ADDRESS                 (MAC_BASE_ADD)
#define CFG_PRE_INSTALL_CODE            (0x78000)

typedef struct _attribute_packed_ {
    u16 opIndex;
    u8 	opSect;
} itemIfno_t;

typedef enum {
    NV_MODULE_ZB_INFO                   = 0, /* mustn't modify it */
    NV_MODULE_ADDRESS_TABLE             = 1, /* mustn't modify it */
    NV_MODULE_APS                       = 2, /* mustn't modify it */
    NV_MODULE_ZCL                       = 3, /* mustn't modify it */
    NV_MODULE_NWK_FRAME_COUNT           = 4, /* mustn't modify it */
    NV_MODULE_OTA                       = 5,
    NV_MODULE_APP                       = 6,
    NV_MODULE_KEYPAIR                   = 7,
    NV_MAX_MODULS
} nv_module_t;

typedef enum {
    NV_ITEM_ID_INVALID                  = 0, /* Item id 0 should not be used. */

    NV_ITEM_ZB_INFO                     = 1, /* mustn't modify it */
    NV_ITEM_ADDRESS_FOR_NEIGHBOR,            /* mustn't modify it */
    NV_ITEM_ADDRESS_FOR_BIND,                /* mustn't modify it */
    NV_ITEM_APS_SSIB,                        /* mustn't modify it */
    NV_ITEM_APS_GROUP_TABLE,                 /* mustn't modify it */
    NV_ITEM_APS_BINDING_TABLE,               /* mustn't modify it */

    NV_ITEM_NWK_FRAME_COUNT,                 /* mustn't modify it */

    NV_ITEM_SS_KEY_PAIR,                     /* mustn't modify it */

    NV_ITEM_OTA_HDR_SERVERINFO,
    NV_ITEM_OTA_CODE,

    NV_ITEM_ED_TIMEOUT,

    NV_ITEM_ZCL_REPORT                  = 0x20,
    NV_ITEM_ZCL_ON_OFF,
    NV_ITEM_ZCL_LEVEL,
    NV_ITEM_ZCL_COLOR_CTRL,
    NV_ITEM_ZCL_SCENE_TABLE,
    NV_ITEM_ZCL_GP_PROXY_TABLE,
    NV_ITEM_ZCL_GP_SINK_TABLE,
    NV_ITEM_ZCL_WWAH_APS_AUTH_INFO,
    NV_ITEM_ZCL_WWAH_APS_ACKS_INFO,
    NV_ITEM_ZCL_WWAH_USE_TC_CLUSTER_INFO,

    NV_ITEM_APP_SIMPLE_DESC,
    NV_ITEM_APP_POWER_CNT,
    NV_ITEM_APP_GP_TRANS_TABLE,
    NV_ITEM_ZCL_WWAH_ATTRS,

    NV_ITEM_APS_BINDING_TABLE_V2        = 0x80, /* mustn't modify it */

    NV_ITEM_ID_MAX                      = 0xFF, /* Item id 0xFF should not be used. */
} nv_item_t;

typedef enum {
    NV_SUCC,
    NV_INVALID_MODULS                   = 1,
    NV_INVALID_ID,
    NV_ITEM_NOT_FOUND,
    NV_NOT_ENOUGH_SAPCE,
    NV_ITEM_LEN_NOT_MATCH,
    NV_CHECK_SUM_ERROR,
    NV_ENABLE_PROTECT_ERROR,
    NV_NO_MEDIA,
    NV_DATA_CHECK_ERROR,
    NV_ITEM_CHECK_ERROR,
    NV_MODULE_NOT_FOUND,
    NV_MODULE_ERASE_NEED
} nv_sts_t;


#define ITEM_FIELD_IDLE                 0xFF

nv_sts_t nv_resetAll(void);
nv_sts_t nv_resetModule(u8 modules);
nv_sts_t nv_flashWriteNew(u8 single, u16 id, u8 itemId, u16 len, u8 *buf);
nv_sts_t nv_flashReadNew(u8 single, u8 id, u8 itemId, u16 len, u8 *buf);
nv_sts_t nv_itemDeleteByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx);
nv_sts_t nv_flashSingleItemRemove(u8 id, u8 itemId, u16 len);
nv_sts_t nv_flashReadByIndex(u8 id, u8 itemId, u8 opSect, u16 opIdx, u16 len, u8 *buf);
void nv_itemLengthCheckAdd(u8 itemId, u16 len);
nv_sts_t nv_resetToFactoryNew(void);
bool nv_facrotyNewRstFlagCheck(void);
void nv_facrotyNewRstFlagSet(void);
void nv_facrotyNewRstFlagClear(void);
nv_sts_t nv_nwkFrameCountSaveToFlash(u32 frameCount);
nv_sts_t nv_nwkFrameCountFromFlash(u32 *frameCount);
nv_sts_t nv_flashSingleItemSizeGet(u8 id, u8 itemId, u16 *len);

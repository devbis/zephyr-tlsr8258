/********************************************************************************************************
 * @file    ss_nv.c
 *
 * @brief   This is the source file for ss_nv
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
#include "../common/includes/zb_common.h"


#ifdef ZB_SECURITY

_CODE_SS_ void zdo_ssInfoSaveToFlash(void)
{
#if NV_ENABLE
    /*
     * user can process network key (Encrypt) here :
     * ss_ib.nwkSecurMaterialSet[0].key and ss_ib.nwkSecurMaterialSet[1].key
     *
     * */
    nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (uint8_t *)&ss_ib);
#endif
}

_CODE_SS_ uint8_t zdo_ssInfoInit(void)
{
    uint8_t ret = NV_ITEM_NOT_FOUND;
#if NV_ENABLE
    ret = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (uint8_t *)&ss_ib);
#if ZB_COORDINATOR_ROLE
    ss_ib.keyPairSetNew = (uint8_t *)g_ssTcKeyPair;
#else
    ss_ib.keyPairSetNew = (uint8_t *)&g_ssDevKeyPair;
#endif

    /*
     * user can process network key(Decrypt) here :
     * ss_ib.nwkSecurMaterialSet[0].key and ss_ib.nwkSecurMaterialSet[1].key
     *
     * */
#endif
    return ret;
}

_CODE_SS_ uint8_t zdo_ssInfoKeyGet(void)
{
    uint8_t ret = NV_ITEM_NOT_FOUND;
#if NV_ENABLE
    ss_info_base_t ss;
    ret = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (uint8_t *)&ss);
    if (ret == NV_SUCC) {
        ret = ss.activeSecureMaterialIndex;
    }
#endif
    return ret;
}

#endif

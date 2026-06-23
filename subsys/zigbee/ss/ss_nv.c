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
#include "zb_common_stub.h"
extern __attribute__((weak)) volatile uint32_t zb_restore_diag_trace[16];


#ifdef ZB_SECURITY

_CODE_SS_ void zdo_ssInfoSaveToFlash(void)
{
#if NV_ENABLE
    /*
     * user can process network key (Encrypt) here :
     * ss_ib.nwkSecurMaterialSet[0].key and ss_ib.nwkSecurMaterialSet[1].key
     *
     * */
    nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (u8 *)&ss_ib);
#endif
}

_CODE_SS_ u8 zdo_ssInfoInit(void)
{
    u8 ret = NV_ITEM_NOT_FOUND;
    bool key_set = false;
#if NV_ENABLE
    ret = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (u8 *)&ss_ib);

    /*
     * user can process network key(Decrypt) here :
     * ss_ib.nwkSecurMaterialSet[0].key and ss_ib.nwkSecurMaterialSet[1].key
     *
     * */
#endif
#if ZB_COORDINATOR_ROLE
    ss_ib.keyPairSetNew = (u8 *)g_ssTcKeyPair;
#else
    ss_ib.keyPairSetNew = (u8 *)&g_ssDevKeyPair;
#endif
    /*
     * Rebuild the canonical pointer defaults at runtime on every platform.
     * The Darwin native_sim image cannot encode relocations into the packed
     * ss_ib default initializer (Mach-O ld64 limitation). But on TC32 with
     * Zephyr-style linking the SAME hole exists for a different reason:
     * ss_apsSecurityME.c:120 declares an uninitialized strong
     * `ss_info_base_t ss_ib;`, which wins linker resolution against the
     * weak-initialized definitions in zb_api_bdb_ed_compat.c:35/40. The
     * pointers end up NULL at boot, ss_apsDecryptFrame later derives the
     * Transport-Key encryption key from a NULL tcLinkKey, and every
     * inbound TC Transport-Key fails CCM (slot[47] = 0x4000000N) — the
     * device never gets the network key. Verified via SWS read:
     * ss_ib.tcLinkKey ptr was 0x00000000 before this fix.
     */
    ss_ib.tcLinkKey = (u8 *)tcLinkKeyCentralDefault;
    ss_ib.distributeLinkKey = (u8 *)linkKeyDistributedMaster;
    ss_ib.touchLinkKey = (u8 *)linkKeyDistributedCertification;
    if (ret != NV_SUCC) {
        ZB_IEEE_ADDR_INVALID(ss_ib.trust_center_address);
    }
    for (u8 i = 0; i < SEC_KEY_LEN; i++) {
        if (ss_ib.nwkSecurMaterialSet[ss_ib.activeSecureMaterialIndex].key[i] != 0U) {
            key_set = true;
            break;
        }
    }
    if (&zb_restore_diag_trace[0] != NULL) {
        zb_restore_diag_trace[7] = 0xA5D40000U | (u32)ret;
        zb_restore_diag_trace[8] = ((u32)ss_ib.activeSecureMaterialIndex) |
                                   ((u32)ss_ib.securityLevel << 8) |
                                   ((u32)ss_ib.activeKeySeqNum << 16) |
                                   ((u32)key_set << 24);
    }
    return ret;
}

_CODE_SS_ u8 zdo_ssInfoKeyGet(void)
{
    u8 ret = NV_ITEM_NOT_FOUND;
#if NV_ENABLE
    ss_info_base_t ss;
    ret = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_SSIB, sizeof(ss_ib), (u8 *)&ss);
    if (ret == NV_SUCC) {
        ret = ss.activeSecureMaterialIndex;
    }
#endif
    return ret;
}

#endif

/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/mac_common.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / mac_trx_api.h / ev_timer.h / mac_phy.h
 * are replaced by the Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "mac/includes/mac_phy.h"
#include "mac/includes/mac_trx_api.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"
#include "mac/includes/mac_internal.h"

_attribute_ram_code_ u8 tl_zbMacHdrSize(u16 frameCtrl)
{
    u8 hdrSize = 3;
    u8 dstAddrMode = (u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
    u8 srcAddrMode = (u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

    if (dstAddrMode != 0U) {
        hdrSize = (dstAddrMode == ADDR_MODE_EXT) ? 13U : 7U;
    }

    if (srcAddrMode != 0U) {
        hdrSize = (u8)(hdrSize + ((srcAddrMode == ADDR_MODE_EXT) ? 8U : 2U));

        if ((frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
            hdrSize = (u8)(hdrSize + 2U);
        }
    }

    return hdrSize;
}

u8 *tl_zbMacHdrBuilder(u8 *buf, tl_zb_mac_mhr_t *mhr)
{
    u8 *p = buf + 3;
    u8 dstAddrMode = (u8)((mhr->frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
    u8 srcAddrMode = (u8)((mhr->frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

    buf[0] = (u8)mhr->frameCtrl;
    buf[1] = (u8)(mhr->frameCtrl >> 8);
    buf[2] = ZB_MAC_DSN();
    ZB_INC_MAC_DSN();

    if (dstAddrMode != 0U) {
        buf[3] = (u8)mhr->dstPanId;
        buf[4] = (u8)(mhr->dstPanId >> 8);

        if (dstAddrMode == ADDR_MODE_EXT) {
            memcpy(buf + 5, &mhr->dstAddr, 8);
            p = buf + 13;
        } else {
            memcpy(buf + 5, &mhr->dstAddr, 2);
            p = buf + 7;
        }
    }

    if (srcAddrMode != 0U) {
        if ((mhr->frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
            p[0] = (u8)mhr->srcPanId;
            p[1] = (u8)(mhr->srcPanId >> 8);
            p += 2;
        }

        if (srcAddrMode == ADDR_MODE_EXT) {
            memcpy(p, &mhr->srcAddr, 8);
            p += 8;
        } else {
            memcpy(p, &mhr->srcAddr, 2);
            p += 2;
        }
    }

    return p;
}

u8 tl_zbMacHdrParse(tl_zb_mac_mhr_t *mhr, u8 *buf)
{
    u16 frameCtrl = (u16)buf[0] | ((u16)buf[1] << 8);
    u8 *p = buf + 3;

    memset(mhr, 0, sizeof(tl_zb_mac_mhr_t));

    mhr->frameCtrl = frameCtrl;
    mhr->seqNum = buf[2];
    mhr->dstAddrMode = (u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
    mhr->srcAddrMode = (u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
    mhr->panIdMode = (frameCtrl & MAC_FCF_INTRA_PAN_MASK) ? 0xffU : 0x00U;

    if (mhr->dstAddrMode != 0U) {
        mhr->dstPanId = (u16)p[0] | ((u16)p[1] << 8);
        p += 2;

        if (mhr->dstAddrMode == ADDR_MODE_EXT) {
            memcpy(&mhr->dstAddr, p, 8);
            p += 8;
        } else {
            memcpy(&mhr->dstAddr, p, 2);
            p += 2;
        }
    }

    if (mhr->srcAddrMode != 0U) {
        if (mhr->panIdMode == 0U) {
            mhr->srcPanId = (u16)p[0] | ((u16)p[1] << 8);
            p += 2;
        }

        if (mhr->srcAddrMode == ADDR_MODE_EXT) {
            memcpy(&mhr->srcAddr, p, 8);
            p += 8;
        } else {
            memcpy(&mhr->srcAddr, p, 2);
            p += 2;
        }
    }

    return (u8)(p - buf);
}

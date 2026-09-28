/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "mac_trx_api.h"
#include "mac_common.h"

_attribute_ram_code_ u8 tl_zbMacHdrSize(u16 frameCtrl)
{
	u8 hdrSize = 3;
	u8 dstAddrMode =
		(u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 srcAddrMode =
		(u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dstAddrMode != ADDR_MODE_NONE) {
		hdrSize = (dstAddrMode == ADDR_MODE_EXT) ? 13U : 7U;
	}

	if (srcAddrMode != ADDR_MODE_NONE) {
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
	u8 dstAddrMode =
		(u8)((mhr->frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 srcAddrMode =
		(u8)((mhr->frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	COPY_U16TOBUFFER(buf, mhr->frameCtrl);
	buf[2] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();

	if (dstAddrMode != ADDR_MODE_NONE) {
		COPY_U16TOBUFFER(buf + 3, mhr->dstPanId);

		if (dstAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(buf + 5, &mhr->dstAddr);
			p = buf + 13;
		} else {
			memcpy(buf + 5, &mhr->dstAddr, 2);
			p = buf + 7;
		}
	}

	if (srcAddrMode != ADDR_MODE_NONE) {
		if ((mhr->frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			COPY_U16TOBUFFER(p, mhr->srcPanId);
			p += 2;
		}

		if (srcAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(p, &mhr->srcAddr);
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
	mhr->dstAddrMode =
		(u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	mhr->srcAddrMode =
		(u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
	mhr->panIdMode = (frameCtrl & MAC_FCF_INTRA_PAN_MASK) ? 0xffU : 0x00U;

	if (mhr->dstAddrMode != ADDR_MODE_NONE) {
		mhr->dstPanId = (u16)p[0] | ((u16)p[1] << 8);
		p += 2;

		if (mhr->dstAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(&mhr->dstAddr, p);
			p += 8;
		} else {
			memcpy(&mhr->dstAddr, p, 2);
			p += 2;
		}
	}

	if (mhr->srcAddrMode != ADDR_MODE_NONE) {
		if (mhr->panIdMode == 0U) {
			mhr->srcPanId = (u16)p[0] | ((u16)p[1] << 8);
			p += 2;
		}

		if (mhr->srcAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(&mhr->srcAddr, p);
			p += 8;
		} else {
			memcpy(&mhr->srcAddr, p, 2);
			p += 2;
		}
	}

	return (u8)(p - buf);
}

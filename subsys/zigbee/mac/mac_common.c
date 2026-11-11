/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "mac_trx_api.h"
#include "mac_common.h"

_attribute_ram_code_ uint8_t zb_MacHdrSize(uint16_t frameCtrl)
{
	uint8_t hdrSize = 3;
	uint8_t dstAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	uint8_t srcAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dstAddrMode != ADDR_MODE_NONE) {
		hdrSize = (dstAddrMode == ADDR_MODE_EXT) ? 13U : 7U;
	}

	if (srcAddrMode != ADDR_MODE_NONE) {
		hdrSize = (uint8_t)(hdrSize + ((srcAddrMode == ADDR_MODE_EXT) ? 8U : 2U));

		if ((frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			hdrSize = (uint8_t)(hdrSize + 2U);
		}
	}

	return hdrSize;
}

uint8_t *zb_MacHdrBuilder(uint8_t *buf, zb_mac_mhr_t *mhr)
{
	uint8_t *p = buf + 3;
	uint8_t dstAddrMode =
		(uint8_t)((mhr->frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	uint8_t srcAddrMode =
		(uint8_t)((mhr->frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

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

uint8_t zb_MacHdrParse(zb_mac_mhr_t *mhr, uint8_t *buf)
{
	uint16_t frameCtrl = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
	uint8_t *p = buf + 3;

	memset(mhr, 0, sizeof(zb_mac_mhr_t));

	mhr->frameCtrl = frameCtrl;
	mhr->seqNum = buf[2];
	mhr->dstAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	mhr->srcAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
	mhr->panIdMode = (frameCtrl & MAC_FCF_INTRA_PAN_MASK) ? 0xffU : 0x00U;

	if (mhr->dstAddrMode != ADDR_MODE_NONE) {
		mhr->dstPanId = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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
			mhr->srcPanId = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
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

	return (uint8_t)(p - buf);
}

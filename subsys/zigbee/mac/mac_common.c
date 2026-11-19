/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_le.h"
#include "mac_trx_api.h"
#include "mac_common.h"

_attribute_ram_code_ uint8_t zb_MacHdrSize(uint16_t frameCtrl)
{
	uint8_t hdrSize = MAC_HDR_FCF_SEQ_LEN;
	uint8_t dstAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	uint8_t srcAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dstAddrMode != ADDR_MODE_NONE) {
		hdrSize = (dstAddrMode == ADDR_MODE_EXT)
				  ? (MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN + MAC_EXT_ADDR_LEN)
				  : (MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN + MAC_SHORT_ADDR_LEN);
	}

	if (srcAddrMode != ADDR_MODE_NONE) {
		hdrSize = (uint8_t)(hdrSize + ((srcAddrMode == ADDR_MODE_EXT) ? MAC_EXT_ADDR_LEN
									 : MAC_SHORT_ADDR_LEN));

		if ((frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			hdrSize = (uint8_t)(hdrSize + MAC_PAN_ID_LEN);
		}
	}

	return hdrSize;
}

uint8_t *zb_MacHdrBuilder(uint8_t *buf, zb_mac_mhr_t *mhr)
{
	uint8_t *p = buf + MAC_HDR_FCF_SEQ_LEN;
	uint8_t dstAddrMode =
		(uint8_t)((mhr->frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	uint8_t srcAddrMode =
		(uint8_t)((mhr->frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	COPY_U16TOBUFFER(buf, mhr->frameCtrl);
	buf[2] = ZB_MAC_DSN();
	ZB_INC_MAC_DSN();

	if (dstAddrMode != ADDR_MODE_NONE) {
		COPY_U16TOBUFFER(buf + MAC_HDR_FCF_SEQ_LEN, mhr->dstPanId);

		if (dstAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(buf + MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN,
					  &mhr->dstAddr);
			p = buf + MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN + MAC_EXT_ADDR_LEN;
		} else {
			memcpy(buf + MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN, &mhr->dstAddr,
			       MAC_SHORT_ADDR_LEN);
			p = buf + MAC_HDR_FCF_SEQ_LEN + MAC_PAN_ID_LEN + MAC_SHORT_ADDR_LEN;
		}
	}

	if (srcAddrMode != ADDR_MODE_NONE) {
		if ((mhr->frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			COPY_U16TOBUFFER(p, mhr->srcPanId);
			p += MAC_PAN_ID_LEN;
		}

		if (srcAddrMode == ADDR_MODE_EXT) {
			ZB_IEEE_ADDR_COPY(p, &mhr->srcAddr);
			p += MAC_EXT_ADDR_LEN;
		} else {
			memcpy(p, &mhr->srcAddr, MAC_SHORT_ADDR_LEN);
			p += MAC_SHORT_ADDR_LEN;
		}
	}

	return p;
}

uint8_t zb_MacHdrParse(zb_mac_mhr_t *mhr, uint8_t *buf)
{
	uint16_t frameCtrl = ZB_LE_U16(&buf[0]);
	uint8_t *p = buf + MAC_HDR_FCF_SEQ_LEN;

	memset(mhr, 0, sizeof(zb_mac_mhr_t));

	mhr->frameCtrl = frameCtrl;
	mhr->seqNum = buf[2];
	mhr->dstAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	mhr->srcAddrMode =
		(uint8_t)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
	mhr->panIdMode = (frameCtrl & MAC_FCF_INTRA_PAN_MASK) ? 1U : 0U;

	if (mhr->dstAddrMode != ADDR_MODE_NONE) {
		mhr->dstPanId = ZB_LE_U16(&p[0]);
		p += MAC_PAN_ID_LEN;

		/* Only the short mode is two octets: the vendor object reads the reserved
		 * mode 1 as an extended address, and so do we. */
		if (mhr->dstAddrMode != ADDR_MODE_SHORT) {
			ZB_IEEE_ADDR_COPY(&mhr->dstAddr, p);
			p += MAC_EXT_ADDR_LEN;
		} else {
			memcpy(&mhr->dstAddr, p, MAC_SHORT_ADDR_LEN);
			p += MAC_SHORT_ADDR_LEN;
		}
	}

	if (mhr->srcAddrMode != ADDR_MODE_NONE) {
		if (mhr->panIdMode == 0U) {
			mhr->srcPanId = ZB_LE_U16(&p[0]);
			p += MAC_PAN_ID_LEN;
		}

		if (mhr->srcAddrMode != ADDR_MODE_SHORT) {
			ZB_IEEE_ADDR_COPY(&mhr->srcAddr, p);
			p += MAC_EXT_ADDR_LEN;
		} else {
			memcpy(&mhr->srcAddr, p, MAC_SHORT_ADDR_LEN);
			p += MAC_SHORT_ADDR_LEN;
		}
	}

	return (uint8_t)(p - buf);
}

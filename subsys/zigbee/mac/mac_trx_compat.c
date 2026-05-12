/* SPDX-License-Identifier: Apache-2.0 */

#include "tl_platform.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "mac/includes/mac_trx_api.h"
#include "mac/includes/tl_zb_mac.h"

sys_diagnostics_t g_sysDiags;
zb_info_t g_zbInfo;

__attribute__((weak)) void tl_zbNwkEdMinimalMacRxIndicate(const u8 *macPld, u8 len, s8 rssi)
{
	ARG_UNUSED(macPld);
	ARG_UNUSED(len);
	ARG_UNUSED(rssi);
}

static u8 zb_mac_src_addr_ptr(u16 frame_ctrl, u8 len, u8 *has_src_addr, u8 *malformed)
{
	u8 idx = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u8 dst_mode = (u8)((frame_ctrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 src_mode = (u8)((frame_ctrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);
	u8 src_len;

	*has_src_addr = 0;
	*malformed = 0;

	if (dst_mode != ZB_ADDR_NO_ADDR) {
		idx += MAC_PAN_ID_FIELD_LEN;
		idx += (dst_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	if (src_mode == ZB_ADDR_NO_ADDR) {
		return 0;
	}

	if ((frame_ctrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
		idx += MAC_PAN_ID_FIELD_LEN;
	}

	src_len = (src_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	if ((u16)idx + src_len > len) {
		*malformed = 1;
		return 0;
	}

	*has_src_addr = 1;
	return idx;
}

u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt)
{
	u16 frame_ctrl;
	u8 frame_type;
	u8 has_src_addr;
	u8 malformed;
	u8 src_idx;

	if (needDrop != NULL) {
		*needDrop = 0;
	}
	if (ackPkt != NULL) {
		*ackPkt = 0;
	}

	if (macPld == NULL || len < MAC_MIN_HDR_LEN) {
		if (needDrop != NULL) {
			*needDrop = 1;
		}
		return macPld;
	}

	frame_ctrl = (u16)macPld[0] | ((u16)macPld[1] << 8);
	frame_type = (u8)((frame_ctrl & MAC_FCF_FRAME_TYPE_MASK) >> MAC_FCF_FRAME_TYPE_POS);
	if (ackPkt != NULL && frame_type == MAC_FRAME_TYPE_ACK) {
		*ackPkt = 1;
	}

	src_idx = zb_mac_src_addr_ptr(frame_ctrl, len, &has_src_addr, &malformed);
	if (malformed) {
		if (needDrop != NULL) {
			*needDrop = 1;
		}
		return macPld;
	}

	if (!has_src_addr) {
		return macPld;
	}

	return &macPld[src_idx];
}

void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
{
	ARG_UNUSED(rxBuf);
	ARG_UNUSED(ackPkt);
	ARG_UNUSED(timestamp);

	tl_zbNwkEdMinimalMacRxIndicate(data, len, rssi);
}

void zb_macDataSendHandler(void)
{
}

u8 tl_zbMacHdrSize(u16 frameCtrl)
{
	u8 size = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	u8 dst_mode = (u8)((frameCtrl & MAC_FCF_DST_ADDR_MODE_MASK) >> MAC_FCF_DST_ADDR_MODE_POS);
	u8 src_mode = (u8)((frameCtrl & MAC_FCF_SRC_ADDR_MODE_MASK) >> MAC_FCF_SRC_ADDR_MODE_POS);

	if (dst_mode != ZB_ADDR_NO_ADDR) {
		size += MAC_PAN_ID_FIELD_LEN;
		size += (dst_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	if (src_mode != ZB_ADDR_NO_ADDR) {
		if ((frameCtrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
			size += MAC_PAN_ID_FIELD_LEN;
		}
		size += (src_mode == ZB_ADDR_64BIT_DEV) ? MAC_EXT_ADDR_FIELD_LEN : MAC_SHORT_ADDR_FIELD_LEN;
	}

	return size;
}

u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 send)
{
	ARG_UNUSED(addrMode);
	ARG_UNUSED(addr);
	ARG_UNUSED(send);

	return MAC_STA_NO_DATA;
}

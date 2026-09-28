/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "aps.h"
#include "aps_data.h"
#include "aps_me.h"
#include "ss_apsSecurityME.h"
#include "zb_buffer.h"
#include "zb_task_queue.h"

static u8 aps_counter_value;
static u8 aps_handle;
aps_group_q_t aps_group_q;

u8 aps_get_counter_value(void)
{
	u8 value = aps_counter_value;
	aps_counter_value = value + 1U;
	return value;
}

u8 aps_get_current_counter_value(void)
{
	return (u8)(aps_counter_value - 1U);
}

u8 aps_get_handle(void)
{
	u8 value = aps_handle;
	aps_handle = value + 1U;
	return value & 0x3fU;
}

void aps_init(void)
{
	aps_counter_value = (u8)drv_u32Rand();
	aps_handle = 0;
	aps_me_init();
}

aps_status_t aps_txBufInit(zb_buf_t **buf, void **payload, u8 size)
{
	zb_buf_t *allocated = zb_buf_allocate();

	if (allocated == NULL) {
		return APS_STATUS_TABLE_FULL;
	}

	*buf = allocated;
	TL_BUF_INITIAL_ALLOC(allocated, size, *payload, void *);

	return APS_STATUS_SUCCESS;
}

/*
 * APS frame control field (Zigbee specification, APS frame formats), least
 * significant bit first:
 *
 *	typedef struct {
 *		u8 frameType : 2;    // 0 data, 1 command, 2 acknowledgement, 3 inter-PAN
 *		u8 deliveryMode : 2; // 0 unicast, 2 broadcast, 3 group addressing
 *		u8 ackFormat : 1;    // command-format acknowledgement
 *		u8 security : 1;
 *		u8 ackRequest : 1;
 *		u8 extendedHeader : 1;
 *	} aps_frame_ctrl_t;
 *
 * The code below reads the same bits with masks.  Equivalent field reads:
 *	frame_type      = fc->frameType;                // data[0] & 0x03
 *	delivery_mode   = fc->deliveryMode;             // data[0] & 0x0c
 *	ackFormat       = fc->ackFormat;                // data[0] & 0x10
 *	extendedHeader  = fc->extendedHeader;           // data[0] & 0x80
 */
u8 aps_hdr_parse(u8 *data, aps_rx_hdr_t *hdr)
{
	u8 frame_control = data[0];
	/* Frame type is bits 0..1 and delivery mode is bits 2..3 (aps_data.h). */
	u8 frame_type = frame_control & APS_FRAME_CTRL_FRAME_TYPE_MASK;
	u8 delivery_mode = frame_control & APS_FRAME_CTRL_DELIVERY_MODE_MASK;
	u8 *cursor = data + 1;

	memset(hdr, 0, sizeof(*hdr));
	hdr->frameCtrl = frame_control;

	if (frame_type == APS_FRAME_CTRL_FRAME_TYPE_INTERPAN) {
		/* Inter-PAN frames (frame type 3): five-byte header, no endpoints or counter. */
		COPY_BUFFERTOU16(hdr->clusterId, data + 1);
		COPY_BUFFERTOU16(hdr->profileId, data + 3);

		cursor = data + 5;
		return (u8)(cursor - data);
	}

	if ((frame_control & APS_FRAME_CTRL_ACK_FORMAT) != 0U ||
	    frame_type == APS_FRAME_CTRL_FRAME_TYPE_COMMAND) {
		/* Command frames and command-format ACKs carry only the APS counter
		 * after the frame control. */
		hdr->apsCounter = data[1];
		cursor = data + 2;
		return (u8)(cursor - data);
	}

	if (delivery_mode != 0U && delivery_mode != APS_FRAME_CTRL_DELIVERY_MODE_BROADCAST) {
		/* Other delivery modes put a 16-bit group address in place of the
		 * destination endpoint. */
		u16 value;

		COPY_BUFFERTOU16(value, data + 1);
		hdr->dstEp = (u8)value;
		hdr->dstAddrHi = (u8)(value >> 8);

		COPY_BUFFERTOU16(hdr->clusterId, data + 3);
		COPY_BUFFERTOU16(hdr->profileId, data + 5);

		hdr->srcEp = data[7];
		hdr->apsCounter = data[8];
		cursor = data + 9;
	} else {
		hdr->dstEp = data[1];

		COPY_BUFFERTOU16(hdr->clusterId, data + 2);
		COPY_BUFFERTOU16(hdr->profileId, data + 4);

		hdr->srcEp = data[6];
		hdr->apsCounter = data[7];
		cursor = data + 8;
	}

	if ((frame_control & APS_FRAME_CTRL_EXTENDED_HEADER) != 0U) {
		hdr->extHdr = *cursor++;

		if (hdr->extHdr != 0U) {
			hdr->blockNum = *cursor++;
		}

		if (frame_type == APS_FRAME_CTRL_FRAME_TYPE_ACK) {
			hdr->ackBit = *cursor++;
		}
	}

	return (u8)(cursor - data);
}

void aps_command_handle(void *arg)
{
	aps_data_ind_t *ind = (aps_data_ind_t *)arg;
	/* The guard discards a command whose source is this device's own short
	 * address, i.e. one it sent itself, not one addressed to it. */
	u16 src = ind->src_short_addr;
	u16 local = g_zbInfo.macPib.shortAddress;
	u8 cmdId = ind->asdu[0];

	if (src == local) {
		zb_buf_free((zb_buf_t *)arg);
		return;
	}

	switch (cmdId) {
#if defined(ZB_COORDINATOR_ROLE)
	case APS_CMD_REQUEST_KEY_ID:
		ss_apsRequestKeyCmdHandle(arg);
		break;
	case APS_CMD_VERIFY_KEY_ID:
		ss_apsVerifyKeyCmdHandle(arg);
		break;
	case APS_CMD_UPDATE_DEVICE_ID:
		ss_apsUpdateDeviceCmdHandle(arg);
		break;
#else
	case APS_CMD_TRANSPORT_KEY_ID:
		ss_apsTransportKeyCmdHandle(arg);
		break;
#if defined(ZB_ROUTER_ROLE)
	case APS_CMD_REMOVE_DEVICE_ID:
		if (!ss_securityModeIsDistributed() &&
		    ((ind->security_status & SECURITY_IN_APSLAYER) != 0U)) {
			ss_apsRemoveDeviceCmdHandle(arg);
			return;
		}
		zb_buf_free((zb_buf_t *)arg);
		return;
#endif
	case APS_CMD_SWITCH_KEY_ID:
		ss_apsSwitchKeyCmdHandle(arg);
		break;
#if defined(ZB_ROUTER_ROLE)
	case APS_CMD_TUNNEL_ID:
		ss_apsTunnelCmdHandle(arg);
		return;
#endif
	case APS_CMD_CONFIRM_KEY_ID:
		ss_apsConfirmKeyCmdHandle(arg);
		break;
#endif
	default:
		zb_buf_free((zb_buf_t *)arg);
		break;
	}
}

void aps_process_group_addressed_packet(zb_buf_t *buf)
{
	u8 pending = (u8)((aps_group_q.pending_wptr - aps_group_q.pending_rptr) & 0x03U);

	if (pending == 3U) {
		zb_buf_free(buf);
		return;
	}

	aps_group_q.pending_refs[aps_group_q.pending_wptr & 0x03U] = (u8)ZB_REF_FROM_BUF(buf);
	aps_group_q.pending_wptr++;
	buf->hdr.handle = 0;
	tl_zbTaskPost(aps_deliver_group_msg, NULL);
}

void aps_deliver_group_msg(void *arg)
{
	(void)arg;

	zb_buf_t *out = zb_buf_allocate();

	if (out == NULL) {
		if (aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
			tl_zbTaskPost(aps_deliver_group_msg, NULL);
		}
		return;
	}

	if (aps_group_q.active_buf == NULL) {
		if (aps_group_q.pending_rptr == aps_group_q.pending_wptr) {
			zb_buf_free(out);
			return;
		}

		{
			u8 ref = aps_group_q.pending_refs[aps_group_q.pending_rptr & 0x03U];

			aps_group_q.pending_rptr++;
			if (ref != 0xffU) {
				u8 epNum = 0;
				zb_buf_t *src = ZB_BUF_FROM_REF(ref);
				u16 groupAddr = (u16)src->buf[2] | ((u16)src->buf[3] << 8);
				u8 *eps = aps_group_ep_info_get(groupAddr, &epNum);

				aps_group_q.active_buf = src;
				aps_group_q.active_ep_count = epNum;
				if (eps != NULL && epNum != 0U) {
					memcpy(aps_group_q.active_eps, eps, epNum);
				}
			}
		}
	}

	if (aps_group_q.active_buf == NULL || aps_group_q.active_ep_count == 0U ||
	    aps_group_q.active_buf->hdr.handle >= aps_group_q.active_ep_count) {
		if (aps_group_q.active_buf != NULL) {
			zb_buf_free(aps_group_q.active_buf);
		}
		aps_group_q.active_buf = NULL;
		aps_group_q.active_ep_count = 0;
		memset(aps_group_q.active_eps, 0, sizeof(aps_group_q.active_eps));
		zb_buf_free(out);

		if (aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
			tl_zbTaskPost(aps_deliver_group_msg, NULL);
		}
		return;
	}

	TL_COPY_BUF(out, aps_group_q.active_buf);
	out->hdr.handle = 0;
	{
		aps_data_ind_t *srcInd = (aps_data_ind_t *)aps_group_q.active_buf;
		aps_data_ind_t *dstInd = (aps_data_ind_t *)out;

		dstInd->asdu = out->buf + (srcInd->asdu - aps_group_q.active_buf->buf);
		dstInd->dst_ep = aps_group_q.active_eps[aps_group_q.active_buf->hdr.handle];
	}
	aps_group_q.active_buf->hdr.handle++;
	tl_zbTaskPost(af_aps_data_entry, out);

	if (aps_group_q.active_buf->hdr.handle >= aps_group_q.active_ep_count) {
		zb_buf_free(aps_group_q.active_buf);
		aps_group_q.active_buf = NULL;
		aps_group_q.active_ep_count = 0;
		memset(aps_group_q.active_eps, 0, sizeof(aps_group_q.active_eps));
	}

	if (aps_group_q.active_buf != NULL ||
	    aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
		tl_zbTaskPost(aps_deliver_group_msg, NULL);
	}
}

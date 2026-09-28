/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_radio_l2, CONFIG_ZIGBEE_LOG_LEVEL);

/*
 * The radio drivers hand a whole burst over in one drain pass (the TLSR8258
 * rx_queue holds 16 frames), so a shallower queue here silently truncates the
 * burst that carries AssocResp/Transport-Key.  Keep it at least as deep as the
 * deepest driver-side queue.
 */
#define ZB_L2_RX_DEPTH 16
/* The L2 hands over the PSDU without its FCS. */
#define ZB_L2_PSDU_MAX (IEEE802154_MAX_PHY_PACKET_SIZE - IEEE802154_FCS_LENGTH)

/*
 * The MAC receive path takes frames in the TLSR8258 RF DMA layout: a 5-byte
 * header whose last byte is the PHY length (PSDU plus FCS), the PSDU, and a
 * 2-byte status trailer.
 */
#define ZB_L2_DMA_HDR_LEN     5U
#define ZB_L2_DMA_PHY_LEN_OFS 4U
#define ZB_L2_DMA_TRAILER_LEN 2U

struct zb_l2_frame {
	uint8_t psdu[ZB_L2_PSDU_MAX];
	uint8_t len;
	int8_t rssi;
};

K_MSGQ_DEFINE(zb_l2_rx_queue, sizeof(struct zb_l2_frame), ZB_L2_RX_DEPTH, 4);

static zb_radio_port_rx_sink_t rx_sink;
static atomic_t zb_l2_rx_drops;
static atomic_t zb_l2_ack_frame_pending;

uint32_t zb_radio_l2_rx_drop_count(void)
{
	return (uint32_t)atomic_get(&zb_l2_rx_drops);
}

void zb_radio_l2_register_rx_sink(zb_radio_port_rx_sink_t sink)
{
	rx_sink = sink;
}

void zb_radio_l2_rx_poll(void)
{
	struct zb_l2_frame frame;
	uint8_t dma[ZB_L2_DMA_HDR_LEN + ZB_L2_PSDU_MAX + ZB_L2_DMA_TRAILER_LEN];
	struct zb_radio_rx_frame_view view = {
		.dma = dma,
	};

	if (rx_sink == NULL) {
		return;
	}

	while (k_msgq_get(&zb_l2_rx_queue, &frame, K_NO_WAIT) == 0) {
		memset(dma, 0, sizeof(dma));
		dma[ZB_L2_DMA_PHY_LEN_OFS] = frame.len + IEEE802154_FCS_LENGTH;
		memcpy(&dma[ZB_L2_DMA_HDR_LEN], frame.psdu, frame.len);
		view.len = ZB_L2_DMA_HDR_LEN + frame.len + ZB_L2_DMA_TRAILER_LEN;
		view.rssi_dbm = frame.rssi;
		(void)rx_sink(&view);
	}
}

static enum net_verdict zigbee_l2_recv(struct net_if *iface, struct net_pkt *pkt)
{
	struct zb_l2_frame frame;
	size_t len = net_pkt_get_len(pkt);

	ARG_UNUSED(iface);
	if (len < 3U || len > sizeof(frame.psdu)) {
		atomic_inc(&zb_l2_rx_drops);
		LOG_WRN("dropping RX frame of unsupported length %zu", len);
		return NET_DROP;
	}

	frame.len = (uint8_t)len;
	frame.rssi = net_pkt_ieee802154_rssi_dbm(pkt);
	net_pkt_cursor_init(pkt);
	if (net_pkt_read(pkt, frame.psdu, len) < 0) {
		atomic_inc(&zb_l2_rx_drops);
		LOG_WRN("dropping RX frame: cannot linearize %zu bytes", len);
		return NET_DROP;
	}

	if (k_msgq_put(&zb_l2_rx_queue, &frame, K_NO_WAIT) < 0) {
		atomic_inc(&zb_l2_rx_drops);
		LOG_WRN("dropping RX frame: Zigbee queue full (total drops %u)",
			(unsigned int)atomic_get(&zb_l2_rx_drops));
		return NET_DROP;
	}

	net_pkt_unref(pkt);
	zb_platform_wake();
	return NET_OK;
}

static int zigbee_l2_send(struct net_if *iface, struct net_pkt *pkt)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(pkt);
	return -ENOTSUP;
}

static int zigbee_l2_enable(struct net_if *iface, bool state)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(state);
	return 0;
}

static enum net_l2_flags zigbee_l2_flags(struct net_if *iface)
{
	ARG_UNUSED(iface);
	return 0;
}

void ieee802154_init(struct net_if *iface)
{
	ARG_UNUSED(iface);
}

bool zb_radio_l2_ack_frame_pending_take(void)
{
	return atomic_clear(&zb_l2_ack_frame_pending) != 0;
}

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	ARG_UNUSED(iface);

	/* Frame Pending bit of the ACK frame control field. */
	if ((net_pkt_get_len(pkt) > 0U) && ((net_pkt_data(pkt)[0] & BIT(4)) != 0U)) {
		atomic_set(&zb_l2_ack_frame_pending, 1);
	}

	return NET_OK;
}

NET_L2_INIT(CUSTOM_IEEE802154_L2, zigbee_l2_recv, zigbee_l2_send,
	    zigbee_l2_enable, zigbee_l2_flags);

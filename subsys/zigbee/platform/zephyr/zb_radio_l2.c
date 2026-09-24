/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_radio_l2, CONFIG_ZIGBEE_LOG_LEVEL);

/*
 * The radio drivers hand a whole burst over in one drain pass (the TLSR8258
 * rx_queue holds 16 frames), so a shallower queue here silently truncates the
 * burst that carries AssocResp/Transport-Key.  Keep it at least as deep as the
 * deepest driver-side queue.
 */
#define ZB_L2_RX_DEPTH 16
#define ZB_L2_PSDU_MAX 125

struct zb_l2_frame {
	uint8_t psdu[ZB_L2_PSDU_MAX];
	uint8_t len;
	int8_t rssi;
};

K_MSGQ_DEFINE(zb_l2_rx_queue, sizeof(struct zb_l2_frame), ZB_L2_RX_DEPTH, 4);

static zb_radio_port_rx_sink_t rx_sink;
static atomic_t zb_l2_rx_drops;

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
	uint8_t dma[5 + ZB_L2_PSDU_MAX + 2];
	struct zb_radio_rx_frame_view view = {
		.dma = dma,
	};

	if (rx_sink == NULL) {
		return;
	}

	while (k_msgq_get(&zb_l2_rx_queue, &frame, K_NO_WAIT) == 0) {
		memset(dma, 0, sizeof(dma));
		dma[4] = frame.len + 2U;
		memcpy(&dma[5], frame.psdu, frame.len);
		view.len = frame.len + 7U;
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

enum net_verdict ieee802154_handle_ack(struct net_if *iface, struct net_pkt *pkt)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(pkt);
	return NET_OK;
}

NET_L2_INIT(CUSTOM_IEEE802154_L2, zigbee_l2_recv, zigbee_l2_send,
	    zigbee_l2_enable, zigbee_l2_flags);

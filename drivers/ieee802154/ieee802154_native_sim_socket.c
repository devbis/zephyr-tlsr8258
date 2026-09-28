/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_native_sim_socket_ieee802154

#define LOG_MODULE_NAME ieee802154_native_sim_socket
#define LOG_LEVEL       CONFIG_IEEE802154_DRIVER_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/native_sim_socket.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/byteorder.h>

#include <nsi_errno.h>
#include <nsi_host_trampolines.h>
#include <soc.h>

#include "cmdline.h"
#include "ieee802154_native_sim_socket_bottom.h"
#include "ieee802154_native_sim_socket_medium.h"

#define NATIVE_SIM_SOCKET_PHY_BYTE_US      32U
#define NATIVE_SIM_SOCKET_PHY_SHR_US       192U
#define NATIVE_SIM_SOCKET_FCF_ACK_REQ      BIT(5)
/* Datagrams handled per worker pass, so that FIFO pressure is reproducible. */
#define NATIVE_SIM_SOCKET_RX_WORKER_BUDGET 4U
#define NATIVE_SIM_SOCKET_STATUS_TIMEOUT   K_MSEC(20)

struct native_sim_socket_config {
	const char *server_host;
	uint16_t server_port;
	uint16_t node_id;
	uint8_t mac_addr[8];
};

#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
struct native_sim_socket_rx_fifo_entry {
	uint8_t psdu[NSS_MEDIUM_MAX_PSDU_SIZE];
	uint8_t len;
	int8_t rssi_dbm;
	uint8_t lqi;
	uint64_t ready_us;
	uint32_t frame_no;
};
#endif

struct native_sim_socket_data {
	struct net_if *iface;
	struct k_sem status_sem;
	struct k_sem tx_status_sem;
	uint8_t mac_addr[8];
	uint8_t filter_ieee_addr[8];
	uint16_t filter_pan_id;
	uint16_t filter_short_addr;
	int fd;
	int16_t tx_power_dbm;
	uint8_t channel;
	int8_t status_rssi_dbm;
	int tx_status_rc;
	bool started;
	bool cca_busy;
#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
	struct native_sim_socket_rx_fifo_entry
		rx_fifo[CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_FIFO_SIZE];
	uint8_t rx_fifo_head;
	uint8_t rx_fifo_tail;
	uint8_t rx_fifo_count;
	uint32_t rx_frame_no;
	uint32_t rx_fifo_overflow;
	uint32_t rx_fault_drops;
	uint64_t tx_blocked_until_us;
#endif
};

static void native_sim_socket_deliver(const struct device *dev, const uint8_t *psdu, uint8_t len,
				      int8_t rssi)
{
	struct native_sim_socket_data *data = dev->data;
	struct net_pkt *pkt;

	if (data->iface == NULL) {
		return;
	}

	pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		LOG_DBG("RX dropped: no buffer (len=%u)", len);
		return;
	}
	if (net_pkt_write(pkt, psdu, len) < 0) {
		net_pkt_unref(pkt);
		return;
	}
	net_pkt_set_ieee802154_rssi_dbm(pkt, rssi);
	if (net_recv_data(data->iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
}

/* The driver is polled from ieee802154_native_sim_socket_poll(). */
static const struct device *native_sim_socket_poll_dev;

static const char *cmd_server_host;
static unsigned int cmd_server_port;
static unsigned int cmd_node_id;
static bool cmd_server_port_set;
static bool cmd_node_id_set;

static const char *native_sim_socket_msg_type_str(enum nss_medium_msg_type type)
{
	switch (type) {
	case NSS_MEDIUM_MSG_HELLO:
		return "HELLO";
	case NSS_MEDIUM_MSG_FILTER:
		return "FILTER";
	case NSS_MEDIUM_MSG_TX:
		return "TX";
	case NSS_MEDIUM_MSG_RX:
		return "RX";
	case NSS_MEDIUM_MSG_STATUS:
		return "STATUS";
	default:
		return "UNKNOWN";
	}
}

static void native_sim_socket_cmd_server_port_set(char *argv, int offset)
{
	ARG_UNUSED(argv);
	ARG_UNUSED(offset);

	cmd_server_port_set = true;
}

static void native_sim_socket_cmd_node_id_set(char *argv, int offset)
{
	ARG_UNUSED(argv);
	ARG_UNUSED(offset);

	cmd_node_id_set = true;
}

static void native_sim_socket_add_options(void)
{
	static struct args_struct_t options[] = {
		{
			.option = "ieee802154-medium-host",
			.name = "ip",
			.type = 's',
			.dest = (void *)&cmd_server_host,
			.descript = "IPv4 address of the IEEE 802.15.4 socket medium",
		},
		{
			.option = "ieee802154-medium-port",
			.name = "port",
			.type = 'u',
			.dest = (void *)&cmd_server_port,
			.call_when_found = native_sim_socket_cmd_server_port_set,
			.descript = "UDP port of the IEEE 802.15.4 socket medium",
		},
		{
			.option = "ieee802154-node-id",
			.name = "id",
			.type = 'u',
			.dest = (void *)&cmd_node_id,
			.call_when_found = native_sim_socket_cmd_node_id_set,
			.descript = "Node id of this device on the IEEE 802.15.4 socket medium",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(options);
}

NATIVE_TASK(native_sim_socket_add_options, PRE_BOOT_1, 10);

static uint16_t native_sim_socket_node_id(const struct native_sim_socket_config *cfg)
{
	return cmd_node_id_set ? (uint16_t)cmd_node_id : cfg->node_id;
}

static uint16_t native_sim_socket_server_port(const struct native_sim_socket_config *cfg)
{
	return cmd_server_port_set ? (uint16_t)cmd_server_port : cfg->server_port;
}

static const char *native_sim_socket_server_host(const struct native_sim_socket_config *cfg)
{
	return (cmd_server_host != NULL) ? cmd_server_host : cfg->server_host;
}

#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
static uint64_t native_sim_socket_now_us(void)
{
	/* Finer than the scheduler tick, so the 192 us turnaround is visible. */
	return k_cyc_to_us_floor64(k_cycle_get_64());
}

static void native_sim_socket_rx_fifo_reset(struct native_sim_socket_data *data)
{
	data->rx_fifo_head = 0U;
	data->rx_fifo_tail = 0U;
	data->rx_fifo_count = 0U;
}

static bool native_sim_socket_rx_fault_drop(uint32_t frame_no)
{
	if ((CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_DROP_FRAME != 0) &&
	    (frame_no == CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_DROP_FRAME)) {
		return true;
	}

	return (CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_DROP_EVERY_N != 0) &&
	       ((frame_no % CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_DROP_EVERY_N) == 0U);
}

static void native_sim_socket_rx_fifo_enqueue(struct native_sim_socket_data *data,
					      const struct nss_medium_msg *msg)
{
	struct native_sim_socket_rx_fifo_entry *entry;
	uint64_t now_us;
	uint64_t ready_us;
	uint32_t frame_no;
	bool ack_req;

	frame_no = ++data->rx_frame_no;
	if (native_sim_socket_rx_fault_drop(frame_no)) {
		data->rx_fault_drops++;
		LOG_DBG("RX #%u dropped by fault injection", frame_no);
		return;
	}

	if (data->rx_fifo_count >= ARRAY_SIZE(data->rx_fifo)) {
		data->rx_fifo_overflow++;
		LOG_DBG("RX #%u dropped: FIFO full", frame_no);
		return;
	}

	entry = &data->rx_fifo[data->rx_fifo_tail];
	entry->len = (uint8_t)msg->psdu_len;
	entry->rssi_dbm = msg->rssi_dbm;
	entry->lqi = msg->lqi;
	entry->frame_no = frame_no;
	memcpy(entry->psdu, msg->psdu, msg->psdu_len);

	/* A frame that requests an ACK keeps the receiver busy for the turnaround. */
	ack_req = (msg->psdu_len != 0U) && ((msg->psdu[0] & NATIVE_SIM_SOCKET_FCF_ACK_REQ) != 0U);
	now_us = native_sim_socket_now_us();
	ready_us = now_us + CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_WORKER_DELAY_US;
	if (ack_req && (ready_us < now_us + CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_TURNAROUND_US)) {
		ready_us = now_us + CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_TURNAROUND_US;
	}
	if (ready_us < data->tx_blocked_until_us) {
		ready_us = data->tx_blocked_until_us;
	}
	entry->ready_us = ready_us;
	LOG_DBG("RX #%u queued len=%u ack=%u fifo=%u/%zu", frame_no, entry->len, ack_req ? 1U : 0U,
		data->rx_fifo_count + 1U, ARRAY_SIZE(data->rx_fifo));

	data->rx_fifo_tail = (uint8_t)((data->rx_fifo_tail + 1U) % ARRAY_SIZE(data->rx_fifo));
	data->rx_fifo_count++;
}

static bool native_sim_socket_rx_fifo_deliver_one(const struct device *dev)
{
	struct native_sim_socket_data *data = dev->data;
	struct native_sim_socket_rx_fifo_entry *entry;

	if (data->rx_fifo_count == 0U) {
		return false;
	}

	entry = &data->rx_fifo[data->rx_fifo_head];
	if (native_sim_socket_now_us() < entry->ready_us) {
		return false;
	}

	native_sim_socket_deliver(dev, entry->psdu, entry->len, entry->rssi_dbm);
	LOG_DBG("RX #%u delivered fifo=%u/%zu", entry->frame_no, data->rx_fifo_count - 1U,
		ARRAY_SIZE(data->rx_fifo));

	data->rx_fifo_head = (uint8_t)((data->rx_fifo_head + 1U) % ARRAY_SIZE(data->rx_fifo));
	data->rx_fifo_count--;
	return true;
}

static void native_sim_socket_rx_worker(const struct device *dev)
{
	for (size_t i = 0; i < NATIVE_SIM_SOCKET_RX_WORKER_BUDGET; i++) {
		if (!native_sim_socket_rx_fifo_deliver_one(dev)) {
			break;
		}
	}
}
#endif /* CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY */

static int native_sim_socket_send_msg(const struct device *dev, const struct nss_medium_msg *msg)
{
	struct native_sim_socket_data *data = dev->data;
	uint8_t packet[NSS_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
	int rc;

	rc = nss_medium_encode(packet, sizeof(packet), msg, &packet_len);
	if (rc < 0) {
		return rc;
	}

	if (ieee802154_native_sim_socket_send(data->fd, packet, packet_len) < 0) {
		return -errno;
	}

	return 0;
}

static void native_sim_socket_publish_state(const struct device *dev, enum nss_medium_msg_type type)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct nss_medium_msg msg;
	int rc;

	if (data->fd < 0) {
		return;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = type;
	msg.node_id = native_sim_socket_node_id(cfg);
	msg.channel = data->channel;
	msg.tx_power_dbm = (int8_t)data->tx_power_dbm;
	msg.rx_on = data->started;
	msg.pan_id = data->filter_pan_id;
	msg.short_addr = data->filter_short_addr;
	memcpy(msg.ieee_addr, data->filter_ieee_addr, sizeof(msg.ieee_addr));

	LOG_DBG("publish %s ch=%u pan=0x%04x short=0x%04x rx_on=%u",
		native_sim_socket_msg_type_str(type), msg.channel, msg.pan_id, msg.short_addr,
		msg.rx_on ? 1U : 0U);
	rc = native_sim_socket_send_msg(dev, &msg);
	if (rc < 0) {
		LOG_WRN("medium %s write failed (%d)", native_sim_socket_msg_type_str(type), rc);
	}
}

static void native_sim_socket_handle_status(struct native_sim_socket_data *data,
					    const struct nss_medium_msg *msg)
{
	bool flag;

	if (nss_medium_status_decode_cca_rsp(msg->psdu, msg->psdu_len, &flag) == 0) {
		data->cca_busy = flag;
		data->status_rssi_dbm = msg->rssi_dbm;
		k_sem_give(&data->status_sem);
		LOG_DBG("CCA busy=%u rssi=%d", flag ? 1U : 0U, msg->rssi_dbm);
	} else if (nss_medium_status_decode_tx_result_rsp(msg->psdu, msg->psdu_len, &flag) == 0) {
		data->tx_status_rc = flag ? -EBUSY : 0;
		k_sem_give(&data->tx_status_sem);
		LOG_DBG("TX result %s", flag ? "collision" : "ok");
	} else {
		LOG_DBG("unknown status record (len=%zu)", msg->psdu_len);
	}
}

/* Returns true when a datagram was consumed. */
static bool native_sim_socket_try_rx_once(const struct device *dev)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	uint8_t packet[NSS_MEDIUM_MAX_PACKET_SIZE];
	struct nss_medium_msg msg;
	long len;
	int err;

	if (data->fd < 0) {
		return false;
	}

	len = ieee802154_native_sim_socket_recv(data->fd, packet, sizeof(packet));
	if (len < 0) {
		err = errno;
		if ((err == EAGAIN) || (err == EWOULDBLOCK) || (err == EINTR)) {
			return false;
		}

		LOG_ERR("medium read failed (errno=%d)", err);
		(void)nsi_host_close(data->fd);
		data->fd = -1;
		data->started = false;
		return false;
	}

	if (len == 0) {
		return false;
	}

	if (nss_medium_decode(&msg, packet, (size_t)len) < 0) {
		LOG_WRN("medium decode failed (len=%ld)", len);
		return true;
	}

	if ((msg.node_id != native_sim_socket_node_id(cfg)) || (msg.channel != data->channel)) {
		LOG_DBG("ignore %s node=0x%04x ch=%u", native_sim_socket_msg_type_str(msg.type),
			msg.node_id, msg.channel);
		return true;
	}

	if (msg.type == NSS_MEDIUM_MSG_STATUS) {
		native_sim_socket_handle_status(data, &msg);
		return true;
	}

	/* A frame arriving while the receiver is off is lost. */
	if ((msg.type != NSS_MEDIUM_MSG_RX) || !data->started ||
	    (msg.psdu_len > NSS_MEDIUM_MAX_PSDU_SIZE)) {
		return true;
	}

	LOG_DBG("RX len=%zu rssi=%d lqi=%u", msg.psdu_len, msg.rssi_dbm, msg.lqi);
#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
	native_sim_socket_rx_fifo_enqueue(data, &msg);
#else
	native_sim_socket_deliver(dev, msg.psdu, (uint8_t)msg.psdu_len, msg.rssi_dbm);
#endif
	return true;
}

/*
 * Service the socket for up to wait_ms milliseconds. Without the behavioral
 * PHY this returns as soon as a datagram was handled; with it, the delayed
 * worker is serviced for the whole window so that a STATUS reply does not
 * end the wait before queued frames are delivered.
 */
static void native_sim_socket_pump_rx(const struct device *dev, int wait_ms)
{
	int remaining = wait_ms;

	while (remaining-- >= 0) {
		bool handled = false;

		while (native_sim_socket_try_rx_once(dev)) {
			handled = true;
		}

#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
		native_sim_socket_rx_worker(dev);
		handled = false;
#endif

		if ((wait_ms == 0) || handled) {
			return;
		}

		k_sleep(K_MSEC(1));
	}
}

void ieee802154_native_sim_socket_poll(void)
{
	const struct device *dev = native_sim_socket_poll_dev;

	if (dev == NULL) {
		return;
	}

	while (native_sim_socket_try_rx_once(dev)) {
	}
#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
	native_sim_socket_rx_worker(dev);
#endif
}

static enum ieee802154_hw_caps native_sim_socket_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER;
}

static int native_sim_socket_query_cca(const struct device *dev, bool *busy)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct nss_medium_msg msg;
	uint8_t payload[1];
	size_t payload_len = 0U;
	int rc;

	if (!data->started || (data->fd < 0)) {
		return -EIO;
	}

	rc = nss_medium_status_encode_cca_req(payload, sizeof(payload), &payload_len);
	if (rc < 0) {
		return rc;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = NSS_MEDIUM_MSG_STATUS;
	msg.node_id = native_sim_socket_node_id(cfg);
	msg.channel = data->channel;
	msg.psdu = payload;
	msg.psdu_len = payload_len;

	k_sem_reset(&data->status_sem);
	rc = native_sim_socket_send_msg(dev, &msg);
	if (rc < 0) {
		LOG_WRN("CCA request failed (%d)", rc);
		return rc;
	}

	native_sim_socket_pump_rx(dev, 1);
	if (k_sem_take(&data->status_sem, NATIVE_SIM_SOCKET_STATUS_TIMEOUT) < 0) {
		LOG_WRN("CCA response timeout");
		return -EIO;
	}

	*busy = data->cca_busy;
	return 0;
}

static int native_sim_socket_cca(const struct device *dev)
{
	bool busy = false;
	int rc;

	rc = native_sim_socket_query_cca(dev, &busy);
	if (rc < 0) {
		return rc;
	}

	return busy ? -EBUSY : 0;
}

static int native_sim_socket_set_channel(const struct device *dev, uint16_t channel)
{
	struct native_sim_socket_data *data = dev->data;

	if ((channel < 11U) || (channel > 26U)) {
		return -EINVAL;
	}

	data->channel = (uint8_t)channel;
	native_sim_socket_publish_state(dev, NSS_MEDIUM_MSG_FILTER);
	return 0;
}

static int native_sim_socket_filter(const struct device *dev, bool set,
				    enum ieee802154_filter_type type,
				    const struct ieee802154_filter *filter)
{
	struct native_sim_socket_data *data = dev->data;

	if (!set || (filter == NULL)) {
		return -ENOTSUP;
	}

	switch (type) {
	case IEEE802154_FILTER_TYPE_PAN_ID:
		data->filter_pan_id = filter->pan_id;
		break;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		data->filter_short_addr = filter->short_addr;
		break;
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		memcpy(data->filter_ieee_addr, filter->ieee_addr, sizeof(data->filter_ieee_addr));
		break;
	default:
		return -ENOTSUP;
	}

	native_sim_socket_publish_state(dev, NSS_MEDIUM_MSG_FILTER);
	return 0;
}

static int native_sim_socket_set_txpower(const struct device *dev, int16_t dbm)
{
	struct native_sim_socket_data *data = dev->data;

	data->tx_power_dbm = dbm;
	return 0;
}

static int native_sim_socket_tx(const struct device *dev, enum ieee802154_tx_mode mode,
				struct net_pkt *pkt, struct net_buf *frag)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct nss_medium_msg msg;
	int rc;

	ARG_UNUSED(pkt);

	if (mode != IEEE802154_TX_MODE_DIRECT) {
		return -ENOTSUP;
	}
	if (!data->started || (data->fd < 0) || (frag == NULL)) {
		return -EIO;
	}

	rc = native_sim_socket_cca(dev);
	if (rc < 0) {
		LOG_DBG("TX blocked by CCA (%d)", rc);
		return rc;
	}

	k_sem_reset(&data->tx_status_sem);
	data->tx_status_rc = -EIO;

	memset(&msg, 0, sizeof(msg));
	msg.type = NSS_MEDIUM_MSG_TX;
	msg.node_id = native_sim_socket_node_id(cfg);
	msg.channel = data->channel;
	msg.tx_power_dbm = (int8_t)data->tx_power_dbm;
	msg.psdu = frag->data;
	msg.psdu_len = frag->len;

	LOG_DBG("TX len=%u", frag->len);
	rc = native_sim_socket_send_msg(dev, &msg);
	if (rc < 0) {
		LOG_WRN("TX failed (%d)", rc);
		return rc;
	}

#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
	data->tx_blocked_until_us = native_sim_socket_now_us() + NATIVE_SIM_SOCKET_PHY_SHR_US +
				    ((uint64_t)frag->len * NATIVE_SIM_SOCKET_PHY_BYTE_US) +
				    CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_TURNAROUND_US;
#endif

	native_sim_socket_pump_rx(dev, 5);
	if (k_sem_take(&data->tx_status_sem, NATIVE_SIM_SOCKET_STATUS_TIMEOUT) < 0) {
		LOG_WRN("TX result timeout");
		return -EIO;
	}

	return data->tx_status_rc;
}

static int native_sim_socket_start(const struct device *dev)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	int fd;

	if (data->started) {
		return -EALREADY;
	}

	if (data->fd < 0) {
		fd = ieee802154_native_sim_socket_open(native_sim_socket_server_host(cfg),
						       native_sim_socket_server_port(cfg));
		if (fd < 0) {
			LOG_ERR("cannot open medium %s:%u (%d)", native_sim_socket_server_host(cfg),
				native_sim_socket_server_port(cfg), fd);
			return -nsi_errno_from_mid(-fd);
		}

		data->fd = fd;
		LOG_INF("medium %s:%u node 0x%04x", native_sim_socket_server_host(cfg),
			native_sim_socket_server_port(cfg), native_sim_socket_node_id(cfg));
	}

	/*
	 * The socket has a single reader: ieee802154_native_sim_socket_poll()
	 * and the synchronous TX/CCA paths, all called from the thread that
	 * owns the radio. A separate RX thread would race them on recv().
	 */
	native_sim_socket_poll_dev = dev;

	data->started = true;
	native_sim_socket_publish_state(dev, NSS_MEDIUM_MSG_HELLO);
	return 0;
}

static int native_sim_socket_stop(const struct device *dev)
{
	struct native_sim_socket_data *data = dev->data;

	if (!data->started) {
		return -EALREADY;
	}

	data->started = false;
#if defined(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_BEHAVIORAL_PHY)
	native_sim_socket_rx_fifo_reset(data);
#endif
	native_sim_socket_publish_state(dev, NSS_MEDIUM_MSG_FILTER);
	return 0;
}

IEEE802154_DEFINE_PHY_SUPPORTED_CHANNELS(native_sim_socket_drv_attr, 11, 26);

static int native_sim_socket_attr_get(const struct device *dev, enum ieee802154_attr attr,
				      struct ieee802154_attr_value *value)
{
	ARG_UNUSED(dev);

	return ieee802154_attr_get_channel_page_and_range(
		attr, IEEE802154_ATTR_PHY_CHANNEL_PAGE_ZERO_OQPSK_2450_BPSK_868_915,
		&native_sim_socket_drv_attr.phy_supported_channels, value);
}

static int native_sim_socket_init(const struct device *dev)
{
	struct native_sim_socket_data *data = dev->data;
	const struct native_sim_socket_config *cfg = dev->config;

	memset(data, 0, sizeof(*data));
	data->fd = -1;
	data->channel = 11U;
	data->tx_status_rc = -EIO;
	data->filter_pan_id = NSS_MEDIUM_BROADCAST_PAN;
	data->filter_short_addr = NSS_MEDIUM_BROADCAST_SHORT;
	k_sem_init(&data->status_sem, 0, 1);
	k_sem_init(&data->tx_status_sem, 0, 1);
	memcpy(data->mac_addr, cfg->mac_addr, sizeof(data->mac_addr));
	memcpy(data->filter_ieee_addr, cfg->mac_addr, sizeof(data->filter_ieee_addr));

	return 0;
}

static void native_sim_socket_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct native_sim_socket_data *data = dev->data;

	net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_IEEE802154);
	data->iface = iface;
	ieee802154_init(iface);
}

static const struct ieee802154_radio_api native_sim_socket_radio_api = {
	.iface_api.init = native_sim_socket_iface_init,
	.get_capabilities = native_sim_socket_get_capabilities,
	.cca = native_sim_socket_cca,
	.set_channel = native_sim_socket_set_channel,
	.filter = native_sim_socket_filter,
	.set_txpower = native_sim_socket_set_txpower,
	.tx = native_sim_socket_tx,
	.start = native_sim_socket_start,
	.stop = native_sim_socket_stop,
	.attr_get = native_sim_socket_attr_get,
};

#define NATIVE_SIM_SOCKET_MAC_ADDR(inst)                                                           \
	{DT_INST_PROP_BY_IDX(inst, local_mac_address, 0),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 1),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 2),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 3),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 4),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 5),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 6),                                          \
	 DT_INST_PROP_BY_IDX(inst, local_mac_address, 7)}

#define NATIVE_SIM_SOCKET_DEFINE(inst)                                                             \
	static const struct native_sim_socket_config native_sim_socket_cfg_##inst = {              \
		.server_host = DT_INST_PROP_OR(inst, server_host, "127.0.0.1"),                    \
		.server_port = DT_INST_PROP(inst, server_port),                                    \
		.node_id = DT_INST_PROP(inst, node_id),                                            \
		.mac_addr = NATIVE_SIM_SOCKET_MAC_ADDR(inst),                                      \
	};                                                                                         \
	static struct native_sim_socket_data native_sim_socket_data_##inst;                        \
	NET_DEVICE_DT_INST_DEFINE(inst, native_sim_socket_init, NULL,                              \
				  &native_sim_socket_data_##inst, &native_sim_socket_cfg_##inst,   \
				  CONFIG_IEEE802154_NATIVE_SIM_SOCKET_INIT_PRIO,                   \
				  &native_sim_socket_radio_api, CUSTOM_IEEE802154_L2,              \
				  NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2), IEEE802154_MTU)

DT_INST_FOREACH_STATUS_OKAY(NATIVE_SIM_SOCKET_DEFINE)

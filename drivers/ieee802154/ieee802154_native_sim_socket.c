/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT zephyr_native_sim_socket_ieee802154

#define LOG_MODULE_NAME ieee802154_native_sim_socket
#define LOG_LEVEL CONFIG_IEEE802154_DRIVER_LOG_LEVEL

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

#include <nsi_errno.h>
#include <nsi_host_trampolines.h>
#include <soc.h>

#include "cmdline.h"
#include "ieee802154_native_sim_socket_bottom.h"

#include <zephyr/zigbee/native_sim_socket_medium.h>
#include <zephyr/zigbee/zb_radio_port.h>

#define NATIVE_SIM_SOCKET_PAYLOAD_OFFSET 5U
#define NATIVE_SIM_SOCKET_FCS_LENGTH     2U

struct native_sim_socket_config {
	const char *server_host;
	uint16_t server_port;
	uint16_t node_id;
	uint8_t mac_addr[8];
};

struct native_sim_socket_data {
	struct net_if *iface;
	struct k_thread rx_thread;
	k_thread_stack_t *rx_stack;
	size_t rx_stack_size;
	struct k_sem status_sem;
	struct zb_native_sim_socket_medium_peer peer;
	uint8_t mac_addr[8];
	int fd;
	int16_t tx_power_dbm;
	uint8_t channel;
	int8_t status_rssi_dbm;
	bool started;
	bool cca_busy;
	bool rx_thread_started;
};

static const char *cmd_server_host;
static unsigned int cmd_server_port;
static unsigned int cmd_node_id;
static bool cmd_server_port_set;
static bool cmd_node_id_set;

static K_KERNEL_STACK_DEFINE(native_sim_socket_rx_stack,
			     CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_STACK_SIZE);

static const char *native_sim_socket_msg_type_str(enum zb_native_sim_socket_medium_msg_type type)
{
	switch (type) {
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO:
		return "HELLO";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER:
		return "FILTER";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX:
		return "TX";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX:
		return "RX";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS:
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
			.option = "zigbee-medium-host",
			.name = "ip",
			.type = 's',
			.dest = (void *)&cmd_server_host,
			.descript = "IPv4 address of the native_sim Zigbee socket medium",
		},
		{
			.option = "zigbee-medium-port",
			.name = "port",
			.type = 'u',
			.dest = (void *)&cmd_server_port,
			.call_when_found = native_sim_socket_cmd_server_port_set,
			.descript = "UDP port of the native_sim Zigbee socket medium",
		},
		{
			.option = "zigbee-node-id",
			.name = "id",
			.type = 'u',
			.dest = (void *)&cmd_node_id,
			.call_when_found = native_sim_socket_cmd_node_id_set,
			.descript = "Node id used by the native_sim Zigbee socket medium",
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

static void native_sim_socket_pump_rx(const struct device *dev, int wait_ms);
static int native_sim_socket_send_msg(const struct device *dev,
				      const struct zb_native_sim_socket_medium_msg *msg);

static void native_sim_socket_publish_state(const struct device *dev,
					    enum zb_native_sim_socket_medium_msg_type type)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct zb_native_sim_socket_medium_msg msg;
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
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
	msg.pan_id = data->peer.pan_id;
	msg.short_addr = data->peer.short_addr;
	memcpy(msg.ieee_addr, data->peer.ieee_addr, sizeof(msg.ieee_addr));

	rc = zb_native_sim_socket_medium_encode(packet, sizeof(packet), &msg, &packet_len);
	if (rc < 0) {
		LOG_WRN("medium state encode failed (rc=%d)", rc);
		printk("zb_sock_radio: publish %s encode failed rc=%d\n",
		       native_sim_socket_msg_type_str(type), rc);
		return;
	}

	printk("zb_sock_radio: publish %s node=0x%04x ch=%u pan=0x%04x short=0x%04x rx_on=%u fd=%d\n",
	       native_sim_socket_msg_type_str(type), msg.node_id, msg.channel,
	       msg.pan_id, msg.short_addr, msg.rx_on ? 1U : 0U, data->fd);
	if (ieee802154_native_sim_socket_send(data->fd, packet, packet_len) < 0) {
		LOG_WRN("medium state write failed (errno=%d)", errno);
		printk("zb_sock_radio: publish %s write failed errno=%d\n",
		       native_sim_socket_msg_type_str(type), errno);
	}

	native_sim_socket_pump_rx(dev, 5);
}

static bool native_sim_socket_try_rx_once(const struct device *dev)
{
	struct native_sim_socket_data *data = dev->data;
	const struct native_sim_socket_config *cfg = dev->config;
	static uint32_t rx_try_trace_count;
	static uint32_t rx_eagain_trace_count;
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	uint8_t dma[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PSDU_SIZE +
		    NATIVE_SIM_SOCKET_PAYLOAD_OFFSET + NATIVE_SIM_SOCKET_FCS_LENGTH];
	struct zb_native_sim_socket_medium_msg msg;
	struct zb_radio_rx_frame_view frame;
	long len;
	int err;

	if (data->fd < 0) {
		return false;
	}

	if (rx_try_trace_count < 8U) {
		printk("zb_sock_radio: try RX fd=%d ch=%u started=%u\n",
		       data->fd, data->channel, data->started ? 1U : 0U);
		rx_try_trace_count++;
	}

	len = ieee802154_native_sim_socket_recv(data->fd, packet, sizeof(packet));
	if (len < 0) {
		err = errno;
		if (err == EAGAIN || err == EWOULDBLOCK || err == EINTR) {
			if (rx_eagain_trace_count < 8U) {
				printk("zb_sock_radio: try RX no data errno=%d\n", err);
				rx_eagain_trace_count++;
			}
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

	if (zb_native_sim_socket_medium_decode(&msg, packet, (size_t)len) < 0) {
		LOG_WRN("medium decode failed (len=%ld)", len);
		printk("zb_sock_radio: rx decode failed len=%ld\n", len);
		return true;
	}

	if (msg.node_id != native_sim_socket_node_id(cfg) ||
	    msg.channel != data->channel) {
		printk("zb_sock_radio: ignore %s node=0x%04x ch=%u len=%zu local_node=0x%04x local_ch=%u\n",
		       native_sim_socket_msg_type_str(msg.type), msg.node_id, msg.channel,
		       msg.psdu_len, native_sim_socket_node_id(cfg), data->channel);
		return true;
	}

	if (msg.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS) {
		bool cca_busy;

		if (zb_native_sim_socket_medium_status_decode_cca_rsp(msg.psdu, msg.psdu_len,
								      &cca_busy) == 0) {
			data->cca_busy = cca_busy;
			data->status_rssi_dbm = msg.rssi_dbm;
			k_sem_give(&data->status_sem);
			printk("zb_sock_radio: status CCA busy=%u rssi=%d\n",
			       cca_busy ? 1U : 0U, msg.rssi_dbm);
		}
		return true;
	}

	if (msg.type != ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX) {
		printk("zb_sock_radio: ignore %s node=0x%04x ch=%u len=%zu\n",
		       native_sim_socket_msg_type_str(msg.type), msg.node_id, msg.channel,
		       msg.psdu_len);
		return true;
	}

	if (msg.psdu_len > ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PSDU_SIZE) {
		return true;
	}

	memset(dma, 0, sizeof(dma));
	dma[4] = (uint8_t)(msg.psdu_len + NATIVE_SIM_SOCKET_FCS_LENGTH);
	memcpy(&dma[NATIVE_SIM_SOCKET_PAYLOAD_OFFSET], msg.psdu, msg.psdu_len);
	frame.dma = dma;
	frame.len = (uint8_t)(NATIVE_SIM_SOCKET_PAYLOAD_OFFSET +
			      msg.psdu_len + NATIVE_SIM_SOCKET_FCS_LENGTH);
	frame.rssi_dbm = msg.rssi_dbm;
	printk("zb_sock_radio: deliver RX node=0x%04x ch=%u len=%zu rssi=%d lqi=%u\n",
	       msg.node_id, msg.channel, msg.psdu_len, msg.rssi_dbm, msg.lqi);
	(void)zb_radio_port_native_sim_socket_register_rx_frame(&frame);
	return true;
}

static int native_sim_socket_send_msg(const struct device *dev,
				      const struct zb_native_sim_socket_medium_msg *msg)
{
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
	int rc;
	struct native_sim_socket_data *data = dev->data;

	rc = zb_native_sim_socket_medium_encode(packet, sizeof(packet), msg, &packet_len);
	if (rc < 0) {
		return rc;
	}

	if (ieee802154_native_sim_socket_send(data->fd, packet, packet_len) < 0) {
		return -errno;
	}

	return 0;
}

static void native_sim_socket_pump_rx(const struct device *dev, int wait_ms)
{
	int remaining = wait_ms;

	while (remaining-- >= 0) {
		bool handled = false;

		while (native_sim_socket_try_rx_once(dev)) {
			handled = true;
		}

		if (handled || wait_ms == 0) {
			return;
		}

		k_sleep(K_MSEC(1));
	}
}

static void native_sim_socket_rx_thread(void *p1, void *p2, void *p3)
{
	const struct device *dev = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	printk("zb_sock_radio: rx thread start\n");

	while (true) {
		if (!native_sim_socket_try_rx_once(dev)) {
			k_sleep(K_MSEC(1));
			continue;
		}

		k_yield();
	}
}

static enum ieee802154_hw_caps native_sim_socket_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER;
}

static int native_sim_socket_cca(const struct device *dev)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct zb_native_sim_socket_medium_msg msg;
	uint8_t payload[ZB_NATIVE_SIM_SOCKET_MEDIUM_STATUS_CCA_REQ_LEN];
	size_t payload_len = 0U;
	int rc;

	if (!data->started || data->fd < 0) {
		return -EIO;
	}

	rc = zb_native_sim_socket_medium_status_encode_cca_req(payload, sizeof(payload),
							       &payload_len);
	if (rc < 0) {
		return rc;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS;
	msg.node_id = native_sim_socket_node_id(cfg);
	msg.channel = data->channel;
	msg.psdu = payload;
	msg.psdu_len = payload_len;

	k_sem_reset(&data->status_sem);
	rc = native_sim_socket_send_msg(dev, &msg);
	if (rc < 0) {
		printk("zb_sock_radio: cca request send failed rc=%d\n", rc);
		return rc;
	}

	native_sim_socket_pump_rx(dev, 1);
	rc = k_sem_take(&data->status_sem, K_MSEC(20));
	if (rc < 0) {
		printk("zb_sock_radio: cca request timeout rc=%d\n", rc);
		return -EIO;
	}

	return data->cca_busy ? -EBUSY : 0;
}

static int native_sim_socket_set_channel(const struct device *dev, uint16_t channel)
{
	struct native_sim_socket_data *data = dev->data;

	if (channel < 11U || channel > 26U) {
		return -EINVAL;
	}

	data->channel = (uint8_t)channel;
	printk("zb_sock_radio: set_channel %u\n", channel);
	native_sim_socket_publish_state(dev, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER);
	return 0;
}

static int native_sim_socket_filter(const struct device *dev, bool set,
				    enum ieee802154_filter_type type,
				    const struct ieee802154_filter *filter)
{
	struct native_sim_socket_data *data = dev->data;

	if (!set || filter == NULL) {
		return -ENOTSUP;
	}

	switch (type) {
	case IEEE802154_FILTER_TYPE_PAN_ID:
		data->peer.pan_id = filter->pan_id;
		printk("zb_sock_radio: filter PAN 0x%04x\n", data->peer.pan_id);
		break;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		data->peer.short_addr = filter->short_addr;
		printk("zb_sock_radio: filter SHORT 0x%04x\n", data->peer.short_addr);
		break;
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		memcpy(data->peer.ieee_addr, filter->ieee_addr, sizeof(data->peer.ieee_addr));
		printk("zb_sock_radio: filter IEEE %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		       data->peer.ieee_addr[0], data->peer.ieee_addr[1], data->peer.ieee_addr[2],
		       data->peer.ieee_addr[3], data->peer.ieee_addr[4], data->peer.ieee_addr[5],
		       data->peer.ieee_addr[6], data->peer.ieee_addr[7]);
		break;
	default:
		return -ENOTSUP;
	}

	native_sim_socket_publish_state(dev, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER);
	return 0;
}

static int native_sim_socket_set_txpower(const struct device *dev, int16_t dbm)
{
	struct native_sim_socket_data *data = dev->data;

	data->tx_power_dbm = dbm;
	return 0;
}

static int native_sim_socket_tx(const struct device *dev,
				enum ieee802154_tx_mode mode,
				struct net_pkt *pkt,
				struct net_buf *frag)
{
	const struct native_sim_socket_config *cfg = dev->config;
	struct native_sim_socket_data *data = dev->data;
	struct zb_native_sim_socket_medium_msg msg;
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
	int rc;

	ARG_UNUSED(pkt);

	if (mode != IEEE802154_TX_MODE_DIRECT) {
		return -ENOTSUP;
	}
	if (!data->started || data->fd < 0 || frag == NULL) {
		printk("zb_sock_radio: tx blocked started=%u fd=%d frag=%p\n",
		       data->started ? 1U : 0U, data->fd, frag);
		return -EIO;
	}

	memset(&msg, 0, sizeof(msg));
	msg.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX;
	msg.node_id = native_sim_socket_node_id(cfg);
	msg.channel = data->channel;
	msg.tx_power_dbm = (int8_t)data->tx_power_dbm;
	msg.psdu = frag->data;
	msg.psdu_len = frag->len;

	rc = zb_native_sim_socket_medium_encode(packet, sizeof(packet), &msg, &packet_len);
	if (rc < 0) {
		printk("zb_sock_radio: tx encode failed rc=%d len=%u\n", rc, frag->len);
		return rc;
	}

	printk("zb_sock_radio: TX node=0x%04x ch=%u len=%u\n",
	       msg.node_id, msg.channel, frag->len);
	if (ieee802154_native_sim_socket_send(data->fd, packet, packet_len) < 0) {
		printk("zb_sock_radio: tx write failed errno=%d\n", errno);
		return -errno;
	}

	native_sim_socket_pump_rx(dev, 5);

	return 0;
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
			printk("zb_sock_radio: open failed host=%s port=%u rc=%d\n",
			       native_sim_socket_server_host(cfg),
			       native_sim_socket_server_port(cfg), fd);
			return -nsi_errno_from_mid(-fd);
		}

		data->fd = fd;
		printk("zb_sock_radio: open host=%s port=%u fd=%d node=0x%04x\n",
		       native_sim_socket_server_host(cfg),
		       native_sim_socket_server_port(cfg), data->fd,
		       native_sim_socket_node_id(cfg));
	}

	if (!data->rx_thread_started) {
		k_thread_create(&data->rx_thread, data->rx_stack, data->rx_stack_size,
				native_sim_socket_rx_thread, (void *)dev, NULL, NULL,
				K_PRIO_PREEMPT(CONFIG_IEEE802154_NATIVE_SIM_SOCKET_RX_THREAD_PRIO),
				0, K_NO_WAIT);
		k_thread_name_set(&data->rx_thread, "zb_sock_rx");
		data->rx_thread_started = true;
	}

	data->started = true;
	printk("zb_sock_radio: start ch=%u\n", data->channel);
	native_sim_socket_publish_state(dev, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO);
	return 0;
}

static int native_sim_socket_stop(const struct device *dev)
{
	struct native_sim_socket_data *data = dev->data;

	if (!data->started) {
		return -EALREADY;
	}

	data->started = false;
	printk("zb_sock_radio: stop\n");
	native_sim_socket_publish_state(dev, ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER);
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
	data->iface = NULL;
	data->rx_stack = native_sim_socket_rx_stack;
	data->rx_stack_size = K_KERNEL_STACK_SIZEOF(native_sim_socket_rx_stack);
	data->fd = -1;
	data->tx_power_dbm = 0;
	data->channel = 11U;
	data->status_rssi_dbm = 0;
	k_sem_init(&data->status_sem, 0, 1);
	memcpy(data->mac_addr, cfg->mac_addr, sizeof(data->mac_addr));
	zb_native_sim_socket_medium_peer_reset(&data->peer);
	memcpy(data->peer.ieee_addr, cfg->mac_addr, sizeof(data->peer.ieee_addr));
	printk("zb_sock_radio: init node=0x%04x host=%s port=%u mac=%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
	       native_sim_socket_node_id(cfg), native_sim_socket_server_host(cfg),
	       native_sim_socket_server_port(cfg), data->mac_addr[0], data->mac_addr[1],
	       data->mac_addr[2], data->mac_addr[3], data->mac_addr[4], data->mac_addr[5],
	       data->mac_addr[6], data->mac_addr[7]);

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

#define NATIVE_SIM_SOCKET_MAC_ADDR(inst) { DT_INST_PROP_BY_IDX(inst, local_mac_address, 0), \
	DT_INST_PROP_BY_IDX(inst, local_mac_address, 1), DT_INST_PROP_BY_IDX(inst, local_mac_address, 2), \
	DT_INST_PROP_BY_IDX(inst, local_mac_address, 3), DT_INST_PROP_BY_IDX(inst, local_mac_address, 4), \
	DT_INST_PROP_BY_IDX(inst, local_mac_address, 5), DT_INST_PROP_BY_IDX(inst, local_mac_address, 6), \
	DT_INST_PROP_BY_IDX(inst, local_mac_address, 7) }

#define NATIVE_SIM_SOCKET_DEFINE(inst) \
	static const struct native_sim_socket_config native_sim_socket_cfg_##inst = { \
		.server_host = DT_INST_PROP_OR(inst, server_host, "127.0.0.1"), \
		.server_port = DT_INST_PROP(inst, server_port), \
		.node_id = DT_INST_PROP(inst, node_id), \
		.mac_addr = NATIVE_SIM_SOCKET_MAC_ADDR(inst), \
	}; \
	static struct native_sim_socket_data native_sim_socket_data_##inst; \
	NET_DEVICE_DT_INST_DEFINE(inst, native_sim_socket_init, NULL, \
				  &native_sim_socket_data_##inst, \
				  &native_sim_socket_cfg_##inst, \
				  CONFIG_IEEE802154_NATIVE_SIM_SOCKET_INIT_PRIO, \
				  &native_sim_socket_radio_api, IEEE802154_L2, \
				  NET_L2_GET_CTX_TYPE(IEEE802154_L2), IEEE802154_MTU)

DT_INST_FOREACH_STATUS_OKAY(NATIVE_SIM_SOCKET_DEFINE)

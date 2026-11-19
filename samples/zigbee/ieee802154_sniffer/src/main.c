/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

/* The L2 hands over the PSDU without its FCS. */
#define PSDU_MAX (IEEE802154_MAX_PHY_PACKET_SIZE - IEEE802154_FCS_LENGTH)

static const struct device *const radio = DEVICE_DT_GET(DT_CHOSEN(zephyr_ieee802154));
static uint8_t channel = 11U;

static enum net_verdict sniffer_l2_recv(struct net_if *iface, struct net_pkt *pkt)
{
	uint8_t psdu[PSDU_MAX];
	size_t len = net_pkt_get_len(pkt);

	ARG_UNUSED(iface);

	if ((len > 0U) && (len <= sizeof(psdu))) {
		net_pkt_cursor_init(pkt);
		if (net_pkt_read(pkt, psdu, len) == 0) {
			printk("received: ");
			for (size_t i = 0U; i < len; i++) {
				printk("%02x", psdu[i]);
			}
			printk(" power: %d lqi: %u time: %u\n", net_pkt_ieee802154_rssi_dbm(pkt),
			       net_pkt_ieee802154_lqi(pkt), k_cyc_to_us_floor32(k_cycle_get_32()));
		}
	}

	net_pkt_unref(pkt);
	return NET_OK;
}

static int sniffer_l2_send(struct net_if *iface, struct net_pkt *pkt)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(pkt);
	return -ENOTSUP;
}

static int sniffer_l2_enable(struct net_if *iface, bool state)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(state);
	return 0;
}

static enum net_l2_flags sniffer_l2_flags(struct net_if *iface)
{
	ARG_UNUSED(iface);
	return 0;
}

/* The radio drivers expect these two symbols from an IEEE 802.15.4 L2. */
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

NET_L2_INIT(CUSTOM_IEEE802154_L2, sniffer_l2_recv, sniffer_l2_send, sniffer_l2_enable,
	    sniffer_l2_flags);

static const struct ieee802154_radio_api *radio_api(void)
{
	return (const struct ieee802154_radio_api *)radio->api;
}

static int cmd_channel(const struct shell *sh, size_t argc, char **argv)
{
	long value;
	int ret;

	if (argc != 2) {
		shell_error(sh, "channel <11..26>");
		return -EINVAL;
	}

	value = strtol(argv[1], NULL, 10);
	if ((value < 11) || (value > 26)) {
		shell_error(sh, "channel %ld is out of range", value);
		return -EINVAL;
	}

	channel = (uint8_t)value;
	ret = radio_api()->set_channel(radio, channel);
	if ((ret < 0) && (ret != -EALREADY)) {
		shell_error(sh, "set_channel failed (%d)", ret);
		return ret;
	}

	return 0;
}

static int cmd_receive(const struct shell *sh, size_t argc, char **argv)
{
	struct ieee802154_config config = {.promiscuous = true};
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = radio_api()->configure(radio, IEEE802154_CONFIG_PROMISCUOUS, &config);
	if (ret < 0) {
		shell_error(sh, "promiscuous mode failed (%d)", ret);
		return ret;
	}

	ret = radio_api()->set_channel(radio, channel);
	if ((ret < 0) && (ret != -EALREADY)) {
		shell_error(sh, "set_channel failed (%d)", ret);
		return ret;
	}

	ret = radio_api()->start(radio);
	if ((ret < 0) && (ret != -EALREADY)) {
		shell_error(sh, "start failed (%d)", ret);
		return ret;
	}

	return 0;
}

static int cmd_sleep(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = radio_api()->stop(radio);
	if ((ret < 0) && (ret != -EALREADY)) {
		shell_error(sh, "stop failed (%d)", ret);
		return ret;
	}

	return 0;
}

SHELL_CMD_ARG_REGISTER(channel, NULL, "Set the channel, 11..26", cmd_channel, 2, 0);
SHELL_CMD_REGISTER(receive, NULL, "Start the capture", cmd_receive);
SHELL_CMD_REGISTER(sleep, NULL, "Stop the capture", cmd_sleep);

int main(void)
{
	if (!device_is_ready(radio)) {
		printk("radio is not ready\n");
	}

	return 0;
}

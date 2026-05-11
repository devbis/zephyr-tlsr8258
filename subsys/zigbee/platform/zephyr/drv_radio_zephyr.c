/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_radio.h"
#include "drv_radio_map.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(zigbee_radio_zephyr, CONFIG_ZIGBEE_LOG_LEVEL);

struct zb_radio_ctx {
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	u8 *rx_target;
	u8 *rx_next;
	u8 rx_shadow[256];
	atomic_t tx_done;
	atomic_t rx_done;
	u8 last_rx_rssi_raw;
	u8 trx_state;
	u8 tx_power;
};

static struct zb_radio_ctx g_radio;

static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len);
extern void rf_tx_irq_handler(void);

static void zb_radio_on_rx(const uint8_t *rx_dma, uint8_t rx_len, int8_t rssi_dbm)
{
	size_t copy_len;

	if (rx_dma == NULL) {
		return;
	}

	copy_len = rx_len;
	memcpy(g_radio.rx_shadow, rx_dma, copy_len);
	if (rssi_dbm <= -110) {
		g_radio.last_rx_rssi_raw = 0;
	} else {
		g_radio.last_rx_rssi_raw = (uint8_t)(rssi_dbm + 110);
	}
	atomic_set(&g_radio.rx_done, 1);
}

void zb_radio_init(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(zb));

	memset(&g_radio, 0, sizeof(g_radio));
	g_radio.trx_state = RF_MODE_OFF;
	g_radio.tx_power = ZB_DEFAULT_TX_POWER_IDX;
	g_radio.rx_next = g_radio.rx_shadow;
	g_radio.last_rx_rssi_raw = 110u;

	if (!device_is_ready(dev)) {
		return;
	}

	g_radio.dev = dev;
	g_radio.api = (const struct ieee802154_radio_api *)dev->api;
	tlsr8258_zigbee_register_rx_cb(zb_radio_on_rx);
}

void zb_radio_smoke_probe(void)
{
	static const u8 smoke_psdu[] = {0x61, 0x88, 0x00};

	zb_radio_init();
	if ((g_radio.dev == NULL) || (g_radio.api == NULL)) {
		return;
	}

	if (g_radio.api->set_channel != NULL) {
		(void)g_radio.api->set_channel(g_radio.dev, 11u);
	}
	if (g_radio.api->start != NULL) {
		(void)g_radio.api->start(g_radio.dev);
	}
	if (g_radio.api->cca != NULL) {
		(void)g_radio.api->cca(g_radio.dev);
	}
	(void)zb_radio_rssi_get();
	(void)zb_radio_submit_tx(smoke_psdu, ARRAY_SIZE(smoke_psdu));
	LOG_INF("zigbee radio smoke: init/channel/cca/tx ok");
}

void zb_radio_reset(void)
{
	zb_radio_init();
}

void zb_radio_trx_switch(u8 mode, u8 phy_chn)
{
	int ret;
	u8 logical_chn;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL)) {
		return;
	}

	if (mode == RF_MODE_OFF) {
		if (g_radio.api->stop != NULL) {
			ret = g_radio.api->stop(g_radio.dev);
			if ((ret < 0) && (ret != -EALREADY)) {
				return;
			}
		}

		g_radio.trx_state = RF_MODE_OFF;
		return;
	}

	logical_chn = zb_radio_logical_from_phy_offset(phy_chn);
	if (g_radio.api->set_channel != NULL) {
		ret = g_radio.api->set_channel(g_radio.dev, logical_chn);
		if ((ret < 0) && (ret != -EALREADY)) {
			return;
		}
	}

	if (g_radio.api->start != NULL) {
		ret = g_radio.api->start(g_radio.dev);
		if ((ret < 0) && (ret != -EALREADY)) {
			return;
		}
	}

	g_radio.trx_state = mode;
}

void zb_radio_trx_off_auto_mode(void)
{
	int ret;

	if (g_radio.trx_state == RF_MODE_AUTO) {
		if ((g_radio.dev != NULL) && (g_radio.api != NULL) &&
		    (g_radio.api->stop != NULL)) {
			ret = g_radio.api->stop(g_radio.dev);
			if ((ret < 0) && (ret != -EALREADY)) {
				return;
			}
		}

		g_radio.trx_state = RF_MODE_OFF;
	}
}

void zb_radio_tx_power_set(u8 level)
{
	g_radio.tx_power = level;
}

s8 zb_radio_rssi_get(void)
{
	int ret;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->cca == NULL)) {
		return -110;
	}

	ret = g_radio.api->cca(g_radio.dev);
	if (ret == 0) {
		return -100;
	}

	if (ret == -EBUSY) {
		return -40;
	}

	return -110;
}

void zb_radio_tx_start(u8 *tx_buf)
{
	const uint8_t *psdu;
	uint8_t psdu_len;
	uint8_t dma_len;
	int ret;

	atomic_set(&g_radio.tx_done, 0);

	if (tx_buf == NULL) {
		return;
	}

	dma_len = (uint8_t)(tx_buf[0] + 4U);
	if (zb_radio_extract_psdu(tx_buf, dma_len, &psdu, &psdu_len) < 0) {
		return;
	}

	ret = zb_radio_submit_tx(psdu, psdu_len);
	if (ret == 0) {
		atomic_set(&g_radio.tx_done, 1);
		rf_tx_irq_handler();
	}
}

static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len)
{
	struct net_buf frag = {
		.data = (uint8_t *)psdu,
		.len = psdu_len,
	};

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->tx == NULL)) {
		return -ENODEV;
	}

	if ((psdu == NULL) || (psdu_len == 0U)) {
		return -EINVAL;
	}

	return g_radio.api->tx(g_radio.dev, IEEE802154_TX_MODE_DIRECT, NULL, &frag);
}

u8 zb_radio_tx_done_get(void)
{
	return (u8)atomic_get(&g_radio.tx_done);
}

void zb_radio_tx_done_clear(void)
{
	atomic_set(&g_radio.tx_done, 0);
}

u8 zb_radio_rx_done_get(void)
{
	return (u8)atomic_get(&g_radio.rx_done);
}

void zb_radio_rx_done_clear(void)
{
	atomic_set(&g_radio.rx_done, 0);
}

u8 zb_radio_trx_state_get(void)
{
	return g_radio.trx_state;
}

void zb_radio_rx_buf_set(u8 *addr)
{
	if (addr == NULL) {
		return;
	}

	if ((g_radio.rx_target != NULL) && (addr != g_radio.rx_target) &&
	    (g_radio.rx_next == NULL)) {
		g_radio.rx_next = addr;
		return;
	}

	g_radio.rx_target = addr;
	if (g_radio.rx_next == addr) {
		g_radio.rx_next = NULL;
	}
}

u8 *zb_radio_next_rx_buf_get(void)
{
	if ((g_radio.rx_next != NULL) && (g_radio.rx_next != g_radio.rx_target)) {
		return g_radio.rx_next;
	}

	return NULL;
}

u8 zb_radio_pkt_rssi_get(const u8 *p)
{
	ARG_UNUSED(p);
	return g_radio.last_rx_rssi_raw;
}

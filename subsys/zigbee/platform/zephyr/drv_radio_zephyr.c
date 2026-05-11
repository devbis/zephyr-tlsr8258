/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_radio.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

struct zb_radio_ctx {
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	u8 *rx_target;
	u8 rx_shadow[256];
	atomic_t tx_done;
	atomic_t rx_done;
	u8 trx_state;
	u8 tx_power;
};

static struct zb_radio_ctx g_radio;

void zb_radio_init(void)
{
	memset(&g_radio, 0, sizeof(g_radio));
	g_radio.rx_target = g_radio.rx_shadow;
	g_radio.trx_state = RF_MODE_OFF;
	g_radio.tx_power = ZB_DEFAULT_TX_POWER_IDX;
}

void zb_radio_reset(void)
{
	zb_radio_init();
}

void zb_radio_trx_switch(u8 mode, u8 phy_chn)
{
	ARG_UNUSED(phy_chn);
	g_radio.trx_state = mode;
}

void zb_radio_trx_off_auto_mode(void)
{
	if (g_radio.trx_state == RF_MODE_AUTO) {
		g_radio.trx_state = RF_MODE_OFF;
	}
}

void zb_radio_tx_power_set(u8 level)
{
	g_radio.tx_power = level;
}

s8 zb_radio_rssi_get(void)
{
	return -80;
}

void zb_radio_tx_start(u8 *tx_buf)
{
	ARG_UNUSED(tx_buf);
	atomic_set(&g_radio.tx_done, 1);
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
	g_radio.rx_target = (addr != NULL) ? addr : g_radio.rx_shadow;
}

u8 *zb_radio_next_rx_buf_get(void)
{
	return (g_radio.rx_target != NULL) ? g_radio.rx_target : g_radio.rx_shadow;
}

u8 zb_radio_pkt_rssi_get(const u8 *p)
{
	ARG_UNUSED(p);
	return 110u;
}

/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_radio.h"
#include "drv_radio_map.h"
#include "zb_radio_smoke.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <zephyr/zigbee/zb_radio_port.h>

LOG_MODULE_REGISTER(zigbee_radio_zephyr, CONFIG_ZIGBEE_LOG_LEVEL);

#define ZB_RADIO_RX_RING_DEPTH         2U
#define ZB_RADIO_RX_BUF_SIZE           136U
#define ZB_RADIO_CCA_BUSY_RSSI_DBM     (-60)
#define ZB_RADIO_CCA_IDLE_RSSI_DBM     (-96)
#define ZB_RADIO_RSSI_FALLBACK_DBM     (-110)

struct zb_radio_ctx {
	const struct device *dev;
	const struct ieee802154_radio_api *api;
	u8 *rx_target;
	u8 *rx_next;
	u8 rx_ring[ZB_RADIO_RX_RING_DEPTH][ZB_RADIO_RX_BUF_SIZE];
	atomic_t started;
	atomic_t tx_done;
	atomic_t rx_done;
	atomic_t last_rx_rssi_valid;
	atomic_t tx_attempts;
	atomic_t tx_success;
	atomic_t tx_failures;
	atomic_t rx_irq_count;
	atomic_t rx_accept_count;
	atomic_t rx_drop_count;
	u8 last_rx_rssi_raw;
	s8 last_rx_rssi_dbm;
	u8 current_channel;
	u8 last_rx_len;
	u8 last_tx_len;
	u8 last_error;
	u8 trx_state;
	u8 tx_power;
};

static struct zb_radio_ctx g_radio;

static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len);
static int zb_radio_extract_rx_psdu(const uint8_t *dma, uint8_t dma_len,
				      const uint8_t **psdu, uint8_t *psdu_len);
static int zb_radio_process_rx_frame(const uint8_t *dma, uint8_t dma_len, int8_t rssi_dbm);
extern void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi);
extern void rf_rx_irq_handler(void);
extern void rf_tx_irq_handler(void);
extern u8 *rf_rxBuf;

static void zb_radio_set_promiscuous(bool enable)
{
	struct ieee802154_config config = {
		.promiscuous = enable,
	};
	int ret;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->configure == NULL)) {
		return;
	}

	ret = g_radio.api->configure(g_radio.dev, IEEE802154_CONFIG_PROMISCUOUS, &config);
	if ((ret < 0) && (ret != -ENOTSUP)) {
		LOG_WRN("zigbee radio promiscuous=%u failed (rc=%d)", enable ? 1U : 0U, ret);
	}
}

static void zb_radio_set_error(u8 err)
{
	g_radio.last_error = err;
}

static int zb_radio_start_impl(u8 channel)
{
	int ret;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NOT_READY);
		return -ENODEV;
	}

	ret = zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_RX, channel);
	if (ret < 0) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_START);
		return ret;
	}

	zb_radio_set_promiscuous(true);

	g_radio.current_channel = channel;
	g_radio.trx_state = RF_MODE_RX;
	atomic_set(&g_radio.started, 1);
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	return 0;
}

static u8 *zb_radio_ring_alternate_buf(const u8 *current)
{
	if (current == g_radio.rx_ring[0]) {
		return g_radio.rx_ring[1];
	}

	if (current == g_radio.rx_ring[1]) {
		return g_radio.rx_ring[0];
	}

	if (current != g_radio.rx_ring[0]) {
		return g_radio.rx_ring[0];
	}

	return g_radio.rx_ring[1];
}

static void zb_radio_rx_ring_prime(const u8 *current)
{
	g_radio.rx_next = zb_radio_ring_alternate_buf(current);
}

static bool zb_radio_live_rssi_sample(s8 *rssi)
{
	int ret;

	if ((rssi == NULL) || (g_radio.dev == NULL) || (g_radio.api == NULL) ||
	    (g_radio.api->cca == NULL)) {
		return false;
	}

	ret = g_radio.api->cca(g_radio.dev);
	if (ret == 0) {
		*rssi = ZB_RADIO_CCA_IDLE_RSSI_DBM;
		return true;
	}

	if ((ret == -EBUSY) || (ret > 0)) {
		*rssi = ZB_RADIO_CCA_BUSY_RSSI_DBM;
		return true;
	}

	LOG_DBG("CCA sample unavailable (rc=%d), using RSSI fallback", ret);
	return false;
}

static int zb_radio_on_rx_sink(const struct tlsr8258_rx_frame_view *frame)
{
	int16_t rssi_clamped;
	const uint8_t *rx_dma;
	uint8_t rx_len;
	int8_t rssi_dbm;
	int rc;

	atomic_inc(&g_radio.rx_irq_count);

	if (frame == NULL) {
		atomic_inc(&g_radio.rx_drop_count);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_RX);
		return -EINVAL;
	}

	rx_dma = frame->dma;
	rx_len = frame->len;
	rssi_dbm = frame->rssi_dbm;

	if ((rx_dma == NULL) || (rx_len == 0U)) {
		atomic_inc(&g_radio.rx_drop_count);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_RX);
		LOG_DBG("RX callback ignored invalid frame (buf=%p len=%u)", rx_dma, rx_len);
		return -EINVAL;
	}

	rc = zb_radio_process_rx_frame(rx_dma, rx_len, rssi_dbm);
	if (rc < 0) {
		atomic_inc(&g_radio.rx_drop_count);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_RX);
		if (rc == -EINVAL) {
			LOG_WRN("RX sink rejected invalid frame (len=%u)", rx_len);
		} else {
			LOG_DBG("RX frame dropped: legacy path unavailable (rc=%d len=%u)", rc, rx_len);
		}
		return rc;
	}

	rssi_clamped = CLAMP((int16_t)rssi_dbm, -110, 17);
	g_radio.last_rx_rssi_raw = (uint8_t)(rssi_clamped + 110);
	g_radio.last_rx_rssi_dbm = (s8)rssi_clamped;
	g_radio.last_rx_len = rx_len;
	atomic_set(&g_radio.last_rx_rssi_valid, 1);
	atomic_inc(&g_radio.rx_accept_count);
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	return 0;
}

static int zb_radio_process_rx_frame(const uint8_t *dma, uint8_t dma_len, int8_t rssi_dbm)
{
	const uint8_t *psdu = NULL;
	uint8_t psdu_len = 0U;
	u8 *saved_rx_buf;

	if ((dma == NULL) || (dma_len == 0U)) {
		return -EINVAL;
	}

	atomic_set(&g_radio.rx_done, 1);
	if (zb_radio_extract_rx_psdu(dma, dma_len, &psdu, &psdu_len) == 0) {
		zb_macDataRecvHandler((u8 *)dma, (u8 *)psdu, psdu_len, 0U, 0U, rssi_dbm);
		return 0;
	}

	if (rf_rxBuf == NULL) {
		return -ENODATA;
	}

	saved_rx_buf = rf_rxBuf;
	rf_rxBuf = (u8 *)dma;
	rf_rx_irq_handler();
	rf_rxBuf = saved_rx_buf;
	return 0;
}

static int zb_radio_extract_rx_psdu(const uint8_t *dma, uint8_t dma_len,
				      const uint8_t **psdu, uint8_t *psdu_len)
{
	uint8_t payload_len;
	uint8_t available_len;

	if ((dma == NULL) || (psdu == NULL) || (psdu_len == NULL) || (dma_len < 7U)) {
		return -EINVAL;
	}

	payload_len = dma[4];
	if (payload_len < 2U) {
		return -EINVAL;
	}

	available_len = (uint8_t)(dma_len - 5U);
	if (payload_len > available_len) {
		return -EINVAL;
	}

	/*
	 * Legacy Zigbee RX path expects the incoming PSDU to still include the
	 * FCS bytes and accounts for them internally.
	 */
	*psdu = &dma[5];
	*psdu_len = payload_len;
	return 0;
}

void zb_radio_init(void)
{
	const struct device *dev = NULL;
	const struct ieee802154_radio_api *api = NULL;
	int ret;

	memset(&g_radio, 0, sizeof(g_radio));
	g_radio.trx_state = RF_MODE_OFF;
	g_radio.tx_power = ZB_DEFAULT_TX_POWER_IDX;
	g_radio.rx_target = g_radio.rx_ring[0];
	g_radio.rx_next = g_radio.rx_ring[1];
	g_radio.last_rx_rssi_raw = 0u;
	g_radio.last_rx_rssi_dbm = ZB_RADIO_RSSI_FALLBACK_DBM;
	g_radio.current_channel = 0u;
	g_radio.last_rx_len = 0u;
	g_radio.last_tx_len = 0u;
	g_radio.last_error = ZB_PLATFORM_RADIO_ERR_NOT_READY;
	atomic_set(&g_radio.last_rx_rssi_valid, 0);
	atomic_set(&g_radio.started, 0);

	ret = zb_radio_port_radio_get(&dev, &api);
	if (ret < 0) {
		LOG_WRN("zigbee radio device not ready");
		return;
	}

	g_radio.dev = dev;
	g_radio.api = api;
	if (g_radio.api == NULL) {
		LOG_WRN("zigbee radio API unavailable");
		return;
	}
	zb_radio_port_register_rx_sink(zb_radio_on_rx_sink);
}

bool zb_radio_is_ready(void)
{
	return (g_radio.dev != NULL) && (g_radio.api != NULL);
}

void zb_radio_smoke_probe(void)
{
	int ret;
	const char *step = "init";

	/* Probe driver hooks directly before MAC state/configuration is established. */
	zb_radio_init();
	if ((g_radio.dev == NULL) || (g_radio.api == NULL)) {
		ret = -ENODEV;
		LOG_ERR("zigbee radio smoke failed at %s (rc=%d)", step, ret);
		return;
	}

	step = "start";
	ret = zb_platform_radio_start_on_channel(11u);
	if (ret < 0) {
		LOG_WRN("zigbee radio smoke failed at %s (rc=%d)", step, ret);
		return;
	}

	step = "cca";
	if (g_radio.api->cca == NULL) {
		ret = -ENOTSUP;
		LOG_ERR("zigbee radio smoke failed at %s (rc=%d)", step, ret);
		return;
	}

	ret = g_radio.api->cca(g_radio.dev);
	if ((ret < 0) && (ret != -EBUSY)) {
		LOG_WRN("zigbee radio smoke failed at %s (rc=%d)", step, ret);
		return;
	}

	(void)zb_radio_rssi_get();

	step = "tx";
	ret = zb_platform_radio_send_beacon_request();
	if (ret < 0) {
		LOG_WRN("zigbee radio smoke failed at %s (rc=%d)", step, ret);
		return;
	}

	LOG_INF("zigbee radio smoke: init/channel/cca/beacon-req ok");
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
		ret = zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_OFF, g_radio.current_channel);
		if (ret < 0) {
			zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_STOP);
			return;
		}

		g_radio.trx_state = RF_MODE_OFF;
		atomic_set(&g_radio.started, 0);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
		return;
	}

	logical_chn = zb_radio_logical_from_phy_offset(phy_chn);
	ret = zb_radio_port_set_trx_state(
		(mode == RF_MODE_TX) ? ZB_RADIO_PORT_TRX_TX :
		(mode == RF_MODE_AUTO) ? ZB_RADIO_PORT_TRX_AUTO :
				       ZB_RADIO_PORT_TRX_RX,
		logical_chn);
	if (ret < 0) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_START);
		return;
	}

	zb_radio_set_promiscuous(true);
	g_radio.current_channel = logical_chn;
	atomic_set(&g_radio.started, 1);
	g_radio.trx_state = mode;
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
}

void zb_radio_trx_off_auto_mode(void)
{
	int ret;

	if (g_radio.trx_state == RF_MODE_AUTO) {
		ret = zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_OFF, g_radio.current_channel);
		if (ret < 0) {
			LOG_WRN("auto-mode stop failed (rc=%d)", ret);
			return;
		}

		g_radio.trx_state = RF_MODE_OFF;
		atomic_set(&g_radio.started, 0);
	}
}

void zb_radio_tx_power_set(u8 level)
{
	g_radio.tx_power = level;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->set_txpower == NULL)) {
		return;
	}

	(void)g_radio.api->set_txpower(g_radio.dev, zb_radio_tx_dbm_from_level(level));
}

s8 zb_radio_rssi_get(void)
{
	s8 live_rssi;

	if (zb_radio_live_rssi_sample(&live_rssi)) {
		return live_rssi;
	}

	if (atomic_get(&g_radio.last_rx_rssi_valid) == 0) {
		return ZB_RADIO_RSSI_FALLBACK_DBM;
	}

	return (s8)((int16_t)g_radio.last_rx_rssi_raw - 110);
}

void zb_radio_tx_start(u8 *tx_buf)
{
	const uint8_t *psdu;
	uint8_t psdu_len;
	uint8_t dma_len;
	int ret;

	atomic_set(&g_radio.tx_done, 0);

	if (tx_buf == NULL) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_TX);
		LOG_WRN("TX start rejected: null DMA buffer");
		return;
	}

	dma_len = (uint8_t)(tx_buf[0] + 4U);
	if (zb_radio_extract_psdu(tx_buf, dma_len, &psdu, &psdu_len) < 0) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_TX);
		LOG_WRN("TX start rejected: invalid DMA payload (dma_len=%u)", dma_len);
		return;
	}

	ret = zb_radio_submit_tx(psdu, psdu_len);
	if (ret < 0) {
		LOG_WRN("TX submit failed (rc=%d len=%u)", ret, psdu_len);
		return;
	}

	atomic_set(&g_radio.tx_done, 1);
	rf_tx_irq_handler();
}

static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len)
{
	struct net_buf frag = {
		.data = (uint8_t *)psdu,
		.len = psdu_len,
	};
	int ret;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->tx == NULL)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NOT_READY);
		LOG_WRN("TX unavailable: radio tx API not ready");
		return -ENODEV;
	}

	if ((psdu == NULL) || (psdu_len == 0U)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_TX);
		LOG_WRN("TX rejected: invalid PSDU");
		return -EINVAL;
	}

	g_radio.last_tx_len = psdu_len;
	atomic_inc(&g_radio.tx_attempts);
	ret = g_radio.api->tx(g_radio.dev, IEEE802154_TX_MODE_CCA, NULL, &frag);
	if (ret < 0) {
		atomic_inc(&g_radio.tx_failures);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_TX_SUBMIT);
		return ret;
	}

	atomic_inc(&g_radio.tx_success);
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	return 0;
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
		LOG_DBG("RX buffer set ignored: null pointer");
		return;
	}

	g_radio.rx_target = addr;
	zb_radio_rx_ring_prime(addr);
}

u8 *zb_radio_next_rx_buf_get(void)
{
	if ((g_radio.rx_next == NULL) || (g_radio.rx_next == g_radio.rx_target)) {
		zb_radio_rx_ring_prime(g_radio.rx_target);
	}

	return g_radio.rx_next;
}

u8 zb_radio_pkt_rssi_get(const u8 *p)
{
	ARG_UNUSED(p);

	if (atomic_get(&g_radio.last_rx_rssi_valid) == 0) {
		return 0;
	}

	return g_radio.last_rx_rssi_raw;
}

int zb_platform_radio_diag_get(struct zb_platform_radio_diag_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return -EINVAL;
	}

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->ready = zb_radio_is_ready();
	snapshot->started = (atomic_get(&g_radio.started) != 0);
	snapshot->channel = g_radio.current_channel;
	snapshot->trx_state = g_radio.trx_state;
	snapshot->tx_power = g_radio.tx_power;
	snapshot->last_rx_len = g_radio.last_rx_len;
	snapshot->last_tx_len = g_radio.last_tx_len;
	snapshot->last_error = g_radio.last_error;
	snapshot->last_rx_rssi_dbm = g_radio.last_rx_rssi_dbm;
	snapshot->tx_attempts = (uint32_t)atomic_get(&g_radio.tx_attempts);
	snapshot->tx_success = (uint32_t)atomic_get(&g_radio.tx_success);
	snapshot->tx_failures = (uint32_t)atomic_get(&g_radio.tx_failures);
	snapshot->rx_irq_count = (uint32_t)atomic_get(&g_radio.rx_irq_count);
	snapshot->rx_accept_count = (uint32_t)atomic_get(&g_radio.rx_accept_count);
	snapshot->rx_drop_count = (uint32_t)atomic_get(&g_radio.rx_drop_count);

	return 0;
}

int zb_platform_radio_start_on_channel(uint8_t channel)
{
	if ((channel < 11U) || (channel > 26U)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_SET_CHANNEL);
		return -EINVAL;
	}

	if (!zb_radio_is_ready()) {
		zb_radio_init();
	}

	return zb_radio_start_impl(channel);
}

int zb_platform_radio_stop(void)
{
	int ret;

	if (!zb_radio_is_ready()) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NOT_READY);
		return -ENODEV;
	}

	ret = zb_radio_port_set_trx_state(ZB_RADIO_PORT_TRX_OFF, g_radio.current_channel);
	if (ret < 0) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_STOP);
		return ret;
	}

	atomic_set(&g_radio.started, 0);
	g_radio.trx_state = RF_MODE_OFF;
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	return 0;
}

int zb_platform_radio_send_raw_psdu(const uint8_t *psdu, uint8_t psdu_len)
{
	return zb_radio_submit_tx(psdu, psdu_len);
}

int zb_platform_radio_send_beacon_request(void)
{
	static uint8_t beacon_seq;
	uint8_t beacon_req[] = {
		0x03, 0x08, beacon_seq++,
		0xff, 0xff,
		0xff, 0xff,
		0x07,
	};

	return zb_radio_submit_tx(beacon_req, ARRAY_SIZE(beacon_req));
}

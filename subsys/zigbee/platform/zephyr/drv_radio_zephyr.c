/* SPDX-License-Identifier: Apache-2.0 */

#include "drv_radio.h"
#include "drv_radio_map.h"
#include "zb_radio_smoke.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
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
	/* Legacy compatibility-facing RX buffer API only; not used by the normal sink pipeline. */
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
	u8 last_tx_seq;
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
extern void zb_macDataSendHandler(void);
extern void mac_trxTask(void *arg);
#if defined(ZB_ROUTER_ROLE)
extern u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 update);
#endif
#include "mac/includes/mac_trx_api.h"
#include "zb_common.h"

/*
 * A router is an always-on 802.15.4 receiver.  The vendor MAC still uses
 * TRX_OFF as a short CSMA/turnaround state, so letting that request reach the
 * Zephyr driver after association can leave the hardware stopped while the
 * stack remains joined.  Keep the guard based on the live PIB tuple rather
 * than only g_zbNwkCtx.joined: the short address is committed before the
 * deferred NWK join-confirm path runs.
 */
static bool zb_radio_keep_router_rx_on_idle(void)
{
#if defined(CONFIG_ZIGBEE_ROUTER) || defined(CONFIG_ZIGBEE_COORDINATOR)
	/*
	 * A router is an always-on FFD during commissioning as well as after
	 * association.  PAN ID and short address are intentionally invalid while
	 * scanning/joining, so using them as an additional gate turns the first
	 * TRX_OFF after a Beacon Request into a permanently stopped radio.  The
	 * RX-on-when-idle PIB bit is the complete policy decision here; the
	 * address filter remains permissive until the join assigns the tuple.
	 */
	return g_zbMacPib.rxOnWhenIdle != 0u;
#else
	return false;
#endif
}

static bool zb_radio_psdu_is_ack(const uint8_t *psdu, uint8_t psdu_len)
{
	return (psdu != NULL) && (psdu_len >= 3U) && ((psdu[0] & 0x07U) == 0x02U);
}

#if defined(ZB_ROUTER_ROLE)
/*
 * The vendor RX IRQ marks an indirect transaction READY before passing a
 * data request to the MAC.  The Zephyr radio sink bypasses that IRQ path, so
 * perform the same transition at the port boundary.
 */
static void zb_radio_mark_pending_data(const uint8_t *psdu, uint8_t psdu_len)
{
	uint16_t frame_ctrl;
	uint8_t dst_mode;
	uint8_t src_mode;
	uint8_t src_len;
	uint8_t header_len;
	uint8_t src_offset;
	const uint8_t *src_addr;

	if ((psdu == NULL) || (psdu_len < 3U)) {
		return;
	}

	frame_ctrl = (uint16_t)psdu[0] | ((uint16_t)psdu[1] << 8);
	if (((frame_ctrl & MAC_FCF_FRAME_TYPE_MASK) >> MAC_FCF_FRAME_TYPE_POS) !=
	    MAC_FRAME_TYPE_COMMAND) {
		return;
	}

	header_len = tl_zbMacHdrSize(frame_ctrl);
	if ((header_len >= psdu_len) || (psdu[header_len] != MAC_CMD_DATA_REQUEST)) {
		return;
	}

	dst_mode = (uint8_t)((frame_ctrl & MAC_FCF_DST_ADDR_MODE_MASK) >>
				     MAC_FCF_DST_ADDR_MODE_POS);
	src_mode = (uint8_t)((frame_ctrl & MAC_FCF_SRC_ADDR_MODE_MASK) >>
				     MAC_FCF_SRC_ADDR_MODE_POS);
	if (src_mode == ADDR_MODE_SHORT) {
		src_len = MAC_SHORT_ADDR_FIELD_LEN;
	} else if (src_mode == ADDR_MODE_EXT) {
		src_len = MAC_EXT_ADDR_FIELD_LEN;
	} else {
		return;
	}

	src_offset = MAC_FCF_FIELD_LEN + MAC_SEQ_NUM_FIELD_LEN;
	if (dst_mode == ADDR_MODE_SHORT) {
		src_offset += MAC_PAN_ID_FIELD_LEN + MAC_SHORT_ADDR_FIELD_LEN;
	} else if (dst_mode == ADDR_MODE_EXT) {
		src_offset += MAC_PAN_ID_FIELD_LEN + MAC_EXT_ADDR_FIELD_LEN;
	} else if (dst_mode != ADDR_MODE_NONE) {
		return;
	}

	if ((frame_ctrl & MAC_FCF_INTRA_PAN_MASK) == 0U) {
		src_offset += MAC_PAN_ID_FIELD_LEN;
	}
	if ((src_offset > psdu_len) || (src_len > (psdu_len - src_offset))) {
		return;
	}

	src_addr = &psdu[src_offset];
	(void)tl_zbMacPendingDataCheck(src_mode, (u8 *)src_addr, 1U);
}
#endif

/*
 * Deferred TX completion: posted from the user task queue so it
 * runs AFTER mac_csmaStart has finished arming its TX-IRQ wait
 * timer (timer_evt_state=1). Calling zb_macDataSendHandler()
 * synchronously inside zb_radio_submit_tx() — which itself runs
 * inside mac_csmaStart's drv_disable_irq() critical section, before
 * the timer is armed — caused the SEND_SUCC handler's
 * `timer_evt_state_get() == 1` guard to fail, leaving the state
 * machine stranded in MAC_TX_UNDERWAY. mac_waitTxIrqCb then timed
 * out as SEND_FAIL → MAC_TX_ABORTED (0x1d).
 *
 * Also clears TX_BUSY here — the vendor radio IRQ that normally does
 * this (rf_tx_irq_handler) never runs in the Zephyr port. Leaving the
 * flag set causes the next rf_performCCA() to return PHY_CCA_BUSY
 * and pushes the second TX into the CSMA-retry path, where it never
 * confirms.
 *
 * When the libzigbee MAC has transitioned to MAC_TX_WAIT_ACK (ack-
 * required frame), also synthesize MAC_TX_EV_ACK_RECV — Zephyr's
 * api->tx consumed the radio's ACK itself, so the vendor MAC would
 * otherwise sit waiting for the ACK and time out as MAC_TX_ABORTED.
 */
/*
 * The MAC waits for an acknowledgement carrying the sequence number of the
 * frame it queued, which is the third byte of the frame the radio is about to
 * send (tl_zbMacTx() reads the same byte into mac_trx_vars.ackSeqNum).
 */
void zb_radio_note_tx_frame(const u8 *frame)
{
	g_radio.last_tx_seq = (frame != NULL) ? frame[2] : 0U;
}

static void zb_radio_tx_complete_deferred(void *arg)
{
	(void)arg;
	rf_busyFlag &= (u8)~TX_BUSY;
	zb_macDataSendHandler();
#if defined(CONFIG_ZIGBEE_ROUTER) || defined(CONFIG_ZIGBEE_COORDINATOR) || \
	defined(CONFIG_ZIGBEE_ED)
	/*
	 * ACK synthesis is specific to the libzigbee MAC state machine
	 * (mac_getTrxState/mac_trxTask live in mac_trx.c/mac.c). Both the router
	 * and the libzigbee-based ED use that MAC and, for ack-required frames
	 * (association-request, data-request poll, ...), sit in MAC_TX_WAIT_ACK
	 * until MAC_TX_EV_ACK_RECV — Zephyr's api->tx already consumed the radio
	 * ACK, so synthesize it here or the TX state machine wedges (assoc-req
	 * retries forever, the poll never leaves the queue). The full-stack ED
	 * follows the same MAC path, so this applies to both FFD and ED roles.
	 */
	if (mac_getTrxState() == MAC_TX_WAIT_ACK) {
		/*
		 * Feed it through the handler the radio uses for real frames
		 * rather than the MAC's internal task: the vendor keeps that
		 * task private, and the archive agrees (mac_trxTask is a local
		 * symbol there). The handler only reads the frame control byte
		 * and the sequence number, and matches the latter against the
		 * frame still awaiting acknowledgement.
		 */
		u8 ack_frame[3] = {
			0x02U, /* frame type ACK, as zb_radio_psdu_is_ack() reads it */
			0U,
			g_radio.last_tx_seq,
		};

		zb_macDataRecvHandler(NULL, ack_frame, sizeof(ack_frame), 1U, 0U, 0);
	}
#endif
}

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

	/*
	 * Keep hardware destination filtering enabled. During join we still
	 * receive broadcast beacons and IEEE-addressed ASSOC_RESP frames, but
	 * we stop queueing unrelated unicast traffic from other nodes on the
	 * channel, which otherwise starves the MAC task queue long enough for
	 * the association-response wait timer to expire first.
	 */
	zb_radio_set_promiscuous(false);

	g_radio.current_channel = channel;
	g_radio.trx_state = RF_MODE_RX;
	atomic_set(&g_radio.started, 1);
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	return 0;
}

static u8 *zb_radio_ring_alternate_buf(const u8 *current)
{
	return (current == g_radio.rx_ring[0]) ? g_radio.rx_ring[1] : g_radio.rx_ring[0];
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

static int zb_radio_on_rx_sink(const struct zb_radio_rx_frame_view *frame)
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
	uint8_t mac_len;
	u8 ack_pkt;
	u8 *rx_buf = g_radio.rx_target;

	if ((dma == NULL) || (dma_len == 0U)) {
		return -EINVAL;
	}

	atomic_set(&g_radio.rx_done, 1);
	if (rx_buf != NULL) {
		if (dma_len > ZB_RADIO_RX_BUF_SIZE) {
			return -EINVAL;
		}

		memcpy(rx_buf, dma, dma_len);
		dma = rx_buf;
		/*
		 * Rotate to the alternate ring slot before invoking the MAC
		 * RX handler. zb_macDataRecvHandler queues the buf for
		 * mac_rxDataParse via tl_zbTaskPost and stashes a pointer
		 * into the rx ring as meta->payload. If the next radio RX
		 * fires before the queued parse runs, it would memcpy into
		 * the same slot and clobber the still-pending frame. Two
		 * slots is enough for the typical "ASSOC_RSP + TRANSPORT_KEY
		 * back-to-back" delivery the host_socket_coordinator sends to
		 * a rx-on router.
		 */
		g_radio.rx_target = zb_radio_ring_alternate_buf(rx_buf);
	}

	if (zb_radio_extract_rx_psdu(dma, dma_len, &psdu, &psdu_len) < 0) {
		return -EINVAL;
	}
	/*
	 * zb_radio_extract_rx_psdu() returns the DMA length field, which already
	 * counts the two FCS bytes the MAC entry point strips.  Adding them again
	 * left every frame two bytes too long, which only shows up where the
	 * length is authenticated: the APS Transport-Key failed its CCM check.
	 */
	mac_len = psdu_len;

#if defined(ZB_ROUTER_ROLE)
	zb_radio_mark_pending_data(psdu, psdu_len);
#endif
	ack_pkt = zb_radio_psdu_is_ack(psdu, psdu_len) ? 1U : 0U;
	zb_macDataRecvHandler((u8 *)dma, (u8 *)psdu, mac_len, ack_pkt, 0U, rssi_dbm);
	return 0;
}

static int zb_radio_extract_rx_psdu(const uint8_t *dma, uint8_t dma_len,
				      const uint8_t **psdu, uint8_t *psdu_len)
{
	uint8_t payload_len;
	uint8_t fallback_len;
	uint8_t available_len;

	if ((dma == NULL) || (psdu == NULL) || (psdu_len == NULL) || (dma_len < 7U)) {
		return -EINVAL;
	}

	payload_len = dma[4];
	available_len = (uint8_t)(dma_len - 5U);
	if ((payload_len < 2U) || (payload_len > available_len)) {
		if (dma[0] < 9U) {
			return -EINVAL;
		}

		fallback_len = (uint8_t)(dma[0] - 9U);
		if ((fallback_len < 2U) || (fallback_len > available_len)) {
			return -EINVAL;
		}

		payload_len = fallback_len;
	}

	if (payload_len > available_len) {
		return -EINVAL;
	}

	/* The legacy MAC callback receives a length including FCS and removes
	 * those bytes before parsing the frame. */
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
	g_radio.last_tx_seq = 0u;
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
	if ((api->set_channel == NULL) || (api->start == NULL) ||
	    (api->stop == NULL) || (api->tx == NULL) || (api->filter == NULL)) {
		LOG_ERR("Zigbee radio lacks channel, start, stop, tx or filter operation");
		g_radio.dev = NULL;
		g_radio.api = NULL;
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
		if (zb_radio_keep_router_rx_on_idle()) {
			/*
			 * RF_MODE_OFF is used by the MAC immediately before a new
			 * CSMA attempt.  For an already joined router this is not an
			 * idle-power request; turning the PHY off creates the exact
			 * post-join deaf state seen on hardware.  Re-arm RX instead,
			 * including recovery if an earlier path already stopped it.
			 */
			logical_chn = zb_radio_logical_from_phy_offset(phy_chn);
			ret = zb_radio_start_impl(logical_chn);
			if ((ret < 0) && (ret != -EALREADY)) {
				zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_START);
				return;
			}
			g_radio.current_channel = logical_chn;
			g_radio.trx_state = RF_MODE_RX;
			atomic_set(&g_radio.started, 1);
			zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
			return;
		}

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

	zb_radio_set_promiscuous(false);
	g_radio.current_channel = logical_chn;
	atomic_set(&g_radio.started, 1);
	g_radio.trx_state = mode;
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
}

void zb_radio_trx_off_auto_mode(void)
{
	int ret;

	if (g_radio.trx_state == RF_MODE_AUTO) {
		if (zb_radio_keep_router_rx_on_idle()) {
			/* RX is continuous for an always-on router; do not stop it
			 * between the MAC's AUTO and the next TX/RX handoff. */
			g_radio.trx_state = RF_MODE_RX;
			return;
		}

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
}

static int zb_radio_submit_tx(const u8 *psdu, u8 psdu_len)
{
	struct net_if *iface;
	struct net_pkt *pkt;
	int ret;

	if ((g_radio.dev == NULL) || (g_radio.api == NULL) || (g_radio.api->tx == NULL)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NOT_READY);
		LOG_WRN("TX unavailable: radio tx API not ready");
		return -ENODEV;
	}

	if ((psdu == NULL) || (psdu_len == 0U) || (psdu_len > 125U)) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_INVALID_TX);
		LOG_WRN("TX rejected: invalid PSDU");
		return -EINVAL;
	}
	iface = net_if_lookup_by_dev(g_radio.dev);
	if (iface == NULL) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NOT_READY);
		return -ENODEV;
	}
	pkt = net_pkt_alloc_with_buffer(iface, psdu_len, NET_AF_UNSPEC, 0, K_NO_WAIT);
	if (pkt == NULL) {
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_TX_SUBMIT);
		return -ENOMEM;
	}
	if (net_pkt_write(pkt, psdu, psdu_len) < 0) {
		net_pkt_unref(pkt);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_TX_SUBMIT);
		return -ENOMEM;
	}
	/* libzigbee supplies the complete MAC header and performs its own security. */
	net_pkt_set_ieee802154_mac_hdr_rdy(pkt, true);
	net_pkt_set_ieee802154_frame_secured(pkt, (psdu[0] & BIT(3)) != 0U);

	g_radio.last_tx_len = psdu_len;
	/*
	 * DATA REQUEST frames must bypass CCA.  During the interview phase the
	 * coordinator retransmits transport-key frames continuously; CCA sees
	 * those transmissions as a busy channel and returns -EBUSY for every
	 * subsequent DATA REQ poll (polls 9-20), preventing the key delivery.
	 * Using DIRECT mode for all TX eliminates that race.
	 */
	enum ieee802154_tx_mode tx_mode = IEEE802154_TX_MODE_DIRECT;
	atomic_inc(&g_radio.tx_attempts);
	ret = g_radio.api->tx(g_radio.dev, tx_mode, pkt, pkt->frags);
	net_pkt_unref(pkt);
	if (ret < 0) {
		atomic_inc(&g_radio.tx_failures);
		zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_TX_SUBMIT);
		return ret;
	}

	atomic_inc(&g_radio.tx_success);
	zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_NONE);
	/*
	 * api->tx is synchronous, but the libzigbee MAC arms its TX-IRQ
	 * wait timer AFTER our submit returns. Defer the completion (which
	 * also synthesizes ACK_RECV when ack was required) until the next
	 * task-queue drain so the state machine has set timer_evt_state=1
	 * before SEND_SUCC fires.
	 */
	ret = tl_zbTxTaskPost(zb_radio_tx_complete_deferred, NULL);
	if (ret != RET_OK) {
		/*
		 * The frame is already on air, so failing the submit would make
		 * the MAC retransmit a duplicate and, worse, leave TX_BUSY set
		 * forever: only zb_radio_tx_complete_deferred clears it. Running
		 * the completion inline is not an option either -- it races the
		 * MAC's own timer arming, which is why it is deferred at all.
		 * Fall back to the general task queue: ordering against pending
		 * RX callbacks degrades, delivery does not.
		 */
		ret = tl_zbTaskPost(zb_radio_tx_complete_deferred, NULL);
		if (ret != RET_OK) {
			LOG_ERR("cannot queue radio TX completion (status=%d)", ret);
			zb_radio_set_error(ZB_PLATFORM_RADIO_ERR_TX_SUBMIT);
			return -ENOBUFS;
		}
		LOG_WRN("radio TX completion queue full, deferred to the general queue");
	}
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

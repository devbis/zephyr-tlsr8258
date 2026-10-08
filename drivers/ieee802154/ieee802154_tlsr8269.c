/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR826x IEEE 802.15.4 radio. Register sequences follow the vendor
 * RF_Init(), RF_ModeSet(), RF_TrxStateSet(), RF_TxPkt() and RF_rx_cfg() in
 * libdrivers_826x.a. The RX/TX interrupt flow and the ACK path follow
 * zigbee/mac/mac_phy.c for MCU_CORE_826x (rf_rx_irq_handler,
 * rf_tx_irq_handler, rf_mac_ack_build).
 *
 * Received frames are checked, filtered against the PAN, short and IEEE
 * address set through tlsr8269_radio_update_filters(), acknowledged when the
 * frame requests it, and queued for tlsr8269_radio_rx_poll(), which hands them
 * to the registered sink in the Zigbee L2 layout. The Zephyr network-stack
 * path (NET_L2_IEEE802154) is not implemented; it is only the raw-mode entry.
 */

#define DT_DRV_COMPAT telink_tlsr8269_zb

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8269.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <tlsr82xx/irq.h>

LOG_MODULE_REGISTER(ieee802154_tlsr8269, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

#define TLSR8269_REG8(addr)  (*(volatile uint8_t *)(addr))
#define TLSR8269_REG16(addr) (*(volatile uint16_t *)(addr))

#define TLSR8269_REG_RST0      TLSR8269_REG8(0x00800060u)
#define TLSR8269_FLD_RST0_RF   BIT(7)

#define TLSR8269_REG_ANA_ADDR  TLSR8269_REG8(0x008000b8u)
#define TLSR8269_REG_ANA_DATA  TLSR8269_REG8(0x008000b9u)
#define TLSR8269_REG_ANA_CTRL  TLSR8269_REG8(0x008000bau)
#define TLSR8269_FLD_ANA_BUSY  BIT(0)
#define TLSR8269_FLD_ANA_RW    BIT(5)
#define TLSR8269_FLD_ANA_CYC0  BIT(6)

#define TLSR8269_REG_RF_IRQ_MASK    TLSR8269_REG16(0x00800f1cu)
#define TLSR8269_REG_RF_IRQ_STATUS  TLSR8269_REG16(0x00800f20u)
#define TLSR8269_REG_RF_TRX_CMD     TLSR8269_REG8(0x00800f02u)
#define TLSR8269_REG_RF_LL_CTRL     TLSR8269_REG8(0x00800f16u)
#define TLSR8269_REG_RF_SRX_CTRL    TLSR8269_REG8(0x00800f03u)
#define TLSR8269_REG_RF_RX_MODE     TLSR8269_REG8(0x00800428u)
#define TLSR8269_REG_RF_FREQ        TLSR8269_REG16(0x008004d6u)
#define TLSR8269_REG_RF_AGC_BASE    0x00800480u

#define TLSR8269_REG_DMA_CHN_EN     TLSR8269_REG8(0x00800520u)
#define TLSR8269_REG_DMA_CHN_MSK    TLSR8269_REG8(0x00800521u)
#define TLSR8269_REG_DMA_TX_RDY0    TLSR8269_REG8(0x00800524u)
#define TLSR8269_REG_DMA_RF_RX_ADDR TLSR8269_REG16(0x00800508u)
#define TLSR8269_REG_DMA_RF_RX_SIZE TLSR8269_REG8(0x0080050au)
#define TLSR8269_REG_DMA_RF_RX_MODE TLSR8269_REG8(0x0080050bu)
#define TLSR8269_REG_DMA_RF_TX_ADDR TLSR8269_REG16(0x0080050cu)

#define TLSR8269_FLD_RF_IRQ_RX      BIT(0)
#define TLSR8269_FLD_RF_IRQ_TX      BIT(1)
#define TLSR8269_FLD_DMA_RF_RX      BIT(2)
#define TLSR8269_FLD_DMA_RF_TX      BIT(3)
#define TLSR8269_FLD_TRX_TX         BIT(4)
#define TLSR8269_FLD_TRX_RX         BIT(5)
#define TLSR8269_FLD_TRX_MASK       (BIT(4) | BIT(5))
#define TLSR8269_RF_MODE_RX_EN      BIT(0)
#define TLSR8269_RF_TRX_OFF_CODE    0x29u
#define TLSR8269_RF_BASE_MHZ        2400u
#define TLSR8269_RF_TX_RDY_DMA3     BIT(3)

#define TLSR8269_PHY_MAX_PSDU       127u
#define TLSR8269_FCS_LEN            2u
#define TLSR8269_RX_BUF_SIZE        144u
#define TLSR8269_RX_HDR_LEN         13u
#define TLSR8269_TX_HDR_LEN         5u
#define TLSR8269_TX_BUF_SIZE        (TLSR8269_TX_HDR_LEN + TLSR8269_PHY_MAX_PSDU)
#define TLSR8269_ACK_BUF_SIZE       12u
#define TLSR8269_RX_QUEUE_DEPTH     4u
#define TLSR8269_RX_FRAME_MAX       (TLSR8269_RX_FRAME_PSDU_OFFSET + TLSR8269_PHY_MAX_PSDU)
#define TLSR8269_TX_TIMEOUT_MS      10u
#define TLSR8269_ACK_TX_WAIT_US     120u
#define TLSR8269_ACK_TX_DONE_US     1000u

#define TLSR8269_CHANNEL_MIN        11u
#define TLSR8269_CHANNEL_MAX        26u

#define TLSR8269_FCF_ACK_REQ        BIT(5)
#define TLSR8269_FRAME_TYPE_ACK     0x02u
#define TLSR8269_ADDR_MODE_SHORT    0x02u
#define TLSR8269_ADDR_MODE_EXT      0x03u
#define TLSR8269_DMA_WINDOW_HI      0x80u

struct tlsr8269_rf_tbl {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

/* Vendor TblRFIni: analog writes (cmd 0xc8) and MMIO writes (cmd 0xc3). */
static const struct tlsr8269_rf_tbl tbl_rf_ini[] = {
	{0x0006u, 0x00u, 0xc8u}, {0x008fu, 0x38u, 0xc8u}, {0x00a2u, 0x2cu, 0xc8u},
	{0x00a0u, 0x28u, 0xc8u}, {0x0004u, 0x7cu, 0xc8u}, {0x00a7u, 0x61u, 0xc8u},
	{0x008du, 0x67u, 0xc8u}, {0x00acu, 0xa7u, 0xc8u}, {0x0081u, 0xd0u, 0xc8u},
	{0x04cau, 0x88u, 0xc3u}, {0x04cbu, 0x04u, 0xc3u}, {0x042du, 0x33u, 0xc3u},
	{0x043bu, 0xfcu, 0xc3u},
};

/* Vendor TblRFIni12M250K (12 MHz oscillator, ZigBee 250 kbit/s). */
static const struct tlsr8269_rf_tbl tbl_rf_ini_12m_250k[] = {
	{0x04ebu, 0xa0u, 0xc3u},
	{0x0099u, 0xb1u, 0xc8u},
	{0x0082u, 0x20u, 0xc8u},
	{0x009eu, 0xadu, 0xc8u},
};

/* Vendor TblRFZigbee250k. */
static const struct tlsr8269_rf_tbl tbl_rf_zigbee_250k[] = {
	{0x009eu, 0xadu, 0xc8u}, {0x00a3u, 0x10u, 0xc8u}, {0x00aau, 0x2au, 0xc8u},
	{0x0093u, 0x50u, 0xc8u}, {0x0400u, 0x03u, 0xc3u}, {0x0401u, 0x40u, 0xc3u},
	{0x0402u, 0x26u, 0xc3u}, {0x0404u, 0xc0u, 0xc3u}, {0x0405u, 0x04u, 0xc3u},
	{0x0408u, 0xc9u, 0xc3u}, {0x0409u, 0x8au, 0xc3u}, {0x040au, 0x11u, 0xc3u},
	{0x040bu, 0xf8u, 0xc3u}, {0x0420u, 0x90u, 0xc3u}, {0x0421u, 0x00u, 0xc3u},
	{0x0422u, 0x1au, 0xc3u}, {0x0424u, 0x52u, 0xc3u}, {0x0428u, 0xe0u, 0xc3u},
	{0x042bu, 0xf5u, 0xc3u}, {0x042cu, 0x90u, 0xc3u}, {0x0464u, 0x07u, 0xc3u},
	{0x04cdu, 0x00u, 0xc3u}, {0x0f03u, 0x1eu, 0xc3u},
};

/* Vendor TblAgc: 26 bytes written to 0x480..0x499. */
static const uint8_t tlsr8269_agc[26] = {
	0x31, 0x32, 0x33, 0x30, 0x38, 0x3c, 0x2c, 0x18, 0x1c, 0x0c, 0x0c, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x0a, 0x0f, 0x15, 0x1b, 0x21, 0x27, 0x2e, 0x32, 0x38, 0x3e,
};

static void tlsr8269_analog_write(uint8_t addr, uint8_t value)
{
	unsigned int key = irq_lock();

	TLSR8269_REG_ANA_ADDR = addr;
	TLSR8269_REG_ANA_DATA = value;
	TLSR8269_REG_ANA_CTRL = TLSR8269_FLD_ANA_CYC0 | TLSR8269_FLD_ANA_RW;
	while ((TLSR8269_REG_ANA_CTRL & TLSR8269_FLD_ANA_BUSY) != 0u) {
	}
	TLSR8269_REG_ANA_CTRL = 0u;
	irq_unlock(key);
}

static void tlsr8269_load_tbl(const struct tlsr8269_rf_tbl *tbl, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t op = tbl[i].cmd & 0x3fu;

		if (op == 0x03u) {
			TLSR8269_REG8(0x00800000u + tbl[i].adr) = tbl[i].dat;
		} else if (op == 0x08u) {
			tlsr8269_analog_write((uint8_t)tbl[i].adr, tbl[i].dat);
		}
	}
}

/* Vendor RF_reset(): pulse the RF bit in RST0. */
static void tlsr8269_rf_reset(void)
{
	TLSR8269_REG_RST0 |= TLSR8269_FLD_RST0_RF;
	TLSR8269_REG_RST0 &= (uint8_t)~TLSR8269_FLD_RST0_RF;
}

/* Vendor RF_Init(RF_OSC_12M, RF_MODE_ZIGBEE_250K) and RF_SetGainManualMax(). */
static void tlsr8269_rf_init(void)
{
	tlsr8269_rf_reset();
	tlsr8269_load_tbl(tbl_rf_ini, ARRAY_SIZE(tbl_rf_ini));
	for (size_t i = 0; i < ARRAY_SIZE(tlsr8269_agc); i++) {
		TLSR8269_REG8(TLSR8269_REG_RF_AGC_BASE + i) = tlsr8269_agc[i];
	}
	tlsr8269_load_tbl(tbl_rf_ini_12m_250k, ARRAY_SIZE(tbl_rf_ini_12m_250k));
	tlsr8269_load_tbl(tbl_rf_zigbee_250k, ARRAY_SIZE(tbl_rf_zigbee_250k));

	TLSR8269_REG8(0x00800433u) = 0x00u;
	TLSR8269_REG8(0x00800434u) = 0x01u;
	TLSR8269_REG8(0x0080043au) = 0x77u;
	TLSR8269_REG8(0x0080043eu) = 0xc9u;
	TLSR8269_REG8(0x008004cdu) = 0x06u;
	TLSR8269_REG8(0x00800430u) = 0x17u;
	TLSR8269_REG8(0x0080043du) = 0xfdu;
	TLSR8269_REG8(0x00800438u) = 0x37u;
}


struct tlsr8269_rx_slot {
	uint8_t dma[TLSR8269_RX_FRAME_MAX];
	uint8_t len;
	int8_t rssi_dbm;
};

struct tlsr8269_radio_data {
	struct net_if *iface;
	uint8_t channel;
	bool started;
	struct k_sem tx_done;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t ieee_addr[8];
	uint8_t rx_buf[TLSR8269_RX_BUF_SIZE] __aligned(16);
	uint8_t tx_buf[TLSR8269_TX_BUF_SIZE] __aligned(4);
	uint8_t ack_buf[TLSR8269_ACK_BUF_SIZE] __aligned(4);
	struct tlsr8269_rx_slot rx_queue[TLSR8269_RX_QUEUE_DEPTH];
	uint8_t rx_head;
	uint8_t rx_count;
};

static struct tlsr8269_radio_data tlsr8269_radio_data_0;
static tlsr8269_rx_sink_t tlsr8269_rx_sink;
static tlsr8269_rx_notify_t tlsr8269_rx_notify;


/* Vendor RF_TrxStateSet(): retune to a physical offset (MHz - 2400). */
static void tlsr8269_trx_state_set(bool tx, uint8_t channel)
{
	uint16_t phys = (uint16_t)((channel - TLSR8269_CHANNEL_MIN) * 5u);

	TLSR8269_REG_RF_TRX_CMD &= (uint8_t)~TLSR8269_FLD_TRX_MASK;
	TLSR8269_REG_RF_FREQ = (uint16_t)(TLSR8269_RF_BASE_MHZ + phys);
	if (tx) {
		TLSR8269_REG_RF_TRX_CMD |= TLSR8269_FLD_TRX_TX;
		TLSR8269_REG_RF_RX_MODE &= (uint8_t)~TLSR8269_RF_MODE_RX_EN;
	} else {
		TLSR8269_REG_RF_TRX_CMD |= TLSR8269_FLD_TRX_RX;
		TLSR8269_REG_RF_RX_MODE |= TLSR8269_RF_MODE_RX_EN;
	}
}

/* Vendor RF_SetTxRxOff(). */
static void tlsr8269_trx_off(void)
{
	TLSR8269_REG_RF_LL_CTRL = TLSR8269_RF_TRX_OFF_CODE;
	TLSR8269_REG_RF_RX_MODE &= (uint8_t)~TLSR8269_RF_MODE_RX_EN;
	TLSR8269_REG_RF_TRX_CMD &= (uint8_t)~TLSR8269_FLD_TRX_MASK;
}

/* Vendor RF_rx_cfg(size, pingpong = 0) plus the RF IRQ setup of ZB_RADIO_TRX_CFG(). */
static void tlsr8269_rx_cfg(void)
{
	TLSR8269_REG_RF_SRX_CTRL &= (uint8_t)~BIT(2);
	TLSR8269_REG_DMA_CHN_MSK &= (uint8_t)~(TLSR8269_FLD_DMA_RF_RX | TLSR8269_FLD_DMA_RF_TX);
	TLSR8269_REG_RF_IRQ_MASK = TLSR8269_FLD_RF_IRQ_RX | TLSR8269_FLD_RF_IRQ_TX;

	TLSR8269_REG_DMA_RF_RX_ADDR = (uint16_t)(uintptr_t)tlsr8269_radio_data_0.rx_buf;
	TLSR8269_REG_DMA_RF_RX_SIZE = (uint8_t)(TLSR8269_RX_BUF_SIZE >> 4);
	TLSR8269_REG_DMA_RF_RX_MODE = 1u;
}

/* Vendor RFDMA_RX_ENABLE and the zigbee RX buffer clear. */
static void tlsr8269_rx_rearm(struct tlsr8269_radio_data *data)
{
	data->rx_buf[12] = 0u;
	*(uint32_t *)data->rx_buf = 0u;
	TLSR8269_REG_DMA_CHN_EN |= TLSR8269_FLD_DMA_RF_RX;
}

static bool tlsr8269_rx_length_ok(const uint8_t *p)
{
	return (p[0] == p[12] + TLSR8269_RX_HDR_LEN) &&
	       (p[0] <= TLSR8269_PHY_MAX_PSDU + TLSR8269_RX_HDR_LEN);
}

static bool tlsr8269_rx_crc_ok(const uint8_t *p)
{
	return (p[p[0] + 3u] & 0x51u) == 0x10u;
}

static bool tlsr8269_addr_is_zero(const uint8_t *a)
{
	for (size_t i = 0; i < 8; i++) {
		if (a[i] != 0u) {
			return false;
		}
	}
	return true;
}

/*
 * Destination check for a PSDU without FCS. Frames without a destination
 * address (beacons) are accepted; PAN 0xffff and short 0xffff are broadcast.
 */
static bool tlsr8269_filter_match(const struct tlsr8269_radio_data *data, const uint8_t *psdu,
				  uint8_t len)
{
	uint8_t mode;
	uint16_t pan;
	uint16_t dst;

	if (len < 2u) {
		return false;
	}

	mode = (psdu[1] >> 2) & 0x03u;
	if (mode == 0u) {
		return true;
	}

	if (len < 5u) {
		return false;
	}
	pan = sys_get_le16(&psdu[3]);
	if (pan != 0xffffu && pan != data->pan_id) {
		return false;
	}

	if (mode == TLSR8269_ADDR_MODE_SHORT) {
		if (len < 7u) {
			return false;
		}
		dst = sys_get_le16(&psdu[5]);
		return (dst == 0xffffu) || (dst == data->short_addr);
	}

	if (mode == TLSR8269_ADDR_MODE_EXT) {
		if (len < 13u || tlsr8269_addr_is_zero(data->ieee_addr)) {
			return false;
		}
		return memcmp(&psdu[5], data->ieee_addr, 8) == 0;
	}

	return false;
}

/* True when the frame asks for an ACK and is addressed to this node by unicast. */
static bool tlsr8269_ack_needed(const struct tlsr8269_radio_data *data, const uint8_t *psdu,
				uint8_t len)
{
	uint8_t mode;

	if (len < 3u || (psdu[0] & TLSR8269_FCF_ACK_REQ) == 0u ||
	    (psdu[0] & 0x07u) == TLSR8269_FRAME_TYPE_ACK) {
		return false;
	}

	mode = (psdu[1] >> 2) & 0x03u;
	if (mode == TLSR8269_ADDR_MODE_SHORT && len >= 7u) {
		return sys_get_le16(&psdu[5]) == data->short_addr;
	}
	if (mode == TLSR8269_ADDR_MODE_EXT && len >= 13u) {
		return memcmp(&psdu[5], data->ieee_addr, 8) == 0;
	}

	return false;
}

/*
 * Vendor rf_mac_ack_build() and the ACK branch of rf_rx_irq_handler(): switch
 * to TX, wait ZB_TX_WAIT_US, start the ACK DMA, poll TX-done, return to RX.
 * The ZB_RT line is masked for the duration, as the vendor code does.
 */
static void tlsr8269_send_ack(struct tlsr8269_radio_data *data, uint8_t seq)
{
	uint32_t waited = 0u;

	/* ACK DMA header: length 4 (psdu 3 + 1), PHY length 5 (psdu 3 + FCS). */
	memset(data->ack_buf, 0, sizeof(data->ack_buf));
	data->ack_buf[0] = 4u;
	data->ack_buf[4] = 5u;
	data->ack_buf[5] = TLSR8269_FRAME_TYPE_ACK;
	data->ack_buf[6] = 0u;
	data->ack_buf[7] = seq;

	irq_disable(TLSR82XX_IRQ_ZB_RT);
	tlsr8269_trx_state_set(true, data->channel);
	k_busy_wait(TLSR8269_ACK_TX_WAIT_US);

	TLSR8269_REG_DMA_RF_TX_ADDR = (uint16_t)(uintptr_t)data->ack_buf;
	TLSR8269_REG_DMA_TX_RDY0 |= TLSR8269_RF_TX_RDY_DMA3;

	while ((TLSR8269_REG_RF_IRQ_STATUS & TLSR8269_FLD_RF_IRQ_TX) == 0u &&
	       waited < TLSR8269_ACK_TX_DONE_US) {
		k_busy_wait(1u);
		waited++;
	}
	TLSR8269_REG_RF_IRQ_STATUS = TLSR8269_FLD_RF_IRQ_TX;

	tlsr8269_trx_state_set(false, data->channel);
	irq_enable(TLSR82XX_IRQ_ZB_RT);
}

static void tlsr8269_rx_queue_push(struct tlsr8269_radio_data *data, const uint8_t *p,
				   uint8_t phy_len)
{
	struct tlsr8269_rx_slot *slot;
	uint8_t idx;

	if (data->rx_count >= TLSR8269_RX_QUEUE_DEPTH) {
		return;
	}

	idx = (uint8_t)((data->rx_head + data->rx_count) % TLSR8269_RX_QUEUE_DEPTH);
	slot = &data->rx_queue[idx];

	memset(slot->dma, 0, TLSR8269_RX_FRAME_PSDU_OFFSET);
	slot->dma[4] = phy_len;
	memcpy(&slot->dma[TLSR8269_RX_FRAME_PSDU_OFFSET], &p[TLSR8269_RX_HDR_LEN], phy_len);
	slot->len = (uint8_t)(TLSR8269_RX_FRAME_PSDU_OFFSET + phy_len);
	slot->rssi_dbm = (int8_t)(p[4] - 110);
	data->rx_count++;
}

static void tlsr8269_rx_handle(struct tlsr8269_radio_data *data)
{
	const uint8_t *p = data->rx_buf;
	uint8_t phy_len;

	if (!tlsr8269_rx_length_ok(p) || !tlsr8269_rx_crc_ok(p)) {
		tlsr8269_rx_rearm(data);
		return;
	}

	phy_len = p[12];
	if (phy_len < TLSR8269_FCS_LEN + 3u || phy_len > TLSR8269_PHY_MAX_PSDU) {
		tlsr8269_rx_rearm(data);
		return;
	}

	if (!tlsr8269_filter_match(data, &p[TLSR8269_RX_HDR_LEN], phy_len - TLSR8269_FCS_LEN)) {
		tlsr8269_rx_rearm(data);
		return;
	}

	if (tlsr8269_ack_needed(data, &p[TLSR8269_RX_HDR_LEN], phy_len - TLSR8269_FCS_LEN)) {
		tlsr8269_send_ack(data, p[TLSR8269_RX_HDR_LEN + 2u]);
	}

	tlsr8269_rx_queue_push(data, p, phy_len);
	tlsr8269_rx_rearm(data);

	if (tlsr8269_rx_notify != NULL) {
		tlsr8269_rx_notify();
	}
}

/*
 * ZB_RT interrupt (IRQ 13): RX-done is served first, then TX-done. Each is
 * cleared by writing the bit back, as the vendor RxFinishClearFlag() and
 * TxFinishClearFlag() do.
 */
static void tlsr8269_rf_isr(const void *arg)
{
	struct tlsr8269_radio_data *data = (struct tlsr8269_radio_data *)arg;
	uint16_t status = TLSR8269_REG_RF_IRQ_STATUS;

	if ((status & TLSR8269_FLD_RF_IRQ_RX) != 0u) {
		TLSR8269_REG_RF_IRQ_STATUS = TLSR8269_FLD_RF_IRQ_RX;
		tlsr8269_rx_handle(data);
	} else if ((status & TLSR8269_FLD_RF_IRQ_TX) != 0u) {
		TLSR8269_REG_RF_IRQ_STATUS = TLSR8269_FLD_RF_IRQ_TX;
		k_sem_give(&data->tx_done);
	}
}

void tlsr8269_radio_register_rx_sink(tlsr8269_rx_sink_t sink)
{
	tlsr8269_rx_sink = sink;
}

void tlsr8269_radio_register_rx_notify(tlsr8269_rx_notify_t notify)
{
	tlsr8269_rx_notify = notify;
}

void tlsr8269_radio_rx_poll(void)
{
	struct tlsr8269_rx_slot local;
	struct tlsr8269_rx_frame_view view;
	bool have;

	for (;;) {
		unsigned int key = irq_lock();

		have = tlsr8269_radio_data_0.rx_count > 0u;
		if (have) {
			memcpy(&local, &tlsr8269_radio_data_0.rx_queue[tlsr8269_radio_data_0.rx_head],
			       sizeof(local));
			tlsr8269_radio_data_0.rx_head =
				(uint8_t)((tlsr8269_radio_data_0.rx_head + 1u) %
					  TLSR8269_RX_QUEUE_DEPTH);
			tlsr8269_radio_data_0.rx_count--;
		}
		irq_unlock(key);

		if (!have) {
			break;
		}

		if (tlsr8269_rx_sink != NULL) {
			view.dma = local.dma;
			view.len = local.len;
			view.rssi_dbm = local.rssi_dbm;
			(void)tlsr8269_rx_sink(&view);
		}
	}
}

void tlsr8269_radio_update_filters(uint16_t pan_id, uint16_t short_addr, const uint8_t *ieee_addr)
{
	unsigned int key = irq_lock();

	tlsr8269_radio_data_0.pan_id = pan_id;
	tlsr8269_radio_data_0.short_addr = short_addr;
	if (ieee_addr != NULL) {
		memcpy(tlsr8269_radio_data_0.ieee_addr, ieee_addr, sizeof(tlsr8269_radio_data_0.ieee_addr));
	}
	irq_unlock(key);
}

/* Persistent IEEE address from the ieee-addr partition; generated once when erased. */
static void tlsr8269_ieee_addr_load(uint8_t mac[8])
{
#if defined(CONFIG_FLASH_MAP) && FIXED_PARTITION_EXISTS(ieee_addr_partition)
	const struct flash_area *fa;
	int rc;

	rc = flash_area_open(FIXED_PARTITION_ID(ieee_addr_partition), &fa);
	if (rc == 0) {
		rc = flash_area_read(fa, 0, mac, 8);
	}

	if (rc == 0) {
		bool erased = true;

		for (size_t i = 0; i < 8; i++) {
			if (mac[i] != 0xffu) {
				erased = false;
				break;
			}
		}
		if (!erased) {
			flash_area_close(fa);
			return;
		}
	}

	sys_rand_get(mac, 8);
	mac[0] = (mac[0] & (uint8_t)~BIT(0)) | BIT(1);
	if (rc == 0) {
		rc = flash_area_write(fa, 0, mac, 8);
		flash_area_close(fa);
	}
	if (rc != 0) {
		LOG_WRN("IEEE address not persisted (%d)", rc);
	}
#else
	sys_rand_get(mac, 8);
	mac[0] = (mac[0] & (uint8_t)~BIT(0)) | BIT(1);
#endif
}

int tlsr8269_radio_get_ieee_addr(uint8_t ieee_addr[8])
{
	if (ieee_addr == NULL) {
		return -EINVAL;
	}

	memcpy(ieee_addr, tlsr8269_radio_data_0.ieee_addr, 8);
	return 0;
}


static int tlsr8269_cca(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -ENOTSUP;
}

static enum ieee802154_hw_caps tlsr8269_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);
	return IEEE802154_HW_FCS;
}

static int tlsr8269_set_channel(const struct device *dev, uint16_t channel)
{
	struct tlsr8269_radio_data *data = dev->data;

	if (channel < TLSR8269_CHANNEL_MIN || channel > TLSR8269_CHANNEL_MAX) {
		return -EINVAL;
	}

	data->channel = (uint8_t)channel;
	if (data->started) {
		tlsr8269_trx_state_set(false, data->channel);
	}

	return 0;
}

static int tlsr8269_filter(const struct device *dev, bool set, enum ieee802154_filter_type type,
			   const struct ieee802154_filter *filter)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(set);
	ARG_UNUSED(type);
	ARG_UNUSED(filter);
	return -ENOTSUP;
}

static int tlsr8269_set_txpower(const struct device *dev, int16_t dbm)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(dbm);
	return -ENOTSUP;
}

static int tlsr8269_start(const struct device *dev)
{
	struct tlsr8269_radio_data *data = dev->data;

	if (data->started) {
		return -EALREADY;
	}

	tlsr8269_rx_cfg();
	tlsr8269_trx_state_set(false, data->channel);
	TLSR8269_REG_DMA_CHN_EN |= TLSR8269_FLD_DMA_RF_RX;
	irq_enable(TLSR82XX_IRQ_ZB_RT);
	data->started = true;

	return 0;
}

static int tlsr8269_stop(const struct device *dev)
{
	struct tlsr8269_radio_data *data = dev->data;

	if (!data->started) {
		return -EALREADY;
	}

	irq_disable(TLSR82XX_IRQ_ZB_RT);
	tlsr8269_trx_off();
	TLSR8269_REG_DMA_CHN_EN &= (uint8_t)~TLSR8269_FLD_DMA_RF_RX;
	data->started = false;

	return 0;
}

static int tlsr8269_tx(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		       struct net_buf *frag)
{
	struct tlsr8269_radio_data *data = dev->data;
	uint8_t len;

	if (mode != IEEE802154_TX_MODE_DIRECT) {
		return -ENOTSUP;
	}

	if (frag == NULL || frag->len == 0u || frag->len > TLSR8269_PHY_MAX_PSDU - TLSR8269_FCS_LEN) {
		return -EINVAL;
	}

	ARG_UNUSED(pkt);
	len = (uint8_t)frag->len;

	/* DMA header (psdu + 1) and PHY length (psdu + FCS), as in rf_mac_ack_build(). */
	data->tx_buf[0] = len + 1u;
	data->tx_buf[1] = 0u;
	data->tx_buf[2] = 0u;
	data->tx_buf[3] = 0u;
	data->tx_buf[4] = len + TLSR8269_FCS_LEN;
	memcpy(&data->tx_buf[TLSR8269_TX_HDR_LEN], frag->data, len);

	k_sem_reset(&data->tx_done);
	tlsr8269_trx_state_set(true, data->channel);
	TLSR8269_REG_DMA_RF_TX_ADDR = (uint16_t)(uintptr_t)data->tx_buf;
	TLSR8269_REG_DMA_TX_RDY0 |= TLSR8269_RF_TX_RDY_DMA3;

	if (k_sem_take(&data->tx_done, K_MSEC(TLSR8269_TX_TIMEOUT_MS)) != 0) {
		tlsr8269_trx_state_set(false, data->channel);
		return -ETIMEDOUT;
	}

	tlsr8269_trx_state_set(false, data->channel);

	return 0;
}

static int tlsr8269_ed_scan(const struct device *dev, uint16_t duration,
			    energy_scan_done_cb_t done_cb)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(duration);
	ARG_UNUSED(done_cb);
	return -ENOTSUP;
}

static int tlsr8269_configure(const struct device *dev, enum ieee802154_config_type type,
			      const struct ieee802154_config *config)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(type);
	ARG_UNUSED(config);
	return -ENOTSUP;
}

static int tlsr8269_attr_get(const struct device *dev, enum ieee802154_attr attr,
			     struct ieee802154_attr_value *value)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
	return -ENOTSUP;
}

static void tlsr8269_iface_init(struct net_if *iface)
{
	tlsr8269_radio_data_0.iface = iface;
}

static const struct ieee802154_radio_api tlsr8269_radio_api = {
	.iface_api.init = tlsr8269_iface_init,
	.get_capabilities = tlsr8269_get_capabilities,
	.cca = tlsr8269_cca,
	.set_channel = tlsr8269_set_channel,
	.filter = tlsr8269_filter,
	.set_txpower = tlsr8269_set_txpower,
	.tx = tlsr8269_tx,
	.start = tlsr8269_start,
	.stop = tlsr8269_stop,
	.configure = tlsr8269_configure,
	.ed_scan = tlsr8269_ed_scan,
	.attr_get = tlsr8269_attr_get,
};

static bool tlsr8269_buf_in_window(const void *buf)
{
	return (((uintptr_t)buf) >> 16) == TLSR8269_DMA_WINDOW_HI;
}

static int tlsr8269_init(const struct device *dev)
{
	struct tlsr8269_radio_data *data = dev->data;

	if (!tlsr8269_buf_in_window(data->rx_buf) || !tlsr8269_buf_in_window(data->tx_buf) ||
	    !tlsr8269_buf_in_window(data->ack_buf)) {
		return -EINVAL;
	}

	k_sem_init(&data->tx_done, 0, 1);
	data->channel = TLSR8269_CHANNEL_MIN;
	data->started = false;
	data->pan_id = 0xffffu;
	data->short_addr = 0xffffu;
	tlsr8269_ieee_addr_load(data->ieee_addr);

	tlsr8269_rf_init();
	tlsr8269_trx_off();
	IRQ_CONNECT(TLSR82XX_IRQ_ZB_RT, 0, tlsr8269_rf_isr, &tlsr8269_radio_data_0, 0);
	irq_disable(TLSR82XX_IRQ_ZB_RT);

	ARG_UNUSED(dev);
	return 0;
}

#if defined(CONFIG_IEEE802154_RAW_MODE)
DEVICE_DT_INST_DEFINE(0, tlsr8269_init, NULL, &tlsr8269_radio_data_0, NULL, POST_KERNEL,
		      CONFIG_IEEE802154_TLSR8269_INIT_PRIO, &tlsr8269_radio_api);
#elif defined(CONFIG_NET_L2_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8269_init, NULL, &tlsr8269_radio_data_0, NULL,
			  CONFIG_IEEE802154_TLSR8269_INIT_PRIO, &tlsr8269_radio_api, IEEE802154_L2,
			  NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  TLSR8269_PHY_MAX_PSDU - TLSR8269_FCS_LEN);
#elif defined(CONFIG_NET_L2_CUSTOM_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8269_init, NULL, &tlsr8269_radio_data_0, NULL,
			  CONFIG_IEEE802154_TLSR8269_INIT_PRIO, &tlsr8269_radio_api,
			  CUSTOM_IEEE802154_L2, NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2),
			  TLSR8269_PHY_MAX_PSDU - TLSR8269_FCS_LEN);
#endif

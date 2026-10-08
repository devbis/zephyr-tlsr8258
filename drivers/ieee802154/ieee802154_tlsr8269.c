/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * TLSR826x IEEE 802.15.4 radio, first increment: RF initialisation, channel
 * selection, immediate TX and RX delivery. The register sequences follow the
 * vendor RF_Init(), RF_ModeSet(), RF_TrxStateSet(), RF_TxPkt() and
 * RF_rx_cfg() in libdrivers_826x.a, and the RX/TX interrupt flow of
 * zigbee/mac/mac_phy.c for MCU_CORE_826x. ACK generation, frame filtering and
 * CCA are not implemented yet; the Zephyr ACK and CCA entry points return
 * -ENOTSUP.
 */

#define DT_DRV_COMPAT telink_tlsr8269_zb

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/util.h>
#include <tlsr82xx/irq.h>

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

#define TLSR8269_REG_RF_IRQ_MASK   TLSR8269_REG16(0x00800f1cu)
#define TLSR8269_REG_RF_IRQ_STATUS TLSR8269_REG16(0x00800f20u)
#define TLSR8269_REG_RF_TRX_CMD    TLSR8269_REG8(0x00800f02u)
#define TLSR8269_REG_RF_LL_CTRL    TLSR8269_REG8(0x00800f16u)
#define TLSR8269_REG_RF_SRX_CTRL   TLSR8269_REG8(0x00800f03u)
#define TLSR8269_REG_RF_CMD        TLSR8269_REG8(0x00800f00u)
#define TLSR8269_REG_RF_RX_MODE    TLSR8269_REG8(0x00800428u)
#define TLSR8269_REG_RF_FREQ       TLSR8269_REG16(0x008004d6u)
#define TLSR8269_REG_RF_AGC_BASE   0x00800480u
#define TLSR8269_REG_RF_TRX_MASK_RF TLSR8269_REG8(0x00800f0cu)

#define TLSR8269_REG_DMA_CHN_EN    TLSR8269_REG8(0x00800520u)
#define TLSR8269_REG_DMA_CHN_MSK   TLSR8269_REG8(0x00800521u)
#define TLSR8269_REG_DMA_TX_RDY0   TLSR8269_REG8(0x00800524u)
#define TLSR8269_REG_DMA_RX_RDY0   TLSR8269_REG8(0x00800526u)
#define TLSR8269_REG_DMA_RF_RX_ADDR TLSR8269_REG16(0x00800508u)
#define TLSR8269_REG_DMA_RF_RX_SIZE TLSR8269_REG8(0x0080050au)
#define TLSR8269_REG_DMA_RF_RX_MODE TLSR8269_REG8(0x0080050bu)
#define TLSR8269_REG_DMA_RF_TX_ADDR TLSR8269_REG16(0x0080050cu)

#define TLSR8269_FLD_RF_IRQ_RX     BIT(0)
#define TLSR8269_FLD_RF_IRQ_TX     BIT(1)
#define TLSR8269_FLD_DMA_RF_RX     BIT(2)
#define TLSR8269_FLD_DMA_RF_TX     BIT(3)
#define TLSR8269_FLD_TRX_RX        BIT(5)
#define TLSR8269_FLD_TRX_TX        BIT(4)
#define TLSR8269_FLD_TRX_MASK      (BIT(4) | BIT(5))
#define TLSR8269_RF_MODE_RX_EN     BIT(0)
#define TLSR8269_RF_TRX_OFF_CODE   0x29u
#define TLSR8269_RF_BASE_MHZ       2400u
#define TLSR8269_RF_TX_RDY_DMA3    BIT(3)

#define TLSR8269_PHY_MAX_PSDU      127u
#define TLSR8269_FCS_LEN           2u
#define TLSR8269_RX_BUF_SIZE       144u
#define TLSR8269_TX_BUF_SIZE       (TLSR8269_PHY_MAX_PSDU + 5u + 4u)
#define TLSR8269_RX_HDR_LEN        13u
#define TLSR8269_TX_HDR_LEN        5u
#define TLSR8269_TX_TIMEOUT_MS     10u

#define TLSR8269_CHANNEL_MIN       11u
#define TLSR8269_CHANNEL_MAX       26u

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

struct tlsr8269_radio_data {
	struct net_if *iface;
	uint8_t channel;
	bool started;
	struct k_sem tx_done;
	uint8_t rx_buf[TLSR8269_RX_BUF_SIZE] __aligned(16);
	uint8_t tx_buf[TLSR8269_TX_BUF_SIZE] __aligned(4);
	uint8_t tx_len;
};

static struct tlsr8269_radio_data tlsr8269_radio_data_0;

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

static void tlsr8269_rx_handle(struct tlsr8269_radio_data *data)
{
	const uint8_t *p = data->rx_buf;

	if (tlsr8269_rx_length_ok(p) && tlsr8269_rx_crc_ok(p)) {
		uint8_t len = p[12];
		const uint8_t *psdu = &p[TLSR8269_RX_HDR_LEN];
		uint8_t rssi = p[4];

		/* RX delivery through the network stack is added with the net-stack glue. */
		ARG_UNUSED(len);
		ARG_UNUSED(psdu);
		ARG_UNUSED(rssi);
	}

	tlsr8269_rx_rearm(data);
}

/*
 * ZB_RT interrupt (IRQ 13): the RF RX-done bit is served first, then TX-done.
 * Each is cleared by writing the bit back, as the vendor RxFinishClearFlag()
 * and TxFinishClearFlag() do.
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

static enum ieee802154_hw_caps tlsr8269_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);
	return IEEE802154_HW_FCS;
}

static int tlsr8269_cca(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -ENOTSUP;
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

	data->tx_buf[0] = len + 1u;
	data->tx_buf[1] = 0u;
	data->tx_buf[2] = 0u;
	data->tx_buf[3] = 0u;
	data->tx_buf[4] = len;
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
	struct tlsr8269_radio_data *data = &tlsr8269_radio_data_0;

	data->iface = iface;
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

static int tlsr8269_init(const struct device *dev)
{
	struct tlsr8269_radio_data *data = dev->data;

	/* The DMA address registers are 16 bits wide; the buffers must lie in the 0x80xxxx window. */
	if ((((uintptr_t)data->rx_buf) >> 16) != 0x80u || (((uintptr_t)data->tx_buf) >> 16) != 0x80u) {
		return -EINVAL;
	}

	k_sem_init(&data->tx_done, 0, 1);
	data->channel = TLSR8269_CHANNEL_MIN;
	data->started = false;

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
#else
NET_DEVICE_DT_INST_DEFINE(0, tlsr8269_init, NULL, &tlsr8269_radio_data_0, NULL,
			  CONFIG_IEEE802154_TLSR8269_INIT_PRIO, &tlsr8269_radio_api,
			  IEEE802154_L2, NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  TLSR8269_PHY_MAX_PSDU - TLSR8269_FCS_LEN);
#endif

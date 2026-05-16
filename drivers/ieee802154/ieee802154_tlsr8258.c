/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_zb

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_pkt.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>

LOG_MODULE_REGISTER(ieee802154_tlsr8258, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

#define TLSR_REG8(addr)  (*(volatile uint8_t *)(0x00800000u + (addr)))
#define TLSR_REG16(addr) (*(volatile uint16_t *)(0x00800000u + (addr)))
#define TLSR_REG32(addr) (*(volatile uint32_t *)(0x00800000u + (addr)))

#define TCMD_UNDER_WR 0x80u
#define TCMD_MASK     0x3fu
#define TCMD_WRITE    0x03u

#define RF_TRX_MODE 0xe0u
#define RF_TRX_OFF  0x45u

#define RF_IRQ_RX          BIT(0)
#define RF_IRQ_TX          BIT(1)
#define RF_IRQ_RX_TIMEOUT  BIT(2)
#define RF_IRQ_CMD_DONE    BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_TX_DS       BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(11)
#define RF_IRQ_ALL         0xffffu

#define DMA_CHN_RF_RX BIT(2)
#define DMA_CHN_RF_TX BIT(3)

#define TLSR8258_RX_BUF_SIZE 256u
#define TLSR8258_TX_BUF_SIZE 132u
#define TLSR8258_PAYLOAD_OFFSET 5u
#define TLSR8258_PHY_MAX_PSDU 127u
#define TLSR8258_FCS_LENGTH 2u
#define TLSR8258_MIN_FRAME_LENGTH 3u
#define TLSR8258_ACK_REQUEST BIT(5)
#define TLSR8258_IEEE_ADDR_SIZE 8u
#define TLSR8258_SHORT_ADDR_SIZE 2u
#define TLSR8258_PAN_ID_SIZE 2u
#define TLSR8258_FRAME_TYPE_OFFSET 0u
#define TLSR8258_DEST_ADDR_TYPE_OFFSET 1u
#define TLSR8258_DEST_ADDR_TYPE_MASK 0x0cu
#define TLSR8258_DEST_ADDR_TYPE_SHORT 0x08u
#define TLSR8258_DEST_ADDR_TYPE_IEEE 0x0cu
#define TLSR8258_PAN_ID_OFFSET 3u
#define TLSR8258_DEST_ADDR_OFFSET 5u
#define TLSR8258_LOGIC_CHANNEL_TO_OFFSET_MHZ(ch) (((ch) - 10u) * 5u)
#define TLSR8258_RSSI_TO_LQI_MIN -87
#define TLSR8258_RSSI_TO_LQI_SCALE 3

struct tblcmdset {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

struct tlsr8258_radio_data {
	struct net_if *iface;
	uint8_t mac_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint8_t rx_buffer[TLSR8258_RX_BUF_SIZE] __aligned(4);
	uint8_t tx_buffer[TLSR8258_TX_BUF_SIZE] __aligned(4);
	uint8_t filter_pan_id[TLSR8258_PAN_ID_SIZE];
	uint8_t filter_short_addr[TLSR8258_SHORT_ADDR_SIZE];
	uint8_t filter_ieee_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint16_t current_channel;
	uint16_t last_irq;
	uint32_t rx_count;
	uint32_t tx_count;
	bool started;
	bool promiscuous;
};

static const struct tblcmdset tbl_rf_init[] = {
	{0x12d2, 0x9b, 0xc3}, {0x12d3, 0x19, 0xc3}, {0x127b, 0x0e, 0xc3},
	{0x1276, 0x50, 0xc3}, {0x1277, 0x73, 0xc3},
};

static const struct tblcmdset tbl_rf_zigbee_250k[] = {
	{0x1220, 0x04, 0xc3}, {0x1221, 0x2b, 0xc3}, {0x1222, 0x43, 0xc3},
	{0x1223, 0x86, 0xc3}, {0x122a, 0x90, 0xc3}, {0x1254, 0x0e, 0xc3},
	{0x1255, 0x09, 0xc3}, {0x1256, 0x0c, 0xc3}, {0x1257, 0x08, 0xc3},
	{0x1258, 0x09, 0xc3}, {0x1259, 0x0f, 0xc3}, {0x0400, 0x13, 0xc3},
	{0x0420, 0x18, 0xc3}, {0x0402, 0x46, 0xc3}, {0x0404, 0xc0, 0xc3},
	{0x0405, 0x04, 0xc3}, {0x0421, 0x23, 0xc3}, {0x0422, 0x04, 0xc3},
	{0x0408, 0xa7, 0xc3}, {0x0409, 0x00, 0xc3}, {0x040a, 0x00, 0xc3},
	{0x040b, 0x00, 0xc3}, {0x0460, 0x36, 0xc3}, {0x0461, 0x46, 0xc3},
	{0x0462, 0x51, 0xc3}, {0x0463, 0x61, 0xc3}, {0x0464, 0x6d, 0xc3},
	{0x0465, 0x78, 0xc3},
};

static const uint8_t rf_power_level_list[] = {
	0x3f, 0x3d, 0x3a, 0x38, 0x35, 0x33, 0x31, 0x2f, 0x2d, 0x2b,
	0x29, 0x27, 0x25, 0x23, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x17,
	0xbf, 0xbd, 0xbb, 0xb9, 0xb6, 0xb4, 0xb2, 0xb0, 0xae, 0xac,
	0xa9, 0xa8, 0xa4, 0xa2, 0xa0, 0x9e, 0x9c, 0x9a, 0x98, 0x96,
	0x94, 0x92, 0x90, 0x8e, 0x8c, 0x8a, 0x88, 0x86, 0x84, 0x82,
};

static struct tlsr8258_radio_data tlsr8258_radio;

static void tlsr8258_load_tbl(const struct tblcmdset *tbl, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t cmd = tbl[i].cmd;

		if (((cmd & TCMD_UNDER_WR) != 0u) && ((cmd & TCMD_MASK) == TCMD_WRITE)) {
			TLSR_REG8(tbl[i].adr) = tbl[i].dat;
		}
	}
}

static __ramfunc void tlsr8258_rf_set_channel_offset(uint8_t chn)
{
	int16_t ch = (int16_t)chn + 0x960;
	uint8_t vco_cap_step = 0u;
	uint32_t rf_chn_word;

	if (ch <= 0x09f5) {
		vco_cap_step = 4u;
	}
	if (ch <= 0x09d7) {
		vco_cap_step = 8u;
	}
	if (ch <= 0x09be) {
		vco_cap_step = 12u;
	}
	if (ch <= 0x09a0) {
		vco_cap_step = 16u;
	}
	if (ch <= 0x0982) {
		vco_cap_step = 20u;
	}
	if (ch <= 0x0964) {
		vco_cap_step = 28u;
	}
	if (ch <= 0x094b) {
		vco_cap_step = 24u;
	}

	rf_chn_word = (uint32_t)(uint16_t)ch << 17;
	TLSR_REG8(0x1244) = (uint8_t)(((rf_chn_word >> 15) | 1u) & 0xffu);
	TLSR_REG8(0x1245) = (uint8_t)((TLSR_REG8(0x1245) & 0xc0u) |
				      ((rf_chn_word >> 23) & 0x3fu));
	TLSR_REG8(0x1229) = (uint8_t)((TLSR_REG8(0x1229) & 0xc3u) | vco_cap_step);
}

static void tlsr8258_rf_set_power_level(uint8_t level)
{
	uint8_t power_code = level & 0x3fu;
	uint32_t power_word = (uint32_t)power_code << 24;

	if ((level & BIT(7)) != 0u) {
		TLSR_REG8(0x1225) |= BIT(6);
	} else {
		TLSR_REG8(0x1225) &= (uint8_t)~BIT(6);
	}

	TLSR_REG8(0x1226) = (uint8_t)((TLSR_REG8(0x1226) & 0x7fu) |
				      ((power_word >> 17) & 0x80u));
	TLSR_REG8(0x1227) = (uint8_t)((TLSR_REG8(0x1227) & 0xe0u) |
				      ((power_word >> 25) & 0x1fu));
}

static void tlsr8258_rf_rx_buffer_set(uint8_t *buffer, uint16_t size)
{
	uintptr_t addr = (uintptr_t)buffer;

	TLSR_REG16(0x0c08) = (uint16_t)addr;
	TLSR_REG8(0x0c42) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG8(0x0c0a) = (uint8_t)(size >> 4);
	TLSR_REG8(0x0c0b) = 1u;
}

static __ramfunc void tlsr8258_rf_tx_pkt(uint8_t *packet)
{
	uintptr_t addr = (uintptr_t)packet;

	TLSR_REG8(0x0c43) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG16(0x0c0c) = (uint16_t)addr;
	TLSR_REG8(0x0c24) |= DMA_CHN_RF_TX;
}

static __ramfunc void tlsr8258_rf_set_rxmode(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	tlsr8258_rf_set_channel_offset(TLSR8258_LOGIC_CHANNEL_TO_OFFSET_MHZ(tlsr8258_radio.current_channel));
	TLSR_REG8(0x0428) = RF_TRX_MODE | BIT(0);
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
}

static __ramfunc void tlsr8258_rf_set_txmode(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x19u;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	tlsr8258_rf_set_channel_offset(TLSR8258_LOGIC_CHANNEL_TO_OFFSET_MHZ(tlsr8258_radio.current_channel));
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	TLSR_REG8(0x0428) &= (uint8_t)~BIT(0);
}

static __ramfunc void tlsr8258_rf_off(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
}

static void tlsr8258_rf_init(void)
{
	tlsr8258_load_tbl(tbl_rf_init, ARRAY_SIZE(tbl_rf_init));
	tlsr8258_load_tbl(tbl_rf_zigbee_250k, ARRAY_SIZE(tbl_rf_zigbee_250k));

	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX | DMA_CHN_RF_TX;
	TLSR_REG8(0x0401) = 0u;
	TLSR_REG8(0x0404) &= (uint8_t)~BIT(5);
	TLSR_REG32(0x0408) = 0x29417671u;
	TLSR_REG8(0x0405) |= BIT(7);
	TLSR_REG8(0x0f15) = 0xf0u;
	TLSR_REG16(0x0f04) = 113u;
	TLSR_REG8(0x0f03) &= (uint8_t)~BIT(2);
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_rx_buffer_set(tlsr8258_radio.rx_buffer, sizeof(tlsr8258_radio.rx_buffer));
	tlsr8258_rf_set_power_level(rf_power_level_list[23]);
}

#if !defined(CONFIG_IEEE802154_RAW_MODE)
static bool tlsr8258_rx_length_ok(const uint8_t *rx)
{
	return rx[0] < (TLSR8258_RX_BUF_SIZE - 3u) &&
	       rx[0] == (uint8_t)(rx[4] + 9u);
}

static bool tlsr8258_rx_crc_ok(const uint8_t *rx)
{
	return (rx[rx[0] + 3u] & 0x51u) == 0x10u;
}

static bool tlsr8258_filter_match(uint8_t *payload)
{
	if (tlsr8258_radio.promiscuous) {
		return true;
	}

	if (memcmp(&payload[TLSR8258_PAN_ID_OFFSET], tlsr8258_radio.filter_pan_id,
		   TLSR8258_PAN_ID_SIZE) != 0 &&
	    sys_get_le16(&payload[TLSR8258_PAN_ID_OFFSET]) != 0xffffu) {
		return false;
	}

	switch (payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) {
	case TLSR8258_DEST_ADDR_TYPE_SHORT:
		return memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], tlsr8258_radio.filter_short_addr,
			      TLSR8258_SHORT_ADDR_SIZE) == 0 ||
		       sys_get_le16(&payload[TLSR8258_DEST_ADDR_OFFSET]) == 0xffffu;
	case TLSR8258_DEST_ADDR_TYPE_IEEE:
		return memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], tlsr8258_radio.filter_ieee_addr,
			      TLSR8258_IEEE_ADDR_SIZE) == 0;
	default:
		return false;
	}
}

static uint8_t tlsr8258_lqi_from_rssi(int8_t rssi)
{
	int32_t lqi;

	if (rssi < TLSR8258_RSSI_TO_LQI_MIN) {
		return 0u;
	}

	lqi = TLSR8258_RSSI_TO_LQI_SCALE * (rssi - TLSR8258_RSSI_TO_LQI_MIN);
	return (uint8_t)MIN(lqi, 0xff);
}

static int8_t tlsr8258_last_rssi_dbm(uint8_t *rx)
{
	return (int8_t)rx[rx[0] + 2u] - 110;
}
#endif /* !CONFIG_IEEE802154_RAW_MODE */

static __ramfunc void tlsr8258_rx_isr(void)
{
	uint8_t *rx = tlsr8258_radio.rx_buffer;
#if !defined(CONFIG_IEEE802154_RAW_MODE)
	uint8_t *payload = &rx[TLSR8258_PAYLOAD_OFFSET];
	uint8_t length;
	int8_t rssi;
	struct net_pkt *pkt;
#endif

	TLSR_REG16(0x0f20) = RF_IRQ_RX;
	tlsr8258_radio.rx_count++;

#if defined(CONFIG_IEEE802154_RAW_MODE)
	ARG_UNUSED(rx);
#else
	if (tlsr8258_radio.iface == NULL || !tlsr8258_rx_length_ok(rx) || !tlsr8258_rx_crc_ok(rx)) {
		return;
	}

	length = rx[4];
	if (!IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
		if (length <= TLSR8258_FCS_LENGTH) {
			return;
		}
		length -= TLSR8258_FCS_LENGTH;
	}

	if (length < TLSR8258_MIN_FRAME_LENGTH || length > TLSR8258_PHY_MAX_PSDU) {
		return;
	}

	if (!tlsr8258_filter_match(payload)) {
		return;
	}

	pkt = net_pkt_rx_alloc_with_buffer(tlsr8258_radio.iface, length, NET_AF_UNSPEC, 0,
					   K_NO_WAIT);
	if (pkt == NULL) {
		return;
	}

	if (net_pkt_write(pkt, payload, length) < 0) {
		net_pkt_unref(pkt);
		return;
	}

	rssi = tlsr8258_last_rssi_dbm(rx);
	net_pkt_set_ieee802154_rssi_dbm(pkt, rssi);
	net_pkt_set_ieee802154_lqi(pkt, tlsr8258_lqi_from_rssi(rssi));

	if (net_recv_data(tlsr8258_radio.iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
#endif
}

static __ramfunc void tlsr8258_rf_isr(const void *unused)
{
	uint16_t irq = TLSR_REG16(0x0f20);

	ARG_UNUSED(unused);
	tlsr8258_radio.last_irq = irq;

	if ((irq & RF_IRQ_RX) != 0u) {
		tlsr8258_rx_isr();
	} else {
		TLSR_REG16(0x0f20) = irq != 0u ? irq : RF_IRQ_ALL;
	}
}

static void tlsr8258_iface_init(struct net_if *iface)
{
	uint8_t *mac = tlsr8258_radio.mac_addr;

#if defined(CONFIG_IEEE802154_TLSR8258_RANDOM_MAC)
		sys_rand_get(mac, TLSR8258_IEEE_ADDR_SIZE);
		mac[0] = (mac[0] & (uint8_t)~BIT(0)) | BIT(1);
#else
		mac[0] = 0xc4;
		mac[1] = 0x19;
		mac[2] = 0xd1;
		mac[3] = 0x00;
		mac[4] = CONFIG_IEEE802154_TLSR8258_MAC4;
		mac[5] = CONFIG_IEEE802154_TLSR8258_MAC5;
		mac[6] = CONFIG_IEEE802154_TLSR8258_MAC6;
		mac[7] = CONFIG_IEEE802154_TLSR8258_MAC7;
#endif

	net_if_set_link_addr(iface, mac, TLSR8258_IEEE_ADDR_SIZE, NET_LINK_IEEE802154);
	tlsr8258_radio.iface = iface;
	ieee802154_init(iface);
}

static enum ieee802154_hw_caps tlsr8258_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER;
}

static __ramfunc int tlsr8258_cca(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!tlsr8258_radio.started) {
		return -ENETDOWN;
	}

	return ((int8_t)TLSR_REG8(0x0441) - 110) < CONFIG_IEEE802154_TLSR8258_CCA_RSSI_THRESHOLD ?
		       0 :
		       -EBUSY;
}

static int tlsr8258_set_channel(const struct device *dev, uint16_t channel)
{
	ARG_UNUSED(dev);

	if (channel < 11u || channel > 26u) {
		return -EINVAL;
	}

	if (tlsr8258_radio.current_channel == channel) {
		return -EALREADY;
	}

	tlsr8258_radio.current_channel = channel;
	if (tlsr8258_radio.started) {
		tlsr8258_rf_set_rxmode();
	}

	return 0;
}

static int tlsr8258_filter(const struct device *dev, bool set,
			   enum ieee802154_filter_type type,
			   const struct ieee802154_filter *filter)
{
	ARG_UNUSED(dev);

	if (!set || filter == NULL) {
		return -ENOTSUP;
	}

	if (type == IEEE802154_FILTER_TYPE_IEEE_ADDR) {
		memcpy(tlsr8258_radio.filter_ieee_addr, filter->ieee_addr,
		       TLSR8258_IEEE_ADDR_SIZE);
		return 0;
	}
	if (type == IEEE802154_FILTER_TYPE_SHORT_ADDR) {
		sys_put_le16(filter->short_addr, tlsr8258_radio.filter_short_addr);
		return 0;
	}
	if (type == IEEE802154_FILTER_TYPE_PAN_ID) {
		sys_put_le16(filter->pan_id, tlsr8258_radio.filter_pan_id);
		return 0;
	}

	return -ENOTSUP;
}

static int tlsr8258_set_txpower(const struct device *dev, int16_t dbm)
{
	ARG_UNUSED(dev);

	if (dbm >= 9) {
		tlsr8258_rf_set_power_level(rf_power_level_list[0]);
	} else if (dbm >= 3) {
		tlsr8258_rf_set_power_level(rf_power_level_list[19]);
	} else if (dbm >= 0) {
		tlsr8258_rf_set_power_level(rf_power_level_list[30]);
	} else {
		tlsr8258_rf_set_power_level(0xffu);
	}

	return 0;
}

static int tlsr8258_start(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (tlsr8258_radio.started) {
		return -EALREADY;
	}

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG16(0x0f1c) = RF_IRQ_RX | RF_IRQ_RX_TIMEOUT | RF_IRQ_FSM_TIMEOUT;
	tlsr8258_rf_set_rxmode();
	irq_enable(TLSR8258_IRQ_ZB_RT);
	tlsr8258_radio.started = true;

	return 0;
}

static int tlsr8258_stop(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!tlsr8258_radio.started) {
		return -EALREADY;
	}

	irq_disable(TLSR8258_IRQ_ZB_RT);
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_off();
	tlsr8258_radio.started = false;

	return 0;
}

static int tlsr8258_set_tx_payload(const uint8_t *payload, uint8_t payload_len)
{
	if (payload_len > (TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH)) {
		return -EINVAL;
	}

	tlsr8258_radio.tx_buffer[0] = payload_len + 1u;
	tlsr8258_radio.tx_buffer[1] = 0u;
	tlsr8258_radio.tx_buffer[2] = 0u;
	tlsr8258_radio.tx_buffer[3] = 0u;
	tlsr8258_radio.tx_buffer[4] = payload_len + TLSR8258_FCS_LENGTH;
	memcpy(&tlsr8258_radio.tx_buffer[TLSR8258_PAYLOAD_OFFSET], payload, payload_len);

	return 0;
}

static int tlsr8258_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		       struct net_pkt *pkt, struct net_buf *frag)
{
	uint32_t waited = 0u;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(pkt);

	if (!tlsr8258_radio.started) {
		return -ENETDOWN;
	}

	if (mode != IEEE802154_TX_MODE_DIRECT && mode != IEEE802154_TX_MODE_CCA) {
		return -ENOTSUP;
	}

	if (mode == IEEE802154_TX_MODE_CCA) {
		ret = tlsr8258_cca(dev);
		if (ret < 0) {
			return ret;
		}
	}

	ret = tlsr8258_set_tx_payload(frag->data, frag->len);
	if (ret < 0) {
		return ret;
	}

	irq_disable(TLSR8258_IRQ_ZB_RT);
	tlsr8258_rf_set_txmode();
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_tx_pkt(tlsr8258_radio.tx_buffer);

	while (waited < CONFIG_IEEE802154_TLSR8258_TX_WAIT_US) {
		uint16_t irq = TLSR_REG16(0x0f20);

		if ((irq & (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_CMD_DONE)) != 0u) {
			TLSR_REG16(0x0f20) = irq;
			tlsr8258_radio.tx_count++;
			tlsr8258_rf_set_rxmode();
			irq_enable(TLSR8258_IRQ_ZB_RT);
			return 0;
		}

		if ((irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
			TLSR_REG16(0x0f20) = irq;
			tlsr8258_rf_set_rxmode();
			irq_enable(TLSR8258_IRQ_ZB_RT);
			return -EIO;
		}

		k_busy_wait(1);
		waited++;
	}

	tlsr8258_rf_set_rxmode();
	irq_enable(TLSR8258_IRQ_ZB_RT);
	return -EIO;
}

static int tlsr8258_ed_scan(const struct device *dev, uint16_t duration,
			    energy_scan_done_cb_t done_cb)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(duration);
	ARG_UNUSED(done_cb);

	return -ENOTSUP;
}

static int tlsr8258_configure(const struct device *dev, enum ieee802154_config_type type,
			      const struct ieee802154_config *config)
{
	ARG_UNUSED(dev);

	if (type == IEEE802154_CONFIG_PROMISCUOUS) {
		tlsr8258_radio.promiscuous = config->promiscuous;
		return 0;
	}

	return -ENOTSUP;
}

IEEE802154_DEFINE_PHY_SUPPORTED_CHANNELS(tlsr8258_attr, 11, 26);

static int tlsr8258_attr_get(const struct device *dev, enum ieee802154_attr attr,
			     struct ieee802154_attr_value *value)
{
	ARG_UNUSED(dev);

	return ieee802154_attr_get_channel_page_and_range(
		attr, IEEE802154_ATTR_PHY_CHANNEL_PAGE_ZERO_OQPSK_2450_BPSK_868_915,
		&tlsr8258_attr.phy_supported_channels, value);
}

static const struct ieee802154_radio_api tlsr8258_radio_api = {
	.iface_api.init = tlsr8258_iface_init,
	.get_capabilities = tlsr8258_get_capabilities,
	.cca = tlsr8258_cca,
	.set_channel = tlsr8258_set_channel,
	.filter = tlsr8258_filter,
	.set_txpower = tlsr8258_set_txpower,
	.start = tlsr8258_start,
	.stop = tlsr8258_stop,
	.tx = tlsr8258_tx,
	.ed_scan = tlsr8258_ed_scan,
	.configure = tlsr8258_configure,
	.attr_get = tlsr8258_attr_get,
};

static int tlsr8258_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	memset(&tlsr8258_radio, 0, sizeof(tlsr8258_radio));
	sys_put_le16(0xffffu, tlsr8258_radio.filter_pan_id);
	sys_put_le16(0xffffu, tlsr8258_radio.filter_short_addr);
	tlsr8258_radio.current_channel = 11u;

	tlsr8258_rf_init();
	tlsr8258_rf_set_channel_offset(TLSR8258_LOGIC_CHANNEL_TO_OFFSET_MHZ(tlsr8258_radio.current_channel));
	IRQ_CONNECT(DT_INST_IRQN(0), 0, tlsr8258_rf_isr, NULL, 0);
	irq_disable(TLSR8258_IRQ_ZB_RT);

	return 0;
}

#if defined(CONFIG_IEEE802154_RAW_MODE)
DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio, NULL,
		      POST_KERNEL, CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
		      &tlsr8258_radio_api);
#elif defined(CONFIG_NET_L2_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio, NULL,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
			  &tlsr8258_radio_api, IEEE802154_L2,
			  NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH);
#elif defined(CONFIG_NET_L2_OPENTHREAD)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio, NULL,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
			  &tlsr8258_radio_api, OPENTHREAD_L2,
			  NET_L2_GET_CTX_TYPE(OPENTHREAD_L2), 1280);
#endif

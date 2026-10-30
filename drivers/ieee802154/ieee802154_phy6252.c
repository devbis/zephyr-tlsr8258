/*
 * SPDX-FileCopyrightText: Copyright 2026 Aleksandr Faronov
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * PHY register values and LL operation sequences are adapted from the
 * PHY6222 backend in zigbee-rs/zigbee-mac at commit
 * 76d184caa4756a69eb17ee4fd74244cf6ed62806. That backend documents the
 * PHY6222/PHY6252 radio path as unverified on hardware. PLL defaults match
 * the PHY6222M005 ROM initialization sequence.
 */

#define DT_DRV_COMPAT phyplus_phy6252_radio

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ieee802154_phy6252, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

#define PHY6252_RF_BASE             0x40030000U
#define PHY6252_LL_BASE             0x40031000U
#define PHY6252_TX_FIFO             0x40031400U
#define PHY6252_RX_FIFO             0x40031C00U
#define PHY6252_CLK_CTRL            0x4000F040U
#define PHY6252_CLK_CFG             0x4000F044U
#define PHY6252_FLASH_BASE          0x11000000U
#define PHY6252_CHIP_ID_OFFSET      0x0800U
#define PHY6252_CHIP_ID_WORDS       64U
#define PHY6252_FACTORY_MAC_OFFSET  0x0900U

#define PHY6252_RF_REG(offset)      (PHY6252_RF_BASE + (offset))
#define PHY6252_LL_REG(offset)      (PHY6252_LL_BASE + (offset))

/*
 * LL status bits as ll_hw_get_irq_status() of the ROM and the SDK's ll_hw_drv.h
 * define them. The shipping Zigbee firmware for this chip reads exactly this
 * layout: a good frame is MD | COK (0x401) and an empty receive window is
 * MD | RTO. The earlier "compact" layout (COK = bit 1, RTO = bit 3) came from
 * the unverified zigbee-rs backend and has no second source, so it is not used.
 * ll_hw_go() clears the whole 0x3fff status range, and only MD is enabled as
 * an interrupt; COK and RTO are read from the status when MD fires.
 */
#define PHY6252_IRQ_MD              BIT(0)
#define PHY6252_IRQ_RTO             BIT(2)
#define PHY6252_IRQ_COK             BIT(10)
#define PHY6252_IRQ_CLEAR           0x3FFFU
#define PHY6252_CRC_ZIGBEE           0x03U

#define PHY6252_CHANNEL_MIN         11U
#define PHY6252_CHANNEL_MAX         26U
#define PHY6252_MAX_PSDU            IEEE802154_MAX_PHY_PACKET_SIZE
#define PHY6252_FCS_LEN             IEEE802154_FCS_LENGTH
#define PHY6252_MAX_MPDU            (PHY6252_MAX_PSDU - PHY6252_FCS_LEN)
#define PHY6252_CCA_THRESHOLD_DBM   (-60)
#define PHY6252_CCA_SETTLE_US       128U
#define PHY6252_TX_TIMEOUT_MS       10U
#define PHY6252_ACK_TIMEOUT_MS      3U
#define PHY6252_ACK_TURNAROUND_US   192U
#define PHY6252_ACK_TX_TIMEOUT_US   1000U
#define PHY6252_RX_QUEUE_DEPTH      16U

#define PHY6252_LL_MODE_STX         0U
#define PHY6252_LL_MODE_SRX         1U
#define PHY6252_LL_STX_CONFIG       0x000100U
#define PHY6252_LL_SRX_CONFIG       0x010001U
#define PHY6252_LL_BYPASS_FIELDS    0x07U

struct phy6252_rx_frame {
	uint8_t psdu[PHY6252_MAX_PSDU];
	uint8_t len;
	int8_t rssi_dbm;
};

struct phy6252_data {
	struct net_if *iface;
	struct k_mutex op_lock;
	struct k_sem tx_done;
	struct k_sem ack_done;
	struct k_work rx_work;
	atomic_t started;
	atomic_t ack_waiting;
	atomic_t expected_ack_seq;
	uint16_t channel;
	uint16_t short_addr;
	uint16_t filter_pan_id;
	uint8_t ieee_addr[8];
	uint8_t filter_ieee_addr[8];
	uint8_t tp_cal0;
	uint8_t tp_cal1;
	int8_t tx_power_dbm;
	bool pan_filter_enabled;
	bool short_filter_enabled;
	bool ieee_filter_enabled;
	bool promiscuous;
};

static struct phy6252_data phy6252_data;

K_MSGQ_DEFINE(phy6252_rx_queue, sizeof(struct phy6252_rx_frame), PHY6252_RX_QUEUE_DEPTH, 4);

static uint32_t phy6252_reg_read(uint32_t addr)
{
	return *(volatile uint32_t *)addr;
}

static void phy6252_reg_write(uint32_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static void phy6252_reg_write_field(uint32_t addr, uint8_t high, uint8_t low, uint32_t value)
{
	uint32_t mask = ((1U << (high - low + 1U)) - 1U) << low;
	uint32_t old_value = phy6252_reg_read(addr);

	phy6252_reg_write(addr, (old_value & ~mask) | ((value << low) & mask));
}

static bool phy6252_one_bit_hot_decode(uint32_t word, uint8_t *value)
{
	uint8_t decoded = 0U;

	if ((word == UINT32_MAX) || (word == 0U)) {
		return false;
	}

	for (uint8_t i = 0U; i < 8U; i++) {
		uint8_t nibble = (uint8_t)((word >> (i * 4U)) & 0x0FU);
		uint8_t bit_pos = 0U;

		if ((nibble == 0U) || ((nibble & (nibble - 1U)) != 0U)) {
			return false;
		}
		while ((nibble & BIT(bit_pos)) == 0U) {
			bit_pos++;
		}
		decoded |= (uint8_t)((bit_pos & 1U) << i);
	}

	*value = decoded;
	return true;
}

static void phy6252_read_ieee_addr(uint8_t addr[8])
{
	uint8_t mac48[6];
	uint32_t hash0;
	uint32_t hash1;

	const volatile uint8_t *chip_id;
	size_t offset;
	bool valid = true;

	for (uint8_t i = 0U; i < ARRAY_SIZE(mac48); i++) {
		uint32_t word_addr = PHY6252_FLASH_BASE + PHY6252_FACTORY_MAC_OFFSET + (i * 4U);
		uint32_t word = phy6252_reg_read(word_addr);

		if (!phy6252_one_bit_hot_decode(word, &mac48[ARRAY_SIZE(mac48) - 1U - i])) {
			valid = false;
			break;
		}
	}

	if (valid && (memcmp(mac48, "\x00\x00\x00\x00\x00\x00", sizeof(mac48)) != 0) &&
	    (memcmp(mac48, "\xff\xff\xff\xff\xff\xff", sizeof(mac48)) != 0)) {
		addr[0] = mac48[0];
		addr[1] = mac48[1];
		addr[2] = mac48[2];
		addr[3] = 0xFFU;
		addr[4] = 0xFEU;
		addr[5] = mac48[3];
		addr[6] = mac48[4];
		addr[7] = mac48[5];
		return;
	}

	/* Use the chip-ID area for a stable locally administered fallback. */
	hash0 = 2166136261U;
	hash1 = 0x9E3779B9U;
	chip_id = (const volatile uint8_t *)(PHY6252_FLASH_BASE + PHY6252_CHIP_ID_OFFSET);

	for (offset = 0U; offset < PHY6252_CHIP_ID_WORDS * sizeof(uint32_t); offset++) {
		hash0 = (hash0 ^ chip_id[offset]) * 16777619U;
		hash1 = (hash1 ^ (chip_id[offset] + (uint8_t)offset)) * 2246822519U;
	}

	addr[0] = 0x02U;
	addr[1] = (uint8_t)(hash0 >> 24);
	addr[2] = (uint8_t)(hash0 >> 16);
	addr[3] = (uint8_t)(hash0 >> 8);
	addr[4] = (uint8_t)hash0;
	addr[5] = (uint8_t)(hash1 >> 16);
	addr[6] = (uint8_t)(hash1 >> 8);
	addr[7] = (uint8_t)hash1;
	LOG_WRN("factory IEEE address invalid; using chip-ID-derived address");
}

static uint8_t phy6252_rssi_to_lqi(int8_t rssi_dbm)
{
	int16_t clamped = CLAMP((int16_t)rssi_dbm, -100, -20);

	return (uint8_t)(((clamped + 100) * 255) / 80);
}

static bool phy6252_is_ack(const uint8_t *psdu, uint8_t len)
{
	return (len >= 3U) && ((psdu[0] & 0x07U) == 0x02U);
}

static bool phy6252_filter_match(struct phy6252_data *data, const uint8_t *psdu, uint8_t len)
{
	uint16_t filter_pan_id;
	uint16_t frame_pan_id;
	uint16_t filter_short_addr;
	uint16_t frame_short_addr;
	uint8_t ieee_addr[sizeof(data->filter_ieee_addr)];
	bool pan_filter_enabled;
	bool short_filter_enabled;
	bool ieee_filter_enabled;
	bool promiscuous;
	uint16_t fcf;
	uint8_t dst_mode;
	uint8_t version;
	unsigned int key;

	key = irq_lock();
	filter_pan_id = data->filter_pan_id;
	filter_short_addr = data->short_addr;
	memcpy(ieee_addr, data->filter_ieee_addr, sizeof(ieee_addr));
	pan_filter_enabled = data->pan_filter_enabled;
	short_filter_enabled = data->short_filter_enabled;
	ieee_filter_enabled = data->ieee_filter_enabled;
	promiscuous = data->promiscuous;
	irq_unlock(key);

	if (promiscuous) {
		return true;
	}
	if (len < 2U) {
		return false;
	}

	fcf = sys_get_le16(psdu);
	dst_mode = (uint8_t)((fcf >> 10) & 0x03U);
	version = (uint8_t)((fcf >> 12) & 0x03U);
	if ((dst_mode == 0U) || (dst_mode == 1U) || (version == 2U)) {
		return true;
	}
	if (len < 5U) {
		return false;
	}

	frame_pan_id = sys_get_le16(&psdu[3]);
	if (pan_filter_enabled && (filter_pan_id != 0xFFFFU) && (frame_pan_id != 0xFFFFU) &&
	    (frame_pan_id != filter_pan_id)) {
		return false;
	}

	if (dst_mode == 2U) {
		if (len < 7U) {
			return false;
		}
		frame_short_addr = sys_get_le16(&psdu[5]);
		return !short_filter_enabled || (frame_short_addr == 0xFFFFU) ||
		       (frame_short_addr == 0xFFFCU) || (frame_short_addr == 0xFFFDU) ||
		       (frame_short_addr == filter_short_addr);
	}

	if (dst_mode == 3U) {
		if (len < 13U) {
			return false;
		}
		return !ieee_filter_enabled || (memcmp(&psdu[5], ieee_addr, sizeof(ieee_addr)) == 0);
	}

	return true;
}

static void phy6252_radio_set_channel(struct phy6252_data *data, uint16_t channel)
{
	uint8_t rf_channel = (uint8_t)((channel - PHY6252_CHANNEL_MIN) * 5U + 5U);
	uint8_t calibrated_channel = CLAMP(rf_channel, 2U, 66U);
	uint16_t index = (uint16_t)((calibrated_channel - 2U) >> 1);
	uint16_t cal0 = data->tp_cal0;
	uint16_t cal1 = data->tp_cal1;
	uint16_t cal = (cal1 >= cal0) ? (cal0 + (index * (cal1 - cal0)) / 32U) :
							 (cal0 - (index * (cal0 - cal1)) / 32U);

	data->channel = channel;
	phy6252_reg_write(PHY6252_RF_REG(0xB4U), rf_channel);
	phy6252_reg_write_field(PHY6252_RF_REG(0x94U), 7U, 0U, (uint8_t)cal);
}

static void phy6252_radio_set_tx_power(struct phy6252_data *data, int8_t dbm)
{
	uint32_t old_value = phy6252_reg_read(PHY6252_RF_REG(0xB8U));
	uint32_t power = (uint32_t)dbm & 0x1FU;

	phy6252_reg_write(PHY6252_RF_REG(0xB8U), (old_value & 0x0FFFU) | (power << 12));
	data->tx_power_dbm = dbm;
}

static uint8_t phy6252_rf_tp_calibrate(uint8_t channel)
{
	uint32_t old_value;
	uint8_t cap;

	phy6252_reg_write(PHY6252_RF_REG(0xB4U), channel);
	old_value = phy6252_reg_read(PHY6252_RF_REG(0x80U));
	phy6252_reg_write(PHY6252_RF_REG(0x80U), old_value | BIT(10));
	k_busy_wait(50U);
	cap = (uint8_t)(phy6252_reg_read(PHY6252_RF_REG(0x94U)) & 0xFFU);
	phy6252_reg_write(PHY6252_RF_REG(0x80U), old_value);

	return ((cap >= 0x05U) && (cap <= 0x3FU)) ? cap : 0x20U;
}

static void phy6252_radio_init_hw(struct phy6252_data *data)
{
	phy6252_reg_write_field(PHY6252_CLK_CTRL, 18U, 18U, 1U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 25U, 24U, 0U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 23U, 22U, 0U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 6U, 5U, 3U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 8U, 8U, 1U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 19U, 18U, 3U);
	phy6252_reg_write_field(PHY6252_RF_REG(0x80U), 0U, 0U, 1U);

	phy6252_reg_write(PHY6252_RF_REG(0xCCU), 0x20000BC0U);
	phy6252_reg_write(PHY6252_RF_REG(0xD0U), 0x00000140U);
	phy6252_reg_write(PHY6252_RF_REG(0xD4U), 0x04083F6AU);
	phy6252_reg_write(PHY6252_RF_REG(0xD8U), 0x04928000U);
	phy6252_reg_write(PHY6252_RF_REG(0x104U), 0x000000A0U);
	phy6252_reg_write(PHY6252_RF_REG(0x108U), 0x07083F9AU);
	phy6252_reg_write(PHY6252_RF_REG(0x10CU), 0x04910000U);
	phy6252_reg_write(PHY6252_RF_REG(0x80U), 0x000024CDU);
	phy6252_reg_write(PHY6252_RF_REG(0xDCU), 0x01BE7F0FU);
	phy6252_reg_write(PHY6252_RF_REG(0xB0U), 0x01000003U);

	phy6252_reg_write(PHY6252_RF_REG(0xE0U), 0x00000080U);
	phy6252_reg_write_field(PHY6252_RF_REG(0xD8U), 20U, 18U, 2U);
	phy6252_reg_write(PHY6252_RF_REG(0x90U), 0x00080000U);
	phy6252_reg_write(PHY6252_RF_REG(0x00U), 0x78068000U);
	phy6252_reg_write(PHY6252_RF_REG(0x48U), 0x00000000U);
	phy6252_reg_write(PHY6252_RF_REG(0x40U), 0x000B2800U);
	phy6252_reg_write(PHY6252_RF_REG(0x4CU), 0x3675EE07U);
	phy6252_reg_write(PHY6252_RF_REG(0x0CU), 128U);

	phy6252_reg_write(PHY6252_LL_REG(0x18U), 192U * 16U);
	phy6252_reg_write(PHY6252_LL_REG(0x1CU), 192U * 16U);
	phy6252_reg_write(PHY6252_LL_REG(0x08U), (80U * 16U << 16) | (80U * 16U));
	phy6252_reg_write(PHY6252_LL_REG(0x34U),
			  PHY6252_CRC_ZIGBEE | (PHY6252_CRC_ZIGBEE << 4));

	data->tp_cal0 = phy6252_rf_tp_calibrate(2U) + 4U;
	data->tp_cal1 = phy6252_rf_tp_calibrate(66U) + 4U;
	phy6252_radio_set_tx_power(data, 0);
	phy6252_radio_set_channel(data, PHY6252_CHANNEL_MIN);
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
}

static void phy6252_radio_abort(void)
{
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
}

static void phy6252_radio_rx_start(void)
{
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_SRX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), UINT16_MAX);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), PHY6252_IRQ_MD);
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 1U);
}

static void phy6252_measure_rssi(struct phy6252_data *data, bool resume_rx, int8_t *rssi)
{
	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_SRX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), 0x0800U);
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 1U);
	k_busy_wait(PHY6252_CCA_SETTLE_US);
	*rssi = (int8_t)(phy6252_reg_read(PHY6252_RF_REG(0xE4U)) >> 24);
	phy6252_radio_abort();

	if (resume_rx && (atomic_get(&data->started) != 0)) {
		phy6252_radio_rx_start();
	}

}

static bool phy6252_filter_frame(struct phy6252_data *data, const uint8_t *psdu, uint8_t len)
{
	return phy6252_filter_match(data, psdu, len);
}

static bool phy6252_frame_requires_ack(struct phy6252_data *data, const uint8_t *psdu,
				      uint8_t len)
{
	uint8_t ieee_addr[sizeof(data->ieee_addr)];
	uint16_t short_addr;
	uint16_t fcf;
	uint8_t frame_type;
	uint8_t dst_mode;
	uint8_t frame_version;
	bool promiscuous;
	bool short_filter_enabled;
	bool ieee_filter_enabled;
	unsigned int key;

	if (len < 3U) {
		return false;
	}

	fcf = sys_get_le16(psdu);
	frame_type = (uint8_t)(fcf & 0x07U);
	frame_version = (uint8_t)((fcf >> 12) & 0x03U);
	if (((fcf & BIT(5)) == 0U) ||
	    ((frame_type != 1U) && (frame_type != 3U)) || (frame_version > 1U)) {
		return false;
	}

	dst_mode = (uint8_t)((fcf >> 10) & 0x03U);
	key = irq_lock();
	promiscuous = data->promiscuous;
	short_filter_enabled = data->short_filter_enabled;
	short_addr = data->short_addr;
	ieee_filter_enabled = data->ieee_filter_enabled;
	memcpy(ieee_addr, ieee_filter_enabled ? data->filter_ieee_addr : data->ieee_addr,
	       sizeof(ieee_addr));
	irq_unlock(key);

	if (promiscuous) {
		return false;
	}

	if (dst_mode == 2U) {
		uint16_t frame_short_addr;

		if ((len < 7U) || !short_filter_enabled) {
			return false;
		}

		frame_short_addr = sys_get_le16(&psdu[5]);
		return (frame_short_addr < 0xFFF8U) && (frame_short_addr == short_addr);
	}

	if (dst_mode == 3U) {
		return (len >= 13U) &&
		       (memcmp(&psdu[5], ieee_addr, sizeof(ieee_addr)) == 0);
	}

	return false;
}

static void phy6252_send_ack(uint8_t sequence, uint32_t rx_irq_entry_cycles)
{
	const uint32_t turnaround_cycles = k_us_to_cyc_ceil32(PHY6252_ACK_TURNAROUND_US);
	const uint32_t tx_timeout_cycles = k_us_to_cyc_ceil32(PHY6252_ACK_TX_TIMEOUT_US);
	volatile uint8_t *tx_fifo = (volatile uint8_t *)PHY6252_TX_FIFO;
	uint32_t tx_start_cycles;

	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_STX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_LL_REG(0x40U), 1U);

	/* The hardware appends the FCS; no pending-data bit is set in the FCF. */
	tx_fifo[0] = (uint8_t)(3U + PHY6252_FCS_LEN);
	tx_fifo[1] = 0x02U;
	tx_fifo[2] = 0U;
	tx_fifo[3] = sequence;

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), PHY6252_IRQ_MD);
	while ((k_cycle_get_32() - rx_irq_entry_cycles) < turnaround_cycles) {
	}
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 1U);

	tx_start_cycles = k_cycle_get_32();
	while ((phy6252_reg_read(PHY6252_LL_REG(0x10U)) & PHY6252_IRQ_MD) == 0U) {
		if ((k_cycle_get_32() - tx_start_cycles) >= tx_timeout_cycles) {
			phy6252_radio_abort();
			return;
		}
	}

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
}

static void phy6252_ack_received(struct phy6252_data *data, const uint8_t *psdu, uint8_t len,
				 int8_t rssi_dbm)
{
	if ((atomic_get(&data->ack_waiting) == 0) || !phy6252_is_ack(psdu, len) ||
	    (psdu[2] != (uint8_t)atomic_get(&data->expected_ack_seq))) {
		return;
	}

#if !defined(CONFIG_IEEE802154_RAW_MODE)
	if (data->iface != NULL) {
		struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, NET_AF_UNSPEC, 0,
								   K_NO_WAIT);

		if (pkt != NULL) {
			if (net_pkt_write(pkt, psdu, len) == 0) {
				net_pkt_cursor_init(pkt);
				net_pkt_set_ieee802154_rssi_dbm(pkt, rssi_dbm);
				net_pkt_set_ieee802154_lqi(pkt, phy6252_rssi_to_lqi(rssi_dbm));
				(void)ieee802154_handle_ack(data->iface, pkt);
			}
			net_pkt_unref(pkt);
		}
	}
#else
	ARG_UNUSED(rssi_dbm);
#endif

	k_sem_give(&data->ack_done);
}

static void phy6252_rx_irq(struct phy6252_data *data, uint32_t irq_status,
			   uint32_t rx_irq_entry_cycles)
{
	struct phy6252_rx_frame frame;
	const volatile uint8_t *rx_fifo = (const volatile uint8_t *)PHY6252_RX_FIFO;
	uint8_t psdu_len;
	uint8_t mpdu_len;
	int8_t rssi_dbm;

	if ((irq_status & PHY6252_IRQ_MD) == 0U) {
		return;
	}
	if ((irq_status & PHY6252_IRQ_COK) == 0U) {
		return;
	}

	psdu_len = rx_fifo[0];
	if ((psdu_len < (PHY6252_FCS_LEN + 3U)) || (psdu_len > PHY6252_MAX_PSDU)) {
		return;
	}
	mpdu_len = psdu_len - PHY6252_FCS_LEN;
	for (uint8_t i = 0U; i < psdu_len; i++) {
		frame.psdu[i] = rx_fifo[i + 1U];
	}
	rssi_dbm = (int8_t)(phy6252_reg_read(PHY6252_RF_REG(0xE4U)) >> 24);

	if (phy6252_is_ack(frame.psdu, mpdu_len)) {
		phy6252_ack_received(data, frame.psdu, mpdu_len, rssi_dbm);
		return;
	}
	if (!phy6252_filter_frame(data, frame.psdu, mpdu_len)) {
		return;
	}
	if (phy6252_frame_requires_ack(data, frame.psdu, mpdu_len)) {
		phy6252_send_ack(frame.psdu[2], rx_irq_entry_cycles);
	}

	frame.len = psdu_len;
	frame.rssi_dbm = rssi_dbm;
	if (k_msgq_put(&phy6252_rx_queue, &frame, K_NO_WAIT) == 0) {
		(void)k_work_submit(&data->rx_work);
	} else {
		LOG_DBG("RX queue full; dropping frame");
	}
}

static void phy6252_radio_isr(const void *arg)
{
	struct phy6252_data *data = (struct phy6252_data *)arg;
	uint32_t rx_irq_entry_cycles = k_cycle_get_32();
	uint32_t irq_status = phy6252_reg_read(PHY6252_LL_REG(0x10U));
	uint32_t mode;

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	mode = phy6252_reg_read(PHY6252_LL_REG(0x04U)) & 0x07U;

	if (((irq_status & PHY6252_IRQ_MD) != 0U) && (mode == PHY6252_LL_MODE_STX)) {
		k_sem_give(&data->tx_done);
	} else if ((mode == PHY6252_LL_MODE_SRX) &&
		   ((irq_status & (PHY6252_IRQ_MD | PHY6252_IRQ_RTO)) != 0U)) {
		phy6252_rx_irq(data, irq_status, rx_irq_entry_cycles);
	}

	if (atomic_get(&data->started) != 0) {
		phy6252_radio_rx_start();
	}
}

static void phy6252_rx_work_handler(struct k_work *work)
{
	struct phy6252_data *data = CONTAINER_OF(work, struct phy6252_data, rx_work);
	struct phy6252_rx_frame frame;
	struct net_pkt *pkt;
	uint8_t len;

	while (k_msgq_get(&phy6252_rx_queue, &frame, K_NO_WAIT) == 0) {
		if ((data->iface == NULL) || (atomic_get(&data->started) == 0)) {
			continue;
		}

		len = frame.len;
		if (!IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
			if (len <= PHY6252_FCS_LEN) {
				continue;
			}
			len -= PHY6252_FCS_LEN;
		}
		pkt = net_pkt_rx_alloc_with_buffer(data->iface, len, NET_AF_UNSPEC, 0, K_NO_WAIT);
		if (pkt == NULL) {
			continue;
		}
		if (net_pkt_write(pkt, frame.psdu, len) < 0) {
			net_pkt_unref(pkt);
			continue;
		}

		net_pkt_set_ieee802154_rssi_dbm(pkt, frame.rssi_dbm);
		net_pkt_set_ieee802154_lqi(pkt, phy6252_rssi_to_lqi(frame.rssi_dbm));
		if (net_recv_data(data->iface, pkt) < 0) {
			net_pkt_unref(pkt);
		}
	}
}

static int phy6252_lock(struct phy6252_data *data)
{
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	return k_mutex_lock(&data->op_lock, K_FOREVER);
}

static void phy6252_iface_init(struct net_if *iface)
{
	struct phy6252_data *data = net_if_get_device(iface)->data;

	phy6252_read_ieee_addr(data->ieee_addr);
	net_if_set_link_addr(iface, data->ieee_addr, sizeof(data->ieee_addr), NET_LINK_IEEE802154);
	data->iface = iface;
	ieee802154_init(iface);
}

static enum ieee802154_hw_caps phy6252_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER | IEEE802154_HW_PROMISC |
	       IEEE802154_HW_TX_RX_ACK | IEEE802154_HW_RX_TX_ACK;
}

static int phy6252_cca(const struct device *dev)
{
	struct phy6252_data *data = dev->data;
	int8_t rssi_dbm;
	int ret;

	ret = phy6252_lock(data);
	if (ret < 0) {
		return ret;
	}
	if (atomic_get(&data->started) == 0) {
		k_mutex_unlock(&data->op_lock);
		return -ENETDOWN;
	}

	phy6252_measure_rssi(data, true, &rssi_dbm);
	k_mutex_unlock(&data->op_lock);

	return (rssi_dbm > PHY6252_CCA_THRESHOLD_DBM) ? -EBUSY : 0;
}

static int phy6252_set_channel(const struct device *dev, uint16_t channel)
{
	struct phy6252_data *data = dev->data;
	int ret;

	if ((channel < PHY6252_CHANNEL_MIN) || (channel > PHY6252_CHANNEL_MAX)) {
		return -EINVAL;
	}
	ret = phy6252_lock(data);
	if (ret < 0) {
		return ret;
	}

	if (data->channel != channel) {
		if (atomic_get(&data->started) != 0) {
			phy6252_radio_abort();
		}
		phy6252_radio_set_channel(data, channel);
		if (atomic_get(&data->started) != 0) {
			phy6252_radio_rx_start();
		}
	}

	k_mutex_unlock(&data->op_lock);
	return 0;
}

static int phy6252_filter(const struct device *dev, bool set,
			  enum ieee802154_filter_type type,
			  const struct ieee802154_filter *filter)
{
	struct phy6252_data *data = dev->data;
	unsigned int key;

	if (set && (filter == NULL)) {
		return -EINVAL;
	}

	key = irq_lock();
	switch (type) {
	case IEEE802154_FILTER_TYPE_PAN_ID:
		data->pan_filter_enabled = set;
		data->filter_pan_id = set ? filter->pan_id : 0xFFFFU;
		break;
	case IEEE802154_FILTER_TYPE_SHORT_ADDR:
		data->short_filter_enabled = set;
		data->short_addr = set ? filter->short_addr : 0xFFFFU;
		break;
	case IEEE802154_FILTER_TYPE_IEEE_ADDR:
		if (set && (filter->ieee_addr == NULL)) {
			irq_unlock(key);
			return -EINVAL;
		}
		data->ieee_filter_enabled = set;
		if (set) {
			memcpy(data->filter_ieee_addr, filter->ieee_addr,
			       sizeof(data->filter_ieee_addr));
		} else {
			memset(data->filter_ieee_addr, 0, sizeof(data->filter_ieee_addr));
		}
		break;
	default:
		irq_unlock(key);
		return -ENOTSUP;
	}
	irq_unlock(key);
	return 0;
}

static int phy6252_set_txpower(const struct device *dev, int16_t dbm)
{
	struct phy6252_data *data = dev->data;
	unsigned int key;

	if ((dbm < 0) || (dbm > 10)) {
		return -EINVAL;
	}

	key = irq_lock();
	phy6252_radio_set_tx_power(data, (int8_t)dbm);
	irq_unlock(key);
	return 0;
}

static int phy6252_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		      struct net_pkt *pkt, struct net_buf *frag)
{
	struct phy6252_data *data = dev->data;
	const uint8_t *psdu;
	size_t len;
	bool ack_requested;
	int ret;

	ARG_UNUSED(pkt);
	if ((mode != IEEE802154_TX_MODE_DIRECT) && (mode != IEEE802154_TX_MODE_CCA)) {
		return -ENOTSUP;
	}
	if ((frag == NULL) || (frag->data == NULL) || (frag->len == 0U)) {
		return -EINVAL;
	}
	ret = phy6252_lock(data);
	if (ret < 0) {
		return ret;
	}
	if (atomic_get(&data->started) == 0) {
		ret = -ENETDOWN;
		goto out;
	}

	psdu = frag->data;
	len = frag->len;
	if (IS_ENABLED(CONFIG_IEEE802154_L2_PKT_INCL_FCS)) {
		if (len <= PHY6252_FCS_LEN) {
			ret = -EINVAL;
			goto out;
		}
		len -= PHY6252_FCS_LEN;
	}
	if ((len == 0U) || (len > PHY6252_MAX_MPDU)) {
		ret = -EINVAL;
		goto out;
	}

	if (mode == IEEE802154_TX_MODE_CCA) {
		int8_t rssi_dbm;

		phy6252_measure_rssi(data, false, &rssi_dbm);
		if (rssi_dbm > PHY6252_CCA_THRESHOLD_DBM) {
			phy6252_radio_rx_start();
			ret = -EBUSY;
			goto out;
		}
	}

	ack_requested = (len >= 3U) && ((psdu[0] & BIT(5)) != 0U);
	k_sem_reset(&data->tx_done);
	k_sem_reset(&data->ack_done);
	atomic_set(&data->expected_ack_seq, (len >= 3U) ? psdu[2] : 0U);
	atomic_set(&data->ack_waiting, ack_requested ? 1 : 0);
	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_STX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_LL_REG(0x40U), 1U);

	volatile uint8_t *tx_fifo = (volatile uint8_t *)PHY6252_TX_FIFO;

	tx_fifo[0] = (uint8_t)(len + PHY6252_FCS_LEN);
	for (size_t i = 0U; i < len; i++) {
		tx_fifo[i + 1U] = psdu[i];
	}

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), PHY6252_IRQ_MD);
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 1U);
	ret = k_sem_take(&data->tx_done, K_MSEC(PHY6252_TX_TIMEOUT_MS));
	if (ret != 0) {
		atomic_set(&data->ack_waiting, 0);
		if (atomic_get(&data->started) != 0) {
			phy6252_radio_rx_start();
		}
		ret = -EIO;
		goto out;
	}

	if (ack_requested) {
		ret = k_sem_take(&data->ack_done, K_MSEC(PHY6252_ACK_TIMEOUT_MS));
		atomic_set(&data->ack_waiting, 0);
		if (ret != 0) {
			ret = -ENOMSG;
			goto out;
		}
	}
	ret = 0;

out:
	k_mutex_unlock(&data->op_lock);
	return ret;
}

static int phy6252_start(const struct device *dev)
{
	struct phy6252_data *data = dev->data;
	int ret = phy6252_lock(data);

	if (ret < 0) {
		return ret;
	}
	if (atomic_cas(&data->started, 0, 1) == false) {
		k_mutex_unlock(&data->op_lock);
		return -EALREADY;
	}
	irq_enable(DT_INST_IRQN(0));
	phy6252_radio_rx_start();
	k_mutex_unlock(&data->op_lock);
	return 0;
}

static int phy6252_stop(const struct device *dev)
{
	struct phy6252_data *data = dev->data;
	int ret = phy6252_lock(data);

	if (ret < 0) {
		return ret;
	}
	if (atomic_cas(&data->started, 1, 0) == false) {
		k_mutex_unlock(&data->op_lock);
		return -EALREADY;
	}
	atomic_set(&data->ack_waiting, 0);
	phy6252_radio_abort();
	irq_disable(DT_INST_IRQN(0));
	k_msgq_purge(&phy6252_rx_queue);
	k_mutex_unlock(&data->op_lock);
	return 0;
}

static int phy6252_configure(const struct device *dev, enum ieee802154_config_type type,
			     const struct ieee802154_config *config)
{
	struct phy6252_data *data = dev->data;
	unsigned int key;

	if ((type == IEEE802154_CONFIG_PROMISCUOUS) && (config == NULL)) {
		return -EINVAL;
	}

	if (type == IEEE802154_CONFIG_PROMISCUOUS) {
		key = irq_lock();
		data->promiscuous = config->promiscuous;
		irq_unlock(key);
		return 0;
	}

	return -ENOTSUP;
}

IEEE802154_DEFINE_PHY_SUPPORTED_CHANNELS(phy6252_attr, PHY6252_CHANNEL_MIN,
					 PHY6252_CHANNEL_MAX);

static int phy6252_attr_get(const struct device *dev, enum ieee802154_attr attr,
			    struct ieee802154_attr_value *value)
{
	ARG_UNUSED(dev);

	return ieee802154_attr_get_channel_page_and_range(
		attr, IEEE802154_ATTR_PHY_CHANNEL_PAGE_ZERO_OQPSK_2450_BPSK_868_915,
		&phy6252_attr.phy_supported_channels, value);
}

static int phy6252_init(const struct device *dev)
{
	struct phy6252_data *data = dev->data;

	k_mutex_init(&data->op_lock);
	k_sem_init(&data->tx_done, 0, 1);
	k_sem_init(&data->ack_done, 0, 1);
	k_work_init(&data->rx_work, phy6252_rx_work_handler);
	data->channel = PHY6252_CHANNEL_MIN;
	data->filter_pan_id = 0xFFFFU;
	data->short_addr = 0xFFFFU;
	data->tx_power_dbm = 0;
	atomic_set(&data->started, 0);
	atomic_set(&data->ack_waiting, 0);
	atomic_set(&data->expected_ack_seq, 0);

	phy6252_radio_init_hw(data);
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority), phy6252_radio_isr,
		    &phy6252_data, 0);
	irq_disable(DT_INST_IRQN(0));

	return 0;
}

static const struct ieee802154_radio_api phy6252_radio_api = {
	.iface_api.init = phy6252_iface_init,
	.get_capabilities = phy6252_get_capabilities,
	.cca = phy6252_cca,
	.set_channel = phy6252_set_channel,
	.filter = phy6252_filter,
	.set_txpower = phy6252_set_txpower,
	.tx = phy6252_tx,
	.start = phy6252_start,
	.stop = phy6252_stop,
	.configure = phy6252_configure,
	.attr_get = phy6252_attr_get,
};

#if defined(CONFIG_IEEE802154_RAW_MODE)
DEVICE_DT_INST_DEFINE(0, phy6252_init, NULL, &phy6252_data, NULL, POST_KERNEL,
		      CONFIG_IEEE802154_PHY6252_INIT_PRIO, &phy6252_radio_api);
#elif defined(CONFIG_NET_L2_CUSTOM_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, phy6252_init, NULL, &phy6252_data, NULL,
			  CONFIG_IEEE802154_PHY6252_INIT_PRIO, &phy6252_radio_api,
			  CUSTOM_IEEE802154_L2,
			  NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2), PHY6252_MAX_MPDU);
#elif defined(CONFIG_NET_L2_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, phy6252_init, NULL, &phy6252_data, NULL,
			  CONFIG_IEEE802154_PHY6252_INIT_PRIO, &phy6252_radio_api,
			  IEEE802154_L2, NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  PHY6252_MAX_MPDU);
#endif

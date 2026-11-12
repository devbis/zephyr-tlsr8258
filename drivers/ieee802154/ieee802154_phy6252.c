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
#define PHY6252_BB_CRC_FMT          0x40030124U
#define PHY6252_LL_TFIFO            0x40031400U

#define PHY6252_CHANNEL_MIN         11U
#define PHY6252_CHANNEL_MAX         26U
#define PHY6252_MAX_PSDU            IEEE802154_MAX_PHY_PACKET_SIZE
#define PHY6252_FCS_LEN             IEEE802154_FCS_LENGTH
#define PHY6252_MAX_MPDU            (PHY6252_MAX_PSDU - PHY6252_FCS_LEN)
#define PHY6252_CCA_THRESHOLD_DBM   (-60)
#define PHY6252_CCA_SETTLE_US       128U
#define PHY6252_TX_TIMEOUT_MS       10U
#define PHY6252_ACK_TIMEOUT_MS      3U
#define PHY6252_ACK_TX_TIMEOUT_US   1000U
#define PHY6252_RX_QUEUE_DEPTH      16U

/*
 * Link-layer helpers of the PHY6222 mask ROM (SDK ll_hw_drv.h names). The shipping Zigbee
 * firmware for this chip uses them for the FIFOs instead of touching the FIFO RAM directly: the
 * TX FIFO is written through one data port with a hardware write pointer, the RX FIFO is read
 * through another, and both are reset through LL+0x58.
 */
#define ROM_LL_HW_RST_RFIFO      ((void (*)(void))0x000110b1U)
#define ROM_LL_HW_READ_RFIFO_ZB  ((uint8_t (*)(uint8_t *, uint16_t *, uint32_t *, uint32_t *)) \
				  0x00010f51U)

#define PHY6252_LL_IGN_CRC_EMPTY    0x03U
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
	atomic_t rx_stopping;
	struct k_sem rx_stopped;
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

static ALWAYS_INLINE uint32_t phy6252_reg_read(uint32_t addr)
{
	return *(volatile uint32_t *)addr;
}

static ALWAYS_INLINE void phy6252_reg_write(uint32_t addr, uint32_t value)
{
	*(volatile uint32_t *)addr = value;
}

static ALWAYS_INLINE void phy6252_reg_write_field(uint32_t addr, uint8_t high, uint8_t low,
						  uint32_t value)
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

static __ramfunc bool phy6252_is_ack(const uint8_t *psdu, uint8_t len)
{
	return (len >= 3U) && ((psdu[0] & 0x07U) == 0x02U);
}

static __ramfunc bool phy6252_filter_match(struct phy6252_data *data, const uint8_t *psdu,
					   uint8_t len)
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
	for (size_t i = 0U; i < sizeof(ieee_addr); i++) {
		ieee_addr[i] = data->filter_ieee_addr[i];
	}
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
		if (!ieee_filter_enabled) {
			return true;
		}
		for (size_t i = 0U; i < sizeof(ieee_addr); i++) {
			if (psdu[5U + i] != ieee_addr[i]) {
				return false;
			}
		}
		return true;
	}

	return true;
}

/*
 * Radio bring-up as in the PHY62x2 SDK (rf_phy_ini, rf_calibrate, ll_hw_set_timing for the
 * Zigbee packet format) and in the zigbee-rs backend that runs on a PB-03F (PHY6252) with
 * hardware FCS.
 */
#define PHY6252_PCR_SW_RESET2       0x4000000CU
#define PHY6252_BBLL_RST_BIT        10U
#define PHY6252_AON_PMU_CTRL        0x4000F014U
#define PHY6252_RX_TIMEOUT_US       0xFFFFU
#define PHY6252_RX_STOP_TIMEOUT_US  33U
#define PHY6252_RX_STOP_WAIT_MS     5U
#define PHY6252_HCLK_MHZ            (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000U)
#define PHY6252_TP_CAL_OFFSET       2U
#define PHY6252_TRX_SETTLE(bb, afe, pll) (((bb) << 16) | ((afe) << 8) | (pll))
/* ll_hw_set_trx_settle(57, 8, 52) of the ROM for the Zigbee packet format */
#define PHY6252_TRX_SETTLE_ZIGBEE   PHY6252_TRX_SETTLE(57U, 8U, 52U)
/* The turnaround of an ACK sent from the interrupt, measured with a sniffer */
#define PHY6252_TRX_SETTLE_ACK      PHY6252_TRX_SETTLE(118U, 8U, 52U)

struct phy6252_pa_entry {
	int8_t dbm;
	uint8_t code;
};

/* PA code of the QFN32 parts (RF_PHY_TX_POWER_* of the SDK), the highest power first */
static const struct phy6252_pa_entry phy6252_pa_table[] = {
	{5, 0x3f}, {0, 0x1f}, {-2, 0x0f}, {-5, 0x0a}, {-20, 0x01},
};

static __ramfunc uint8_t phy6252_rf_channel_index(uint16_t channel)
{
	return (uint8_t)(5U + 5U * (channel - PHY6252_CHANNEL_MIN));
}

/* RF_PHY_TPCAL_CALC() with the clamp that ll_hw_go() applies to the RF channel */
static __ramfunc uint8_t phy6252_tpcal_for_channel(const struct phy6252_data *data, uint8_t rf_chn)
{
	uint32_t index = (uint32_t)(CLAMP(rf_chn, 2U, 80U) - 2U) >> 1;
	uint32_t cal0 = data->tp_cal0;
	uint32_t cal1 = data->tp_cal1;

	if (cal0 > cal1) {
		return (uint8_t)(((cal0 << 5) - ((cal0 - cal1) * index) + 16U) >> 5);
	}
	return (uint8_t)cal0;
}

static void phy6252_radio_set_channel(struct phy6252_data *data, uint16_t channel)
{
	data->channel = channel;
	phy6252_reg_write(PHY6252_RF_REG(0xB4U), phy6252_rf_channel_index(channel));
}

/* ll_hw_go(): clear the status, unmask the mode done, trigger, then load the channel's cal */
static __ramfunc void phy6252_ll_go(void)
{
	struct phy6252_data *data = &phy6252_data;

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), PHY6252_IRQ_MD);
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 1U);
	phy6252_reg_write(PHY6252_RF_REG(0x94U),
			  0x1000U | phy6252_tpcal_for_channel(data,
						phy6252_rf_channel_index(data->channel)));
}

/* Reset the TX FIFO and load [PHR][PSDU] through its word-wide data port */
static __ramfunc void phy6252_tfifo_load(const uint8_t *buf, size_t len)
{
	phy6252_reg_write(PHY6252_LL_REG(0x58U), 0x0001U);
	for (size_t i = 0U; i < len; i += 4U) {
		uint32_t word = 0U;

		for (size_t j = 0U; (j < 4U) && ((i + j) < len); j++) {
			word |= (uint32_t)buf[i + j] << (8U * j);
		}
		phy6252_reg_write(PHY6252_LL_TFIFO, word);
	}
}

/* rf_phy_set_txPower(): the supply trims and the PA code in 0xB8[16:12] */
static void phy6252_radio_set_tx_power(struct phy6252_data *data, int8_t dbm)
{
	const struct phy6252_pa_entry *entry = &phy6252_pa_table[ARRAY_SIZE(phy6252_pa_table) - 1U];
	uint32_t b8;

	for (size_t i = 0U; i < ARRAY_SIZE(phy6252_pa_table); i++) {
		if (phy6252_pa_table[i].dbm <= dbm) {
			entry = &phy6252_pa_table[i];
			break;
		}
	}

	if (entry->code == 0x3fU) {
		phy6252_reg_write_field(PHY6252_AON_PMU_CTRL, 18U, 15U, 0x08U);
		phy6252_reg_write_field(PHY6252_RF_REG(0xCCU), 11U, 10U, 0U);
		phy6252_reg_write_field(PHY6252_RF_REG(0xDCU), 9U, 5U, 1U << 2);
	} else {
		phy6252_reg_write_field(PHY6252_AON_PMU_CTRL, 18U, 15U, 0x0AU);
		phy6252_reg_write_field(PHY6252_RF_REG(0xCCU), 11U, 10U, 2U);
		phy6252_reg_write_field(PHY6252_RF_REG(0xDCU), 9U, 5U, 1U);
	}
	b8 = phy6252_reg_read(PHY6252_RF_REG(0xB8U));
	phy6252_reg_write(PHY6252_RF_REG(0xB8U), (b8 & 0x0FFFU) | ((uint32_t)entry->code << 12));
	data->tx_power_dbm = dbm;
}

/* rf_tp_cal(rfChn, fDev = 1): the raw two-point modulation calibration code */
static uint8_t phy6252_rf_tp_calibrate(uint8_t rf_chn)
{
	const uint32_t bit23 = (PHY6252_HCLK_MHZ == 48U) ? 1U : 0U;
	uint8_t code;

	phy6252_reg_write(PHY6252_RF_REG(0x40U), 0x00030010U);
	phy6252_reg_write_field(PHY6252_RF_REG(0xD8U), 20U, 18U, 2U);
	phy6252_reg_write(PHY6252_RF_REG(0x8CU), 0x0053407FU);
	phy6252_reg_write_field(PHY6252_RF_REG(0x8CU), 23U, 23U, bit23);
	phy6252_reg_write(PHY6252_RF_REG(0xB4U), rf_chn);
	phy6252_reg_write(PHY6252_RF_REG(0xA0U), 0x0000000EU);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000000U);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000114U);
	k_busy_wait(3000U);
	code = (uint8_t)((phy6252_reg_read(PHY6252_RF_REG(0xF4U)) >> 16) & 0xFFU);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000000U);
	phy6252_reg_write(PHY6252_RF_REG(0xA0U), 0x00000000U);
	phy6252_reg_write(PHY6252_RF_REG(0x8CU), 0x00104040U);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000140U);
	phy6252_reg_write(PHY6252_RF_REG(0x40U), 0x00032800U);
	phy6252_reg_write_field(PHY6252_RF_REG(0x8CU), 23U, 23U, bit23);

	return code;
}

/* rf_rxDcoc_cfg(rfChn, bwSet = 1): the RX DC-offset calibration, 0x20200000 if it fails */
static uint32_t phy6252_rx_dcoc_calibrate(uint8_t rf_chn)
{
	const uint32_t flt_phy = 0x034DU;
	uint32_t result = 0x20200000U;
	int64_t deadline;

	phy6252_reg_write(PHY6252_LL_REG(0x24U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), 0U);
	phy6252_reg_write(PHY6252_RF_REG(0xB4U), rf_chn);
	phy6252_reg_write(PHY6252_RF_REG(0xA0U), 0x0000000EU);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000100U);
	phy6252_reg_write(PHY6252_RF_REG(0xA8U), 0x00000040U);
	phy6252_reg_write(PHY6252_RF_REG(0xACU), 0x00000050U);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x0000012AU);
	phy6252_reg_write(PHY6252_RF_REG(0xE0U), 0x00000100U);
	phy6252_reg_write(PHY6252_RF_REG(0xC8U), 0x000001A4U);
	phy6252_reg_write(PHY6252_RF_REG(0xC4U), 0x00002020U);
	phy6252_reg_write(PHY6252_RF_REG(0x50U), 0x200C5680U);
	k_busy_wait(100U);
	phy6252_reg_write(PHY6252_RF_REG(0x78U), (flt_phy << 22) | 0x00216564U);
	phy6252_reg_write(PHY6252_RF_REG(0x78U), (flt_phy << 22) | 0x00216565U);

	deadline = k_uptime_get() + 4;
	while (k_uptime_get() < deadline) {
		uint32_t ec = phy6252_reg_read(PHY6252_RF_REG(0xECU));

		if (((ec >> 30) & 0x3U) == 0x3U) {
			result = ec & 0x3F3FFFFFU;
			phy6252_reg_write(PHY6252_RF_REG(0xC4U), 0x00010000U | (result >> 16));
			break;
		}
	}

	phy6252_reg_write(PHY6252_RF_REG(0x78U), (flt_phy << 22) | 0x00216564U);
	phy6252_reg_write(PHY6252_RF_REG(0xA8U), 0U);
	phy6252_reg_write(PHY6252_RF_REG(0xACU), 0U);
	phy6252_reg_write(PHY6252_RF_REG(0xC8U), 0U);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000100U);
	phy6252_reg_write(PHY6252_RF_REG(0xA0U), 0U);
	phy6252_reg_write(PHY6252_RF_REG(0xA4U), 0x00000140U);

	return result;
}

/* Reset the LL engine (BBLL_RST) and clear its FIFOs and status */
static void phy6252_ll_reset(void)
{
	phy6252_reg_write_field(PHY6252_PCR_SW_RESET2, PHY6252_BBLL_RST_BIT, PHY6252_BBLL_RST_BIT,
				0U);
	phy6252_reg_write_field(PHY6252_PCR_SW_RESET2, PHY6252_BBLL_RST_BIT, PHY6252_BBLL_RST_BIT,
				1U);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x58U), 0x0100U);
	phy6252_reg_write(PHY6252_LL_REG(0x58U), 0x0001U);
}

/* rf_phy_ana_cfg() with the 16 MHz crystal as the RF PHY clock */
static void phy6252_radio_ana_cfg(void)
{
	phy6252_reg_write_field(PHY6252_CLK_CTRL, 18U, 18U, 1U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 25U, 24U, 2U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 23U, 22U, 0U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 6U, 5U, 3U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 8U, 8U, 1U);
	phy6252_reg_write_field(PHY6252_CLK_CFG, 19U, 18U, 3U);
	phy6252_reg_write_field(PHY6252_RF_REG(0x8CU), 23U, 23U,
				(PHY6252_HCLK_MHZ == 48U) ? 1U : 0U);

	phy6252_reg_write(PHY6252_RF_REG(0xCCU), 0x20000BC0U);
	phy6252_reg_write(PHY6252_RF_REG(0xD0U), 0x00000160U);
	phy6252_reg_write(PHY6252_RF_REG(0xD4U), 0x076A3E7AU);
	phy6252_reg_write(PHY6252_RF_REG(0xD8U), 0x04890000U);
	phy6252_reg_write(PHY6252_RF_REG(0xD0U), 0x00000180U);
	phy6252_reg_write(PHY6252_RF_REG(0xD4U), 0x076A3E7AU);
	phy6252_reg_write(PHY6252_RF_REG(0xD8U), 0x04890000U);
	phy6252_reg_write(PHY6252_RF_REG(0x104U), 0x00000180U);
	phy6252_reg_write(PHY6252_RF_REG(0x108U), 0x076A3E7AU);
	phy6252_reg_write(PHY6252_RF_REG(0x10CU), 0x04898000U);
	phy6252_reg_write(PHY6252_RF_REG(0x80U), 0x000024CCU);
	phy6252_reg_write_field(PHY6252_RF_REG(0x80U), 0U, 0U, 1U);
	phy6252_reg_write(PHY6252_RF_REG(0xB8U), 0x0825U);
	phy6252_reg_write(PHY6252_RF_REG(0xDCU), 0x01A6FC2FU);
}

/* rf_phy_bb_cfg() for the Zigbee packet format */
static void phy6252_radio_bb_cfg(const struct phy6252_data *data)
{
	static const uint32_t agc_table[] = {
		0x545C9CA4U, 0x4243444CU, 0x464C5241U, 0x2E343A40U, 0x557F0028U,
		0x3D43494FU, 0x4C2B3137U, 0x343A4046U, 0x1C22282EU,
	};

	phy6252_reg_write(PHY6252_RF_REG(0xE0U), 0x00000080U);
	phy6252_reg_write_field(PHY6252_RF_REG(0xD8U), 20U, 18U, 2U);
	phy6252_reg_write(PHY6252_RF_REG(0x90U), 0x00087000U);
	phy6252_reg_write(PHY6252_RF_REG(0xB0U), 0x01000002U);
	phy6252_reg_write(PHY6252_RF_REG(0x94U), 0x00001000U + data->tp_cal0);
	phy6252_reg_write(PHY6252_RF_REG(0x00U), 0x78068000U);
	phy6252_reg_write(PHY6252_RF_REG(0x48U), 0x00000000U);
	phy6252_reg_write(PHY6252_RF_REG(0x40U), 0x000B2800U);
	phy6252_reg_write(PHY6252_RF_REG(0x4CU), 0x3675EE07U);
	/* ll_hw_set_crc_fmt(): the TX format in bits 15:8, the RX format in 7:0 */
	phy6252_reg_write(PHY6252_BB_CRC_FMT, (PHY6252_CRC_ZIGBEE << 8) | PHY6252_CRC_ZIGBEE);
	phy6252_reg_write(PHY6252_RF_REG(0x50U), 0x22086680U);
	phy6252_reg_write_field(PHY6252_RF_REG(0x0CU), 7U, 0U, 0xFFU);
	for (size_t i = 0U; i < ARRAY_SIZE(agc_table); i++) {
		phy6252_reg_write(PHY6252_RF_REG(0x54U + (4U * i)), agc_table[i]);
	}
}

/* ll_hw_set_timing() of the ROM for the Zigbee format, in HCLK cycles, and the RX windows */
static void phy6252_radio_ll_cfg(void)
{
	phy6252_reg_write(PHY6252_LL_REG(0x20U),
			  ((10U * PHY6252_HCLK_MHZ) << 16) | PHY6252_HCLK_MHZ);
	phy6252_reg_write(PHY6252_LL_REG(0x1CU), 62U * PHY6252_HCLK_MHZ);
	phy6252_reg_write(PHY6252_LL_REG(0x18U), 72U * PHY6252_HCLK_MHZ);
	phy6252_reg_write(PHY6252_RF_REG(0xBCU), PHY6252_TRX_SETTLE_ZIGBEE);
	phy6252_reg_write(PHY6252_LL_REG(0x68U), PHY6252_LL_IGN_CRC_EMPTY);
	phy6252_reg_write(PHY6252_LL_REG(0x24U), PHY6252_RX_TIMEOUT_US);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), PHY6252_RX_TIMEOUT_US);
}

static void phy6252_radio_init_hw(struct phy6252_data *data)
{
	phy6252_ll_reset();
	phy6252_radio_ana_cfg();
	phy6252_radio_set_tx_power(data, 0);

	data->tp_cal0 = phy6252_rf_tp_calibrate(2U) + PHY6252_TP_CAL_OFFSET;
	data->tp_cal1 = phy6252_rf_tp_calibrate(66U) + PHY6252_TP_CAL_OFFSET;
	(void)phy6252_rx_dcoc_calibrate(88U);

	/* The calibration overwrites RF+0x40, 0x50 and 0xE0: the baseband is set up after it */
	phy6252_radio_bb_cfg(data);
	phy6252_radio_ll_cfg();
	phy6252_radio_set_channel(data, PHY6252_CHANNEL_MIN);

	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
}

static __ramfunc void phy6252_radio_abort(void)
{
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x0CU), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
}

static __ramfunc void phy6252_radio_rx_start(void)
{
	phy6252_reg_write(PHY6252_LL_REG(0x00U), 0U);
	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_LL_REG(0x58U), 0x0100U);
	phy6252_reg_write(PHY6252_LL_REG(0x24U), PHY6252_RX_TIMEOUT_US);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), PHY6252_RX_TIMEOUT_US);
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_SRX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_ll_go();
}

/*
 * Stop the receiver that runs continuously: the RX window is cut to 33 us so the engine ends
 * with a timeout, or completes a frame it has already synchronized to. Stopping it by clearing
 * the trigger leaves the next transmit unable to start. Resetting the LL engine is the bounded
 * fallback. The receiver is not re-armed here.
 */
static void phy6252_rx_stop(struct phy6252_data *data)
{
	if (atomic_get(&data->started) == 0) {
		return;
	}

	k_sem_reset(&data->rx_stopped);
	atomic_set(&data->rx_stopping, 1);
	phy6252_reg_write(PHY6252_LL_REG(0x24U), PHY6252_RX_STOP_TIMEOUT_US);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), PHY6252_RX_STOP_TIMEOUT_US);
	if (k_sem_take(&data->rx_stopped, K_MSEC(PHY6252_RX_STOP_WAIT_MS)) != 0) {
		phy6252_ll_reset();
		phy6252_radio_ll_cfg();
	}
	atomic_set(&data->rx_stopping, 0);
}

static void phy6252_measure_rssi(struct phy6252_data *data, bool resume_rx, int8_t *rssi)
{
	phy6252_rx_stop(data);
	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_SRX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_LL_REG(0x28U), 0x0800U);
	phy6252_ll_go();
	k_busy_wait(PHY6252_CCA_SETTLE_US);
	/* RF+0xE8 holds the received level as a positive magnitude, as the SDK reads it. */
	*rssi = -(int8_t)(phy6252_reg_read(PHY6252_RF_REG(0xE8U)) >> 24);
	phy6252_radio_abort();

	if (resume_rx && (atomic_get(&data->started) != 0)) {
		phy6252_radio_rx_start();
	}

}

static __ramfunc bool phy6252_filter_frame(struct phy6252_data *data, const uint8_t *psdu,
					   uint8_t len)
{
	return phy6252_filter_match(data, psdu, len);
}

static __ramfunc bool phy6252_frame_requires_ack(struct phy6252_data *data, const uint8_t *psdu,
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
	for (size_t i = 0U; i < sizeof(ieee_addr); i++) {
		ieee_addr[i] = ieee_filter_enabled ? data->filter_ieee_addr[i] : data->ieee_addr[i];
	}
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
		if (len < 13U) {
			return false;
		}
		for (size_t i = 0U; i < sizeof(ieee_addr); i++) {
			if (psdu[5U + i] != ieee_addr[i]) {
				return false;
			}
		}
		return true;
	}

	return false;
}

static __ramfunc void phy6252_send_ack(uint8_t sequence)
{
	const uint32_t tx_timeout_cycles = k_us_to_cyc_ceil32(PHY6252_ACK_TX_TIMEOUT_US);
	/* The hardware appends the FCS; no pending-data bit is set in the FCF. */
	const uint8_t ack[4] = {(uint8_t)(3U + PHY6252_FCS_LEN), 0x02U, 0U, sequence};
	uint32_t tx_start_cycles;

	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_STX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);
	phy6252_reg_write(PHY6252_RF_REG(0xBCU), PHY6252_TRX_SETTLE_ACK);
	/*
	 * Trigger first and load the frame right after, as the shipping firmware does: the
	 * baseband reads the TX FIFO only when the settle time has passed, and that settle
	 * time is what makes the acknowledgement start about a turnaround time after the
	 * end of the received frame.
	 */
	phy6252_ll_go();
	phy6252_tfifo_load(ack, sizeof(ack));

	tx_start_cycles = k_cycle_get_32();
	while ((phy6252_reg_read(PHY6252_LL_REG(0x10U)) & PHY6252_IRQ_MD) == 0U) {
		if ((k_cycle_get_32() - tx_start_cycles) >= tx_timeout_cycles) {
			phy6252_radio_abort();
			phy6252_reg_write(PHY6252_RF_REG(0xBCU), PHY6252_TRX_SETTLE_ZIGBEE);
			return;
		}
	}

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	phy6252_reg_write(PHY6252_RF_REG(0xBCU), PHY6252_TRX_SETTLE_ZIGBEE);
}

static __ramfunc void phy6252_ack_received(struct phy6252_data *data, const uint8_t *psdu,
					   uint8_t len, int8_t rssi_dbm)
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

static __ramfunc void phy6252_rx_irq(struct phy6252_data *data, uint32_t irq_status)
{
	struct phy6252_rx_frame frame;
	/* The ROM copies whole FIFO words: the PHR, the PSDU with its FCS and up to 3 bytes of pad. */
	uint8_t rx_fifo[PHY6252_MAX_PSDU + 4U + 1U];
	uint16_t rx_len = 0U;
	uint32_t foot0 = 0U;
	uint32_t foot1 = 0U;
	uint8_t psdu_len;
	uint8_t mpdu_len;
	int8_t rssi_dbm;

	if ((irq_status & PHY6252_IRQ_MD) == 0U) {
		return;
	}
	if ((irq_status & PHY6252_IRQ_COK) == 0U) {
		return;
	}

	if (ROM_LL_HW_READ_RFIFO_ZB(rx_fifo, &rx_len, &foot0, &foot1) == 0U) {
		return;
	}
	ROM_LL_HW_RST_RFIFO();

	psdu_len = rx_fifo[0];
	if ((psdu_len < (PHY6252_FCS_LEN + 3U)) || (psdu_len > PHY6252_MAX_PSDU)) {
		return;
	}
	mpdu_len = psdu_len - PHY6252_FCS_LEN;
	for (uint8_t i = 0U; i < psdu_len; i++) {
		frame.psdu[i] = rx_fifo[i + 1U];
	}
	/* The high byte of the second footer word is the level as a positive magnitude. */
	rssi_dbm = -(int8_t)(foot1 >> 24);

	if (phy6252_is_ack(frame.psdu, mpdu_len)) {
		phy6252_ack_received(data, frame.psdu, mpdu_len, rssi_dbm);
		return;
	}
	if (!phy6252_filter_frame(data, frame.psdu, mpdu_len)) {
		return;
	}
	if (phy6252_frame_requires_ack(data, frame.psdu, mpdu_len)) {
		phy6252_send_ack(frame.psdu[2]);
	}

	frame.len = psdu_len;
	frame.rssi_dbm = rssi_dbm;
	if (k_msgq_put(&phy6252_rx_queue, &frame, K_NO_WAIT) == 0) {
		(void)k_work_submit(&data->rx_work);
	} else {
		LOG_DBG("RX queue full; dropping frame");
	}
}

static __ramfunc void phy6252_radio_isr(const void *arg)
{
	struct phy6252_data *data = (struct phy6252_data *)arg;
	uint32_t irq_status = phy6252_reg_read(PHY6252_LL_REG(0x10U));
	uint32_t mode;

	phy6252_reg_write(PHY6252_LL_REG(0x14U), PHY6252_IRQ_CLEAR);
	mode = phy6252_reg_read(PHY6252_LL_REG(0x04U)) & 0x07U;

	if (((irq_status & PHY6252_IRQ_MD) != 0U) && (mode == PHY6252_LL_MODE_STX)) {
		k_sem_give(&data->tx_done);
	} else if ((mode == PHY6252_LL_MODE_SRX) &&
		   ((irq_status & (PHY6252_IRQ_MD | PHY6252_IRQ_RTO)) != 0U)) {
		phy6252_rx_irq(data, irq_status);
	}

	if (atomic_get(&data->rx_stopping) != 0) {
		k_sem_give(&data->rx_stopped);
		return;
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
	phy6252_rx_stop(data);
	phy6252_radio_abort();
	phy6252_reg_write(PHY6252_LL_REG(0x04U), PHY6252_LL_STX_CONFIG);
	phy6252_reg_write(PHY6252_LL_REG(0x38U), PHY6252_LL_BYPASS_FIELDS);

	uint8_t tx_buf[PHY6252_MAX_PSDU + 1U];

	/* PHR (PSDU length including the FCS the hardware appends) followed by the PSDU. */
	tx_buf[0] = (uint8_t)(len + PHY6252_FCS_LEN);
	memcpy(&tx_buf[1], psdu, len);
	phy6252_tfifo_load(tx_buf, len + 1U);
	phy6252_ll_go();
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
	k_sem_init(&data->rx_stopped, 0, 1);
	atomic_set(&data->rx_stopping, 0);
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

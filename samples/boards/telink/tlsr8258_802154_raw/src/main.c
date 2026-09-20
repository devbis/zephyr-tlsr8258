/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define TLSR_REG8(addr)  (*(volatile uint8_t *)(0x00800000u + (addr)))
#define TLSR_REG16(addr) (*(volatile uint16_t *)(0x00800000u + (addr)))
#define TLSR_REG32(addr) (*(volatile uint32_t *)(0x00800000u + (addr)))

#define TCMD_UNDER_WR 0x80u
#define TCMD_MASK     0x3fu
#define TCMD_WRITE    0x03u

#define RF_MODE_ZIGBEE_250K BIT(3)
#define RF_TRX_MODE         0xe0u
#define RF_TRX_OFF          0x45u

#define RF_IRQ_RX          BIT(0)
#define RF_IRQ_TX          BIT(1)
#define RF_IRQ_RX_TIMEOUT  BIT(2)
#define RF_IRQ_CMD_DONE    BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_TX_DS       BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(12)
#define RF_IRQ_ALL         0xffffu

#define DMA_CHN_RF_RX BIT(2)
#define DMA_CHN_RF_TX BIT(3)

#define TX_WAIT_SPINS 200000u
#define RX_WAIT_SPINS 200000u

/*
 * This is the Telink RF channel offset, not yet a Zephyr ieee802154 channel
 * number. Vendor selftests use 11 here for the first 8258 bring-up.
 */
#define RAW_RF_CHANNEL_OFFSET_MHZ 11

struct tblcmdset {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

struct tlsr_802154_status {
	uint32_t marker;
	uint32_t init_count;
	uint32_t tx_attempts;
	uint32_t tx_ok;
	uint32_t tx_timeout;
	uint32_t rx_ok;
	uint32_t rx_timeout;
	uint32_t rx_bad_len;
	uint32_t rx_bad_crc;
	uint32_t last_tx_irq;
	uint32_t last_rx_irq;
	uint32_t last_rx_dma_len;
	uint32_t last_rx_psdu_len;
	uint32_t last_rssi;
	uint32_t dma_chn_en;
	uint32_t dma2_addr_hi;
	uint32_t dma3_addr_hi;
	uint32_t rf_ll_ctrl0;
	uint32_t rf_ll_ctrl3;
	uint32_t rf_irq_mask;
	uint32_t rf_irq_status;
};

volatile struct tlsr_802154_status tlsr_802154_status;

static uint8_t rx_buffer[256] __aligned(4);
static uint8_t tx_packet[11] __aligned(4);

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

static void sample_registers(void)
{
	tlsr_802154_status.dma_chn_en = TLSR_REG8(0x0c20);
	tlsr_802154_status.dma2_addr_hi = TLSR_REG8(0x0c42);
	tlsr_802154_status.dma3_addr_hi = TLSR_REG8(0x0c43);
	tlsr_802154_status.rf_ll_ctrl0 = TLSR_REG8(0x0f02);
	tlsr_802154_status.rf_ll_ctrl3 = TLSR_REG8(0x0f16);
	tlsr_802154_status.rf_irq_mask = TLSR_REG16(0x0f1c);
	tlsr_802154_status.rf_irq_status = TLSR_REG16(0x0f20);
}

static FUNC_NORETURN void park(uint32_t marker)
{
	tlsr_802154_status.marker = marker;
	sample_registers();

	for (;;) {
		compiler_barrier();
	}
}

static void load_tbl_cmd_set(const struct tblcmdset *tbl, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t cmd = tbl[i].cmd;

		if (((cmd & TCMD_UNDER_WR) != 0u) && ((cmd & TCMD_MASK) == TCMD_WRITE)) {
			TLSR_REG8(tbl[i].adr) = tbl[i].dat;
		}
	}
}

static void rf_set_channel(int8_t chn)
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

static void rf_set_power_level(uint8_t level)
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

static void rf_rx_buffer_set(uint8_t *buffer, uint16_t size)
{
	uintptr_t addr = (uintptr_t)buffer;

	TLSR_REG16(0x0c08) = (uint16_t)addr;
	TLSR_REG8(0x0c42) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG8(0x0c0a) = (uint8_t)(size >> 4);
	TLSR_REG8(0x0c0b) = 1u;
}

static void rf_prepare_common(void)
{
	load_tbl_cmd_set(tbl_rf_init, ARRAY_SIZE(tbl_rf_init));
	load_tbl_cmd_set(tbl_rf_zigbee_250k, ARRAY_SIZE(tbl_rf_zigbee_250k));

	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX | DMA_CHN_RF_TX;
	TLSR_REG8(0x0401) = 0u;
	TLSR_REG8(0x0404) &= (uint8_t)~BIT(5);
	TLSR_REG32(0x0408) = 0x29417671u;
	TLSR_REG8(0x0405) |= BIT(7);

	rf_set_channel(RAW_RF_CHANNEL_OFFSET_MHZ);
	rf_set_power_level(rf_power_level_list[23]);
	rf_rx_buffer_set(rx_buffer, sizeof(rx_buffer));

	TLSR_REG8(0x0f15) = 0xf0u;
	TLSR_REG16(0x0f04) = 113u;
	TLSR_REG8(0x0f03) &= (uint8_t)~BIT(2);
	TLSR_REG16(0x0f1c) = RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_CMD_DONE |
			     RF_IRQ_STX_TIMEOUT | RF_IRQ_RX | RF_IRQ_RX_TIMEOUT |
			     RF_IRQ_FSM_TIMEOUT;
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
}

static void rf_set_tx_mode(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x19u;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	rf_set_channel(RAW_RF_CHANNEL_OFFSET_MHZ);
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	TLSR_REG8(0x0428) &= (uint8_t)~BIT(0);
}

static void rf_set_rx_mode(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	rf_set_channel(RAW_RF_CHANNEL_OFFSET_MHZ);
	TLSR_REG8(0x0428) = RF_TRX_MODE | BIT(0);
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
}

static void prepare_tx_packet(void)
{
	tx_packet[0] = 7u;
	tx_packet[1] = 0u;
	tx_packet[2] = 0u;
	tx_packet[3] = 0u;
	tx_packet[4] = 8u;
	tx_packet[5] = 0x61u;
	tx_packet[6] = 0x88u;
	tx_packet[7] = 0x52u;
	tx_packet[8] = 0xadu;
	tx_packet[9] = 0x01u;
	tx_packet[10] = 0x00u;
}

static void rf_tx_pkt(uint8_t *packet)
{
	uintptr_t addr = (uintptr_t)packet;

	TLSR_REG8(0x0c43) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG16(0x0c0c) = (uint16_t)addr;
	TLSR_REG8(0x0c24) |= DMA_CHN_RF_TX;
}

static bool rx_length_ok(void)
{
	return rx_buffer[0] < (sizeof(rx_buffer) - 3u) &&
	       rx_buffer[0] == (uint8_t)(rx_buffer[4] + 9u);
}

static bool rx_crc_ok(void)
{
	return (rx_buffer[rx_buffer[0] + 3u] & 0x51u) == 0x10u;
}

int main(void)
{
	tlsr_802154_status.marker = 0x82586000u;
	prepare_tx_packet();
	rf_prepare_common();
	tlsr_802154_status.init_count++;
	sample_registers();

	rf_set_tx_mode();
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr_802154_status.tx_attempts++;
	rf_tx_pkt(tx_packet);

	for (uint32_t i = 0u; i < TX_WAIT_SPINS; i++) {
		uint16_t irq = TLSR_REG16(0x0f20);

		tlsr_802154_status.last_tx_irq = irq;
		if ((irq & (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_CMD_DONE)) != 0u) {
			TLSR_REG16(0x0f20) = irq;
			tlsr_802154_status.tx_ok++;
			goto tx_done;
		}
		if ((irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
			TLSR_REG16(0x0f20) = irq;
			tlsr_802154_status.tx_timeout++;
			park(0x8258e601u);
		}
	}

	tlsr_802154_status.tx_timeout++;
	park(0x8258e602u);

tx_done:
	rf_set_rx_mode();
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;

	for (uint32_t i = 0u; i < RX_WAIT_SPINS; i++) {
		uint16_t irq = TLSR_REG16(0x0f20);

		tlsr_802154_status.last_rx_irq = irq;
		if ((irq & RF_IRQ_RX) == 0u) {
			continue;
		}

		tlsr_802154_status.last_rx_dma_len = rx_buffer[0];
		tlsr_802154_status.last_rx_psdu_len = rx_buffer[4];
		TLSR_REG16(0x0f20) = irq;

		if (!rx_length_ok()) {
			tlsr_802154_status.rx_bad_len++;
			park(0x8258e603u);
		}
		tlsr_802154_status.last_rssi = rx_buffer[rx_buffer[0] + 2u];
		if (!rx_crc_ok()) {
			tlsr_802154_status.rx_bad_crc++;
			park(0x8258e604u);
		}

		tlsr_802154_status.rx_ok++;
		park(0x82580001u);
	}

	tlsr_802154_status.rx_timeout++;
	park(0x82580000u);
}

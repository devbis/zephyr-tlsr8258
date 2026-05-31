/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_zb

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_pkt.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>
#include <tlsr825x/irq.h>

#include "ieee802154_tlsr8258_tx_irq.h"
#include "ieee802154_tlsr8258_radio_op.h"
#include "ieee802154_tlsr8258_rf_irq.h"
#include "ieee802154_tlsr8258_rx_queue.h"

LOG_MODULE_REGISTER(ieee802154_tlsr8258, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

#define TLSR_REG8(addr)  (*(volatile uint8_t *)(0x00800000u + (addr)))
#define TLSR_REG16(addr) (*(volatile uint16_t *)(0x00800000u + (addr)))
#define TLSR_REG32(addr) (*(volatile uint32_t *)(0x00800000u + (addr)))

#define TCMD_UNDER_WR 0x80u
#define TCMD_MASK     0x3fu
#define TCMD_WRITE    0x03u

#define RF_TRX_MODE 0xe0u
#define RF_TRX_OFF  0x45u
#define RF_LL_MODE_TX  0u
#define RF_LL_MODE_RX  1u
#define RF_LL_MODE_OFF 3u

#define RF_IRQ_RX          BIT(0)
#define RF_IRQ_TX          BIT(1)
#define RF_IRQ_RX_TIMEOUT  BIT(2)
#define RF_IRQ_RX_CRC_2    BIT(4)
#define RF_IRQ_CMD_DONE    BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_RX_DR       BIT(9)
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
#define TLSR8258_ACK_TURNAROUND_US 120u
#define TLSR8258_ACK_REQUEST BIT(5)
#define TLSR8258_FRAME_PENDING BIT(4)
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
#define TLSR8258_RSSI_TO_LQI_MIN -87
#define TLSR8258_RSSI_TO_LQI_SCALE 3
#define TLSR8258_RX_WORKER_STACK_SIZE 768
#define TLSR8258_RX_SLOT_COUNT 4u
struct tblcmdset {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

struct tlsr8258_radio_data {
	struct net_if *iface;
	uint8_t mac_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint8_t rx_buffer[TLSR8258_RX_BUF_SIZE] __aligned(4);
	uint8_t rx_shadow[TLSR8258_RX_BUF_SIZE] __aligned(4);
	uint8_t tx_buffer[TLSR8258_TX_BUF_SIZE] __aligned(4);
	uint8_t filter_pan_id[TLSR8258_PAN_ID_SIZE];
	uint8_t filter_short_addr[TLSR8258_SHORT_ADDR_SIZE];
	uint8_t filter_ieee_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint16_t current_channel;
	uint16_t last_irq;
	uint32_t rx_count;
	uint32_t tx_count;
	struct tlsr8258_radio_op op;
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

static int tlsr8258_set_tx_payload(const uint8_t *payload, uint8_t payload_len);
static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size);
static void tlsr8258_rx_capture_isr(void);
static bool tlsr8258_filter_match_for_ack(const uint8_t *payload);
static bool tlsr8258_ack_requested(const uint8_t *payload, uint8_t length);

static struct tlsr8258_radio_data tlsr8258_radio;

K_KERNEL_STACK_MEMBER(tlsr8258_rx_worker_stack, TLSR8258_RX_WORKER_STACK_SIZE);
static struct k_sem tlsr8258_rx_sem;
static struct k_sem tlsr8258_tx_wait;
static struct k_thread tlsr8258_rx_worker_thread;
static struct tlsr8258_rx_queue tlsr8258_rx_queue;
static struct tlsr8258_rx_slot tlsr8258_rx_slots[TLSR8258_RX_SLOT_COUNT];

/*
 * Live RF ISR trace for hardware-only RX handoff debugging. Keep this in
 * retained RAM so the programmer can sample it after the failing join window.
 */
volatile uint32_t __noinit tlsr8258_rf_isr_entry_count;
volatile uint32_t __noinit tlsr8258_rf_isr_rx_event_count;
volatile uint32_t __noinit tlsr8258_rx_capture_debug_count;
volatile uint16_t __noinit tlsr8258_rf_irq_raw_debug;
volatile uint16_t __noinit tlsr8258_rf_irq_effective_debug;
volatile uint16_t __noinit tlsr8258_rf_irq_mask_debug;
volatile uint16_t __noinit tlsr8258_rf_irq_ack_debug;
volatile uint16_t __noinit tlsr8258_rx_capture_irq_debug;
volatile uint8_t __noinit tlsr8258_rf_dma_len_debug;
volatile uint8_t __noinit tlsr8258_rf_psdu_len_debug;
volatile uint8_t __noinit tlsr8258_rf_crc_debug;
volatile uint8_t __noinit tlsr8258_rf_branch_debug;
volatile uint32_t __noinit tlsr8258_tx_diag_trace[32];
volatile uint8_t __noinit tlsr8258_tx_diag_trace_head;

static void tlsr8258_tx_diag_put(uint32_t word)
{
	uint8_t head = tlsr8258_tx_diag_trace_head;

	tlsr8258_tx_diag_trace[head & (ARRAY_SIZE(tlsr8258_tx_diag_trace) - 1u)] = word;
	tlsr8258_tx_diag_trace_head = head + 1u;
}

static void tlsr8258_rf_debug_reset(void)
{
	tlsr8258_rf_isr_entry_count = 0u;
	tlsr8258_rf_isr_rx_event_count = 0u;
	tlsr8258_rx_capture_debug_count = 0u;
	tlsr8258_rf_irq_raw_debug = 0u;
	tlsr8258_rf_irq_effective_debug = 0u;
	tlsr8258_rf_irq_mask_debug = 0u;
	tlsr8258_rf_irq_ack_debug = 0u;
	tlsr8258_rx_capture_irq_debug = 0u;
	tlsr8258_rf_dma_len_debug = 0u;
	tlsr8258_rf_psdu_len_debug = 0u;
	tlsr8258_rf_crc_debug = 0u;
	tlsr8258_rf_branch_debug = 0u;
	memset((void *)tlsr8258_tx_diag_trace, 0, sizeof(tlsr8258_tx_diag_trace));
	tlsr8258_tx_diag_trace_head = 0u;
}

/*
 * TC32 LLVM has miscompiled direct accesses to the scalar tail of
 * tlsr8258_radio on hardware, e.g. tlsr8258_tx() read tx_buffer[4] where the
 * source asked for started. Use explicit offset-based volatile helpers for
 * those tail fields so codegen materializes the correct address.
 */
static inline volatile uint8_t *tlsr8258_radio_u8_field(size_t offset)
{
	return (volatile uint8_t *)((volatile uint8_t *)&tlsr8258_radio + offset);
}

static inline volatile uint16_t *tlsr8258_radio_u16_field(size_t offset)
{
	return (volatile uint16_t *)((volatile uint8_t *)&tlsr8258_radio + offset);
}

static inline volatile uint32_t *tlsr8258_radio_u32_field(size_t offset)
{
	return (volatile uint32_t *)((volatile uint8_t *)&tlsr8258_radio + offset);
}

static inline uint16_t tlsr8258_radio_current_channel_get(void)
{
	return *tlsr8258_radio_u16_field(offsetof(struct tlsr8258_radio_data, current_channel));
}

static inline void tlsr8258_radio_current_channel_set(uint16_t channel)
{
	*tlsr8258_radio_u16_field(offsetof(struct tlsr8258_radio_data, current_channel)) = channel;
}

static inline void tlsr8258_radio_last_irq_set(uint16_t irq)
{
	*tlsr8258_radio_u16_field(offsetof(struct tlsr8258_radio_data, last_irq)) = irq;
}

static inline void tlsr8258_radio_rx_count_inc(void)
{
	(*tlsr8258_radio_u32_field(offsetof(struct tlsr8258_radio_data, rx_count)))++;
}

static inline void tlsr8258_radio_tx_count_inc(void)
{
	(*tlsr8258_radio_u32_field(offsetof(struct tlsr8258_radio_data, tx_count)))++;
}

static inline bool tlsr8258_radio_started_get(void)
{
	return *tlsr8258_radio_u8_field(offsetof(struct tlsr8258_radio_data, started)) != 0u;
}

static inline void tlsr8258_radio_started_set(bool started)
{
	*tlsr8258_radio_u8_field(offsetof(struct tlsr8258_radio_data, started)) =
		started ? 1u : 0u;
}

static inline bool tlsr8258_radio_promiscuous_get(void)
{
	return *tlsr8258_radio_u8_field(offsetof(struct tlsr8258_radio_data, promiscuous)) != 0u;
}

static inline void tlsr8258_radio_promiscuous_set(bool promiscuous)
{
	*tlsr8258_radio_u8_field(offsetof(struct tlsr8258_radio_data, promiscuous)) =
		promiscuous ? 1u : 0u;
}

static tlsr8258_zigbee_rx_sink_t tlsr8258_zigbee_rx_sink;

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink)
{
	tlsr8258_zigbee_rx_sink = sink;
}

void tlsr8258_zigbee_update_filters(uint16_t pan_id, uint16_t short_addr,
				    const uint8_t *ieee_addr)
{
	sys_put_le16(pan_id, tlsr8258_radio.filter_pan_id);
	sys_put_le16(short_addr, tlsr8258_radio.filter_short_addr);
	if (ieee_addr != NULL) {
		memcpy(tlsr8258_radio.filter_ieee_addr, ieee_addr,
		       TLSR8258_IEEE_ADDR_SIZE);
	}
}

static void tlsr8258_load_tbl(const struct tblcmdset *tbl, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t cmd = tbl[i].cmd;

		if (((cmd & TCMD_UNDER_WR) != 0u) && ((cmd & TCMD_MASK) == TCMD_WRITE)) {
			TLSR_REG8(tbl[i].adr) = tbl[i].dat;
		}
	}
}

static void tlsr8258_rf_set_channel_offset(uint8_t chn)
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

static uint8_t tlsr8258_rf_channel_from_logical(uint16_t channel)
{
	return (uint8_t)((channel - 10u) * 5u);
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

static void tlsr8258_rf_tx_pkt(uint8_t *packet)
{
	uintptr_t addr = (uintptr_t)packet;

	TLSR_REG8(0x0c43) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG16(0x0c0c) = (uint16_t)addr;
	TLSR_REG8(0x0c24) |= DMA_CHN_RF_TX;
}

static inline void tlsr8258_rf_ll_mode_set(uint8_t mode)
{
	TLSR_REG8(0x0f16) = (uint8_t)((TLSR_REG8(0x0f16) & 0xfcu) | (mode & 0x03u));
}

static void tlsr8258_rf_set_rxmode(void)
{
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	tlsr8258_rf_set_channel_offset(
		tlsr8258_rf_channel_from_logical(tlsr8258_radio_current_channel_get()));
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
	TLSR_REG8(0x0428) = RF_TRX_MODE | BIT(0);
	tlsr8258_rf_ll_mode_set(RF_LL_MODE_RX);
}

static void tlsr8258_rf_set_txmode(void)
{
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	tlsr8258_rf_set_channel_offset(
		tlsr8258_rf_channel_from_logical(tlsr8258_radio_current_channel_get()));
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	TLSR_REG8(0x0428) &= (uint8_t)~BIT(0);
	tlsr8258_rf_ll_mode_set(RF_LL_MODE_TX);
}

static uint16_t tlsr8258_snapshot_rx_frame(uint8_t *dst, uint16_t dst_size)
{
	uint8_t *src = tlsr8258_radio.rx_buffer;
	uint16_t dma_len = (uint16_t)src[0] + 4u;
	uint16_t copy_len;

	if (dst == NULL || dst_size == 0u) {
		return 0u;
	}

	copy_len = MAX(dma_len, TLSR8258_PAYLOAD_OFFSET);
	copy_len = MIN(copy_len, (uint16_t)TLSR8258_RX_BUF_SIZE);
	copy_len = MIN(copy_len, dst_size);
	memcpy(dst, src, copy_len);

	return copy_len;
}

static uint8_t tlsr8258_mac_hdr_size(uint16_t fcf, uint8_t psdu_len)
{
	uint8_t idx = 3u;
	uint8_t dst_mode = (uint8_t)((fcf >> 10) & 0x03u);
	uint8_t src_mode = (uint8_t)((fcf >> 14) & 0x03u);

	if (psdu_len < idx) {
		return 0u;
	}

	if (dst_mode != 0u) {
		idx += TLSR8258_PAN_ID_SIZE;
		idx += (dst_mode == 0x03u) ? TLSR8258_IEEE_ADDR_SIZE : TLSR8258_SHORT_ADDR_SIZE;
	}

	if (src_mode != 0u) {
		if ((fcf & BIT(6)) == 0u) {
			idx += TLSR8258_PAN_ID_SIZE;
		}
		idx += (src_mode == 0x03u) ? TLSR8258_IEEE_ADDR_SIZE : TLSR8258_SHORT_ADDR_SIZE;
	}

	return (idx <= psdu_len) ? idx : 0u;
}

static bool tlsr8258_psdu_is_data_request(const uint8_t *psdu, uint8_t psdu_len)
{
	uint16_t fcf;
	uint8_t hdr_len;

	if (psdu == NULL || psdu_len < 4u) {
		return false;
	}

	fcf = sys_get_le16(psdu);
	if ((fcf & 0x0007u) != 0x03u) {
		return false;
	}

	hdr_len = tlsr8258_mac_hdr_size(fcf, psdu_len);
	if (hdr_len == 0u || hdr_len >= psdu_len) {
		return false;
	}

	return psdu[hdr_len] == 0x04u;
}

static bool tlsr8258_psdu_is_beacon_request(const uint8_t *psdu, uint8_t psdu_len)
{
	uint16_t fcf;
	uint8_t hdr_len;

	if (psdu == NULL || psdu_len < 4u) {
		return false;
	}

	fcf = sys_get_le16(psdu);
	if ((fcf & 0x0007u) != 0x03u) {
		return false;
	}

	hdr_len = tlsr8258_mac_hdr_size(fcf, psdu_len);
	if (hdr_len == 0u || hdr_len >= psdu_len) {
		return false;
	}

	return psdu[hdr_len] == 0x07u;
}

static bool tlsr8258_psdu_is_ack_for_seq(const uint8_t *psdu, uint8_t psdu_len, uint8_t seq)
{
	if (psdu == NULL || psdu_len < 3u) {
		return false;
	}

	return ((psdu[0] & 0x07u) == 0x02u) && (psdu[2] == seq);
}

static bool tlsr8258_psdu_is_pending_response(const uint8_t *psdu, uint8_t psdu_len, uint8_t seq)
{
	bool has_ack_match_fields = false;
	bool is_ack;
	bool rx_is_pending_response;

	if (psdu == NULL || psdu_len < (TLSR8258_DEST_ADDR_OFFSET + TLSR8258_SHORT_ADDR_SIZE)) {
		return false;
	}

	is_ack = tlsr8258_psdu_is_ack_for_seq(psdu, psdu_len, seq);
	switch (psdu[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) {
	case TLSR8258_DEST_ADDR_TYPE_SHORT:
		has_ack_match_fields = true;
		break;
	case TLSR8258_DEST_ADDR_TYPE_IEEE:
		has_ack_match_fields =
			psdu_len >= (TLSR8258_DEST_ADDR_OFFSET + TLSR8258_IEEE_ADDR_SIZE);
		break;
	default:
		break;
	}

	rx_is_pending_response = has_ack_match_fields && !is_ack &&
				     tlsr8258_filter_match_for_ack(psdu);
	/* Legacy equivalent: return has_ack_match_fields && !is_ack && ... */

	return rx_is_pending_response;
}

static void tlsr8258_rf_off(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	tlsr8258_rf_ll_mode_set(RF_LL_MODE_OFF);
}

static void tlsr8258_rf_init(void)
{
	tlsr8258_load_tbl(tbl_rf_init, ARRAY_SIZE(tbl_rf_init));
	tlsr8258_load_tbl(tbl_rf_zigbee_250k, ARRAY_SIZE(tbl_rf_zigbee_250k));

	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX | DMA_CHN_RF_TX;
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
	if (tlsr8258_radio_promiscuous_get()) {
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

#endif /* !CONFIG_IEEE802154_RAW_MODE */

static bool tlsr8258_filter_match_for_ack(const uint8_t *payload)
{
	uint16_t filter_pan = sys_get_le16(tlsr8258_radio.filter_pan_id);
	uint16_t payload_pan = sys_get_le16(&payload[TLSR8258_PAN_ID_OFFSET]);

	if ((filter_pan != 0xffffu) && (payload_pan != filter_pan) && (payload_pan != 0xffffu)) {
		return false;
	}

	switch (payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) {
	case TLSR8258_DEST_ADDR_TYPE_SHORT: {
		uint16_t filter_short = sys_get_le16(tlsr8258_radio.filter_short_addr);
		uint16_t payload_short = sys_get_le16(&payload[TLSR8258_DEST_ADDR_OFFSET]);

		if (payload_short == 0xffffu) {
			return true;
		}

		return (filter_short != 0xffffu) && (payload_short == filter_short);
	}
	case TLSR8258_DEST_ADDR_TYPE_IEEE:
		return memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], tlsr8258_radio.filter_ieee_addr,
			      TLSR8258_IEEE_ADDR_SIZE) == 0;
	default:
		return false;
	}
}

static bool tlsr8258_ack_requested(const uint8_t *payload, uint8_t length)
{
	if (length < TLSR8258_MIN_FRAME_LENGTH) {
		return false;
	}

	if ((payload[0] & TLSR8258_ACK_REQUEST) == 0u) {
		return false;
	}

	return (payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) != 0x02u;
}

static void tlsr8258_send_ack_if_needed(const uint8_t *payload, uint8_t length,
					bool tx_prepared, uint32_t tx_prepared_at_cycles)
{
	uint8_t ack_psdu[3] = {0x02u, 0x00u, 0u};
	uint16_t waited = 0u;
	uint32_t prep_elapsed_us = 0u;

	if (!tlsr8258_ack_requested(payload, length)) {
		return;
	}

	if (!tlsr8258_filter_match_for_ack(payload)) {
		if (tx_prepared) {
			tlsr8258_rf_set_rxmode();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
		}
		return;
	}

	ack_psdu[2] = payload[2];
	if (tlsr8258_set_tx_payload(ack_psdu, sizeof(ack_psdu)) < 0) {
		if (tx_prepared) {
			tlsr8258_rf_set_rxmode();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
		}
		return;
	}

	if (!tx_prepared) {
		tlsr8258_rf_set_txmode();
	} else {
		prep_elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - tx_prepared_at_cycles);
	}

	if (prep_elapsed_us < TLSR8258_ACK_TURNAROUND_US) {
		k_busy_wait(TLSR8258_ACK_TURNAROUND_US - prep_elapsed_us);
	}
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_tx_pkt(tlsr8258_radio.tx_buffer);
	tlsr8258_rf_ll_mode_set(RF_LL_MODE_TX);

	while (waited < 300u) {
		uint16_t irq = TLSR_REG16(0x0f20);

		if ((irq & (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_CMD_DONE)) != 0u) {
			TLSR_REG16(0x0f20) = irq;
			break;
		}

		k_busy_wait(1);
		waited++;
	}

	tlsr8258_rf_set_rxmode();
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
}

static int8_t tlsr8258_rx_rssi_dbm(const uint8_t *rx)
{
	if (rx[0] < (TLSR8258_RX_BUF_SIZE - 2u)) {
		return (int8_t)rx[rx[0] + 2u] - 110;
	}

	return -110;
}

static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size)
{
	uint8_t *rx = tlsr8258_radio.rx_buffer;
	uint8_t *payload = &rx[TLSR8258_PAYLOAD_OFFSET];
	uint16_t snapshot_len = 0u;
	uint16_t rx_dma_len;
	int8_t rx_rssi_dbm;
	uint8_t length = rx[4];
	bool ack_requested = tlsr8258_ack_requested(payload, length);
	uint32_t ack_prepared_at_cycles = 0u;

	tlsr8258_rx_capture_debug_count++;
	tlsr8258_rx_capture_irq_debug = irq_status;
	TLSR_REG16(0x0f20) = RF_IRQ_RX;
	tlsr8258_rf_irq_ack_debug = RF_IRQ_RX;
	tlsr8258_radio_rx_count_inc();
	if (ack_requested) {
		tlsr8258_rf_set_txmode();
		ack_prepared_at_cycles = k_cycle_get_32();
	}

	if ((snapshot != NULL) && (snapshot_size > 0u)) {
		snapshot_len = tlsr8258_snapshot_rx_frame(snapshot, snapshot_size);
		if (snapshot_len >= TLSR8258_PAYLOAD_OFFSET) {
			rx = snapshot;
			payload = &rx[TLSR8258_PAYLOAD_OFFSET];
			length = rx[4];
		}
	}

	rx_rssi_dbm = tlsr8258_rx_rssi_dbm(rx);
	rx_dma_len = (snapshot_len >= TLSR8258_PAYLOAD_OFFSET) ? snapshot_len : (uint16_t)rx[0] + 4u;
	rx_dma_len = MIN(rx_dma_len, UINT8_MAX);
	if (tlsr8258_rx_queue_try_enqueue(&tlsr8258_rx_queue, rx, (uint8_t)rx_dma_len, rx_rssi_dbm)) {
		k_sem_give(&tlsr8258_rx_sem);
	}
	tlsr8258_send_ack_if_needed(payload, length, ack_requested, ack_prepared_at_cycles);
}

static void tlsr8258_rx_dispatch(const struct tlsr8258_rx_frame *frame)
{
	const uint8_t *rx = frame->dma;
	struct tlsr8258_rx_frame_view view;
	int rc;
#if !defined(CONFIG_IEEE802154_RAW_MODE)
	uint8_t length;
	int8_t rssi;
	struct net_pkt *pkt;
#endif

	if (tlsr8258_zigbee_rx_sink != NULL) {
		view.dma = rx;
		view.len = frame->len;
		view.rssi_dbm = frame->rssi_dbm;
		rc = tlsr8258_zigbee_rx_sink(&view);
		if (rc < 0) {
			if (rc == -ENODATA) {
				LOG_DBG("zigbee RX sink deferred frame handling (len=%u)", frame->len);
			} else {
				LOG_WRN("zigbee RX sink rejected frame (rc=%d len=%u)", rc, frame->len);
			}
		}
		return;
	}

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

	if (!tlsr8258_filter_match((uint8_t *)&rx[TLSR8258_PAYLOAD_OFFSET])) {
		return;
	}

	pkt = net_pkt_rx_alloc_with_buffer(tlsr8258_radio.iface, length, NET_AF_UNSPEC, 0,
					   K_NO_WAIT);
	if (pkt == NULL) {
		return;
	}

	if (net_pkt_write(pkt, &rx[TLSR8258_PAYLOAD_OFFSET], length) < 0) {
		net_pkt_unref(pkt);
		return;
	}

	rssi = frame->rssi_dbm;
	net_pkt_set_ieee802154_rssi_dbm(pkt, rssi);
	net_pkt_set_ieee802154_lqi(pkt, tlsr8258_lqi_from_rssi(rssi));

	if (net_recv_data(tlsr8258_radio.iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
#endif
}

static void tlsr8258_rx_worker(void *arg1, void *arg2, void *arg3)
{
	struct tlsr8258_rx_frame frame;
	const uint8_t *psdu;
	uint8_t psdu_len;
	bool is_ack;
	bool ack_pending;
	bool is_pending_response;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_sem_take(&tlsr8258_rx_sem, K_FOREVER);
		while (tlsr8258_rx_queue_try_dequeue(&tlsr8258_rx_queue, &frame)) {
			psdu = NULL;
			psdu_len = 0u;
			is_ack = false;
			ack_pending = false;
			is_pending_response = false;
			if (frame.len >= TLSR8258_PAYLOAD_OFFSET) {
				psdu = &frame.dma[TLSR8258_PAYLOAD_OFFSET];
				psdu_len = frame.dma[4];
				is_ack = tlsr8258_psdu_is_ack_for_seq(
					psdu, psdu_len, tlsr8258_radio.op.tx_seq);
				ack_pending = is_ack && ((psdu[0] & TLSR8258_FRAME_PENDING) != 0u);
				{
					uint8_t tx_seq = tlsr8258_radio.op.tx_seq;

					is_pending_response = tlsr8258_psdu_is_pending_response(psdu, psdu_len, tx_seq);
				}
			}
			tlsr8258_rx_dispatch(&frame);
			{
				bool rx_complete;
				uint32_t key = irq_lock();

				rx_complete = (tlsr8258_radio.op.state ==
					       TLSR8258_RADIO_OP_WAITING_POST_TX_RX) &&
					      tlsr8258_radio_op_on_rx(&tlsr8258_radio.op, is_ack,
							      ack_pending,
							      is_pending_response);
				irq_unlock(key);
				if (rx_complete) {
					k_sem_give(&tlsr8258_tx_wait);
				}
			}
		}
	}
}

static void tlsr8258_rx_capture_isr(void)
{
	tlsr8258_rx_capture_common(RF_IRQ_RX, NULL, 0u);
}

static void tlsr8258_rf_isr(const void *unused)
{
	uint16_t irq = TLSR_REG16(0x0f20);
	uint16_t effective_irq = tlsr8258_rf_irq_effective_status(
		irq, tlsr8258_radio.rx_buffer, sizeof(tlsr8258_radio.rx_buffer));
	uint8_t dma_len = tlsr8258_radio.rx_buffer[0];
	uint8_t crc = 0u;

	ARG_UNUSED(unused);
	tlsr8258_rf_isr_entry_count++;
	tlsr8258_rf_irq_raw_debug = irq;
	tlsr8258_rf_irq_effective_debug = effective_irq;
	tlsr8258_rf_irq_mask_debug = TLSR_REG16(0x0f1c);
	tlsr8258_rf_dma_len_debug = dma_len;
	tlsr8258_rf_psdu_len_debug = tlsr8258_radio.rx_buffer[4];
	if ((dma_len != 0u) && (dma_len < (TLSR8258_RX_BUF_SIZE - 3u))) {
		crc = tlsr8258_radio.rx_buffer[dma_len + 3u];
	}
	tlsr8258_rf_crc_debug = crc;
	tlsr8258_radio_last_irq_set(effective_irq);

	if (tlsr8258_rf_irq_has_rx_event(effective_irq)) {
		tlsr8258_rf_branch_debug = 1u;
		tlsr8258_rf_isr_rx_event_count++;
		tlsr8258_rx_capture_isr();
	} else if ((effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u) {
		bool tx_complete;
		uint32_t key;

		tlsr8258_rf_branch_debug = 2u;
		tlsr8258_rf_irq_ack_debug = effective_irq;
		TLSR_REG16(0x0f20) = effective_irq;
		tlsr8258_radio_tx_count_inc();
		tlsr8258_rf_set_rxmode();
		key = irq_lock();
		tx_complete = (tlsr8258_radio.op.state == TLSR8258_RADIO_OP_TX_PENDING) &&
			      tlsr8258_radio_op_on_tx_success(&tlsr8258_radio.op);
		irq_unlock(key);
		if (tx_complete) {
			k_sem_give(&tlsr8258_tx_wait);
		}
	} else if ((effective_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
		bool tx_failed = false;
		uint32_t key;

		tlsr8258_rf_branch_debug = 3u;
		tlsr8258_rf_irq_ack_debug = effective_irq;
		TLSR_REG16(0x0f20) = effective_irq;
		tlsr8258_rf_set_rxmode();
		key = irq_lock();
		if (tlsr8258_radio.op.state == TLSR8258_RADIO_OP_TX_PENDING ||
		    tlsr8258_radio.op.state == TLSR8258_RADIO_OP_WAITING_POST_TX_RX) {
			tlsr8258_radio_op_on_tx_error(&tlsr8258_radio.op, -EIO);
			tx_failed = true;
		}
		irq_unlock(key);
		if (tx_failed) {
			k_sem_give(&tlsr8258_tx_wait);
		}
	} else {
		uint16_t ack = effective_irq != 0u ? effective_irq : RF_IRQ_ALL;

		tlsr8258_rf_branch_debug = (effective_irq != 0u) ? 4u : 5u;
		tlsr8258_rf_irq_ack_debug = ack;
		TLSR_REG16(0x0f20) = ack;
	}
}

static void tlsr8258_iface_init(struct net_if *iface)
{
	uint8_t *mac = tlsr8258_radio.mac_addr;
	const uint8_t *runtime_ieee = zb_platform_runtime_ieee_addr_get();

#if defined(CONFIG_IEEE802154_TLSR8258_RANDOM_MAC)
		sys_rand_get(mac, TLSR8258_IEEE_ADDR_SIZE);
		mac[0] = (mac[0] & (uint8_t)~BIT(0)) | BIT(1);
#else
		if (runtime_ieee != NULL) {
			memcpy(mac, runtime_ieee, TLSR8258_IEEE_ADDR_SIZE);
		} else {
			mac[0] = 0xc4;
			mac[1] = 0x19;
			mac[2] = 0xd1;
			mac[3] = 0x00;
			mac[4] = CONFIG_IEEE802154_TLSR8258_MAC4;
			mac[5] = CONFIG_IEEE802154_TLSR8258_MAC5;
			mac[6] = CONFIG_IEEE802154_TLSR8258_MAC6;
			mac[7] = CONFIG_IEEE802154_TLSR8258_MAC7;
		}
#endif

	net_if_set_link_addr(iface, mac, TLSR8258_IEEE_ADDR_SIZE, NET_LINK_IEEE802154);
	memcpy(tlsr8258_radio.filter_ieee_addr, mac, TLSR8258_IEEE_ADDR_SIZE);
	tlsr8258_radio.iface = iface;
	ieee802154_init(iface);
}

static enum ieee802154_hw_caps tlsr8258_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER | IEEE802154_HW_RX_TX_ACK;
}

static int tlsr8258_cca(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!tlsr8258_radio_started_get()) {
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

	if (tlsr8258_radio_current_channel_get() == channel) {
		return -EALREADY;
	}

	tlsr8258_radio_current_channel_set(channel);
	if (tlsr8258_radio_started_get()) {
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
	const uint16_t runtime_irq_mask =
		tlsr8258_rf_irq_runtime_mask() |
		RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT;

	ARG_UNUSED(dev);

	if (tlsr8258_radio_started_get()) {
		return -EALREADY;
	}

	TLSR_REG8(0x0401) = 0u;
	TLSR_REG8(0x0404) &= (uint8_t)~BIT(5);
	TLSR_REG8(0x0405) |= BIT(7);
	TLSR_REG8(0x0f15) = 0xf0u;
	TLSR_REG16(0x0f04) = 113u;
	TLSR_REG8(0x0f03) &= (uint8_t)~BIT(2);
	tlsr8258_rf_debug_reset();
	tlsr8258_rf_rx_buffer_set(tlsr8258_radio.rx_buffer, sizeof(tlsr8258_radio.rx_buffer));
	tlsr8258_radio.rx_buffer[0] = 0u;
	tlsr8258_radio.rx_buffer[4] = 0u;
	tlsr8258_rf_set_power_level(rf_power_level_list[23]);
	TLSR_REG8(0x0c21) &= (uint8_t)~(DMA_CHN_RF_RX | DMA_CHN_RF_TX);
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f1c) = runtime_irq_mask;
	TLSR_REG8(0x0430) |= BIT(1);
	tlsr8258_rf_set_rxmode();
	irq_enable(TLSR8258_IRQ_ZB_RT);
	tlsr8258_radio_started_set(true);

	return 0;
}

static int tlsr8258_stop(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (!tlsr8258_radio_started_get()) {
		return -EALREADY;
	}

	irq_disable(TLSR8258_IRQ_ZB_RT);
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_off();
	tlsr8258_radio_started_set(false);

	return 0;
}

static int tlsr8258_set_tx_payload(const uint8_t *payload, uint8_t payload_len)
{
	uint32_t dma_len;

	if (payload_len > (TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH)) {
		return -EINVAL;
	}

	/*
	 * Direct-register TLSR8258 implementations program the TX DMA header as
	 * a 32-bit transfer length: rf_len (1 byte) + payload + auto-appended CRC.
	 */
	/*
	 * The validated 8258 Zigbee TX path expects the DMA header to carry
	 * a plain byte length of (PSDU + length field). The PHY length at
	 * byte 4 still includes the auto-generated FCS.
	 */
	dma_len = (uint32_t)payload_len + 1u;
	tlsr8258_radio.tx_buffer[0] = (uint8_t)dma_len;
	tlsr8258_radio.tx_buffer[1] = (uint8_t)(dma_len >> 8);
	tlsr8258_radio.tx_buffer[2] = (uint8_t)(dma_len >> 16);
	tlsr8258_radio.tx_buffer[3] = (uint8_t)(dma_len >> 24);
	tlsr8258_radio.tx_buffer[4] = payload_len + TLSR8258_FCS_LENGTH;
	memcpy(&tlsr8258_radio.tx_buffer[TLSR8258_PAYLOAD_OFFSET], payload, payload_len);

	return 0;
}

static int tlsr8258_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		       struct net_pkt *pkt, struct net_buf *frag)
{
	uint8_t tx_seq = (frag != NULL && frag->len >= 3u) ? frag->data[2] : 0xffu;
	bool expect_ack;
	bool expect_post_tx_rx;
	uint32_t wait_budget_us;
	int ret;

	ARG_UNUSED(dev);
	ARG_UNUSED(pkt);

	if (!tlsr8258_radio_started_get()) {
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

	expect_ack = tlsr8258_ack_requested(frag->data, frag->len);
	expect_post_tx_rx = tlsr8258_psdu_is_data_request(frag->data, frag->len) ||
				 tlsr8258_psdu_is_beacon_request(frag->data, frag->len);
				 // || tlsr8258_psdu_is_assoc_request(frag->data, frag->len);
	wait_budget_us = expect_post_tx_rx ? 150000u : CONFIG_IEEE802154_TLSR8258_TX_WAIT_US;
	k_timeout_t wait_timeout = K_USEC(expect_post_tx_rx ? 150000u :
					      CONFIG_IEEE802154_TLSR8258_TX_WAIT_US);

	irq_disable(TLSR8258_IRQ_ZB_RT);
	k_sem_reset(&tlsr8258_tx_wait);
	tlsr8258_radio_op_prepare_tx(&tlsr8258_radio.op, tx_seq, expect_ack,
					 expect_post_tx_rx);
	tlsr8258_rf_set_txmode();
	k_busy_wait(120);
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_tx_diag_put((0x10u << 24) | ((uint32_t)tx_seq << 16) | (uint32_t)mode);
	tlsr8258_rf_tx_pkt(tlsr8258_radio.tx_buffer);
	irq_enable(TLSR8258_IRQ_ZB_RT);

	ret = k_sem_take(&tlsr8258_tx_wait, wait_timeout);
	if (ret == -EAGAIN) {
		tlsr8258_tx_diag_put((0x15u << 24) | ((uint32_t)tx_seq << 16) |
				     wait_budget_us);
		tlsr8258_radio_op_on_timeout(&tlsr8258_radio.op);
		tlsr8258_rf_set_rxmode();
		TLSR_REG16(0x0f20) = RF_IRQ_ALL;
		return tlsr8258_radio_op_result_errno(&tlsr8258_radio.op);
	}

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	return tlsr8258_radio_op_result_errno(&tlsr8258_radio.op);
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
		tlsr8258_radio_promiscuous_set(config->promiscuous);
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
	/*
	 * Guard against double-invocation.  The Zephyr kernel calls this via
	 * do_device_init() at POST_KERNEL level, but on TLSR8258 the .data
	 * section copy from flash LMA > boot-mirror window may fail, leaving
	 * device_state.init_res stale.  zb_radio_port_tlsr8258_get() works
	 * around that by calling device_init() as a fallback; the flag below
	 * prevents the k_thread_create path from running twice.
	 */
	static bool tlsr8258_hw_inited;

	ARG_UNUSED(dev);

	memset(&tlsr8258_radio, 0, sizeof(tlsr8258_radio));
	tlsr8258_rf_debug_reset();
	sys_put_le16(0xffffu, tlsr8258_radio.filter_pan_id);
	sys_put_le16(0xffffu, tlsr8258_radio.filter_short_addr);
	tlsr8258_radio_current_channel_set(11u);

	tlsr8258_rx_queue_init(&tlsr8258_rx_queue, tlsr8258_rx_slots, TLSR8258_RX_SLOT_COUNT);
	k_sem_init(&tlsr8258_rx_sem, 0, TLSR8258_RX_SLOT_COUNT);
	k_sem_init(&tlsr8258_tx_wait, 0, 1);

	if (!tlsr8258_hw_inited) {
		k_thread_create(&tlsr8258_rx_worker_thread, tlsr8258_rx_worker_stack,
				K_KERNEL_STACK_SIZEOF(tlsr8258_rx_worker_stack),
				tlsr8258_rx_worker, NULL, NULL, NULL,
				K_PRIO_COOP(2), 0, K_NO_WAIT);
		tlsr8258_hw_inited = true;
	}

	tlsr8258_rf_init();
	tlsr8258_rf_set_channel_offset(
		tlsr8258_rf_channel_from_logical(tlsr8258_radio_current_channel_get()));
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

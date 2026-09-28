/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT telink_tlsr8258_zb

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/ieee802154/tlsr8258.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ieee802154.h>
#include <zephyr/net/ieee802154_frame.h>
#include <zephyr/net/ieee802154_pkt.h>
#include <zephyr/net/ieee802154_radio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <tlsr825x/irq.h>
#include <tlsr825x/power.h>

#include "ieee802154_tlsr8258_tx_irq.h"
#include "ieee802154_tlsr8258_radio_op.h"
#include "ieee802154_tlsr8258_rf_irq.h"
#include "ieee802154_tlsr8258_rx_queue.h"
#include "ieee802154_tlsr8258_ack_filter.h"
#include "ieee802154_tlsr8258_fake_phy_core.h"

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
#define RF_IRQ_RX_CRC_2    BIT(4)
#define RF_IRQ_CMD_DONE    BIT(5)
#define RF_IRQ_FSM_TIMEOUT BIT(6)
#define RF_IRQ_RX_EVENTS   (RF_IRQ_RX | RF_IRQ_RX_CRC_2 | RF_IRQ_RX_DR)
#define RF_IRQ_RX_DR       BIT(9)
#define RF_IRQ_TX_DS       BIT(8)
#define RF_IRQ_STX_TIMEOUT BIT(12)
#define RF_IRQ_ALL         0xffffu

#define DMA_CHN_RF_RX BIT(2)
#define DMA_CHN_RF_TX BIT(3)

#define TLSR8258_RX_BUF_SIZE       144u
#define TLSR8258_RX_DMA_SIZE       144u
#define TLSR8258_TX_BUF_SIZE       132u
#define TLSR8258_PAYLOAD_OFFSET    5u
#define TLSR8258_PHY_MAX_PSDU      127u
#define TLSR8258_FCS_LENGTH        2u
#define TLSR8258_MIN_FRAME_LENGTH  3u
/*
 * Delay from RX completion to the start of the MAC ACK transmission. The
 * TX-mode switch, filter lookup and ACK setup all run inside this window, so
 * it is measured from the RX-complete timestamp and only the remainder is
 * spun. Starting a fresh window after entering TX mode makes the ACK miss
 * macAckWaitDuration (864 us) on the peer.
 */
#define TLSR8258_ACK_TURNAROUND_US 120u
/*
 * TC32 has no hardware divider, so converting (k_cycle_get_32() - start) to
 * microseconds inside the ACK hot path costs ~3-6us per call via the
 * software 32-bit divide emitted by k_cyc_to_us_floor32(). Pre-multiply the
 * turnaround once at compile time and compare in the cycle domain instead.
 */
#define TLSR8258_ACK_TURNAROUND_CYC                                                                \
	((uint32_t)TLSR8258_ACK_TURNAROUND_US * (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u))
#define TLSR8258_ACK_REQUEST           BIT(5)
#define TLSR8258_FRAME_PENDING         BIT(4)
#define TLSR8258_IEEE_ADDR_SIZE        8u
#define TLSR8258_SHORT_ADDR_SIZE       2u
#define TLSR8258_PAN_ID_SIZE           2u
#define TLSR8258_FRAME_TYPE_OFFSET     0u
#define TLSR8258_DEST_ADDR_TYPE_OFFSET 1u
#define TLSR8258_DEST_ADDR_TYPE_MASK   0x0cu
#define TLSR8258_DEST_ADDR_TYPE_SHORT  0x08u
#define TLSR8258_DEST_ADDR_TYPE_IEEE   0x0cu
#define TLSR8258_PAN_ID_OFFSET         3u
#define TLSR8258_DEST_ADDR_OFFSET      5u
#define TLSR8258_RSSI_TO_LQI_MIN       -87
#define TLSR8258_RSSI_TO_LQI_SCALE     3
#define TLSR8258_RX_SLOT_COUNT         16u

struct tblcmdset {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

struct tlsr8258_radio_config {
	void (*irq_config_func)(const struct device *dev);
};

struct tlsr8258_radio_data {
	struct net_if *iface;
	uint8_t mac_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint8_t rx_buffer[TLSR8258_RX_BUF_SIZE] __aligned(4);
	uint8_t rx_shadow[TLSR8258_RX_BUF_SIZE] __aligned(4);
	uint8_t tx_buffer[TLSR8258_TX_BUF_SIZE] __aligned(4);
	/*
	 * MAC ACKs are emitted from the RX ISR while a normal TX may still be
	 * completing in the RF/DMA state machine.  Keep their DMA descriptor in a
	 * separate buffer: reusing tx_buffer here can replace an encrypted NWK
	 * frame with the next frame's plaintext while DMA3 is still on it.
	 */
	uint8_t ack_buffer[TLSR8258_TX_BUF_SIZE] __aligned(4);
	/*
	 * Double-buffered RX (vendor mac_phy.c model). rx_active = the buffer the
	 * RF DMA is currently filling; on each RX-done the ISR swaps the DMA to the
	 * other of {rx_buffer, rx_shadow} BEFORE processing, so the next frame lands
	 * in a fresh buffer while rx_proc (the just-filled one) is consumed. This is
	 * what lets the router receive a frame arriving immediately after its own
	 * ACK-requested poll (the ASSOCIATION-RESPONSE) — a single buffer misses it.
	 */
	uint8_t *rx_active;
	uint8_t *rx_proc;
	uint8_t filter_pan_id[TLSR8258_PAN_ID_SIZE];
	uint8_t filter_short_addr[TLSR8258_SHORT_ADDR_SIZE];
	uint8_t filter_ieee_addr[TLSR8258_IEEE_ADDR_SIZE];
	uint16_t current_channel;
	uint16_t last_irq;
	uint32_t rx_count;
	uint32_t tx_count;
	struct tlsr8258_radio_op op;
	struct k_sem tx_wait;
	struct tlsr8258_rx_queue rx_queue;
	struct tlsr8258_rx_slot rx_slots[TLSR8258_RX_SLOT_COUNT];
	bool started;
	bool promiscuous;
};

/*
 * The RF ISR runs from RAM while the TLSR8258 XIP/cache path is in a
 * restricted state.  Do not make it dereference a Zephyr `struct device`
 * argument: the device object is in flash, and the `dev->data` load can
 * stall the core before the ISR has even recorded its diagnostics.  Pass
 * this RAM object directly as the IRQ argument instead.
 */
static struct tlsr8258_radio_data tlsr8258_radio_data_0;

static const struct tblcmdset tbl_rf_init[] = {
	{0x12d2, 0x9b, 0xc3}, {0x12d3, 0x19, 0xc3}, {0x127b, 0x0e, 0xc3},
	{0x1276, 0x50, 0xc3}, {0x1277, 0x73, 0xc3}, {0x0430, 0x3e, 0xc3},
};

static const struct tblcmdset tbl_rf_zigbee_250k[] = {
	{0x1220, 0x04, 0xc3}, {0x1221, 0x2b, 0xc3}, {0x1222, 0x43, 0xc3}, {0x1223, 0x86, 0xc3},
	{0x122a, 0x90, 0xc3}, {0x1254, 0x0e, 0xc3}, {0x1255, 0x09, 0xc3}, {0x1256, 0x0c, 0xc3},
	{0x1257, 0x08, 0xc3}, {0x1258, 0x09, 0xc3}, {0x1259, 0x0f, 0xc3}, {0x0400, 0x13, 0xc3},
	{0x0420, 0x18, 0xc3}, {0x0402, 0x46, 0xc3}, {0x0404, 0xc0, 0xc3}, {0x0405, 0x04, 0xc3},
	{0x0421, 0x23, 0xc3}, {0x0422, 0x04, 0xc3}, {0x0408, 0xa7, 0xc3}, {0x0409, 0x00, 0xc3},
	{0x040a, 0x00, 0xc3}, {0x040b, 0x00, 0xc3}, {0x0460, 0x36, 0xc3}, {0x0461, 0x46, 0xc3},
	{0x0462, 0x51, 0xc3}, {0x0463, 0x61, 0xc3}, {0x0464, 0x6d, 0xc3}, {0x0465, 0x78, 0xc3},
};

static const uint8_t rf_power_level_list[] = {
	0x3f, 0x3d, 0x3a, 0x38, 0x35, 0x33, 0x31, 0x2f, 0x2d, 0x2b, 0x29, 0x27, 0x25,
	0x23, 0x21, 0x1f, 0x1d, 0x1b, 0x19, 0x17, 0xbf, 0xbd, 0xbb, 0xb9, 0xb6, 0xb4,
	0xb2, 0xb0, 0xae, 0xac, 0xa9, 0xa8, 0xa4, 0xa2, 0xa0, 0x9e, 0x9c, 0x9a, 0x98,
	0x96, 0x94, 0x92, 0x90, 0x8e, 0x8c, 0x8a, 0x88, 0x86, 0x84, 0x82,
};

static int tlsr8258_set_tx_payload(struct tlsr8258_radio_data *radio, const uint8_t *payload,
				   uint8_t payload_len);
static int tlsr8258_set_tx_payload_to(uint8_t *tx_buffer, const uint8_t *payload,
				      uint8_t payload_len);
static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size, struct tlsr8258_radio_data *radio);
static void tlsr8258_rx_capture_isr(uint16_t irq_status, struct tlsr8258_radio_data *radio);
static void tlsr8258_rf_irq_reenable(void);
static void tlsr8258_rf_rx_buffer_set(uint8_t *buffer, uint16_t size);
static void tlsr8258_rf_set_rxmode_vendor(void);
static void tlsr8258_rf_rearm_idle_rx(struct tlsr8258_radio_data *radio);
static bool tlsr8258_rf_recover_stuck_rx(struct tlsr8258_radio_data *radio);
static bool tlsr8258_filter_match_for_ack(const uint8_t *payload, uint8_t length,
					  const struct tlsr8258_radio_data *radio);
static bool tlsr8258_ack_requested(const uint8_t *payload, uint8_t length);

static inline uint16_t tlsr8258_radio_current_channel_get(struct tlsr8258_radio_data *radio)
{
	return *(volatile uint16_t *)&radio->current_channel;
}

static inline void tlsr8258_radio_current_channel_set(struct tlsr8258_radio_data *radio,
						      uint16_t channel)
{
	*(volatile uint16_t *)&radio->current_channel = channel;
}

static inline void tlsr8258_radio_last_irq_set(struct tlsr8258_radio_data *radio, uint16_t irq)
{
	*(volatile uint16_t *)&radio->last_irq = irq;
}

static inline void tlsr8258_radio_rx_count_inc(struct tlsr8258_radio_data *radio)
{
	(*(volatile uint32_t *)&radio->rx_count)++;
}

static inline void tlsr8258_radio_tx_count_inc(struct tlsr8258_radio_data *radio)
{
	(*(volatile uint32_t *)&radio->tx_count)++;
}

static inline bool tlsr8258_radio_started_get(struct tlsr8258_radio_data *radio)
{
	return *(volatile uint8_t *)&radio->started != 0u;
}

static inline void tlsr8258_radio_started_set(struct tlsr8258_radio_data *radio, bool started)
{
	*(volatile uint8_t *)&radio->started = started ? 1u : 0u;
}

bool tlsr8258_pm_radio_can_suspend(void)
{
	return !tlsr8258_radio_started_get(&tlsr8258_radio_data_0);
}

static inline void tlsr8258_radio_promiscuous_set(struct tlsr8258_radio_data *radio,
						  bool promiscuous)
{
	*(volatile uint8_t *)&radio->promiscuous = promiscuous ? 1u : 0u;
}

/* ack_tx_pending is written by the RF ISR and read by the Zigbee thread. */
static inline bool tlsr8258_ack_tx_pending_get(struct tlsr8258_radio_data *radio)
{
	return *(volatile uint8_t *)&radio->op.ack_tx_pending != 0u;
}

static inline bool tlsr8258_radio_promiscuous_get(struct tlsr8258_radio_data *radio)
{
	return *(volatile uint8_t *)&radio->promiscuous != 0u;
}

static tlsr8258_rx_sink_t tlsr8258_rx_sink;
static tlsr8258_rx_notify_t tlsr8258_rx_notify;

/*
 * Filter, channel and address requests can arrive before tlsr8258_init()
 * runs, which clears radio_data. Keep them outside radio_data so init can
 * apply them.
 */
static uint8_t tlsr8258_filter_pan_id_shadow[TLSR8258_PAN_ID_SIZE] = {0xffu, 0xffu};
static uint8_t tlsr8258_filter_short_addr_shadow[TLSR8258_SHORT_ADDR_SIZE] = {0xffu, 0xffu};
static uint8_t tlsr8258_filter_ieee_addr_shadow[TLSR8258_IEEE_ADDR_SIZE];
static uint16_t tlsr8258_channel_shadow = 11u;

void tlsr8258_radio_register_rx_sink(tlsr8258_rx_sink_t sink)
{
	tlsr8258_rx_sink = sink;
}

void tlsr8258_radio_register_rx_notify(tlsr8258_rx_notify_t notify)
{
	tlsr8258_rx_notify = notify;
}

int tlsr8258_radio_get_ieee_addr(uint8_t ieee_addr[8])
{
	const struct tlsr8258_radio_data *radio = &tlsr8258_radio_data_0;

	if (radio->iface == NULL) {
		return -EAGAIN;
	}

	memcpy(ieee_addr, radio->mac_addr, TLSR8258_IEEE_ADDR_SIZE);
	return 0;
}

void tlsr8258_radio_update_filters(uint16_t pan_id, uint16_t short_addr, const uint8_t *ieee_addr)
{
	struct tlsr8258_radio_data *radio = &tlsr8258_radio_data_0;

	sys_put_le16(pan_id, tlsr8258_filter_pan_id_shadow);
	sys_put_le16(short_addr, tlsr8258_filter_short_addr_shadow);
	if (ieee_addr != NULL) {
		memcpy(tlsr8258_filter_ieee_addr_shadow, ieee_addr, TLSR8258_IEEE_ADDR_SIZE);
	}

	if (radio == NULL) {
		return;
	}

	sys_put_le16(pan_id, radio->filter_pan_id);
	sys_put_le16(short_addr, radio->filter_short_addr);
	if (ieee_addr != NULL) {
		memcpy(radio->filter_ieee_addr, ieee_addr, TLSR8258_IEEE_ADDR_SIZE);
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

/* Program the synthesizer and modem for a 2.4 GHz channel (11..26). */
static void tlsr8258_rf_set_channel(uint16_t channel)
{
	uint16_t physical;
	uint16_t freq_mhz;
	uint16_t modem_val;
	uint8_t band;

	if (channel < 11u || channel > 26u) {
		return;
	}

	physical = (uint16_t)(channel - 10u) * 5u;
	freq_mhz = (uint16_t)(2400u + physical);
	band = (freq_mhz > 2464u) ? 0x0cu : (freq_mhz > 2434u) ? 0x10u : 0x14u;

	/* Stop link-layer activity before retuning; the caller re-enters RX. */
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	TLSR_REG8(0x040d) = (uint8_t)physical;
	TLSR_REG16(0x04d6) = freq_mhz;
	modem_val = (uint16_t)((freq_mhz << 2) | 1u);
	TLSR_REG8(0x1244) = (uint8_t)modem_val;
	TLSR_REG8(0x1245) = (uint8_t)((TLSR_REG8(0x1245) & 0xc0u) | ((modem_val >> 8) & 0x3fu));
	TLSR_REG8(0x1229) = (uint8_t)((TLSR_REG8(0x1229) & 0xc3u) | band);

	for (uint32_t i = 0u; i < 2000u; i++) {
		__asm__ volatile("nop");
	}
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

	TLSR_REG8(0x1226) = (uint8_t)((TLSR_REG8(0x1226) & 0x7fu) | ((power_word >> 17) & 0x80u));
	TLSR_REG8(0x1227) = (uint8_t)((TLSR_REG8(0x1227) & 0xe0u) | ((power_word >> 25) & 0x1fu));
}

static void tlsr8258_rf_rx_buffer_set(uint8_t *buffer, uint16_t size)
{
	uintptr_t addr = (uintptr_t)buffer;
	ARG_UNUSED(size);

	/*
	 * Clear the RF RX status and the DMA2 completion latch, then toggle the
	 * RF RX DMA channel off and on around the buffer update. Re-pointing the
	 * buffer alone leaves the channel in its post-transfer state and the next
	 * frame is not written or signalled.
	 */
	TLSR_REG8(0x0f20) = RF_IRQ_RX;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_RX;
	TLSR_REG8(0x0c20) &= (uint8_t)~DMA_CHN_RF_RX;
	TLSR_REG16(0x0c08) = (uint16_t)addr;
	TLSR_REG8(0x0c42) = (uint8_t)((addr >> 16) & 0x0fu);
	TLSR_REG8(0x0c0a) = (uint8_t)(TLSR8258_RX_DMA_SIZE >> 4);
	TLSR_REG8(0x0c0b) = 1u;
	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX;
}

static bool tlsr8258_rf_recover_stuck_rx(struct tlsr8258_radio_data *radio)
{
	const uint32_t cpu_rx_sources = BIT(TLSR8258_IRQ_DMA) | BIT(TLSR8258_IRQ_ZB_RT);
	bool rf_rx_pending = (TLSR_REG16(0x0f20) & RF_IRQ_RX) != 0u;

	/*
	 * A pending RF completion with a live CPU source is left to the next
	 * vector. If the RF/DMA completion is latched but no CPU source is
	 * asserted, no vector will ever consume it: clear the module status and
	 * arm a fresh buffer without resetting the RF state machine.
	 */
	if (radio == NULL || !rf_rx_pending || (*TLSR8258_REG_IRQ_SRC & cpu_rx_sources) != 0u) {
		return false;
	}

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_RX;
	radio->rx_active[0] = 0u;
	radio->rx_active[4] = 0u;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	tlsr8258_rf_set_rxmode_vendor();
	return true;
}

static inline void tlsr8258_rf_tx_status_clear(void)
{
	/* RF TX-done, DMA3 completion, and the shared RF CPU source. */
	/*
	 * RF_IRQ_TX_DS is bit 8.  An 8-bit write silently discarded it and
	 * could leave the TX completion latch asserted after a software MAC ACK,
	 * blocking the next always-RX interrupt.
	 */
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;
}

static inline void tlsr8258_rf_cpu_irq_sources_clear(void)
{
	/*
	 * 0x0648-0x64a are IRQ source readbacks, not W1C registers.  The
	 * level-triggered CPU sources deassert only after their modules are clear.
	 */
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_RX | DMA_CHN_RF_TX;
}

/*
 * Clear the RF CPU sources unless a new RX completion is already latched, and
 * unmask the RF DMA and ZB_RT sources. The global interrupt gate is left to
 * irq_lock()/irq_unlock() and the interrupt exit path.
 */
__attribute__((noinline, section(".ram_code"))) static void tlsr8258_rf_irq_reenable(void)
{
	unsigned int key = irq_lock();

	if ((TLSR_REG16(0x0f20) & RF_IRQ_RX_EVENTS) == 0u) {
		tlsr8258_rf_cpu_irq_sources_clear();
	}
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() | BIT(TLSR8258_IRQ_DMA) |
				BIT(TLSR8258_IRQ_ZB_RT));
	irq_unlock(key);
}

static void tlsr8258_rf_tx_pkt(uint8_t *packet)
{
	uintptr_t addr = (uintptr_t)packet;

	/*
	 * Set both TX DMA ready latches. With reg_dma_tx_rdy1 (0x0c5b) left
	 * alone the RF reports a completed TX while the PSDU never reaches the
	 * air after an RX/TX turnaround.
	 */
	TLSR_REG8(0x0c43) = 0x04u;
	TLSR_REG16(0x0c0c) = (uint16_t)addr;
	TLSR_REG8(0x0c5b) |= DMA_CHN_RF_TX;
	TLSR_REG8(0x0c24) |= DMA_CHN_RF_TX;
}

/*
 * Switch to RX: RX enable in 0x0428, then RX in the link-layer state
 * register. There is deliberately no RF_TRX_OFF state-machine reset and no
 * channel reload in front of it. Either one leaves the receiver deaf for
 * hundreds of microseconds or more after a TX, and the peer's response to an
 * ACK-requested frame arrives inside that window. The reset belongs to the
 * explicit RF-off and channel-change paths.
 */
static void tlsr8258_rf_set_rxmode_vendor(void)
{
	TLSR_REG8(0x0428) = RF_TRX_MODE | BIT(0);
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
}

/* Restore RX after a TX timeout. Only called while the radio is started. */
static void tlsr8258_rf_rearm_idle_rx(struct tlsr8258_radio_data *radio)
{
	radio->rx_active[0] = 0u;
	radio->rx_active[4] = 0u;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	tlsr8258_rf_set_rxmode_vendor();
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
}

static void tlsr8258_rf_set_txmode(struct tlsr8258_radio_data *radio)
{
	ARG_UNUSED(radio);
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	TLSR_REG8(0x0428) &= (uint8_t)~BIT(0);
}

/*
 * Leave RX and clear the TX completion latches before every TX. Relying on
 * the state left by the preceding RX or MAC ACK can make a frame after a long
 * RX period not reach the air while the TX still reports completion.
 */
static void tlsr8258_rf_prepare_normal_tx(void)
{
	/* Stop the link-layer RX state without using the RF power-off command. */
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;

	/* Clear the TX RF and DMA completion latches owned by this operation. */
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;
	TLSR_REG8(0x0c0e) = (uint8_t)(TLSR8258_TX_BUF_SIZE >> 4);
	TLSR_REG8(0x0c0f) = 0u;
}

/*
 * TX-mode switch for a MAC ACK from the RX ISR: reset the link-layer state
 * machine, enable TX and disable the RX gate. No channel reload in this path.
 */
static void tlsr8258_rf_set_txmode_for_ack(void)
{
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	TLSR_REG8(0x0428) &= (uint8_t)~BIT(0);
}

static uint16_t tlsr8258_snapshot_rx_frame(struct tlsr8258_radio_data *radio, uint8_t *dst,
					   uint16_t dst_size)
{
	uint8_t *src = radio->rx_proc;
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

static uint8_t tlsr8258_dma_payload_len_get(const uint8_t *rx, uint16_t dma_total_len)
{
	uint8_t payload_len;
	uint8_t fallback_len;
	uint16_t available_len;

	if ((rx == NULL) || (dma_total_len < TLSR8258_PAYLOAD_OFFSET)) {
		return 0u;
	}

	available_len = dma_total_len - TLSR8258_PAYLOAD_OFFSET;
	payload_len = rx[4];
	if ((payload_len >= 2u) && (payload_len <= available_len)) {
		return payload_len;
	}

	if (rx[0] < 9u) {
		return 0u;
	}

	fallback_len = (uint8_t)(rx[0] - 9u);
	if ((fallback_len >= 2u) && (fallback_len <= available_len)) {
		return fallback_len;
	}

	return 0u;
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

/*
 * self-originated / src-matches-local / data-req / beacon-req helpers moved to
 * tlsr8258_core_* in ieee802154_tlsr8258_fake_phy_core.h and are now driven via
 * tlsr8258_core_rx_ack_decision() so the RX-ISR ACK decision is covered by the
 * tlsr8258_rx_ack_decision host unit test.
 */

static bool tlsr8258_psdu_is_ack_for_seq(const uint8_t *psdu, uint8_t psdu_len, uint8_t seq)
{
	return tlsr8258_core_psdu_is_ack_for_seq(psdu, psdu_len, seq);
}

static bool tlsr8258_psdu_is_pending_response(const uint8_t *psdu, uint8_t psdu_len, uint8_t seq,
					      const struct tlsr8258_radio_data *radio)
{
	struct tlsr8258_core_filter_ctx filter;
	struct tlsr8258_core_rx_result result;

	if (psdu == NULL) {
		return false;
	}

	filter = (struct tlsr8258_core_filter_ctx){
		.pan_id = radio->filter_pan_id,
		.short_addr = radio->filter_short_addr,
		.ieee_addr = radio->filter_ieee_addr,
	};
	tlsr8258_core_handle_rx_frame(psdu, psdu_len, seq, &filter, &result);

	return result.is_pending_response;
}

static void tlsr8258_rf_off(void)
{
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	/* Keep the vendor's fixed active-session LL mode across RF off/on. */
}

static void tlsr8258_rf_init(void)
{
	/* Required clock/reset release from the hardware-proven PHY init. */
	TLSR_REG8(0x0065) = 0xffu;
	TLSR_REG8(0x0060) = 0u;
	TLSR_REG8(0x0061) = 0u;
	TLSR_REG8(0x0062) = 0u;
	TLSR_REG8(0x0063) = 0xffu;
	TLSR_REG8(0x0064) = 0xffu;

	tlsr8258_load_tbl(tbl_rf_init, ARRAY_SIZE(tbl_rf_init));
	tlsr8258_load_tbl(tbl_rf_zigbee_250k, ARRAY_SIZE(tbl_rf_zigbee_250k));
}

/*
 * rx_length_ok / rx_crc_ok are used by the RX ISR (tlsr8258_rx_capture_common)
 * in every config, so they must live OUTSIDE the CONFIG_IEEE802154_RAW_MODE
 * (net-stack path) guard below.
 */
static bool tlsr8258_rx_length_ok(const uint8_t *rx)
{
	/*
	 * Vendor RF_ZIGBEE_PACKET_LENGTH_OK (platform/.../rf_drv.h,
	 * mac_phy.c rf_rx_irq_handler): a real Zigbee RX DMA buffer has its length
	 * header (rx[0]) consistent with the PHY payload-length byte (rx[4]):
	 *   rx[0] == rx[4] + 9
	 * On a busy channel the radio also DMAs noise / collisions whose rx[0]/rx[4]
	 * are inconsistent (garbage). The consistency check rejects those; also bound
	 * rx[0] so the CRC-status read rx[rx[0]+3] stays inside the RX buffer.
	 */
	if ((((uint16_t)rx[0] + 3u) >= TLSR8258_RX_BUF_SIZE) || (rx[4] > TLSR8258_PHY_MAX_PSDU)) {
		return false;
	}

	return (uint16_t)rx[0] == (uint16_t)rx[4] + 9u;
}

static bool tlsr8258_rx_crc_ok(const uint8_t *rx)
{
	return (rx[rx[0] + 3u] & 0x51u) == 0x10u;
}

/*
 * This filter is also used before the Zigbee RX sink is called.  The TLSR
 * sample enables CONFIG_IEEE802154_RAW_MODE because it bypasses Zephyr's
 * net_pkt path, but that does not make the Zigbee sink promiscuous: unrelated
 * frames must not consume the deferred RX slots while a joining ED is waiting
 * for its Association Response.
 */
static bool tlsr8258_filter_match(struct tlsr8258_radio_data *radio, uint8_t *payload)
{
	uint16_t filter_pan;
	uint8_t frame_type;

	if (tlsr8258_radio_promiscuous_get(radio)) {
		return true;
	}

	frame_type = payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u;
	if (frame_type == IEEE802154_FRAME_TYPE_BEACON) {
		/*
		 * Beacon frames carry no destination addressing, so the normal
		 * short/IEEE destination filter below would reject every active
		 * scan response once promiscuous mode is disabled. Router join
		 * relies on receiving coordinator beacons after our beacon
		 * request, and the higher MAC/NWK layers already validate PAN /
		 * payload content before using them.
		 */
		return true;
	}

	/*
	 * Mirror the auto-ACK filter's PAN logic: when our filter PAN is
	 * still the wildcard 0xFFFF (pre-association / pre-rejoin), accept
	 * frames addressed to our IEEE on ANY PAN. Otherwise the IEEE
	 * 802.15.4 ASSOCIATION-RESPONSE — sent by the coordinator with its
	 * own PAN ID and our extaddr as dst — is dropped by the receive
	 * filter even though the auto-ACK fires for it, and the joining
	 * device sits in tl_zbWaitForAssociationRespTimeout forever.
	 */
	filter_pan = sys_get_le16(radio->filter_pan_id);
	if ((filter_pan != 0xffffu) &&
	    memcmp(&payload[TLSR8258_PAN_ID_OFFSET], radio->filter_pan_id, TLSR8258_PAN_ID_SIZE) !=
		    0 &&
	    sys_get_le16(&payload[TLSR8258_PAN_ID_OFFSET]) != 0xffffu) {
		return false;
	}

	switch (payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) {
	case TLSR8258_DEST_ADDR_TYPE_SHORT:
		return memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], radio->filter_short_addr,
			      TLSR8258_SHORT_ADDR_SIZE) == 0 ||
		       sys_get_le16(&payload[TLSR8258_DEST_ADDR_OFFSET]) == 0xffffu;
	case TLSR8258_DEST_ADDR_TYPE_IEEE:
		return memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], radio->filter_ieee_addr,
			      TLSR8258_IEEE_ADDR_SIZE) == 0;
	default:
		return false;
	}
}

/*
 * Association Response is the one valid inbound frame that can be addressed
 * to our IEEE while the radio's PAN/short filters still describe the previous
 * network (or the pre-association state).  The hardware already ACKs it, so
 * the software receive filter must not ACK-and-drop it before MLME sees it.
 */
static bool tlsr8258_assoc_resp_for_us(const struct tlsr8258_radio_data *radio,
				       const uint8_t *payload, uint8_t length)
{
	uint16_t fcf;
	uint8_t hdr_len;

	if ((payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) != 0x03u ||
	    (payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) !=
		    TLSR8258_DEST_ADDR_TYPE_IEEE) {
		return false;
	}

	fcf = sys_get_le16(payload);
	hdr_len = tlsr8258_mac_hdr_size(fcf, length);

	return hdr_len != 0u && (uint16_t)(hdr_len + 4u) <= length &&
	       payload[hdr_len] == 0x02u &&      /* MAC_CMD_ASSOCIATION_RESPONSE */
	       payload[hdr_len + 3u] == 0x00u && /* MAC_SUCCESS */
	       memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], radio->filter_ieee_addr,
		      TLSR8258_IEEE_ADDR_SIZE) == 0;
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

/*
 * Thin wrappers over the pure, host-testable decision logic in
 * ieee802154_tlsr8258_ack_filter.h. Keep the actual matching rules THERE so
 * the host unit tests cover exactly what runs on hardware.
 */
static bool tlsr8258_filter_match_for_ack(const uint8_t *payload, uint8_t length,
					  const struct tlsr8258_radio_data *radio)
{
	struct tlsr8258_core_filter_ctx filter = {
		.pan_id = radio->filter_pan_id,
		.short_addr = radio->filter_short_addr,
		.ieee_addr = radio->filter_ieee_addr,
	};

	return tlsr8258_ackf_dst_matches_filter(payload, filter.pan_id, filter.short_addr,
						filter.ieee_addr) ||
	       tlsr8258_core_assoc_resp_to_ieee(payload, length, &filter);
}

static bool tlsr8258_ack_requested(const uint8_t *payload, uint8_t length)
{
	return tlsr8258_ackf_ack_requested(payload, length);
}

static void tlsr8258_send_ack_if_needed(const uint8_t *payload, uint8_t length, bool tx_prepared,
					uint32_t tx_prepared_at_cycles,
					uint32_t rx_complete_at_cycles,
					struct tlsr8258_radio_data *radio)
{
	/*
	 * The caller has already checked the ACK request bit. The turnaround is
	 * measured from rx_complete_at_cycles; only its remainder is spun here.
	 */
	uint8_t ack_psdu[3];
	uint32_t elapsed_cyc;

	/*
	 * During association the PAN/short filter still holds the pre-join
	 * values while the Association Response is addressed to our extended
	 * address. It is accepted by the receive filter and must be ACKed too,
	 * otherwise the coordinator keeps retransmitting it.
	 */
	bool ack_filter_match = tlsr8258_filter_match_for_ack(payload, length, radio);
	if (!ack_filter_match) {
		if (tx_prepared) {
			tlsr8258_rf_set_rxmode_vendor();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
		}
		return;
	}

	ack_psdu[0] = 0x02u;
	ack_psdu[1] = 0x00u;
	ack_psdu[2] = payload[2];
	if (tlsr8258_set_tx_payload_to(radio->ack_buffer, ack_psdu, sizeof(ack_psdu)) < 0) {
		if (tx_prepared) {
			tlsr8258_rf_set_rxmode_vendor();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
		}
		return;
	}

	if (!tx_prepared) {
		tlsr8258_rf_set_txmode(radio);
		tx_prepared_at_cycles = k_cycle_get_32();
	}

	do {
		elapsed_cyc = k_cycle_get_32() - rx_complete_at_cycles;
	} while (elapsed_cyc < TLSR8258_ACK_TURNAROUND_CYC);

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_tx_pkt(radio->ack_buffer);

	/*
	 * Complete the ACK before leaving the RX ISR: the short, software
	 * triggered ACK does not reliably raise a TX-done interrupt, and the
	 * radio would stay in TX and miss the next frame. The wait is bounded; a
	 * three-byte ACK completes well within it. RX DMA is re-armed by the
	 * caller.
	 */
	uint32_t ack_wait_start = k_cycle_get_32();
	uint32_t ack_wait_budget =
		(uint32_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 2000u; /* 500 us */

	while ((TLSR_REG16(0x0f20) & (RF_IRQ_TX | RF_IRQ_TX_DS)) == 0u &&
	       (k_cycle_get_32() - ack_wait_start) < ack_wait_budget) {
		k_busy_wait(1u);
	}

	tlsr8258_rf_tx_status_clear();
	tlsr8258_rf_set_rxmode_vendor();
	radio->op.ack_tx_pending = false;
}

static int8_t tlsr8258_rx_rssi_dbm(const uint8_t *rx)
{
	if (rx[0] < (TLSR8258_RX_BUF_SIZE - 2u)) {
		return (int8_t)rx[rx[0] + 2u] - 110;
	}

	return -110;
}

static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size, struct tlsr8258_radio_data *radio)
{
	uint8_t *rx = radio->rx_proc;
	uint8_t *payload = &rx[TLSR8258_PAYLOAD_OFFSET];
	uint16_t rx_ack = irq_status & RF_IRQ_RX_EVENTS;
	uint16_t snapshot_len = 0u;
	uint16_t rx_dma_len;
	int8_t rx_rssi_dbm;
	/* Reference point of the ACK turnaround. */
	uint32_t isr_entry_cycles = k_cycle_get_32();
	if (rx_ack == 0u) {
		rx_ack = RF_IRQ_RX;
	}
	TLSR_REG16(0x0f20) = rx_ack;
	tlsr8258_radio_rx_count_inc(radio);

	/*
	 * Drop frames with an inconsistent length before any parsing: on a busy
	 * channel the radio also writes noise and collisions with a garbage
	 * length byte, which must neither be parsed past the buffer nor ACKed.
	 * The caller has already armed the other buffer for the next frame.
	 */
	if (!tlsr8258_rx_length_ok(rx)) {
		tlsr8258_rf_set_rxmode_vendor();
		return;
	}
	if (!tlsr8258_rx_crc_ok(rx)) {
		/*
		 * RF_IRQ_RX is only raised for a frame with a good CRC. With the
		 * 0x0f03 profile used here the trailer status byte does not always
		 * agree, so treat RF_IRQ_RX as authoritative and drop only frames
		 * that raised the CRC2 event alone.
		 */
		if ((irq_status & RF_IRQ_RX) == 0u) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
	}

	uint8_t length = tlsr8258_dma_payload_len_get(rx, (uint16_t)rx[0] + 4u);
	uint32_t ack_prepared_at_cycles = 0u;
	bool ack_mode_prepared = false;
	bool ack_requested_early = tlsr8258_ack_requested(payload, length);

	/*
	 * Enter TX before any header or filter work so that the ACK meets the
	 * turnaround deadline.
	 */
	if (ack_requested_early) {
		tlsr8258_rf_set_txmode_for_ack();
		ack_prepared_at_cycles = k_cycle_get_32();
		ack_mode_prepared = true;
	}
	struct tlsr8258_core_filter_ctx ack_filter_ctx = {
		.pan_id = radio->filter_pan_id,
		.short_addr = radio->filter_short_addr,
		.ieee_addr = radio->filter_ieee_addr,
	};
	struct tlsr8258_core_rx_ack_decision ack_decision;

	/*
	 * Shared with the tlsr8258_rx_ack_decision host unit test (fake_phy_core.h)
	 * so the ISR's self-originated / ack-request / should-ACK decision is
	 * exercised deterministically off-hardware.
	 */
	tlsr8258_core_rx_ack_decision(payload, length, &ack_filter_ctx, &ack_decision);
	bool self_originated = ack_decision.self_originated;
	bool ack_requested = ack_decision.ack_requested;

	if (self_originated) {
		tlsr8258_rf_set_rxmode_vendor();
		return;
	}

	/*
	 * MAC ACK transmission, hybrid vendor pattern.  TLSR8258 will not drive
	 * the antenna from tx_pkt while the TRX state register sits in RX mode
	 * (the DMA fires but no frame appears on air), so the lightweight
	 * tlsr8258_rf_set_txmode_for_ack() is called first; it skips the
	 * PLL/channel reload, so the state transition is only ~5us. The
	 * ack_requested flag was decided once above; do NOT re-check inside
	 * tlsr8258_send_ack_if_needed() (the second look-up adds a redundant
	 * PSDU access on every RX while we are racing the coordinator's
	 * aTurnaroundTime window).
	 */
	if (ack_requested) {
		if (!ack_mode_prepared) {
			tlsr8258_rf_set_txmode_for_ack();
			ack_prepared_at_cycles = k_cycle_get_32();
		}
		tlsr8258_send_ack_if_needed(payload, length, true, ack_prepared_at_cycles,
					    isr_entry_cycles, radio);
	}

	/*
	 * Filter before a frame takes one of the RX slots, so that broadcasts
	 * and unicasts for other nodes cannot fill the queue while frames for
	 * this node are ACKed and then dropped. ACK frames are only kept while a
	 * TX is waiting for one.
	 */
	if ((payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) == 0x02u) {
		if ((radio->op.state != TLSR8258_RADIO_OP_TX_PENDING) &&
		    (radio->op.state != TLSR8258_RADIO_OP_WAITING_POST_TX_RX)) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
	} else if (tlsr8258_rx_sink != NULL) {
		if (!tlsr8258_filter_match(radio, payload) &&
		    !tlsr8258_assoc_resp_for_us(radio, payload, length)) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
	} else {
#if !defined(CONFIG_IEEE802154_RAW_MODE)
		if (!tlsr8258_filter_match(radio, payload) &&
		    !tlsr8258_assoc_resp_for_us(radio, payload, length)) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
#endif
	}

	/*
	 * A successful Association Response addressed to our extended address
	 * carries the PAN and short address assigned to us. The coordinator
	 * sends its next frames to that short address within a few milliseconds,
	 * before the MAC has applied it, so program it into the filter here.
	 * This runs after the ACK is sent to stay out of the ACK turnaround.
	 */
	if (((payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) == 0x03u) &&
	    ((payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) ==
	     TLSR8258_DEST_ADDR_TYPE_IEEE)) {
		uint16_t arsp_fcf = sys_get_le16(payload);
		uint8_t arsp_hdr = tlsr8258_mac_hdr_size(arsp_fcf, length);

		if ((arsp_hdr != 0u) && ((uint16_t)(arsp_hdr + 4u) <= length) &&
		    (payload[arsp_hdr] == 0x02u) &&      /* MAC_CMD_ASSOCIATION_RESPONSE */
		    (payload[arsp_hdr + 3u] == 0x00u) && /* MAC_SUCCESS */
		    (memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], radio->filter_ieee_addr,
			    TLSR8258_IEEE_ADDR_SIZE) == 0)) {
			radio->filter_pan_id[0] = payload[TLSR8258_PAN_ID_OFFSET];
			radio->filter_pan_id[1] = payload[TLSR8258_PAN_ID_OFFSET + 1u];
			radio->filter_short_addr[0] = payload[arsp_hdr + 1u];
			radio->filter_short_addr[1] = payload[arsp_hdr + 2u];
		}
	}

	if ((snapshot != NULL) && (snapshot_size > 0u)) {
		snapshot_len = tlsr8258_snapshot_rx_frame(radio, snapshot, snapshot_size);
		if (snapshot_len >= TLSR8258_PAYLOAD_OFFSET) {
			rx = snapshot;
			payload = &rx[TLSR8258_PAYLOAD_OFFSET];
			length = tlsr8258_dma_payload_len_get(rx, snapshot_len);
		}
	}

	rx_rssi_dbm = tlsr8258_rx_rssi_dbm(rx);
	rx_dma_len =
		(snapshot_len >= TLSR8258_PAYLOAD_OFFSET) ? snapshot_len : (uint16_t)rx[0] + 4u;
	rx_dma_len = MIN(rx_dma_len, (uint16_t)TLSR8258_RX_BUF_SIZE);
	if (tlsr8258_rx_queue_try_enqueue(&radio->rx_queue, rx, (uint8_t)rx_dma_len, rx_rssi_dbm) &&
	    (tlsr8258_rx_notify != NULL)) {
		tlsr8258_rx_notify();
	}
}

static void tlsr8258_rx_dispatch(struct tlsr8258_radio_data *radio,
				 const struct tlsr8258_rx_frame *frame)
{
	const uint8_t *rx = frame->dma;
	struct tlsr8258_rx_frame_view view;
	int rc;
#if !defined(CONFIG_IEEE802154_RAW_MODE)
	uint8_t length;
	int8_t rssi;
	struct net_pkt *pkt;
#endif

	if (tlsr8258_rx_sink != NULL) {
		view.dma = rx;
		view.len = frame->len;
		view.rssi_dbm = frame->rssi_dbm;
		rc = tlsr8258_rx_sink(&view);
		if (rc < 0) {
			if (rc == -ENODATA) {
				LOG_DBG("RX sink deferred frame (len=%u)", frame->len);
			} else {
				LOG_WRN("RX sink rejected frame (rc=%d len=%u)", rc, frame->len);
			}
		}
		return;
	}

#if defined(CONFIG_IEEE802154_RAW_MODE)
	ARG_UNUSED(rx);
#else
	if (radio->iface == NULL || !tlsr8258_rx_length_ok(rx) || !tlsr8258_rx_crc_ok(rx)) {
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

	if (!tlsr8258_filter_match(radio, (uint8_t *)&rx[TLSR8258_PAYLOAD_OFFSET]) &&
	    !tlsr8258_assoc_resp_for_us(radio, (uint8_t *)&rx[TLSR8258_PAYLOAD_OFFSET], length)) {
		return;
	}

	pkt = net_pkt_rx_alloc_with_buffer(radio->iface, length, NET_AF_UNSPEC, 0, K_NO_WAIT);
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

	if (net_recv_data(radio->iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
#endif
}

/*
 * Deliver queued frames. Runs only in the thread that calls
 * tlsr8258_radio_rx_poll(), the single consumer of rx_queue. TX completion
 * is signalled from the ISR and does not depend on this drain.
 */
static void tlsr8258_rx_drain_pending(struct tlsr8258_radio_data *radio)
{
	struct tlsr8258_rx_frame frame;

	while (tlsr8258_rx_queue_try_dequeue(&radio->rx_queue, &frame)) {
		bool is_ack = false;
		bool ack_pending = false;
		bool is_pending_response = false;

		if (frame.len >= TLSR8258_PAYLOAD_OFFSET) {
			const uint8_t *psdu = &frame.dma[TLSR8258_PAYLOAD_OFFSET];
			uint8_t psdu_len = tlsr8258_dma_payload_len_get(frame.dma, frame.len);
			uint8_t tx_seq = radio->op.tx_seq;

			is_ack = tlsr8258_psdu_is_ack_for_seq(psdu, psdu_len, tx_seq);
			ack_pending = is_ack && ((psdu[0] & TLSR8258_FRAME_PENDING) != 0u);
			is_pending_response =
				tlsr8258_psdu_is_pending_response(psdu, psdu_len, tx_seq, radio);
		}

		tlsr8258_rx_dispatch(radio, &frame);
		tlsr8258_rx_queue_release(&radio->rx_queue, frame.slot);

		{
			bool rx_complete;
			uint32_t key = irq_lock();

			rx_complete = (radio->op.state == TLSR8258_RADIO_OP_WAITING_POST_TX_RX) &&
				      tlsr8258_radio_op_on_rx(&radio->op, is_ack, ack_pending,
							      is_pending_response);
			irq_unlock(key);
			if (rx_complete) {
				k_sem_give(&radio->tx_wait);
			}
		}
	}
}

static void tlsr8258_rx_capture_isr(uint16_t irq_status, struct tlsr8258_radio_data *radio)
{
	tlsr8258_rx_capture_common(irq_status, NULL, 0u, radio);
}

/*
 * Placed in .ram_code, which the SoC keeps in the locked instruction cache,
 * so that the ACK path is not exposed to flash fetch latency.
 */
__attribute__((section(".ram_code"))) static void tlsr8258_rf_isr(const void *arg)
{
	struct tlsr8258_radio_data *radio = (struct tlsr8258_radio_data *)arg;
	uint16_t irq = TLSR_REG16(0x0f20);
	/*
	 * The RF RX status can be raised before DMA2 has written the length
	 * byte and trailer. Re-arming the buffer in that window discards the
	 * frame, so wait a bounded time (well inside the ACK deadline) for the
	 * buffer to become consistent.
	 */
	if ((irq & RF_IRQ_RX_EVENTS) != 0u &&
	    (!tlsr8258_rx_length_ok(radio->rx_active) || !tlsr8258_rx_crc_ok(radio->rx_active))) {
		for (uint32_t spin = 0u; spin < 40u; spin++) {
			if (tlsr8258_rx_length_ok(radio->rx_active) &&
			    tlsr8258_rx_crc_ok(radio->rx_active)) {
				break;
			}
			k_busy_wait(1u);
		}
	}
	uint16_t effective_irq =
		tlsr8258_rf_irq_effective_status(irq, radio->rx_active, TLSR8258_RX_BUF_SIZE);
	bool has_rx = tlsr8258_rf_irq_has_rx_event(effective_irq);
	bool has_tx = (effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u;

	tlsr8258_radio_last_irq_set(radio, effective_irq);

	/*
	 * ZB_RT is level triggered. If the raw status carried an RX event that
	 * tlsr8258_rf_irq_effective_status() dropped, no branch below clears it
	 * and the interrupt would fire again forever. Clear it and re-arm DMA2,
	 * whose completion latch would otherwise block the next frame.
	 */
	if (!has_rx && (irq & RF_IRQ_RX_EVENTS) != 0u) {
		TLSR_REG16(0x0f20) = RF_IRQ_RX_EVENTS;
		radio->rx_active[0] = 0u;
		radio->rx_active[4] = 0u;
		tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
		tlsr8258_rf_set_rxmode_vendor();
	}

	/*
	 * Handle TX completion before RX when both are reported in one ISR: a
	 * response can follow a data request quickly enough to be reported
	 * together with its TX done, and the TX operation must complete first.
	 */
	if (has_tx) {
		bool tx_complete;
		uint32_t key;
		bool ack_tx_completion;
		struct tlsr8258_core_tx_done_result tx_done_result;

		tlsr8258_rf_tx_status_clear();
		TLSR_REG16(0x0f20) = effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS);
		tlsr8258_radio_tx_count_inc(radio);
		{
			tlsr8258_core_handle_tx_done(
				effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS), has_rx,
				radio->op.ack_tx_pending, radio->op.expect_post_tx_rx,
				radio->op.state == TLSR8258_RADIO_OP_TX_PENDING, &tx_done_result);

			if (tx_done_result.enter_rx_fast) {
				/* Re-arm the RX buffer first, then switch to RX. */
				if (tx_done_result.rearm_rx_buffer) {
					radio->rx_active = tlsr8258_core_next_rx_buffer(
						radio->rx_active, radio->rx_buffer,
						radio->rx_shadow);
					radio->rx_active[0] = 0u;
					radio->rx_active[4] = 0u;
					tlsr8258_rf_rx_buffer_set(radio->rx_active,
								  TLSR8258_RX_BUF_SIZE);
				}
				tlsr8258_rf_set_rxmode_vendor();
			}
		}
		key = irq_lock();
		/*
		 * A completion of a MAC ACK sent from the RX ISR only clears the
		 * pending flag; it must not complete a stack TX.
		 */
		ack_tx_completion = tx_done_result.count_ack_tx_completion;
		if (tx_done_result.clear_ack_tx_pending) {
			radio->op.ack_tx_pending = false;
		}
		tx_complete = tx_done_result.complete_stack_tx &&
			      tlsr8258_radio_op_on_tx_success(&radio->op);
		irq_unlock(key);
		if (tx_complete) {
			k_sem_give(&radio->tx_wait);
		}
	}

	if (has_rx) {
		uint8_t rx_pass;

		/*
		 * A second frame can complete while the first one is ACKed and
		 * copied. Handle at most two here and leave anything later to the
		 * next interrupt.
		 */
		for (rx_pass = 0u; rx_pass < 2u; rx_pass++) {
			struct tlsr8258_core_rx_dma_result rx_dma_result;

			if (rx_pass != 0u) {
				irq = TLSR_REG16(0x0f20);
				effective_irq = tlsr8258_rf_irq_effective_status(
					irq, radio->rx_active, TLSR8258_RX_BUF_SIZE);
				if (!tlsr8258_rf_irq_has_rx_event(effective_irq)) {
					break;
				}
			}

			/*
			 * Double buffering: hand the filled buffer to rx_proc and point
			 * the DMA at the other one before parsing or ACKing.
			 */
			tlsr8258_core_handle_rx_dma(radio->rx_active, radio->rx_buffer,
						    radio->rx_shadow, &rx_dma_result);
			radio->rx_proc = rx_dma_result.rx_proc;
			radio->rx_active = rx_dma_result.next_rx_active;
			if (rx_dma_result.rearm_rx_buffer) {
				/*
				 * Keep DMA2 disabled until the completed frame has been
				 * handled: enabling it before the RF completion is cleared
				 * latches the same completion again and the interrupt
				 * keeps firing for one frame.
				 */
				radio->rx_active[0] = 0u;
				radio->rx_active[4] = 0u;
				TLSR_REG8(0x0c20) &= (uint8_t)~DMA_CHN_RF_RX;
			}

			tlsr8258_rx_capture_isr(effective_irq, radio);
			if (rx_dma_result.rearm_rx_buffer) {
				tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_BUF_SIZE);
				/*
				 * A MAC ACK is kicked from rx_capture_common() and its
				 * TX-done ISR owns the TX->RX transition.  Do not switch
				 * the RF state machine underneath that pending ACK.
				 */
				if (!radio->op.ack_tx_pending) {
					tlsr8258_rf_set_rxmode_vendor();
				}
			}
		}
		if (has_tx) {
			uint16_t residual_irq =
				effective_irq & ~(RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_RX_EVENTS);

			if (residual_irq != 0u) {
				TLSR_REG16(0x0f20) = residual_irq;
			}
		} else if (radio->op.state == TLSR8258_RADIO_OP_TX_PENDING) {
			/*
			 * The TX of an ACK-requested frame can be reported only through
			 * the RX of its ACK, without a TX status bit. An RX alone is no
			 * proof of TX completion, so complete only on the ACK for this
			 * TX's sequence number.
			 */
			bool tx_complete;
			uint32_t key;
			struct tlsr8258_core_rx_only_tx_result rx_only_tx_result;
			bool ack_for_tx = false;

			if (radio->op.expect_ack && radio->rx_proc != NULL) {
				const uint8_t *psdu = &radio->rx_proc[TLSR8258_PAYLOAD_OFFSET];
				uint8_t psdu_len = tlsr8258_dma_payload_len_get(
					radio->rx_proc, radio->rx_proc[0] + 4u);

				ack_for_tx = tlsr8258_psdu_is_ack_for_seq(psdu, psdu_len,
									  radio->op.tx_seq);
			}

			tlsr8258_core_handle_rx_only_tx_completion(
				false, radio->op.state == TLSR8258_RADIO_OP_TX_PENDING, ack_for_tx,
				&rx_only_tx_result);
			key = irq_lock();
			tx_complete = rx_only_tx_result.complete_stack_tx &&
				      tlsr8258_radio_op_on_tx_success(&radio->op);
			irq_unlock(key);
			if (tx_complete) {
				k_sem_give(&radio->tx_wait);
			}
			/* The TX-to-RX switch of the has_tx path did not run. */
			tlsr8258_rf_set_rxmode_vendor();
		}
	} else if (!has_tx && (effective_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
		bool tx_failed = false;
		uint32_t key;

		TLSR_REG16(0x0f20) = effective_irq;
		tlsr8258_rf_rearm_idle_rx(radio);
		key = irq_lock();
		if (radio->op.state == TLSR8258_RADIO_OP_TX_PENDING ||
		    radio->op.state == TLSR8258_RADIO_OP_WAITING_POST_TX_RX) {
			tlsr8258_radio_op_on_tx_error(&radio->op, -EIO);
			tx_failed = true;
		}
		irq_unlock(key);
		if (tx_failed) {
			k_sem_give(&radio->tx_wait);
		}
	} else {
		uint16_t ack = effective_irq != 0u ? effective_irq : RF_IRQ_ALL;

		if (has_tx) {
			ack &= ~(RF_IRQ_TX | RF_IRQ_TX_DS);
		}
		if (ack == 0u) {
			goto irq_reenable;
		}

		TLSR_REG16(0x0f20) = ack;
	}

irq_reenable:
	(void)tlsr8258_rf_recover_stuck_rx(radio);
	tlsr8258_rf_irq_reenable();
}

/*
 * The IEEE address lives in the first eight bytes of the factory data area,
 * where the Telink tools write it. Chips carrying a Telink OUI store it as
 * the six upper bytes followed by the two lower ones; other data is taken as
 * the address in over-the-air order. An erased area gets a random address
 * with the Telink OUI, stored in the same format so it survives reboots.
 */
static const uint8_t tlsr8258_telink_ouis[][3] = {
	{0x38u, 0xc1u, 0xa4u}, {0xd1u, 0x19u, 0xc4u}, {0xcbu, 0x0bu, 0xd8u}, {0x77u, 0x5fu, 0xd8u},
	{0xb4u, 0xcfu, 0x3cu}, {0xc7u, 0xa3u, 0xc0u}, {0x28u, 0x22u, 0x38u},
};

static bool tlsr8258_ieee_addr_has_telink_oui(const uint8_t *stored)
{
	for (size_t i = 0; i < ARRAY_SIZE(tlsr8258_telink_ouis); i++) {
		if (memcmp(&stored[3], tlsr8258_telink_ouis[i], 3) == 0) {
			return true;
		}
	}

	return false;
}

static bool tlsr8258_ieee_addr_is_erased(const uint8_t *stored)
{
	for (size_t i = 0; i < TLSR8258_IEEE_ADDR_SIZE; i++) {
		if (stored[i] != 0xffu) {
			return false;
		}
	}

	return true;
}

static void tlsr8258_ieee_addr_load(uint8_t mac[TLSR8258_IEEE_ADDR_SIZE])
{
#if defined(CONFIG_FLASH_MAP) && FIXED_PARTITION_EXISTS(ieee_addr_partition)
	const struct flash_area *fa;
	uint8_t stored[TLSR8258_IEEE_ADDR_SIZE];
	int rc;

	rc = flash_area_open(FIXED_PARTITION_ID(ieee_addr_partition), &fa);
	if (rc == 0) {
		rc = flash_area_read(fa, 0, stored, sizeof(stored));
	}

	if ((rc == 0) && !tlsr8258_ieee_addr_is_erased(stored)) {
		flash_area_close(fa);
		if (tlsr8258_ieee_addr_has_telink_oui(stored)) {
			memcpy(&mac[2], &stored[0], 6);
			memcpy(&mac[0], &stored[6], 2);
		} else {
			memcpy(mac, stored, TLSR8258_IEEE_ADDR_SIZE);
		}
		return;
	}

	sys_rand_get(mac, 5);
	memcpy(&mac[5], tlsr8258_telink_ouis[0], 3);

	if (rc == 0) {
		memcpy(&stored[0], &mac[2], 6);
		memcpy(&stored[6], &mac[0], 2);
		rc = flash_area_write(fa, 0, stored, sizeof(stored));
		flash_area_close(fa);
	}
	if (rc != 0) {
		LOG_WRN("IEEE address not persisted (%d)", rc);
	}
#else
	sys_rand_get(mac, TLSR8258_IEEE_ADDR_SIZE);
	mac[0] = (mac[0] & (uint8_t)~BIT(0)) | BIT(1);
#endif
}

static void tlsr8258_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct tlsr8258_radio_data *radio = dev->data;
	uint8_t *mac = radio->mac_addr;

	tlsr8258_ieee_addr_load(mac);

	net_if_set_link_addr(iface, mac, TLSR8258_IEEE_ADDR_SIZE, NET_LINK_IEEE802154);
	memcpy(radio->filter_ieee_addr, mac, TLSR8258_IEEE_ADDR_SIZE);
	radio->iface = iface;
	ieee802154_init(iface);
}

static enum ieee802154_hw_caps tlsr8258_get_capabilities(const struct device *dev)
{
	ARG_UNUSED(dev);

	return IEEE802154_HW_FCS | IEEE802154_HW_FILTER | IEEE802154_HW_RX_TX_ACK;
}

static int tlsr8258_cca(const struct device *dev)
{
	struct tlsr8258_radio_data *radio = dev->data;

	if (!tlsr8258_radio_started_get(radio)) {
		return -ENETDOWN;
	}

	return ((int8_t)TLSR_REG8(0x0441) - 110) < CONFIG_IEEE802154_TLSR8258_CCA_RSSI_THRESHOLD
		       ? 0
		       : -EBUSY;
}

static int tlsr8258_set_channel(const struct device *dev, uint16_t channel)
{
	struct tlsr8258_radio_data *radio = dev->data;

	if (channel < 11u || channel > 26u) {
		return -EINVAL;
	}

	/* Update the persistent shadow even when the live value already matches. */
	tlsr8258_channel_shadow = channel;

	if (tlsr8258_radio_current_channel_get(radio) == channel) {
		return -EALREADY;
	}

	tlsr8258_radio_current_channel_set(radio, channel);
	if (tlsr8258_radio_started_get(radio)) {
		tlsr8258_rf_set_channel(channel);
		/* tlsr8258_rf_set_channel() leaves the RF off; re-enter RX. */
		tlsr8258_rf_set_rxmode_vendor();
	}

	return 0;
}

static int tlsr8258_filter(const struct device *dev, bool set, enum ieee802154_filter_type type,
			   const struct ieee802154_filter *filter)
{
	struct tlsr8258_radio_data *radio = dev->data;

	if (!set || filter == NULL) {
		return -ENOTSUP;
	}

	if (type == IEEE802154_FILTER_TYPE_IEEE_ADDR) {
		memcpy(radio->filter_ieee_addr, filter->ieee_addr, TLSR8258_IEEE_ADDR_SIZE);
		return 0;
	}
	if (type == IEEE802154_FILTER_TYPE_SHORT_ADDR) {
		sys_put_le16(filter->short_addr, radio->filter_short_addr);
		return 0;
	}
	if (type == IEEE802154_FILTER_TYPE_PAN_ID) {
		sys_put_le16(filter->pan_id, radio->filter_pan_id);
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
	struct tlsr8258_radio_data *radio = dev->data;
	const uint16_t runtime_irq_mask =
		tlsr8258_rf_irq_runtime_mask() | RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT;

	if (tlsr8258_radio_started_get(radio)) {
		return -EALREADY;
	}

	/* RF bring-up: stop the link layer, reset status, load the PHY tables. */
	TLSR_REG8(0x0f00) = 0x80u;
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	TLSR_REG8(0x0f01) = 0x3fu;
	TLSR_REG8(0x0f01) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG8(0x0f15) = 0x10u;
	TLSR_REG16(0x0f04) = 149u;
	tlsr8258_rf_init();
	tlsr8258_rf_set_channel(tlsr8258_radio_current_channel_get(radio));

	/*
	 * Receive profile. Bit 7 of 0x0405 is required for frames with an
	 * extended destination address; without it those frames are dropped
	 * before DMA2 while short-addressed and broadcast frames still arrive.
	 * The link-layer mode and settle values (0xf0, 113) belong to this
	 * profile.
	 */
	TLSR_REG8(0x0401) = 0u;
	TLSR_REG8(0x0404) &= (uint8_t)~BIT(5);
	TLSR_REG8(0x0405) = 0x84u;
	TLSR_REG8(0x0f15) = 0xf0u;
	TLSR_REG16(0x0f04) = 113u;
	TLSR_REG8(0x0f03) &= (uint8_t)~BIT(2);
	radio->rx_active = radio->rx_buffer;
	radio->rx_proc = radio->rx_buffer;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	radio->rx_buffer[0] = 0u;
	radio->rx_buffer[4] = 0u;
	radio->rx_shadow[0] = 0u;
	radio->rx_shadow[4] = 0u;
	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX | DMA_CHN_RF_TX;
	/* 0 dBm until set_txpower() is called. */
	tlsr8258_rf_set_power_level(rf_power_level_list[30]);
	TLSR_REG8(0x0c26) = 0x0cu;
	TLSR_REG8(0x0c21) = 0x04u;
	/*
	 * Bit 5 of 0x0f03 is documented for BLE only, but without it the radio
	 * stops receiving and sending ACKs once it has been idle in RX. Bit 2
	 * stays cleared.
	 */
	TLSR_REG8(0x0f03) |= BIT(5);
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f1c) = runtime_irq_mask;
	TLSR_REG8(0x0430) |= BIT(1);
	/* The RF was stopped above, so no state-machine reset is needed. */
	tlsr8258_rf_set_rxmode_vendor();
	/* Clear stale RF CPU sources and unmask them. */
	tlsr8258_rf_irq_reenable();
	tlsr8258_radio_started_set(radio, true);

	return 0;
}

static int tlsr8258_stop(const struct device *dev)
{
	struct tlsr8258_radio_data *radio = dev->data;

	if (!tlsr8258_radio_started_get(radio)) {
		return -EALREADY;
	}

	irq_disable(TLSR8258_IRQ_ZB_RT);
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	tlsr8258_rf_off();
	tlsr8258_radio_started_set(radio, false);

	return 0;
}

static int tlsr8258_set_tx_payload(struct tlsr8258_radio_data *radio, const uint8_t *payload,
				   uint8_t payload_len)
{
	uint8_t *tx_buffer;
	int ret;

	if (payload_len > (TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH)) {
		return -EINVAL;
	}

	tx_buffer = radio->tx_buffer;
	ret = tlsr8258_set_tx_payload_to(tx_buffer, payload, payload_len);

	return ret;
}

static int tlsr8258_set_tx_payload_to(uint8_t *tx_buffer, const uint8_t *payload,
				      uint8_t payload_len)
{
	uint32_t dma_len;

	if (payload_len > (TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH)) {
		return -EINVAL;
	}

	/*
	 * MCU_CORE_8258's vendor path uses a raw DMA byte count here.  The
	 * rf_tx_packet_dma_len() word/remainder encoding belongs to B91/8278,
	 * not this 8258 RF block.
	 */
	dma_len = (uint32_t)payload_len + 1u;
	tx_buffer[0] = (uint8_t)dma_len;
	tx_buffer[1] = (uint8_t)(dma_len >> 8);
	tx_buffer[2] = (uint8_t)(dma_len >> 16);
	tx_buffer[3] = (uint8_t)(dma_len >> 24);
	tx_buffer[4] = payload_len + TLSR8258_FCS_LENGTH;
	/*
	 * The DMA header makes the payload destination unaligned by five bytes.
	 * Use an explicit byte copy; the TC32 memcpy path can leave the final MIC
	 * byte stale on this boundary.
	 */
	for (uint8_t i = 0U; i < payload_len; i++) {
		tx_buffer[TLSR8258_PAYLOAD_OFFSET + i] = payload[i];
	}
	return 0;
}

/*
 * TX-done busy-poll bound for the synchronous assoc-poll. A 16-byte DataReq at
 * 250 kbps is ~0.7 ms on air; 3 ms is a generous ceiling before we give up.
 */
#define TLSR8258_SYNC_POLL_TX_DONE_TIMEOUT_US 3000u
#define TLSR8258_SYNC_POLL_TX_DONE_STEP_US    20u
#define TLSR8258_TX_DONE_POLL_STEP_US         200u

/*
 * Synchronous TX for frames that are followed by an immediate RX window
 * (beacon requests, ACK-requested data requests). With the RF CPU sources
 * masked, kick the frame, poll for TX done, re-arm the RX DMA buffer and
 * switch to RX without a state-machine reset or PLL reload, then unmask the
 * RF sources so the response is received by the ISR. This gives the same
 * TX-to-RX turnaround on every call, independent of when the TX-done
 * interrupt would have been taken.
 */
static int tlsr8258_tx_sync_followup(struct tlsr8258_radio_data *radio, uint8_t tx_seq,
				     bool expect_ack)
{
	uint32_t waited_us = 0u;
	bool tx_done = false;
	unsigned int key;

	/*
	 * Run the whole TX-to-RX turnaround with interrupts locked: the peer's
	 * response follows its ACK within a few milliseconds, and any other
	 * interrupt taken here delays the switch back to RX past it. The RF DMA
	 * and ZB_RT sources are also masked so that no RX completion consumes
	 * the buffer while this TX owns the RF state machine; they are unmasked
	 * again by tlsr8258_rf_irq_reenable() below.
	 */
	key = irq_lock();
	tlsr8258_irq_mask_write(tlsr8258_irq_mask_read() &
				~(BIT(TLSR8258_IRQ_DMA) | BIT(TLSR8258_IRQ_ZB_RT)));
	tlsr8258_radio_op_prepare_tx(&radio->op, tx_seq, expect_ack, false);
	/*
	 * Leave RX so that the RX gate is not active while DMA3 is handed to
	 * the link-layer TX state machine.
	 */
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;

	/*
	 * Prepare RX DMA before switching the link layer to TX, then clear only
	 * the TX and DMA3 latches owned by this operation.
	 */
	radio->rx_active[0] = 0u;
	radio->rx_active[4] = 0u;
	radio->rx_proc = radio->rx_active;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	TLSR_REG8(0x0c0e) = (uint8_t)(TLSR8258_RX_DMA_SIZE >> 4);
	TLSR_REG8(0x0c0f) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	/* Let the TX DMA state settle before asserting DMA ready. */
	k_busy_wait(250);
	tlsr8258_rf_tx_pkt(radio->tx_buffer);

	while (waited_us < TLSR8258_SYNC_POLL_TX_DONE_TIMEOUT_US) {
		if ((TLSR_REG16(0x0f20) & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u) {
			tx_done = true;
			break;
		}
		k_busy_wait(TLSR8258_SYNC_POLL_TX_DONE_STEP_US);
		waited_us += TLSR8258_SYNC_POLL_TX_DONE_STEP_US;
	}
	/*
	 * Clear RF TX and DMA3 completion before exposing the armed DMA2 buffer
	 * to the RX state machine.
	 */
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;

	/*
	 * Re-arm DMA2 after TX done even though it was prepared before TX: the
	 * peer's MAC ACK can complete DMA2 during the TX/RX handoff and leave
	 * its completion latch set, and the next frame is then not received.
	 * Re-arm first, then switch to RX without a state-machine reset.
	 */
	radio->rx_active[0] = 0u;
	radio->rx_active[4] = 0u;
	radio->rx_proc = radio->rx_active;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	tlsr8258_rf_set_rxmode_vendor();

	if (tx_done) {
		(void)tlsr8258_radio_op_on_tx_success(&radio->op);
	} else {
		tlsr8258_radio_op_on_timeout(&radio->op);
	}
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	/* Unmask the RF CPU sources masked above. */
	tlsr8258_rf_irq_reenable();
	irq_unlock(key);
	return tlsr8258_radio_op_result_errno(&radio->op);
}

static int tlsr8258_tx(const struct device *dev, enum ieee802154_tx_mode mode, struct net_pkt *pkt,
		       struct net_buf *frag)
{
	struct tlsr8258_radio_data *radio = dev->data;
	uint8_t tx_seq = (frag != NULL && frag->len >= 3u) ? frag->data[2] : 0xffu;
	bool expect_ack;
	bool expect_post_tx_rx;
	bool expect_post_tx_followup;
	uint32_t wait_budget_us;
	uint16_t saved_irq_mask;
	uint16_t session_irq_mask;
	int ret;

	ARG_UNUSED(pkt);

	if (!tlsr8258_radio_started_get(radio)) {
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

	ret = tlsr8258_set_tx_payload(radio, frag->data, frag->len);
	if (ret < 0) {
		return ret;
	}

	expect_ack = tlsr8258_ack_requested(frag->data, frag->len);
	expect_post_tx_followup =
		tlsr8258_core_psdu_expects_post_tx_followup(frag->data, frag->len);
	/*
	 * Frames that need an immediate RX follow-up use a bounded hardware poll
	 * and deterministic RX turnaround. The async completion path can wait
	 * indefinitely when the RF TX-done interrupt remains latched.
	 */
	if (expect_post_tx_followup) {
		return tlsr8258_tx_sync_followup(radio, tx_seq, expect_ack);
	}

	/*
	 * A software MAC-ACK is kicked from the RX ISR and owns the RF TX state
	 * until its TX-done interrupt returns the chip to RX.  Do not reset the
	 * shared radio-op for a stack TX in that interval: doing so lets the ACK's
	 * completion wake the new operation before its own DMA transfer, which
	 * presents as a successful API call with no frame on air.
	 */
	for (uint32_t wait_us = 0u; tlsr8258_ack_tx_pending_get(radio) && wait_us < 2000u;
	     wait_us += 50u) {
		k_busy_wait(50u);
	}
	if (tlsr8258_ack_tx_pending_get(radio)) {
		/* The ACK TX never completed: restore RX and let the MAC retry. */
		tlsr8258_rf_rearm_idle_rx(radio);
		radio->op.ack_tx_pending = false;
		return -EAGAIN;
	}
	/*
	 * With a raw RX sink installed the upper layer handles follow-up frames
	 * itself, so tx() does not wait for them.
	 */
	expect_post_tx_rx = (tlsr8258_rx_sink == NULL) && expect_post_tx_followup;
	wait_budget_us = expect_post_tx_rx ? 150000u : CONFIG_IEEE802154_TLSR8258_TX_WAIT_US;
	k_timeout_t wait_timeout = K_USEC(wait_budget_us);
	saved_irq_mask = TLSR_REG16(0x0f1c);
	session_irq_mask = tlsr8258_tx_irq_session_mask(saved_irq_mask, expect_post_tx_followup);

	irq_disable(TLSR8258_IRQ_ZB_RT);
	k_sem_reset(&radio->tx_wait);
	tlsr8258_radio_op_prepare_tx(&radio->op, tx_seq, expect_ack, expect_post_tx_rx);
	tlsr8258_rf_prepare_normal_tx();
	tlsr8258_rf_set_txmode(radio);
	/* Let the TX DMA state settle before asserting DMA ready. */
	k_busy_wait(250);
	if (session_irq_mask != saved_irq_mask) {
		TLSR_REG16(0x0f1c) = 0u;
		TLSR_REG16(0x0f1c) = session_irq_mask;
	}
	if (tlsr8258_tx_force_manual_off_before_start(expect_post_tx_followup)) {
		TLSR_REG8(0x0f00) = 0x80u;
	}
	TLSR_REG16(0x0f20) = tlsr8258_tx_irq_start_clear_mask(expect_post_tx_followup);
	tlsr8258_rf_tx_pkt(radio->tx_buffer);
	/* Unmask the RF CPU sources disabled above. */
	tlsr8258_rf_irq_reenable();

	ret = k_sem_take(&radio->tx_wait, wait_timeout);
	if (session_irq_mask != saved_irq_mask) {
		TLSR_REG16(0x0f1c) = 0u;
		TLSR_REG16(0x0f1c) = saved_irq_mask;
	}
	if (ret == -EAGAIN) {
		uint16_t pending_irq = TLSR_REG16(0x0f20);

		/*
		 * The TX-done status can latch without the RF interrupt having
		 * been taken. Poll the status for a bounded time before treating
		 * the operation as lost and resetting the RF state machine.
		 */
		if ((pending_irq &
		     (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) == 0u) {
			for (uint32_t extra_us = 0u;
			     extra_us < CONFIG_IEEE802154_TLSR8258_TX_DONE_POLL_US;
			     extra_us += TLSR8258_TX_DONE_POLL_STEP_US) {
				k_busy_wait(TLSR8258_TX_DONE_POLL_STEP_US);
				pending_irq = TLSR_REG16(0x0f20);
				if ((pending_irq & (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT |
						    RF_IRQ_FSM_TIMEOUT)) != 0u) {
					break;
				}
			}
		}

		if ((pending_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u) {
			/*
			 * The frame was sent, but for an ACK-requested frame the
			 * ACK was not observed by the driver: do not report
			 * success, so the MAC retries it.
			 */
			if (radio->op.expect_post_tx_rx) {
				tlsr8258_radio_op_on_timeout(&radio->op);
			} else if (radio->op.expect_ack) {
				tlsr8258_radio_op_on_tx_error(&radio->op, -ENOMSG);
			} else {
				(void)tlsr8258_radio_op_on_tx_success(&radio->op);
			}
			/*
			 * The completion was not handled by the ISR, so the RX
			 * double-buffer swap did not run; re-arm a clean RX buffer
			 * when a follow-up frame is expected.
			 */
			if (tlsr8258_tx_poll_needs_rx_rearm(expect_post_tx_followup, false)) {
				radio->rx_active[0] = 0u;
				radio->rx_active[4] = 0u;
				tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
			}
			/* DMA first, then RX mode without a state-machine reset. */
			tlsr8258_rf_set_rxmode_vendor();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
			return tlsr8258_radio_op_result_errno(&radio->op);
		}

		if ((pending_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
			/*
			 * Hardware TX timeout: the RF state machine is still
			 * coherent, so only restore RX. The MAC retries on -EIO.
			 */
			tlsr8258_radio_op_on_tx_error(&radio->op, -EIO);
			tlsr8258_rf_rearm_idle_rx(radio);
			return tlsr8258_radio_op_result_errno(&radio->op);
		}

		tlsr8258_radio_op_on_timeout(&radio->op);

		/*
		 * Neither TX done nor a TX timeout was reported: the RF state
		 * machine may be stuck in an intermediate TX state from which no
		 * further TX completes. Turn the RF off and back to RX.
		 */
		tlsr8258_rf_off();
		k_busy_wait(50);
		tlsr8258_rf_rearm_idle_rx(radio);
		return tlsr8258_radio_op_result_errno(&radio->op);
	}

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	return tlsr8258_radio_op_result_errno(&radio->op);
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
	struct tlsr8258_radio_data *radio = dev->data;

	if (type == IEEE802154_CONFIG_PROMISCUOUS) {
		tlsr8258_radio_promiscuous_set(radio, config->promiscuous);
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

static void tlsr8258_irq_config(const struct device *dev)
{
	/*
	 * rf_irq_reenable() enables both the RF ZB_RT source (IRQ 13) and
	 * the RF DMA completion source (IRQ 4).  IRQ 4 must have a real
	 * vector: leaving it at Zephyr's default z_irq_spurious handler makes
	 * a normal RX/TX DMA completion fatal under interview traffic.
	 * The RF ISR clears both latched sources, so use the same handler for
	 * the two hardware sources.  The TC32 IRQ dispatcher services IRQ 13
	 * first when both are pending and will enter here again for IRQ 4 if
	 * the DMA latch remains set.
	 */
	IRQ_CONNECT(TLSR8258_IRQ_DMA, 0, tlsr8258_rf_isr, &tlsr8258_radio_data_0, 0);
	IRQ_CONNECT(DT_INST_IRQN(0), 0, tlsr8258_rf_isr, &tlsr8258_radio_data_0, 0);
	irq_disable(TLSR8258_IRQ_DMA);
	irq_disable(TLSR8258_IRQ_ZB_RT);
	ARG_UNUSED(dev);
}

/* Set once tlsr8258_init() has initialized the RX queue. */
static bool tlsr8258_hw_inited;

void tlsr8258_radio_rx_poll(void)
{
	if (tlsr8258_hw_inited) {
		tlsr8258_rx_drain_pending(&tlsr8258_radio_data_0);
	}
}

static const struct tlsr8258_radio_config tlsr8258_radio_config_0 = {
	.irq_config_func = tlsr8258_irq_config,
};

static int tlsr8258_init(const struct device *dev)
{
	const struct tlsr8258_radio_config *config = dev->config;
	struct tlsr8258_radio_data *radio = dev->data;
	/* The radio port may call device_init() again; initialize only once. */
	if (tlsr8258_hw_inited) {
		return 0;
	}

	memset(radio, 0, sizeof(*radio));
	memcpy(radio->filter_pan_id, tlsr8258_filter_pan_id_shadow, TLSR8258_PAN_ID_SIZE);
	memcpy(radio->filter_short_addr, tlsr8258_filter_short_addr_shadow,
	       TLSR8258_SHORT_ADDR_SIZE);
	memcpy(radio->filter_ieee_addr, tlsr8258_filter_ieee_addr_shadow, TLSR8258_IEEE_ADDR_SIZE);
	tlsr8258_radio_current_channel_set(radio, tlsr8258_channel_shadow);

	if (!tlsr8258_rx_queue_init(&radio->rx_queue, radio->rx_slots, TLSR8258_RX_SLOT_COUNT)) {
		return -EINVAL;
	}
	k_sem_init(&radio->tx_wait, 0, 1);

	/* RX is delivered from tlsr8258_radio_rx_poll(); there is no RX thread. */
	config->irq_config_func(dev);
	tlsr8258_hw_inited = true;

	return 0;
}

#if defined(CONFIG_IEEE802154_RAW_MODE)
DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0, &tlsr8258_radio_config_0,
		      POST_KERNEL, CONFIG_IEEE802154_TLSR8258_INIT_PRIO, &tlsr8258_radio_api);
#elif defined(CONFIG_NET_L2_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0, &tlsr8258_radio_config_0,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO, &tlsr8258_radio_api, IEEE802154_L2,
			  NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH);
#elif defined(CONFIG_NET_L2_OPENTHREAD)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0, &tlsr8258_radio_config_0,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO, &tlsr8258_radio_api, OPENTHREAD_L2,
			  NET_L2_GET_CTX_TYPE(OPENTHREAD_L2), 1280);
#elif defined(CONFIG_NET_L2_CUSTOM_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0, &tlsr8258_radio_config_0,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO, &tlsr8258_radio_api,
			  CUSTOM_IEEE802154_L2, NET_L2_GET_CTX_TYPE(CUSTOM_IEEE802154_L2),
			  TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH);
#endif

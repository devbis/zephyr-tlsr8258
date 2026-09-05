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
#include <zephyr/net/ieee802154_frame.h>
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
#include "ieee802154_tlsr8258_ack_filter.h"
#include "ieee802154_tlsr8258_fake_phy_core.h"

LOG_MODULE_REGISTER(ieee802154_tlsr8258, CONFIG_IEEE802154_DRIVER_LOG_LEVEL);

#define TLSR_REG8(addr)  (*(volatile uint8_t *)(0x00800000u + (addr)))
#define TLSR_REG16(addr) (*(volatile uint16_t *)(0x00800000u + (addr)))
#define TLSR_REG32(addr) (*(volatile uint32_t *)(0x00800000u + (addr)))

#define TCMD_UNDER_WR 0x80u
#define TCMD_MASK     0x3fu
#define TCMD_WRITE    0x03u

/*
 * TLSR825x hardware watchdog (timer2-based), per
 * ../tl_zigbee_sdk/platform/chip_8258/watchdog.h and register.h
 * (reg_tmr_ctrl=0x620, reg_tmr_sta=0x623, reg_tmr2_tick=0x638;
 * FLD_TMR2_EN=BIT6, FLD_TMR_WD_CAPT=BIT_RNG(9,22), FLD_TMR_WD_EN=BIT23,
 * FLD_TMR_STA_WD=BIT3). This project has repeatedly hit ZB-thread hangs
 * that are plain infinite loops or a stuck hardware bus access rather than
 * a CPU fault/exception -- k_sys_fatal_error_handler() never runs for
 * those, so it cannot self-heal them. Feeding this watchdog once per
 * zb_thread main-loop pass converts ANY such hang (root cause found or
 * not) into a hardware-forced reboot instead of a permanent silent death;
 * the existing boot-time bootstrap (zb_core_bootstrap_once() / zdo_init())
 * already knows how to resume a retained-joined ED/router after a reset.
 */
#define TLSR_REG_TMR_CTRL   0x0620u
#define TLSR_REG_TMR_STA    0x0623u
#define TLSR_REG_TMR2_TICK  0x0638u
#define TLSR_FLD_TMR2_EN     BIT(6)
#define TLSR_FLD_TMR_WD_EN   BIT(23)
#define TLSR_FLD_TMR_STA_WD  BIT(3)

/* system_clk_mHz for TLSR825x is fixed at 16 (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
 * = 16000000). period_ms*1000*16 >> 18, then placed at bits[9:22] (14 bits,
 * max ~16.7s). Use a generous window: long enough that no legitimate
 * blocking operation in the loop (flash NV writes, etc.) ever comes close,
 * short enough to recover promptly from a real hang. */
#define TLSR_WATCHDOG_PERIOD_MS 8000u

/*
 * A watchdog-triggered reset on this SoC does not clear the watchdog's own
 * enable bit or its running tick -- the same peripheral-state retention this
 * project has already documented for SRAM across a soft reset. Left armed,
 * the just-fired watchdog immediately resumes counting from a stale tick
 * value close to (or past) the period threshold, re-firing again within a
 * fraction of the configured period on the very next boot -- a crash loop
 * that never gets far enough into bootstrap to reach tlsr8258_watchdog_init()
 * below. Call this as the very first thing at boot, before anything else,
 * to guarantee a clean slate; tlsr8258_watchdog_init() re-arms it once
 * bootstrap actually completes.
 */
void tlsr8258_watchdog_disable(void)
{
	TLSR_REG32(TLSR_REG_TMR_CTRL) &= ~(TLSR_FLD_TMR2_EN | TLSR_FLD_TMR_WD_EN);
	TLSR_REG32(TLSR_REG_TMR2_TICK) = 0u;
	TLSR_REG8(TLSR_REG_TMR_STA) = (uint8_t)TLSR_FLD_TMR_STA_WD;
}

void tlsr8258_watchdog_init(void)
{
	uint32_t period_field = ((uint64_t)TLSR_WATCHDOG_PERIOD_MS * 1000u * 16u) >> 18;
	uint32_t ctrl;

	tlsr8258_watchdog_disable();

	ctrl = TLSR_REG32(TLSR_REG_TMR_CTRL);
	ctrl &= ~(0x3fffu << 9);
	ctrl |= (period_field & 0x3fffu) << 9;
	TLSR_REG32(TLSR_REG_TMR_CTRL) = ctrl;
	TLSR_REG32(TLSR_REG_TMR2_TICK) = 0u;
	TLSR_REG32(TLSR_REG_TMR_CTRL) |= TLSR_FLD_TMR2_EN | TLSR_FLD_TMR_WD_EN;
}

void tlsr8258_watchdog_feed(void)
{
	/*
	 * Re-assert the enable bits on every feed, not just at init. Confirmed
	 * on hardware: reg_tmr_ctrl's TMR2_EN/WD_EN bits were found cleared
	 * (whole register read back as 0) partway through a normal run that
	 * had clearly gotten past tlsr8258_watchdog_init() (hundreds of loop
	 * passes in) -- nothing else in this codebase writes reg_tmr_ctrl, so
	 * treat however that happens as unexplained and just make every feed
	 * self-healing instead of a bare status-clear that silently does
	 * nothing once disarmed. Setting already-set bits is a no-op.
	 */
	TLSR_REG32(TLSR_REG_TMR_CTRL) |= TLSR_FLD_TMR2_EN | TLSR_FLD_TMR_WD_EN;
	TLSR_REG8(TLSR_REG_TMR_STA) = (uint8_t)TLSR_FLD_TMR_STA_WD;
}

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

#define TLSR8258_RX_BUF_SIZE 144u
#define TLSR8258_RX_DMA_SIZE 144u
#define TLSR8258_TX_BUF_SIZE 132u
#define TLSR8258_PAYLOAD_OFFSET 5u
#define TLSR8258_PHY_MAX_PSDU 127u
#define TLSR8258_FCS_LENGTH 2u
#define TLSR8258_MIN_FRAME_LENGTH 3u
/*
 * Total prep budget between the fast-path set_txmode_for_ack call and the
 * tx_pkt write for a MAC ACK.  Capture analysis:
 *   - 150us TURNAROUND landed ACK ~80us after end-of-coord-PSDU (the lower
 *     end of the 802.15.4 spec window; on the one run where this worked,
 *     coord accepted and TK was delivered).
 *   - 350us TURNAROUND pushed ACK to ~1.3ms after end-of-coord-PSDU, which
 *     exceeds macAckWaitDuration (864us).  Coord dropped that ACK, exhausted
 *     macMaxFrameRetries, and refused to queue Transport-Key.
 * Stick with 150us — better to be slightly early than too late.  We use the
 * fast-path set_txmode_for_ack (no PLL reload) to keep total latency tight.
 *
 * 2026-08: zigbee-rs anchors this settle window at RX completion, before
 * clearing the RX event and parsing the frame.  The TX-mode switch and
 * address/pending lookup happen inside that window; waiting a fresh 120us
 * after the ISR enters TX makes the ACK miss macAckWaitDuration.
 */
#define TLSR8258_ACK_TURNAROUND_US 120u
/*
 * TC32 has no hardware divider, so converting (k_cycle_get_32() - start) to
 * microseconds inside the ACK hot path costs ~3-6us per call via the
 * software 32-bit divide emitted by k_cyc_to_us_floor32(). Pre-multiply the
 * turnaround once at compile time and compare in the cycle domain instead.
 */
#define TLSR8258_ACK_TURNAROUND_CYC \
	((uint32_t)TLSR8258_ACK_TURNAROUND_US * \
	 (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u))
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
#define TLSR8258_RX_SLOT_COUNT 16u

struct tblcmdset {
	uint16_t adr;
	uint8_t dat;
	uint8_t cmd;
};

struct tlsr8258_radio_config {
	void (*irq_config_func)(const struct device *dev);
};

struct tlsr8258_radio_debug {
	volatile uint32_t rf_isr_entry_count;
	volatile uint32_t rf_isr_rx_event_count;
	volatile uint32_t rx_capture_debug_count;
	volatile uint16_t rf_irq_raw_debug;
	volatile uint16_t rf_irq_effective_debug;
	volatile uint16_t rf_irq_mask_debug;
	volatile uint16_t rf_irq_ack_debug;
	volatile uint16_t rx_capture_irq_debug;
	volatile uint8_t rf_dma_len_debug;
	volatile uint8_t rf_psdu_len_debug;
	volatile uint8_t rf_crc_debug;
	volatile uint8_t rf_branch_debug;
	volatile uint32_t tx_diag_trace[32];
	volatile uint8_t tx_diag_trace_head;
	/*
	 * Snapshot of RF chip control registers taken at the start of every
	 * RX ISR.  Lets us inspect what state the chip is actually in without
	 * having to halt via SWS — read these via debugger / TlsrPgm dump.
	 *  reg_0f00: reg_rf_ll_cmd       (state machine command)
	 *  reg_0f02: reg_rf_ll_ctrl_0    (TRX state: OFF/TX/RX bits)
	 *  reg_0f03: reg_rf_ll_ctrl_1    (FSM timeout / CRC enables)
	 *  reg_0f15: reg_rf_ll_ctrl_2    (pipe / mode select)
	 *  reg_0f16: reg_rf_ll_ctrl_3    (auto-FSM mode bits, our LL_MODE_*)
	 *  reg_0428: TX/RX gate register
	 *  reg_0430: another RF control register
	 */
	volatile uint8_t rf_reg_0f00;
	volatile uint8_t rf_reg_0f02;
	volatile uint8_t rf_reg_0f03;
	volatile uint8_t rf_reg_0f15;
	volatile uint8_t rf_reg_0f16;
	volatile uint8_t rf_reg_0428;
	volatile uint8_t rf_reg_0430;
	/*
	 * Cycle-counter snapshots for ACK-path latency profiling.  Captured on
	 * every ack-requested RX so the SWS-side reader gets a consistent set:
	 *
	 *  isr_entry_cyc      — first instruction of tlsr8258_rf_isr
	 *  ack_capture_cyc    — timestamp immediately after switching to TX mode
	 *  pre_busy_wait_cyc  — right before the cycle-domain spin in send_ack
	 *  tx_kick_cyc        — right before tlsr8258_rf_tx_pkt for the ACK
	 *  tx_done_cyc        — after TX_DS / CMD_DONE wait, before set_rxmode
	 *
	 * Time deltas (microseconds) computed by the driver into the matching
	 * _us fields so they can be read directly without a software divide.
	 */
	volatile uint32_t isr_entry_cyc;
	volatile uint32_t ack_capture_cyc;
	volatile uint32_t pre_busy_wait_cyc;
	volatile uint32_t tx_kick_cyc;
	volatile uint32_t tx_done_cyc;
	volatile uint16_t isr_to_capture_us;
	volatile uint16_t capture_to_wait_us;
	volatile uint16_t wait_duration_us;
	volatile uint16_t tx_send_duration_us;
	/*
	 * Inter-ISR gap profiling. Each rf_isr entry computes the duration
	 * since the previous entry; we track the LAST gap and the MAX seen so
	 * far. If RF IRQ delivery is being delayed by a CPU-busy / IRQ-masked
	 * critical section, inter_isr_gap_max_us will spike well above the
	 * coordinator's macAckWaitDuration (864us).
	 */
	volatile uint32_t prev_isr_entry_cyc;
	volatile uint32_t inter_isr_gap_us;
	volatile uint32_t inter_isr_gap_max_us;
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
	struct tlsr8258_radio_debug *debug;
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

static int tlsr8258_set_tx_payload(struct tlsr8258_radio_data *radio, const uint8_t *payload,
				   uint8_t payload_len);
static int tlsr8258_set_tx_payload_to(uint8_t *tx_buffer, const uint8_t *payload,
					      uint8_t payload_len);
static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size,
				       struct tlsr8258_radio_data *radio);
static void tlsr8258_rx_capture_isr(uint16_t irq_status, struct tlsr8258_radio_data *radio);
static void tlsr8258_rf_irq_reenable(void);
static void tlsr8258_rf_irq_reenable_thread_ctx(void);
static void tlsr8258_rf_rx_buffer_set(uint8_t *buffer, uint16_t size);
static void tlsr8258_rf_set_rxmode_vendor(void);
static void tlsr8258_rf_rearm_idle_rx(struct tlsr8258_radio_data *radio);
static bool tlsr8258_rf_recover_stuck_rx(struct tlsr8258_radio_data *radio);
static bool tlsr8258_filter_match_for_ack(const uint8_t *payload, uint8_t length,
						  const struct tlsr8258_radio_data *radio);
static bool tlsr8258_ack_requested(const uint8_t *payload, uint8_t length);

#if defined(CONFIG_IEEE802154_TLSR8258_RETAINED_DEBUG)
static struct tlsr8258_radio_debug __noinit tlsr8258_radio_debug_state;
#endif

static struct tlsr8258_radio_debug *tlsr8258_radio_debug_get(struct tlsr8258_radio_data *radio)
{
	return radio->debug;
}

static void tlsr8258_tx_diag_put(struct tlsr8258_radio_data *radio, uint32_t word)
{
	struct tlsr8258_radio_debug *debug = tlsr8258_radio_debug_get(radio);
	uint8_t head;

	if (debug == NULL) {
		return;
	}

	head = debug->tx_diag_trace_head;
	debug->tx_diag_trace[head & (ARRAY_SIZE(debug->tx_diag_trace) - 1u)] = word;
	debug->tx_diag_trace_head = head + 1u;
}

static void tlsr8258_rf_debug_reset(struct tlsr8258_radio_data *radio)
{
	struct tlsr8258_radio_debug *debug = tlsr8258_radio_debug_get(radio);

	if (debug == NULL) {
		return;
	}

	memset((void *)debug, 0, sizeof(*debug));
}

/*
 * TC32 LLVM has miscompiled direct accesses to the scalar tail of
 * tlsr8258_radio on hardware, e.g. tlsr8258_tx() read tx_buffer[4] where the
 * source asked for started. Use explicit offset-based volatile helpers for
 * those tail fields so codegen materializes the correct address.
 */
static inline volatile uint8_t *tlsr8258_radio_u8_field(struct tlsr8258_radio_data *radio,
							size_t offset)
{
	return (volatile uint8_t *)((volatile uint8_t *)radio + offset);
}

static inline volatile uint16_t *tlsr8258_radio_u16_field(struct tlsr8258_radio_data *radio,
							  size_t offset)
{
	return (volatile uint16_t *)((volatile uint8_t *)radio + offset);
}

static inline volatile uint32_t *tlsr8258_radio_u32_field(struct tlsr8258_radio_data *radio,
							  size_t offset)
{
	return (volatile uint32_t *)((volatile uint8_t *)radio + offset);
}

static inline uint16_t tlsr8258_radio_current_channel_get(struct tlsr8258_radio_data *radio)
{
	return *tlsr8258_radio_u16_field(radio,
					 offsetof(struct tlsr8258_radio_data, current_channel));
}

static inline void tlsr8258_radio_current_channel_set(struct tlsr8258_radio_data *radio,
						      uint16_t channel)
{
	*tlsr8258_radio_u16_field(radio,
				  offsetof(struct tlsr8258_radio_data, current_channel)) = channel;
}

static inline void tlsr8258_radio_last_irq_set(struct tlsr8258_radio_data *radio, uint16_t irq)
{
	*tlsr8258_radio_u16_field(radio, offsetof(struct tlsr8258_radio_data, last_irq)) = irq;
}

static inline void tlsr8258_radio_rx_count_inc(struct tlsr8258_radio_data *radio)
{
	(*tlsr8258_radio_u32_field(radio, offsetof(struct tlsr8258_radio_data, rx_count)))++;
}

static inline void tlsr8258_radio_tx_count_inc(struct tlsr8258_radio_data *radio)
{
	(*tlsr8258_radio_u32_field(radio, offsetof(struct tlsr8258_radio_data, tx_count)))++;
}

static inline bool tlsr8258_radio_started_get(struct tlsr8258_radio_data *radio)
{
	return *tlsr8258_radio_u8_field(radio, offsetof(struct tlsr8258_radio_data, started)) != 0u;
}

static inline void tlsr8258_radio_started_set(struct tlsr8258_radio_data *radio, bool started)
{
	*tlsr8258_radio_u8_field(radio, offsetof(struct tlsr8258_radio_data, started)) =
		started ? 1u : 0u;
}

static inline void tlsr8258_radio_promiscuous_set(struct tlsr8258_radio_data *radio,
						  bool promiscuous)
{
	*tlsr8258_radio_u8_field(radio, offsetof(struct tlsr8258_radio_data, promiscuous)) =
		promiscuous ? 1u : 0u;
}

/* ack_tx_pending is written by the RF ISR and read by the Zigbee thread.
 * Keep this read volatile and offset-based: the TC32 LLVM backend has already
 * demonstrated bad codegen for scalar members in this driver structure. */
static inline bool tlsr8258_ack_tx_pending_get(struct tlsr8258_radio_data *radio)
{
	return *((volatile uint8_t *)((volatile uint8_t *)&radio->op +
					 offsetof(struct tlsr8258_radio_op, ack_tx_pending))) != 0u;
}

static inline bool tlsr8258_radio_promiscuous_get(struct tlsr8258_radio_data *radio)
{
	return *tlsr8258_radio_u8_field(radio,
					offsetof(struct tlsr8258_radio_data, promiscuous)) != 0u;
}

static tlsr8258_zigbee_rx_sink_t tlsr8258_zigbee_rx_sink;

/*
 * Keep the Zigbee filter request across radio initialisation.  The restored
 * router state is applied before the late device_init() fallback can run;
 * tlsr8258_init() clears radio_data with memset(), so fields written directly
 * into radio_data at that point would otherwise be lost.
 */
static uint8_t tlsr8258_filter_pan_id_shadow[TLSR8258_PAN_ID_SIZE] = {0xffu, 0xffu};
static uint8_t tlsr8258_filter_short_addr_shadow[TLSR8258_SHORT_ADDR_SIZE] = {0xffu, 0xffu};
static uint8_t tlsr8258_filter_ieee_addr_shadow[TLSR8258_IEEE_ADDR_SIZE];
/*
 * Keep the logical channel outside radio_data as well.  A late device_init()
 * fallback clears radio_data with memset(); restoring the hard-coded channel
 * 11 there leaves the joined router deaf while the MAC PIB still says 25.
 */
#if defined(CONFIG_ZIGBEE_CHANNEL) && (CONFIG_ZIGBEE_CHANNEL >= 11) && \
	(CONFIG_ZIGBEE_CHANNEL <= 26)
static uint16_t tlsr8258_channel_shadow = (uint16_t)CONFIG_ZIGBEE_CHANNEL;
#else
static uint16_t tlsr8258_channel_shadow = 11u;
#endif

static inline struct tlsr8258_radio_data *tlsr8258_zigbee_radio_data_get(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(zb));

	return (dev != NULL) ? dev->data : NULL;
}

void tlsr8258_zigbee_register_rx_sink(tlsr8258_zigbee_rx_sink_t sink)
{
	tlsr8258_zigbee_rx_sink = sink;
}

void tlsr8258_zigbee_update_filters(uint16_t pan_id, uint16_t short_addr,
				    const uint8_t *ieee_addr)
{
	struct tlsr8258_radio_data *radio = tlsr8258_zigbee_radio_data_get();

	sys_put_le16(pan_id, tlsr8258_filter_pan_id_shadow);
	sys_put_le16(short_addr, tlsr8258_filter_short_addr_shadow);
	if (ieee_addr != NULL) {
		memcpy(tlsr8258_filter_ieee_addr_shadow, ieee_addr,
		       TLSR8258_IEEE_ADDR_SIZE);
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

/*
 * RX is armed once at radio start and is restored only at real RX/TX
 * ownership handoffs.  Do not periodically write the RF state registers from
 * the Zigbee thread: on TLSR8258 that creates an artificial TX/RX window and
 * can reset the LL state machine while a coordinator frame is arriving.
 *
 * Keep normal continuously-armed RX untouched here.  The guard repairs the
 * CPU interrupt mask if a TX handoff/arch IRQ path dropped ZB_RT, and only
 * resets RX DMA when the impossible tuple (RF_RX pending, no CPU source) is
 * observed.  This is the same CPU-side recovery contract as zigbee-rs, with
 * an additional hardware-latch escape hatch for the C port.
 */
void tlsr8258_zigbee_idle_rx_guard(void)
{
	struct tlsr8258_radio_data *radio = tlsr8258_zigbee_radio_data_get();
	uint8_t irq_en;
	static uint8_t stuck_tx_checks;

	if (radio == NULL) {
		return;
	}

	/*
	 * TLSR8258 can leave the RF completion latch asserted while its parent CPU
	 * source is masked.  Recover that specific impossible state below.  Do not
	 * periodically re-arm based only on an unchanged RX counter: register
	 * 0x0c26 (reg_dma_rx_rdy0) reads as 0x04 while RX DMA is armed, so treating
	 * it as a pending completion causes a reset/re-arm every 250 ms and creates
	 * the very idle-deaf window this guard is meant to repair.
	 */
	/* The MAC can issue TRX_OFF during the join/interview handoff after the
	 * RF start path has already armed RX.  In that narrow path the PHY remains
	 * active, but the driver's software `started` bit is cleared; subsequent
	 * API TX calls then return -ENETDOWN and the router becomes receive-only.
	 * Recover only this coherent hardware/software mismatch. */
	if (!tlsr8258_radio_started_get(radio) &&
	    TLSR_REG8(0x0f02) != RF_TRX_OFF &&
	    (TLSR_REG8(0x0c20) & DMA_CHN_RF_RX) != 0u) {
		tlsr8258_radio_started_set(radio, true);
	}
	if ((TLSR_REG32(0x0640) & BIT(TLSR8258_IRQ_ZB_RT)) == 0u) {
		tlsr8258_rf_irq_reenable_thread_ctx();
	}

	/*
	 * The RF vector clears the chip-level IRQ gate (0x800643) on entry.
	 * Normally tlsr8258_rf_irq_reenable() restores it from the ISR, but a
	 * TX/RX handoff can leave the RF/DMA mask valid while the global gate is
	 * still clear.  In that state RF completion remains pending forever and
	 * the router is deaf even though the radio is visibly in RX mode.  This
	 * guard runs from the normal Zigbee thread, outside an irq_lock() critical
	 * section, so restoring the gate here is safe and does not reset the RF
	 * state machine.
	 */
	irq_en = TLSR_REG8(0x0643);
	if ((irq_en & BIT(0)) == 0u) {
		TLSR_REG8(0x0643) = irq_en | BIT(0);
	}

	/*
	 * A TLSR8258 RX completion can survive after the RF ISR has cleared the
	 * parent CPU source.  In that state 0x0f20 says RX is pending, DMA2 is
	 * still enabled, but no new CPU vector can be generated; the router then
	 * hears beacons only intermittently until reboot.  Recover only this
	 * inconsistent tuple.  A normal armed RX has either no RF completion or a
	 * live CPU source, and is left completely untouched.
	 */
	if (tlsr8258_rf_recover_stuck_rx(radio)) {
		tlsr8258_rf_irq_reenable_thread_ctx();
	}

	/*
	 * A completed TX can leave the 8258 TRX state latched at TX-enable
	 * (0x0f02 == 0x55) without a pending RF or DMA interrupt.  There is then
	 * no ISR edge which could execute the normal TX->RX handoff, so the router
	 * remains deaf indefinitely.  This was observed after a successful
	 * interview: the coordinator's remove/read requests were not even
	 * MAC-ACKed, while the software operation was COMPLETE_OK.
	 *
	 * Do not reset the RF while a real stack TX is in flight.  The
	 * ack_tx_pending bit is intentionally not an exclusion here: if the RF
	 * completion edge was lost, that bit is exactly the stale software state
	 * which prevents the router from recovering.  Require two consecutive 1 ms
	 * loop observations of the impossible state so a normal short ACK handoff
	 * is left alone.  The recovery is deliberately conditional and does not
	 * periodically re-enable RX during normal idle operation.
	 */
	if (tlsr8258_radio_started_get(radio) &&
	    TLSR_REG8(0x0f02) == (RF_TRX_OFF | BIT(4)) &&
	    TLSR_REG16(0x0f20) == 0u &&
	    (TLSR_REG8(0x0c20) & DMA_CHN_RF_RX) != 0u &&
	    radio->op.state != TLSR8258_RADIO_OP_TX_PENDING &&
	    radio->op.state != TLSR8258_RADIO_OP_WAITING_POST_TX_RX &&
	    !radio->op.ack_pending) {
		if (stuck_tx_checks < UINT8_MAX) {
			stuck_tx_checks++;
		}
		if (stuck_tx_checks >= 2u) {
			tlsr8258_rf_rearm_idle_rx(radio);
			/* The corresponding TX completion IRQ was lost, so no later ISR
			 * can clear this ACK-only software latch for us. */
			radio->op.ack_tx_pending = false;
			stuck_tx_checks = 0u;
		}
	} else {
		stuck_tx_checks = 0u;
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

/* Match the hardware-proven SDK/Rust PHY channel sequence. */
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
    band = (freq_mhz > 2464u) ? 0x0cu :
           (freq_mhz > 2434u) ? 0x10u : 0x14u;

    /* set_trx_off() in zigbee-rs: stop LL activity before retuning, but
     * leave the PHY mode sequence itself to set_rxmode_vendor() below. */
    TLSR_REG8(0x0f02) = RF_TRX_OFF;
    TLSR_REG8(0x040d) = (uint8_t)physical;
    TLSR_REG16(0x04d6) = freq_mhz;
    modem_val = (uint16_t)((freq_mhz << 2) | 1u);
    TLSR_REG8(0x1244) = (uint8_t)modem_val;
    TLSR_REG8(0x1245) = (uint8_t)((TLSR_REG8(0x1245) & 0xc0u) |
                                  ((modem_val >> 8) & 0x3fu));
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

	TLSR_REG8(0x1226) = (uint8_t)((TLSR_REG8(0x1226) & 0x7fu) |
				      ((power_word >> 17) & 0x80u));
	TLSR_REG8(0x1227) = (uint8_t)((TLSR_REG8(0x1227) & 0xe0u) |
				      ((power_word >> 25) & 0x1fu));
}

static void tlsr8258_rf_rx_buffer_set(uint8_t *buffer, uint16_t size)
{
	uintptr_t addr = (uintptr_t)buffer;
	ARG_UNUSED(size);

	/*
	 * Vendor-verified RX re-arm (libdrivers_8258.a rf_rx_irq_handler /
	 * mac_phy.c): the RF-RX DMA channel enable (reg_dma_chn_en 0x0c20 bit2 =
	 * FLD_DMA_CHN_RF_RX) MUST be toggled disable->enable each time RX is
	 * re-armed — the vendor does ZB_RADIO_RX_DISABLE at RX-ISR entry and
	 * ZB_RADIO_RX_ENABLE at exit on EVERY frame. Merely re-pointing the RX
	 * buffer address (0x0c08) leaves the DMA channel in its post-transfer
	 * state; after the HW auto-receives the coordinator's ACK to our own
	 * ACK-requested DataReq poll (surfaced as RF_IRQ_TX_DS on the tx-done
	 * path, which never ran the RX ISR), the channel is never re-armed, so
	 * the indirect ASSOCIATION-RESPONSE arriving a few ms later cannot DMA in
	 * and fires no RX IRQ — the join stalls. Our driver previously set 0x0c20
	 * once at init and never again; toggle it here so every RX re-arm resets
	 * the channel exactly like the vendor.
	 */
	/*
	 * Match the proven 8258 PHY rearm sequence.  RF status alone is not
	 * sufficient: the DMA channel and the CPU RF source have their own
	 * latched completion bits.  Leaving either latched makes the next RX
	 * look like an already-consumed DMA transaction and eventually leaves
	 * the router idle-deaf.
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
	const uint32_t cpu_rx_sources = BIT(4) | BIT(TLSR8258_IRQ_ZB_RT);
	bool rf_rx_pending = (TLSR_REG16(0x0f20) & RF_IRQ_RX) != 0u;
	bool global_irq_disabled = (TLSR_REG8(0x0643) & BIT(0)) == 0u;

	/*
	 * A pending RF completion with a live CPU source is normally left alone:
	 * the IRQ vector will consume it.  There is one important exception: the
	 * TLSR global IRQ gate can be cleared by the RF vector while the source
	 * remains asserted.  In that state the source is technically "live", but
	 * no vector can run, so the old source-absent test never repaired it.  This
	 * is the post-join failure mode: the radio reads RX/ DMA-enabled over SWS,
	 * yet the coordinator's unicast retries receive no MAC ACK.
	 */
	if (radio == NULL || !rf_rx_pending ||
	    ((TLSR_REG32(0x0648) & cpu_rx_sources) != 0u && !global_irq_disabled)) {
		return false;
	}

	/* RF/DMA completion is latched but its parent CPU source is gone. Clear the
	 * originating module status and arm a fresh buffer; the caller re-enables
	 * the CPU sources after this reset-free recovery. */
	TLSR_REG8(0x0643) = 0u;
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
	/* RF_IRQ_TX_DS is bit 8.  An 8-bit write silently discarded it and
	 * could leave the TX completion latch asserted after a software MAC ACK,
	 * blocking the next always-RX interrupt. */
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;
}

static inline void tlsr8258_rf_cpu_irq_sources_clear(void)
{
	/* 0x0648-0x64a are IRQ source readbacks, not W1C registers.  The
	 * level-triggered CPU sources deassert only after their modules are clear. */
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_RX | DMA_CHN_RF_TX;
}

/*
 * Shared body, force-inlined into both callers below so each gets its own
 * complete, independent copy in its own section -- no cross-section call
 * or return between them.
 */
static inline __attribute__((always_inline)) void tlsr8258_rf_irq_reenable_body(void)
{
	uint8_t global_irq = TLSR_REG8(0x0643);
	uint32_t irq_mask;

	/*
	 * RF IRQ entry clears reg_irq_en on this silicon.  irq_enable() only
	 * updates reg_irq_mask, and arch_irq_unlock() would restore the already
	 * cleared value, leaving the CPU deaf after the first RF event.  Keep the
	 * whole handoff in one masked sequence, as in the updated zigbee-rs
	 * always-RX path.
	 *
	 * Do not clear a source if a new RX completion arrived while this ISR was
	 * processing the previous frame: preserving it lets the just-rearmed DMA
	 * buffer trigger the next vector.
	 */
	TLSR_REG8(0x0643) = 0u;
	if ((TLSR_REG16(0x0f20) & RF_IRQ_RX_EVENTS) == 0u) {
		tlsr8258_rf_cpu_irq_sources_clear();
	}
	irq_mask = TLSR_REG32(0x0640);
	/* zigbee-rs CPU_RX_IRQ_MASK = DMA completion (bit4) plus the
	 * baseband ZB_RT source (bit13). Enabling only ZB_RT leaves RF
	 * auto-ACKs working while software RX completions never vector. */
	TLSR_REG32(0x0640) = irq_mask | BIT(4) | BIT(TLSR8258_IRQ_ZB_RT);
	compiler_barrier();
	TLSR_REG8(0x0643) = global_irq | BIT(0);
}

/* ISR-context callers: keep this resident in RAM so the tight ACK-turnaround
 * window is never exposed to a flash cache-miss stall mid-sequence. */
__attribute__((noinline, section(".ram_code")))
static void tlsr8258_rf_irq_reenable(void)
{
	tlsr8258_rf_irq_reenable_body();
}

/*
 * Thread-context caller (tlsr8258_zigbee_idle_rx_guard(), polled every
 * zb_thread loop pass -- not interrupt/timing-critical). Give it its own
 * plain-flash copy instead of jumping into the ISR's .ram_code copy from
 * ordinary flash-resident code: this project has already hit a documented
 * LLVM/TC32 defect where a flash->ram_code call from outside the expected
 * ISR calling context does not reliably return (see the historical
 * _attribute_ram_code_ / __ramfunc wedge). Confirmed on hardware: the whole
 * ZB thread loop can freeze forever (no CPU fault, just stops advancing)
 * while repeatedly polling idle_rx_guard() during an interview retry storm --
 * exactly the access pattern this call site has and the ISR call sites do
 * not. Two independent inlined copies cost a few dozen bytes of flash to
 * avoid ever making that cross-section call from thread context again.
 */
__attribute__((noinline))
static void tlsr8258_rf_irq_reenable_thread_ctx(void)
{
	tlsr8258_rf_irq_reenable_body();
}

static void tlsr8258_rf_tx_pkt(uint8_t *packet)
{
	uintptr_t addr = (uintptr_t)packet;

	/* Exact libdrivers_8258.a::rf_tx_pkt sequence from the original asm.
	 * The vendor sets both TX DMA ready latches.  Leaving reg_dma_tx_rdy1
	 * (0x0c5b) untouched lets the API report a completed TX while the PSDU
	 * never reaches the air after an RX/TX handoff. */
	TLSR_REG8(0x0c43) = 0x04u;
	TLSR_REG16(0x0c0c) = (uint16_t)addr;
	TLSR_REG8(0x0c5b) |= DMA_CHN_RF_TX;
	TLSR_REG8(0x0c24) |= DMA_CHN_RF_TX;
}

/*
 * Fast RX-mode switch that SKIPS the channel-offset reload (the PLL re-lock,
 * tens-to-hundreds of us on TLSR8258 — see tlsr8258_rf_set_txmode_for_ack).
 * The channel is already programmed, so the reload is wasted latency. This
 * matters on the TX-done path: the coordinator's ASSOCIATION-RESPONSE arrives
 * only ~260 us after it ACKs our poll (sniffer-measured), and the full
 * tlsr8258_rf_set_rxmode()'s PLL re-lock left the radio not-yet-listening in
 * that window, so the AssocResp was missed and the join never completed. This
 * mirrors the vendor rf_set_rxmode() which also does no channel reload.
 *
 * Retained for reference but no longer called: unlike set_rxmode_vendor() it
 * still writes 0x0f02 = RF_TRX_OFF first (the TX/RX state-machine reset), which
 * opens a ~200us-13ms deaf window on the TX->RX turnaround. Every post-TX RX
 * re-arm now uses the reset-free set_rxmode_vendor() instead (see the has_tx ISR
 * branch), so unicasts the coordinator sends right after we ACK are not lost.
 */
static void tlsr8258_rf_set_rxmode_fast(void) __maybe_unused;
static void tlsr8258_rf_set_rxmode_fast(void)
{
	TLSR_REG8(0x0f02) = RF_TRX_OFF;
	TLSR_REG8(0x0428) = (uint8_t)(TLSR_REG8(0x0428) | BIT(0));
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
}

/*
 * Vendor-EXACT RX-mode switch (chip_8258/rf_drv.h rf_set_rxmode): ONLY
 *   0x0428 = RF_TRX_MODE | BIT(0)   // rx enable
 *   0x0f02 = RF_TRX_OFF  | BIT(5)   // RX enable
 * Crucially it does NOT write 0x0f02 = RF_TRX_OFF first (the TX/RX state-
 * machine RESET) and does NOT reload the channel/PLL — the vendor switches
 * straight from TX-enable (0x55) to RX-enable (0x65). Our set_rxmode /
 * set_rxmode_fast both insert that 0x45 reset (and set_rxmode also relocks the
 * PLL) on every TX->RX turnaround; the poll TX completes as plain RF_IRQ_TX
 * (confirmed, no auto-ACK), so the coordinator's ACK+AssocResp must be caught
 * as ordinary RX right after — and the reset/relock in that ~200us window is
 * the prime suspect for the radio not being live when the reply lands. Keep
 * 0x0f16 at the vendor's fixed active-session value; it is not switched per
 * TX/RX operation.
 */
static void tlsr8258_rf_set_rxmode_vendor(void)
{
	/* Exact libzigbee/sdk/platform/chip_8258/rf_drv.h::rf_set_rxmode().
	 * The state-machine reset and channel/PLL programming belong to the
	 * explicit TX/RX-off and channel-change paths, not to the idle RX handoff.
	 * Keeping this two-write sequence is what makes the router continuously
	 * receivable immediately after an ACK or normal TX completion. */
	TLSR_REG8(0x0428) = RF_TRX_MODE | BIT(0);
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(5);
}

/* Restore always-RX after a synchronous TX timeout. */
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
 * Match zigbee-rs::send_mac_frame() before every ordinary TX.  The
 * association poll has its own copy of this sequence; ordinary ZDP/ZCL TX
 * used to skip it and relied on whatever RF/DMA state the preceding RX or
 * MAC-ACK left behind.  That was sufficient immediately after join but made
 * the first response after a long always-RX idle window disappear while the
 * software TX API still reported success.
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
 * Fast TX-mode switch for MAC ACK transmission from inside the RX ISR.
 * This is the vendor rf_trx_state_set(RF_MODE_TX) sequence observed in
 * libdrivers_8258.a: reset the link-layer state machine, enable TX, and clear
 * the vendor TX gate bit. Do not reload the channel/PLL in this hot path.
 */
static void tlsr8258_rf_set_txmode_for_ack(void)
{
	/* Match libdrivers_8258.a::rf_trx_state_set(RF_MODE_TX): reset the
	 * RF state machine, enable TX, and disable the RX gate. */
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

/* rx_length_ok / rx_crc_ok are used by the RX ISR (tlsr8258_rx_capture_common)
 * in every config, so they must live OUTSIDE the CONFIG_IEEE802154_RAW_MODE
 * (net-stack path) guard below. */
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
	if (((uint16_t)rx[0] + 3u) >= TLSR8258_RX_BUF_SIZE) {
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
	    memcmp(&payload[TLSR8258_PAN_ID_OFFSET], radio->filter_pan_id,
		   TLSR8258_PAN_ID_SIZE) != 0 &&
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
	       payload[hdr_len] == 0x02u && /* MAC_CMD_ASSOCIATION_RESPONSE */
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
 * tests/unit/zigbee_ack_filter_match covers exactly what runs on hardware.
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

static void tlsr8258_send_ack_if_needed(const uint8_t *payload, uint8_t length,
					bool tx_prepared, uint32_t tx_prepared_at_cycles,
					uint32_t rx_complete_at_cycles,
					struct tlsr8258_radio_data *radio)
{
	/*
	 * Hot-path layout (option D):
	 *   - Caller (rx_capture_common) has already verified ack_requested via
	 *     tlsr8258_ack_requested() — do NOT re-check here.
	 *   - ACK PSDU is built from a 3-byte template (frame-control + seq);
	 *     only the seq byte changes per ACK, so writing it inline avoids the
	 *     stack copy that the previous local array forced.
	 *   - Settle is measured from the RX-completion timestamp, matching
	 *     zigbee-rs; only the remaining part of the 120us window is spun.
	 *   - All radio->debug stores have moved out of the timing-critical
	 *     window; they only run after the ACK is on air.
	 */
	uint8_t ack_psdu[3];
	uint32_t elapsed_cyc;
	uint16_t waited = 0u;

	/*
	 * During association the PAN/short filter still contains the invalid
	 * pre-join tuple (normally 0xffff/0xffff), while the coordinator's
	 * successful Association Response is addressed to our IEEE address.
	 * It is already accepted by the RX software filter below; it must also
	 * be ACKed here, otherwise the coordinator keeps retransmitting the
	 * response and never queues the Transport-Key burst.
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
	if (tlsr8258_set_tx_payload_to(radio->ack_buffer, ack_psdu,
				       sizeof(ack_psdu)) < 0) {
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

	uint32_t pre_wait_cyc = k_cycle_get_32();
	if (radio->debug != NULL) {
		radio->debug->pre_busy_wait_cyc = pre_wait_cyc;
		uint32_t delta =
			(pre_wait_cyc - tx_prepared_at_cycles) /
			(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u);
		radio->debug->capture_to_wait_us =
			(uint16_t)((delta > 0xffffu) ? 0xffffu : delta);
	}

	/*
	 * Cycle-domain busy-wait. The timestamp is taken at RX completion (the
	 * ISR-entry timestamp is the closest available C equivalent), not after
	 * the TX handoff or frame parsing. This is the same remaining-settle
	 * calculation used by zigbee-rs::send_ack_fast().
	 */
	do {
		elapsed_cyc = k_cycle_get_32() - rx_complete_at_cycles;
	} while (elapsed_cyc < TLSR8258_ACK_TURNAROUND_CYC);

	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	uint32_t tx_kick_cyc = k_cycle_get_32();
	if (radio->debug != NULL) {
		radio->debug->tx_kick_cyc = tx_kick_cyc;
		uint32_t delta =
			(tx_kick_cyc - pre_wait_cyc) /
			(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u);
		radio->debug->wait_duration_us =
			(uint16_t)((delta > 0xffffu) ? 0xffffu : delta);
	}
	tlsr8258_rf_tx_pkt(radio->ack_buffer);

	/*
	 * Complete the MAC-ACK handoff before leaving this RX ISR, matching
	 * libzigbee/platform/chip_8258/rf_drv.h::rf_rx_irq_handler().  The
	 * TLSR8258 does not reliably generate a follow-up TX-done CPU edge for
	 * this short, software-triggered ACK.  Deferring the wait therefore
	 * leaves the RF state machine in TX (0x0f02 == 0x55), and the next
	 * coordinator request arrives while the router is deaf.  The old
	 * deferred version relied on the idle guard to repair that state, but
	 * the repair is necessarily too late for the next unicast.
	 *
	 * Keep the wait bounded.  A normal three-byte ACK completes well below
	 * this limit; on a lost completion latch we still clear the TX status
	 * and force the reset-free vendor TX->RX transition before returning.
	 * RX DMA is re-armed by the caller after this function returns.
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
	(void)waited;
}

static int8_t tlsr8258_rx_rssi_dbm(const uint8_t *rx)
{
	if (rx[0] < (TLSR8258_RX_BUF_SIZE - 2u)) {
		return (int8_t)rx[rx[0] + 2u] - 110;
	}

	return -110;
}

static void tlsr8258_rx_capture_common(uint16_t irq_status, uint8_t *snapshot,
				       uint16_t snapshot_size,
				       struct tlsr8258_radio_data *radio)
{
	uint8_t *rx = radio->rx_proc;
	uint8_t *payload = &rx[TLSR8258_PAYLOAD_OFFSET];
	uint16_t rx_ack = irq_status & RF_IRQ_RX_EVENTS;
	uint16_t snapshot_len = 0u;
	uint16_t rx_dma_len;
	int8_t rx_rssi_dbm;
	/*
	 * Capture the RX-ISR-entry timestamp before any other work for diagnostics.
	 * The ACK turnaround timestamp is captured separately, immediately after
	 * switching to TX mode, matching vendor mac_phy.c.
	 */
	uint32_t isr_entry_cycles = k_cycle_get_32();
	if (radio->debug != NULL) {
		radio->debug->isr_entry_cyc = isr_entry_cycles;
	}
	if (rx_ack == 0u) {
		rx_ack = RF_IRQ_RX;
	}
	TLSR_REG16(0x0f20) = rx_ack;
	tlsr8258_radio_rx_count_inc(radio);

	/*
	 * Vendor mac_phy.c rf_rx_irq_handler drops CRC-fail / length-inconsistent
	 * frames HERE — before the address filter, MAC-ACK, or PSDU parse:
	 *   if ((!ZB_RADIO_CRC_OK(p)) || (!ZB_RADIO_PACKET_LENGTH_OK(p)) ...) {
	 *       ZB_RADIO_RX_BUF_CLEAR(rf_rxBuf); ZB_RADIO_RX_ENABLE; return; }
	 * On a busy channel the radio DMAs noise/collisions as frames with a garbage
	 * length byte (RTT: "RX sink rejected invalid frame len=146/226/242"). This
	 * guard MUST precede dma_payload_len_get()/rx_ack_decision() below: a garbage
	 * rx[0]/rx[4] otherwise yields a bogus `length` (up to ~250) so the PSDU parse
	 * reads several bytes past the DMA buffer into the adjacent rx_shadow AND — the
	 * real hazard — we would MAC-ACK and run the AssocResp filter-handoff on pure
	 * noise. Re-arm RX and drop, exactly as the vendor does. rx_active/rx_proc were
	 * already swapped by the caller, so the DMA stays armed for the next frame.
	 */
	if (!tlsr8258_rx_length_ok(rx)) {
		tlsr8258_rf_set_rxmode_vendor();
		return;
	}
	if (!tlsr8258_rx_crc_ok(rx)) {
		/* The 8258 RF RX latch is CRC-gated.  With the TB03F active
		 * 0x0f03 profile, the trailer/status byte does not match the old
		 * vendor macro even though RF_IRQ_RX is asserted and the frame is
		 * successfully ACKed.  Treat the hardware RX event as authoritative;
		 * only a CRC2-only event is a real CRC failure. */
		if ((irq_status & RF_IRQ_RX) == 0u) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
	}

	uint8_t length = tlsr8258_dma_payload_len_get(rx, (uint16_t)rx[0] + 4u);
	uint32_t ack_prepared_at_cycles = 0u;
	bool ack_mode_prepared = false;
	bool ack_requested_early = tlsr8258_ack_requested(payload, length);

	/* Match zigbee-rs take_completed_rx(): leave RX and enter TX before any
	 * header/debug/filter work.  AssocResp is only valid for a very short ACK
	 * turnaround window; doing this below the parser made the ACK intermittent
	 * and the coordinator retransmit the response. */
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
		if (radio->debug != NULL) {
			radio->debug->rx_capture_debug_count++;
			radio->debug->rx_capture_irq_debug = irq_status;
			radio->debug->rf_irq_ack_debug = rx_ack;
		}
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
		if (radio->debug != NULL) {
			radio->debug->ack_capture_cyc = ack_prepared_at_cycles;
			uint32_t delta =
				(ack_prepared_at_cycles - isr_entry_cycles) /
				(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u);
			radio->debug->isr_to_capture_us =
				(uint16_t)((delta > 0xffffu) ? 0xffffu : delta);
		}
		tlsr8258_send_ack_if_needed(payload, length, true,
					    ack_prepared_at_cycles, isr_entry_cycles, radio);
	}

	/* The Zigbee sink bypasses Zephyr's net-stack receive path, so the
	 * ordinary tlsr8258_filter_match() in rx_dispatch() is not reached there.
	 * Apply the same PAN/address filter before consuming one of the 16 deferred
	 * RX slots. Otherwise broadcasts and unrelated unicast traffic can fill the
	 * FIFO while the ISR still emits MAC ACKs, leaving the coordinator's next
	 * interview request ACKed but never delivered to the stack. Keep ACK frames
	 * only while a stack TX is waiting for that ACK; idle ACKs have no Zigbee
	 * payload and only waste a deferred slot. */
	if ((payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) == 0x02u) {
		if ((radio->op.state != TLSR8258_RADIO_OP_TX_PENDING) &&
		    (radio->op.state != TLSR8258_RADIO_OP_WAITING_POST_TX_RX)) {
			tlsr8258_rf_set_rxmode_vendor();
			return;
		}
	} else if (tlsr8258_zigbee_rx_sink != NULL) {
		/* RAW_MODE only selects the Zephyr L2 delivery ABI.  Once the
		 * Zigbee sink is installed, retain the normal address filter so
		 * broadcast traffic and unrelated unicast traffic cannot fill the
		 * ISR-to-Zigbee queue and evict join/interview frames. */
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
	 * Early radio-filter handoff (Zigbee join fix). The coordinator sends its
	 * Transport-Key burst to our freshly-assigned short within a few ms of the
	 * ASSOCIATION-RESPONSE — before the stack-side fast-handoff runs — so program
	 * the assigned short (and PAN) into the filter the instant we RX a SUCCESS
	 * AssocResp addressed to our IEEE, so the Transport-Key frames are ACKed
	 * straight from this ISR.
	 *
	 * Done AFTER the ACK is on air, not before: this parse (mac_hdr_size +
	 * 8-byte memcmp) sits between the RX-ISR-entry timestamp the ACK turnaround
	 * is measured from and the ACK transmission, so running it first delayed our
	 * MAC-ACK of the (large dual-IEEE) AssocResp past the coordinator's
	 * aTurnaroundTime window — the coord then never saw our ACK, retransmitted
	 * the AssocResp, and gave up without sending the Transport-Key. The filter
	 * update is only needed for the Transport-Key that arrives ~ms later, so it
	 * is safe to defer to just after the ACK.
	 */
	if (((payload[TLSR8258_FRAME_TYPE_OFFSET] & 0x07u) == 0x03u) &&
	    ((payload[TLSR8258_DEST_ADDR_TYPE_OFFSET] & TLSR8258_DEST_ADDR_TYPE_MASK) ==
	     TLSR8258_DEST_ADDR_TYPE_IEEE)) {
		uint16_t arsp_fcf = sys_get_le16(payload);
		uint8_t arsp_hdr = tlsr8258_mac_hdr_size(arsp_fcf, length);

		if ((arsp_hdr != 0u) && ((uint16_t)(arsp_hdr + 4u) <= length) &&
		    (payload[arsp_hdr] == 0x02u) &&        /* MAC_CMD_ASSOCIATION_RESPONSE */
		    (payload[arsp_hdr + 3u] == 0x00u) &&   /* MAC_SUCCESS */
		    (memcmp(&payload[TLSR8258_DEST_ADDR_OFFSET], radio->filter_ieee_addr,
			    TLSR8258_IEEE_ADDR_SIZE) == 0)) {
			radio->filter_pan_id[0] = payload[TLSR8258_PAN_ID_OFFSET];
			radio->filter_pan_id[1] = payload[TLSR8258_PAN_ID_OFFSET + 1u];
			radio->filter_short_addr[0] = payload[arsp_hdr + 1u];
			radio->filter_short_addr[1] = payload[arsp_hdr + 2u];
		}
	}

	/*
	 * Debug bookkeeping deferred until after the ACK is on air. Same
	 * principle as in send_ack_if_needed: nothing in the busy-wait /
	 * tx-kick window should touch debug RAM.
	 */
	if (radio->debug != NULL) {
		radio->debug->rx_capture_debug_count++;
		radio->debug->rx_capture_irq_debug = irq_status;
		radio->debug->rf_irq_ack_debug = rx_ack;
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
	rx_dma_len = (snapshot_len >= TLSR8258_PAYLOAD_OFFSET) ? snapshot_len : (uint16_t)rx[0] + 4u;
	rx_dma_len = MIN(rx_dma_len, UINT8_MAX);
	(void)tlsr8258_rx_queue_try_enqueue(&radio->rx_queue, rx, (uint8_t)rx_dma_len,
					    rx_rssi_dbm);
}

/* Keep the worker-side sink handoff as a real call boundary. */
__attribute__((noinline))
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
	    !tlsr8258_assoc_resp_for_us(radio, (uint8_t *)&rx[TLSR8258_PAYLOAD_OFFSET],
					 length)) {
		return;
	}

	pkt = net_pkt_rx_alloc_with_buffer(radio->iface, length, NET_AF_UNSPEC, 0,
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

	if (net_recv_data(radio->iface, pkt) < 0) {
		net_pkt_unref(pkt);
	}
#endif
}

/*
 * Drain rx_queue from the ZB thread (see zb_platform_radio_rx_poll). This is
 * the SOLE RX consumer: there is deliberately no separate COOP RX worker
 * thread, because the zb_thread no-yield busy-loop (zb_main.c:390)
 * intermittently starves a cooperative worker — on some boots it never
 * dequeues a frame, so RX (AssocResp / Transport-Key / interview) silently
 * stalls and the router re-associates forever. Single-consumer here (zb_thread
 * only) removes BOTH the starvation and the zb_buf-pool data race a second
 * concurrent consumer would cause. TX/ACK completion is independent — handled
 * directly in tlsr8258_rf_isr (k_sem_give tx_wait on WAITING_POST_TX_RX) — so
 * it is unaffected.
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
 * Vendor-pattern .ram_code placement. Symbol stays at a FLASH address but
 * sits inside the icache-locked low-flash window configured by reset.S
 * SET_IC, so every instruction fetch in the RF IRQ handler is a 1-cycle
 * cache hit instead of a multi-cycle XIP miss.  No SRAM copy, no veneer
 * thunks, no _sw_isr_table timing window.
 */
__attribute__((section(".ram_code")))
static void tlsr8258_rf_isr(const void *arg)
{
	/*
	 * Capture the ISR-entry cycle counter as the very first instruction so
	 * that downstream latency measurements (rx_capture_common timestamp,
	 * busy_wait entry, tx kick, tx done) all have a common t=0 reference.
	 * Stashed in a local because radio->debug is dereferenced through dev
	 * a few instructions later; using a local first keeps the measurement
	 * stable even if the compiler reorders the prologue.
	 */
	uint32_t isr_entry_cyc = k_cycle_get_32();
	struct tlsr8258_radio_data *radio = (struct tlsr8258_radio_data *)arg;
	uint16_t irq = TLSR_REG16(0x0f20);
	/* On this silicon the RF RX latch can vector before DMA2 has written
	 * the length byte and trailer.  Do not swap/re-arm the active buffer in
	 * that window: doing so discards the just-received long unicast (most
	 * visibly the indirect Association Response).  A valid frame normally
	 * becomes coherent immediately; the bounded wait is only on the invalid
	 * transient path and remains inside the MAC ACK deadline. */
	if ((irq & RF_IRQ_RX_EVENTS) != 0u &&
	    (!tlsr8258_rx_length_ok(radio->rx_active) ||
	     !tlsr8258_rx_crc_ok(radio->rx_active))) {
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
	uint8_t dma_len = radio->rx_active[0];
	uint8_t crc = 0u;
	struct tlsr8258_radio_debug *debug = radio->debug;
	bool has_rx = tlsr8258_rf_irq_has_rx_event(effective_irq);
	bool has_tx = (effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u;

	if (debug != NULL) {
		uint32_t prev = debug->prev_isr_entry_cyc;
		uint32_t gap_us = (isr_entry_cyc - prev) /
				  (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000u);

		debug->isr_entry_cyc = isr_entry_cyc;
		debug->prev_isr_entry_cyc = isr_entry_cyc;
		if (prev != 0u) {
			debug->inter_isr_gap_us = gap_us;
			if (gap_us > debug->inter_isr_gap_max_us) {
				debug->inter_isr_gap_max_us = gap_us;
			}
		}
		debug->rf_isr_entry_count++;
		debug->rf_irq_raw_debug = irq;
		debug->rf_irq_effective_debug = effective_irq;
		debug->rf_irq_mask_debug = TLSR_REG16(0x0f1c);
		debug->rf_dma_len_debug = dma_len;
		debug->rf_psdu_len_debug = radio->rx_active[4];
		/*
		 * NOTE: reading registers 0x0f00, 0x0f02, 0x0428, 0x0430 inside
		 * the RF ISR triggered a chip hang in testing — the device sent
		 * exactly one BeaconReq and then stopped responding to RF and to
		 * SWire.  Some of these RF control registers must have read-side
		 * effects on the TLSR8258 state machine, so we skip the snapshot.
		 * If we need to inspect chip state later, do it outside the ISR
		 * (e.g. from a periodic timer in the main thread).
		 */
	}
	if ((dma_len != 0u) && (dma_len < (TLSR8258_RX_BUF_SIZE - 3u))) {
		crc = radio->rx_active[dma_len + 3u];
	}
	if (debug != NULL) {
		debug->rf_crc_debug = crc;
	}
	tlsr8258_radio_last_irq_set(radio, effective_irq);

	/*
	 * LEVEL-IRQ SAFETY: if the raw IRQ carried an RX-class bit but
	 * tlsr8258_rf_irq_effective_status() dropped it (invalid/transient DMA
	 * frame — !dma_valid && !dma_sane), then has_rx is false and none of the
	 * branches below clear those raw RX bits. ZB_RT (the RF IRQ) is
	 * level-triggered, so an uncleared RX bit keeps the interrupt asserted
	 * and the ISR re-fires forever — starving the ZB thread (observed as the
	 * post-join freeze: nested==1, reg_irq_src bit13 set, rf_irq 0xf20=RX).
	 * Clear the dropped RX bits here so the level can deassert.
	 */
	if (!has_rx && (irq & RF_IRQ_RX_EVENTS) != 0u) {
		TLSR_REG16(0x0f20) = RF_IRQ_RX_EVENTS;
		/* A raw RX completion can be transient/invalid before the DMA header
		 * is coherent.  Clearing only RF_IRQ_RX leaves DMA2 in its completed
		 * state and makes this the last ISR forever.  zigbee-rs still consumes
		 * that completion through take_completed_rx(), which clears DMA2 and
		 * arms the next buffer; mirror that behavior here. */
		radio->rx_active[0] = 0u;
		radio->rx_active[4] = 0u;
		tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
		tlsr8258_rf_set_rxmode_vendor();
		if (debug != NULL) {
			debug->rf_irq_ack_debug = RF_IRQ_RX_EVENTS;
		}
	}

	/*
	 * Handle TX completion before RX when both bits are asserted in the same
	 * ISR.  Data Request polling can receive the ACK/pending frame quickly
	 * enough that hardware reports a combined TX+RX event; if RX wins, the
	 * radio-op remains TX_PENDING and tx() times out even though the frame was
	 * received and delivered asynchronously.
	 */
	if (has_tx) {
		bool tx_complete;
		uint32_t key;
		bool ack_tx_completion;
		struct tlsr8258_core_tx_done_result tx_done_result;

		if (debug != NULL) {
			debug->rf_branch_debug = has_rx ? 6u : 2u;
			debug->rf_irq_ack_debug = effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS);
		}
		tlsr8258_rf_tx_status_clear();
		TLSR_REG16(0x0f20) = effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS);
		tlsr8258_radio_tx_count_inc(radio);
		{
			tlsr8258_core_handle_tx_done(effective_irq & (RF_IRQ_TX | RF_IRQ_TX_DS),
						      has_rx, radio->op.ack_tx_pending,
						      radio->op.expect_post_tx_rx,
						      radio->op.state == TLSR8258_RADIO_OP_TX_PENDING,
						      &tx_done_result);

			if (tx_done_result.enter_rx_fast) {
			bool poll_followup =
				tlsr8258_core_psdu_expects_post_tx_followup(
					&radio->tx_buffer[TLSR8258_PAYLOAD_OFFSET],
					radio->tx_buffer[4] - TLSR8258_FCS_LENGTH) &&
				!has_rx;

			/*
			 * Post-TX return to RX, mirroring the vendor rf_rx_irq_handler
			 * ORDER: (1) switch to RX mode, then (2) re-point the RX buffer and
			 * re-enable the RX DMA channel LAST. ([80] proved our poll completes
			 * as plain RF_IRQ_TX, not TX_DS — the HW does NOT auto-consume the
			 * ACK — so the coordinator's ACK+AssocResp arrive as ordinary RX
			 * ~200us-13ms later and the radio must be cleanly listening by then.)
			 * Order: re-point the RX buffer + re-arm the RX DMA channel, THEN
			 * switch to RX mode (the config that produced the first-ever
			 * AssocResp reception + ACK; the reverse order regressed to 0
			 * handoffs on HW).
			 */
				if (tx_done_result.rearm_rx_buffer) {
					radio->rx_active =
						tlsr8258_core_next_rx_buffer(radio->rx_active,
									      radio->rx_buffer,
									      radio->rx_shadow);
					radio->rx_active[0] = 0u;
					radio->rx_active[4] = 0u;
					tlsr8258_rf_rx_buffer_set(radio->rx_active,
								  TLSR8258_RX_BUF_SIZE);
				}
				if (poll_followup) {
					tlsr8258_rf_set_rxmode_vendor();
				} else {
					/*
					 * Post-TX (including MAC-ACK completion) return to
					 * RX. Use the vendor no-reset turnaround rather than
					 * set_rxmode_fast(): _fast still writes
					 * 0x0f02 = RF_TRX_OFF first (the TX/RX state-machine
					 * reset), which leaves the radio deaf for
					 * ~200us-13ms. After we MAC-ACK a frame addressed to
					 * us, that deaf window drops the coordinator's very
					 * next frame to us (the Transport-Key burst and the
					 * post-join ZDP interview requests), so those unicasts
					 * are never received. Switching to the vendor-exact
					 * TX-en -> RX-en turnaround (no reset, no PLL reload)
					 * measured ~8x more of our unicasts ACKed on HW. Same
					 * root cause as the assoc-poll fix (commit dd3854ab),
					 * which only covered the poll_followup path above.
					 */
					tlsr8258_rf_set_rxmode_vendor();
				}
			}
		}
		key = irq_lock();
		/*
		 * Step 2 — if this TX completion is for the MAC ACK we kicked
		 * inside the previous RF ISR, just clear the pending flag and
		 * skip the stack-TX state machine (the op state is not
		 * TX_PENDING for ACK transmissions, but we still want to make
		 * absolutely sure tx_wait doesn't get a stray give and that
		 * op_on_tx_success doesn't reset state on an ack-only TX).
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
		 * Match the updated zigbee-rs always-RX vector: a second packet can
		 * complete while the first one is being ACKed/copied.  Drain only a
		 * bounded pair here; if the latch is still asserted afterward, leave
		 * it pending for the next CPU vector instead of spinning in the ISR.
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

		if (debug != NULL) {
				debug->rf_branch_debug = has_tx ? 7u : 1u;
				debug->rf_isr_rx_event_count++;
			}
			/*
			 * Double-buffer swap (vendor mac_phy.c rf_rx_irq_handler): the RF DMA
			 * just filled rx_active. Hand that buffer to rx_proc for processing and
			 * point the DMA at the OTHER of {rx_buffer, rx_shadow} BEFORE parsing or
			 * ACKing, so the next frame lands in a fresh buffer.
			 */
			tlsr8258_core_handle_rx_dma(radio->rx_active, radio->rx_buffer,
							radio->rx_shadow, &rx_dma_result);
			radio->rx_proc = rx_dma_result.rx_proc;
			radio->rx_active = rx_dma_result.next_rx_active;
			if (rx_dma_result.rearm_rx_buffer) {
				/*
				 * Keep DMA2 disabled while rx_capture_common() performs
				 * the MAC-ACK/parse/queue handoff.  Re-enabling it before
				 * clearing the current RF completion lets this TLSR8258
				 * latch the same completion again; on hardware that becomes
				 * an ISR storm (thousands of dispatches for one frame) and
				 * starves the actual Zigbee traffic.  The vendor path updates
				 * the next address first and enables RX only after consuming
				 * the completed buffer.
				 */
				radio->rx_active[0] = 0u;
				radio->rx_active[4] = 0u;
				TLSR_REG8(0x0c20) &= (uint8_t)~DMA_CHN_RF_RX;
			}

			tlsr8258_rx_capture_isr(effective_irq, radio);
			if (rx_dma_result.rearm_rx_buffer) {
				tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_BUF_SIZE);
				/* A MAC ACK is kicked from rx_capture_common() and its
				 * TX-done ISR owns the TX->RX transition.  Do not switch
				 * the RF state machine underneath that pending ACK. */
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
			 * Some TX completions on this chip arrive as RX_EVENT only (the
			 * MAC ACK reception that auto-follows a frame with the ACK_REQUEST
			 * bit set), with neither RF_IRQ_TX nor RF_IRQ_TX_DS asserted in the
			 * same ISR.  The RX event is not, by itself, proof of TX completion:
			 * a coordinator retry or an unrelated beacon can arrive while our
			 * TX is pending.  Complete only when the completed DMA buffer is the
			 * MAC ACK for the exact sequence number of this TX.
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

			tlsr8258_core_handle_rx_only_tx_completion(false,
									 radio->op.state ==
										 TLSR8258_RADIO_OP_TX_PENDING,
									 ack_for_tx,
									 &rx_only_tx_result);
			key = irq_lock();
			tx_complete = rx_only_tx_result.complete_stack_tx &&
				      tlsr8258_radio_op_on_tx_success(&radio->op);
			irq_unlock(key);
			if (tx_complete) {
				k_sem_give(&radio->tx_wait);
			}
			/*
			 * Our ack-requested poll completed via its ACK arriving as an
			 * RX event only (no RF_IRQ_TX), so the has_tx TX->RX turnaround
			 * above never ran and the RX state machine was left un-re-armed.
			 * The coordinator's ASSOCIATION-RESPONSE (indirect, to our
			 * not-yet-short IEEE) follows only ~260us later; without a
			 * reset-free re-arm here the radio is not listening and the reply
			 * is lost (confirmed: AssocResp on air, 0 reach rx_capture_common).
			 * Mirror the has_tx branch's reset-free re-arm so the reply lands.
			 */
			tlsr8258_rf_set_rxmode_vendor();
		}
	} else if (!has_tx && (effective_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
		bool tx_failed = false;
		uint32_t key;

		if (debug != NULL) {
			debug->rf_branch_debug = 3u;
			debug->rf_irq_ack_debug = effective_irq;
		}
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

			if (debug != NULL && !has_tx) {
				debug->rf_branch_debug = (effective_irq != 0u) ? 4u : 5u;
				debug->rf_irq_ack_debug = ack;
			}
		TLSR_REG16(0x0f20) = ack;
	}

	/*
	 * TLSR8258 clears the global IRQ-enable latch on entry to the RF
	 * interrupt.  The Rust/vendor IRQ vector explicitly sets it again before
	 * returning; without the matching re-enable here the first RF event is
	 * handled, then no further RX/TX interrupt can arrive.  That presents as
	 * an idle-deaf radio (0x0f02 falls back to RF_TRX_OFF) and also prevents
	 * the MAC-ACK path from keeping the coordinator's retry window alive.
	 */
irq_reenable:
	(void)tlsr8258_rf_recover_stuck_rx(radio);
	tlsr8258_rf_irq_reenable();
}

static void tlsr8258_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct tlsr8258_radio_data *radio = dev->data;
	uint8_t *mac = radio->mac_addr;
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

	return ((int8_t)TLSR_REG8(0x0441) - 110) < CONFIG_IEEE802154_TLSR8258_CCA_RSSI_THRESHOLD ?
		       0 :
		       -EBUSY;
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
		/* Channel changes must return directly to RX.  The resetful helper
		 * creates the same idle-deaf window as the old TX->RX path. */
		tlsr8258_rf_set_rxmode_vendor();
	}

	return 0;
}

static int tlsr8258_filter(const struct device *dev, bool set,
			   enum ieee802154_filter_type type,
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
		tlsr8258_rf_irq_runtime_mask() |
		RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT;

	if (tlsr8258_radio_started_get(radio)) {
		return -EALREADY;
	}

	/* Full SDK/Rust PHY bring-up order. */
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

	/* Keep the TLSR8258 Zigbee receive profile that was validated by the
	 * Zephyr router interview on hardware.  Bit7 of 0x0405 is required by the
	 * 8258 RX access-code path for IEEE-unicast frames; without it the radio
	 * still receives beacons/broadcasts but drops the coordinator's long-
	 * addressed AssocResp before DMA2.  The active-session LL/settle values
	 * below are the matching vendor profile (0xf0/113), not the newer generic
	 * Rust PHY defaults (0x10/150) which are not interchangeable here.
	 */
	TLSR_REG8(0x0401) = 0u;
	TLSR_REG8(0x0404) &= (uint8_t)~BIT(5);
	TLSR_REG8(0x0405) = 0x84u;
	TLSR_REG8(0x0f15) = 0xf0u;
	TLSR_REG16(0x0f04) = 113u;
	TLSR_REG8(0x0f03) &= (uint8_t)~BIT(2);
	tlsr8258_rf_debug_reset(radio);
	radio->rx_active = radio->rx_buffer;
	radio->rx_proc = radio->rx_buffer;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	radio->rx_buffer[0] = 0u;
	radio->rx_buffer[4] = 0u;
	radio->rx_shadow[0] = 0u;
	radio->rx_shadow[4] = 0u;
	TLSR_REG8(0x0c20) |= DMA_CHN_RF_RX | DMA_CHN_RF_TX;
	/* 0 dBm (rf_power_level_list[30] = 0xa9). TX power is not the interview-
	 * reliability factor; use a clean standard 0 dBm. */
	tlsr8258_rf_set_power_level(rf_power_level_list[30]);
	TLSR_REG8(0x0c26) = 0x0cu;
	TLSR_REG8(0x0c21) = 0x04u;
	/* The vendor headers label bit 5 as BLE NESN-init and do not set it for
	 * Zigbee.  On this TB03F, however, the live PHY loses all MAC-ACK/RX
	 * activity after the idle handoff unless the RX latch is asserted here;
	 * the A/B test is unambiguous (0x1a: no ACK, 0x3a: ACK).  Keep this
	 * board-specific workaround until the underlying 8258 RF blob behaviour
	 * is replaceable.  Bit 2 remains cleared as in vendor rf_drv.h. */
	TLSR_REG8(0x0f03) |= BIT(5);
	TLSR_REG16(0x0f20) = RF_IRQ_ALL;
	TLSR_REG16(0x0f1c) = 0u;
	TLSR_REG16(0x0f1c) = runtime_irq_mask;
	TLSR_REG8(0x0430) |= BIT(1);
	/* Match zigbee-rs set_rx_mode(): no RF_TRX_OFF reset here.  The radio is
	 * already stopped by the ordered bring-up above; this is the first and
	 * only RX arm for the idle router. */
	tlsr8258_rf_set_rxmode_vendor();
	/* RF IRQ entry clears the global CPU gate on TLSR8258.  Use the same
	 * atomic mask/source/global restore as the updated zigbee-rs driver even
	 * for the initial RX arm; irq_enable() only changes REG_IRQ_MASK. */
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

	/* Keep multiple DMA-owned packets alive. The zigbee-rs reference uses a
	 * TX FIFO for the same reason: RF_IRQ_TX can complete before DMA3 has
	 * released the descriptor, while the next stack request is already
	 * preparing another encrypted frame. */
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

	/* MCU_CORE_8258's vendor path uses a raw DMA byte count here.  The
	 * rf_tx_packet_dma_len() word/remainder encoding belongs to B91/8278,
	 * not this 8258 RF block. */
	dma_len = (uint32_t)payload_len + 1u;
	tx_buffer[0] = (uint8_t)dma_len;
	tx_buffer[1] = (uint8_t)(dma_len >> 8);
	tx_buffer[2] = (uint8_t)(dma_len >> 16);
	tx_buffer[3] = (uint8_t)(dma_len >> 24);
	tx_buffer[4] = payload_len + TLSR8258_FCS_LENGTH;
	/* The DMA header makes the payload destination unaligned by five bytes.
	 * Use an explicit byte copy; the TC32 memcpy path can leave the final MIC
	 * byte stale on this boundary. */
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

/*
 * Vendor-model synchronous path for the ACK-requested DataReq association poll.
 *
 * The async completion of this poll was non-deterministic: it finished either
 * via the RF ISR (fast, set_rxmode_vendor) OR — when the TX IRQ missed the ISR
 * — via the tx() status-poll fallback, which re-entered RX with the SLOW full
 * set_rxmode (channel reload + PLL relock). So the TX->RX turnaround timing
 * jittered run-to-run and was sometimes too slow, leaving the radio not
 * listening in the ~200 us-13 ms window when the coordinator's indirect
 * ASSOCIATION-RESPONSE lands. That variance is why reception was marginal.
 *
 * Mirror the vendor rf_tx_irq_handler instead: with the RF ISR masked, kick the
 * poll, busy-poll for TX-done, then IMMEDIATELY re-arm the RX DMA buffer and
 * switch to RX via the vendor-exact set_rxmode (no reset, no PLL reload) — a
 * single deterministic fast turnaround every time. Re-enable the ISR so the
 * normal RX path receives + MAC-ACKs the AssocResp as usual. Only the ACK-
 * requested DataReq poll takes this path; beacon-requests and all other TX stay
 * on the async path (discovery RX is unaffected).
 */
static int tlsr8258_tx_sync_assoc_poll(struct tlsr8258_radio_data *radio, uint8_t tx_seq)
{
	uint32_t waited_us = 0u;
	bool tx_done = false;

	/*
	 * Own the complete radio operation, including DMA2's CPU source.  The
	 * generic irq_disable() call only masked ZB_RT; an RX-DMA completion could
	 * otherwise vector in the middle of this handoff and consume the buffer
	 * while the poll still owns the RF state machine.  This is the same
	 * CPU_RX_IRQ_MASK critical section used by the current zigbee-rs PHY.
	 */
	TLSR_REG8(0x0643) = 0u;
	TLSR_REG32(0x0640) &= ~(BIT(4) | BIT(TLSR8258_IRQ_ZB_RT));
	compiler_barrier();
	tlsr8258_radio_op_prepare_tx(&radio->op, tx_seq, true, false);
	/* C's lightweight CCA does not run the Rust perform_csma_ca() teardown.
	 * Reproduce its set_trx_off() explicitly so the RX gate cannot remain
	 * active while DMA3 is being handed to the LL TX state machine. */
	TLSR_REG8(0x0f16) = 0x29u;
	TLSR_REG8(0x0428) = RF_TRX_MODE;
	TLSR_REG8(0x0f02) = RF_TRX_OFF;

	/* Match zigbee-rs send_mac_frame(): prepare RX DMA before changing the
	 * LL to TX, then clear only the TX/DMA3 latches owned by this operation. */
	radio->rx_active[0] = 0u;
	radio->rx_active[4] = 0u;
	radio->rx_proc = radio->rx_active;
	tlsr8258_rf_rx_buffer_set(radio->rx_active, TLSR8258_RX_DMA_SIZE);
	TLSR_REG8(0x0c0e) = (uint8_t)(TLSR8258_RX_DMA_SIZE >> 4);
	TLSR_REG8(0x0c0f) = 0u;
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;
	TLSR_REG8(0x0f02) = RF_TRX_OFF | BIT(4);
	/* PHY settle delay used by the updated zigbee-rs TLSR8258 path. */
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
	/* Match zigbee-rs tx_done_clear(): clear RF TX and DMA3 completion before
	 * exposing the already-armed DMA2 buffer to the RX state machine. */
	TLSR_REG16(0x0f20) = RF_IRQ_TX | RF_IRQ_TX_DS;
	TLSR_REG8(0x0c26) = DMA_CHN_RF_TX;

	/* Re-arm DMA2 after TX-done, even though it was prepared before TX. On
	 * TLSR8258 the coordinator's MAC ACK can complete DMA2 during the TX/RX
	 * handoff and leave the channel's completion latch set; merely switching
	 * the LL back to RX then misses the indirect AssocResp a few milliseconds
	 * later. Re-point/re-arm first, then use the reset-free RX mode switch.
	 * Avoid RF_TRX_OFF here: it reopens the deaf window after an idle poll. */
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
	/* The sync poll owns the TX->RX handoff; restore the CPU RF source
	 * explicitly before the coordinator's indirect AssocResp arrives. */
	tlsr8258_rf_irq_reenable();
	return tlsr8258_radio_op_result_errno(&radio->op);
}

static int tlsr8258_tx(const struct device *dev, enum ieee802154_tx_mode mode,
		       struct net_pkt *pkt, struct net_buf *frag)
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
	 * The ACK-requested DataReq association poll takes the deterministic
	 * vendor-model synchronous turnaround so the coordinator's indirect
	 * ASSOCIATION-RESPONSE lands in a reliably-armed RX window. Beacon
	 * requests (post-tx-followup but not ACK-requested) and all other frames
	 * keep the async path below.
	 */
	if (expect_post_tx_followup && expect_ack) {
		return tlsr8258_tx_sync_assoc_poll(radio, tx_seq);
	}

	/*
	 * A software MAC-ACK is kicked from the RX ISR and owns the RF TX state
	 * until its TX-done interrupt returns the chip to RX.  Do not reset the
	 * shared radio-op for a stack TX in that interval: doing so lets the ACK's
	 * completion wake the new operation before its own DMA transfer, which
	 * presents as a successful API call with no frame on air.
	 */
	for (uint32_t wait_us = 0u;
	     tlsr8258_ack_tx_pending_get(radio) && wait_us < 2000u;
	     wait_us += 50u) {
		k_busy_wait(50u);
	}
	if (tlsr8258_ack_tx_pending_get(radio)) {
		/* Lost ACK-TX completion: restore the always-RX invariant, then let the
		 * upper MAC retry this stack frame instead of corrupting the handoff. */
		tlsr8258_rf_rearm_idle_rx(radio);
		radio->op.ack_tx_pending = false;
		return -EAGAIN;
	}
	/*
	 * Under the Zigbee async RX sink, keep tx() short and let the upper
	 * layer consume Data Request follow-up traffic asynchronously.
	 */
	expect_post_tx_rx = (tlsr8258_zigbee_rx_sink == NULL) && expect_post_tx_followup;
	wait_budget_us = expect_post_tx_rx ? 150000u : CONFIG_IEEE802154_TLSR8258_TX_WAIT_US;
	k_timeout_t wait_timeout = K_USEC(wait_budget_us);
	saved_irq_mask = TLSR_REG16(0x0f1c);
	session_irq_mask =
		tlsr8258_tx_irq_session_mask(saved_irq_mask, expect_post_tx_followup);

	irq_disable(TLSR8258_IRQ_ZB_RT);
	k_sem_reset(&radio->tx_wait);
	tlsr8258_radio_op_prepare_tx(&radio->op, tx_seq, expect_ack, expect_post_tx_rx);
	tlsr8258_rf_prepare_normal_tx();
	tlsr8258_rf_set_txmode(radio);
	/* Rust keeps the TX DMA state stable for ~250 us after the mode switch
	 * before asserting the DMA-ready bit. */
	k_busy_wait(250);
	if (session_irq_mask != saved_irq_mask) {
		TLSR_REG16(0x0f1c) = 0u;
		TLSR_REG16(0x0f1c) = session_irq_mask;
	}
	if (tlsr8258_tx_force_manual_off_before_start(expect_post_tx_followup)) {
		TLSR_REG8(0x0f00) = 0x80u;
	}
	TLSR_REG16(0x0f20) = tlsr8258_tx_irq_start_clear_mask(expect_post_tx_followup);
	tlsr8258_tx_diag_put(radio, (0x10u << 24) | ((uint32_t)tx_seq << 16) | (uint32_t)mode);
	tlsr8258_rf_tx_pkt(radio->tx_buffer);
	/* irq_enable() updates only REG_IRQ_MASK; restore the global gate too. */
	tlsr8258_rf_irq_reenable();

	ret = k_sem_take(&radio->tx_wait, wait_timeout);
	if (session_irq_mask != saved_irq_mask) {
		TLSR_REG16(0x0f1c) = 0u;
		TLSR_REG16(0x0f1c) = saved_irq_mask;
	}
	if (ret == -EAGAIN) {
		uint16_t pending_irq = TLSR_REG16(0x0f20);

		/*
		 * On this silicon, TX completions for DataReq (and other short
		 * ACK-only round-trips) regularly miss the RF ISR — neither
		 * RF_IRQ_TX/TX_DS nor RF_IRQ_STX_TIMEOUT/FSM_TIMEOUT fire while
		 * the ISR is running, but the chip's IRQ status register at
		 * 0x0f20 holds the bit by the time tx() polls after the
		 * k_sem_take timeout.  Snapshot 0x0f20 BEFORE clearing it: if
		 * TX_DS or TX is asserted, the transmission did complete and
		 * the upper layer should not treat this as a failure.  Without
		 * this fallback every DataReq poll returns -EAGAIN even though
		 * the sniffer captures the frame and the coord's response.
		 *
		 * If the first read doesn't show the bit, give the chip a brief
		 * additional polling window (up to ~5 ms in 200 us steps) before
		 * triggering the destructive RF reset. Under channel contention
		 * the bit can latch slightly after our 10 ms semaphore expiry;
		 * catching those late assertions here avoids the rf_off bounce
		 * that itself costs further latency on the next TX.
		 */
		if ((pending_irq & (RF_IRQ_TX | RF_IRQ_TX_DS | RF_IRQ_STX_TIMEOUT |
				    RF_IRQ_FSM_TIMEOUT)) == 0u) {
			uint32_t extra_us;

			for (extra_us = 0u; extra_us < 5000u; extra_us += 200u) {
				k_busy_wait(200);
				pending_irq = TLSR_REG16(0x0f20);
				if ((pending_irq & (RF_IRQ_TX | RF_IRQ_TX_DS |
						    RF_IRQ_STX_TIMEOUT |
						    RF_IRQ_FSM_TIMEOUT)) != 0u) {
					break;
				}
			}
		}

		if ((pending_irq & (RF_IRQ_TX | RF_IRQ_TX_DS)) != 0u) {
			tlsr8258_tx_diag_put(radio, (0x16u << 24) |
						 ((uint32_t)tx_seq << 16) |
						 pending_irq);
			if (radio->op.expect_post_tx_rx) {
				tlsr8258_radio_op_on_timeout(&radio->op);
			} else {
				(void)tlsr8258_radio_op_on_tx_success(&radio->op);
			}
			/*
			 * This poll completed OUTSIDE the RF ISR (TX_DS masked for
			 * the session), so the ISR double-buffer swap never ran. If
			 * this was an ACK-requested poll expecting an indirect
			 * follow-up, the HW auto-received the coordinator's ACK into
			 * rx_active, leaving it occupied; re-arm a clean RX DMA
			 * buffer here so the ASSOCIATION-RESPONSE can land and raise
			 * an RX IRQ. Without this the join stalls with no post-poll
			 * raw RX of any class (see ieee802154_tlsr8258_tx_irq.c).
			 */
			if (tlsr8258_tx_poll_needs_rx_rearm(expect_post_tx_followup,
							    false)) {
				radio->rx_active[0] = 0u;
				radio->rx_active[4] = 0u;
				tlsr8258_rf_rx_buffer_set(radio->rx_active,
							  TLSR8258_RX_DMA_SIZE);
			}
			/* Reference order: DMA first, then reset-free RX mode. */
			tlsr8258_rf_set_rxmode_vendor();
			TLSR_REG16(0x0f20) = RF_IRQ_ALL;
			return tlsr8258_radio_op_result_errno(&radio->op);
		}

		if ((pending_irq & (RF_IRQ_STX_TIMEOUT | RF_IRQ_FSM_TIMEOUT)) != 0u) {
			/*
			 * Chip reported a hardware TX timeout explicitly. This
			 * is a soft failure — the RF state machine is still
			 * coherent — so skip the rf_off bounce and just restore
			 * RX mode. Upper layer gets -EIO to retry.
			 */
			tlsr8258_tx_diag_put(radio, (0x17u << 24) |
						 ((uint32_t)tx_seq << 16) |
						 pending_irq);
			tlsr8258_radio_op_on_tx_error(&radio->op, -EIO);
			tlsr8258_rf_rearm_idle_rx(radio);
			return tlsr8258_radio_op_result_errno(&radio->op);
		}

		tlsr8258_tx_diag_put(radio, (0x15u << 24) | ((uint32_t)tx_seq << 16) |
					 wait_budget_us);
		tlsr8258_radio_op_on_timeout(&radio->op);

		/*
		 * Aggressive RF recovery: on this silicon, a tx_wait timeout
		 * with neither the TX nor TIMEOUT IRQ bits set in 0x0f20 means
		 * the RF state machine is wedged in an intermediate state.
		 * Just calling tlsr8258_rf_set_rxmode() leaves the chip with
		 * lingering TX-side configuration that prevents subsequent TXes
		 * from ever generating a TX-done event (observed: 5 of 46 TXes
		 * succeed, the rest pile up timeouts and eventually the chip
		 * stops responding to SWS entirely).  Bounce the RF off then
		 * back into RX mode to force the state machine to a clean RX
		 * idle from any stuck TX-side state.
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

/* Set true once tlsr8258_init() has run rx_queue_init; gates the zb_thread
 * RX-poll drain so it never touches an uninitialized k_fifo. */
static bool tlsr8258_hw_inited;

/*
 * Override the weak zb_main.c hook. The ZB thread busy-loop calls this every
 * tick; it is the sole RX consumer (there is no separate COOP RX worker
 * thread — see tlsr8258_init). Draining from the always-runnable ZB thread makes RX delivery
 * independent of cooperative-thread scheduling.
 */
void zb_platform_radio_rx_poll(void)
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
	/*
	 * Guard against double-invocation.  The Zephyr kernel calls this via
	 * do_device_init() at POST_KERNEL level, but on TLSR8258 the .data
	 * section copy from flash LMA > boot-mirror window may fail, leaving
	 * device_state.init_res stale.  zb_radio_port_tlsr8258_get() works
	 * around that by calling device_init() as a fallback; the flag below
	 * prevents the k_thread_create path from running twice.
	 */
	if (tlsr8258_hw_inited) {
		return 0;
	}

	memset(radio, 0, sizeof(*radio));
#if defined(CONFIG_IEEE802154_TLSR8258_RETAINED_DEBUG)
	radio->debug = &tlsr8258_radio_debug_state;
#endif
	tlsr8258_rf_debug_reset(radio);
	memcpy(radio->filter_pan_id, tlsr8258_filter_pan_id_shadow,
	       TLSR8258_PAN_ID_SIZE);
	memcpy(radio->filter_short_addr, tlsr8258_filter_short_addr_shadow,
	       TLSR8258_SHORT_ADDR_SIZE);
	memcpy(radio->filter_ieee_addr, tlsr8258_filter_ieee_addr_shadow,
	       TLSR8258_IEEE_ADDR_SIZE);
	tlsr8258_radio_current_channel_set(radio, tlsr8258_channel_shadow);

	tlsr8258_rx_queue_init(&radio->rx_queue, radio->rx_slots, TLSR8258_RX_SLOT_COUNT);
	k_sem_init(&radio->tx_wait, 0, 1);

	/*
	 * RX is drained from the ZB thread via zb_platform_radio_rx_poll (single
	 * consumer, no zb_buf race, no cooperative-starvation) — there is no
	 * separate RX worker thread.
	 */
	config->irq_config_func(dev);
	tlsr8258_hw_inited = true;

	return 0;
}

#if defined(CONFIG_IEEE802154_RAW_MODE)
DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0, &tlsr8258_radio_config_0,
		      POST_KERNEL, CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
		      &tlsr8258_radio_api);
#elif defined(CONFIG_NET_L2_IEEE802154)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0,
			  &tlsr8258_radio_config_0,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
			  &tlsr8258_radio_api, IEEE802154_L2,
			  NET_L2_GET_CTX_TYPE(IEEE802154_L2),
			  TLSR8258_PHY_MAX_PSDU - TLSR8258_FCS_LENGTH);
#elif defined(CONFIG_NET_L2_OPENTHREAD)
NET_DEVICE_DT_INST_DEFINE(0, tlsr8258_init, NULL, &tlsr8258_radio_data_0,
			  &tlsr8258_radio_config_0,
			  CONFIG_IEEE802154_TLSR8258_INIT_PRIO,
			  &tlsr8258_radio_api, OPENTHREAD_L2,
			  NET_L2_GET_CTX_TYPE(OPENTHREAD_L2), 1280);
#endif

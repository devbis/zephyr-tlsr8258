/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zigbee radio abstraction for Zephyr.
 * Phase 1: stub macros that satisfy the compiler.
 * Phase 2: real implementation wired to ieee802154_tlsr8258 driver.
 *
 * Mirrors the MCU_CORE_8258 section of tl_zigbee_sdk/proj/drivers/drv_radio.h.
 */
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/zigbee/zb_types.h>

/* RF trx mode constants (match SDK rf.h RF_TRX_MODE enum) */
#define RF_MODE_TX     0
#define RF_MODE_RX     1
#define RF_MODE_AUTO   2
#define RF_MODE_OFF    3

/* TX/RX packet buffer layout (5-byte DMA header for TLSR8258) */
#define ZB_RADIO_TX_HDR_LEN    5
#define ZB_RADIO_RX_HDR_LEN    5

/* TX wait (microseconds) — matches SDK ZB_TX_WAIT_US */
#define ZB_TX_WAIT_US          120

/* Default TX power index */
#define ZB_RADIO_TX_0DBM       23
#define ZB_DEFAULT_TX_POWER_IDX 23

/* Channel conversion: logical (11-26) → physical offset (5 MHz steps) */
#define LOGICCHANNEL_TO_PHYSICAL(p)   (((p) - 10) * 5)

/* sys timer clock rate (24 MHz / 1 µs) */
#define S_TIMER_CLOCK_1US     24

/* clock_time() — returns current system tick (matches SDK usage pattern) */
static inline u32 clock_time(void)
{
	return (u32)k_cycle_get_32();
}

static inline bool clock_time_exceed(u32 ref, u32 span_us)
{
	return (u32)(clock_time() - ref) >= (span_us * S_TIMER_CLOCK_1US);
}

/* ─── Adapter contract (Phase 2) ─────────────────────────────────── */
void zb_radio_init(void);
void zb_radio_reset(void);
void zb_radio_trx_switch(u8 mode, u8 phy_chn);
void zb_radio_trx_off_auto_mode(void);
void zb_radio_tx_power_set(u8 level);
s8 zb_radio_rssi_get(void);
void zb_radio_tx_start(u8 *tx_buf);
u8 zb_radio_tx_done_get(void);
void zb_radio_tx_done_clear(void);
u8 zb_radio_rx_done_get(void);
void zb_radio_rx_done_clear(void);
u8 zb_radio_trx_state_get(void);
void zb_radio_rx_buf_set(u8 *addr);
u8 *zb_radio_next_rx_buf_get(void);
u8 zb_radio_pkt_rssi_get(const u8 *p);

#define ZB_RADIO_INIT()                       zb_radio_init()
#define ZB_RADIO_RESET()                      zb_radio_reset()
#define ZB_RADIO_TRX_SWITCH(mode, chn)        zb_radio_trx_switch((mode), (chn))
#define ZB_RADIO_TRX_OFF_AUTO_MODE()          zb_radio_trx_off_auto_mode()
#define ZB_RADIO_TX_POWER_SET(level)          zb_radio_tx_power_set(level)
#define ZB_RADIO_RSSI_GET()                   zb_radio_rssi_get()
#define ZB_RADIO_TX_START(txBuf)              zb_radio_tx_start(txBuf)
#define ZB_RADIO_TX_DONE                      zb_radio_tx_done_get()
#define ZB_RADIO_TX_DONE_CLR                  zb_radio_tx_done_clear()
#define ZB_RADIO_RX_DONE                      zb_radio_rx_done_get()
#define ZB_RADIO_RX_DONE_CLR                  zb_radio_rx_done_clear()
#define ZB_RADIO_TRX_STA_GET()                zb_radio_trx_state_get()
#define ZB_RADIO_RX_BUF_SET(addr)             zb_radio_rx_buf_set(addr)
static inline u8 *tl_getRxBuf(void) { return zb_radio_next_rx_buf_get(); }

/* Still-stubbed operations used by mac_phy.c */
#define RF_DMA_BUSY()                         (0)
#define ZB_RADIO_SRX_START(tick)              do { } while (0)
#define ZB_RADIO_MODE_MAX_GAIN()              do { } while (0)
#define ZB_RADIO_MODE_AUTO_GAIN()             do { } while (0)
#define RFDMA_TX_ENABLE                       do { } while (0)
#define RFDMA_TX_DISABLE                      do { } while (0)
#define RFDMA_RX_ENABLE                       do { } while (0)
#define RFDMA_RX_DISABLE                      do { } while (0)
#define ZB_RADIO_TX_ENABLE                    RFDMA_TX_ENABLE
#define ZB_RADIO_TX_DISABLE                   RFDMA_TX_DISABLE
#define ZB_RADIO_RX_ENABLE                    RFDMA_RX_ENABLE
#define ZB_RADIO_RX_DISABLE                   RFDMA_RX_DISABLE
#define ZB_RADIO_IRQ_MASK_CLR                 do { } while (0)
#define ZB_RADIO_IRQ_MASK_SET                 do { } while (0)
#define ZB_RADIO_RX_MAX_LEN_SET(len)          do { } while (0)
#define ZB_TIMESTAMP_ENABLE                   do { } while (0)
#define ZB_TIMER_INIT()                       do { } while (0)

#define ZB_RADIO_RX_BUF_CLEAR(p) do { \
	(p)[0] = 0; \
	(p)[4] = 0; \
} while (0)

#define ZB_RADIO_DMA_HDR_BUILD(pBuf, len) do { \
	u8 *_p = (u8 *)(pBuf); \
	_p[0] = (u8)((len) + 1); \
	_p[1] = 0; \
	_p[2] = 0; \
	_p[3] = 0; \
} while (0)

#define ZB_RADIO_TRX_CFG(size)                do { } while (0)

/* Stubs for hardware functions used by mac_phy.c */
#define drv_gpio_write(pin, val)  do { (void)(pin); (void)(val); } while (0)
static inline void WaitUs(u32 us) { k_busy_wait(us); }

/* Packet field accessors — stub values from fixed offsets used by TLSR8258 RF */
#define ZB_RADIO_ACTUAL_PAYLOAD_LEN(p)  ((p)[4])
#define ZB_RADIO_CRC_OK(p)              (1)
#define ZB_RADIO_PACKET_LENGTH_OK(p)    ((p)[4] >= 5 && (p)[4] <= 127)
#define ZB_RADIO_TIMESTAMP_GET(p)       (0u)
#define ZB_RADION_PKT_RSSI_GET(p)       zb_radio_pkt_rssi_get(p)

/* RSSI → LQI conversion (same formula as SDK for 8258) */
#define ZB_RADIO_RSSI_TO_LQI(mode, rssi, lqi) do { \
	(void)(mode); \
	s16 _r = (s16)(rssi); \
	s16 _min = -99, _max = -15; \
	if (_r > _max) { _r = _max; } \
	if (_r < _min) { _r = _min; } \
	(lqi) = (u8)(255 * (_r - _min) / (_max - _min)); \
} while (0)

#define ZB_LQI_TO_PATH_COST(lqi, path_cost) do { \
	if ((lqi) > 118)      { (path_cost) = 1; } \
	else if ((lqi) > 94)  { (path_cost) = 2; } \
	else if ((lqi) > 69)  { (path_cost) = 3; } \
	else if ((lqi) > 45)  { (path_cost) = 5; } \
	else                  { (path_cost) = 7; } \
} while (0)

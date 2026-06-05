/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Optional application hooks invoked by the Zigbee platform bootstrap thread.
 * Applications may override any of these functions.
 */
struct zb_platform_bdb_fixed_target {
	uint8_t channel;
	uint16_t pan_id;
	uint16_t short_addr;
	uint8_t ext_pan_id[8];
	uint8_t network_key[16];
	uint8_t tc_addr[8];
	bool tc_addr_valid;
};

struct zb_platform_bdb_join_profile {
	uint32_t channel_mask;
	uint16_t pan_id;
	uint8_t ext_pan_id[8];
	uint8_t network_key[16];
	uint8_t tc_addr[8];
	bool pan_id_valid;
	bool ext_pan_id_valid;
	bool network_key_valid;
	bool tc_addr_valid;
};

void zb_platform_app_bootstrap_ready(void);
bool zb_platform_app_enable_radio_smoke_probe(void);
bool zb_platform_app_should_start_commissioning(void);
void zb_platform_app_start_commissioning(void);
bool zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target);
bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile);
void zb_platform_app_apply_runtime_ieee_addr(void);
const uint8_t *zb_platform_runtime_ieee_addr_get(void);

/*
 * Platform-managed BDB helpers used by lightweight samples.
 * These are no-ops when BDB is not enabled.
 */
int zb_platform_bdb_init_default(void);
uint8_t zb_platform_bdb_network_steer_start(void);
bool zb_platform_bdb_service_persistent_rejoin(void);
/*
 * Abandon any in-flight persisted-state rejoin and return the BDB/NWK state
 * machine to IDLE so that fresh commissioning can proceed.  Used by the
 * bootstrap when a stale joined=1 keeps the NWK manager non-idle past its
 * budget without delivering a real join.
 */
void zb_platform_bdb_abandon_persistent_rejoin(void);
int zb_platform_restore_persistent_state(void);
int zb_platform_clear_persistent_state(void);
void zb_platform_app_network_left(void);

/*
 * Called by zb_platform_bdb_init_default() before BDB attribute
 * initialization.  Applications override this weak stub to call
 * zcl_init / af_endpointRegister / zcl_register for their endpoints.
 */
void zb_platform_app_register_endpoints(void);

/*
 * Identity strings used by the fallback Basic-cluster interview path in
 * mac_trx_compat.c.  Applications override these weak stubs so the
 * interview answers mirror the real ZCL attribute table.
 */
const char *zb_platform_app_basic_mfr_name(void);
const char *zb_platform_app_basic_model_id(void);

#define ZB_PLATFORM_RADIO_ERR_NONE          0U
#define ZB_PLATFORM_RADIO_ERR_NOT_READY     1U
#define ZB_PLATFORM_RADIO_ERR_SET_CHANNEL   2U
#define ZB_PLATFORM_RADIO_ERR_START         3U
#define ZB_PLATFORM_RADIO_ERR_STOP          4U
#define ZB_PLATFORM_RADIO_ERR_INVALID_RX    5U
#define ZB_PLATFORM_RADIO_ERR_RX_NO_BUFFER  6U
#define ZB_PLATFORM_RADIO_ERR_INVALID_TX    7U
#define ZB_PLATFORM_RADIO_ERR_TX_SUBMIT     8U

struct zb_platform_radio_diag_snapshot {
	bool ready;
	bool started;
	uint8_t channel;
	uint8_t trx_state;
	uint8_t tx_power;
	uint8_t last_rx_len;
	uint8_t last_tx_len;
	uint8_t last_error;
	int8_t last_rx_rssi_dbm;
	uint32_t tx_attempts;
	uint32_t tx_success;
	uint32_t tx_failures;
	uint32_t rx_irq_count;
	uint32_t rx_accept_count;
	uint32_t rx_drop_count;
};

int zb_platform_radio_diag_get(struct zb_platform_radio_diag_snapshot *snapshot);
int zb_platform_radio_start_on_channel(uint8_t channel);
int zb_platform_radio_stop(void);
int zb_platform_radio_send_raw_psdu(const uint8_t *psdu, uint8_t psdu_len);
int zb_platform_radio_send_beacon_request(void);

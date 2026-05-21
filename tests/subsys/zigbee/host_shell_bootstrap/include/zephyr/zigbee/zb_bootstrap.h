#ifndef ZEPHYR_ZIGBEE_ZB_BOOTSTRAP_H_
#define ZEPHYR_ZIGBEE_ZB_BOOTSTRAP_H_

#include <stdbool.h>
#include <stdint.h>

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
	uint8_t network_key[16];
	uint8_t tc_addr[8];
	bool network_key_valid;
	bool tc_addr_valid;
};

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

bool zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target);
bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile);
void zb_platform_app_bootstrap_ready(void);
bool zb_platform_app_enable_radio_smoke_probe(void);
bool zb_platform_app_should_start_commissioning(void);
void zb_platform_app_start_commissioning(void);
void zb_platform_app_network_left(void);
void zb_platform_app_bdb_commissioning_status(uint8_t status, bool joinedNetwork);

#endif

/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/zigbee/zb_bootstrap.h>

LOG_MODULE_REGISTER(main);

#if defined(CONFIG_ZIGBEE_BDB)
static bool commissioning_start_requested;
static bool bdb_runtime_ready;
static struct k_work_delayable commissioning_retry_work;
static bool commissioning_retry_work_ready;
static const uint8_t zigbee_shell_fixed_tc_addr[8] = {
	0x60, 0x2d, 0xce, 0xfe, 0xff, 0x89, 0xc0, 0x1c,
};

extern bool zb_isDeviceJoinedNwk(void);
extern uint8_t zb_setPollRate(uint32_t newRate);
extern uint32_t zb_getPollRate(void);

static void zigbee_shell_activate_poll_rate(void)
{
	uint32_t poll_rate = zb_getPollRate();

	if (poll_rate != 0U) {
		(void)zb_setPollRate(poll_rate);
	}
}

static void zigbee_shell_commissioning_retry(struct k_work *work)
{
	ARG_UNUSED(work);

	if (zb_isDeviceJoinedNwk()) {
		return;
	}

	commissioning_start_requested = false;
	zb_platform_app_start_commissioning();
}

static void zigbee_shell_commissioning_retry_schedule(void)
{
	if (!commissioning_retry_work_ready) {
		k_work_init_delayable(&commissioning_retry_work,
				      zigbee_shell_commissioning_retry);
		commissioning_retry_work_ready = true;
	}

	(void)k_work_reschedule(&commissioning_retry_work, K_SECONDS(5));
}
#else
static bool radio_validation_started;
static struct k_work_delayable radio_probe_work;

static void zigbee_shell_radio_probe(struct k_work *work)
{
	struct zb_platform_radio_diag_snapshot snapshot;
	int rc;

	ARG_UNUSED(work);

	rc = zb_platform_radio_start_on_channel(CONFIG_ZIGBEE_CHANNEL);
	if (rc < 0) {
		LOG_ERR("radio probe start failed (ch=%d rc=%d)", CONFIG_ZIGBEE_CHANNEL, rc);
		goto reschedule;
	}

	rc = zb_platform_radio_send_beacon_request();
	if (rc < 0) {
		LOG_ERR("radio probe beacon request failed (rc=%d)", rc);
	}

	if (zb_platform_radio_diag_get(&snapshot) == 0) {
		LOG_INF("radio diag: ch=%u started=%u tx=%u/%u fail=%u rx=%u drop=%u err=%u rssi=%d",
			snapshot.channel, snapshot.started, snapshot.tx_success,
			snapshot.tx_attempts, snapshot.tx_failures, snapshot.rx_accept_count,
			snapshot.rx_drop_count, snapshot.last_error,
			snapshot.last_rx_rssi_dbm);
	}

reschedule:
	(void)k_work_schedule(&radio_probe_work, K_SECONDS(1));
}
#endif

bool zb_platform_app_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	ARG_UNUSED(target);
	return false;
#else
	ARG_UNUSED(target);
	return false;
#endif
}

bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	ARG_UNUSED(profile);
	return false;
#else
	if (profile == NULL) {
		return false;
	}

	memset(profile, 0, sizeof(*profile));
	profile->channel_mask = ((uint32_t)1U << CONFIG_ZIGBEE_CHANNEL);
	memcpy(profile->tc_addr, zigbee_shell_fixed_tc_addr,
	       sizeof(zigbee_shell_fixed_tc_addr));
	profile->tc_addr_valid = true;

	return true;
#endif
}

void zb_platform_app_bootstrap_ready(void)
{
#if !defined(CONFIG_ZIGBEE_BDB)
	if (!radio_validation_started) {
		k_work_init_delayable(&radio_probe_work, zigbee_shell_radio_probe);
		radio_validation_started = true;
		(void)k_work_schedule(&radio_probe_work, K_MSEC(250));
		LOG_INF("zigbee_shell radio validation scheduled on channel %d",
			CONFIG_ZIGBEE_CHANNEL);
	}
#endif
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		int err = zb_platform_bdb_init_default();

		if (err == 0) {
			bdb_runtime_ready = true;
			if (zb_isDeviceJoinedNwk()) {
				zigbee_shell_activate_poll_rate();
			}
		} else {
			LOG_ERR("zigbee_shell BDB runtime init failed (%d)", err);
		}
	}
#endif
}

bool zb_platform_app_enable_radio_smoke_probe(void)
{
	/* Keep smoke probe as opt-in diagnostics only. */
	return false;
}

bool zb_platform_app_should_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		return false;
	}

	return true;
#else
	return false;
#endif
}

void zb_platform_app_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	uint8_t status;

	if (!bdb_runtime_ready) {
		LOG_WRN("zigbee_shell commissioning start: BDB runtime not initialized");
		return;
	}

	if (commissioning_start_requested) {
		LOG_WRN("zigbee_shell commissioning start: already requested");
		return;
	}

	status = zb_platform_bdb_network_steer_start();
	if (status == 0U) {
		commissioning_start_requested = true;
		if (commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		LOG_INF("zigbee_shell commissioning start requested (bdb status: 0x%02x)", status);
	} else {
		commissioning_start_requested = false;
		zigbee_shell_commissioning_retry_schedule();
		LOG_WRN("zigbee_shell commissioning start rejected (bdb status: 0x%02x)", status);
	}
#else
#endif
}

void zb_platform_app_network_left(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	commissioning_start_requested = false;
	if (commissioning_retry_work_ready) {
		(void)k_work_cancel_delayable(&commissioning_retry_work);
	}

	if (!bdb_runtime_ready) {
		zigbee_shell_commissioning_retry_schedule();
		return;
	}

	zb_platform_app_start_commissioning();
#endif
}

void zb_platform_app_bdb_commissioning_status(uint8_t status, bool joinedNetwork)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (joinedNetwork) {
		zigbee_shell_activate_poll_rate();
		commissioning_start_requested = true;
		if (commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		LOG_INF("zigbee_shell joined network (bdb status: 0x%02x)", status);
		return;
	}

	commissioning_start_requested = false;
	LOG_INF("zigbee_shell not joined yet (bdb status: 0x%02x), retry scheduled", status);
	zigbee_shell_commissioning_retry_schedule();
#else
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
#endif
}

int main(void)
{
	return 0;
}

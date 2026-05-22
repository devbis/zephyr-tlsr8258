/* SPDX-License-Identifier: Apache-2.0 */
/*
 * app_bdb.c — Commissioning / rejoin / poll-rate / leave policy for the
 * zigbee_shell sample.
 *
 * Contains all runtime BDB state that was previously inlined in main.c.
 * main.c delegates each zb_platform_app_* hook to the functions here.
 */

#include "app_bdb.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_config.h>

LOG_MODULE_REGISTER(app_bdb);

#if defined(CONFIG_ZIGBEE_BDB)
static bool commissioning_start_requested;
static bool bdb_runtime_ready;
static bool leave_recommission_pending;
static struct k_work_delayable commissioning_retry_work;
static bool commissioning_retry_work_ready;
static const uint8_t app_bdb_fixed_tc_addr[8] = {
	0x60, 0x2d, 0xce, 0xfe, 0xff, 0x89, 0xc0, 0x1c,
};

extern bool zb_isDeviceJoinedNwk(void);
extern uint8_t zb_setPollRate(uint32_t newRate);
extern uint32_t zb_getPollRate(void);

static void app_bdb_activate_poll_rate(void)
{
	uint32_t poll_rate = zb_getPollRate();

	if (poll_rate == 0U) {
		poll_rate = POLL_RATE;
	}

	(void)zb_setPollRate(poll_rate);
}

static void app_bdb_commissioning_retry_schedule(void);
/* forward declaration so retry handler can call it */
void app_bdb_start_commissioning(void);

static void app_bdb_commissioning_retry(struct k_work *work)
{
	ARG_UNUSED(work);

	if (leave_recommission_pending) {
		if (!bdb_runtime_ready || zb_isDeviceJoinedNwk() || commissioning_start_requested) {
			app_bdb_commissioning_retry_schedule();
			return;
		}

		leave_recommission_pending = false;
		app_bdb_start_commissioning();
		if (!commissioning_start_requested) {
			leave_recommission_pending = true;
			app_bdb_commissioning_retry_schedule();
		}
		return;
	}

	if (zb_isDeviceJoinedNwk()) {
		app_bdb_activate_poll_rate();
		commissioning_start_requested = true;
		return;
	}

	commissioning_start_requested = false;
	app_bdb_start_commissioning();
}

static void app_bdb_commissioning_retry_schedule(void)
{
	if (!commissioning_retry_work_ready) {
		k_work_init_delayable(&commissioning_retry_work,
				      app_bdb_commissioning_retry);
		commissioning_retry_work_ready = true;
	}

	(void)k_work_reschedule(&commissioning_retry_work, K_SECONDS(5));
}
#endif /* CONFIG_ZIGBEE_BDB */

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool app_bdb_get_fixed_join_target(struct zb_platform_bdb_fixed_target *target)
{
	ARG_UNUSED(target);
	return false;
}

bool app_bdb_get_join_profile(struct zb_platform_bdb_join_profile *profile)
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
	memcpy(profile->tc_addr, app_bdb_fixed_tc_addr,
	       sizeof(app_bdb_fixed_tc_addr));
	profile->tc_addr_valid = true;

	return true;
#endif
}

void app_bdb_bootstrap_ready(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		int err = zb_platform_bdb_init_default();

		if (err == 0) {
			bdb_runtime_ready = true;
			if (zb_isDeviceJoinedNwk()) {
				app_bdb_activate_poll_rate();
			}
		} else {
			LOG_ERR("zigbee_shell BDB runtime init failed (%d)", err);
		}
	}
#endif
}

bool app_bdb_should_start_commissioning(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (!bdb_runtime_ready) {
		return false;
	}
	if (commissioning_start_requested) {
		return false;
	}

	return !zb_isDeviceJoinedNwk();
#else
	return false;
#endif
}

void app_bdb_start_commissioning(void)
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

	if (zb_isDeviceJoinedNwk()) {
		commissioning_start_requested = true;
		app_bdb_activate_poll_rate();
		if (commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		return;
	}

	status = zb_platform_bdb_network_steer_start();
	if (status == 0U) {
		commissioning_start_requested = true;
		if (zb_isDeviceJoinedNwk() && commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		if (!zb_isDeviceJoinedNwk()) {
			app_bdb_commissioning_retry_schedule();
		}
		LOG_INF("zigbee_shell commissioning start requested (bdb status: 0x%02x)", status);
	} else {
		commissioning_start_requested = false;
		app_bdb_commissioning_retry_schedule();
		LOG_WRN("zigbee_shell commissioning start rejected (bdb status: 0x%02x)", status);
	}
#endif
}

void app_bdb_network_left(void)
{
#if defined(CONFIG_ZIGBEE_BDB)
	commissioning_start_requested = false;
	leave_recommission_pending = true;
	if (commissioning_retry_work_ready) {
		(void)k_work_cancel_delayable(&commissioning_retry_work);
	}

	app_bdb_commissioning_retry_schedule();
#endif
}

void app_bdb_commissioning_status(uint8_t status, bool joinedNetwork)
{
#if defined(CONFIG_ZIGBEE_BDB)
	if (joinedNetwork) {
		app_bdb_activate_poll_rate();
		leave_recommission_pending = false;
		commissioning_start_requested = true;
		if (commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		LOG_INF("zigbee_shell joined network (bdb status: 0x%02x)", status);
		return;
	}

	if (zb_isDeviceJoinedNwk()) {
		app_bdb_activate_poll_rate();
		leave_recommission_pending = false;
		commissioning_start_requested = true;
		if (commissioning_retry_work_ready) {
			(void)k_work_cancel_delayable(&commissioning_retry_work);
		}
		LOG_INF("zigbee_shell joined network after status check (bdb status: 0x%02x)",
			status);
		return;
	}

	commissioning_start_requested = false;
	LOG_INF("zigbee_shell not joined yet (bdb status: 0x%02x), retry scheduled", status);
	app_bdb_commissioning_retry_schedule();
#else
	ARG_UNUSED(status);
	ARG_UNUSED(joinedNetwork);
#endif
}

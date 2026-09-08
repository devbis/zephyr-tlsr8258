#include <zephyr/ztest.h>
#include <zephyr/zigbee/zb_bootstrap.h>

static int bdb_init_calls;
static int bdb_start_calls;
static int bdb_init_status;
static bool joined_network;
static bool nwk_manager_idle = true;
static uint32_t poll_rate;
static int poll_rate_set_calls;

int zb_platform_bdb_init_default(void)
{
	bdb_init_calls++;
	return bdb_init_status;
}

uint8_t zb_platform_bdb_network_steer_start(void)
{
	bdb_start_calls++;
	return 0;
}

uint8_t bdb_networkSteerStart(void)
{
	bdb_start_calls++;
	return 0;
}

bool zb_isDeviceJoinedNwk(void)
{
	return joined_network;
}

bool zdo_ifZdoNwkManagerIdle(void)
{
	return nwk_manager_idle;
}

uint8_t zb_setPollRate(uint32_t newRate)
{
	poll_rate = newRate;
	poll_rate_set_calls++;
	return 0;
}

uint32_t zb_getPollRate(void)
{
	return poll_rate;
}

int zb_platform_radio_diag_get(struct zb_platform_radio_diag_snapshot *snapshot)
{
	ARG_UNUSED(snapshot);
	return 0;
}

int zb_platform_radio_start_on_channel(uint8_t channel)
{
	ARG_UNUSED(channel);
	return 0;
}

int zb_platform_radio_stop(void)
{
	return 0;
}

int zb_platform_radio_send_raw_psdu(const uint8_t *psdu, uint8_t psdu_len)
{
	ARG_UNUSED(psdu);
	ARG_UNUSED(psdu_len);
	return 0;
}

int zb_platform_radio_send_beacon_request(void)
{
	return 0;
}

static void reset_state(void)
{
	bdb_init_calls = 0;
	bdb_start_calls = 0;
	bdb_init_status = 0;
	joined_network = false;
	nwk_manager_idle = true;
	poll_rate = 0;
	poll_rate_set_calls = 0;
}

ZTEST(zigbee_shell_bootstrap, test_bootstrap_initializes_bdb_runtime)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zassert_equal(bdb_init_calls, 1);
}

ZTEST(zigbee_shell_bootstrap, test_commissioning_start_requires_bdb_init_success)
{
	reset_state();
	bdb_init_status = -1;
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	zassert_equal(bdb_start_calls, 0);
}

ZTEST(zigbee_shell_bootstrap, test_commissioning_start_runs_after_bdb_init)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	zassert_equal(bdb_start_calls, 1);
}

ZTEST(zigbee_shell_bootstrap, test_commissioning_should_not_restart_while_request_in_flight)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zassert_true(zb_platform_app_should_start_commissioning());
	zb_platform_app_start_commissioning();
	zassert_false(zb_platform_app_should_start_commissioning());
}

ZTEST(zigbee_shell_bootstrap, test_commissioning_start_skips_network_steer_when_already_joined)
{
	reset_state();
	joined_network = true;
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	zassert_equal(bdb_start_calls, 0);
	zassert_true(poll_rate_set_calls >= 1);
}

ZTEST(zigbee_shell_bootstrap, test_commissioning_status_false_uses_runtime_join_state)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	joined_network = true;
	zb_platform_app_bdb_commissioning_status(0x23, false);
	zassert_false(zb_platform_app_should_start_commissioning());
	zassert_equal(poll_rate_set_calls, 1);
}

ZTEST(zigbee_shell_bootstrap, test_radio_diag_api_header_contract)
{
	struct zb_platform_radio_diag_snapshot snapshot = {0};

	zassert_equal(ZB_PLATFORM_RADIO_ERR_NONE, 0);
	zassert_equal(ZB_PLATFORM_RADIO_ERR_NOT_READY, 1);
	zassert_equal(snapshot.channel, 0);
	zassert_false(snapshot.ready);
	zassert_false(snapshot.started);
}

ZTEST(zigbee_shell_bootstrap, test_radio_smoke_probe_hook_stays_opt_in)
{
	zassert_false(zb_platform_app_enable_radio_smoke_probe());
}

ZTEST_SUITE(zigbee_shell_bootstrap, NULL, NULL, NULL, NULL, NULL);

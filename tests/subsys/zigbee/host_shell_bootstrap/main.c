#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/zigbee/zb_bootstrap.h>

static int failures;
static int bdb_init_calls;
static int bdb_start_calls;
static int bdb_init_status;
static bool joined_network;
static uint32_t poll_rate;
static int poll_rate_set_calls;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		printf("FAIL %s:%d expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d %s=%lld expected %lld\n", __FILE__, __LINE__, \
		       #actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

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

bool zb_isDeviceJoinedNwk(void)
{
	return joined_network;
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
	(void)snapshot;
	return 0;
}

int zb_platform_radio_start_on_channel(uint8_t channel)
{
	(void)channel;
	return 0;
}

int zb_platform_radio_stop(void)
{
	return 0;
}

int zb_platform_radio_send_raw_psdu(const uint8_t *psdu, uint8_t psdu_len)
{
	(void)psdu;
	(void)psdu_len;
	return 0;
}

int zb_platform_radio_send_beacon_request(void)
{
	return 0;
}

#define CONFIG_ZIGBEE_BDB 1
/* app_bdb.c owns the commissioning state; include it directly so the
 * reset_state() helper can access its static variables. */
#include "../../../../samples/zigbee/zigbee_shell/src/app_bdb.c"
#define main zigbee_shell_sample_main
#include "../../../../samples/zigbee/zigbee_shell/src/main.c"
#undef main

static void reset_state(void)
{
	bdb_init_calls = 0;
	bdb_start_calls = 0;
	bdb_init_status = 0;
	joined_network = false;
	poll_rate = 0;
	poll_rate_set_calls = 0;

	commissioning_start_requested = false;
	bdb_runtime_ready = false;
	commissioning_retry_work_ready = false;
}

static void test_commissioning_should_not_restart_while_request_in_flight(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	EXPECT_TRUE(zb_platform_app_should_start_commissioning());
	zb_platform_app_start_commissioning();
	EXPECT_FALSE(zb_platform_app_should_start_commissioning());
}

static void test_commissioning_start_skips_network_steer_when_already_joined(void)
{
	reset_state();
	joined_network = true;
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	EXPECT_EQ(bdb_start_calls, 0);
	EXPECT_TRUE(poll_rate_set_calls >= 1);
}

static void test_commissioning_status_false_uses_runtime_join_state(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	joined_network = true;
	zb_platform_app_bdb_commissioning_status(0x23, false);
	EXPECT_FALSE(zb_platform_app_should_start_commissioning());
	EXPECT_EQ(poll_rate_set_calls, 1);
}

static void test_network_left_defers_recommission_until_runtime_resets(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;

	zb_platform_app_network_left();

	EXPECT_EQ(bdb_start_calls, 0);
	EXPECT_FALSE(commissioning_start_requested);
	EXPECT_TRUE(commissioning_retry_work_ready);
	EXPECT_TRUE(commissioning_retry_work.scheduled);
}

static void test_leave_retry_while_still_joined_keeps_recommission_pending(void)
{
	struct k_work work = {0};

	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;
	zb_platform_app_network_left();

	commissioning_retry_work.handler(&work);

	EXPECT_EQ(bdb_start_calls, 0);
	EXPECT_FALSE(commissioning_start_requested);
	EXPECT_TRUE(commissioning_retry_work.scheduled);
}

static void test_leave_retry_starts_after_runtime_is_not_joined(void)
{
	struct k_work work = {0};

	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;
	zb_platform_app_network_left();
	joined_network = false;

	commissioning_retry_work.handler(&work);

	EXPECT_EQ(bdb_start_calls, 1);
	EXPECT_TRUE(commissioning_start_requested);
}

int main(void)
{
	test_commissioning_should_not_restart_while_request_in_flight();
	test_commissioning_start_skips_network_steer_when_already_joined();
	test_commissioning_status_false_uses_runtime_join_state();
	test_network_left_defers_recommission_until_runtime_resets();
	test_leave_retry_while_still_joined_keeps_recommission_pending();
	test_leave_retry_starts_after_runtime_is_not_joined();

	if (failures != 0) {
		printf("host_shell_bootstrap: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_shell_bootstrap: PASS\n");
	return 0;
}

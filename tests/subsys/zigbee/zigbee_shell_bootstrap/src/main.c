#include <zephyr/ztest.h>
#include <zephyr/zigbee/zb_bootstrap.h>

static int bdb_init_calls;
static int bdb_start_calls;
static int bdb_init_status;

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

static void reset_state(void)
{
	bdb_init_calls = 0;
	bdb_start_calls = 0;
	bdb_init_status = 0;
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

ZTEST_SUITE(zigbee_shell_bootstrap, NULL, NULL, NULL, NULL, NULL);

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/zigbee/zb_bootstrap.h>

static int failures;
static int bdb_init_calls;
static int bdb_init_status;
static bool joined_network;
static bool nwk_manager_idle;
static uint32_t poll_rate;
static int poll_rate_set_calls;
static int router_start_calls;
static int network_steer_calls;
static int ev_timer_task_post_calls;
static int ev_timer_task_cancel_calls;

struct ev_timer_event_t {
	int (*cb)(void *data);
	void *data;
	uint32_t timeout_ms;
	bool scheduled;
};

static struct ev_timer_event_t ev_timer_task_stub;

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
	network_steer_calls++;
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

uint8_t zb_routerStart(void)
{
	router_start_calls++;
	return 0;
}

struct ev_timer_event_t *ev_timer_taskPost(int (*func)(void *data), void *arg, uint32_t t_ms)
{
	ev_timer_task_post_calls++;
	ev_timer_task_stub.cb = func;
	ev_timer_task_stub.data = arg;
	ev_timer_task_stub.timeout_ms = t_ms;
	ev_timer_task_stub.scheduled = true;
	return &ev_timer_task_stub;
}

uint8_t ev_timer_taskCancel(struct ev_timer_event_t **evt)
{
	ev_timer_task_cancel_calls++;
	if (evt != NULL && *evt != NULL) {
		(*evt)->scheduled = false;
		*evt = NULL;
		return 0;
	}

	return 1;
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

#include "../host_shell_bootstrap/include/zdo_zcl_stubs.h"

#define CONFIG_ZIGBEE_BDB 1
#define CONFIG_ZIGBEE_ROUTER 1
#include "../../../../samples/zigbee/zigbee_shell/src/app_bdb.c"
#define main zigbee_shell_sample_main
#include "../../../../samples/zigbee/zigbee_shell/src/main.c"
#undef main

zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq,
				u8 *seqNo, zdo_callback indCb)
{
	(void)dstNwkAddr;
	(void)pReq;
	(void)seqNo;
	(void)indCb;
	return 0;
}

zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq,
				  u8 *seqNo, zdo_callback indCb)
{
	(void)dstNwkAddr;
	(void)pReq;
	(void)seqNo;
	(void)indCb;
	return 0;
}

status_t zcl_read(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 manuCode,
		  u8 disableDefaultRsp, u8 direction, u8 seqNo,
		  zclReadCmd_t *readCmd)
{
	(void)srcEp;
	(void)pDstEpInfo;
	(void)clusterId;
	(void)manuCode;
	(void)disableDefaultRsp;
	(void)direction;
	(void)seqNo;
	(void)readCmd;
	return 0;
}

static void reset_state(void)
{
	bdb_init_calls = 0;
	bdb_init_status = 0;
	joined_network = false;
	nwk_manager_idle = true;
	poll_rate = 0;
	poll_rate_set_calls = 0;
	router_start_calls = 0;
	network_steer_calls = 0;
	ev_timer_task_post_calls = 0;
	ev_timer_task_cancel_calls = 0;
	ev_timer_task_stub.cb = NULL;
	ev_timer_task_stub.data = NULL;
	ev_timer_task_stub.timeout_ms = 0;
	ev_timer_task_stub.scheduled = false;

	commissioning_start_requested = false;
	bdb_runtime_ready = false;
	leave_recommission_pending = false;
	commissioning_retry_timer = NULL;
}

static void test_router_join_target_publishes_static_formation(void)
{
	struct zb_platform_bdb_fixed_target target = {0};

	reset_state();

	/* Router reuses the fixed-target hook to publish its static
	 * formation parameters (channel, PAN, ext PAN). short_addr is
	 * 0x0000 for the standalone router; tc_addr is left invalid
	 * because the router is the trust center in distributed mode.
	 */
	EXPECT_TRUE(app_bdb_get_fixed_join_target(&target));
	EXPECT_EQ(target.short_addr, 0x0000U);
	EXPECT_FALSE(target.tc_addr_valid);
}

static void test_router_commissioning_uses_router_start(void)
{
	reset_state();
	app_bdb_bootstrap_ready();
	EXPECT_TRUE(app_bdb_should_start_commissioning());

	app_bdb_start_commissioning();

	EXPECT_EQ(router_start_calls, 1);
	EXPECT_EQ(network_steer_calls, 0);
	EXPECT_EQ(ev_timer_task_post_calls, 0);
	EXPECT_EQ(poll_rate_set_calls, 0);
}

int main(void)
{
	test_router_join_target_publishes_static_formation();
	test_router_commissioning_uses_router_start();

	if (failures != 0) {
		printf("host_shell_router_bootstrap: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_shell_router_bootstrap: PASS\n");
	return 0;
}

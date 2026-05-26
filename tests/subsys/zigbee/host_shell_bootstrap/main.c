#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/zigbee/zb_bootstrap.h>

static int failures;
static int bdb_init_calls;
static int bdb_start_calls;
static int bdb_init_status;
static bool joined_network;
static bool nwk_manager_idle;
static uint32_t poll_rate;
static int poll_rate_set_calls;
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

static void fire_retry_timer(void)
{
	if (ev_timer_task_stub.cb != NULL) {
		ev_timer_task_stub.scheduled = false;
		(void)ev_timer_task_stub.cb(ev_timer_task_stub.data);
	}
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

/* ------------------------------------------------------------------ */
/* Minimal ZDO / ZCL stubs for the post-join interview seam            */
/*                                                                     */
/* Included BEFORE app_bdb.c so that the types and forward             */
/* declarations are visible at every call site inside that file.       */
/* ------------------------------------------------------------------ */
#include "zdo_zcl_stubs.h"

#define CONFIG_ZIGBEE_BDB 1
/* app_bdb.c owns the commissioning state; include it directly so the
 * reset_state() helper can access its static variables. */
#include "../../../../samples/zigbee/zigbee_shell/src/app_bdb.c"
#define main zigbee_shell_sample_main
#include "../../../../samples/zigbee/zigbee_shell/src/main.c"
#undef main

static int active_ep_req_calls;
static u16 active_ep_req_dst;
static int simple_desc_req_calls;
static u16 simple_desc_req_dst;
static u8  simple_desc_req_ep;
static int zcl_basic_read_calls;
static u16 zcl_basic_read_cluster;
static u16 zcl_basic_read_dst;

zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq,
				u8 *seqNo, zdo_callback indCb)
{
	active_ep_req_calls++;
	active_ep_req_dst = dstNwkAddr;
	(void)pReq; (void)seqNo; (void)indCb;
	return 0;
}

zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq,
				  u8 *seqNo, zdo_callback indCb)
{
	simple_desc_req_calls++;
	simple_desc_req_dst = dstNwkAddr;
	if (pReq != NULL) {
		simple_desc_req_ep = pReq->endpoint;
	}
	(void)seqNo; (void)indCb;
	return 0;
}

status_t zcl_read(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 manuCode,
		  u8 disableDefaultRsp, u8 direction, u8 seqNo,
		  zclReadCmd_t *readCmd)
{
	zcl_basic_read_calls++;
	zcl_basic_read_cluster = clusterId;
	if (pDstEpInfo != NULL) {
		zcl_basic_read_dst = pDstEpInfo->dstAddr.shortAddr;
	}
	(void)srcEp; (void)manuCode; (void)disableDefaultRsp;
	(void)direction; (void)seqNo; (void)readCmd;
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

	active_ep_req_calls = 0;
	active_ep_req_dst = 0xFFFFU;
	simple_desc_req_calls = 0;
	simple_desc_req_dst = 0xFFFFU;
	simple_desc_req_ep = 0xFFU;
	zcl_basic_read_calls = 0;
	zcl_basic_read_cluster = 0xFFFFU;
	zcl_basic_read_dst = 0xFFFFU;
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

static void test_commissioning_start_schedules_vendor_retry_timer(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();

	zb_platform_app_start_commissioning();

	EXPECT_EQ(ev_timer_task_post_calls, 1);
	EXPECT_EQ(ev_timer_task_stub.timeout_ms, 5000);
	EXPECT_TRUE(ev_timer_task_stub.scheduled);
}

static void test_retry_does_not_restart_commissioning_while_nwk_manager_busy(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();
	nwk_manager_idle = false;

	fire_retry_timer();

	EXPECT_EQ(bdb_start_calls, 1);
	EXPECT_TRUE(commissioning_start_requested);
	EXPECT_EQ(ev_timer_task_post_calls, 2);
	EXPECT_TRUE(ev_timer_task_stub.scheduled);
}

static void test_network_left_defers_recommission_until_runtime_resets(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;

	zb_platform_app_network_left();

	EXPECT_EQ(bdb_start_calls, 0);
	EXPECT_FALSE(commissioning_start_requested);
	EXPECT_EQ(ev_timer_task_post_calls, 1);
	EXPECT_TRUE(ev_timer_task_stub.scheduled);
}

static void test_leave_retry_while_still_joined_keeps_recommission_pending(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;
	zb_platform_app_network_left();

	fire_retry_timer();

	EXPECT_EQ(bdb_start_calls, 0);
	EXPECT_FALSE(commissioning_start_requested);
	EXPECT_EQ(ev_timer_task_post_calls, 2);
	EXPECT_TRUE(ev_timer_task_stub.scheduled);
}

static void test_leave_retry_starts_after_runtime_is_not_joined(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	joined_network = true;
	zb_platform_app_network_left();
	joined_network = false;

	fire_retry_timer();

	EXPECT_EQ(bdb_start_calls, 1);
	EXPECT_TRUE(commissioning_start_requested);
}

/*
 * RED: after a successful join, the bootstrap layer MUST initiate the
 * post-association ZDO/ZCL interview against the coordinator (0x0000):
 *   1. Active-EP request  — discover which endpoints the coordinator has
 *   2. Simple-Descriptor request — fetch the profile/device-ID for ep 0x01
 *   3. Basic-cluster read — read Manufacturer Name + Model ID (cluster 0x0000)
 *
 * This test is intentionally RED until app_bdb_commissioning_status()
 * (or a helper it calls) is wired to invoke these APIs.
 */
static void test_post_join_initiates_zdo_zcl_interview(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();

	/*
	 * BDB reports a successful join (status=0x00, joinedNetwork=true).
	 * This is the JOINING -> INTERVIEW handoff point.
	 */
	zb_platform_app_bdb_commissioning_status(0x00, true);

	/* 1. Active-EP request must be directed at coordinator short addr 0x0000. */
	EXPECT_TRUE(active_ep_req_calls >= 1);
	EXPECT_EQ(active_ep_req_dst, 0x0000);

	/*
	 * 2. Simple-Descriptor request must target coordinator 0x0000 for
	 *    the generic endpoint (APP_PROFILE_ENDPOINT = 0x01).
	 */
	EXPECT_TRUE(simple_desc_req_calls >= 1);
	EXPECT_EQ(simple_desc_req_dst, 0x0000);
	EXPECT_EQ(simple_desc_req_ep, 0x01);

	/* 3. Basic-cluster attribute read (cluster ID 0x0000) must be issued. */
	EXPECT_TRUE(zcl_basic_read_calls >= 1);
	EXPECT_EQ(zcl_basic_read_cluster, 0x0000);
	EXPECT_EQ(zcl_basic_read_dst, 0x0000);
}

static void test_commissioning_join_success_cancels_pending_retry_timer(void)
{
	reset_state();
	zb_platform_app_bootstrap_ready();
	zb_platform_app_start_commissioning();

	/* The not-yet-joined path schedules a retry timer. */
	EXPECT_EQ(ev_timer_task_post_calls, 1);
	EXPECT_TRUE(ev_timer_task_stub.scheduled);

	/* BDB reports join success with joinedNetwork=true (the interview-ready
	 * state: transport key is installed and the coordinator can now query
	 * the device). */
	zb_platform_app_bdb_commissioning_status(0x00, true);

	/* Retry timer must be cancelled, poll rate activated, and commissioning
	 * must not be eligible for restart. */
	EXPECT_EQ(ev_timer_task_cancel_calls, 1);
	EXPECT_FALSE(ev_timer_task_stub.scheduled);
	EXPECT_EQ(poll_rate_set_calls, 1);
	EXPECT_FALSE(zb_platform_app_should_start_commissioning());
}

int main(void)
{
	test_commissioning_should_not_restart_while_request_in_flight();
	test_commissioning_start_skips_network_steer_when_already_joined();
	test_commissioning_status_false_uses_runtime_join_state();
	test_commissioning_start_schedules_vendor_retry_timer();
	test_retry_does_not_restart_commissioning_while_nwk_manager_busy();
	test_network_left_defers_recommission_until_runtime_resets();
	test_leave_retry_while_still_joined_keeps_recommission_pending();
	test_leave_retry_starts_after_runtime_is_not_joined();
	test_commissioning_join_success_cancels_pending_retry_timer();
	test_post_join_initiates_zdo_zcl_interview();

	if (failures != 0) {
		printf("host_shell_bootstrap: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_shell_bootstrap: PASS\n");
	return 0;
}

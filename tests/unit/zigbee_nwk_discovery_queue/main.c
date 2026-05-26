/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zb_common_stub.h"
#include "os/ev_timer.h"

static int g_failures;
static int g_task_post_calls;
static tl_zb_callback_t g_last_task_fn;
static void *g_last_task_arg;
static int g_beacon_request_calls;
static u8 g_rf_channel = 11U;
static u8 g_dsn;

zb_info_t g_zbInfo;
nwk_ctx_t g_zbNwkCtx;
ss_ib_t ss_ib;
aps_ib_t aps_ib;
sys_diagnostics_t g_sysDiags;
zdo_appIndCb_t *zdoAppIndCbLst;
const addrExt_t g_invalid_addr = { [0 ... 7] = 0xff };
const addrExt_t g_zero_addr = { 0 };

u8 tl_zbTaskPost(tl_zb_callback_t fn, void *arg)
{
	g_task_post_calls++;
	g_last_task_fn = fn;
	g_last_task_arg = arg;
	return RET_OK;
}

void ev_on_timer(ev_timer_event_t *evt, u32 timeout)
{
	evt->timeout = timeout;
}

void ev_unon_timer(ev_timer_event_t *evt)
{
	evt->timeout = 0U;
}

void zb_info_save(void *arg)
{
	ARG_UNUSED(arg);
}

u8 tl_zbMacAttrSet(u8 attr, const void *value, u8 len)
{
	ARG_UNUSED(attr);
	ARG_UNUSED(value);
	ARG_UNUSED(len);
	return RET_OK;
}

void ss_securityModeSet(u8 mode)
{
	ARG_UNUSED(mode);
}

u8 zb_platform_radio_send_raw_psdu(const u8 *frame, u8 len)
{
	ARG_UNUSED(frame);
	ARG_UNUSED(len);
	return RET_OK;
}

int zb_platform_radio_send_beacon_request(void)
{
	g_beacon_request_calls++;
	return 0;
}

bool zb_platform_app_get_join_profile(struct zb_platform_bdb_join_profile *profile)
{
	memset(profile, 0, sizeof(*profile));
	profile->channel_mask = BIT(11);
	return true;
}

bool zb_nwk_schedule_task_or_timer(tl_zb_callback_t task_cb, ev_timer_event_t *timer_evt,
				   ev_timer_callback_t timer_cb, void *arg, u32 delay_ms)
{
	ARG_UNUSED(task_cb);
	ARG_UNUSED(timer_evt);
	ARG_UNUSED(timer_cb);
	ARG_UNUSED(arg);
	ARG_UNUSED(delay_ms);
	return false;
}

u8 rf_getChannel(void)
{
	return g_rf_channel;
}

u8 zb_zdoSendDevAnnance(void)
{
	return ZDO_SUCCESS;
}

u32 zdo_af_get_syn_rate(void)
{
	return 0U;
}

void bdb_ed_runtime_join_complete(void)
{
}

void tl_zdoEdMinimalDiscoveryDone(u8 status)
{
	ARG_UNUSED(status);
}

void tl_zdoEdMinimalJoinDone(u8 status, bool rejoinMode)
{
	ARG_UNUSED(status);
	ARG_UNUSED(rejoinMode);
}

u8 zb_minimal_ccm_encrypt_auth(const u8 *key, const u8 *nonce, u8 mic_len,
				 const u8 *aad, u8 aad_len, const u8 *payload, u8 payload_len,
				 u8 *out)
{
	ARG_UNUSED(key);
	ARG_UNUSED(nonce);
	ARG_UNUSED(mic_len);
	ARG_UNUSED(aad);
	ARG_UNUSED(aad_len);
	memcpy(out, payload, payload_len);
	memset(out + payload_len, 0, 4);
	return payload_len + 4U;
}

u8 nv_nwkFrameCountSaveToFlash(u32 frame_counter)
{
	ARG_UNUSED(frame_counter);
	return RET_OK;
}

u8 ZB_MAC_DSN(void)
{
	return g_dsn;
}

void ZB_INC_MAC_DSN(void)
{
	g_dsn++;
}

int zb_radio_port_set_trx_state(u8 state, u8 channel)
{
	ARG_UNUSED(state);
	g_rf_channel = channel;
	return 0;
}

int zb_radio_port_set_channel(u8 channel)
{
	g_rf_channel = channel;
	return 0;
}

void zb_radio_port_update_filters(u16 pan_id, u16 short_addr, const addrExt_t ieee)
{
	ARG_UNUSED(pan_id);
	ARG_UNUSED(short_addr);
	ARG_UNUSED(ieee);
}

#include "../../../subsys/zigbee/nwk/nwk_ed_minimal.c"

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		fprintf(stderr, "FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
			#actual, _actual, _expected); \
		g_failures++; \
	} \
} while (0)

#define EXPECT_TRUE(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_failures++; \
	} \
} while (0)

static void reset_state(void)
{
	g_failures = 0;
	g_task_post_calls = 0;
	g_last_task_fn = NULL;
	g_last_task_arg = NULL;
	g_beacon_request_calls = 0;
	g_rf_channel = 11U;
	g_dsn = 0U;
	memset(&g_zbInfo, 0, sizeof(g_zbInfo));
	memset(&g_zbNwkCtx, 0, sizeof(g_zbNwkCtx));
	memset(&ss_ib, 0, sizeof(ss_ib));
	memset(&aps_ib, 0, sizeof(aps_ib));
	memset(&g_sysDiags, 0, sizeof(g_sysDiags));
	tl_zbNwkInit(TRUE);
}

static void build_beacon_frame(u8 *buf, u8 *len)
{
	static const u8 beacon[] = {
		0x00, 0x80, 0xff, 0x27, 0x5b, 0x00, 0x00, 0xff, 0xcf, 0x00, 0x00, 0x00,
		0x22, 0x84, 0x3b, 0x09, 0x9d, 0x06, 0x4f, 0x8f, 0xee, 0x70, 0xff, 0xff,
		0xff, 0x00, 0x00, 0x00,
	};

	memcpy(buf, beacon, sizeof(beacon));
	*len = (u8)sizeof(beacon);
}

static void build_traffic_candidate_frame(u8 *buf, u8 *len)
{
	buf[0] = 0x63;
	buf[1] = 0x88;
	buf[2] = 0x01;
	buf[3] = 0x27;
	buf[4] = 0x5b;
	buf[5] = 0x00;
	buf[6] = 0x00;
	buf[7] = 0xa2;
	buf[8] = 0x4a;
	buf[9] = MAC_CMD_DATA_REQUEST;
	buf[10] = 0x00;
	buf[11] = 0x00;
	buf[12] = 0x00;
	buf[13] = 0x00;
	buf[14] = 0x00;
	*len = 15U;
}

static bool test_factory_new_discovery_ignores_traffic_candidate_frames(void)
{
	u8 frame[32];
	u8 len;

	reset_state();
	EXPECT_TRUE(tl_zbNwkEdMinimalDiscoveryStart(BIT(11), 0U));
	EXPECT_EQ(g_beacon_request_calls, 1);

	build_traffic_candidate_frame(frame, &len);
	tl_zbNwkEdMinimalMacRxIndicate(frame, len, -80);

	EXPECT_EQ(g_task_post_calls, 0);
	return g_failures == 0;
}

static bool test_factory_new_discovery_still_accepts_beacons(void)
{
	u8 frame[32];
	u8 len;

	reset_state();
	EXPECT_TRUE(tl_zbNwkEdMinimalDiscoveryStart(BIT(11), 0U));

	build_beacon_frame(frame, &len);
	tl_zbNwkEdMinimalMacRxIndicate(frame, len, -63);

	EXPECT_EQ(g_task_post_calls, 1);
	EXPECT_TRUE(g_last_task_fn == nwk_ed_minimal_rx_event_task);
	return g_failures == 0;
}

static bool test_rejoin_discovery_keeps_traffic_candidate_frames(void)
{
	u8 frame[32];
	u8 len;

	reset_state();
	EXPECT_TRUE(tl_zbNwkEdMinimalRejoinStart(BIT(11), 0U, false));

	build_traffic_candidate_frame(frame, &len);
	tl_zbNwkEdMinimalMacRxIndicate(frame, len, -80);

	EXPECT_EQ(g_task_post_calls, 1);
	EXPECT_TRUE(g_last_task_fn == nwk_ed_minimal_rx_event_task);
	return g_failures == 0;
}

int main(void)
{
	if (!test_factory_new_discovery_ignores_traffic_candidate_frames()) {
		return 1;
	}

	if (!test_factory_new_discovery_still_accepts_beacons()) {
		return 1;
	}

	if (!test_rejoin_discovery_keeps_traffic_candidate_frames()) {
		return 1;
	}

	return 0;
}

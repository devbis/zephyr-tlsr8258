/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Dimmable light: On/Off and Level Control servers with led0 as the load.
 * The LED has no brightness control, so it is lit while the light is on at a
 * level above zero.
 */

#include "app_common.h"

#include "zcl/general/zcl_group.h"
#include "zcl/general/zcl_onoff.h"
#include "zcl/general/zcl_level.h"
#include "os/ev_timer.h"

#define LEVEL_TICK_MS 100

/* Fixed-point (8.8) level arithmetic keeps slow transitions smooth. */
#define LEVEL_FP_SHIFT 8

static const u16 cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;

static bool onoff_state;
static u8 level_current = ZCL_LEVEL_ATTR_MAX_LEVEL;
static u8 level_min = ZCL_LEVEL_ATTR_MIN_LEVEL;
static u8 level_max = ZCL_LEVEL_ATTR_MAX_LEVEL;
static u16 level_remaining;
static u16 level_onoff_transition = 0U;
static u8 group_name_support;

static const zclAttrInfo_t group_attr_tbl[] = {
	{ZCL_ATTRID_GROUP_NAME_SUPPORT, ZCL_DATA_TYPE_BITMAP8, ACCESS_CONTROL_READ,
	 &group_name_support},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&cluster_rev},
};

static const zclAttrInfo_t onoff_attr_tbl[] = {
	{ZCL_ATTRID_ONOFF, ZCL_DATA_TYPE_BOOLEAN, ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE,
	 (u8 *)&onoff_state},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&cluster_rev},
};

static const zclAttrInfo_t level_attr_tbl[] = {
	{ZCL_ATTRID_LEVEL_CURRENT_LEVEL, ZCL_DATA_TYPE_UINT8,
	 ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, &level_current},
	{ZCL_ATTRID_LEVEL_REMAINING_TIME, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&level_remaining},
	{ZCL_ATTRID_LEVEL_MIN_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, &level_min},
	{ZCL_ATTRID_LEVEL_MAX_LEVEL, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, &level_max},
	{ZCL_ATTRID_LEVEL_ON_OFF_TRANSITION_TIME, ZCL_DATA_TYPE_UINT16,
	 ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8 *)&level_onoff_transition},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (u8 *)&cluster_rev},
};

static u16 in_clusters[] = {
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_IDENTIFY,
	ZCL_CLUSTER_GEN_GROUPS,
	ZCL_CLUSTER_GEN_ON_OFF,
	ZCL_CLUSTER_GEN_LEVEL_CONTROL,
};

static af_simple_descriptor_t simple_desc = {
	.app_profile_id = APP_HA_PROFILE_ID,
	.app_dev_id = 0x0101U, /* Dimmable Light */
	.endpoint = APP_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = ARRAY_SIZE(in_clusters),
	.app_in_cluster_lst = in_clusters,
	.app_out_cluster_count = 0U,
	.app_out_cluster_lst = NULL,
};

/* Level transition state, in 8.8 fixed point. */
static ev_timer_event_t *level_timer;
static int32_t level_fp;
static int32_t level_target_fp;
static int32_t level_step_fp;
static uint16_t level_ticks_left;
/* Switch the light off once a "with on/off" transition reaches level_min. */
static bool level_off_at_end;

static void light_update(void)
{
	app_led_set(onoff_state && (level_current > 0U));
}

static void onoff_set(bool on)
{
	onoff_state = on;
	zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_GEN_ON_OFF, ZCL_ATTRID_ONOFF, (u8 *)&onoff_state);
	light_update();
}

static void level_publish(void)
{
	level_current = (u8)(level_fp >> LEVEL_FP_SHIFT);
	level_remaining = level_ticks_left * (LEVEL_TICK_MS / 100);
	zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_GEN_LEVEL_CONTROL, ZCL_ATTRID_LEVEL_CURRENT_LEVEL,
		       &level_current);
	zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_GEN_LEVEL_CONTROL,
		       ZCL_ATTRID_LEVEL_REMAINING_TIME, (u8 *)&level_remaining);
	light_update();
}

static void level_finish(void)
{
	level_ticks_left = 0U;
	level_fp = level_target_fp;
	level_publish();

	if (level_off_at_end) {
		level_off_at_end = false;
		onoff_set(false);
	}
}

static int level_tick(void *arg)
{
	ARG_UNUSED(arg);

	if (level_ticks_left > 1U) {
		level_ticks_left--;
		level_fp += level_step_fp;
		level_publish();
		return LEVEL_TICK_MS;
	}

	level_timer = NULL;
	level_finish();
	return -1;
}

static void level_stop(void)
{
	if (level_timer != NULL) {
		(void)ev_timer_taskCancel(&level_timer);
		level_timer = NULL;
	}
	level_off_at_end = false;
	level_ticks_left = 0U;
	level_remaining = 0U;
}

/* Move to @target over @time_ds tenths of a second. */
static void level_go(u8 target, u32 time_ds, bool with_onoff)
{
	level_stop();

	if (target < level_min) {
		target = level_min;
	} else if (target > level_max) {
		target = level_max;
	}

	if (with_onoff) {
		if ((target > level_min) && !onoff_state) {
			/* Turning on ramps up from the minimum level. */
			level_fp = (int32_t)level_min << LEVEL_FP_SHIFT;
			level_current = level_min;
			onoff_set(true);
		}
		level_off_at_end = (target <= level_min) && onoff_state;
	}

	level_target_fp = (int32_t)target << LEVEL_FP_SHIFT;

	if ((time_ds == 0U) || (time_ds == 0xFFFFU)) {
		level_finish();
		return;
	}

	level_ticks_left = (u16)time_ds * (100U / LEVEL_TICK_MS);
	level_step_fp = (level_target_fp - level_fp) / (int32_t)level_ticks_left;
	level_timer = ev_timer_taskPost(level_tick, NULL, LEVEL_TICK_MS);
}

/* Move at @rate levels per second until a limit is reached. */
static void level_run(bool up, u8 rate, bool with_onoff)
{
	u32 span;
	u8 target = up ? level_max : level_min;

	if (rate == 0U) {
		return;
	}

	span = up ? (u32)(level_max - level_current) : (u32)(level_current - level_min);
	level_go(target, (span * 10U) / rate, with_onoff);
}

static status_t onoff_cb(zclIncomingAddrInfo_t *addr_info, u8 cmd_id, void *payload)
{
	ARG_UNUSED(addr_info);
	ARG_UNUSED(payload);

	switch (cmd_id) {
	case ZCL_CMD_ONOFF_ON:
		onoff_set(true);
		break;
	case ZCL_CMD_ONOFF_OFF:
		onoff_set(false);
		break;
	case ZCL_CMD_ONOFF_TOGGLE:
		onoff_set(!onoff_state);
		break;
	default:
		return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	return ZCL_STA_SUCCESS;
}

static status_t level_cb(zclIncomingAddrInfo_t *addr_info, u8 cmd_id, void *payload)
{
	zcl_level_cmdPayload_t *p = payload;
	bool with_onoff = cmd_id >= ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF;

	ARG_UNUSED(addr_info);

	switch (cmd_id) {
	case ZCL_CMD_LEVEL_MOVE_TO_LEVEL:
	case ZCL_CMD_LEVEL_MOVE_TO_LEVEL_WITH_ON_OFF:
		level_go(p->moveToLevel.level, p->moveToLevel.transitionTime, with_onoff);
		break;
	case ZCL_CMD_LEVEL_MOVE:
	case ZCL_CMD_LEVEL_MOVE_WITH_ON_OFF:
		level_run(p->move.moveMode == LEVEL_MOVE_UP, p->move.rate, with_onoff);
		break;
	case ZCL_CMD_LEVEL_STEP:
	case ZCL_CMD_LEVEL_STEP_WITH_ON_OFF: {
		int32_t target = level_current;

		if (p->step.stepMode == LEVEL_STEP_UP) {
			target += p->step.stepSize;
		} else {
			target -= p->step.stepSize;
		}
		target = CLAMP(target, level_min, level_max);
		level_go((u8)target, p->step.transitionTime, with_onoff);
		break;
	}
	case ZCL_CMD_LEVEL_STOP:
	case ZCL_CMD_LEVEL_STOP_WITH_ON_OFF:
		level_stop();
		level_fp = (int32_t)level_current << LEVEL_FP_SHIFT;
		level_publish();
		break;
	default:
		return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	return ZCL_STA_SUCCESS;
}

static zcl_specClusterInfo_t clusters[APP_COMMON_CLUSTER_NUM + 3U];

void app_device_register(void)
{
	app_common_clusters(clusters);

	clusters[2] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_GROUPS,
		.attrNum = ARRAY_SIZE(group_attr_tbl),
		.attrTbl = group_attr_tbl,
		.clusterRegisterFunc = zcl_group_register,
	};
	clusters[3] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_ON_OFF,
		.attrNum = ARRAY_SIZE(onoff_attr_tbl),
		.attrTbl = onoff_attr_tbl,
		.clusterRegisterFunc = zcl_onOff_register,
		.clusterAppCb = onoff_cb,
	};
	clusters[4] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_LEVEL_CONTROL,
		.attrNum = ARRAY_SIZE(level_attr_tbl),
		.attrTbl = level_attr_tbl,
		.clusterRegisterFunc = zcl_level_register,
		.clusterAppCb = level_cb,
	};

	level_fp = (int32_t)level_current << LEVEL_FP_SHIFT;
	app_endpoint_register(&simple_desc, clusters, ARRAY_SIZE(clusters), NULL);
	app_buttons_init(NULL);
}

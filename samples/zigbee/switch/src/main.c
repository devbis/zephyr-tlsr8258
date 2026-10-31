/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * On/Off switch: sw0 toggles the lights in range, sw1 steps their level.
 * Commands are broadcast to all rx-on-when-idle devices (0xFFFC), so the
 * sample needs no binding to control a light.
 */

#include "app_common.h"

#include <string.h>

#include "aps/aps_api.h"
#include "zbapi/zb_api.h"
#include "zcl/general/zcl_group.h"
#include "zcl/general/zcl_onoff.h"
#include "zcl/general/zcl_level.h"

#define BROADCAST_RX_ON_IDLE 0xFFFCU
#define LEVEL_STEP           50U
#define LEVEL_TRANSITION_DS  10U

static u16 out_clusters[] = {
	ZCL_CLUSTER_GEN_GROUPS,
	ZCL_CLUSTER_GEN_ON_OFF,
	ZCL_CLUSTER_GEN_LEVEL_CONTROL,
};

static u16 in_clusters[] = {
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_IDENTIFY,
};

static af_simple_descriptor_t simple_desc = {
	.app_profile_id = APP_HA_PROFILE_ID,
	.app_dev_id = 0x0000U, /* On/Off Switch */
	.endpoint = APP_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = ARRAY_SIZE(in_clusters),
	.app_in_cluster_lst = in_clusters,
	.app_out_cluster_count = ARRAY_SIZE(out_clusters),
	.app_out_cluster_lst = out_clusters,
};

/* Client clusters have no attributes of their own. */
static zcl_specClusterInfo_t clusters[APP_COMMON_CLUSTER_NUM + 3U];

static void dst_broadcast(epInfo_t *dst)
{
	memset(dst, 0, sizeof(*dst));
	dst->dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst->dstAddr.shortAddr = BROADCAST_RX_ON_IDLE;
	dst->dstEp = APP_ENDPOINT;
	dst->profileId = APP_HA_PROFILE_ID;
}

static void button_cb(uint8_t idx, bool pressed)
{
	static u8 level;
	epInfo_t dst;

	if (!pressed || !zb_isDeviceJoinedNwk()) {
		return;
	}

	dst_broadcast(&dst);

	if (idx == 0U) {
		zcl_onOff_toggleCmd(APP_ENDPOINT, &dst, TRUE);
	} else {
		moveToLvl_t move = {
			.transitionTime = LEVEL_TRANSITION_DS,
		};

		level += LEVEL_STEP;
		if (level > ZCL_LEVEL_ATTR_MAX_LEVEL) {
			level = ZCL_LEVEL_ATTR_MIN_LEVEL;
		}
		move.level = level;
		zcl_level_move2levelCmd(APP_ENDPOINT, &dst, TRUE, &move);
	}
}

void app_device_register(void)
{
	app_common_clusters(clusters);

	clusters[2] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_GROUPS,
		.clusterRegisterFunc = zcl_group_register,
	};
	clusters[3] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_ON_OFF,
		.clusterRegisterFunc = zcl_onOff_register,
	};
	clusters[4] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_GEN_LEVEL_CONTROL,
		.clusterRegisterFunc = zcl_level_register,
	};

	app_endpoint_register(&simple_desc, clusters, ARRAY_SIZE(clusters), NULL);
	app_buttons_init(button_cb);
}

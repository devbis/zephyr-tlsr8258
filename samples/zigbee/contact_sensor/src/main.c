/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * Contact sensor: an IAS Zone server (zone type contact switch). sw0 is the
 * contact: pressing it raises ALARM1, releasing it clears it. Each change is
 * reported to the coordinator with a Zone Status Change Notification.
 *
 * Enrollment follows the "auto-enroll request" method: once a CIE writes its
 * address to IAS_CIE_Address, the sensor sends an Enroll Request to the
 * writer and stores the Zone ID from the Enroll Response.
 */

#include "app_common.h"

#include <string.h>

#include "aps/aps_api.h"
#include "zbapi/zb_api.h"
#include "zcl/security_safety/zcl_ias_zone.h"

#define CIE_ADDR_LEN 8U

static const uint16_t cluster_rev = ZCL_ATTR_GLOBAL_CLUSTER_REVISION_DEFAULT;
static uint8_t zone_state = ZONE_STATE_NOT_ENROLLED;
static const uint16_t zone_type = ZONE_TYPE_CONTACT_SWITCH;
static uint16_t zone_status;
static uint8_t zone_id = ZCL_ZONE_ID_INVALID;
static uint8_t cie_addr[CIE_ADDR_LEN];

static const zclAttrInfo_t zone_attr_tbl[] = {
	{ZCL_ATTRID_ZONE_STATE, ZCL_DATA_TYPE_ENUM8, ACCESS_CONTROL_READ, &zone_state},
	{ZCL_ATTRID_ZONE_TYPE, ZCL_DATA_TYPE_ENUM16, ACCESS_CONTROL_READ, (uint8_t *)&zone_type},
	{ZCL_ATTRID_ZONE_STATUS, ZCL_DATA_TYPE_BITMAP16, ACCESS_CONTROL_READ,
	 (uint8_t *)&zone_status},
	{ZCL_ATTRID_IAS_CIE_ADDR, ZCL_DATA_TYPE_IEEE_ADDR,
	 ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, cie_addr},
	{ZCL_ATTRID_ZONE_ID, ZCL_DATA_TYPE_UINT8, ACCESS_CONTROL_READ, &zone_id},
	{ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, ZCL_DATA_TYPE_UINT16, ACCESS_CONTROL_READ,
	 (uint8_t *)&cluster_rev},
};

static uint16_t in_clusters[] = {
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_IDENTIFY,
	ZCL_CLUSTER_SS_IAS_ZONE,
};

static af_simple_descriptor_t simple_desc = {
	.app_profile_id = APP_HA_PROFILE_ID,
	.app_dev_id = 0x0402U, /* IAS Zone */
	.endpoint = APP_ENDPOINT,
	.app_dev_ver = 1U,
	.reserved = 0U,
	.app_in_cluster_count = ARRAY_SIZE(in_clusters),
	.app_in_cluster_lst = in_clusters,
	.app_out_cluster_count = 0U,
	.app_out_cluster_lst = NULL,
};

static zcl_specClusterInfo_t clusters[APP_COMMON_CLUSTER_NUM + 1U];

static void dst_unicast(epInfo_t *dst, uint16_t addr, uint8_t ep)
{
	memset(dst, 0, sizeof(*dst));
	dst->dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst->dstAddr.shortAddr = addr;
	dst->dstEp = ep;
	dst->profileId = APP_HA_PROFILE_ID;
}

static bool cie_addr_set(void)
{
	for (size_t i = 0; i < CIE_ADDR_LEN; i++) {
		if (cie_addr[i] != 0U) {
			return true;
		}
	}

	return false;
}

static void zone_status_report(bool alarm)
{
	zoneStatusChangeNoti_t noti = {0};
	epInfo_t dst;

	if (alarm) {
		zone_status |= ZONE_STATUS_BIT_ALARM1;
	} else {
		zone_status &= (uint16_t)~ZONE_STATUS_BIT_ALARM1;
	}
	zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_ATTRID_ZONE_STATUS,
		       (uint8_t *)&zone_status);

	if (!zb_isDeviceJoinedNwk() || (zone_state != ZONE_STATE_ENROLLED)) {
		return;
	}

	noti.zoneStatus = zone_status;
	noti.zoneId = zone_id;
	/* The CIE is the coordinator, which is always at 0x0000, endpoint 1. */
	dst_unicast(&dst, 0x0000U, 0x01U);
	(void)zcl_iasZone_statusChangeNotificationCmd(APP_ENDPOINT, &dst, TRUE, &noti);
}

static void button_cb(uint8_t idx, bool pressed)
{
	if (idx == 0U) {
		zone_status_report(pressed);
	}
}

static status_t zone_cb(zclIncomingAddrInfo_t *addr_info, uint8_t cmd_id, void *payload)
{
	ARG_UNUSED(addr_info);

	switch (cmd_id) {
	case ZCL_CMD_ZONE_ENROLL_RSP: {
		zoneEnrollRsp_t *rsp = payload;

		if (rsp->code == 0U) {
			zone_id = rsp->zoneId;
			zone_state = ZONE_STATE_ENROLLED;
			zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_SS_IAS_ZONE, ZCL_ATTRID_ZONE_ID,
				       &zone_id);
			zcl_setAttrVal(APP_ENDPOINT, ZCL_CLUSTER_SS_IAS_ZONE,
				       ZCL_ATTRID_ZONE_STATE, &zone_state);
		}
		break;
	}
	case ZCL_CMD_INIT_NORMAL_OPERATION_MODE:
	case ZCL_CMD_INIT_TEST_MODE:
		break;
	default:
		return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	return ZCL_STA_SUCCESS;
}

/* Runs after every foundation command; reacts to a CIE writing its address. */
static void foundation_hook(zclIncoming_t *in)
{
	zoneEnrollReq_t req = {
		.zoneType = ZONE_TYPE_CONTACT_SWITCH,
		.manufacturerCode = 0xFFFFU,
	};
	epInfo_t dst;

	if ((in->hdr.cmd != ZCL_CMD_WRITE) ||
	    (in->msg->indInfo.cluster_id != ZCL_CLUSTER_SS_IAS_ZONE) ||
	    (zone_state == ZONE_STATE_ENROLLED) || !cie_addr_set()) {
		return;
	}

	dst_unicast(&dst, in->msg->indInfo.src_short_addr, in->msg->indInfo.src_ep);
	(void)zcl_iasZone_enrollReqCmd(APP_ENDPOINT, &dst, TRUE, &req);
}

void app_device_register(void)
{
	app_common_clusters(clusters);

	clusters[2] = (zcl_specClusterInfo_t){
		.clusterId = ZCL_CLUSTER_SS_IAS_ZONE,
		.attrNum = ARRAY_SIZE(zone_attr_tbl),
		.attrTbl = zone_attr_tbl,
		.clusterRegisterFunc = zcl_iasZone_register,
		.clusterAppCb = zone_cb,
	};

	app_endpoint_register(&simple_desc, clusters, ARRAY_SIZE(clusters), foundation_hook);
	app_buttons_init(button_cb);
}

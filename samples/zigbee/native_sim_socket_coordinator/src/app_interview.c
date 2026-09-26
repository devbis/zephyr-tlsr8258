/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Interview every device that announces itself, in the order Zigbee2MQTT
 * uses: Node Descriptor, Active Endpoints, Simple Descriptor of each
 * endpoint, then the Basic cluster model identifier.
 */

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "platform/zephyr/compiler_zephyr.h"
#include <zephyr/zigbee/zb_types.h>
#include "zb_common.h"
#include "zbapi/zb_api.h"
#include "zdo/zdp.h"
#include "zcl/zcl_include.h"

#include "app_interview.h"
#include "app_profile.h"

LOG_MODULE_REGISTER(app_interview);

#define APP_INTERVIEW_SLOTS     4U
#define APP_INTERVIEW_MODEL_MAX 32U

struct app_interview {
	bool used;
	uint16_t nwk_addr;
	uint8_t ieee_addr[8];
};

static struct app_interview interviews[APP_INTERVIEW_SLOTS];

static struct app_interview *app_interview_find(uint16_t nwk_addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(interviews); i++) {
		if (interviews[i].used && (interviews[i].nwk_addr == nwk_addr)) {
			return &interviews[i];
		}
	}

	return NULL;
}

static void app_interview_fail(uint16_t nwk_addr, const char *step, u8 status)
{
	struct app_interview *interview = app_interview_find(nwk_addr);

	LOG_ERR("interview 0x%04x: %s failed (%u)", nwk_addr, step, status);
	if (interview != NULL) {
		interview->used = false;
	}
}

static void app_interview_read_model_id(uint16_t nwk_addr, uint8_t ep)
{
	epInfo_t ep_info;
	struct {
		u8 numAttr;
		u16 attrID[1];
	} read_cmd = { 1U, { ZCL_ATTRID_BASIC_MODEL_ID } };

	memset(&ep_info, 0, sizeof(ep_info));
	ep_info.dstAddr.shortAddr = nwk_addr;
	ep_info.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	ep_info.dstEp = ep;
	ep_info.profileId = APP_PROFILE_HA_PROFILE_ID;
	ep_info.txOptions = APS_TX_OPT_ACK_TX;
	ep_info.radius = 0U;

	if (zcl_read(APP_PROFILE_ENDPOINT, &ep_info, ZCL_CLUSTER_GEN_BASIC,
		     MANUFACTURER_CODE_NONE, 0U, ZCL_FRAME_CLIENT_SERVER_DIR, ZCL_SEQ_NUM,
		     (zclReadCmd_t *)&read_cmd) != ZCL_STA_SUCCESS) {
		app_interview_fail(nwk_addr, "model identifier read", 0U);
	}
}

static void app_interview_simple_desc_rsp(void *arg)
{
	const zdo_zdpDataInd_t *ind = arg;
	const zdo_simple_descriptor_resp_t *rsp = (const zdo_simple_descriptor_resp_t *)ind->zpdu;

	if (ind->status != ZDO_SUCCESS) {
		app_interview_fail(ind->src_addr, "simple descriptor", ind->status);
		return;
	}

	LOG_INF("interview 0x%04x: simple descriptor ep %u profile 0x%04x", ind->src_addr,
		rsp->simple_descriptor.endpoint, rsp->simple_descriptor.app_profile_id);
	app_interview_read_model_id(ind->src_addr, rsp->simple_descriptor.endpoint);
}

static void app_interview_active_ep_rsp(void *arg)
{
	const zdo_zdpDataInd_t *ind = arg;
	const zdo_active_ep_resp_t *rsp = (const zdo_active_ep_resp_t *)ind->zpdu;
	zdo_simple_descriptor_req_t req;
	u8 seq = 0U;

	if ((ind->status != ZDO_SUCCESS) || (rsp->active_ep_count == 0U)) {
		app_interview_fail(ind->src_addr, "active endpoints", ind->status);
		return;
	}

	LOG_INF("interview 0x%04x: %u active endpoint(s), first %u", ind->src_addr,
		rsp->active_ep_count, rsp->active_ep_lst[0]);
	req.nwk_addr_interest = ind->src_addr;
	req.endpoint = rsp->active_ep_lst[0];
	(void)zb_zdoSimpleDescReq(ind->src_addr, &req, &seq, app_interview_simple_desc_rsp);
}

static void app_interview_node_desc_rsp(void *arg)
{
	const zdo_zdpDataInd_t *ind = arg;
	zdo_active_ep_req_t req;
	u8 seq = 0U;

	if (ind->status != ZDO_SUCCESS) {
		app_interview_fail(ind->src_addr, "node descriptor", ind->status);
		return;
	}

	LOG_INF("interview 0x%04x: node descriptor", ind->src_addr);
	req.nwk_addr_interest = ind->src_addr;
	(void)zb_zdoActiveEpReq(ind->src_addr, &req, &seq, app_interview_active_ep_rsp);
}

void zb_platform_app_device_announce(uint16_t nwk_addr, const uint8_t ieee_addr[8])
{
	struct app_interview *interview = NULL;
	zdo_node_descriptor_req_t req;
	u8 seq = 0U;

	/* A device that announces again, possibly with a new network address,
	 * restarts its interview in the slot it already has.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(interviews); i++) {
		if (interviews[i].used &&
		    (memcmp(interviews[i].ieee_addr, ieee_addr, sizeof(interviews[i].ieee_addr)) == 0)) {
			interview = &interviews[i];
			break;
		}
	}
	for (size_t i = 0; (interview == NULL) && (i < ARRAY_SIZE(interviews)); i++) {
		if (!interviews[i].used) {
			interview = &interviews[i];
		}
	}
	if (interview == NULL) {
		LOG_WRN("interview 0x%04x: no free slot", nwk_addr);
		return;
	}

	interview->used = true;
	interview->nwk_addr = nwk_addr;
	memcpy(interview->ieee_addr, ieee_addr, sizeof(interview->ieee_addr));

	LOG_INF("interview 0x%04x: started", nwk_addr);
	req.nwk_addr_interest = nwk_addr;
	(void)zb_zdoNodeDescReq(nwk_addr, &req, &seq, app_interview_node_desc_rsp);
}

void app_interview_zcl_msg(zclIncoming_t *msg)
{
	const zclReadRspCmd_t *rsp = msg->attrCmd;
	uint16_t nwk_addr = msg->msg->indInfo.src_short_addr;
	struct app_interview *interview;
	char model_id[APP_INTERVIEW_MODEL_MAX + 1];
	const u8 *data;
	u8 len;

	if ((msg->hdr.cmd != ZCL_CMD_READ_RSP) ||
	    (msg->msg->indInfo.cluster_id != ZCL_CLUSTER_GEN_BASIC)) {
		return;
	}

	interview = app_interview_find(nwk_addr);
	if (interview == NULL) {
		return;
	}

	for (u8 i = 0U; i < rsp->numAttr; i++) {
		if ((rsp->attrList[i].attrID != ZCL_ATTRID_BASIC_MODEL_ID) ||
		    (rsp->attrList[i].status != ZCL_STA_SUCCESS) ||
		    (rsp->attrList[i].dataType != ZCL_DATA_TYPE_CHAR_STR)) {
			continue;
		}

		data = rsp->attrList[i].data;
		len = MIN(data[0], APP_INTERVIEW_MODEL_MAX);
		memcpy(model_id, &data[1], len);
		model_id[len] = '\0';

		LOG_INF("interview complete nwk=0x%04x ieee=%02x%02x%02x%02x%02x%02x%02x%02x "
			"model-id=%s",
			nwk_addr, interview->ieee_addr[7], interview->ieee_addr[6],
			interview->ieee_addr[5], interview->ieee_addr[4], interview->ieee_addr[3],
			interview->ieee_addr[2], interview->ieee_addr[1], interview->ieee_addr[0],
			model_id);
		interview->used = false;
		return;
	}
}

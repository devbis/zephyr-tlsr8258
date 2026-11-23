/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * EZSP commands and callbacks of the application data path: the endpoints the host registers,
 * the messages it sends and the messages and device events it receives. ZDO is not a command set
 * of its own here: the host builds the ZDO frame and sends it to endpoint 0 like any other.
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/zigbee/zb_bootstrap.h>

#include "aps/aps_data.h"
#include "ss/ss_security_flags.h"
#include "zb_common.h"
#include "zb_ezsp_frame.h"
#include "zb_ezsp_internal.h"
#include "zb_ezsp_session.h"

/* The short address of a device the stack does not know. */
#define EZSP_NWK_ADDR_UNKNOWN   0xFFFEU

#define EZSP_MAX_ENDPOINTS      16U
#define EZSP_MAX_CLUSTERS       24U
#define EZSP_MAX_PENDING        8U
#define EZSP_DEFAULT_RADIUS     30U
/*
 * Buffers kept free for received frames, acknowledgments and the stack's own traffic. A request
 * that would take them is refused, since the stack then cannot process what the radio delivers.
 */
#define EZSP_BUF_RESERVE        8U
/* A request the stack has not confirmed by then is reported as undelivered and its slot reused. */
#define EZSP_PENDING_TIMEOUT_MS 30000U

/* EmberOutgoingMessageType and EmberIncomingMessageType. */
#define EZSP_OUT_DIRECT         0U
#define EZSP_OUT_MULTICAST      3U
#define EZSP_OUT_BROADCAST      6U
#define EZSP_IN_UNICAST         0U
#define EZSP_IN_MULTICAST       2U
#define EZSP_IN_BROADCAST       4U

/* EmberApsOption bits that map to the stack. */
#define EZSP_APS_OPTION_ENCRYPTION 0x0020U
#define EZSP_APS_OPTION_RETRY      0x0040U

/* EmberDeviceUpdate and EmberJoinDecision of trustCenterJoinHandler. */
#define EZSP_DEVICE_UNSECURED_JOIN 0x01U
#define EZSP_DEVICE_LEFT           0x02U
#define EZSP_JOIN_USE_PRECONFIGURED_KEY 0x00U
#define EZSP_JOIN_NO_ACTION        0x03U

/* The EmberApsFrame: profile, cluster, endpoints, options, group and sequence number. */
#define EZSP_APS_FRAME_LEN      11U

/*
 * What an incomingMessageHandler holds besides the message: the type, the APS frame, the link
 * quality and RSSI, the sender, the binding and address table index and the length.
 */
#define EZSP_INCOMING_OVERHEAD  (1U + EZSP_APS_FRAME_LEN + 2U + 2U + 2U + 1U)
#define EZSP_INCOMING_ROOM      (EZSP_MAX_FRAME - EZSP_EXT_HEADER_LEN - EZSP_INCOMING_OVERHEAD)

struct ezsp_aps_frame {
	uint16_t profile;
	uint16_t cluster;
	uint8_t src_ep;
	uint8_t dst_ep;
	uint16_t options;
	uint16_t group;
	uint8_t sequence;
};

struct ezsp_endpoint {
	bool used;
	af_simple_descriptor_t desc;
	uint16_t in[EZSP_MAX_CLUSTERS];
	uint16_t out[EZSP_MAX_CLUSTERS];
};

/* A request waiting for its confirmation from the stack. */
struct ezsp_pending {
	bool used;
	uint8_t aps_cnt;
	uint8_t type;
	uint8_t tag;
	uint16_t destination;
	struct ezsp_aps_frame aps;
	uint32_t start_ms;
};

static struct ezsp_endpoint endpoints[EZSP_MAX_ENDPOINTS];
static struct ezsp_pending pending[EZSP_MAX_PENDING];

static void aps_frame_read(struct ezsp_rd *in, struct ezsp_aps_frame *f)
{
	f->profile = ezsp_rd_u16(in);
	f->cluster = ezsp_rd_u16(in);
	f->src_ep = ezsp_rd_u8(in);
	f->dst_ep = ezsp_rd_u8(in);
	f->options = ezsp_rd_u16(in);
	f->group = ezsp_rd_u16(in);
	f->sequence = ezsp_rd_u8(in);
}

static void aps_frame_write(struct ezsp_wr *out, const struct ezsp_aps_frame *f)
{
	ezsp_wr_u16(out, f->profile);
	ezsp_wr_u16(out, f->cluster);
	ezsp_wr_u8(out, f->src_ep);
	ezsp_wr_u8(out, f->dst_ep);
	ezsp_wr_u16(out, f->options);
	ezsp_wr_u16(out, f->group);
	ezsp_wr_u8(out, f->sequence);
}

/* messageSentHandler: type, destination, APS frame, tag, status and no message contents. */
static void message_sent(uint8_t type, uint16_t destination, const struct ezsp_aps_frame *aps,
			 uint8_t tag, uint8_t status)
{
	uint8_t buf[1U + 2U + EZSP_APS_FRAME_LEN + 1U + 1U + 1U];
	struct ezsp_wr w;

	ezsp_wr_init(&w, buf, sizeof(buf));
	ezsp_wr_u8(&w, type);
	ezsp_wr_u16(&w, destination);
	aps_frame_write(&w, aps);
	ezsp_wr_u8(&w, tag);
	ezsp_wr_u8(&w, status);
	ezsp_wr_u8(&w, 0U);
	(void)zb_ezsp_callback(EZSP_ID_MESSAGE_SENT_HANDLER, buf, w.len);
}

static uint8_t map_aps_status(uint8_t aps)
{
	return (aps == APS_STATUS_SUCCESS) ? EMBER_SUCCESS : EMBER_DELIVERY_FAILED;
}

/* Confirmation from the stack, for every endpoint the host registered. */
static void ezsp_ep_cnf(void *arg)
{
	const apsdeDataConf_t *cnf = (const apsdeDataConf_t *)arg;

	for (size_t i = 0; i < ARRAY_SIZE(pending); i++) {
		if (pending[i].used && pending[i].aps_cnt == cnf->apsCnt &&
		    pending[i].aps.src_ep == cnf->srcEndpoint) {
			pending[i].used = false;
			message_sent(pending[i].type, pending[i].destination, &pending[i].aps,
				     pending[i].tag, map_aps_status(cnf->status));
			break;
		}
	}

	ev_buf_free((uint8_t *)arg);
}

/* incomingMessageHandler for an application frame for a host endpoint. */
static void ezsp_ep_rx(void *arg)
{
	const apsdeDataInd_t *ind = (const apsdeDataInd_t *)arg;
	const aps_data_ind_t *info = &ind->indInfo;
	uint8_t out[EZSP_MAX_FRAME];
	struct ezsp_wr w;
	struct ezsp_aps_frame aps = {
		.profile = info->profile_id,
		.cluster = info->cluster_id,
		.src_ep = info->src_ep,
		.dst_ep = info->dst_ep,
		.options = (info->security_status != 0U) ? EZSP_APS_OPTION_ENCRYPTION : 0U,
		.group = (info->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) ? info->dst_addr : 0U,
		.sequence = info->aps_counter,
	};
	uint8_t type = EZSP_IN_UNICAST;
	uint8_t n = (uint8_t)MIN(ind->asduLen, EZSP_INCOMING_ROOM);

	if (info->dst_addr_mode == APS_SHORT_GROUPADDR_NOEP) {
		type = EZSP_IN_MULTICAST;
	} else if (ZB_NWK_IS_ADDRESS_BROADCAST(info->dst_addr)) {
		type = EZSP_IN_BROADCAST;
	}

	ezsp_wr_init(&w, out, sizeof(out));
	ezsp_wr_u8(&w, type);
	aps_frame_write(&w, &aps);
	ezsp_wr_u8(&w, info->lqi);
	/* The stack keeps no RSSI of a frame */
	ezsp_wr_u8(&w, 0U);
	ezsp_wr_u16(&w, info->src_short_addr);
	/* No binding or address table entry */
	ezsp_wr_u8(&w, 0xFFU);
	ezsp_wr_u8(&w, 0xFFU);
	ezsp_wr_u8(&w, n);
	ezsp_wr_bytes(&w, ind->asdu, n);

	(void)zb_ezsp_callback(EZSP_ID_INCOMING_MESSAGE_HANDLER, out, w.len);
	ev_buf_free((uint8_t *)arg);
}

static void cmd_add_endpoint(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	struct ezsp_endpoint *e = NULL;
	uint8_t endpoint = ezsp_rd_u8(in);
	uint16_t profile = ezsp_rd_u16(in);
	uint16_t device = ezsp_rd_u16(in);
	uint8_t version = ezsp_rd_u8(in);
	uint8_t in_count = ezsp_rd_u8(in);
	uint8_t out_count = ezsp_rd_u8(in);
	const uint8_t *in_list;
	const uint8_t *out_list;

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (in_count > EZSP_MAX_CLUSTERS || out_count > EZSP_MAX_CLUSTERS) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_CALL);
		return;
	}

	in_list = ezsp_rd_bytes(in, (size_t)in_count * 2U);
	out_list = ezsp_rd_bytes(in, (size_t)out_count * 2U);
	if (in->err) {
		return;
	}

	/* Registering an endpoint again replaces it */
	for (size_t i = 0; i < ARRAY_SIZE(endpoints); i++) {
		if (endpoints[i].used && endpoints[i].desc.endpoint == endpoint) {
			e = &endpoints[i];
			(void)af_endpointUnregister(endpoint);
			break;
		}
	}
	if (e == NULL) {
		(void)af_endpointUnregister(endpoint);
		for (size_t i = 0; i < ARRAY_SIZE(endpoints); i++) {
			if (!endpoints[i].used) {
				e = &endpoints[i];
				break;
			}
		}
	}
	if (e == NULL) {
		ezsp_wr_u8(out, EZSP_ERROR_OUT_OF_MEMORY);
		return;
	}

	memset(e, 0, sizeof(*e));
	e->desc.endpoint = endpoint;
	e->desc.app_profile_id = profile;
	e->desc.app_dev_id = device;
	e->desc.app_dev_ver = version & 0x0fU;
	e->desc.app_in_cluster_count = in_count;
	e->desc.app_out_cluster_count = out_count;
	for (uint8_t i = 0; i < in_count; i++) {
		e->in[i] = (uint16_t)(in_list[2U * i] | ((uint16_t)in_list[2U * i + 1U] << 8));
	}
	for (uint8_t i = 0; i < out_count; i++) {
		e->out[i] = (uint16_t)(out_list[2U * i] | ((uint16_t)out_list[2U * i + 1U] << 8));
	}
	e->desc.app_in_cluster_lst = e->in;
	e->desc.app_out_cluster_lst = e->out;

	if (!af_endpointRegister(endpoint, &e->desc, ezsp_ep_rx, ezsp_ep_cnf)) {
		ezsp_wr_u8(out, EZSP_ERROR_OUT_OF_MEMORY);
		return;
	}

	e->used = true;
	ezsp_wr_u8(out, EZSP_SUCCESS);
}

/* EzspExtendedValueId and the endpoint flags: the endpoints that are registered are enabled. */
#define EZSP_EXTENDED_VALUE_ENDPOINT_FLAGS 0x00U
#define EZSP_ENDPOINT_ENABLED              0x0001U

/*
 * getExtendedValue, for the flags of an endpoint, which a host reads to learn whether it has
 * registered its endpoints already. The value is two bytes either way. Other extended values do not
 * exist here.
 */
static void cmd_get_extended_value(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	uint8_t id = ezsp_rd_u8(in);
	uint32_t characteristics = ezsp_rd_u32(in);
	bool registered = false;

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (id != EZSP_EXTENDED_VALUE_ENDPOINT_FLAGS) {
		ezsp_wr_u8(out, EZSP_ERROR_INVALID_ID);
		ezsp_wr_u8(out, 0U);
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(endpoints); i++) {
		if (endpoints[i].used && endpoints[i].desc.endpoint == characteristics) {
			registered = true;
			break;
		}
	}

	ezsp_wr_u8(out, registered ? EZSP_SUCCESS : EZSP_ERROR_INVALID_ID);
	ezsp_wr_u8(out, 2U);
	ezsp_wr_u16(out, registered ? EZSP_ENDPOINT_ENABLED : 0U);
}

/* A free slot to track a request, reclaiming one the stack never confirmed. */
static struct ezsp_pending *pending_slot_get(void)
{
	uint32_t now = k_uptime_get_32();

	for (size_t i = 0; i < ARRAY_SIZE(pending); i++) {
		if (pending[i].used && (now - pending[i].start_ms) > EZSP_PENDING_TIMEOUT_MS) {
			pending[i].used = false;
			message_sent(pending[i].type, pending[i].destination, &pending[i].aps,
				     pending[i].tag, EMBER_DELIVERY_FAILED);
		}
	}

	for (size_t i = 0; i < ARRAY_SIZE(pending); i++) {
		if (!pending[i].used) {
			return &pending[i];
		}
	}

	return NULL;
}

static uint16_t src_profile(uint8_t ep)
{
	const af_simple_descriptor_t *desc = af_simpleDescGet(ep);

	return (desc != NULL) ? desc->app_profile_id : HA_PROFILE_ID;
}

enum ezsp_dst {
	EZSP_DST_NWK,
	EZSP_DST_GROUP,
};

/*
 * Send an application frame through the APS layer, which tracks its acknowledgment and retries
 * and confirms it to the source endpoint.
 */
static uint8_t aps_send(enum ezsp_dst mode, uint16_t dst, const struct ezsp_aps_frame *aps,
			uint8_t radius, const uint8_t *data, uint16_t data_len, uint8_t *aps_cnt)
{
	aps_data_req_t req;

	memset(&req, 0, sizeof(req));
	req.src_endpoint = aps->src_ep;
	req.cluster_id = aps->cluster;
	req.profile_id = (aps->profile != 0U) ? aps->profile : src_profile(aps->src_ep);
	req.radius = radius;
	req.apsCnt = aps_get_counter_value();
	req.enableNWKsecurity = (ss_ib_security_level_get() != 0U);
	req.tx_options = ((aps->options & EZSP_APS_OPTION_RETRY) != 0U) ? APS_TX_OPT_ACK_TX : 0U;
	*aps_cnt = req.apsCnt;

	if (mode == EZSP_DST_GROUP) {
		req.dst_addr_mode = APS_SHORT_GROUPADDR_NOEP;
		req.aps_addr.dst_group_addr = aps->group;
	} else {
		req.dst_addr_mode = APS_SHORT_DSTADDR_WITHEP;
		req.aps_addr.dst_short_addr = dst;
		req.aps_addr.dst_endpoint = aps->dst_ep;
	}

	if (data_len > aps_ib.aps_fragment_payload_size && mode != EZSP_DST_GROUP) {
		req.tx_options |= APS_TX_OPT_ACK_TX;
		return apsDataFragmentRequest(&req, (uint8_t *)data, data_len);
	}

	return apsDataRequest(&req, (uint8_t *)data, (uint8_t)data_len);
}

/* A ZDO frame has no APS acknowledgment to track, so it is sent directly. */
static uint8_t zdo_frame_send(uint16_t dst, uint16_t cluster, uint8_t radius, const uint8_t *data,
			      uint16_t data_len, uint8_t *aps_cnt)
{
	epInfo_t ep;

	memset(&ep, 0, sizeof(ep));
	ep.dstAddr.shortAddr = dst;
	ep.dstEp = ZDO_EP;
	ep.profileId = ZDO_PROFILE_ID;
	ep.dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	ep.radius = radius;

	return af_dataSend(ZDO_EP, &ep, cluster, data_len, (uint8_t *)data, aps_cnt);
}

/* What the confirmation of a ZDO frame reports, once the response has been queued. */
static struct {
	uint8_t type;
	uint16_t destination;
	struct ezsp_aps_frame aps;
	uint8_t tag;
} zdo_sent;

static void zdo_sent_after(struct ezsp_ctx *c)
{
	ARG_UNUSED(c);
	message_sent(zdo_sent.type, zdo_sent.destination, &zdo_sent.aps, zdo_sent.tag,
		     EMBER_SUCCESS);
}

/*
 * Send a message and write the response: the status and the APS sequence number. The result of the
 * transmission follows as messageSentHandler.
 */
static void data_request(struct ezsp_ctx *c, struct ezsp_wr *out, uint8_t type,
			 uint16_t destination, enum ezsp_dst mode, struct ezsp_aps_frame *aps,
			 uint8_t radius, uint8_t tag, const uint8_t *data, uint8_t data_len)
{
	uint8_t aps_cnt = 0U;
	uint8_t status;
	bool zdo = (aps->dst_ep == ZDO_EP && mode == EZSP_DST_NWK);
	struct ezsp_pending *slot = zdo ? NULL : pending_slot_get();

	if (zb_buf_free_count() < EZSP_BUF_RESERVE || (!zdo && slot == NULL)) {
		ezsp_wr_u8(out, EMBER_NO_BUFFERS);
		ezsp_wr_u8(out, 0U);
		return;
	}

	if (zdo) {
		status = zdo_frame_send(destination, aps->cluster, radius, data, data_len,
					&aps_cnt);
	} else {
		status = aps_send(mode, destination, aps, radius, data, data_len, &aps_cnt);
	}
	if (status != APS_STATUS_SUCCESS) {
		ezsp_wr_u8(out, EMBER_DELIVERY_FAILED);
		ezsp_wr_u8(out, 0U);
		return;
	}

	aps->sequence = aps_cnt;
	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_u8(out, aps_cnt);

	if (zdo) {
		/* A ZDO frame has no confirmation of the stack: it was handed over, which is all
		 * there is to report.
		 */
		zdo_sent.type = type;
		zdo_sent.destination = destination;
		zdo_sent.aps = *aps;
		zdo_sent.tag = tag;
		c->after = zdo_sent_after;
		return;
	}

	slot->used = true;
	slot->aps_cnt = aps_cnt;
	slot->type = type;
	slot->tag = tag;
	slot->destination = destination;
	slot->aps = *aps;
	slot->start_ms = k_uptime_get_32();
}

static void cmd_send_unicast(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	struct ezsp_aps_frame aps;
	uint8_t type = ezsp_rd_u8(in);
	uint16_t destination = ezsp_rd_u16(in);
	uint8_t tag;
	uint8_t len;
	const uint8_t *data;

	aps_frame_read(in, &aps);
	tag = ezsp_rd_u8(in);
	len = ezsp_rd_u8(in);
	data = ezsp_rd_bytes(in, len);
	if (in->err) {
		return;
	}

	/* A destination by address table or binding index needs those tables */
	if (type != EZSP_OUT_DIRECT) {
		ezsp_wr_u8(out, EMBER_INVALID_CALL);
		ezsp_wr_u8(out, 0U);
		return;
	}

	data_request(c, out, type, destination, EZSP_DST_NWK, &aps, EZSP_DEFAULT_RADIUS, tag, data,
		     len);
}

static void cmd_send_broadcast(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	struct ezsp_aps_frame aps;
	uint16_t destination = ezsp_rd_u16(in);
	uint8_t radius;
	uint8_t tag;
	uint8_t len;
	const uint8_t *data;

	aps_frame_read(in, &aps);
	radius = ezsp_rd_u8(in);
	tag = ezsp_rd_u8(in);
	len = ezsp_rd_u8(in);
	data = ezsp_rd_bytes(in, len);
	if (in->err) {
		return;
	}

	data_request(c, out, EZSP_OUT_BROADCAST, destination, EZSP_DST_NWK, &aps, radius, tag, data,
		     len);
}

static void cmd_send_multicast(struct ezsp_ctx *c, struct ezsp_rd *in, struct ezsp_wr *out)
{
	struct ezsp_aps_frame aps;
	uint8_t hops;
	uint8_t tag;
	uint8_t len;
	const uint8_t *data;

	aps_frame_read(in, &aps);
	hops = ezsp_rd_u8(in);
	/* The radius for members of the group that are not routers or the coordinator */
	(void)ezsp_rd_u8(in);
	tag = ezsp_rd_u8(in);
	len = ezsp_rd_u8(in);
	data = ezsp_rd_bytes(in, len);
	if (in->err) {
		return;
	}

	data_request(c, out, EZSP_OUT_MULTICAST, aps.group, EZSP_DST_GROUP, &aps,
		     (hops != 0U) ? hops : EZSP_DEFAULT_RADIUS, tag, data, len);
}

/*
 * The multicast table: the groups the host has this node join, each for an endpoint. An entry with
 * endpoint 0 is not used. Every used entry is a group of the stack's APS layer.
 */
#define EZSP_MULTICAST_TABLE_SIZE 16U

static struct {
	uint16_t id;
	uint8_t endpoint;
} multicast[EZSP_MULTICAST_TABLE_SIZE];

static void cmd_get_multicast_table_entry(struct ezsp_ctx *c, struct ezsp_rd *in,
					  struct ezsp_wr *out)
{
	uint8_t index = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	if (in->err) {
		return;
	}

	if (index >= EZSP_MULTICAST_TABLE_SIZE) {
		static const uint8_t none[2U + 1U + 1U];

		ezsp_wr_u8(out, EMBER_INDEX_OUT_OF_RANGE);
		ezsp_wr_bytes(out, none, sizeof(none));
		return;
	}

	ezsp_wr_u8(out, EMBER_SUCCESS);
	ezsp_wr_u16(out, multicast[index].id);
	ezsp_wr_u8(out, multicast[index].endpoint);
	/* The network index */
	ezsp_wr_u8(out, 0U);
}

static void cmd_set_multicast_table_entry(struct ezsp_ctx *c, struct ezsp_rd *in,
					  struct ezsp_wr *out)
{
	uint8_t index = ezsp_rd_u8(in);
	uint16_t id = ezsp_rd_u16(in);
	uint8_t endpoint = ezsp_rd_u8(in);

	ARG_UNUSED(c);
	(void)ezsp_rd_u8(in);
	if (in->err) {
		return;
	}

	if (index >= EZSP_MULTICAST_TABLE_SIZE) {
		ezsp_wr_u8(out, EMBER_INDEX_OUT_OF_RANGE);
		return;
	}

	if (multicast[index].endpoint != 0U) {
		aps_delete_group_req_t del = {
			.ep = multicast[index].endpoint,
			.group_addr = multicast[index].id,
		};

		(void)aps_me_group_delete_req(&del);
		multicast[index].endpoint = 0U;
	}

	if (endpoint != 0U) {
		aps_add_group_req_t add = {.ep = endpoint, .group_addr = id};

		if (aps_me_group_add_req(&add) != APS_STATUS_SUCCESS) {
			ezsp_wr_u8(out, EMBER_ERR_FATAL);
			return;
		}
		multicast[index].endpoint = endpoint;
	}

	multicast[index].id = id;
	ezsp_wr_u8(out, EMBER_SUCCESS);
}

/* The configuration of what ZDO the host receives, and its bits. */
#define EZSP_CFG_APPLICATION_ZDO_FLAGS       0x2AU
#define EZSP_ZDO_RECEIVES_SUPPORTED_REQUESTS 0x0001U
#define EZSP_ZDO_RECEIVES_UNSUPPORTED_REQUESTS 0x0002U

/* The cluster of a ZDO response, and of the device announcement, which is sent to everyone. */
#define EZSP_ZDO_RESPONSE_BIT                0x8000U
#define EZSP_ZDO_DEVICE_ANNOUNCE             0x0013U

/*
 * Whether the host receives a ZDO frame. Responses and device announcements always go to it. The
 * requests to this node, which the stack answers itself, go only to a host that asked for them,
 * as an EmberZNet NCP does: a host that does not expect them takes every ZDO frame for a response.
 */
static bool zdo_wanted(uint16_t cluster)
{
	uint16_t flags = 0U;

	if ((cluster & EZSP_ZDO_RESPONSE_BIT) != 0U || cluster == EZSP_ZDO_DEVICE_ANNOUNCE) {
		return true;
	}

	return zb_ezsp_config_get(EZSP_CFG_APPLICATION_ZDO_FLAGS, &flags) &&
	       (flags & (EZSP_ZDO_RECEIVES_SUPPORTED_REQUESTS |
			 EZSP_ZDO_RECEIVES_UNSUPPORTED_REQUESTS)) != 0U;
}

/* Received ZDO frames go to the host as messages of the ZDO endpoint. */
void zb_platform_app_zdo_rx(uint16_t src_addr, bool broadcast, uint16_t cluster_id,
			    const uint8_t *payload, uint16_t len)
{
	uint8_t out[EZSP_MAX_FRAME];
	struct ezsp_wr w;
	struct ezsp_aps_frame aps = {
		.profile = ZDO_PROFILE_ID,
		.cluster = cluster_id,
		.src_ep = ZDO_EP,
		.dst_ep = ZDO_EP,
	};

	if (len < 1U || len > EZSP_INCOMING_ROOM || !zdo_wanted(cluster_id)) {
		return;
	}

	/* The sequence number of the ZDO frame is also the APS sequence number */
	aps.sequence = payload[0];

	ezsp_wr_init(&w, out, sizeof(out));
	ezsp_wr_u8(&w, broadcast ? EZSP_IN_BROADCAST : EZSP_IN_UNICAST);
	aps_frame_write(&w, &aps);
	ezsp_wr_u8(&w, 0U);
	ezsp_wr_u8(&w, 0U);
	ezsp_wr_u16(&w, src_addr);
	ezsp_wr_u8(&w, 0xFFU);
	ezsp_wr_u8(&w, 0xFFU);
	ezsp_wr_u8(&w, (uint8_t)len);
	ezsp_wr_bytes(&w, payload, len);

	(void)zb_ezsp_callback(EZSP_ID_INCOMING_MESSAGE_HANDLER, out, w.len);
}

void zb_platform_app_device_joined(uint16_t nwk_addr, const uint8_t ieee_addr[8],
				   const uint8_t parent_ieee_addr[8])
{
	uint8_t out[2U + 8U + 1U + 1U + 2U];
	struct ezsp_wr w;
	uint8_t local[8];
	uint16_t parent = EZSP_NWK_ADDR_UNKNOWN;

	/* A device that joined through the trust center itself has no other parent */
	zb_getLocalExtAddr(local);
	if (memcmp(parent_ieee_addr, local, 8U) == 0 || ZB_IEEE_ADDR_IS_ZERO(parent_ieee_addr) ||
	    zb_getNwkAddrByExtAddr((uint8_t *)parent_ieee_addr, &parent) != 0U) {
		parent = zb_getLocalShortAddr();
	}

	ezsp_wr_init(&w, out, sizeof(out));
	ezsp_wr_u16(&w, nwk_addr);
	ezsp_wr_bytes(&w, ieee_addr, 8U);
	ezsp_wr_u8(&w, EZSP_DEVICE_UNSECURED_JOIN);
	ezsp_wr_u8(&w, EZSP_JOIN_USE_PRECONFIGURED_KEY);
	ezsp_wr_u16(&w, parent);
	(void)zb_ezsp_callback(EZSP_ID_TRUST_CENTER_JOIN_HANDLER, out, w.len);
}

void zb_platform_app_device_left(const uint8_t ieee_addr[8], bool rejoin)
{
	uint8_t out[2U + 8U + 1U + 1U + 2U];
	struct ezsp_wr w;
	uint16_t src = EZSP_NWK_ADDR_UNKNOWN;

	/* A device that leaves to rejoin is announced again when it joins */
	ARG_UNUSED(rejoin);
	(void)zb_getNwkAddrByExtAddr((uint8_t *)ieee_addr, &src);

	ezsp_wr_init(&w, out, sizeof(out));
	ezsp_wr_u16(&w, src);
	ezsp_wr_bytes(&w, ieee_addr, 8U);
	ezsp_wr_u8(&w, EZSP_DEVICE_LEFT);
	ezsp_wr_u8(&w, EZSP_JOIN_NO_ACTION);
	ezsp_wr_u16(&w, EZSP_NWK_ADDR_UNKNOWN);
	(void)zb_ezsp_callback(EZSP_ID_TRUST_CENTER_JOIN_HANDLER, out, w.len);
}

static const struct ezsp_cmd aps_cmds[] = {
	{EZSP_ID_ADD_ENDPOINT, cmd_add_endpoint},
	{EZSP_ID_GET_EXTENDED_VALUE, cmd_get_extended_value},
	{EZSP_ID_GET_MULTICAST_TABLE_ENTRY, cmd_get_multicast_table_entry},
	{EZSP_ID_SET_MULTICAST_TABLE_ENTRY, cmd_set_multicast_table_entry},
	{EZSP_ID_SEND_UNICAST, cmd_send_unicast},
	{EZSP_ID_SEND_BROADCAST, cmd_send_broadcast},
	{EZSP_ID_SEND_MULTICAST, cmd_send_multicast},
};

const struct ezsp_cmd_set ezsp_aps_cmds = {
	.cmds = aps_cmds,
	.count = ARRAY_SIZE(aps_cmds),
};

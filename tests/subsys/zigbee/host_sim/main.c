/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SIM_COORD_SHORT_ADDR 0x0000u
#define SIM_DEVICE_IEEE     0xa4c138e050020002ULL
#define SIM_PAN_ID          0x5b27u
#define SIM_POLL_RATE_MS    1000u
#define SIM_POLL_STOPPED    UINT32_MAX
#define SIM_NO_SHORT_ADDR   0xffffu
#define SIM_MAX_WIRE_LEN    128u

#define SIM_MAC_FCF_COMMAND_SHORT 0x8863u
#define SIM_MAC_CMD_ASSOC_REQ     0x01u
#define SIM_MAC_CMD_ASSOC_RSP     0x02u
#define SIM_MAC_CMD_DATA_REQ      0x04u

#define SIM_PROFILE_HA            0x0104u
#define SIM_ZDO_DEVICE_ANNCE      0x0013u
#define SIM_ZDO_ACTIVE_EP_REQ     0x0005u
#define SIM_ZDO_ACTIVE_EP_RSP     0x8005u
#define SIM_ZDO_SIMPLE_DESC_REQ   0x0004u
#define SIM_ZDO_SIMPLE_DESC_RSP   0x8004u
#define SIM_CLUSTER_BASIC         0x0000u
#define SIM_CLUSTER_NWK_MGMT      0xff00u
#define SIM_NWK_TIMEOUT_REQ       0x0bu
#define SIM_NWK_TIMEOUT_RSP       0x0cu
#define SIM_ZCL_CMD_READ          0x00u
#define SIM_ZCL_CMD_READ_RSP      0x01u
#define SIM_ZCL_ATTR_MODEL_ID     0x0005u
#define SIM_ZCL_TYPE_CHAR_STRING  0x42u

enum sim_frame_type {
	SIM_FRAME_ASSOC_REQ,
	SIM_FRAME_ASSOC_RSP,
	SIM_FRAME_DATA_REQ,
	SIM_FRAME_TRANSPORT_KEY,
	SIM_FRAME_END_DEVICE_TIMEOUT_REQ,
	SIM_FRAME_END_DEVICE_TIMEOUT_RSP,
	SIM_FRAME_DEVICE_ANNOUNCE,
	SIM_FRAME_ACTIVE_EP_REQ,
	SIM_FRAME_ACTIVE_EP_RSP,
	SIM_FRAME_SIMPLE_DESC_REQ,
	SIM_FRAME_SIMPLE_DESC_RSP,
	SIM_FRAME_BASIC_MODEL_ID_READ,
	SIM_FRAME_BASIC_MODEL_ID_READ_RSP,
};

enum sim_device_state {
	SIM_DEVICE_FACTORY_NEW,
	SIM_DEVICE_JOINING,
	SIM_DEVICE_WAIT_TRANSPORT_KEY,
	SIM_DEVICE_JOINED_IDLE,
};

struct sim_frame {
	enum sim_frame_type type;
	uint16_t src;
	uint16_t dst;
	uint16_t assigned_short;
	uint8_t status;
	uint8_t seq;
	uint8_t wire[SIM_MAX_WIRE_LEN];
	size_t wire_len;
	char model_id[32];
};

struct sim_queue {
	struct sim_frame frames[32];
	size_t head;
	size_t count;
};

struct sim_device {
	enum sim_device_state state;
	uint64_t ieee;
	uint16_t short_addr;
	uint16_t parent_short;
	uint16_t active_parent_short;
	uint16_t pan_id;
	uint16_t active_pan_id;
	uint32_t poll_rate_ms;
	uint32_t next_poll_ms;
	uint32_t poll_tx_count;
	uint8_t endpoint;
	bool joined;
	bool have_transport_key;
	bool sent_timeout_req;
	bool sent_device_announce;
	bool interview_complete;
	char model_id[32];
};

struct sim_coordinator {
	bool permit_join;
	bool interview_enabled;
	bool interview_complete;
	bool got_device_announce;
	bool got_timeout_req;
	struct sim_frame last_rx;
	struct sim_frame model_id_rsp;
	uint16_t pan_id;
	uint16_t next_child_short;
	uint16_t child_short;
	char observed_model_id[32];
	struct sim_queue indirect;
};

struct sim {
	uint32_t now_ms;
	uint8_t next_seq;
	struct sim_device device;
	struct sim_coordinator coord;
};

static int failures;

#define EXPECT_TRUE(expr) do { \
	if (!(expr)) { \
		printf("FAIL %s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_FALSE(expr) do { \
	if (expr) { \
		printf("FAIL %s:%d: expected false: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

#define EXPECT_EQ(actual, expected) do { \
	long long _actual = (long long)(actual); \
	long long _expected = (long long)(expected); \
	if (_actual != _expected) { \
		printf("FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__, \
		       #actual, _actual, _expected); \
		failures++; \
	} \
} while (0)

#define EXPECT_STR_EQ(actual, expected) do { \
	if (strcmp((actual), (expected)) != 0) { \
		printf("FAIL %s:%d: %s=\"%s\" expected \"%s\"\n", __FILE__, __LINE__, \
		       #actual, (actual), (expected)); \
		failures++; \
	} \
} while (0)

static void put_le16(uint8_t *buf, uint16_t value)
{
	buf[0] = (uint8_t)value;
	buf[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *buf)
{
	return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static void put_le64(uint8_t *buf, uint64_t value)
{
	for (size_t i = 0; i < 8; i++) {
		buf[i] = (uint8_t)(value >> (8 * i));
	}
}

static uint64_t get_le64(const uint8_t *buf)
{
	uint64_t value = 0;

	for (size_t i = 0; i < 8; i++) {
		value |= (uint64_t)buf[i] << (8 * i);
	}

	return value;
}

static const char *sim_frame_name(enum sim_frame_type type)
{
	switch (type) {
	case SIM_FRAME_ASSOC_REQ:
		return "assoc_req";
	case SIM_FRAME_ASSOC_RSP:
		return "assoc_rsp";
	case SIM_FRAME_DATA_REQ:
		return "data_req";
	case SIM_FRAME_TRANSPORT_KEY:
		return "transport_key";
	case SIM_FRAME_END_DEVICE_TIMEOUT_REQ:
		return "end_device_timeout_req";
	case SIM_FRAME_END_DEVICE_TIMEOUT_RSP:
		return "end_device_timeout_rsp";
	case SIM_FRAME_DEVICE_ANNOUNCE:
		return "device_announce";
	case SIM_FRAME_ACTIVE_EP_REQ:
		return "active_ep_req";
	case SIM_FRAME_ACTIVE_EP_RSP:
		return "active_ep_rsp";
	case SIM_FRAME_SIMPLE_DESC_REQ:
		return "simple_desc_req";
	case SIM_FRAME_SIMPLE_DESC_RSP:
		return "simple_desc_rsp";
	case SIM_FRAME_BASIC_MODEL_ID_READ:
		return "basic_model_id_read";
	case SIM_FRAME_BASIC_MODEL_ID_READ_RSP:
		return "basic_model_id_read_rsp";
	}

	return "unknown";
}

static void sim_encode_mac_command(struct sim *sim, struct sim_frame *frame, uint8_t cmd)
{
	uint8_t *wire = frame->wire;

	put_le16(&wire[0], SIM_MAC_FCF_COMMAND_SHORT);
	wire[2] = sim->next_seq++;
	put_le16(&wire[3], sim->coord.pan_id);
	put_le16(&wire[5], frame->dst);
	put_le16(&wire[7], frame->src);
	wire[9] = cmd;
	frame->seq = wire[2];
	frame->wire_len = 10;
}

static void sim_encode_assoc_rsp(struct sim *sim, struct sim_frame *frame)
{
	sim_encode_mac_command(sim, frame, SIM_MAC_CMD_ASSOC_RSP);
	put_le16(&frame->wire[10], frame->assigned_short);
	frame->wire[12] = frame->status;
	frame->wire_len = 13;
}

static void sim_encode_data_frame(struct sim *sim, struct sim_frame *frame, uint16_t cluster,
				  uint8_t command, const uint8_t *payload, size_t payload_len)
{
	uint8_t *wire = frame->wire;
	size_t idx = 15;

	put_le16(&wire[0], 0x8861u);
	wire[2] = sim->next_seq++;
	put_le16(&wire[3], sim->coord.pan_id);
	put_le16(&wire[5], frame->dst);
	put_le16(&wire[7], frame->src);
	put_le16(&wire[9], SIM_PROFILE_HA);
	put_le16(&wire[11], cluster);
	wire[13] = 1;
	wire[14] = 1;
	wire[idx++] = 0x00;
	wire[idx++] = wire[2];
	wire[idx++] = command;
	if (payload_len > 0) {
		memcpy(&wire[idx], payload, payload_len);
		idx += payload_len;
	}
	frame->seq = wire[2];
	frame->wire_len = idx;
}

static void sim_frame_encode(struct sim *sim, struct sim_frame *frame)
{
	uint8_t payload[48];
	size_t len;

	memset(frame->wire, 0, sizeof(frame->wire));
	frame->wire_len = 0;

	switch (frame->type) {
	case SIM_FRAME_ASSOC_REQ:
		sim_encode_mac_command(sim, frame, SIM_MAC_CMD_ASSOC_REQ);
		put_le64(&frame->wire[10], sim->device.ieee);
		frame->wire_len = 18;
		break;
	case SIM_FRAME_ASSOC_RSP:
		sim_encode_assoc_rsp(sim, frame);
		break;
	case SIM_FRAME_DATA_REQ:
		sim_encode_mac_command(sim, frame, SIM_MAC_CMD_DATA_REQ);
		break;
	case SIM_FRAME_TRANSPORT_KEY:
		payload[0] = 0x05;
		sim_encode_data_frame(sim, frame, 0x0038u, 0x05, payload, 1);
		break;
	case SIM_FRAME_END_DEVICE_TIMEOUT_REQ:
		payload[0] = SIM_NWK_TIMEOUT_REQ;
		payload[1] = 0x08;
		sim_encode_data_frame(sim, frame, SIM_CLUSTER_NWK_MGMT, SIM_NWK_TIMEOUT_REQ,
				      payload, 2);
		break;
	case SIM_FRAME_END_DEVICE_TIMEOUT_RSP:
		payload[0] = SIM_NWK_TIMEOUT_RSP;
		payload[1] = 0x00;
		payload[2] = 0x02;
		sim_encode_data_frame(sim, frame, SIM_CLUSTER_NWK_MGMT, SIM_NWK_TIMEOUT_RSP,
				      payload, 3);
		break;
	case SIM_FRAME_DEVICE_ANNOUNCE:
		put_le16(&payload[0], frame->src);
		put_le64(&payload[2], sim->device.ieee);
		payload[10] = 0x80;
		sim_encode_data_frame(sim, frame, SIM_ZDO_DEVICE_ANNCE, 0x00, payload, 11);
		break;
	case SIM_FRAME_ACTIVE_EP_REQ:
		put_le16(&payload[0], frame->dst);
		sim_encode_data_frame(sim, frame, SIM_ZDO_ACTIVE_EP_REQ, 0x00, payload, 2);
		break;
	case SIM_FRAME_ACTIVE_EP_RSP:
		payload[0] = 0x00;
		put_le16(&payload[1], frame->src);
		payload[3] = 1;
		payload[4] = 1;
		sim_encode_data_frame(sim, frame, SIM_ZDO_ACTIVE_EP_RSP, 0x00, payload, 5);
		break;
	case SIM_FRAME_SIMPLE_DESC_REQ:
		put_le16(&payload[0], frame->dst);
		payload[2] = 1;
		sim_encode_data_frame(sim, frame, SIM_ZDO_SIMPLE_DESC_REQ, 0x00, payload, 3);
		break;
	case SIM_FRAME_SIMPLE_DESC_RSP:
		payload[0] = 0x00;
		put_le16(&payload[1], frame->src);
		payload[3] = 8;
		payload[4] = 1;
		put_le16(&payload[5], SIM_PROFILE_HA);
		put_le16(&payload[7], 0x0100u);
		payload[9] = 0;
		payload[10] = 1;
		put_le16(&payload[11], SIM_CLUSTER_BASIC);
		sim_encode_data_frame(sim, frame, SIM_ZDO_SIMPLE_DESC_RSP, 0x00, payload, 13);
		break;
	case SIM_FRAME_BASIC_MODEL_ID_READ:
		put_le16(&payload[0], SIM_ZCL_ATTR_MODEL_ID);
		sim_encode_data_frame(sim, frame, SIM_CLUSTER_BASIC, SIM_ZCL_CMD_READ, payload, 2);
		break;
	case SIM_FRAME_BASIC_MODEL_ID_READ_RSP:
		len = strlen(frame->model_id);
		put_le16(&payload[0], SIM_ZCL_ATTR_MODEL_ID);
		payload[2] = 0x00;
		payload[3] = SIM_ZCL_TYPE_CHAR_STRING;
		payload[4] = (uint8_t)len;
		memcpy(&payload[5], frame->model_id, len);
		sim_encode_data_frame(sim, frame, SIM_CLUSTER_BASIC, SIM_ZCL_CMD_READ_RSP,
				      payload, len + 5);
		break;
	}
}

static bool sim_decode_mac_command(const uint8_t *wire, size_t len, struct sim_frame *frame)
{
	uint8_t cmd;

	if (len < 10 || get_le16(&wire[0]) != SIM_MAC_FCF_COMMAND_SHORT) {
		return false;
	}

	cmd = wire[9];
	frame->seq = wire[2];
	frame->dst = get_le16(&wire[5]);
	frame->src = get_le16(&wire[7]);

	switch (cmd) {
	case SIM_MAC_CMD_ASSOC_REQ:
		if (len < 18 || get_le64(&wire[10]) != SIM_DEVICE_IEEE) {
			return false;
		}
		frame->type = SIM_FRAME_ASSOC_REQ;
		return true;
	case SIM_MAC_CMD_ASSOC_RSP:
		if (len < 13) {
			return false;
		}
		frame->type = SIM_FRAME_ASSOC_RSP;
		frame->assigned_short = get_le16(&wire[10]);
		frame->status = wire[12];
		return true;
	case SIM_MAC_CMD_DATA_REQ:
		frame->type = SIM_FRAME_DATA_REQ;
		return true;
	default:
		return false;
	}
}

static bool sim_decode_model_id_rsp(const uint8_t *payload, size_t len, struct sim_frame *frame)
{
	uint8_t str_len;

	if (len < 5 || get_le16(&payload[0]) != SIM_ZCL_ATTR_MODEL_ID ||
	    payload[2] != 0x00 || payload[3] != SIM_ZCL_TYPE_CHAR_STRING) {
		return false;
	}

	str_len = payload[4];
	if ((size_t)str_len + 5 > len || str_len >= sizeof(frame->model_id)) {
		return false;
	}

	memcpy(frame->model_id, &payload[5], str_len);
	frame->model_id[str_len] = '\0';
	return true;
}

static bool sim_frame_decode(const uint8_t *wire, size_t len, struct sim_frame *frame)
{
	uint16_t cluster;
	uint8_t command;
	const uint8_t *payload;
	size_t payload_len;

	memset(frame, 0, sizeof(*frame));
	if (len > sizeof(frame->wire)) {
		return false;
	}
	memcpy(frame->wire, wire, len);
	frame->wire_len = len;

	if (sim_decode_mac_command(wire, len, frame)) {
		return true;
	}

	if (len < 18 || get_le16(&wire[0]) != 0x8861u) {
		return false;
	}

	frame->seq = wire[2];
	frame->dst = get_le16(&wire[5]);
	frame->src = get_le16(&wire[7]);
	cluster = get_le16(&wire[11]);
	command = wire[17];
	payload = &wire[18];
	payload_len = len - 18;

	switch (cluster) {
	case 0x0038u:
		frame->type = SIM_FRAME_TRANSPORT_KEY;
		return command == 0x05;
	case SIM_CLUSTER_NWK_MGMT:
		if (command == SIM_NWK_TIMEOUT_REQ) {
			frame->type = SIM_FRAME_END_DEVICE_TIMEOUT_REQ;
			return payload_len >= 2 && payload[0] == SIM_NWK_TIMEOUT_REQ;
		}
		if (command == SIM_NWK_TIMEOUT_RSP) {
			frame->type = SIM_FRAME_END_DEVICE_TIMEOUT_RSP;
			return payload_len >= 3 && payload[0] == SIM_NWK_TIMEOUT_RSP;
		}
		return false;
	case SIM_ZDO_DEVICE_ANNCE:
		frame->type = SIM_FRAME_DEVICE_ANNOUNCE;
		return payload_len >= 11 && get_le16(&payload[0]) == frame->src &&
		       get_le64(&payload[2]) == SIM_DEVICE_IEEE;
	case SIM_ZDO_ACTIVE_EP_REQ:
		frame->type = SIM_FRAME_ACTIVE_EP_REQ;
		return payload_len >= 2 && get_le16(&payload[0]) == frame->dst;
	case SIM_ZDO_ACTIVE_EP_RSP:
		frame->type = SIM_FRAME_ACTIVE_EP_RSP;
		return payload_len >= 5 && payload[0] == 0x00 && payload[3] == 1 && payload[4] == 1;
	case SIM_ZDO_SIMPLE_DESC_REQ:
		frame->type = SIM_FRAME_SIMPLE_DESC_REQ;
		return payload_len >= 3 && get_le16(&payload[0]) == frame->dst && payload[2] == 1;
	case SIM_ZDO_SIMPLE_DESC_RSP:
		frame->type = SIM_FRAME_SIMPLE_DESC_RSP;
		return payload_len >= 13 && payload[0] == 0x00 &&
		       get_le16(&payload[5]) == SIM_PROFILE_HA &&
		       get_le16(&payload[11]) == SIM_CLUSTER_BASIC;
	case SIM_CLUSTER_BASIC:
		if (command == SIM_ZCL_CMD_READ) {
			frame->type = SIM_FRAME_BASIC_MODEL_ID_READ;
			return payload_len >= 2 && get_le16(&payload[0]) == SIM_ZCL_ATTR_MODEL_ID;
		}
		if (command == SIM_ZCL_CMD_READ_RSP) {
			frame->type = SIM_FRAME_BASIC_MODEL_ID_READ_RSP;
			return sim_decode_model_id_rsp(payload, payload_len, frame);
		}
		return false;
	default:
		return false;
	}
}

static void sim_queue_push(struct sim_queue *queue, const struct sim_frame *frame)
{
	size_t pos;

	if (queue->count >= (sizeof(queue->frames) / sizeof(queue->frames[0]))) {
		printf("FAIL queue overflow while pushing %s\n", sim_frame_name(frame->type));
		failures++;
		return;
	}

	pos = (queue->head + queue->count) % (sizeof(queue->frames) / sizeof(queue->frames[0]));
	queue->frames[pos] = *frame;
	queue->count++;
}

static bool sim_queue_pop_for_dst(struct sim_queue *queue, uint16_t dst, struct sim_frame *frame)
{
	size_t capacity = sizeof(queue->frames) / sizeof(queue->frames[0]);

	for (size_t i = 0; i < queue->count; i++) {
		size_t pos = (queue->head + i) % capacity;

		if (queue->frames[pos].dst != dst) {
			continue;
		}

		*frame = queue->frames[pos];
		for (size_t j = i; j + 1 < queue->count; j++) {
			size_t cur = (queue->head + j) % capacity;
			size_t next = (queue->head + j + 1) % capacity;

			queue->frames[cur] = queue->frames[next];
		}
		queue->count--;
		return true;
	}

	return false;
}

static void sim_device_init(struct sim_device *device)
{
	memset(device, 0, sizeof(*device));
	device->state = SIM_DEVICE_FACTORY_NEW;
	device->ieee = SIM_DEVICE_IEEE;
	device->short_addr = SIM_NO_SHORT_ADDR;
	device->parent_short = SIM_NO_SHORT_ADDR;
	device->active_parent_short = SIM_NO_SHORT_ADDR;
	device->pan_id = SIM_NO_SHORT_ADDR;
	device->active_pan_id = SIM_NO_SHORT_ADDR;
	device->poll_rate_ms = SIM_POLL_RATE_MS;
	device->endpoint = 1;
	strcpy(device->model_id, "tlsr8258-minimal");
}

static void sim_coordinator_init(struct sim_coordinator *coord)
{
	memset(coord, 0, sizeof(*coord));
	coord->pan_id = SIM_PAN_ID;
	coord->next_child_short = 0x2700u;
	coord->child_short = SIM_NO_SHORT_ADDR;
	coord->interview_enabled = true;
}

static void sim_init(struct sim *sim)
{
	memset(sim, 0, sizeof(*sim));
	sim_device_init(&sim->device);
	sim_coordinator_init(&sim->coord);
}

static void sim_tx_to_device(struct sim *sim, enum sim_frame_type type)
{
	struct sim_frame frame = {
		.type = type,
		.src = SIM_COORD_SHORT_ADDR,
		.dst = sim->coord.child_short,
	};

	sim_frame_encode(sim, &frame);
	sim_queue_push(&sim->coord.indirect, &frame);
}

static void sim_coord_start_interview(struct sim *sim)
{
	if (!sim->coord.interview_enabled) {
		return;
	}

	sim_tx_to_device(sim, SIM_FRAME_ACTIVE_EP_REQ);
}

static void sim_coord_receive(struct sim *sim, const struct sim_frame *frame);
static void sim_device_receive(struct sim *sim, const struct sim_frame *frame);

static void sim_coord_receive_bytes(struct sim *sim, const uint8_t *wire, size_t wire_len)
{
	struct sim_frame frame;

	if (!sim_frame_decode(wire, wire_len, &frame)) {
		printf("FAIL coordinator received undecodable frame len=%zu\n", wire_len);
		failures++;
		return;
	}

	sim_coord_receive(sim, &frame);
}

static void sim_device_receive_bytes(struct sim *sim, const uint8_t *wire, size_t wire_len)
{
	struct sim_frame frame;

	if (!sim_frame_decode(wire, wire_len, &frame)) {
		printf("FAIL device received undecodable frame len=%zu\n", wire_len);
		failures++;
		return;
	}

	sim_device_receive(sim, &frame);
}

static void sim_device_send(struct sim *sim, enum sim_frame_type type)
{
	struct sim_frame frame = {
		.type = type,
		.src = sim->device.short_addr,
		.dst = SIM_COORD_SHORT_ADDR,
	};

	if (type == SIM_FRAME_DATA_REQ) {
		sim->device.poll_tx_count++;
	}
	sim_frame_encode(sim, &frame);
	sim_coord_receive_bytes(sim, frame.wire, frame.wire_len);
}

static void sim_coord_receive(struct sim *sim, const struct sim_frame *frame)
{
	sim->coord.last_rx = *frame;

	switch (frame->type) {
	case SIM_FRAME_ASSOC_REQ: {
		struct sim_frame rsp = {
			.type = SIM_FRAME_ASSOC_RSP,
			.src = SIM_COORD_SHORT_ADDR,
			.dst = SIM_NO_SHORT_ADDR,
			.status = sim->coord.permit_join ? 0 : 1,
			.assigned_short = sim->coord.permit_join ? sim->coord.next_child_short :
								  SIM_NO_SHORT_ADDR,
		};

		if (sim->coord.permit_join) {
			sim->coord.child_short = rsp.assigned_short;
			sim->coord.next_child_short++;
		}

		sim_frame_encode(sim, &rsp);
		sim_device_receive_bytes(sim, rsp.wire, rsp.wire_len);
		break;
	}
	case SIM_FRAME_DATA_REQ: {
		struct sim_frame pending;

		if (sim_queue_pop_for_dst(&sim->coord.indirect, frame->src, &pending)) {
			sim_device_receive_bytes(sim, pending.wire, pending.wire_len);
		}
		break;
	}
	case SIM_FRAME_END_DEVICE_TIMEOUT_REQ:
		sim->coord.got_timeout_req = true;
		sim_tx_to_device(sim, SIM_FRAME_END_DEVICE_TIMEOUT_RSP);
		break;
	case SIM_FRAME_DEVICE_ANNOUNCE:
		sim->coord.got_device_announce = true;
		sim_coord_start_interview(sim);
		break;
	case SIM_FRAME_ACTIVE_EP_RSP:
		sim_tx_to_device(sim, SIM_FRAME_SIMPLE_DESC_REQ);
		break;
	case SIM_FRAME_SIMPLE_DESC_RSP:
		sim_tx_to_device(sim, SIM_FRAME_BASIC_MODEL_ID_READ);
		break;
	case SIM_FRAME_BASIC_MODEL_ID_READ_RSP:
		sim->coord.model_id_rsp = *frame;
		strcpy(sim->coord.observed_model_id, frame->model_id);
		sim->coord.interview_complete = true;
		break;
	default:
		break;
	}
}

static void sim_device_receive(struct sim *sim, const struct sim_frame *frame)
{
	switch (frame->type) {
	case SIM_FRAME_ASSOC_RSP:
		if (sim->device.state != SIM_DEVICE_JOINING || frame->status != 0) {
			sim->device.state = SIM_DEVICE_FACTORY_NEW;
			return;
		}

		sim->device.short_addr = frame->assigned_short;
		sim->device.parent_short = SIM_COORD_SHORT_ADDR;
		sim->device.active_parent_short = SIM_COORD_SHORT_ADDR;
		sim->device.pan_id = sim->coord.pan_id;
		sim->device.active_pan_id = sim->coord.pan_id;
		sim->device.state = SIM_DEVICE_WAIT_TRANSPORT_KEY;
		sim->device.next_poll_ms = sim->now_ms + 200u;
		sim->coord.child_short = frame->assigned_short;
		sim_tx_to_device(sim, SIM_FRAME_TRANSPORT_KEY);
		break;
	case SIM_FRAME_TRANSPORT_KEY:
		if (sim->device.state != SIM_DEVICE_WAIT_TRANSPORT_KEY) {
			break;
		}
		sim->device.have_transport_key = true;
		sim->device.joined = true;
		sim->device.state = SIM_DEVICE_JOINED_IDLE;
		sim->device.next_poll_ms = sim->now_ms + sim->device.poll_rate_ms;
		sim_device_send(sim, SIM_FRAME_END_DEVICE_TIMEOUT_REQ);
		sim->device.sent_timeout_req = true;
		sim_device_send(sim, SIM_FRAME_DEVICE_ANNOUNCE);
		sim->device.sent_device_announce = true;
		break;
	case SIM_FRAME_END_DEVICE_TIMEOUT_RSP:
		break;
	case SIM_FRAME_ACTIVE_EP_REQ:
		sim_device_send(sim, SIM_FRAME_ACTIVE_EP_RSP);
		break;
	case SIM_FRAME_SIMPLE_DESC_REQ:
		sim_device_send(sim, SIM_FRAME_SIMPLE_DESC_RSP);
		break;
	case SIM_FRAME_BASIC_MODEL_ID_READ: {
		struct sim_frame rsp = {
			.type = SIM_FRAME_BASIC_MODEL_ID_READ_RSP,
			.src = sim->device.short_addr,
			.dst = SIM_COORD_SHORT_ADDR,
		};

		strcpy(rsp.model_id, sim->device.model_id);
		sim_frame_encode(sim, &rsp);
		sim_coord_receive_bytes(sim, rsp.wire, rsp.wire_len);
		sim->device.interview_complete = true;
		break;
	}
	default:
		break;
	}
}

static void sim_device_start_commissioning(struct sim *sim)
{
	sim->device.state = SIM_DEVICE_JOINING;
	sim_device_send(sim, SIM_FRAME_ASSOC_REQ);
}

static void sim_device_restore_joined(struct sim *sim, uint16_t short_addr)
{
	sim->device.short_addr = short_addr;
	sim->device.parent_short = SIM_COORD_SHORT_ADDR;
	sim->device.active_parent_short = SIM_COORD_SHORT_ADDR;
	sim->device.pan_id = sim->coord.pan_id;
	sim->device.active_pan_id = sim->coord.pan_id;
	sim->device.state = SIM_DEVICE_JOINED_IDLE;
	sim->device.joined = true;
	sim->device.have_transport_key = true;
	sim->device.next_poll_ms = sim->now_ms + sim->device.poll_rate_ms;
	sim->coord.child_short = short_addr;
}

static bool sim_device_has_restorable_join_context(const struct sim *sim)
{
	return sim->device.state == SIM_DEVICE_JOINED_IDLE &&
	       sim->device.pan_id == sim->coord.pan_id &&
	       sim->device.short_addr != SIM_NO_SHORT_ADDR &&
	       sim->device.parent_short == SIM_COORD_SHORT_ADDR &&
	       sim->device.have_transport_key;
}

static void sim_device_repair_joined_context_if_needed(struct sim *sim)
{
	if (sim->device.joined || !sim_device_has_restorable_join_context(sim)) {
		return;
	}

	sim->device.active_pan_id = sim->device.pan_id;
	sim->device.active_parent_short = sim->device.parent_short;
	sim->device.joined = true;
}

static void sim_device_poll_ensure(struct sim *sim)
{
	if (sim->device.state != SIM_DEVICE_JOINED_IDLE) {
		return;
	}

	sim_device_repair_joined_context_if_needed(sim);
	if (sim->device.joined && sim->device.next_poll_ms == SIM_POLL_STOPPED) {
		sim->device.next_poll_ms = sim->now_ms + 1u;
	}
}

static void sim_device_leave_reset(struct sim *sim)
{
	sim->device.state = SIM_DEVICE_FACTORY_NEW;
	sim->device.short_addr = SIM_NO_SHORT_ADDR;
	sim->device.parent_short = SIM_NO_SHORT_ADDR;
	sim->device.active_parent_short = SIM_NO_SHORT_ADDR;
	sim->device.pan_id = SIM_NO_SHORT_ADDR;
	sim->device.active_pan_id = SIM_NO_SHORT_ADDR;
	sim->device.next_poll_ms = 0;
	sim->device.joined = false;
	sim->device.have_transport_key = false;
	sim->device.sent_timeout_req = false;
	sim->device.sent_device_announce = false;
	sim->device.interview_complete = false;
	sim->coord.child_short = SIM_NO_SHORT_ADDR;
	sim->coord.indirect.head = 0;
	sim->coord.indirect.count = 0;
}

static void sim_device_poll_if_due(struct sim *sim)
{
	if ((sim->device.state != SIM_DEVICE_JOINED_IDLE &&
	     sim->device.state != SIM_DEVICE_WAIT_TRANSPORT_KEY) ||
	    sim->device.next_poll_ms == SIM_POLL_STOPPED ||
	    sim->now_ms < sim->device.next_poll_ms) {
		return;
	}

	if (sim->device.state == SIM_DEVICE_JOINED_IDLE) {
		sim_device_repair_joined_context_if_needed(sim);
		if (!sim->device.joined ||
		    sim->device.active_pan_id == SIM_NO_SHORT_ADDR ||
		    sim->device.active_parent_short == SIM_NO_SHORT_ADDR) {
			return;
		}
	}

	sim_device_send(sim, SIM_FRAME_DATA_REQ);
	sim->device.next_poll_ms += sim->device.poll_rate_ms;
}

static void sim_advance_ms(struct sim *sim, uint32_t delta_ms)
{
	uint32_t target = sim->now_ms + delta_ms;

	while (sim->now_ms < target) {
		sim->now_ms++;
		sim_device_poll_ensure(sim);
		sim_device_poll_if_due(sim);
	}
}

static void test_permit_join_interview_success(void)
{
	struct sim sim;

	sim_init(&sim);
	sim.coord.permit_join = true;

	sim_device_start_commissioning(&sim);
	sim_advance_ms(&sim, 7000);

	EXPECT_TRUE(sim.device.joined);
	EXPECT_EQ(sim.device.state, SIM_DEVICE_JOINED_IDLE);
	EXPECT_EQ(sim.device.short_addr, 0x2700);
	EXPECT_TRUE(sim.device.sent_timeout_req);
	EXPECT_TRUE(sim.coord.got_timeout_req);
	EXPECT_TRUE(sim.device.sent_device_announce);
	EXPECT_TRUE(sim.coord.got_device_announce);
	EXPECT_TRUE(sim.device.interview_complete);
	EXPECT_TRUE(sim.coord.interview_complete);
	EXPECT_STR_EQ(sim.coord.observed_model_id, "tlsr8258-minimal");
	EXPECT_EQ(sim.coord.model_id_rsp.type, SIM_FRAME_BASIC_MODEL_ID_READ_RSP);
	EXPECT_EQ(get_le16(&sim.coord.model_id_rsp.wire[11]), SIM_CLUSTER_BASIC);
	EXPECT_EQ(sim.coord.model_id_rsp.wire[17], SIM_ZCL_CMD_READ_RSP);
	EXPECT_EQ(get_le16(&sim.coord.model_id_rsp.wire[18]), SIM_ZCL_ATTR_MODEL_ID);
}

static void test_permit_join_disabled_rejects_association(void)
{
	struct sim sim;

	sim_init(&sim);
	sim.coord.permit_join = false;

	sim_device_start_commissioning(&sim);
	sim_advance_ms(&sim, 5000);

	EXPECT_FALSE(sim.device.joined);
	EXPECT_EQ(sim.device.state, SIM_DEVICE_FACTORY_NEW);
	EXPECT_EQ(sim.coord.child_short, SIM_NO_SHORT_ADDR);
	EXPECT_FALSE(sim.coord.interview_complete);
}

static void test_restore_joined_polls_indirect_interview(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_device_restore_joined(&sim, 0x3344);

	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_advance_ms(&sim, 5000);

	EXPECT_TRUE(sim.device.joined);
	EXPECT_EQ(sim.device.short_addr, 0x3344);
	EXPECT_TRUE(sim.device.interview_complete);
	EXPECT_TRUE(sim.coord.interview_complete);
	EXPECT_STR_EQ(sim.coord.observed_model_id, "tlsr8258-minimal");
}

static void test_restored_split_joined_flag_recovers_polling(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_device_restore_joined(&sim, 0x4455);
	sim.device.joined = false;

	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_advance_ms(&sim, 5000);

	EXPECT_TRUE(sim.device.joined);
	EXPECT_TRUE(sim.device.interview_complete);
	EXPECT_TRUE(sim.coord.interview_complete);
	EXPECT_STR_EQ(sim.coord.observed_model_id, "tlsr8258-minimal");
}

static void test_restored_joined_stopped_poll_timer_recovers(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_device_restore_joined(&sim, 0x6655);
	sim.device.next_poll_ms = SIM_POLL_STOPPED;

	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_advance_ms(&sim, 5000);

	EXPECT_TRUE(sim.device.interview_complete);
	EXPECT_TRUE(sim.coord.interview_complete);
	EXPECT_STR_EQ(sim.coord.observed_model_id, "tlsr8258-minimal");
}

static void test_split_joined_flag_without_key_does_not_poll(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_device_restore_joined(&sim, 0x5566);
	sim.device.joined = false;
	sim.device.have_transport_key = false;

	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_advance_ms(&sim, 5000);

	EXPECT_FALSE(sim.device.joined);
	EXPECT_EQ(sim.coord.indirect.count, 1);
	EXPECT_FALSE(sim.coord.interview_complete);
}

static void test_post_interview_polling_continues(void)
{
	struct sim sim;
	uint32_t polls_after_interview;

	sim_init(&sim);
	sim.coord.permit_join = true;

	sim_device_start_commissioning(&sim);
	sim_advance_ms(&sim, 7000);
	polls_after_interview = sim.device.poll_tx_count;
	EXPECT_TRUE(sim.coord.interview_complete);

	memset(&sim.coord.model_id_rsp, 0, sizeof(sim.coord.model_id_rsp));
	sim_tx_to_device(&sim, SIM_FRAME_BASIC_MODEL_ID_READ);
	sim_advance_ms(&sim, 2500);

	EXPECT_EQ(sim.coord.model_id_rsp.type, SIM_FRAME_BASIC_MODEL_ID_READ_RSP);
	EXPECT_TRUE(sim.device.poll_tx_count > polls_after_interview);
}

static void test_leave_resets_join_state_filters_and_persistence_model(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_device_restore_joined(&sim, 0x3344);
	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_device_leave_reset(&sim);
	sim_advance_ms(&sim, 5000);

	EXPECT_FALSE(sim.device.joined);
	EXPECT_FALSE(sim.device.have_transport_key);
	EXPECT_EQ(sim.device.short_addr, SIM_NO_SHORT_ADDR);
	EXPECT_EQ(sim.device.active_pan_id, SIM_NO_SHORT_ADDR);
	EXPECT_EQ(sim.coord.indirect.count, 0);
	EXPECT_FALSE(sim.coord.interview_complete);
}

static void test_no_poll_before_restore_or_join(void)
{
	struct sim sim;

	sim_init(&sim);
	sim_tx_to_device(&sim, SIM_FRAME_ACTIVE_EP_REQ);
	sim_advance_ms(&sim, 5000);

	EXPECT_FALSE(sim.device.joined);
	EXPECT_EQ(sim.coord.indirect.count, 1);
	EXPECT_FALSE(sim.coord.interview_complete);
}

int main(void)
{
	test_permit_join_interview_success();
	test_permit_join_disabled_rejects_association();
	test_restore_joined_polls_indirect_interview();
	test_restored_split_joined_flag_recovers_polling();
	test_restored_joined_stopped_poll_timer_recovers();
	test_split_joined_flag_without_key_does_not_poll();
	test_post_interview_polling_continues();
	test_leave_resets_join_state_filters_and_persistence_model();
	test_no_poll_before_restore_or_join();

	if (failures != 0) {
		printf("host_sim: %d failure(s)\n", failures);
		return 1;
	}

	printf("host_sim: PASS\n");
	return 0;
}

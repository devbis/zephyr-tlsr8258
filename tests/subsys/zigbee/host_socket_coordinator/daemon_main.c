/* SPDX-License-Identifier: Apache-2.0 */

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <zephyr/zigbee/native_sim_socket_medium.h>
#include <zephyr/zigbee/native_sim_socket_medium_model.h>

#include "coord_logic.h"

#define DEFAULT_BIND_HOST "127.0.0.1"
#define DEFAULT_BIND_PORT 19011
#define DEFAULT_MODEL_ID  "native-sim-ed"
#define ZB_COORD_RX_TX_TURNAROUND_US 192U
#define ZB_COORD_CCA_BUSY_RSSI_DBM   (-60)
#define ZB_COORD_CCA_IDLE_RSSI_DBM   (-96)
#define ZB_COORD_RX_RSSI_DBM         (-40)
#define ZB_COORD_RX_LQI              255U
#define ZB_MEDIUM_MAX_PEERS          8U
#define ZB_MEDIUM_MAX_PENDING        16U

enum pending_delivery_kind {
	PENDING_DELIVERY_RX = 0,
	PENDING_DELIVERY_TX_STATUS = 1,
};

struct daemon_config {
	const char *bind_host;
	const char *model_id;
	uint16_t bind_port;
	bool permit_join;
};

struct medium_peer_entry {
	bool valid;
	struct sockaddr_in addr;
	struct zb_native_sim_socket_medium_peer peer;
};

struct pending_delivery {
	bool valid;
	enum pending_delivery_kind kind;
	uint16_t node_id;
	uint8_t channel;
	uint64_t tx_start_us;
	uint64_t due_time_us;
	size_t psdu_len;
	enum zb_host_socket_frame_type frame_type;
	bool tx_result_collision;
	struct sockaddr_in peer_addr;
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
};

static const char *medium_msg_type_str(enum zb_native_sim_socket_medium_msg_type type)
{
	switch (type) {
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO:
		return "HELLO";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER:
		return "FILTER";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX:
		return "TX";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX:
		return "RX";
	case ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS:
		return "STATUS";
	default:
		return "UNKNOWN";
	}
}

static const char *frame_type_str(enum zb_host_socket_frame_type type)
{
	switch (type) {
	case ZB_HOST_SOCKET_FRAME_ASSOC_REQ:
		return "ASSOC_REQ";
	case ZB_HOST_SOCKET_FRAME_ASSOC_RSP:
		return "ASSOC_RSP";
	case ZB_HOST_SOCKET_FRAME_DATA_REQ:
		return "DATA_REQ";
	case ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY:
		return "TRANSPORT_KEY";
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ:
		return "TIMEOUT_REQ";
	case ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP:
		return "TIMEOUT_RSP";
	case ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE:
		return "DEVICE_ANNOUNCE";
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ:
		return "ACTIVE_EP_REQ";
	case ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP:
		return "ACTIVE_EP_RSP";
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ:
		return "SIMPLE_DESC_REQ";
	case ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP:
		return "SIMPLE_DESC_RSP";
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ:
		return "BASIC_MODEL_ID_READ";
	case ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP:
		return "BASIC_MODEL_ID_READ_RSP";
	default:
		return "UNKNOWN";
	}
}

static void dump_psdu_hex(const uint8_t *psdu, size_t len)
{
	if (psdu == NULL || len == 0U) {
		return;
	}

	fputs(" psdu=", stderr);
	for (size_t i = 0U; i < len; i++) {
		fprintf(stderr, "%02x", psdu[i]);
	}
}

static void usage(FILE *stream, const char *argv0)
{
	fprintf(stream,
		"Usage: %s [--bind-host ip] [--bind-port port] [--model-id str]\n"
		"          [--permit-join|--deny-join] [--help]\n",
		argv0);
}

static int parse_u16(const char *value, uint16_t *out)
{
	char *end = NULL;
	unsigned long parsed;

	if (value == NULL || out == NULL) {
		return -EINVAL;
	}

	errno = 0;
	parsed = strtoul(value, &end, 0);
	if (errno != 0 || end == value || *end != '\0' || parsed > UINT16_MAX) {
		return -EINVAL;
	}

	*out = (uint16_t)parsed;
	return 0;
}

static int parse_args(int argc, char **argv, struct daemon_config *cfg)
{
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "--bind-host") == 0) {
			if (++i >= argc) {
				return -EINVAL;
			}
			cfg->bind_host = argv[i];
		} else if (strcmp(arg, "--bind-port") == 0) {
			if (++i >= argc || parse_u16(argv[i], &cfg->bind_port) < 0) {
				return -EINVAL;
			}
		} else if (strcmp(arg, "--model-id") == 0) {
			if (++i >= argc) {
				return -EINVAL;
			}
			cfg->model_id = argv[i];
		} else if (strcmp(arg, "--permit-join") == 0) {
			cfg->permit_join = true;
		} else if (strcmp(arg, "--deny-join") == 0) {
			cfg->permit_join = false;
		} else if (strcmp(arg, "--help") == 0) {
			usage(stdout, argv[0]);
			return 1;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

static int open_server_socket(const struct daemon_config *cfg)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(cfg->bind_port),
	};
	int fd;
	int enable = 1;

	if (inet_pton(AF_INET, cfg->bind_host, &addr.sin_addr) != 1) {
		return -EINVAL;
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -errno;
	}

	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;

		(void)close(fd);
		return -err;
	}

	return fd;
}

static bool same_endpoint(const struct sockaddr_in *a, const struct sockaddr_in *b)
{
	return a->sin_family == b->sin_family &&
	       a->sin_port == b->sin_port &&
	       a->sin_addr.s_addr == b->sin_addr.s_addr;
}

static struct medium_peer_entry *find_peer_by_endpoint(struct medium_peer_entry *peers,
						       size_t peer_count,
						       const struct sockaddr_in *addr)
{
	for (size_t i = 0; i < peer_count; i++) {
		if (peers[i].valid && same_endpoint(&peers[i].addr, addr)) {
			return &peers[i];
		}
	}

	return NULL;
}

static struct medium_peer_entry *alloc_peer_entry(struct medium_peer_entry *peers, size_t peer_count)
{
	for (size_t i = 0; i < peer_count; i++) {
		if (!peers[i].valid) {
			peers[i].valid = true;
			zb_native_sim_socket_medium_peer_reset(&peers[i].peer);
			return &peers[i];
		}
	}

	return NULL;
}

static int update_peer_entry(struct medium_peer_entry *peers, size_t peer_count,
			     const struct sockaddr_in *addr,
			     const struct zb_native_sim_socket_medium_msg *msg)
{
	struct medium_peer_entry *entry;
	int rc;

	entry = find_peer_by_endpoint(peers, peer_count, addr);
	if (entry == NULL) {
		entry = alloc_peer_entry(peers, peer_count);
		if (entry == NULL) {
			return -ENOSPC;
		}
	}

	entry->addr = *addr;
	rc = zb_native_sim_socket_medium_peer_apply(&entry->peer, msg);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int send_output(int fd, const struct sockaddr_in *peer_addr,
		       const struct zb_native_sim_socket_medium_msg *output)
{
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
	int rc;

	if (output == NULL) {
		return 0;
	}

	rc = zb_native_sim_socket_medium_encode(packet, sizeof(packet), output, &packet_len);
	if (rc < 0) {
		return rc;
	}

	if (output->type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX) {
		fprintf(stderr,
			"socket coordinator: reply %s frame=%s node=0x%04x ch=%u len=%zu\n",
			medium_msg_type_str(output->type),
			frame_type_str(zb_host_socket_coord_identify_frame(output->psdu,
									 output->psdu_len)),
			output->node_id, output->channel, output->psdu_len);
		dump_psdu_hex(output->psdu, output->psdu_len);
		fputc('\n', stderr);
	} else {
		fprintf(stderr,
			"socket coordinator: reply %s node=0x%04x ch=%u len=%zu\n",
			medium_msg_type_str(output->type), output->node_id,
			output->channel, output->psdu_len);
	}

	rc = (int)sendto(fd, packet, packet_len, 0,
			 (const struct sockaddr *)peer_addr, sizeof(*peer_addr));
	if (rc < 0) {
		return -errno;
	}

	return 0;
}

static uint64_t monotonic_time_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

static bool ranges_overlap(uint64_t start_a, uint64_t end_a, uint64_t start_b, uint64_t end_b)
{
	return start_a < end_b && start_b < end_a;
}

static int poll_timeout_ms(const struct pending_delivery *pending, size_t pending_count,
			   uint64_t now_us)
{
	uint64_t earliest_due_us = 0U;
	bool found = false;

	if (pending == NULL) {
		return -1;
	}

	for (size_t i = 0; i < pending_count; i++) {
		if (!pending[i].valid) {
			continue;
		}

		if (!found || pending[i].due_time_us < earliest_due_us) {
			earliest_due_us = pending[i].due_time_us;
			found = true;
		}
	}

	if (!found) {
		return -1;
	}

	if (earliest_due_us <= now_us) {
		return 0;
	}

	return (int)(((earliest_due_us - now_us) + 999ULL) / 1000ULL);
}

static void clear_pending_delivery(struct pending_delivery *pending)
{
	if (pending == NULL) {
		return;
	}

	memset(pending, 0, sizeof(*pending));
}

static struct pending_delivery *alloc_pending_delivery(struct pending_delivery *pending,
						       size_t pending_count)
{
	for (size_t i = 0; i < pending_count; i++) {
		if (!pending[i].valid) {
			return &pending[i];
		}
	}

	return NULL;
}

static int schedule_pending_output(struct pending_delivery *pending, size_t pending_count,
				   const struct sockaddr_in *peer_addr,
				   const struct zb_native_sim_socket_medium_msg *output,
				   uint64_t tx_start_us, uint64_t due_time_us,
				   enum pending_delivery_kind kind,
				   enum zb_host_socket_frame_type frame_type,
				   bool tx_result_collision)
{
	struct pending_delivery *slot;
	int rc;

	if (pending == NULL || peer_addr == NULL || output == NULL) {
		return -EINVAL;
	}

	slot = alloc_pending_delivery(pending, pending_count);
	if (slot == NULL) {
		return -ENOSPC;
	}

	rc = zb_native_sim_socket_medium_encode(slot->packet, sizeof(slot->packet), output,
						&slot->packet_len);
	if (rc < 0) {
		return rc;
	}

	slot->valid = true;
	slot->kind = kind;
	slot->node_id = output->node_id;
	slot->channel = output->channel;
	slot->tx_start_us = tx_start_us;
	slot->due_time_us = due_time_us;
	slot->psdu_len = output->psdu_len;
	slot->frame_type = frame_type;
	slot->tx_result_collision = tx_result_collision;
	slot->peer_addr = *peer_addr;
	return 0;
}

static int schedule_reply(struct pending_delivery *pending, size_t pending_count,
			  struct zb_native_sim_socket_medium_model *medium,
			  const struct sockaddr_in *peer_addr,
			  const struct zb_native_sim_socket_medium_msg *output,
			  uint64_t input_end_us)
{
	uint64_t reply_start_us;
	uint64_t reply_end_us;
	enum zb_native_sim_socket_medium_window_result reserve_result;

	if (pending == NULL || medium == NULL || peer_addr == NULL || output == NULL) {
		return -EINVAL;
	}

	reply_start_us = input_end_us + ZB_COORD_RX_TX_TURNAROUND_US;
	reply_end_us = reply_start_us + zb_native_sim_socket_medium_airtime_us(output->psdu_len);
	reserve_result = zb_native_sim_socket_medium_model_reserve_window(
		medium, output->channel, reply_start_us, reply_end_us, NULL);
	if (reserve_result != ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK) {
		return -EAGAIN;
	}

	return schedule_pending_output(
		pending, pending_count, peer_addr, output, reply_start_us, reply_end_us,
		PENDING_DELIVERY_RX,
		zb_host_socket_coord_identify_frame(output->psdu, output->psdu_len), false);
}

static int build_tx_result_output(struct zb_native_sim_socket_medium_msg *output,
				  uint8_t *payload, size_t payload_capacity,
				  uint16_t node_id, uint8_t channel, bool collision)
{
	size_t payload_len = 0U;
	int rc;

	if (output == NULL || payload == NULL) {
		return -EINVAL;
	}

	rc = zb_native_sim_socket_medium_status_encode_tx_result_rsp(payload, payload_capacity,
								     collision, &payload_len);
	if (rc < 0) {
		return rc;
	}

	memset(output, 0, sizeof(*output));
	output->type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS;
	output->node_id = node_id;
	output->channel = channel;
	output->psdu = payload;
	output->psdu_len = payload_len;
	return 0;
}

static int send_tx_result(int fd, const struct sockaddr_in *peer_addr, uint16_t node_id,
			  uint8_t channel, bool collision)
{
	struct zb_native_sim_socket_medium_msg output;
	uint8_t payload[ZB_NATIVE_SIM_SOCKET_MEDIUM_STATUS_TX_RESULT_RSP_LEN];
	int rc;

	rc = build_tx_result_output(&output, payload, sizeof(payload), node_id, channel, collision);
	if (rc < 0) {
		return rc;
	}

	return send_output(fd, peer_addr, &output);
}

static int schedule_tx_result(struct pending_delivery *pending, size_t pending_count,
			      const struct sockaddr_in *peer_addr, uint16_t node_id,
			      uint8_t channel, uint64_t tx_start_us, uint64_t due_time_us,
			      bool collision)
{
	struct zb_native_sim_socket_medium_msg output;
	uint8_t payload[ZB_NATIVE_SIM_SOCKET_MEDIUM_STATUS_TX_RESULT_RSP_LEN];
	int rc;

	rc = build_tx_result_output(&output, payload, sizeof(payload), node_id, channel, collision);
	if (rc < 0) {
		return rc;
	}

	return schedule_pending_output(pending, pending_count, peer_addr, &output,
				       tx_start_us, due_time_us,
				       PENDING_DELIVERY_TX_STATUS,
				       ZB_HOST_SOCKET_FRAME_DATA_REQ, collision);
}

static int handle_status_request(int fd, const struct sockaddr_in *peer_addr,
				 struct zb_native_sim_socket_medium_model *medium,
				 const struct zb_native_sim_socket_medium_msg *input,
				 uint64_t now_us)
{
	struct zb_native_sim_socket_medium_msg output;
	uint8_t payload[ZB_NATIVE_SIM_SOCKET_MEDIUM_STATUS_CCA_RSP_LEN];
	size_t payload_len = 0U;
	bool busy;
	int rc;

	if (input == NULL || peer_addr == NULL || medium == NULL || fd < 0) {
		return -EINVAL;
	}

	if (!zb_native_sim_socket_medium_status_is_cca_req(input->psdu, input->psdu_len)) {
		return 0;
	}

	busy = zb_native_sim_socket_medium_model_channel_busy(medium, input->channel, now_us);
	rc = zb_native_sim_socket_medium_status_encode_cca_rsp(payload, sizeof(payload), busy,
							       &payload_len);
	if (rc < 0) {
		return rc;
	}

	memset(&output, 0, sizeof(output));
	output.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS;
	output.node_id = input->node_id;
	output.channel = input->channel;
	output.rssi_dbm = busy ? ZB_COORD_CCA_BUSY_RSSI_DBM : ZB_COORD_CCA_IDLE_RSSI_DBM;
	output.psdu = payload;
	output.psdu_len = payload_len;
	return send_output(fd, peer_addr, &output);
}

static int schedule_peer_fanout(struct pending_delivery *pending, size_t pending_count,
				const struct medium_peer_entry *peers, size_t peer_count,
				const struct sockaddr_in *sender_addr,
				const struct zb_native_sim_socket_medium_msg *input,
				uint64_t input_start_us, uint64_t input_end_us)
{
	struct zb_native_sim_socket_medium_msg output;

	if (pending == NULL || peers == NULL || sender_addr == NULL || input == NULL) {
		return -EINVAL;
	}

	for (size_t i = 0; i < peer_count; i++) {
		if (!peers[i].valid || same_endpoint(&peers[i].addr, sender_addr)) {
			continue;
		}

		if (peers[i].peer.channel != input->channel ||
		    !zb_native_sim_socket_medium_peer_accepts_psdu(&peers[i].peer,
								   input->psdu, input->psdu_len)) {
			continue;
		}

		memset(&output, 0, sizeof(output));
		output.type = ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX;
		output.node_id = peers[i].peer.node_id;
		output.channel = input->channel;
		output.rssi_dbm = ZB_COORD_RX_RSSI_DBM;
		output.lqi = ZB_COORD_RX_LQI;
		output.psdu = input->psdu;
		output.psdu_len = input->psdu_len;

		if (schedule_pending_output(pending, pending_count, &peers[i].addr, &output,
					    input_start_us, input_end_us,
					    PENDING_DELIVERY_RX,
					    zb_host_socket_coord_identify_frame(input->psdu,
										 input->psdu_len),
					    false) < 0) {
			return -ENOSPC;
		}
	}

	return 0;
}

static int maybe_send_pending(int fd, struct pending_delivery *pending, size_t pending_count,
			      uint64_t now_us)
{
	for (size_t i = 0; i < pending_count; i++) {
		int rc;

		if (pending == NULL || !pending[i].valid || now_us < pending[i].due_time_us) {
			continue;
		}

		if (pending[i].kind == PENDING_DELIVERY_TX_STATUS) {
			fprintf(stderr,
				"socket coordinator: reply STATUS node=0x%04x ch=%u len=%zu tx-result=%s delayed-us=%llu\n",
				pending[i].node_id, pending[i].channel, pending[i].psdu_len,
				pending[i].tx_result_collision ? "collision" : "ok",
				(unsigned long long)(pending[i].due_time_us - pending[i].tx_start_us));
		} else {
			fprintf(stderr,
				"socket coordinator: reply RX frame=%s node=0x%04x ch=%u len=%zu delayed-us=%llu\n",
				frame_type_str(pending[i].frame_type), pending[i].node_id,
				pending[i].channel, pending[i].psdu_len,
				(unsigned long long)(pending[i].due_time_us - pending[i].tx_start_us));
		}
		rc = (int)sendto(fd, pending[i].packet, pending[i].packet_len, 0,
				 (const struct sockaddr *)&pending[i].peer_addr,
				 sizeof(pending[i].peer_addr));
		if (rc < 0) {
			return -errno;
		}

		clear_pending_delivery(&pending[i]);
	}

	return 0;
}

static int cancel_overlapping_pending(int fd, struct pending_delivery *pending,
				      size_t pending_count, uint8_t channel,
				      uint64_t start_us, uint64_t end_us)
{
	for (size_t i = 0; i < pending_count; i++) {
		int rc;

		if (!pending[i].valid || pending[i].channel != channel) {
			continue;
		}

		if (!ranges_overlap(start_us, end_us, pending[i].tx_start_us, pending[i].due_time_us)) {
			continue;
		}

		if (pending[i].kind == PENDING_DELIVERY_TX_STATUS) {
			rc = send_tx_result(fd, &pending[i].peer_addr, pending[i].node_id,
					    pending[i].channel, true);
			if (rc < 0) {
				return rc;
			}
			fprintf(stderr,
				"socket coordinator: cancel pending TX status node=0x%04x ch=%u due to collision\n",
				pending[i].node_id, channel);
		} else {
			fprintf(stderr,
				"socket coordinator: cancel pending delivery frame=%s node=0x%04x ch=%u due to collision\n",
				frame_type_str(pending[i].frame_type), pending[i].node_id, channel);
		}
		clear_pending_delivery(&pending[i]);
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct daemon_config cfg = {
		.bind_host = DEFAULT_BIND_HOST,
		.bind_port = DEFAULT_BIND_PORT,
		.model_id = DEFAULT_MODEL_ID,
		.permit_join = true,
	};
	struct zb_host_socket_coord coord;
	struct zb_native_sim_socket_medium_model medium;
	struct pending_delivery pending[ZB_MEDIUM_MAX_PENDING] = { 0 };
	struct medium_peer_entry peers[ZB_MEDIUM_MAX_PEERS] = { 0 };
	int fd;
	int rc;

	rc = parse_args(argc, argv, &cfg);
	if (rc == 1) {
		return 0;
	}
	if (rc < 0) {
		usage(stderr, argv[0]);
		return 2;
	}

	fd = open_server_socket(&cfg);
	if (fd < 0) {
		fprintf(stderr, "socket coordinator: bind failed (%d)\n", -fd);
		return 1;
	}

	zb_host_socket_coord_init(&coord);
	zb_native_sim_socket_medium_model_init(&medium);
	coord.permit_join = cfg.permit_join;

	fprintf(stderr, "socket coordinator: listening on %s:%u model-id=%s permit-join=%s\n",
		cfg.bind_host, cfg.bind_port, cfg.model_id, cfg.permit_join ? "on" : "off");

	for (;;) {
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN,
		};
		struct sockaddr_in peer_addr;
		socklen_t peer_addr_len = sizeof(peer_addr);
		struct zb_native_sim_socket_medium_msg input;
		struct zb_native_sim_socket_medium_msg output;
		uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
		uint64_t now_us;
		int poll_rc;
		ssize_t len;

		now_us = monotonic_time_us();
		poll_rc = poll(&pfd, 1, poll_timeout_ms(pending, ZB_MEDIUM_MAX_PENDING, now_us));
		if (poll_rc < 0) {
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "socket coordinator: poll failed (%d)\n", errno);
			break;
		}

		now_us = monotonic_time_us();
		rc = maybe_send_pending(fd, pending, ZB_MEDIUM_MAX_PENDING, now_us);
		if (rc < 0) {
			fprintf(stderr, "socket coordinator: delayed send failed (%d)\n", -rc);
			break;
		}
		if (poll_rc == 0) {
			continue;
		}

		if ((pfd.revents & POLLIN) == 0) {
			continue;
		}

		len = recvfrom(fd, packet, sizeof(packet), 0,
			       (struct sockaddr *)&peer_addr, &peer_addr_len);
		if (len < 0) {
			if (errno == EINTR) {
				continue;
			}
			fprintf(stderr, "socket coordinator: recvfrom failed (%d)\n", errno);
			break;
		}

		if (zb_native_sim_socket_medium_decode(&input, packet, (size_t)len) < 0) {
			fprintf(stderr, "socket coordinator: dropped malformed packet len=%zd\n", len);
			continue;
		}

		fprintf(stderr,
			"socket coordinator: recv %s from %s:%u node=0x%04x ch=%u pan=0x%04x short=0x%04x rx_on=%u len=%zu",
			medium_msg_type_str(input.type), inet_ntoa(peer_addr.sin_addr),
			ntohs(peer_addr.sin_port), input.node_id, input.channel, input.pan_id,
			input.short_addr, input.rx_on ? 1U : 0U, input.psdu_len);
		if (input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX && input.psdu != NULL) {
			fprintf(stderr, " frame=%s",
				frame_type_str(zb_host_socket_coord_identify_frame(input.psdu,
									 input.psdu_len)));
			dump_psdu_hex(input.psdu, input.psdu_len);
		}
		fputc('\n', stderr);

		if (input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO ||
		    input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER) {
			rc = update_peer_entry(peers, ZB_MEDIUM_MAX_PEERS, &peer_addr, &input);
			if (rc < 0) {
				fprintf(stderr, "socket coordinator: peer table update failed (%d)\n",
					-rc);
				break;
			}
		}

		memset(&output, 0, sizeof(output));
		if (input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_TX) {
			uint64_t input_start_us = now_us;
			uint64_t input_end_us =
				now_us + zb_native_sim_socket_medium_airtime_us(input.psdu_len);
			enum zb_native_sim_socket_medium_window_result reserve_result;

			reserve_result = zb_native_sim_socket_medium_model_reserve_window(
				&medium, input.channel, now_us, input_end_us, NULL);
			if (reserve_result != ZB_NATIVE_SIM_SOCKET_MEDIUM_WINDOW_OK) {
				rc = cancel_overlapping_pending(fd, pending, ZB_MEDIUM_MAX_PENDING,
								input.channel, now_us, input_end_us);
				if (rc < 0) {
					fprintf(stderr,
						"socket coordinator: collision status send failed (%d)\n",
						-rc);
					break;
				}
				rc = send_tx_result(fd, &peer_addr, input.node_id, input.channel, true);
				if (rc < 0) {
					fprintf(stderr,
						"socket coordinator: current TX collision status failed (%d)\n",
						-rc);
					break;
				}

				fprintf(stderr,
					"socket coordinator: drop TX collision node=0x%04x ch=%u len=%zu\n",
					input.node_id, input.channel, input.psdu_len);
				continue;
			}

			rc = schedule_peer_fanout(pending, ZB_MEDIUM_MAX_PENDING,
						peers, ZB_MEDIUM_MAX_PEERS, &peer_addr,
						&input, input_start_us, input_end_us);
			if (rc < 0) {
				fprintf(stderr, "socket coordinator: fanout scheduling failed (%d)\n",
					-rc);
				break;
			}

			rc = zb_host_socket_coord_process(&coord, &input, &output);
			if (rc > 0) {
				rc = schedule_reply(pending, ZB_MEDIUM_MAX_PENDING,
						    &medium, &peer_addr, &output,
						    input_end_us);
				if (rc < 0) {
					fprintf(stderr,
						"socket coordinator: reply scheduling failed (%d)\n",
						-rc);
					break;
				}
			}

			rc = schedule_tx_result(pending, ZB_MEDIUM_MAX_PENDING, &peer_addr,
						input.node_id, input.channel,
						input_start_us, input_end_us, false);
			if (rc < 0) {
				fprintf(stderr,
					"socket coordinator: TX status scheduling failed (%d)\n",
					-rc);
				break;
			}
		} else {
			if (input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_STATUS) {
				rc = handle_status_request(fd, &peer_addr, &medium, &input, now_us);
				if (rc < 0) {
					fprintf(stderr, "socket coordinator: status reply failed (%d)\n",
						-rc);
					break;
				}
			} else {
				rc = zb_host_socket_coord_process(&coord, &input, &output);
				if (rc > 0) {
					rc = send_output(fd, &peer_addr, &output);
					if (rc < 0) {
						fprintf(stderr, "socket coordinator: send failed (%d)\n",
							-rc);
						break;
					}
				}
			}
		}

		if (coord.interview_complete) {
			char model_id[sizeof(coord.observed_model_id)];

			zb_host_socket_coord_observed_model_id(&coord, model_id, sizeof(model_id));
			fprintf(stderr, "socket coordinator: interview complete model-id=%s\n",
				model_id[0] != '\0' ? model_id : cfg.model_id);
		}
	}

	(void)close(fd);
	return 1;
}

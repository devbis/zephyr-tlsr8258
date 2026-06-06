/* SPDX-License-Identifier: Apache-2.0 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <zephyr/zigbee/native_sim_socket_medium.h>

#include "coord_logic.h"

#define DEFAULT_BIND_HOST "127.0.0.1"
#define DEFAULT_BIND_PORT 19011
#define DEFAULT_MODEL_ID  "native-sim-ed"

struct daemon_config {
	const char *bind_host;
	const char *model_id;
	uint16_t bind_port;
	bool permit_join;
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

static int maybe_reply(int fd, const struct sockaddr_in *peer_addr,
		       const struct zb_native_sim_socket_medium_msg *output)
{
	uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
	size_t packet_len;
	int rc;

	if (output == NULL || output->type != ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_RX) {
		return 0;
	}

	rc = zb_native_sim_socket_medium_encode(packet, sizeof(packet), output, &packet_len);
	if (rc < 0) {
		return rc;
	}

	fprintf(stderr,
		"socket coordinator: reply %s frame=%s node=0x%04x ch=%u len=%zu\n",
		medium_msg_type_str(output->type),
		frame_type_str(zb_host_socket_coord_identify_frame(output->psdu, output->psdu_len)),
		output->node_id, output->channel, output->psdu_len);
	dump_psdu_hex(output->psdu, output->psdu_len);
	fputc('\n', stderr);

	rc = (int)sendto(fd, packet, packet_len, 0,
			 (const struct sockaddr *)peer_addr, sizeof(*peer_addr));
	if (rc < 0) {
		return -errno;
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
	struct sockaddr_in active_peer = { 0 };
	bool have_active_peer = false;
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
	coord.permit_join = cfg.permit_join;

	fprintf(stderr, "socket coordinator: listening on %s:%u model-id=%s permit-join=%s\n",
		cfg.bind_host, cfg.bind_port, cfg.model_id, cfg.permit_join ? "on" : "off");

	for (;;) {
		struct sockaddr_in peer_addr;
		socklen_t peer_addr_len = sizeof(peer_addr);
		struct zb_native_sim_socket_medium_msg input;
		struct zb_native_sim_socket_medium_msg output;
		uint8_t packet[ZB_NATIVE_SIM_SOCKET_MEDIUM_MAX_PACKET_SIZE];
		ssize_t len;

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

		if (!have_active_peer ||
		    input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_HELLO ||
		    input.type == ZB_NATIVE_SIM_SOCKET_MEDIUM_MSG_FILTER) {
			active_peer = peer_addr;
			have_active_peer = true;
		} else if (!same_endpoint(&active_peer, &peer_addr)) {
			continue;
		}

		memset(&output, 0, sizeof(output));
		rc = zb_host_socket_coord_process(&coord, &input, &output);
		if (rc > 0) {
			rc = maybe_reply(fd, &peer_addr, &output);
			if (rc < 0) {
				fprintf(stderr, "socket coordinator: send failed (%d)\n", -rc);
				break;
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

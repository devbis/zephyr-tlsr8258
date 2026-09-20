/* SPDX-License-Identifier: Apache-2.0 */

#ifndef TESTS_SUBSYS_ZIGBEE_HOST_SOCKET_COORDINATOR_COORD_LOGIC_H_
#define TESTS_SUBSYS_ZIGBEE_HOST_SOCKET_COORDINATOR_COORD_LOGIC_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/zigbee/native_sim_socket_medium.h>

enum zb_host_socket_frame_type {
	ZB_HOST_SOCKET_FRAME_BEACON_REQ,
	ZB_HOST_SOCKET_FRAME_BEACON,
	ZB_HOST_SOCKET_FRAME_ASSOC_REQ,
	ZB_HOST_SOCKET_FRAME_ASSOC_RSP,
	ZB_HOST_SOCKET_FRAME_DATA_REQ,
	ZB_HOST_SOCKET_FRAME_TRANSPORT_KEY,
	ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_REQ,
	ZB_HOST_SOCKET_FRAME_END_DEVICE_TIMEOUT_RSP,
	ZB_HOST_SOCKET_FRAME_DEVICE_ANNOUNCE,
	ZB_HOST_SOCKET_FRAME_NODE_DESC_REQ,
	ZB_HOST_SOCKET_FRAME_NODE_DESC_RSP,
	ZB_HOST_SOCKET_FRAME_ACTIVE_EP_REQ,
	ZB_HOST_SOCKET_FRAME_ACTIVE_EP_RSP,
	ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_REQ,
	ZB_HOST_SOCKET_FRAME_SIMPLE_DESC_RSP,
	ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ,
	ZB_HOST_SOCKET_FRAME_BASIC_MODEL_ID_READ_RSP,
	/* APS Update-Device (cmd 0x06) from a router relaying a new child's join. */
	ZB_HOST_SOCKET_FRAME_UPDATE_DEVICE,
};

struct zb_host_socket_coord {
	bool permit_join;
	bool got_timeout_req;
	bool got_device_announce;
	bool got_node_desc_rsp;
	bool interview_complete;
	/*
	 * Set when the last joiner's AssocReq advertised
	 * MAC_CAP_RX_ON_WHEN_IDLE (cap byte bit 3). For such joiners
	 * (routers / FFDs) the coord pushes queued frames like
	 * TRANSPORT_KEY immediately after AssocResp instead of waiting
	 * for an indirect-data DataRequest poll — the daemon drains them
	 * via zb_host_socket_coord_drain_unsolicited().
	 */
	bool deliver_queued_unsolicited;
	uint8_t last_assoc_status;
	uint16_t pan_id;
	uint16_t next_child_short;
	uint16_t child_short;
	uint8_t queued_frames[8];
	size_t queued_count;
	char observed_model_id[32];
	struct zb_native_sim_socket_medium_peer peer;
	uint8_t output_psdu[128];
	size_t output_psdu_len;
	/*
	 * Deferred Tunnel(Transport-Key). When a router relays a grandchild's
	 * join (Update-Device), the child's MAC association completes
	 * asynchronously — the child only learns its assigned short address
	 * after polling for the indirect AssocResp, and only then republishes
	 * that short to the medium. If we emit the Tunnel immediately the router
	 * relays the key while the child's medium filter is still 0xffff and the
	 * unicast is dropped. Instead we stash the encoded Tunnel here keyed on
	 * the child's assigned short (carried in the Update-Device payload) plus
	 * the relaying router's node id, and the daemon releases it once a peer
	 * publishes that short with rx_on — i.e. the child is addressable.
	 */
	bool pending_tunnel_valid;
	uint16_t pending_tunnel_child_short;
	uint16_t pending_tunnel_router_node;
	uint8_t pending_tunnel_psdu[128];
	size_t pending_tunnel_len;
	uint8_t pending_tunnel_channel;
};

void zb_host_socket_coord_init(struct zb_host_socket_coord *coord);
int zb_host_socket_coord_process(struct zb_host_socket_coord *coord,
				 const struct zb_native_sim_socket_medium_msg *input,
				 struct zb_native_sim_socket_medium_msg *output);
enum zb_host_socket_frame_type zb_host_socket_coord_identify_frame(const uint8_t *psdu,
								   size_t psdu_len);
bool zb_host_socket_coord_nwk_decrypt(const uint8_t *psdu, size_t psdu_len,
				      uint8_t *out, size_t *out_len);
struct zb_native_sim_socket_medium_msg zb_host_socket_coord_make_tx(
	uint16_t node_id, uint8_t channel, enum zb_host_socket_frame_type type,
	const char *model_id);
void zb_host_socket_coord_observed_model_id(const struct zb_host_socket_coord *coord,
					    char *buffer, size_t buffer_len);
uint8_t zb_host_socket_coord_last_assoc_status(const struct zb_host_socket_coord *coord);

/*
 * Pop the next queued frame onto `output` if the joiner advertised
 * rx-on-when-idle in its last AssocReq. Returns 1 if `output` was
 * filled in, 0 otherwise. The daemon calls this in a loop after the
 * normal coord_process reply so router-class joiners receive
 * TRANSPORT_KEY etc. without having to send a DataRequest poll.
 */
int zb_host_socket_coord_drain_unsolicited(struct zb_host_socket_coord *coord,
					    struct zb_native_sim_socket_medium_msg *output);

/*
 * Release a deferred Tunnel(Transport-Key) if the just-observed peer short
 * matches the pending child short (i.e. the joining child is now addressable
 * on the medium). Returns 1 and fills `output` (addressed to the relaying
 * router) plus `*router_node` with that router's node id when a tunnel is
 * released, 0 otherwise.
 */
int zb_host_socket_coord_take_ready_tunnel(struct zb_host_socket_coord *coord,
					   uint16_t observed_short, bool observed_rx_on,
					   struct zb_native_sim_socket_medium_msg *output,
					   uint16_t *router_node);

#endif /* TESTS_SUBSYS_ZIGBEE_HOST_SOCKET_COORDINATOR_COORD_LOGIC_H_ */

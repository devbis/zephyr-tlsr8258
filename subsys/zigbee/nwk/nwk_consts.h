/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_CONSTS_H
#define _NWK_CONSTS_H

#include "mac_consts.h"

/*
 * Constants of the network layer.  Each value keeps the type and suffix of
 * the literal it replaces, so that the generated code stays the same.
 */

/* NWK frame control, low octet (Zigbee spec 3.3.1): frame type, protocol version, discover route.
 */
#define NWK_FC_LOW_FRAME_TYPE_MASK      0x03U /* frame type, bits 0 and 1 */
#define NWK_FC_LOW_FRAME_TYPE_COMMAND   0x01U /* frame type 1: NWK command */
#define NWK_FC_LOW_PROTO_VER_SHIFT      2U    /* protocol version, bits 2 to 5 */
#define NWK_FC_LOW_PROTO_VER_FIELD_MASK 0x0fU /* the protocol version after the shift */
#define NWK_FC_LOW_PROTO_VER_MASK       0x3cU /* the protocol version in place */
#define NWK_FC_LOW_PROTO_VER_PRO        0x08U /* protocol version 2, in place */
#define NWK_FC_LOW_DISCOVER_ROUTE_SHIFT 6U    /* discover route, bits 6 and 7 */
#define NWK_FC_LOW_DISCOVER_ROUTE_CLEAR 0x3fU /* every bit but the discover route */

/* NWK frame control, high octet (3.3.1): flags. */
#define NWK_FC_HIGH_MULTICAST      0x01U /* multicast flag, bit 0 */
#define NWK_FC_HIGH_SECURITY       0x02U /* security, bit 1 */
#define NWK_FC_HIGH_SECURITY_SHIFT 1U
#define NWK_FC_HIGH_DST_IEEE       0x08U /* destination IEEE address present, bit 3 */
#define NWK_FC_HIGH_SRC_IEEE       0x10U /* source IEEE address present, bit 4 */
#define NWK_FC_HIGH_ED_INITIATOR   0x20U /* end device initiator, bit 5 */

/* Multicast control field (3.3.1.2): number of hops outside the group. */
#define NWK_MCAST_NONMEMBER_RADIUS_MASK 0x07U

/* Neighbor status byte: rxOnWhenIdle (bit 0), device type (bits 1 and 2), relationship (bits 4 to
 * 6). */
#define NWK_NEB_STATUS_MASK         0x7eU /* device type and relationship */
#define NWK_NEB_STATUS_ROUTER_CHILD 0x14U /* device type router, relationship child */

/* Short address values with the unsigned type of the literals they replace. */
#define NWK_SHORT_ADDR_ASSIGN_MAX 0xfff7U /* highest short address that is assigned to a device */
#define NWK_PANID_INVALID         0xffffU /* PAN ID that no network uses */

/* A permit joining duration of 0xff keeps the network open until it is closed (3.2.2.5). */
#define NWK_PERMIT_JOIN_FOREVER 0xffU

/* Result of an address map lookup that found nothing. */
#define NWK_ADDR_LOOKUP_FAILED 0xffU

/* Beacon (3.6.1.3 and the MAC superframe specification, IEEE 802.15.4 7.3.1.3). */
#define NWK_SUPERFRAME_BEACON_ORDER_MASK  0x0fU /* beacon order, bits 0 to 3 */
#define NWK_SUPERFRAME_ORDER_SHIFT        4U    /* superframe order, bits 4 to 7 */
#define NWK_SUPERFRAME_PAN_COORDINATOR    0x4000U
#define NWK_SUPERFRAME_ASSOC_PERMIT       0x8000U
#define NWK_SUPERFRAME_ASSOC_PERMIT_SHIFT 15U
#define NWK_BEACON_TX_OFFSET_UNKNOWN      0xffU /* the three transmit offset octets, 3.6.1.3 */
#define NWK_BEACON_NIBBLE_MASK            0x0fU /* stack profile and device depth */

/* Link status (3.4.8): costs and the number of entries in a frame. */
#define NWK_LINK_COST_MASK             0x07U
#define NWK_LINK_STATUS_ENTRY_CNT_MASK 0x1fU

/* Link status transmit jitter: random part and fixed part in ms. */
#define NWK_LINK_STATUS_JITTER_MASK 0x7fU
#define NWK_LINK_STATUS_JITTER_MIN  5U

/* Backoff period of the unit backoff, in microseconds (20 symbols of 16 us). */
#define NWK_UNIT_BACKOFF_US 320UL

/* Route discovery: a cost that no route can have, and the request jitter mask. */
#define NWK_ROUTE_RESID_COST_MAX  0xffU
#define NWK_ROUTE_REQ_JITTER_MASK 0x7fU
#define NWK_ROUTE_FORGET_CNT_MAX  0xffU /* the route is never forgotten */

/* Address conflict report jitter. */
#define NWK_ADDR_CONFLICT_JITTER_MASK 0x3fU

/* LQI given to a neighbor that is added without a received frame. */
#define NWK_NEIGHBOR_LQI_PRESET 0xdeU

/* Base of the end device timeout, in seconds: 60 << endDevTimeoutDefault (3.6.10.4). */
#define NWK_END_DEV_TIMEOUT_BASE_S 60UL

/* Report and update commands (3.4.9, 3.4.10): the information count field. */
#define NWK_REPORT_INFO_COUNT_MASK 0x1fU

/* Number of valid address map entries above which the neighbor table is reported full. */
#define NWK_ADDR_MAP_FULL_COORDINATOR 63U
#define NWK_ADDR_MAP_FULL_ROUTER      127U

/* Time a transmission waits in the pending table, and the PAN ID conflict timers. */
#define NWK_TX_PEND_EXPIRATION     60
#define NWK_PANID_CONFLICT_RECHECK 2000 /* ms between the detection rounds */
#define NWK_PANID_CONFLICT_DELAY   20   /* ms before the double check */

/* Delay with which the broadcast timer is retried when no timer entry is free. */
#define NWK_BRC_TIMER_RETRY 20

/* Number of octets of the NWK context that a reset clears. */
#define NWK_CTX_RESET_LEN 0x4f

/* Sizes and counts of NWK commands (Zigbee spec 3.4): the command ID with its options or payload.
 */
#define NWK_LEAVE_CMD_LEN              2
#define NWK_REJOIN_REQ_CMD_LEN         2
#define NWK_REJOIN_RSP_CMD_LEN         4
#define NWK_END_DEV_TIMEOUT_REQ_LEN    3
#define NWK_END_DEV_TIMEOUT_RSP_LEN    3
#define NWK_NETWORK_UPDATE_CMD_LEN     13
#define NWK_PANID_REPORT_PANIDS_OFFSET 10
#define NWK_EXT_ADDR_LEN               8U
#define NWK_RREQ_LEN                   6U
#define NWK_RREQ_LEN_WITH_IEEE         14U
#define NWK_RREP_LEN                   8U
#define NWK_RREP_LEN_WITH_IEEE         16U

/* Route reply options (3.4.2.3.2): an originator or a responder IEEE address follows. */
#define NWK_RREP_OPT_ORIGINATOR_IEEE BIT(4)
#define NWK_RREP_OPT_RESPONDER_IEEE  BIT(5)

/* Many-to-one field of a route request (3.4.1.3.1): with or without route record caching. */
#define NWK_MANY_TO_ONE_ROUTE_CACHE    1U
#define NWK_MANY_TO_ONE_NO_ROUTE_CACHE 2U

/* Frame type 2 of the NWK frame control is reserved. */
#define NWK_FRAME_TYPE_RESERVED 2U

/* Route discovery: how long (in seconds) a table entry lives, the cost of a link that is not
 * known, and the smallest delay before a route request is repeated. */
#define NWK_ROUTE_EXPIRY        10U
#define NWK_LINK_COST_UNKNOWN   7U
#define NWK_ROUTE_REQ_DELAY_MIN 2U

/* Worst value of the channel selection metrics at the start of a network formation. */
#define NWK_FORMATION_METRIC_WORST 0xffU

/* An end device sends a keepalive after a third of its timeout; the default timeout is 10 s. */
#define NWK_END_DEV_KEEPALIVE_DIVISOR 3U
#define NWK_END_DEV_TIMEOUT_DEFAULT_S 10UL

/* A rejoin response is polled for this many times, bit 3 of the capability information is
 * "receiver on when idle" (3.6.1.4.1). */
#define NWK_REJOIN_RESP_POLL_MAX        2U
#define NWK_CAPABILITY_RX_ON_IDLE_SHIFT 3U

/* The PAN ID conflict is confirmed in this many rounds. */
#define NWK_PANID_CONFLICT_ROUNDS 2

/* Link quality is averaged as (old * 15 + new) / 16. */
#define NWK_LQI_AVG_OLD_WEIGHT 15
#define NWK_LQI_AVG_DIVISOR    16

#endif /* _NWK_CONSTS_H */

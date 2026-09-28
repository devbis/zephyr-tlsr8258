/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _APS_CONSTS_H
#define _APS_CONSTS_H

/*
 * Constants of the application support sub-layer.  Each value keeps the type
 * and suffix of the literal it replaces, so that the generated code stays the
 * same.
 */

/* The APS handle given to a transmission wraps around after 64 values. */
#define APS_HANDLE_MASK 0x3fU

/* The group delivery queue holds four packets; entries are indexed modulo 4. */
#define APS_GROUP_Q_MASK     0x03U
#define APS_GROUP_Q_REF_NONE 0xffU /* a queue entry without a buffer reference */

/* Destination endpoint 0xff: delivery to every endpoint of the device (2.2.4.1.1). */
#define APS_EP_BROADCAST 0xffU

/* Short addresses 0xfff8 to 0xffff are broadcast addresses (Zigbee spec 3.6.5). */
#define APS_NWK_BROADCAST_MASK 0xfff8U

/* APS header lengths (2.2.5.1): data frame with a destination endpoint, group frame. */
#define APS_DATA_HDR_LEN    8U
#define APS_GROUP_HDR_LEN   9U
/* APS acknowledgement header (2.2.5.2.2): without and with the extended header. */
#define APS_ACK_HDR_LEN     9U
#define APS_ACK_HDR_EXT_LEN 11U

/* Fragmentation (2.2.8.4): up to eight blocks per window, one bit of ackBit each. */
#define APS_FRAG_WINDOW_MAX 8U
#define APS_FRAG_ACK_ALL    0xffU /* every block of the window was received */

/* Duplicate rejection: the entry of a received frame ages out in these seconds. */
#define APS_DUP_CLOCK_MASK 0x07U

/* Delay before a device with stack revision 21 or above asks its parent for a timeout. */
#define APS_STACK_REVISION_R20      20U
#define APS_ED_TIMEOUT_REQ_DELAY_MS 200

/* Smallest payload of a stored binding table in the format that is migrated. */
#define APS_OLD_BINDING_TBL_MIN_PAYLOAD 22U

/* Result of apsDataRequest when no stack buffer is free. */
#define APS_DATA_REQUEST_NO_BUFFER 0x39

/* Extended frame control (fragmentation, 2.2.5.1.1.2): 1 marks the first fragment of a transfer and
 * 2 each fragment after it. */
#define APS_EXT_FRAME_CTRL_FIRST 1U
#define APS_EXT_FRAME_CTRL_NEXT  2U

/* Fragmentation: the interframe delay in ms used when apsInterframeDelay is zero, and the
 * number of receive windows (slots) that are kept. */
#define APS_FRAG_INTERFRAME_DELAY_DEFAULT_MS 100U
#define APS_FRAG_RCV_SLOTS                   2

/* The group delivery queue is full when it holds three entries. */
#define APS_GROUP_Q_FULL 3U

/* Default of apsNonmemberRadius (Zigbee spec 2.2.7.1). */
#define APS_NONMEMBER_RADIUS_DEFAULT 2

/* APS header with only the frame control and the counter (a command), the compact inter-PAN header
 * (frame control, cluster, profile), and what a Tunnel command adds in front of the 8-byte
 * address: the two header octets and the command ID. */
#define APS_CMD_HDR_LEN       2
#define APS_INTERPAN_HDR_LEN  5
#define APS_TUNNEL_PREFIX_LEN 3U

#endif /* _APS_CONSTS_H */

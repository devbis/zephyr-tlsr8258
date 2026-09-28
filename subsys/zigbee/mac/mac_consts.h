/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_CONSTS_H
#define _MAC_CONSTS_H

/*
 * Constants of the MAC layer.  Each value keeps the type and suffix of the
 * literal it replaces, so that the generated code stays the same.
 */

/* Frame type field of the MAC frame control (IEEE 802.15.4 7.2.1.1.1). */
#define MAC_FCF_FRAME_TYPE_FIELD_MASK 0x07U

/* Destination addressing mode "short address" (value 2) in the high frame control octet. */
#define MAC_FCF_HIGH_DST_ADDR_SHORT 0x08U

/* Superframe specification of a beacon (7.3.1.3): octet 0 holds the beacon order (bits 0 to 3)
 * and the superframe order (bits 4 to 7), octet 1 the final CAP slot (bits 0 to 3), BLE (bit 4),
 * PAN coordinator (bit 6) and association permit (bit 7).
 */
#define MAC_SF_BEACON_ORDER_MASK   0x0fU
#define MAC_SF_BEACON_ORDER_NONE   0x0fU /* a non-beacon network: beacon order 15 */
#define MAC_SF_ORDER_SHIFT         4U
#define MAC_SF_OCTET0_NON_BEACON   0xffU /* beacon order and superframe order are both 15 */
#define MAC_SF_FINAL_CAP_SLOT_MASK 0x0fU
#define MAC_SF_FINAL_CAP_SLOT_LAST 0x0fU
#define MAC_SF_BLE_SHIFT           4U
#define MAC_SF_PAN_COORDINATOR     0x40U /* octet 1, bit 6 */
#define MAC_SF_ASSOC_PERMIT_SHIFT  7U

/* Beacon header: superframe specification (2), GTS specification (1), pending address
 * specification (1), then the pending addresses (7.3.1) and the beacon payload.
 */
#define MAC_BEACON_HDR_LEN         4U
/* Counts of short (bits 0 to 2) and extended (bits 4 to 6) pending addresses. */
#define MAC_BEACON_PEND_COUNT_MASK 0x07U
#define MAC_BEACON_PEND_EXT_SHIFT  4U
#define MAC_BEACON_PEND_SHORT_LEN  2U
#define MAC_BEACON_PEND_EXT_LEN    8U

/* Beacon indication: it is copied inside the receive buffer, which holds 0xc0 octets, at an offset
 * of at least 30 octets.
 */
#define MAC_BEACON_IND_COPY_MIN  30U
#define MAC_RX_BUF_PAYLOAD_LIMIT 0xc0U

/* Short addresses above 0xfffd are not device addresses (0xfffe: use the extended address). */
#define MAC_SHORT_ADDR_LAST_DEVICE 0xfffdU

/* Energy detect scan: no channel is being measured. */
#define MAC_SCAN_ED_CHAN_NONE 0xffU

/* Timers, in microseconds: the CSMA backoff period is 20 symbols of 16 us. */
#define MAC_UNIT_BACKOFF_US     320U
#define MAC_CSMA_BACKOFF_MIN_US 200U /* delay used when the random backoff is zero */
#define MAC_CSMA_SLOT_RAND_MASK 0xffffU
#define MAC_ACK_WAIT_US         2000U
#define MAC_TOTAL_WAIT_UNIT_US  296UL       /* frameTotalWaitTime is derived in these units */
#define MAC_TIMER_HALF_RANGE    0x80000000U /* a 32-bit tick difference past half of its range */

/* Random jitter before a beacon is sent: the mask is 0x14 in the vendor object. */
#define MAC_BEACON_JITTER_MASK 0x14U

/* Beacon order 15 selects a non-beacon network and the largest beacon order there is; a
 * superframe order of 15 means that the superframe has no active portion (7.5.2.3.4). */
#define MAC_BEACON_ORDER_MAX        15U
#define MAC_BEACON_ORDER_NON_BEACON 15U
#define MAC_SUPERFRAME_ORDER_NONE   15U

/* A ZigBee beacon payload is 15 octets (Zigbee spec 3.6.7). */
#define MAC_ZB_BEACON_PAYLOAD_LEN 15U

/* An association is attempted this many times before the beacon request is given up. */
#define MAC_BEACON_REQ_TRIES 3

/* Beacon request command: frame control and sequence number (3), destination PAN (2),
 * destination short address (2) and the command ID (1).
 */
#define MAC_BEACON_REQ_PSDU_LEN 8

/* Offset of the energy list in the scan confirm, and the largest scan duration accepted. */
#define MAC_SCAN_CNF_ED_LIST_OFFSET 8
#define MAC_SCAN_DURATION_LIMIT     7U

/* An acknowledgement is 5 octets with the FCS: frame control (2), sequence number (1), FCS (2). */
#define MAC_ACK_FRAME_LEN 5U

/* Hardware timer that the CSMA backoff uses, and the user task queue entries kept free. */
#define MAC_HW_TIMER_CSMA    3
#define MAC_TASKQ_RX_RESERVE 5U

/* Disassociation reasons (7.3.4.3). */
#define MAC_DISASSOC_REASON_COORD_LEAVE  1U /* the coordinator wishes the device to leave */
#define MAC_DISASSOC_REASON_DEVICE_LEAVE 2U /* the device wishes to leave */

/* Association request and response and disassociation notification commands: octets that the
 * command adds after the MAC header (command ID and the capability, short address and status,
 * or the reason).
 */
#define MAC_ASSOC_REQ_CMD_LEN       2U
#define MAC_ASSOC_RSP_CMD_LEN       4U
#define MAC_DISASSOC_NOTIFY_CMD_LEN 2U

/* Coordinator realignment command (7.3.8): the command ID, PAN ID (2), coordinator short
 * address (2), channel (1), short address (2) and channel page (1).
 */
#define MAC_REALIGN_CMD_LEN            9U
#define MAC_REALIGN_PAN_ID_OFFSET      1
#define MAC_REALIGN_COORD_SHORT_OFFSET 3
#define MAC_REALIGN_CHANNEL_OFFSET     5
#define MAC_REALIGN_SHORT_ADDR_OFFSET  6
#define MAC_REALIGN_PAGE_OFFSET        8

/* Raw MHR of the coordinator realignment: frame control at 0, destination PAN at 6,
 * destination address at 8, source PAN at 16, source extended address at 18.
 */
#define MAC_RAW_MHR_DST_PAN_OFFSET  6
#define MAC_RAW_MHR_DST_ADDR_OFFSET 8
#define MAC_RAW_MHR_SRC_PAN_OFFSET  16
#define MAC_RAW_MHR_SRC_EXT_OFFSET  18

/* MAC header: frame control (2) and sequence number (1), then PAN ID and addresses. */
#define MAC_HDR_FCF_SEQ_LEN 3
#define MAC_PAN_ID_LEN      2U
#define MAC_SHORT_ADDR_LEN  2U
#define MAC_EXT_ADDR_LEN    8U

#endif /* _MAC_CONSTS_H */

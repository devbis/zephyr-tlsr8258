/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _GP_CONSTS_H
#define _GP_CONSTS_H

/*
 * Constants of the Green Power proxy.  Each value keeps the type and suffix of
 * the literal it replaces, so that the generated code stays the same.
 */

/* NWK frame control of a GP frame (Green Power spec A.1.4.1). */
#define GP_NWK_FC_FRAME_TYPE_MASK 0x03U /* frame type, bits 0 and 1 */
#define GP_NWK_FC_PROTO_VER_MASK  0x3cU /* protocol version, bits 2 to 5 */
#define GP_NWK_FC_PROTO_VER_GP    0x0cU /* protocol version 3, in place */
#define GP_NWK_FC_AUTO_COMM       0x40U /* auto-commissioning, bit 6 */
#define GP_NWK_FC_EXT_PRESENT     0x80U /* extended NWK frame control present, bit 7 */
/* Every bit but the frame type and the extension flag: version and auto-commissioning. */
#define GP_NWK_FC_VER_AUTO_MASK   0x7cU
#define GP_NWK_FC_MAINT_WITH_EXT  0x81U /* maintenance frame with the extension */

/* Extended NWK frame control (A.1.4.1.3). */
#define GP_EXT_FC_APP_ID_MASK    0x07U /* application ID, bits 0 to 2 */
#define GP_EXT_FC_SEC_LEVEL_MASK 0x18U /* security level, bits 3 and 4, in place */
#define GP_EXT_FC_SEC_LEVEL_1    0x08U /* level 1, reserved */
#define GP_EXT_FC_SEC_LEVEL_2    0x10U /* level 2, 4-byte MIC */
#define GP_EXT_FC_SEC_LEVEL_3    0x18U /* level 3, encrypted with a 4-byte MIC */
#define GP_EXT_FC_RX_AFTER_TX    0x40U /* RxAfterTx, bit 6 */
#define GP_SEC_LEVEL_FIELD_MASK  0x03U /* security level after the shift */

/* GP data request transmit options (A.1.5.2.1) and the matching MCPS-GP ones. */
#define GP_DATA_REQ_TX_OPT_CSMA    0x02U
#define GP_DATA_REQ_TX_OPT_MAC_ACK 0x04U
#define GP_MCPS_TX_OPT_CSMA        0x01U
#define GP_MCPS_TX_OPT_MAC_ACK     0x02U

/* GPD command ID of the channel configuration command (A.4.2.1.3). */
#define GP_CMD_CHANNEL_CONFIGURATION 0xf3U

/* Security control octet of the nonce of an outgoing GPD frame (A.1.4.1.4). */
#define GP_NONCE_SEC_CTRL_OUT 0xc5U

/* The GP transmit channel configuration expires after this many ms. */
#define GP_TX_CHANNEL_TIMEOUT_MS 5000

/* Highest short address that is derived as an alias of a GPD (A.3.5.2.2). */
#define GP_ALIAS_SHORT_ADDR_MAX 0xfff6U

/* Tunneling delays in ms (A.3.5.2.1.2). */
#define GP_TUNNEL_DELAY_RX_AFTER_TX    32U
#define GP_TUNNEL_DELAY_NO_RX_AFTER_TX 5U
#define GP_TUNNEL_DELAY_LQI_KNOWN      32U
#define GP_TUNNEL_DELAY_LQI_UNKNOWN    96U
#define GP_TUNNEL_DELAY_NO_ROUTE       100U /* added when no route to the sink is known */

/* Maximum value of the age of a duplicate-filter entry. */
#define GP_DATA_IND_ENTRY_TIMEOUT_MAX 0xffU

/* A queued GP frame stays in the transmit queue for the longest possible time. */
#define GP_TX_QUEUE_ENTRY_LIFETIME_MAX 0x00ffffffUL

/* The GP channel numbers are counted from channel 11; the direction is bit 7 of the extended
 * NWK frame control (A.1.4.1.3). */
#define GP_CHANNEL_OFFSET         11U
#define GP_EXT_FC_DIRECTION_SHIFT 7U

#endif /* _GP_CONSTS_H */

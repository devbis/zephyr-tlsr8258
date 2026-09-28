/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _SS_CONSTS_H
#define _SS_CONSTS_H

/*
 * Constants of the security service.  Each value keeps the type and suffix of
 * the literal it replaces, so that the generated code stays the same.
 */

/* Security control byte of the auxiliary header (Zigbee spec 4.5.1.1). */
#define SS_SEC_CTRL_LEVEL_MASK           0x07U /* security level, bits 0..2 */
#define SS_SEC_CTRL_LEVEL_CLEAR_MASK     0xf8U /* every bit but the security level */
#define SS_SEC_CTRL_KEY_ID_MASK          0x18U /* key identifier, bits 3 and 4 */
#define SS_SEC_CTRL_EXT_NONCE            0x20U /* extended nonce, bit 5 */
/* Key identifiers (4.5.1.1.2, Table 4-39) shifted into the key-id field. */
#define SS_SEC_CTRL_KEY_ID_KEY_TRANSPORT 0x10U /* 2: key-transport key */
#define SS_SEC_CTRL_KEY_ID_KEY_LOAD      0x18U /* 3: key-load key */

/* Security level 5, ENC-MIC-32 (4.5.1.1.1, Table 4-38). */
#define SS_SECURITY_LEVEL_ENC_MIC_32 5U

/* Auxiliary frame header layout (4.5.1). */
#define SS_AUX_HDR_FRAME_CNT_OFFSET 1  /* after the security control byte */
#define SS_AUX_HDR_SRC_ADDR_OFFSET  5  /* after the frame counter */
#define SS_APS_AUX_HDR_LEN          5  /* security control + frame counter */
#define SS_APS_AUX_HDR_EXT_LEN      13 /* with the extended nonce: 1 + 4 + 8 */

/* One-octet inputs of the keyed hash (4.5.3, 4.4.11.7.4). */
#define SS_KEY_HASH_INPUT_KEY_TRANSPORT 0U /* key-transport key */
#define SS_KEY_HASH_INPUT_KEY_LOAD      2U /* key-load key */
#define SS_KEY_HASH_INPUT_VERIFY_KEY    3  /* Verify-Key hash value */

/* Outgoing and incoming NWK frame counter values (4.3.1). */
#define SS_FRAME_CNT_INVALID   0xffffffffUL /* the counter value never accepted */
#define SS_FRAME_CNT_HIGH_MARK 0x80000000UL /* counter past half of its range */

/* APS command frame (4.4.10): the command identifier precedes the fields. */
#define SS_APS_CMD_ID_LEN 1

/* The APS header in front of a tunneled command: frame control + counter. */
#define SS_APS_TUNNEL_HDR_LEN 2

/* Transport-Key command (4.4.10.1). */
#define SS_TRANSPORT_KEY_CMD_ALLOC_LEN  39 /* buffer the vendor allocates */
#define SS_TRANSPORT_KEY_CMD_KEY_OFFSET 2
#define SS_TRANSPORT_KEY_CMD_NWK_SEQ    18 /* network key: key sequence number */
#define SS_TRANSPORT_KEY_CMD_NWK_DST    19 /* network key: destination address */
#define SS_TRANSPORT_KEY_CMD_NWK_SRC    27 /* network key: source address */
#define SS_TRANSPORT_KEY_CMD_TCLK_DST   18 /* trust-center link key: destination */
#define SS_TRANSPORT_KEY_CMD_TCLK_SRC   26 /* trust-center link key: source */
#define SS_TRANSPORT_KEY_CMD_TCLK_LEN   34 /* trust-center link key payload */

/* Update-Device command (4.4.10.2). */
#define SS_UPDATE_DEVICE_CMD_LEN        12
#define SS_UPDATE_DEVICE_CMD_SHORT_ADDR 9
#define SS_UPDATE_DEVICE_CMD_STATUS     11

/* Request-Key command (4.4.10.4) without the partner address. */
#define SS_REQUEST_KEY_CMD_SHORT_LEN 2U

/* Confirm-Key command (4.4.10.8). */
#define SS_CONFIRM_KEY_CMD_LEN      11
#define SS_CONFIRM_KEY_CMD_STATUS   1
#define SS_CONFIRM_KEY_CMD_KEY_TYPE 2
#define SS_CONFIRM_KEY_CMD_DST_ADDR 3

/* Radius of a command a parent relays to its child. */
#define SS_TUNNEL_RELAY_RADIUS 1

/* Timers, in milliseconds. */
#define SS_UPDATE_DEVICE_RETRY_DELAY_MS 20   /* secured copy of an Update-Device */
#define SS_TC_TRANSPORT_KEY_DELAY_MS    1000 /* Transport-Key of a new network key */
#define SS_TC_SWITCH_KEY_DELAY_MS       3000 /* Switch-Key after the Transport-Key */
#define SS_TIMEOUT_PERIOD_DEFAULT_MS    5000 /* default ss_ib.ssTimeoutPeriod */
#define SS_MS_PER_SECOND                1000U

/* zdo_mgmt_nwk_flag bits owned by the security service. */
#define SS_MGMT_NWK_FLAG_TC_KEY_UPDATE 0x02U /* trust center: a new network key is out */
#define SS_MGMT_NWK_FLAG_KEY_REJOIN    0x04U /* rotation key came without APS security */

/* Network key material: two slots, selected by one bit. */
#define SS_NWK_KEY_SLOT_MASK 0x01U
#define SS_NWK_KEY_SLOT_MAX  1U

/* ss_tcPolicy_t.allowTCLKrequest: any device may request a link key. */
#define SS_TCLK_REQUEST_ANY 1U

/* Stack revision 20 (pre-R21) of the node descriptor. */
#define SS_STACK_REVISION_R20 20U

/* No authentication status for an invalid rejoin state. */
#define SS_ZDO_AUTH_STATUS_INVALID 0xffU

/* CCM* (Zigbee spec annex A) and the keyed hash (annex B.1.4). */
#define SS_CCM_NONCE_LEN    13
#define SS_CCM_FLAGS_ADATA  0x40U /* B0 flags: additional authenticated data */
#define SS_CCM_FAIL         0xffU /* what the vendor returns on a failure */
#define SS_HMAC_OPAD        0x5cU
#define SS_HMAC_IPAD        0x36U
#define SS_MMO_PAD_BYTE     0x80  /* first padding byte of the MMO hash */
#define SS_TTLMAC_BUF_LEN   0x80  /* scratch buffer of the keyed hash */
#define SS_TTLMAC_INPUT_MAX 0x70U /* longest input that fits next to the key */

#endif

/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZDO_CONSTS_H
#define _ZDO_CONSTS_H

#include "nwk_consts.h"

/*
 * Constants of the ZigBee device object, the device profile services and the
 * application API.  Each value keeps the type and suffix of the literal it
 * replaces, so that the generated code stays the same.
 */

/* A ZDP response uses the request cluster ID with bit 15 set (Zigbee spec 2.4.2). */
#define ZDP_RSP_CLUSTER_BIT 0x8000U

/* Endpoints (Zigbee spec 2.5.2): application endpoints are 1 to 240, 242 is the GP endpoint. */
#define ZDP_EP_MAX_APP   240
#define ZDP_EP_GP        242
#define ZDP_EP_BROADCAST 0xffU

/* A network address that no device has. */
#define ZDP_NWK_ADDR_UNKNOWN 0xffffU

/* Channels of the 2.4 GHz band (11 to 26); 27 is returned when a mask holds none. */
#define ZDP_CHANNEL_MIN   11
#define ZDP_CHANNEL_MAX   26
#define ZDO_CHANNEL_FIRST 11U
#define ZDO_CHANNEL_NONE  27U

/* Accept-any value of the channel that a Mgmt_NWK_Update_req may select. */
#define ZDP_ACCEPT_CHANNEL_ANY 0xff

/* Mgmt_NWK_Update_req (2.4.3.3.9): ScanDuration up to 5 scans, 0xfe changes the channel,
 * 0xff changes the network manager attributes (ZDO_NWK_MANAGER_*).
 */
#define ZDP_NWK_UPDATE_SCAN_DURATION_MAX 5U

/* Delays, in ms. */
#define ZDP_CHANGE_CHANNEL_DELAY_MS       46U
#define ZDP_CHANNEL_CHANGE_FIRST_DELAY_MS 460
#define ZDP_END_DEV_BIND_TIMEOUT_MS       20000
#define ZDP_PARENT_ANNCE_DELAY_BASE_MS    1000U
#define ZDP_PARENT_ANNCE_JITTER_MASK      0x1fU
#define ZDP_PARENT_ANNCE_JITTER_STEP_MS   50U
#define ZDO_SELF_LEAVE_DELAY_MS           200U
#define ZDO_DEFAULT_POLL_RATE_MS          500U

/* Bind_req / Unbind_req payload (2.4.3.2.2): without the transaction sequence number, for a
 * group destination (short address) and for an extended destination with an endpoint.
 */
#define ZDP_BIND_REQ_LEN_SHORT 14U
#define ZDP_BIND_REQ_LEN_EXT   21U
#define ZDP_TSN_LEN            1U

/* Mgmt_Lqi_rsp neighbor table record fields (2.4.4.3.2). */
#define ZDP_LQI_DEVICE_TYPE_MASK  0x03U
#define ZDP_LQI_RELATIONSHIP_MASK 0x07U

/* Bit 0 of zdo_mgmt_nwk_flag: an energy scan requested by a Mgmt_NWK_Update_req is running. */
#define ZDO_MGMT_NWK_FLAG_ED_SCAN 0x01U

/* The rejoin backoff iteration counter stops at this value. */
#define ZDO_REJOIN_BACKOFF_ITER_MAX 0xffff

/* Application API (zb_api.c): returned by a function that does not exist for the role. */
#define ZB_API_RET_ROLE_UNSUPPORTED 0x32
/* Match_Desc_req: the number of input and output clusters is counted in the low 7 bits. */
#define ZB_API_CLUSTER_COUNT_MASK   0x7fU

#endif /* _ZDO_CONSTS_H */

/* SPDX-License-Identifier: Apache-2.0 */
/* Default Zigbee stack config — replaces zigbee/common/includes/zb_config.h defaults */
#pragma once

/* APS binding */
#ifndef APS_BINDING_TABLE_NUM
#define APS_BINDING_TABLE_NUM           8
#endif

/* NWK (stubs until Phase 3) */
#ifndef NWK_ROUTE_RECORD_TABLE_NUM
#define NWK_ROUTE_RECORD_TABLE_NUM      0
#endif
#ifndef NWK_ROUTE_TABLE_NUM
#define NWK_ROUTE_TABLE_NUM             0
#endif

/* ZCL defaults */
#ifndef ZCL_CLUSTER_NUM_MAX
#define ZCL_CLUSTER_NUM_MAX             8
#endif
#ifndef ZCL_ATTRIBUTE_NUM_MAX
#define ZCL_ATTRIBUTE_NUM_MAX           16
#endif

/* Inter-PAN */
#ifndef ZB_INTER_PAN_MAX_MSQ_LEN
#define ZB_INTER_PAN_MAX_MSQ_LEN        4
#endif

/* TX power default index */
#ifndef ZB_DEFAULT_TX_POWER_IDX
#define ZB_DEFAULT_TX_POWER_IDX         23
#endif

/* MAC constants */
#ifndef MAC_MCPS_DATA_REQ_TABLE_SIZE
#define MAC_MCPS_DATA_REQ_TABLE_SIZE    4
#endif

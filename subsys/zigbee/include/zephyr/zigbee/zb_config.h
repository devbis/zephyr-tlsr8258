/* SPDX-License-Identifier: Apache-2.0 */
/* Default Zigbee stack config — replaces zigbee/common/includes/zb_config.h defaults */
#pragma once

/* Security / CCM constants (from tl_zigbee_sdk zigbee/common/includes/zb_config.h) */
#ifndef CCM_KEY_SIZE
#define CCM_KEY_SIZE                            16
#endif
#ifndef SECUR_N_SECUR_MATERIAL
#define SECUR_N_SECUR_MATERIAL                  2
#endif
#ifndef ZB_SECURITY
#define ZB_SECURITY                             1
#endif
#ifndef APS_FRAME_SECURITY
#define APS_FRAME_SECURITY
#endif

/* Polling defaults used by ED/ZLL commissioning paths */
#ifndef POLL_RATE_QUARTERSECONDS
#define POLL_RATE_QUARTERSECONDS                250
#endif
#ifndef POLL_RATE
#define POLL_RATE                               (4 * POLL_RATE_QUARTERSECONDS)
#endif
#ifndef RESPONSE_POLL_RATE
#define RESPONSE_POLL_RATE                      POLL_RATE_QUARTERSECONDS
#endif
#ifndef QUEUE_POLL_RATE
#define QUEUE_POLL_RATE                         POLL_RATE_QUARTERSECONDS
#endif
#ifndef REJOIN_POLL_RATE
#define REJOIN_POLL_RATE                        (2 * POLL_RATE_QUARTERSECONDS)
#endif

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

/* Security constants */
#ifndef SEC_KEY_LEN
#define SEC_KEY_LEN                     16
#endif

/* Default device role — used by zb_af.c node descriptor initialisation.
 * Can be overridden by application Kconfig before including this header. */
#if !defined(ZB_COORDINATOR_ROLE) && !defined(ZB_ROUTER_ROLE) && !defined(ZB_ED_ROLE)
#define ZB_ED_ROLE                      1
#endif

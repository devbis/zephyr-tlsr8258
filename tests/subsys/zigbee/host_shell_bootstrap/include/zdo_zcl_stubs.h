/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal ZDO / ZCL type stubs for the host_shell_bootstrap test harness.
 *
 * These definitions stand in for the real SDK types until the production
 * headers are available on the host build path.  When app_bdb.c starts
 * including the real ZDO/ZCL headers this file must be kept in sync with
 * — or replaced by — the SDK-provided typedefs so there is no conflict.
 *
 * Layout rules:
 *   - Only define types referenced by the stub function implementations in
 *     main.c.  Do not copy SDK fields that are not used by the stubs.
 *   - epInfo_t covers the dstAddr / profileId / dstEp / dstAddrMode /
 *     txOptions / radius fields that the real struct exposes in its
 *     short-address path; sufficient for the stub to capture the destination.
 */

#ifndef ZDO_ZCL_STUBS_H_
#define ZDO_ZCL_STUBS_H_

#include <stdint.h>

typedef uint16_t u16;
typedef uint8_t  u8;
typedef uint8_t  zdo_status_t;
typedef uint8_t  status_t;
typedef void (*zdo_callback)(void *p);

typedef struct {
	u16 nwk_addr_interest;
} zdo_active_ep_req_t;

typedef struct {
	u16 nwk_addr_interest;
	u8  endpoint;
} zdo_simple_descriptor_req_t;

/* Minimal epInfo_t — layout is compatible with the production struct in
 * af/zb_af.h: the address occupies the first member of the dstAddr union
 * (shortAddr at offset 0), followed by profileId, dstEp, dstAddrMode,
 * txOptions, radius; sufficient for the stub to capture the destination. */
typedef struct {
	union { u16 shortAddr; } dstAddr;
	u16 profileId;
	u8  dstEp;
	u8  dstAddrMode;
	u8  txOptions;
	u8  radius;
} epInfo_t;

/* APS address-mode and TX-option constants (values match aps/aps_api.h). */
#define APS_SHORT_DSTADDR_WITHEP 2U
#define APS_TX_OPT_ACK_TX        0x04U

typedef struct {
	u8  numAttr;
	u16 attrID[1];
} zclReadCmd_t;

/* Forward declarations of the three ZDO/ZCL APIs stubbed in main.c.
 * Placing them here makes the signatures visible to app_bdb.c when it
 * is text-included into the test TU before the stub definitions appear. */
zdo_status_t zb_zdoActiveEpReq(u16 dstNwkAddr, zdo_active_ep_req_t *pReq,
				u8 *seqNo, zdo_callback indCb);
zdo_status_t zb_zdoSimpleDescReq(u16 dstNwkAddr, zdo_simple_descriptor_req_t *pReq,
				  u8 *seqNo, zdo_callback indCb);
status_t zcl_read(u8 srcEp, epInfo_t *pDstEpInfo, u16 clusterId, u16 manuCode,
		  u8 disableDefaultRsp, u8 direction, u8 seqNo,
		  zclReadCmd_t *readCmd);

#endif /* ZDO_ZCL_STUBS_H_ */

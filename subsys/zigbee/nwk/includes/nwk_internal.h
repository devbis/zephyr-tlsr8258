/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal NWK declarations shared between the ported libzigbee
 * sources. Mirrors the parts of libzigbee/src/include/nwk_internal.h
 * referenced by the Zephyr port. Add to this file as more libzigbee
 * NWK TUs land.
 */
#pragma once

#include "zb_common_stub.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_neighbor.h"

/* Route-discovery / TX-pend forward decls. */
extern void nwkReportCmdHandler(void *arg, nwkCmd_t *cmd);
extern void nwkReportCmdSend(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, u8 handle);
extern u32 getPassiveAckTimeout(void);

/* Scan/start confirm handlers (defined in nwk_formation.c,
 * nwk_discovery.c, nwk_join.c). Referenced by the central NWK
 * dispatcher in nwk.c.
 */
extern void nwk_formationScanCnfHandler(void *arg);
extern void nwk_formationStartCnfHandler(void *arg);
extern void nwk_discoveryScanCnfHandler(void *arg);
extern void nwk_rejoinScanCnfHandler(void *arg);
extern void nwk_directJoinScanCnfHandler(void *arg);
extern void nwk_edScanCnfHandler(void *arg);
extern void nwk_startRouterCnfHandler(void *arg);
extern void nwk_panIdConflictCnfHandler(void *arg);

/* Table initializers used by tl_zbNwkInit. */
extern void nwkBrcTransTabInit(void);
extern void nwkRouteDiscTabInit(void);
extern void nwkRoutingTabInit(void);
extern void nwkTxDataPendTabInit(void);
extern void tl_zbNeighborTableRst(void);
extern void tl_zbNwkAddrMapInit(void);

/* MAC/NWK initial constants defined in subsys/zigbee/common/zb_config.c. */
struct tl_zb_mac_pib_t_; /* opaque fwd: real type in mac headers */
struct nwk_nib_t_;
extern const tl_zb_mac_pib_t macPibDefault;
extern const nwk_nib_t nwkNibDefault;

extern nwk_routingTabEntry_t *nwkRoutingTabEntryDstActiveGet(u16 dstAddr);
extern nwk_routingTabEntry_t *nwkRoutingTabEntryDstFind(u16 dstAddr);
extern nwk_routingTabEntry_t *nwkRoutingTabEntryCreate(u16 dstAddr);
extern void nwkRoutingTabEntryClear(nwk_routingTabEntry_t *entry);
extern void nwkRoutingTabEntryDstDel(u16 dstAddr);
extern u16 nwkRoutingTabGetNextHop(nwk_routingTabEntry_t *entry);
extern tl_zb_normal_neighbor_entry_t *tl_zbNeighborTabSearchForChildEndDev(void *entry);
extern tl_zb_normal_neighbor_entry_t *tl_zbNeighborTableSearchFromAddrmapIdx(u16 idx);
extern tl_zb_normal_neighbor_entry_t *nwkValidNeighborToFwd(u16 shortAddr);

extern void nwk_tx(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u16 nextHop, u8 indirect,
		   u8 *payload, u8 payloadLen);
extern void nwk_fwdPacket(zb_buf_t *buf, nwk_hdr_t *pNwkHdr, u8 *payload, u8 payloadLen);
extern u8 getNwkHdrSize(nwk_hdr_t *pNwkHdr);

/* Buffer helpers used across NWK TUs (vendor-internal API). The Zephyr
 * port provides these in platform/zephyr/ev_buffer_zephyr.c.
 */
extern zb_buf_t *zb_buf_allocate(void);
extern void zb_buf_free(zb_buf_t *buf);
extern void *tl_bufInitalloc(zb_buf_t *p, u8 size);

/* Route-discovery cache layout (vendor's nwk_route_disc_cache_buf_t).
 * Mirrors libzigbee/src/include/nwk_internal.h.
 */
enum {
	NWK_ROUTE_DISC_CACHE_HDR_OFFSET = 5,
	NWK_ROUTE_DISC_CACHE_SAVED_HANDLE_OFFSET = 0xc1,
	NWK_ROUTE_DISC_CACHE_SIZE = 0xc3,
};

typedef struct _attribute_packed_ {
	u8 payloadLen;
	u8 *payload;
	nwk_hdr_t hdr;
	u8 reserved[NWK_ROUTE_DISC_CACHE_SAVED_HANDLE_OFFSET -
		    NWK_ROUTE_DISC_CACHE_HDR_OFFSET -
		    (int)sizeof(nwk_hdr_t)];
	u8 savedHandle;
	u8 tail[1];
} nwk_route_disc_cache_buf_t;

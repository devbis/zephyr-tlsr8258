/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * The device tables a ZNP host reads: the associated-device (child and
 * neighbor) queries of UTIL, and the address manager NV item that a backup
 * lists the devices from. Both are built from the stack's neighbor table and
 * address map.
 */
#include <string.h>

#include <zephyr/logging/log.h>

#include "zb_common.h"
#include "ss/ss_zdoSecurityME.h"
#include "nwk/includes/nwk_addr_map.h"
#include "nwk/includes/nwk_neighbor.h"
#include "zb_znp_internal.h"

LOG_MODULE_DECLARE(zigbee_znp, CONFIG_ZIGBEE_LOG_LEVEL);

/* Z-Stack node relations. */
#define ZNP_REL_PARENT          0U
#define ZNP_REL_CHILD_RFD       1U
#define ZNP_REL_CHILD_RFD_IDLE  2U
#define ZNP_REL_CHILD_FFD       3U
#define ZNP_REL_CHILD_FFD_IDLE  4U
#define ZNP_REL_NEIGHBOR        5U
#define ZNP_REL_NOTUSED         0xffU

/* associated_devices_t as sent by Z-Stack 3.0: packed, 28 bytes. */
#define ZNP_ASSOC_DEVICE_LEN    28U

#define ZNP_ADDRMGR_USER_ASSOC    0x01U
#define ZNP_ADDRMGR_USER_SECURITY 0x02U

static uint8_t relation_of(const tl_zb_normal_neighbor_entry_t *e)
{
	switch (e->relationship) {
	case NEIGHBOR_IS_PARENT:
		return ZNP_REL_PARENT;
	case NEIGHBOR_IS_CHILD:
		if (e->deviceType == NWK_DEVICE_TYPE_ROUTER) {
			return (e->rxOnWhileIdle != 0U) ? ZNP_REL_CHILD_FFD_IDLE : ZNP_REL_CHILD_FFD;
		}
		return (e->rxOnWhileIdle != 0U) ? ZNP_REL_CHILD_RFD_IDLE : ZNP_REL_CHILD_RFD;
	default:
		return ZNP_REL_NEIGHBOR;
	}
}

static const tl_zb_normal_neighbor_entry_t *neighbor_at(uint8_t index)
{
	const tl_zb_normal_neighbor_entry_t *e = g_zb_neighborTbl.activeHead;

	while (e != NULL) {
		if (e->used != 0U) {
			if (index == 0U) {
				return e;
			}
			index--;
		}
		e = e->activeNext;
	}

	return NULL;
}

static void device_fill(uint8_t *out, const tl_zb_normal_neighbor_entry_t *e)
{
	uint8_t *p = out;

	memset(out, 0, ZNP_ASSOC_DEVICE_LEN);
	if (e == NULL) {
		(void)znp_put_u16(p, 0xFFFEU);
		out[4] = ZNP_REL_NOTUSED;
		return;
	}

	p = znp_put_u16(p, tl_zbshortAddrByIdx(e->addrmapIdx));
	p = znp_put_u16(p, e->addrmapIdx);
	*p++ = relation_of(e);
	*p++ = 0U; /* device status */
	*p++ = 0U; /* association count */
	*p++ = e->age;

	/* link info */
	*p++ = e->transFailure;
	*p++ = e->outgoingCost;
	*p++ = e->lqi;
#ifdef ZB_SECURITY
	*p++ = e->keySeqNum;
	p = znp_put_u32(p, e->incomingFrameCnt);
#else
	*p++ = 0U;
	p = znp_put_u32(p, 0U);
#endif
	p = znp_put_u16(p, e->transFailure);

	/* aging end device and keepalive state */
	*p++ = (uint8_t)e->endDevCfg;
	p = znp_put_u32(p, e->devTimeout);
	p = znp_put_u32(p, e->timeoutCnt);
	*p++ = e->keepaliveRcvd ? 0xffU : 0U;
}

static void util_assoc_count(const uint8_t *d, uint8_t len)
{
	const tl_zb_normal_neighbor_entry_t *e = g_zb_neighborTbl.activeHead;
	uint16_t count = 0U;
	uint8_t rsp[2];

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x48U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	for (; e != NULL; e = e->activeNext) {
		uint8_t rel = relation_of(e);

		if (e->used != 0U && rel >= d[0] && rel <= d[1]) {
			count++;
		}
	}

	(void)znp_put_u16(rsp, count);
	znp_srsp(ZNP_SUBSYS_UTIL, 0x48U, rsp, sizeof(rsp));
}

static void util_assoc_find_device(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[ZNP_ASSOC_DEVICE_LEN];

	device_fill(rsp, (len >= 1U) ? neighbor_at(d[0]) : NULL);
	znp_srsp(ZNP_SUBSYS_UTIL, 0x49U, rsp, sizeof(rsp));
}

static void util_assoc_get_with_address(const uint8_t *d, uint8_t len)
{
	const tl_zb_normal_neighbor_entry_t *e;
	uint8_t rsp[ZNP_ASSOC_DEVICE_LEN];

	if (len < 10U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x4AU, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	/* The extended address wins; all zeroes means look up by short address. */
	e = ZB_IEEE_ADDR_IS_ZERO(d) ? nwk_neTblGetByShortAddr(znp_get_u16(&d[8]))
				    : nwk_neTblGetByExtAddr((u8 *)d);
	device_fill(rsp, e);
	znp_srsp(ZNP_SUBSYS_UTIL, 0x4AU, rsp, sizeof(rsp));
}

/* Add a device the host knows of, as the Z-Stack node relation @p rel says. */
static bool device_add(uint16_t nwk, const uint8_t *ext, uint8_t rel)
{
	tl_zb_normal_neighbor_entry_t n;
	tl_zb_normal_neighbor_entry_t *entry;
	u16 idx = 0U;

	if (tl_zbNwkAddrMapAdd(nwk, (u8 *)ext, &idx) != NWK_STATUS_SUCCESS) {
		return false;
	}

	memset(&n, 0, sizeof(n));
	n.addrmapIdx = idx;
	n.rxOnWhileIdle = (rel == ZNP_REL_CHILD_RFD_IDLE || rel == ZNP_REL_CHILD_FFD_IDLE ||
			   rel == ZNP_REL_NEIGHBOR)
				  ? 1U
				  : 0U;
	n.deviceType = (rel == ZNP_REL_CHILD_FFD || rel == ZNP_REL_CHILD_FFD_IDLE ||
			rel == ZNP_REL_NEIGHBOR)
			       ? NWK_DEVICE_TYPE_ROUTER
			       : NWK_DEVICE_TYPE_ED;
	n.relationship = (rel == ZNP_REL_NEIGHBOR) ? NEIGHBOR_IS_SIBLING : NEIGHBOR_IS_CHILD;
	n.lqi = 0xdeU;
	n.outgoingCost = 1U;

	entry = tl_zbNeighborTableUpdate(&n, 1U);
	if (entry == NULL) {
		return false;
	}

	/* A child is written to flash, so that its parent keeps it across a restart. */
	if (n.relationship == NEIGHBOR_IS_CHILD) {
		ss_zdoChildTableStore(entry);
	}

	return true;
}

static void util_assoc_add(const uint8_t *d, uint8_t len)
{
	uint8_t rel;

	if (len < 11U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x64U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	rel = d[10];
	if (rel < ZNP_REL_CHILD_RFD || rel > ZNP_REL_NEIGHBOR) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x64U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	znp_srsp_status(ZNP_SUBSYS_UTIL, 0x64U,
			device_add(znp_get_u16(d), &d[2], rel) ? ZNP_STATUS_SUCCESS
							       : ZNP_STATUS_FAILURE);
}

static void util_assoc_remove(const uint8_t *d, uint8_t len)
{
	tl_zb_normal_neighbor_entry_t *e;
	zb_addrForNeighbor_t info;

	if (len < 8U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x63U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	e = nwk_neTblGetByExtAddr((u8 *)d);
	if (e == NULL) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x63U, ZNP_STATUS_FAILURE);
		return;
	}

	memset(&info, 0, sizeof(info));
	info.shortAddr = tl_zbshortAddrByIdx(e->addrmapIdx);
	tl_zbExtAddrByIdx(e->addrmapIdx, info.extAddr);
	info.relationship = e->relationship;
	(void)nwk_nodeAddrInfoDelete(&info);
	tl_zbNeighborTableDelete(e);

	znp_srsp_status(ZNP_SUBSYS_UTIL, 0x63U, ZNP_STATUS_SUCCESS);
}

static void util_addrmgr_ext_lookup(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[2];
	u16 nwk = 0xFFFFU;
	u16 idx;

	if (len < 8U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x40U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	if (tl_zbShortAddrByExtAddr(&nwk, (u8 *)d, &idx) != 0U) {
		nwk = 0xFFFEU;
	}
	(void)znp_put_u16(rsp, nwk);
	znp_srsp(ZNP_SUBSYS_UTIL, 0x40U, rsp, sizeof(rsp));
}

static void util_addrmgr_nwk_lookup(const uint8_t *d, uint8_t len)
{
	uint8_t rsp[8] = {0};
	u16 idx;

	if (len < 2U) {
		znp_srsp_status(ZNP_SUBSYS_UTIL, 0x41U, ZNP_STATUS_INVALID_PARAMETER);
		return;
	}

	if (tl_zbExtAddrByShortAddr(znp_get_u16(d), rsp, &idx) != 0U) {
		memset(rsp, 0xff, sizeof(rsp));
	}
	znp_srsp(ZNP_SUBSYS_UTIL, 0x41U, rsp, sizeof(rsp));
}

static const struct znp_cmd tables_cmds[] = {
	{ZNP_SUBSYS_UTIL, 0x40U, util_addrmgr_ext_lookup},
	{ZNP_SUBSYS_UTIL, 0x41U, util_addrmgr_nwk_lookup},
	{ZNP_SUBSYS_UTIL, 0x48U, util_assoc_count},
	{ZNP_SUBSYS_UTIL, 0x49U, util_assoc_find_device},
	{ZNP_SUBSYS_UTIL, 0x4AU, util_assoc_get_with_address},
	{ZNP_SUBSYS_UTIL, 0x63U, util_assoc_remove},
	{ZNP_SUBSYS_UTIL, 0x64U, util_assoc_add},
};

bool znp_tables_handle(uint8_t subsys, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	return znp_cmd_dispatch(tables_cmds, ARRAY_SIZE(tables_cmds), subsys, cmd, data, len);
}

/* The list of child short addresses for UTIL_GET_DEVICE_INFO. */
uint8_t znp_tables_children(uint16_t *out, uint8_t max)
{
	const tl_zb_normal_neighbor_entry_t *e = g_zb_neighborTbl.activeHead;
	uint8_t n = 0U;

	for (; e != NULL && n < max; e = e->activeNext) {
		if (e->used != 0U && e->relationship == NEIGHBOR_IS_CHILD) {
			out[n++] = tl_zbshortAddrByIdx(e->addrmapIdx);
		}
	}

	return n;
}

/* Rebuild the address manager item from the address map. */
void znp_tables_sync(void)
{
	uint8_t table[ZNP_ADDRMGR_ENTRIES * ZNP_ADDRMGR_ENTRY_LEN];
	const tl_zb_addr_map_entry_t *a = g_nwkAddrMap.activeHead;
	size_t used = 0U;

	memset(table, 0xff, sizeof(table));
	for (size_t i = 0; i < ZNP_ADDRMGR_ENTRIES; i++) {
		table[i * ZNP_ADDRMGR_ENTRY_LEN] = 0U;
	}

	for (; a != NULL && used < ZNP_ADDRMGR_ENTRIES; a = a->activeNext) {
		uint8_t *entry = &table[used * ZNP_ADDRMGR_ENTRY_LEN];
		const tl_zb_normal_neighbor_entry_t *nb = nwk_neTblGetByShortAddr(a->shortAddr);

		if (a->used == 0U) {
			continue;
		}

		/* Other devices are listed too, or a backup would leave them out. */
		entry[0] = (nb != NULL && nb->relationship == NEIGHBOR_IS_CHILD)
				   ? ZNP_ADDRMGR_USER_ASSOC
				   : ZNP_ADDRMGR_USER_SECURITY;
		(void)znp_put_u16(&entry[1], a->shortAddr);
		memcpy(&entry[3], a->extAddr, 8U);
		used++;
	}

	(void)znp_nv_set_derived(ZNP_NV_ADDRMGR, table, sizeof(table));
}

/*
 * A host that restores a backup writes the device tables, and expects them to
 * be what the stack knows afterwards.
 */
static bool addr_valid(uint16_t nwk, const uint8_t *ext)
{
	return nwk < 0xFFF8U && !ZB_IEEE_ADDR_IS_ZERO(ext) && !znp_all_ff(ext, 8U);
}

static void addrmgr_import(void)
{
	uint8_t table[ZNP_ADDRMGR_ENTRIES * ZNP_ADDRMGR_ENTRY_LEN];
	uint8_t local[8];
	size_t n;

	if (znp_nv_read(ZNP_NV_ADDRMGR, 0U, table, sizeof(table), &n) != ZNP_STATUS_SUCCESS ||
	    n != sizeof(table)) {
		return;
	}

	zb_getLocalExtAddr(local);
	for (size_t i = 0; i < ZNP_ADDRMGR_ENTRIES; i++) {
		const uint8_t *entry = &table[i * ZNP_ADDRMGR_ENTRY_LEN];
		uint16_t nwk = znp_get_u16(&entry[1]);
		const uint8_t *ext = &entry[3];

		if (!addr_valid(nwk, ext) || nwk == zb_getLocalShortAddr() ||
		    memcmp(ext, local, 8U) == 0) {
			continue;
		}

		/* An associated device is a child of this node, anything else a router. */
		(void)device_add(nwk, ext,
				 ((entry[0] & ZNP_ADDRMGR_USER_ASSOC) != 0U) ? ZNP_REL_CHILD_RFD
									    : ZNP_REL_NEIGHBOR);
	}
}

static void tclk_import(uint16_t id)
{
	uint8_t entry[ZNP_TCLK_ENTRY_LEN];
	uint8_t seed[16];
	uint8_t key[16];
	ss_dev_pair_set_t pair;
	size_t n;

	if (znp_nv_read(id, 0U, entry, sizeof(entry), &n) != ZNP_STATUS_SUCCESS ||
	    n != sizeof(entry) || entry[16] == ZNP_TCLK_KEY_DEFAULT || !addr_valid(0U, &entry[8]) ||
	    znp_nv_read(ZNP_NV_TCLK_SEED, 0U, seed, sizeof(seed), &n) != ZNP_STATUS_SUCCESS ||
	    n != sizeof(seed)) {
		return;
	}

	znp_tclk_key(seed, entry[18], &entry[8], key);

	memset(&pair, 0, sizeof(pair));
	memcpy(pair.device_address, &entry[8], 8U);
	memcpy(pair.linkKey, key, sizeof(key));
	pair.apsLinkKeyType = SS_UNIQUE_LINK_KEY;
	/* The attribute values are the stack's own: provisional, unverified, verified. */
	pair.keyAttr = entry[16];
	pair.outgoingFrameCounter = znp_get_u32(&entry[0]);
	pair.incomingFrameCounter = znp_get_u32(&entry[4]);

	(void)ss_devKeyPairDelete(pair.device_address);
	ss_devKeyPairSave(&pair);
}

void znp_tables_nv_written(uint16_t id)
{
	if (id == ZNP_NV_ADDRMGR) {
		addrmgr_import();
	} else if (id >= ZNP_NV_TCLK_FIRST && id < ZNP_NV_TCLK_FIRST + ZNP_TCLK_ENTRIES) {
		tclk_import(id);
	}
}

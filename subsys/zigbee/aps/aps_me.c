/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "aps_me.h"
#include "zb_task_queue.h"

u8 APS_BINDING_TABLE_SIZE_V1 = APS_BINDING_TABLE_NUM_V1;
u16 APS_OLD_BINDING_TBL_SIZE_MAX = sizeof(aps_binding_table_t);
static inline u16 aps_binding_table_bytes(void)
{
	return (u16)(APS_BINDING_TABLE_SIZE * sizeof(aps_binding_entry_t));
}

static inline bool aps_binding_used(const aps_binding_entry_t *entry)
{
	return entry->used == 1U;
}

void aps_bindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum);

u8 aps_oldBindingTblRecover(void)
{
	u16 oldSize = 0;
	u8 status = nv_flashSingleItemSizeGet(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, &oldSize);
	u16 payloadSize;
	u8 elementCount;

	if (status != NV_SUCC || oldSize > APS_OLD_BINDING_TBL_SIZE_MAX) {
		return status;
	}

	payloadSize = (u16)(oldSize - 4U);
	if (payloadSize < 22U) {
		return status;
	}

	for (elementCount = 1U; elementCount <= APS_BINDING_TABLE_NUM_V1; elementCount++) {
		u16 expected = (u16)(2U * elementCount * (elementCount + 10U));

		if (payloadSize == expected) {
			break;
		}
	}

	if (elementCount > APS_BINDING_TABLE_NUM_V1) {
		return status;
	}

	APS_BINDING_TABLE_SIZE_V1 = elementCount;

	u8 *oldTbl = ev_buf_allocate(oldSize);
	if (oldTbl == NULL) {
		return status;
	}

	status = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, oldSize, oldTbl);
	if (status == NV_SUCC) {
		aps_bindingTblRestore(oldTbl, oldSize, elementCount);
	}

	ev_buf_free(oldTbl);
	return status;
}

void aps_userBindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum)
{
	u16 storedSize = 0;

	if (nv_flashSingleItemSizeGet(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, &storedSize) !=
		    NV_SUCC ||
	    storedSize != tblSize || oldTbl == NULL) {
		return;
	}

	if (nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, storedSize,
			    (u8 *)oldTbl) == NV_SUCC) {
		aps_bindingTblRestore(oldTbl, storedSize, eleNum);
	}
}

void aps_bindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum)
{
	u8 infoBuf[10] = {0};
	u8 addrBuf[10];
	u16 mappedIdx = 0;
	itemIfno_t *info = (itemIfno_t *)infoBuf;
	u8 *oldBytes = (u8 *)oldTbl;
	aps_binding_table_t *legacyTable = (aps_binding_table_t *)oldTbl;
	aps_binding_entry_t *newTbl = bindTblEntryGet();

	(void)tblSize;

	if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(infoBuf),
			    infoBuf) == NV_SUCC) {
		for (u16 i = 0; i <= info->opIndex; i++) {
			u8 readStatus = nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
							    NV_ITEM_ADDRESS_FOR_BIND, info->opSect,
							    i, sizeof(addrBuf), addrBuf);
			if (readStatus == 3U) {
				continue;
			}

			if (tl_idxByExtAddr(&mappedIdx, &addrBuf[2]) != RET_OK) {
				(void)tl_zbNwkAddrMapAdd(ZB_UNKNOWN_SHORT_ADDR, &addrBuf[2],
							 &mappedIdx);
				tl_addrMappingForBind(1, mappedIdx);
			}

			if (eleNum != 0U) {
				u16 mask;

				COPY_BUFFERTOU16(mask, addrBuf);
				for (u16 j = 0; j < eleNum; j++) {
					if ((mask & (u16)(1U << j)) != 0U) {
						legacyTable->table[j].dst_table.group_addr =
							mappedIdx;
					}
				}
			}
		}
	}

	if (tl_idxByExtAddr(&mappedIdx, g_zbInfo.nwkNib.ieeeAddr) != RET_OK) {
		(void)tl_zbNwkAddrMapAdd(ZB_UNKNOWN_SHORT_ADDR, g_zbInfo.nwkNib.ieeeAddr,
					 &mappedIdx);
		tl_addrMappingForBind(1, mappedIdx);
	}

	if (oldBytes[2] == 0U) {
		goto write_new_table;
	}

	for (u16 i = 0; i < eleNum && i < oldBytes[0]; i++) {
		u8 *src = oldBytes + 4U + (i * sizeof(aps_bind_tbl_t));
		if (src[13] == 1U) {
			COPY_U16TOBUFFER(src + 10U, mappedIdx);
		}
	}

	u8 boundCount = oldBytes[2];
	u8 restored = 0;
	u8 *map = oldBytes + 4U + (eleNum * sizeof(aps_bind_tbl_t));

	for (u8 i = 0; i < boundCount && restored < APS_BINDING_TABLE_SIZE; i++) {
		u8 srcIdx = map[0];
		u8 dstIdx = map[1];
		map += sizeof(boundTblMapList_t);

		if (srcIdx >= eleNum || dstIdx >= eleNum) {
			continue;
		}

		aps_bind_tbl_t *src = &legacyTable->table[srcIdx];
		aps_bind_tbl_t *dst = &legacyTable->table[dstIdx];
		aps_binding_entry_t *entry = &newTbl[restored++];

		entry->used = 1U;
		entry->srcEp = src->src_table.src_ep;
		entry->clusterId = src->src_table.cluster_id;

		if (dst->dst_table.dst_addr_mode == APS_BIND_DST_ADDR_GROUP) {
			entry->dstAddrMode = APS_SHORT_GROUPADDR_NOEP;
			entry->groupAddr = dst->dst_table.group_addr;
		} else {
			entry->dstAddrMode = APS_LONG_DSTADDR_WITHEP;
			tl_zbExtAddrByIdx(dst->dst_table.long_addr.dst_addr,
					  entry->dstExtAddrInfo.extAddr);
			entry->dstExtAddrInfo.dstEp = dst->dst_table.long_addr.dst_end;
		}
	}

write_new_table:
	if (nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2,
			     aps_binding_table_bytes(), (u8 *)newTbl) != NV_SUCC) {
		nv_flashSingleItemRemove(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE, tblSize);

		if (nv_flashReadNew(0, NV_MODULE_ADDRESS_TABLE, ITEM_FIELD_IDLE, sizeof(infoBuf),
				    infoBuf) == NV_SUCC) {
			for (u16 i = 0; i <= info->opIndex; i++) {
				if (nv_flashReadByIndex(NV_MODULE_ADDRESS_TABLE,
							NV_ITEM_ADDRESS_FOR_BIND, info->opSect, i,
							sizeof(addrBuf), addrBuf) != 3U) {
					(void)nv_itemDeleteByIndex(NV_MODULE_ADDRESS_TABLE,
								   NV_ITEM_ADDRESS_FOR_BIND,
								   info->opSect, i);
				}
			}
		}
	}
}

void aps_bindingTabInit(void)
{
	memset(bindTblEntryGet(), 0, aps_binding_table_bytes());
}

void aps_bindingTblSave2Flash(void *arg)
{
	(void)arg;
	nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2, aps_binding_table_bytes(),
			 (u8 *)bindTblEntryGet());
}

u8 aps_bindingTblNvInit(void)
{
	nv_itemLengthCheckAdd(NV_ITEM_APS_BINDING_TABLE_V2, aps_binding_table_bytes());

	u8 ret = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2,
				 sizeof(aps_binding_entry_t), (u8 *)bindTblEntryGet());
	if (ret != NV_SUCC) {
		aps_oldBindingTblRecover();
	}

	return ret;
}

void aps_bindingTab_clear(void)
{
	aps_bindingTabInit();
	nv_flashSingleItemRemove(NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2,
				 sizeof(aps_binding_entry_t));
}

aps_binding_entry_t *aps_bindingTblEntryGet(void)
{
	return bindTblEntryGet();
}

u8 aps_bindingTblEntryNum(void)
{
	aps_binding_entry_t *table = bindTblEntryGet();
	u8 count = 0;

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		if (aps_binding_used(&table[i])) {
			count++;
		}
	}

	return count;
}

bool aps_bindingTblMatched(u16 clusterId, u8 srcEp)
{
	aps_binding_entry_t *table = bindTblEntryGet();

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		if (!aps_binding_used(&table[i])) {
			continue;
		}

		if (table[i].clusterId == clusterId && table[i].srcEp == srcEp) {
			return true;
		}
	}

	return false;
}

u8 aps_bindingTblExist(addrExt_t extAddr)
{
	aps_binding_entry_t *table = bindTblEntryGet();

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		if (!aps_binding_used(&table[i])) {
			continue;
		}

		if (table[i].dstAddrMode != APS_LONG_DSTADDR_WITHEP) {
			continue;
		}

		if (ZB_IEEE_ADDR_CMP(table[i].dstExtAddrInfo.extAddr, extAddr)) {
			return 1;
		}
	}

	return 0;
}

aps_binding_entry_t *aps_bindingTblMatch(u16 clusterId, u8 srcEp, u8 dstAddrMode, u8 *dstAddrInfo)
{
	aps_binding_entry_t *table = bindTblEntryGet();

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		aps_binding_entry_t *entry = &table[i];

		if (dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
			u16 groupAddr;

			COPY_BUFFERTOU16(groupAddr, dstAddrInfo);
			if (entry->groupAddr == groupAddr) {
				return entry;
			}

			continue;
		}

		/* Vendor matching accepts any non-zero value in the active flag. */
		if (entry->used == 0U) {
			continue;
		}

		if (entry->clusterId != clusterId || entry->srcEp != srcEp ||
		    entry->dstAddrMode != dstAddrMode) {
			continue;
		}

		if (dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
			if (!ZB_IEEE_ADDR_CMP(entry->dstExtAddrInfo.extAddr, dstAddrInfo)) {
				continue;
			}

			if (entry->dstExtAddrInfo.dstEp != dstAddrInfo[EXT_ADDR_LEN]) {
				continue;
			}
		} else {
			continue;
		}

		return entry;
	}

	return NULL;
}

u8 aps_bindingTblEntryAdd(u16 clusterId, u8 srcEp, u8 dstAddrMode, u8 *dstAddrInfo)
{
	aps_binding_entry_t *table = bindTblEntryGet();

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		aps_binding_entry_t *entry = &table[i];

		if (aps_binding_used(entry)) {
			continue;
		}

		entry->used = 1;
		entry->clusterId = clusterId;
		entry->srcEp = srcEp;
		entry->dstAddrMode = dstAddrMode;

		if (dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
			entry->groupAddr = (u16)dstAddrInfo[0] | ((u16)dstAddrInfo[1] << 8);
			return 1;
		}

		if (dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
			ZB_IEEE_ADDR_COPY(entry->dstExtAddrInfo.extAddr, dstAddrInfo);
			entry->dstExtAddrInfo.dstEp = dstAddrInfo[EXT_ADDR_LEN];
			return 1;
		}

		entry->used = 0;
		return 0;
	}

	return 0;
}

void aps_bindingTblEntryDel(aps_binding_entry_t *entry)
{
	u8 usedCount = aps_bindingTblEntryNum();
	aps_binding_entry_t *table = bindTblEntryGet();

	if (entry == NULL) {
		return;
	}

	entry->used = 0;

	if (usedCount == 0) {
		return;
	}

	u8 index = (u8)(entry - table);
	if (index < (u8)(usedCount - 1U)) {
		memcpy(entry, entry + 1, (usedCount - 1U - index) * sizeof(aps_binding_entry_t));
	}

	table[usedCount - 1U].used = 0;
}

void aps_bindingTblEntryDelByDstExtAddr(addrExt_t extAddr)
{
	aps_binding_entry_t *table = bindTblEntryGet();
	bool deleted = FALSE;

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		aps_binding_entry_t *entry = &table[i];

		if (!aps_binding_used(entry) || entry->dstAddrMode != APS_LONG_DSTADDR_WITHEP) {
			continue;
		}

		if (!ZB_IEEE_ADDR_CMP(entry->dstExtAddrInfo.extAddr, extAddr)) {
			continue;
		}

		aps_bindingTblEntryDel(entry);
		deleted = TRUE;
	}

	if (deleted) {
		tl_zbTaskPost(aps_bindingTblSave2Flash, NULL);
	}
}

aps_status_t aps_search_dst_from_bind_tbl(aps_data_req_t *apsreq, bind_dst_list_tbl *bindList)
{
	aps_binding_entry_t *table = bindTblEntryGet();
	bind_dst_list *out = bindList->list;
	u8 total = 0;

	for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
		aps_binding_entry_t *entry = &table[i];

		if (!aps_binding_used(entry)) {
			continue;
		}

		if (entry->srcEp != apsreq->src_endpoint ||
		    entry->clusterId != apsreq->cluster_id) {
			continue;
		}

		out->dst_addr_mode = entry->dstAddrMode;
		if (entry->dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
			ZB_IEEE_ADDR_COPY(out->aps_addr.dst_ext_addr,
					  entry->dstExtAddrInfo.extAddr);
			out->aps_addr.dst_endpoint = entry->dstExtAddrInfo.dstEp;
		} else {
			out->aps_addr.dst_group_addr = entry->groupAddr;
		}

		out++;
		total++;
	}

	bindList->txCnt = 0;
	bindList->totalCnt = total;

	return total ? APS_STATUS_SUCCESS : APS_STATUS_NO_BOUND_DEVICE;
}

aps_status_t aps_me_bind_req(aps_me_bind_req_t *amr)
{
	u16 clusterId = (u16)amr->cid16_l | ((u16)amr->cid16_h << 8);
	u8 *dstInfo = (u8 *)&amr->dst_group_addr;

	if (aps_bindingTblEntryNum() == APS_BINDING_TABLE_SIZE) {
		return APS_STATUS_TABLE_FULL;
	}

	if (aps_bindingTblMatch(clusterId, amr->src_ep, amr->dst_addr_mode, dstInfo) != NULL) {
		return APS_STATUS_SUCCESS;
	}

	if (!aps_bindingTblEntryAdd(clusterId, amr->src_ep, amr->dst_addr_mode, dstInfo)) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	tl_zbTaskPost(aps_bindingTblSave2Flash, NULL);
	return APS_STATUS_SUCCESS;
}

aps_status_t aps_me_unbind_req(aps_me_unbind_req_t *amr)
{
	u16 clusterId = (u16)amr->cid16_l | ((u16)amr->cid16_h << 8);
	aps_binding_entry_t *entry = aps_bindingTblMatch(clusterId, amr->src_ep, amr->dst_addr_mode,
							 (u8 *)&amr->dst_group_addr);
	if (entry == NULL) {
		return APS_STATUS_INVALID_BINDING;
	}

	aps_bindingTblEntryDel(entry);
	tl_zbTaskPost(aps_bindingTblSave2Flash, NULL);
	return APS_STATUS_SUCCESS;
}

void aps_me_init(void)
{
	if (aps_groupTblNvInit() == NV_SUCC) {
		aps_init_group_num_set();
	} else {
		aps_groupTblReset();
	}

	if (aps_bindingTblNvInit() != NV_SUCC) {
		aps_bindingTabInit();
	}

	aps_ib.aps_channel_mask = ZB_TRANSCEIVER_ALL_CHANNELS_MASK;
	memset(aps_ib.aps_use_ext_panid, 0, sizeof(aps_ib.aps_use_ext_panid));
	aps_ib.aps_designated_coordinator = FALSE;
	aps_ib.aps_nonmember_radius = 2;
	aps_ib.aps_use_insecure_join = TRUE;
	aps_ib.aps_authenticated = FALSE;
	aps_ib.aps_zdo_restricted_mode = FALSE;
	aps_ib.aps_interframe_delay = APS_INTERFRAME_DELAY;
	aps_ib.aps_max_window_size = APS_MAX_WINDOW_SIZE ? APS_MAX_WINDOW_SIZE : 1U;
	aps_ib.aps_fragment_payload_size = APS_FRAGMEMT_PAYLOAD_SIZE;
}

aps_status_t apsSetChnMsk(u32 chnMask)
{
	if ((chnMask & ~ZB_TRANSCEIVER_ALL_CHANNELS_MASK) != 0U) {
		return APS_STATUS_INVALID_PARAMETER;
	}

	aps_ib.aps_channel_mask = chnMask;
	return APS_STATUS_SUCCESS;
}

void tl_zbApsPibSet(u8 attribute, u8 length, const void *value)
{
	if (attribute == 1U) {
		memcpy(&aps_ib, value, length);
	}
}

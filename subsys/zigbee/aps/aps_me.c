/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/aps_me.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / zb_buffer.h / zb_task_queue.h are
 * replaced by the Zephyr include set.
 */
#include "zb_common_stub.h"
#include "common/static_assert.h"
#include "os/ev_timer.h"
#include "mac/includes/tl_zb_mac.h"
#include "mac/includes/tl_zb_mac_pib.h"
#include "nwk/includes/nwk.h"
#include "nwk/includes/nwk_internal.h"
#include "aps/aps_api.h"
#include "aps/aps_internal.h"


u8 APS_BINDING_TABLE_SIZE_V1 = APS_BINDING_TABLE_NUM_V1;
u16 APS_OLD_BINDING_TBL_SIZE_MAX = sizeof(aps_binding_table_t);
aps_pib_attributes_t aps_ib = {0};

static inline u16 aps_binding_table_bytes(void)
{
    return (u16)(APS_BINDING_TABLE_SIZE * sizeof(aps_binding_entry_t));
}

static inline bool aps_binding_used(const aps_binding_entry_t *entry)
{
    return entry->used == 1U;
}

u8 aps_oldBindingTblRecover(void)
{
    aps_binding_table_t oldTbl;
    u8 status;

    memset(&oldTbl, 0, sizeof(oldTbl));
    status = nv_flashReadNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE,
                             sizeof(oldTbl), (u8 *)&oldTbl);
    if (status == NV_SUCC) {
        aps_userBindingTblRestore(&oldTbl, sizeof(oldTbl), APS_BINDING_TABLE_NUM_V1);
        nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2,
                         aps_binding_table_bytes(), (u8 *)bindTblEntryGet());
    }

    return status;
}

void aps_userBindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum)
{
    aps_binding_table_t *oldBindingTbl = (aps_binding_table_t *)oldTbl;
    aps_binding_entry_t *newTbl = bindTblEntryGet();
    u16 maxBindings;
    u8 restored = 0;

    if (oldTbl == NULL || tblSize < sizeof(aps_binding_table_t)) {
        return;
    }

    memset(newTbl, 0, aps_binding_table_bytes());

    maxBindings = oldBindingTbl->bound_cnt;
    if (maxBindings > (u16)(APS_BINDING_TABLE_NUM_V1 * APS_BINDING_TABLE_NUM_V1)) {
        maxBindings = (u16)(APS_BINDING_TABLE_NUM_V1 * APS_BINDING_TABLE_NUM_V1);
    }
    if (eleNum < maxBindings) {
        maxBindings = eleNum;
    }

    for (u16 i = 0; i < maxBindings && restored < APS_BINDING_TABLE_SIZE; i++) {
        u8 srcIdx = oldBindingTbl->BoudList[i].srcTblIdx;
        u8 dstIdx = oldBindingTbl->BoudList[i].dstTblIdx;
        aps_bind_tbl_t *src;
        aps_bind_tbl_t *dst;
        aps_binding_entry_t *entry;

        if (srcIdx >= APS_BINDING_TABLE_NUM_V1 || dstIdx >= APS_BINDING_TABLE_NUM_V1) {
            continue;
        }

        src = &oldBindingTbl->table[srcIdx];
        dst = &oldBindingTbl->table[dstIdx];
        if (src->src_table.used != 1U || dst->dst_table.used != 1U) {
            continue;
        }

        entry = &newTbl[restored++];
        memset(entry, 0, sizeof(*entry));
        entry->clusterId = src->src_table.cluster_id;
        entry->srcEp = src->src_table.src_ep;
        entry->used = 1;

        if (dst->dst_table.dst_addr_mode == APS_BIND_DST_ADDR_GROUP) {
            entry->dstAddrMode = APS_SHORT_GROUPADDR_NOEP;
            entry->groupAddr = dst->dst_table.group_addr;
        } else {
            entry->dstAddrMode = APS_LONG_DSTADDR_WITHEP;
            tl_zbExtAddrByIdx(dst->dst_table.long_addr.dst_addr, entry->dstExtAddrInfo.extAddr);
            entry->dstExtAddrInfo.dstEp = dst->dst_table.long_addr.dst_end;
        }
    }
}

void aps_bindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum)
{
    aps_userBindingTblRestore(oldTbl, tblSize, eleNum);
}

void aps_bindingTabInit(void)
{
    memset(bindTblEntryGet(), 0, aps_binding_table_bytes());
}

void aps_bindingTblSave2Flash(void *arg)
{
    (void)arg;
    nv_flashWriteNew(1, NV_MODULE_APS, NV_ITEM_APS_BINDING_TABLE_V2,
                     aps_binding_table_bytes(), (u8 *)bindTblEntryGet());
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

u8 aps_bindingTblMatched(u16 clusterId, u8 srcEp)
{
    aps_binding_entry_t *table = bindTblEntryGet();

    for (u8 i = 0; i < APS_BINDING_TABLE_SIZE; i++) {
        if (!aps_binding_used(&table[i])) {
            continue;
        }

        if (table[i].clusterId == clusterId && table[i].srcEp == srcEp) {
            return 1;
        }
    }

    return 0;
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

        if (memcmp(table[i].dstExtAddrInfo.extAddr, extAddr, EXT_ADDR_LEN) == 0) {
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

        if (!aps_binding_used(entry)) {
            continue;
        }

        if (entry->clusterId != clusterId || entry->srcEp != srcEp || entry->dstAddrMode != dstAddrMode) {
            continue;
        }

        if (dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
            if (memcmp(entry->dstExtAddrInfo.extAddr, dstAddrInfo, EXT_ADDR_LEN) != 0) {
                continue;
            }

            if (entry->dstExtAddrInfo.dstEp != dstAddrInfo[EXT_ADDR_LEN]) {
                continue;
            }
        } else if (dstAddrMode == APS_SHORT_GROUPADDR_NOEP) {
            u16 groupAddr = (u16)dstAddrInfo[0] | ((u16)dstAddrInfo[1] << 8);
            if (entry->groupAddr != groupAddr) {
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
            memcpy(entry->dstExtAddrInfo.extAddr, dstAddrInfo, EXT_ADDR_LEN);
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
    aps_binding_entry_t *table = bindTblEntryGet();
    u8 usedCount = aps_bindingTblEntryNum();

    if (entry == NULL) {
        return;
    }

    entry->used = 0;

    if (usedCount == 0) {
        return;
    }

    u8 index = (u8)(entry - table);
    if (index < (u8)(usedCount - 1U)) {
        memmove(entry, entry + 1, (usedCount - 1U - index) * sizeof(aps_binding_entry_t));
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

        if (memcmp(entry->dstExtAddrInfo.extAddr, extAddr, EXT_ADDR_LEN) != 0) {
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

        if (entry->srcEp != apsreq->src_endpoint || entry->clusterId != apsreq->cluster_id) {
            continue;
        }

        out->dst_addr_mode = entry->dstAddrMode;
        if (entry->dstAddrMode == APS_LONG_DSTADDR_WITHEP) {
            memcpy(out->aps_addr.dst_ext_addr, entry->dstExtAddrInfo.extAddr, EXT_ADDR_LEN);
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
    aps_ib.aps_updateDevice_holdApsSecurity = FALSE;
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

void tl_zbApsPibSet(void *arg)
{
    if (arg != NULL) {
        memcpy(&aps_ib, arg, sizeof(aps_ib));
    }
}

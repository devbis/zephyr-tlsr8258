/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _APS_ME_H
#define _APS_ME_H

#include "zb_common.h"

u8 aps_oldBindingTblRecover(void);
void aps_userBindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum);
void aps_bindingTblRestore(void *oldTbl, u16 tblSize, u16 eleNum);
void aps_bindingTabInit(void);
void aps_bindingTblSave2Flash(void *arg);
u8 aps_bindingTblNvInit(void);
void aps_bindingTab_clear(void);
u8 aps_bindingTblEntryNum(void);
bool aps_bindingTblMatched(u16 clusterId, u8 srcEp);
u8 aps_bindingTblExist(addrExt_t extAddr);
u8 aps_bindingTblEntryAdd(u16 clusterId, u8 srcEp, u8 dstAddrMode, u8 *dstAddrInfo);
void aps_bindingTblEntryDel(aps_binding_entry_t *entry);
void aps_bindingTblEntryDelByDstExtAddr(addrExt_t extAddr);
aps_status_t aps_search_dst_from_bind_tbl(aps_data_req_t *apsreq, bind_dst_list_tbl *bindList);
aps_status_t aps_me_bind_req(aps_me_bind_req_t *amr);
aps_status_t aps_me_unbind_req(aps_me_bind_req_t *amr);
void aps_me_init(void);
aps_status_t apsSetChnMsk(u32 chnMask);
void tl_zbApsPibSet(u8 attribute, u8 length, const void *value);

#endif

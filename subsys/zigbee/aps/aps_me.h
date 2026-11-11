/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _APS_ME_H
#define _APS_ME_H

#include "zb_common.h"

uint8_t aps_oldBindingTblRecover(void);
void aps_userBindingTblRestore(void *oldTbl, uint16_t tblSize, uint16_t eleNum);
void aps_bindingTblRestore(void *oldTbl, uint16_t tblSize, uint16_t eleNum);
void aps_bindingTabInit(void);
void aps_bindingTblSave2Flash(void *arg);
uint8_t aps_bindingTblNvInit(void);
void aps_bindingTab_clear(void);
uint8_t aps_bindingTblEntryNum(void);
bool aps_bindingTblMatched(uint16_t clusterId, uint8_t srcEp);
uint8_t aps_bindingTblExist(addrExt_t extAddr);
uint8_t aps_bindingTblEntryAdd(uint16_t clusterId, uint8_t srcEp, uint8_t dstAddrMode, uint8_t *dstAddrInfo);
void aps_bindingTblEntryDel(aps_binding_entry_t *entry);
void aps_bindingTblEntryDelByDstExtAddr(addrExt_t extAddr);
aps_status_t aps_search_dst_from_bind_tbl(aps_data_req_t *apsreq, bind_dst_list_tbl *bindList);
aps_status_t aps_me_bind_req(aps_me_bind_req_t *amr);
aps_status_t aps_me_unbind_req(aps_me_bind_req_t *amr);
void aps_me_init(void);
aps_status_t apsSetChnMsk(uint32_t chnMask);
void zb_ApsPibSet(uint8_t attribute, uint8_t length, const void *value);

#endif

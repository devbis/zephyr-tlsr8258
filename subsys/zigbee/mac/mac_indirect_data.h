/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_INDIRECT_DATA_H
#define _MAC_INDIRECT_DATA_H

#include "zb_common.h"

#if defined(ZB_ROUTER_ROLE)
typedef struct mac_pending_entry {
	struct mac_pending_entry *next;
	void *buf;
	u8 addr[8];
	u8 addrMode;
	u8 timeout;
	u8 expiry;
	u8 state;
	u8 status;
} mac_pending_entry_t;
#endif

#if defined(ZB_ROUTER_ROLE)
int macIndirPeriodic(void *arg);
u8 macDataPending(void *buf, u32 dstAddrLo, u32 dstAddrHi, u8 dstAddrMode);
void macDataPendingListProc(void *arg);
void macDataPendingListManage(void *arg, u8 status);
u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 update);
int tl_zbMacPendingDataSearch(u8 addrMode, u8 *addr);
void tl_zbMacMlmeDataRequestCb(void *arg);
#endif
void tl_zbMacDataRequestStatusCheck(zb_buf_t *buf, u8 status);
u8 tl_zbMacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, u8 status);

#endif

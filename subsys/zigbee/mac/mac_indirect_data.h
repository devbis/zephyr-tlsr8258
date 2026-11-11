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
	addrExt_t addr;
	uint8_t addrMode;
	uint8_t timeout;
	uint8_t expiry;
	uint8_t state;
	uint8_t status;
} mac_pending_entry_t;
#endif

#if defined(ZB_ROUTER_ROLE)
int macIndirPeriodic(void *arg);
uint8_t macDataPending(void *buf, uint32_t dstAddrLo, uint32_t dstAddrHi, uint8_t dstAddrMode);
void macDataPendingListProc(void *arg);
void macDataPendingListManage(void *arg, uint8_t status);
uint8_t zb_MacPendingDataCheck(uint8_t addrMode, uint8_t *addr, uint8_t update);
int zb_MacPendingDataSearch(uint8_t addrMode, uint8_t *addr);
void zb_MacMlmeDataRequestCb(void *arg);
#endif
void zb_MacDataRequestStatusCheck(zb_buf_t *buf, uint8_t status);
uint8_t zb_MacMlmeDataRequestCmdSend(zb_mlme_data_req_cmd_t *req, zb_buf_t *buf, uint8_t status);

#endif

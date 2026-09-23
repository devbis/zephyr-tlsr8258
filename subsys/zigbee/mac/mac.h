/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _MAC_H
#define _MAC_H

#include "zb_common.h"

/* Handles reserved for internal MAC command/confirmation paths. */
typedef enum {
	MAC_INTERNAL_ASSOCIATE_REQUEST_HANDLE = 0xe0,
	MAC_INTERNAL_ASSOCIATE_RESPONSE_HANDLE,
	MAC_INTERNAL_DISASSOCIATE_NOTIFICATION_HANDLE,
	MAC_INTERNAL_BEACON_SEND_HANDLE,
	MAC_INTERNAL_BEACON_REQUEST_HANDLE,
	MAC_INTERNAL_ORPHAN_RESPONSE_HANDLE,
	MAC_INTERNAL_START_REQUEST_HANDLE,
	MAC_INTERNAL_PANID_CONFLICT_HANDLE,
	MAC_INTERNAL_DATA_REQUEST_HANDLE,
	MAC_INTERNAL_MLME_DATA_REQUEST_HANDLE,
	MAC_INTERNAL_SCAN_HANDLE,
} mac_internal_handle_e;

typedef struct {
	u32 timestamp;
	u8 *payload;
	u8 dstAddrMode;
	u8 srcAddrMode;
	u8 srcAddr[8];
	u8 frameType;
	u8 payloadLen;
	u8 linkQuality;
	u8 curChannel;
} mac_phy_ind_meta_t;

extern mac_appIndCb_t *macAppIndCb;

void tl_zbPhyIndication(void *arg, u8 *raw, u8 len);
void tl_zbMacChannelSet(u8 chan);
void mac_pibNvInit(u8 coldReset);
void tl_zbMacReset(void);
void tl_zbMacInit(u8 coldReset);
void tl_zbMaxTxConfirmCb(void *arg, u8 status);
void tl_zbMacTaskProc(void);
void mac_appIndCbRegister(mac_appIndCb_t *cb);
void generateIEEEAddr(void);
zb_buf_t *zb_buf_allocate(void);

#endif

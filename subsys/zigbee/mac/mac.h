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

/* Receive indication record at the start of the zb_buf, as mac_rxDataParse()
 * fills it from the parsed MAC header: source PAN ID, source address (short
 * or extended, according to srcAddrMode) and source address mode.
 */
typedef struct {
	uint32_t timestamp;
	uint8_t *payload;
	uint16_t srcPanId;
	addrExt_t srcAddr;
	uint8_t srcAddrMode;
	uint8_t payloadLen;
	uint8_t linkQuality;
	uint8_t curChannel;
} mac_phy_ind_meta_t;

extern mac_appIndCb_t *macAppIndCb;

void zb_PhyIndication(void *arg, uint8_t *raw, uint8_t len);
void zb_MacChannelSet(uint8_t chan);
void mac_pibNvInit(uint8_t coldReset);
void zb_MacReset(void);
void zb_MacInit(uint8_t coldReset);
void zb_MaxTxConfirmCb(void *arg, uint8_t status);
void zb_MacTaskProc(void);
void mac_appIndCbRegister(mac_appIndCb_t *cb);
void generateIEEEAddr(void);
zb_buf_t *zb_buf_allocate(void);

#endif

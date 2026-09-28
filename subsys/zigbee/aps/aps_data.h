/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _APS_DATA_H
#define _APS_DATA_H

#include "zb_common.h"
#include "aps_stackUse.h"

/* APS frame-control field: frame type occupies bits 0..1 and delivery mode
 * occupies bits 2..3.  These are local wire-format definitions; the vendor
 * APS API header only defines the public address-mode enumeration. */
#define APS_FRAME_CTRL_FRAME_TYPE_MASK         0x03U
#define APS_FRAME_CTRL_FRAME_TYPE_DATA         0x00U
#define APS_FRAME_CTRL_FRAME_TYPE_COMMAND      0x01U
#define APS_FRAME_CTRL_FRAME_TYPE_ACK          0x02U
#define APS_FRAME_CTRL_DELIVERY_MODE_MASK      0x0cU
#define APS_FRAME_CTRL_DELIVERY_MODE_BROADCAST 0x08U
#define APS_FRAME_CTRL_DELIVERY_MODE_GROUP     0x0cU
#define APS_FRAME_CTRL_ACK_FORMAT              0x10U
#define APS_FRAME_CTRL_SECURITY                0x20U
#define APS_FRAME_CTRL_ACK_REQUEST             0x40U
#define APS_FRAME_CTRL_EXTENDED_HEADER         0x80U
#define APS_FRAME_CTRL_DATA_ACK                APS_FRAME_CTRL_FRAME_TYPE_ACK
#define APS_FRAME_CTRL_COMMAND_ACK (APS_FRAME_CTRL_FRAME_TYPE_ACK | APS_FRAME_CTRL_ACK_FORMAT)
#define APS_FRAME_CTRL_EXTENDED_DATA_ACK                                                           \
	(APS_FRAME_CTRL_FRAME_TYPE_ACK | APS_FRAME_CTRL_EXTENDED_HEADER)

enum {
	APS_CMD_TRANSPORT_KEY_ID = 5,
	APS_CMD_UPDATE_DEVICE_ID = 6,
	APS_CMD_REMOVE_DEVICE_ID = 7,
	APS_CMD_REQUEST_KEY_ID = 8,
	APS_CMD_SWITCH_KEY_ID = 9,
	APS_CMD_TUNNEL_ID = 14,
	APS_CMD_VERIFY_KEY_ID = 15,
	APS_CMD_CONFIRM_KEY_ID = 16,
};

typedef struct _attribute_packed_ {
	zb_buf_t *txBuf;
	u8 *adu;
	tl_zb_addr_t dstAddr;
	u8 addrMode;
	u8 aduLen;
	u8 secure;
	u8 secureNwkLayer;
	u8 reserved;
} aps_cmd_send_req_t;

void aps_nwk_data_confirm_cb(void *arg);
void aps_nwk_data_indication_cb(void *arg);
void aps_interPanDataIndCb(void *arg);
int apsDuplicatePeriodic(void *arg);
int apsAckPeriodic(void *arg);
void aps_data_request(void *arg);
void aps_cmd_send(void *arg, u8 handle);

void tl_apsDataIndRegister(apsDataIndCb_t cb);
void apsCleanToStopSecondClock(void);
u8 aps_duplicate_check(u16 src_addr, u8 aps_counter);
void apsTxEventPost(aps_tx_cache_list_t *cache, aps_tx_cache_evt_e event, u8 status);
u8 apsHandleIsExit(u8 handle);
u8 apsDataRequest(aps_data_req_t *dataReq, u8 *asdu, u8 length);
u8 apsDataFragmentRequest(aps_data_req_t *dataReq, u8 *asdu, u16 length);

extern u16 dstPanID;
extern u8 g_apsTxCacheNum;
extern u8 deviceInfoRsp;

#endif

/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZDP_SERVICES_H
#define _ZDP_SERVICES_H

#include "zb_common.h"

int apsParentAnncePeriodic(void *arg);
void zdo_send_req(zdo_zdp_req_t *req);
uint8_t zdo_end_device_bind_req(const zdo_edBindReq_t *req, zdo_zdp_req_t *zzr);
void zdo_device_announce_send(void);
void zdo_parent_announce_send(void);
void zdo_bind_toggle_cb(void *arg);
void zdo_end_device_unbind_cb(void *arg);
void zdo_end_device_bind_cb(void *arg);
void zdo_devAnnce(uint16_t nwkAddr, const addrExt_t ieeeAddr, uint8_t capability);
zdo_status_t zdo_apsmeBindUnBind(bool bind, aps_me_bind_req_t *req);
void zdo_apsParentAnnceTimerStart(void);
#if defined(ZB_ROUTER_ROLE)
void zdo_parentAnnounceIndicate(void *arg);
void zdo_remoteAddrNotify(void *arg);
void zdo_parentAnnounceNotify(void *arg);
#endif
uint8_t zdp_data_send(uint8_t *payload, uint8_t payloadLen, zdo_zdp_req_t *req);

#endif

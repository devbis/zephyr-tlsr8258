/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _ZDO_H
#define _ZDO_H

#include "zb_common.h"

u8 zdo_af_get_accept_nwk_update_channel(void);
u16 zdo_af_get_accept_nwk_update_pan_id(void);
u8 zdo_af_get_link_retry_threshold(void);
u16 zdo_af_get_max_rejoin_backoff_time(void);
bool zdo_af_get_mgmtLeave_use_aps_sec(void);
u16 zdo_af_get_nwk_time_btwn_scans(void);
u8 zdo_af_get_permit_join_duration(void);
u16 zdo_af_get_rejoin_backoff_iteration(void);
u16 zdo_af_get_rejoin_backoff_time(void);
u16 zdo_af_get_rejoin_duration(void);
u8 zdo_af_get_rejoin_times(void);
u8 zdo_af_get_scan_attempts(void);
u32 zdo_af_get_syn_rate(void);
bool zdo_af_get_use_tc_sec_on_nwk_key_rotation(void);
void zdo_zdpCbTblRegister(zdo_appIndCb_t *cbTbl);
void zdo_init(void);

#endif

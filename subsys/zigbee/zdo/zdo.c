/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/zdo.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / ev_timer.h are replaced by the
 * Zephyr include set.
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
#include "zdo/zdo_api.h"
#include "zdo/zdp.h"
#include "zdo/zdo_internal.h"

#define ZDO_DEFAULT_POLL_RATE_MS                 1000U
#define ZDO_DEFAULT_TIME_BETWEEN_SCANS_MS        100U
#define ZDO_DEFAULT_SCAN_ATTEMPTS                5U
#define ZDO_DEFAULT_PERMIT_JOIN_DURATION         0U
#define ZDO_DEFAULT_LINK_RETRY_THRESHOLD         3U
#define ZDO_DEFAULT_REJOIN_TIMES                 0U
#define ZDO_DEFAULT_REJOIN_DURATION_S            0U
#define ZDO_DEFAULT_REJOIN_BACKOFF_S             0U
#define ZDO_DEFAULT_MAX_REJOIN_BACKOFF_S         0U
#define ZDO_DEFAULT_REJOIN_BACKOFF_ITERATION     0U
#define ZDO_DEFAULT_ACCEPT_UPDATE_PAN_ID         0xFFFFU
#define ZDO_DEFAULT_ACCEPT_UPDATE_CHANNEL        0xFFU
#define ZDO_DEFAULT_USE_MGMT_LEAVE_APS_SEC       FALSE
#define ZDO_DEFAULT_USE_TC_SEC_ON_KEY_ROTATION   FALSE
#define ZDO_DEFAULT_SCAN_DURATION                5U

zdo_appIndCb_t *zdoAppIndCbLst = NULL;
zdo_touchLinkCb_t *zdoTouchLinkCb = NULL;

extern const zdo_attrCfg_t zdoCfgAttrDefault;

zdo_attrCfg_t zdo_cfg_attributes;

void zdo_zdpCbTblRegister(zdo_appIndCb_t *cbTbl)
{
    zdoAppIndCbLst = cbTbl;
}

void zdo_touchLinkCbRegister(zdo_touchLinkCb_t *cbTbl)
{
    zdoTouchLinkCb = cbTbl;
}

void zdo_af_set_link_retry_threshold(u8 threshold)
{
    zdo_cfg_attributes.config_parent_link_retry_threshold = threshold;
}

u8 zdo_af_get_link_retry_threshold(void)
{
    return zdo_cfg_attributes.config_parent_link_retry_threshold;
}

u32 zdo_af_get_syn_rate(void)
{
    return zdo_cfg_attributes.config_nwk_indirectPollRate;
}

void zdo_af_set_syn_rate(u32 newRate)
{
    zdo_cfg_attributes.config_nwk_indirectPollRate = newRate;
}

void zdo_af_set_rejoin_times(u8 times)
{
    zdo_cfg_attributes.config_rejoin_times = times;
}

u8 zdo_af_get_rejoin_times(void)
{
    return zdo_cfg_attributes.config_rejoin_times;
}

void zdo_af_set_rejoin_duration(u16 duration)
{
    zdo_cfg_attributes.config_rejoin_duration = duration;
}

u16 zdo_af_get_rejoin_duration(void)
{
    return zdo_cfg_attributes.config_rejoin_duration;
}

void zdo_af_set_rejoin_backoff_time(u16 interval)
{
    zdo_cfg_attributes.config_rejoin_backoff_time = interval;
}

u16 zdo_af_get_rejoin_backoff_time(void)
{
    return zdo_cfg_attributes.config_rejoin_backoff_time;
}

void zdo_af_set_max_rejoin_backoff_time(u16 interval)
{
    zdo_cfg_attributes.config_max_rejoin_backoff_time = interval;
}

u16 zdo_af_get_max_rejoin_backoff_time(void)
{
    return zdo_cfg_attributes.config_max_rejoin_backoff_time;
}

void zdo_af_set_rejoin_backoff_iteration(u16 iteration)
{
    zdo_cfg_attributes.config_rejoin_backoff_iteration = iteration;
}

u16 zdo_af_get_rejoin_backoff_iteration(void)
{
    return zdo_cfg_attributes.config_rejoin_backoff_iteration;
}

void zdo_af_set_accept_nwk_update_pan_id(u16 panId)
{
    zdo_cfg_attributes.config_accept_nwk_update_pan_id = panId;
}

u16 zdo_af_get_accept_nwk_update_pan_id(void)
{
    return zdo_cfg_attributes.config_accept_nwk_update_pan_id;
}

void zdo_af_set_accept_nwk_update_channel(u8 channel)
{
    zdo_cfg_attributes.config_accept_nwk_update_channel = channel;
}

u8 zdo_af_get_accept_nwk_update_channel(void)
{
    return zdo_cfg_attributes.config_accept_nwk_update_channel;
}

void zdo_af_set_mgmtLeave_use_aps_sec(bool enabled)
{
    zdo_cfg_attributes.config_mgmt_leave_use_aps_sec = enabled;
}

bool zdo_af_get_mgmtLeave_use_aps_sec(void)
{
    return zdo_cfg_attributes.config_mgmt_leave_use_aps_sec;
}

void zdo_af_set_use_tc_sec_on_nwk_key_rotation(bool enabled)
{
    zdo_cfg_attributes.config_use_tc_sec_on_nwk_key_rotation = enabled;
}

bool zdo_af_get_use_tc_sec_on_nwk_key_rotation(void)
{
    return zdo_cfg_attributes.config_use_tc_sec_on_nwk_key_rotation;
}

u8 zdo_af_get_scan_attempts(void)
{
    return zdo_cfg_attributes.config_nwk_scan_attempts;
}

void zdo_af_set_scan_attempts(u8 attempts)
{
    zdo_cfg_attributes.config_nwk_scan_attempts = attempts;
}

u16 zdo_af_get_nwk_time_btwn_scans(void)
{
    return zdo_cfg_attributes.config_nwk_time_btwn_scans;
}

u8 zdo_af_get_permit_join_duration(void)
{
    return zdo_cfg_attributes.config_permit_join_duration;
}

u8 zdo_channel_page2num(u32 channelMask)
{
    for (u8 ch = 11U; ch < 27U; ch++) {
        if ((channelMask >> ch) & 0x1U) {
            return ch;
        }
    }

    return 27U;
}

void zdo_startupAttrCfg(zdo_startup_attr_t *startupAttr)
{
    if (memcmp(startupAttr->nwkExtendedPANID, g_zero_addr, EXT_ADDR_LEN) == 0) {
        memcpy(aps_ib.aps_use_ext_panid, startupAttr->apsUseExtendedPANID, EXT_ADDR_LEN);
        aps_ib.aps_designated_coordinator = startupAttr->apsDesignatedCoordinator;
    } else {
        memcpy(g_zbNIB.extPANId, startupAttr->nwkExtendedPANID, EXT_ADDR_LEN);
    }

    aps_ib.aps_channel_mask = startupAttr->apsChannelMask;
    aps_ib.aps_use_insecure_join = startupAttr->apsUseInsecureJoin & 0x1U;
}

void zdo_init(void)
{
    zdp_init();
    memcpy(&zdo_cfg_attributes, &zdoCfgAttrDefault, sizeof(zdo_cfg_attributes));
    /* Single-shot scan so bdb_nwkDiscCnfCb fires after the first scan
     * round and we proceed to AssocReq instead of looping inside
     * zdo_nlme_network_discovery_confirm_cb's attempt counter.
     */
    zdo_cfg_attributes.config_nwk_scan_attempts = 1U;
}

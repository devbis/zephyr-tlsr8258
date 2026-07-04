/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Adapted from libzigbee/src/aps.c. Vendor file kept structurally
 * one-for-one; vendor zb_local.h / zb_buffer.h / zb_task_queue.h are
 * replaced by the Zephyr include set.
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

typedef struct _attribute_packed_ {
    u8 reserved0;
    u8 frame_control;
    u8 dst_endpoint;
    u8 src_endpoint;
    u8 reserved4;
    u8 reserved5;
    u8 field6;
    u8 field7;
    u8 field8;
    u8 field9;
    u8 field10;
    u8 field11;
    u8 aps_counter;
    u8 ext_header;
    u8 block_number;
} aps_hdr_parsed_t;

enum {
    APS_TX_DUP_BUF_SIZE = 0xc3,
};

static u8 aps_counter_value;
static u8 aps_handle;
aps_group_q_t aps_group_q;

static inline u16 aps_u16_get(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static inline u32 aps_u32_get(const u8 *p)
{
    return (u32)p[0] |
           ((u32)p[1] << 8) |
           ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

u8 aps_get_counter_value(void)
{
    u8 value = aps_counter_value;
    aps_counter_value = value + 1U;
    return value;
}

u8 aps_get_current_counter_value(void)
{
    return (u8)(aps_counter_value - 1U);
}

u8 aps_get_handle(void)
{
    u8 value = aps_handle;
    aps_handle = value + 1U;
    return value & 0x3fU;
}

void aps_init(void)
{
    aps_counter_value = (u8)drv_u32Rand();
    aps_handle = 0;
    aps_me_init();
}

aps_status_t aps_txBufInit(zb_buf_t **buf, void **payload, u8 size)
{
    zb_buf_t *allocated = zb_buf_allocate();

    if (allocated == NULL) {
        return APS_STATUS_TABLE_FULL;
    }

    *buf = allocated;
    *payload = tl_bufInitalloc(allocated, size);

    return APS_STATUS_SUCCESS;
}

u8 aps_hdr_parse(u8 *data, aps_hdr_parsed_t *parsed)
{
    u8 frame_control = data[0];
    u8 delivery_mode = frame_control & 0x03U;
    u8 frame_type = frame_control & 0x0cU;
    u8 *cursor = data + 1;

    memset(parsed, 0, sizeof(*parsed));
    parsed->frame_control = frame_control;

    if (delivery_mode == 3U) {
        u16 value = aps_u16_get(data + 1);
        parsed->field8 = (u8)value;
        parsed->field9 = (u8)(value >> 8);

        value = aps_u16_get(data + 3);
        parsed->field10 = (u8)value;
        parsed->field11 = (u8)(value >> 8);

        cursor = data + 5;
        return (u8)(cursor - data);
    }

    if ((frame_control & 0x80U) != 0U || delivery_mode == 1U) {
        parsed->dst_endpoint = data[1];
        cursor = data + 2;
        return (u8)(cursor - data);
    }

    if (frame_type != 0U && frame_type != 0x08U) {
        u16 value = aps_u16_get(data + 1);
        parsed->field6 = (u8)value;
        parsed->field7 = (u8)(value >> 8);

        value = aps_u16_get(data + 3);
        parsed->field8 = (u8)value;
        parsed->field9 = (u8)(value >> 8);

        value = aps_u16_get(data + 5);
        parsed->field10 = (u8)value;
        parsed->field11 = (u8)(value >> 8);

        parsed->src_endpoint = data[7];
        parsed->dst_endpoint = data[8];
        cursor = data + 9;
    } else {
        u16 value;

        parsed->field6 = data[1];

        value = aps_u16_get(data + 2);
        parsed->field8 = (u8)value;
        parsed->field9 = (u8)(value >> 8);

        value = aps_u16_get(data + 4);
        parsed->field10 = (u8)value;
        parsed->field11 = (u8)(value >> 8);

        parsed->src_endpoint = data[6];
        parsed->dst_endpoint = data[7];
        cursor = data + 8;
    }

    if ((frame_control & 0x80U) != 0U) {
        parsed->aps_counter = *cursor++;

        if (parsed->aps_counter != 0U) {
            parsed->ext_header = *cursor++;
        }

        if (delivery_mode == 2U) {
            parsed->block_number = *cursor++;
        }
    }

    return (u8)(cursor - data);
}

void aps_command_handle(void *arg)
{
    aps_data_ind_t *ind = (aps_data_ind_t *)arg;
    u16 dst = ind->dst_addr;
    u16 local = g_zbInfo.nwkNib.nwkAddr;
    u8 cmdId = ind->asdu[0];

    /*
     * Vendor libzigbee check looked like `if (dst == local) drop;`
     * which inverts the intended semantics: APS commands addressed
     * to us (TC → router Transport-Key, Switch-Key, etc.) MUST be
     * processed locally, not dropped. The original Zephyr port
     * carried the inverted comparison through, which silently
     * dropped every inbound Transport-Key frame and left the
     * device stuck in auth-wait forever (see ZDO trace in
     * zephyr-docs/router-rx-fix-step0-1-2-progress-2026-06-20.md).
     * Flip the test: drop if NOT for us (we don't relay APS
     * commands here — there's a separate relay path in
     * ss_apsTransportKeyCmdHandle for the parent-router case).
     */
    if (dst != local) {
        zb_buf_free((zb_buf_t *)arg);
        return;
    }

    switch (cmdId) {
    case 5:
        ss_apsTransportKeyCmdHandle(arg);
        break;
#if defined(ZB_ROUTER_ROLE)
    case 7:
        if (!ss_securityModeIsDistributed() && ((ind->security_status & SECURITY_IN_APSLAYER) != 0U)) {
            ss_apsRemoveDeviceCmdHandle(arg);
            return;
        }
        zb_buf_free((zb_buf_t *)arg);
        return;
#endif
    case 9:
        ss_apsSwitchKeyCmdHandle(arg);
        break;
#if defined(ZB_ROUTER_ROLE)
    case 14:
        ss_apsTunnelCmdHandle(arg);
        return;
#endif
    case 16:
        ss_apsConfirmKeyCmdHandle(arg);
        break;
    default:
        zb_buf_free((zb_buf_t *)arg);
        break;
    }
}

void aps_process_group_addressed_packet(zb_buf_t *buf)
{
    u8 pending = (u8)((aps_group_q.pending_wptr - aps_group_q.pending_rptr) & 0x03U);

    if (pending == 3U) {
        zb_buf_free(buf);
        return;
    }

    aps_group_q.pending_refs[aps_group_q.pending_wptr & 0x03U] = (u8)ZB_REF_FROM_BUF(buf);
    aps_group_q.pending_wptr++;
    buf->hdr.handle = 0;
    tl_zbTaskPost(aps_deliver_group_msg, NULL);
}

void aps_deliver_group_msg(void *arg)
{
    (void)arg;

    zb_buf_t *out = zb_buf_allocate();

    if (out == NULL) {
        if (aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
            tl_zbTaskPost(aps_deliver_group_msg, NULL);
        }
        return;
    }

    if (aps_group_q.active_buf == NULL) {
        if (aps_group_q.pending_rptr == aps_group_q.pending_wptr) {
            zb_buf_free(out);
            return;
        }

        {
            u8 ref = aps_group_q.pending_refs[aps_group_q.pending_rptr & 0x03U];

            aps_group_q.pending_rptr++;
            if (ref != 0xffU) {
                u8 epNum = 0;
                zb_buf_t *src = ZB_BUF_FROM_REF(ref);
                u16 groupAddr = (u16)src->buf[2] | ((u16)src->buf[3] << 8);
                u8 *eps = aps_group_ep_info_get(groupAddr, &epNum);

                aps_group_q.active_buf = src;
                aps_group_q.active_ep_count = epNum;
                if (eps != NULL && epNum != 0U) {
                    memcpy(aps_group_q.active_eps, eps, epNum);
                }
            }
        }
    }

    if (aps_group_q.active_buf == NULL ||
        aps_group_q.active_ep_count == 0U ||
        aps_group_q.active_buf->hdr.handle >= aps_group_q.active_ep_count) {
        if (aps_group_q.active_buf != NULL) {
            zb_buf_free(aps_group_q.active_buf);
        }
        aps_group_q.active_buf = NULL;
        aps_group_q.active_ep_count = 0;
        memset(aps_group_q.active_eps, 0, sizeof(aps_group_q.active_eps));
        zb_buf_free(out);

        if (aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
            tl_zbTaskPost(aps_deliver_group_msg, NULL);
        }
        return;
    }

    memcpy(out, aps_group_q.active_buf, APS_TX_DUP_BUF_SIZE);
    out->hdr.handle = 0;
    {
        aps_data_ind_t *srcInd = (aps_data_ind_t *)aps_group_q.active_buf;
        aps_data_ind_t *dstInd = (aps_data_ind_t *)out;

        dstInd->asdu = (u8 *)out + (srcInd->asdu - (u8 *)aps_group_q.active_buf);
        dstInd->dst_ep = aps_group_q.active_eps[aps_group_q.active_buf->hdr.handle];
    }
    aps_group_q.active_buf->hdr.handle++;
    tl_zbTaskPost(af_aps_data_entry, out);

    if (aps_group_q.active_buf->hdr.handle >= aps_group_q.active_ep_count) {
        zb_buf_free(aps_group_q.active_buf);
        aps_group_q.active_buf = NULL;
        aps_group_q.active_ep_count = 0;
        memset(aps_group_q.active_eps, 0, sizeof(aps_group_q.active_eps));
    }

    if (aps_group_q.active_buf != NULL || aps_group_q.pending_rptr != aps_group_q.pending_wptr) {
        tl_zbTaskPost(aps_deliver_group_msg, NULL);
    }
}

/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "zb_common_stub.h"

enum {
	ZB_RADIO_PORT_TRX_RX = 1,
};

int zb_radio_port_set_trx_state(u8 state, u8 channel);
int zb_radio_port_set_channel(u8 channel);
void zb_radio_port_update_filters(u16 pan_id, u16 short_addr, const addrExt_t ieee);

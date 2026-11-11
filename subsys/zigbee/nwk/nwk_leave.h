/* Copyright 2026 libzigbee
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef _NWK_LEAVE_H
#define _NWK_LEAVE_H

#include "zb_common.h"
#include "nwk.h"
#include "nwk_addr_map.h"

/*
 * The device address of the NLME-LEAVE.confirm of a leave command that went out to dstAddr.
 *
 * A device leaves itself with a broadcast (NWK_BROADCAST_RX_ON_WHEN_IDLE), and the confirmation
 * has to say so with the address the request carried: the null one. zdo_nlme_leave_confirm_cb()
 * acts on the null address or on its own; the invalid address (all 0xff) it takes for another
 * device and forgets it, so a router never ended its own leave: the network stayed in the NV.
 * A unicast leave names the device by its short address; the invalid address stays when the
 * address map no longer knows it.
 */
static inline void nwk_leaveCnfAddr(addrExt_t extAddr, uint16_t dstAddr)
{
	if (ZB_NWK_IS_ADDRESS_BROADCAST(dstAddr)) {
		ZB_IEEE_ADDR_ZERO(extAddr);
		return;
	}

	ZB_IEEE_ADDR_INVALID(extAddr);
	(void)zb_address_ieee_by_short(dstAddr, extAddr);
}

void nwk_leaveCmdSendCnf(void *arg, uint16_t dstAddr);
int nwkLeaveReqSend(void *arg, nwk_hdr_t *pNwkHdr, nwkCmd_t *cmd, uint8_t handle);

#endif

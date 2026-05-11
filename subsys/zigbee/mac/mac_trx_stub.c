/* SPDX-License-Identifier: Apache-2.0 */

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmacro-redefined"
#endif
#include "zb_common_stub.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

sys_diagnostics_t g_sysDiags __attribute__((weak));
zb_info_t g_zbInfo __attribute__((weak));

u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt) __attribute__((weak));
u8 *zb_macDataFilter(u8 *macPld, u8 len, u8 *needDrop, u8 *ackPkt)
{
	ARG_UNUSED(len);

	if (needDrop != NULL) {
		*needDrop = 1;
	}

	if (ackPkt != NULL) {
		*ackPkt = 0;
	}

	return macPld;
}

void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
	__attribute__((weak));
void zb_macDataRecvHandler(u8 *rxBuf, u8 *data, u8 len, u8 ackPkt, u32 timestamp, s8 rssi)
{
	ARG_UNUSED(rxBuf);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(ackPkt);
	ARG_UNUSED(timestamp);
	ARG_UNUSED(rssi);
}

void zb_macDataSendHandler(void) __attribute__((weak));
void zb_macDataSendHandler(void)
{
}

u8 tl_zbMacHdrSize(u16 frameCtrl) __attribute__((weak));
u8 tl_zbMacHdrSize(u16 frameCtrl)
{
	ARG_UNUSED(frameCtrl);
	return 0;
}

u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 send) __attribute__((weak));
u8 tl_zbMacPendingDataCheck(u8 addrMode, u8 *addr, u8 send)
{
	ARG_UNUSED(addrMode);
	ARG_UNUSED(addr);
	ARG_UNUSED(send);
	return 1;
}

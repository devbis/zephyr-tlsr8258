/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_initialize.h"
#include "second_clock.h"
#include "ss_zdoSecurityME.h"

zb_info_t g_zbInfo = {0};
aps_pib_attributes_t aps_ib = {0};

const addrExt_t g_zero_addr = {0};
const u8 touchLinkKeyMaster[SEC_KEY_LEN] = {
	0x9f, 0x55, 0x95, 0xf1, 0x02, 0x57, 0xc8, 0xa4,
	0x69, 0xcb, 0xf4, 0x2b, 0xc9, 0x3f, 0xee, 0x31,
};
const u8 g_null_securityKey[SEC_KEY_LEN] = {0};
const u8 linkKeyDistributedMaster[SEC_KEY_LEN] = {
	0x81, 0x42, 0x86, 0x86, 0x5d, 0xc1, 0xc8, 0xb2,
	0xc8, 0xcb, 0xc5, 0x2e, 0x5d, 0x65, 0xd1, 0xb8,
};
const addrExt_t g_invalid_addr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

u8 zb_info_load(void)
{
	return nv_flashReadNew(1, 0, 1, sizeof(g_zbInfo), (u8 *)&g_zbInfo);
}

void zb_info_save(void *arg)
{
	(void)arg;
	(void)nv_flashWriteNew(1, 0, 1, sizeof(g_zbInfo), (u8 *)&g_zbInfo);
}

_attribute_no_inline_ void zb_nwkKeySet(void)
{
	const u8 *nwkKey = nwkKeyDefault;

	for (u8 i = 0; i < SEC_KEY_LEN; i++) {
		if (nwkKeyDefault[i] != 0U) {
			ss_zdoNwkKeyConfigure((u8 *)nwkKeyDefault, 0, 1);
			return;
		}
	}

	u8 randomKey[SEC_KEY_LEN];
	for (u8 i = 0; i < SEC_KEY_LEN; i += 2U) {
		u16 rnd = (u16)drv_u32Rand();
		COPY_U16TOBUFFER(randomKey + i, rnd);
	}

	nwkKey = randomKey;
	ss_zdoNwkKeyConfigure((u8 *)nwkKey, 0, 1);
}

void zb_reset(void)
{
	tl_zbMacReset();
	tl_zbNwkInit(1);
	aps_init();
#if defined(ZB_ROUTER_ROLE)
	zb_nwkKeySet();
	ss_zdoUseKey(0);
#endif
	ss_ib.ssTimeoutPeriod = 0;
	ss_ib.outgoingFrameCounter = 0;
	tl_bdbAttrInit();
}

void zb_init(void)
{
	if (nv_facrotyNewRstFlagCheck()) {
		nv_resetToFactoryNew();
	}

	/* "10: tjeq 3a" - a successful zb_info_load() is the warm start, and the
	 * warm path rejoins the common tail at af_init() ("4a: tj 0x30"), skipping
	 * only the key setup and tl_bdbAttrInit(). */
	if (zb_info_load() == NV_SUCC) {
		tl_zbMacInit(0);
		tl_zbNwkInit(0);
		aps_init();
	} else {
		tl_zbMacInit(1);
		tl_zbNwkInit(1);
		aps_init();
#if defined(ZB_ROUTER_ROLE)
		zb_nwkKeySet();
		ss_zdoUseKey(0);
#endif
		tl_bdbAttrInit();
	}

	af_init();
	zdo_init();
}

void os_reset(u8 isRetention)
{
	if (!isRetention) {
		ev_buf_init();
		ev_timer_init();
		zb_sched_init();
		tl_zbBufferInit();
	}

	secondClockRun();
}

void os_init(u8 isRetention)
{
	os_reset(isRetention);
}

bool tl_stackBusy(void)
{
	if (rf_busyFlag != 0U) {
		return TRUE;
	}

	return tl_zbMacStateBusy() ? TRUE : FALSE;
}

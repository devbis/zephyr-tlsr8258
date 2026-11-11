/*
 * Copyright 2026 libzigbee
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "zb_common.h"
#include "zb_initialize.h"
#include "second_clock.h"
#include "ss_zdoSecurityME.h"
#include <zephyr/zigbee/zb_bootstrap.h>

zb_info_t g_zbInfo = {0};
aps_pib_attributes_t aps_ib = {0};

const addrExt_t g_zero_addr = {0};
const uint8_t touchLinkKeyMaster[SEC_KEY_LEN] = {
	0x9f, 0x55, 0x95, 0xf1, 0x02, 0x57, 0xc8, 0xa4,
	0x69, 0xcb, 0xf4, 0x2b, 0xc9, 0x3f, 0xee, 0x31,
};
const uint8_t g_null_securityKey[SEC_KEY_LEN] = {0};
const uint8_t linkKeyDistributedMaster[SEC_KEY_LEN] = {
	0x81, 0x42, 0x86, 0x86, 0x5d, 0xc1, 0xc8, 0xb2,
	0xc8, 0xcb, 0xc5, 0x2e, 0x5d, 0x65, 0xd1, 0xb8,
};
const addrExt_t g_invalid_addr = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

uint8_t zb_info_load(void)
{
	return nv_flashReadNew(1, 0, 1, sizeof(g_zbInfo), (uint8_t *)&g_zbInfo);
}

void zb_info_save(void *arg)
{
	(void)arg;
	(void)nv_flashWriteNew(1, 0, 1, sizeof(g_zbInfo), (uint8_t *)&g_zbInfo);
}

_attribute_no_inline_ void zb_nwkKeySet(void)
{
	const uint8_t *nwkKey = nwkKeyDefault;

	for (uint8_t i = 0; i < SEC_KEY_LEN; i++) {
		if (nwkKeyDefault[i] != 0U) {
			ss_zdoNwkKeyConfigure((uint8_t *)nwkKeyDefault, 0, 1);
			return;
		}
	}

	uint8_t randomKey[SEC_KEY_LEN];
	for (uint8_t i = 0; i < SEC_KEY_LEN; i += 2U) {
		uint16_t rnd = (uint16_t)drv_u32Rand();
		COPY_U16TOBUFFER(randomKey + i, rnd);
	}

	nwkKey = randomKey;
	ss_zdoNwkKeyConfigure((uint8_t *)nwkKey, 0, 1);
}

void zb_reset(void)
{
	zb_MacReset();
	/* the reset cleared the PIB, the device's own address with it */
	zb_platform_apply_runtime_ieee_addr();
	zb_NwkInit(1);
	aps_init();
#if defined(ZB_ROUTER_ROLE)
	zb_nwkKeySet();
	ss_zdoUseKey(0);
#endif
	ss_ib.ssTimeoutPeriod = 0;
	ss_ib.outgoingFrameCounter = 0;
	zb_bdbAttrInit();
}

void zb_init(void)
{
	if (nv_facrotyNewRstFlagCheck()) {
		nv_resetToFactoryNew();
	}

	/* A successful zb_info_load() is the warm start; the warm path skips only
	 * the key setup and zb_bdbAttrInit() before the common af_init() tail. */
	if (zb_info_load() == NV_SUCC) {
		zb_MacInit(0);
		zb_NwkInit(0);
		aps_init();
	} else {
		zb_MacInit(1);
		zb_NwkInit(1);
		aps_init();
#if defined(ZB_ROUTER_ROLE)
		zb_nwkKeySet();
		ss_zdoUseKey(0);
#endif
		zb_bdbAttrInit();
	}

	af_init();
	zdo_init();
}

void os_reset(uint8_t isRetention)
{
	if (!isRetention) {
		ev_buf_init();
		ev_timer_init();
		zb_sched_init();
		zb_BufferInit();
	}

	secondClockRun();
}

void os_init(uint8_t isRetention)
{
	os_reset(isRetention);
}

bool zb_stackBusy(void)
{
	if (rf_busyFlag != 0U) {
		return TRUE;
	}

	return zb_MacStateBusy() ? TRUE : FALSE;
}

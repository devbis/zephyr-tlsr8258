/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The address in the NLME-LEAVE.confirm of a leave command (nwk_leaveCnfAddr()).
 *
 * A device leaves its network by broadcasting a leave command to itself.
 * zdo_nlme_leave_confirm_cb() ends such a leave (stops the timers, erases the NV, resets the
 * stack) only when the confirmation carries the null address or the device's own. The
 * confirmation of the broadcast used to carry the invalid address (all 0xff): the callback took
 * it for another device, the leave never ended, and a router that was told to leave (a leave
 * request of the coordinator, zb_factoryReset()) stayed in its network, with the network in
 * the NV.
 *
 * Run: west twister -T tests/subsys/zigbee/nwk_leave_cnf -p native_sim/native/64
 * or, with no west workspace (the Zephyr tree may be read-only):
 *   ZEPHYR_TOOLCHAIN_VARIANT=host cmake -GNinja -DBOARD=native_sim/native/64 \
 *       -DUSER_CACHE_DIR=<build>/cache tests/subsys/zigbee/nwk_leave_cnf && ninja &&
 *       ./zephyr/zephyr.exe
 * The mutation check: make nwk_leaveCnfAddr() return the invalid address for a broadcast, two
 * tests fail.
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "nwk_leave.h"
#include "zb_common.h"

const addrExt_t g_invalid_addr = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const addrExt_t g_zero_addr = {0};

#define KNOWN_SHORT 0x1234U
static const addrExt_t known_ieee = {0xaf, 0x5f, 0x93, 0x0c, 0x74, 0x38, 0xc1, 0xa4};

/* The address map: one device */
uint8_t zb_address_ieee_by_short(uint16_t short_addr, addrExt_t ieee_address)
{
	if (short_addr != KNOWN_SHORT) {
		return 0xffU;
	}
	memcpy(ieee_address, known_ieee, sizeof(addrExt_t));
	return 0U;
}

/* What zdo_nlme_leave_confirm_cb() takes for the leave of this very device */
static bool ends_own_leave(const addrExt_t cnf_addr, const addrExt_t own)
{
	return ZB_IEEE_ADDR_IS_ZERO(cnf_addr) || ZB_IEEE_ADDR_CMP(cnf_addr, own);
}

ZTEST_SUITE(nwk_leave_cnf, NULL, NULL, NULL, NULL, NULL);

ZTEST(nwk_leave_cnf, test_own_leave_is_confirmed_with_the_null_address)
{
	static const addrExt_t own = {1, 2, 3, 4, 5, 6, 7, 8};
	addrExt_t addr;

	/* the destination of the leave a device sends to itself */
	memset(addr, 0x55, sizeof(addr));
	nwk_leaveCnfAddr(addr, NWK_BROADCAST_RX_ON_WHEN_IDLE);

	zassert_true(ZB_IEEE_ADDR_IS_ZERO(addr), "the invalid address ends no leave");
	zassert_true(ends_own_leave(addr, own));
}

ZTEST(nwk_leave_cnf, test_every_broadcast_address_is_the_null_address)
{
	static const uint16_t dst[] = {0xfff8, 0xfffb, 0xfffc, 0xfffd, 0xfffe, 0xffff};
	addrExt_t addr;

	for (size_t i = 0; i < ARRAY_SIZE(dst); i++) {
		memset(addr, 0x55, sizeof(addr));
		nwk_leaveCnfAddr(addr, dst[i]);
		zassert_true(ZB_IEEE_ADDR_IS_ZERO(addr), "dst 0x%04x", dst[i]);
	}
}

ZTEST(nwk_leave_cnf, test_unicast_leave_names_the_device)
{
	static const addrExt_t own = {1, 2, 3, 4, 5, 6, 7, 8};
	addrExt_t addr;

	nwk_leaveCnfAddr(addr, KNOWN_SHORT);

	zassert_mem_equal(addr, known_ieee, sizeof(addrExt_t));
	/* the leave of a child does not end the leave of this device */
	zassert_false(ends_own_leave(addr, own));
}

ZTEST(nwk_leave_cnf, test_unicast_leave_of_an_unknown_device_stays_invalid)
{
	static const addrExt_t own = {1, 2, 3, 4, 5, 6, 7, 8};
	addrExt_t addr;

	memset(addr, 0x55, sizeof(addr));
	nwk_leaveCnfAddr(addr, 0x4321U);

	zassert_mem_equal(addr, g_invalid_addr, sizeof(addrExt_t));
	/* a child the address map has forgotten must not take this device out of its network */
	zassert_false(ends_own_leave(addr, own));
}

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Layout and wire-format contracts behind the secure-join fixes.
 *
 * Each stack fix below was derived from a vendor object file, and each one
 * rests on where a field sits in a shared structure or on how a bit travels on
 * air. Those premises are what this test pins down: reading the disassembly
 * again costs a session, so a structure that drifts must fail here instead.
 *
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "zb_common.h"
#include "aps_api.h"
#include "aps_data.h"
#include "security_service.h"

/*
 * nwkNebManagePeriodic() ages an unauthenticated child on authTimeout, the
 * word at entry offset 8 of the 32-bit vendor layout ("60: tadds r2,#188"
 * against a table that starts at g_zb_neighborTbl+180).  nwk_join_accept() has
 * to arm that same word; arming timeoutCnt instead left the counter at zero and
 * the first one-second tick after the association response deleted the joining
 * child.  The offsets are written against the two list pointers so that the
 * same contract holds on a 64-bit host.
 */
ZTEST(join_layout, test_neighbor_timer_fields)
{
	const size_t after_links = 2U * sizeof(void *);

	zassert_equal(OFFSETOF(tl_zb_normal_neighbor_entry_t, authTimeout), after_links);
	zassert_equal(OFFSETOF(tl_zb_normal_neighbor_entry_t, timeoutCnt), after_links + 4U);
	zassert_equal(OFFSETOF(tl_zb_normal_neighbor_entry_t, devTimeout), after_links + 8U);
}

/*
 * aps_command_handle() discards a command this device sent itself: the vendor
 * compares aps_data_ind_t+20 (the source short address) with g_zbInfo+26
 * (g_zbMacPib.shortAddress). Comparing the destination instead dropped every
 * command addressed to this device, the Transport-Key included.
 */
ZTEST(join_layout, test_aps_command_self_check_fields)
{
	/*
	 * asdu is the first pointer in the indication; the source address follows
	 * it and the receive timestamp, with no padding of its own.
	 */
	zassert_equal(OFFSETOF(aps_data_ind_t, src_short_addr),
		      OFFSETOF(aps_data_ind_t, asdu) + sizeof(void *) + sizeof(u32));
	zassert_equal(OFFSETOF(aps_data_ind_t, rx_tick),
		      OFFSETOF(aps_data_ind_t, asdu) + sizeof(void *));
	zassert_equal(OFFSETOF(tl_zb_mac_pib_t, extAddress), 12);
	zassert_equal(OFFSETOF(tl_zb_mac_pib_t, shortAddress), 26);
	zassert_equal(OFFSETOF(zb_info_t, macPib), 0);
}

/*
 * ss_apsmeTransportKeyReq() writes the two security flags at cmdReq+18 and
 * cmdReq+19 ("c4: tstorerb r3,[r5,#18]", "5c: tstorerb r3,[r5,#19]").
 */
ZTEST(join_layout, test_aps_cmd_send_req_layout)
{
	const size_t addr_mode = 2U * sizeof(void *) + EXT_ADDR_LEN;

	zassert_equal(OFFSETOF(aps_cmd_send_req_t, addrMode), addr_mode);
	zassert_equal(OFFSETOF(aps_cmd_send_req_t, aduLen), addr_mode + 1U);
	zassert_equal(OFFSETOF(aps_cmd_send_req_t, secure), addr_mode + 2U);
	zassert_equal(OFFSETOF(aps_cmd_send_req_t, secureNwkLayer), addr_mode + 3U);
}

/*
 * ss_apsSecureFrame() and ss_apsDecryptFrame() gate on ss_ib+80, which is
 * tcLinkKeyType, not preConfiguredKeyType at ss_ib+64.  The two fields hold
 * different enumerations, so reading the wrong one silently inverts the
 * trust-center key policy.
 */
ZTEST(join_layout, test_security_ib_key_policy_fields)
{
	/*
	 * preConfiguredKeyType sits one byte past the key sequence number that
	 * follows the trust-center address; tcLinkKeyType is three fields and two
	 * pointers further on.  On the 32-bit target those are ss_ib+64 and
	 * ss_ib+80, the two offsets the disassembly distinguishes.
	 */
	zassert_equal(OFFSETOF(ss_info_base_t, preConfiguredKeyType),
		      OFFSETOF(ss_info_base_t, trust_center_address) + EXT_ADDR_LEN + 2U);
	zassert_equal(OFFSETOF(ss_info_base_t, tcLinkKeyType),
		      OFFSETOF(ss_info_base_t, preConfiguredKeyType) + 1U + sizeof(ss_tcPolicy_t) +
			      2U * sizeof(void *));
	/*
	 * A zeroed ss_ib means "unique link keys", which is why a trust center
	 * that never ran bdb_linkKeyCfg() refused to encrypt any key.
	 */
	zassert_equal(SS_UNIQUE_LINK_KEY, 0);
	zassert_equal(SS_GLOBAL_LINK_KEY, 1);
}

/*
 * nwk_nlmeJoinCnf() clears the high nibble of g_zbNwkCtx+47 and keeps the low
 * one ("36: tloadrb r1,[r2,r3]; 38: tmovs r0,#15; 3a: tands r1,r0").  The low
 * nibble is user_state: a device stays NLME_JOINING until the trust center
 * authenticates it, and the network layer drops inbound frames when it is not.
 */
ZTEST(join_layout, test_nwk_ctx_state_nibbles)
{
	nwk_ctx_t ctx;
	const unsigned char *raw = (const unsigned char *)&ctx;
	/* The three flag bytes end where leaveRejoin begins. */
	const size_t states = OFFSETOF(nwk_ctx_t, leaveRejoin) - 1U;
	const size_t flags = states - 2U;

	memset(&ctx, 0, sizeof(ctx));
	ctx.user_state = NLME_JOINING;
	ctx.state = NLME_STATE_ROUTER_START;

	zassert_equal(raw[states] & 0x0fU, NLME_JOINING);
	zassert_equal((raw[states] >> 4) & 0x0fU, NLME_STATE_ROUTER_START);

	/* Clearing the NLME state must leave the user state alone. */
	ctx.state = NLME_STATE_IDLE;
	zassert_equal(ctx.user_state, NLME_JOINING);

	/* joined is bit 2 of the first flag byte, as the same routine reads it. */
	memset(&ctx, 0, sizeof(ctx));
	ctx.joined = 1;
	zassert_equal(raw[flags], 0x04U);
}

/*
 * bdb_coordinatorStart() sets aps_ib+12, and the beacon builder turns that into
 * the PAN coordinator bit of the superframe specification.  nwk_discovery.c
 * reads the same bit back as the device type of a potential parent, so a
 * coordinator that leaves it clear is filed as a plain router: the joining
 * router then takes the distributed-security branch for its own children and
 * never asks the trust center to authenticate them.
 */
ZTEST(join_layout, test_designated_coordinator_field)
{
	zassert_equal(OFFSETOF(aps_pib_attributes_t, aps_designated_coordinator),
		      sizeof(u32) + EXT_ADDR_LEN);
}

/*
 * tl_zbMacTx() records the sequence number from the third byte of the frame
 * it queues, so every caller must hand over the frame start; the buffer
 * header that follows the payload area must not move under that frame.
 */
ZTEST(join_layout, test_buffer_header_follows_payload)
{
	zassert_equal(OFFSETOF(zb_buf_t, hdr), ZB_BUF_SIZE);
}

/*
 * ss_apsUpdateDeviceCmdHandle() builds an APSME-UPDATE-DEVICE.indication over
 * the very buffer the command arrived in, and the ASDU it parses is reached
 * through a pointer stored in that buffer.  The indication's own fields land on
 * top of that pointer, so every value has to be taken out of the frame before
 * the first one is written back.  The vendor gets away with reading afterwards
 * only because its compiler happens to keep the pointer in a register.
 */
ZTEST(join_layout, test_update_device_indication_overlay)
{
	const size_t asdu_first = OFFSETOF(aps_data_ind_t, asdu);
	const size_t asdu_last = asdu_first + sizeof(u8 *) - 1U;
	const size_t ind_first = OFFSETOF(ss_apsmeUpdateDeviceInd_t, devAddr);
	const size_t ind_last = OFFSETOF(ss_apsmeUpdateDeviceInd_t, status);

	zassert_equal(ind_first <= asdu_last && ind_last >= asdu_first, 1);
	/* The source address the same handler resolves survives the write. */
	zassert_equal(OFFSETOF(aps_data_ind_t, src_short_addr) > ind_last, 1);
}

ZTEST_SUITE(join_layout, NULL, NULL, NULL, NULL, NULL);

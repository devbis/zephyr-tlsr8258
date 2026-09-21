/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Layout and wire-format contracts behind the secure-join fixes.
 *
 * Each stack fix below was derived from a vendor object file, and each one
 * rests on where a field sits in a shared structure or on how a bit travels on
 * air. Those premises are what this test pins down: reading the disassembly
 * again costs a session, so a structure that drifts must fail here instead.
 *
 * The behavioural half of the same set is checked by run.sh.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "zb_common.h"
#include "aps_api.h"
#include "aps_data.h"
#include "security_service.h"

/* zb_common.h carries a handful of static inline helpers that reach into the
 * radio port; nothing here calls them, but the linker still wants the symbol.
 */
u8 *zb_radio_next_rx_buf_get(void);
u8 *zb_radio_next_rx_buf_get(void)
{
	return NULL;
}

static int failures;

#define EXPECT_EQ(actual, expected)                                                                \
	do {                                                                                       \
		long long _actual = (long long)(actual);                                           \
		long long _expected = (long long)(expected);                                       \
		if (_actual != _expected) {                                                        \
			printf("FAIL %s:%d: %s=%lld expected %lld\n", __FILE__, __LINE__,          \
			       #actual, _actual, _expected);                                       \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

/*
 * nwkNebManagePeriodic() ages an unauthenticated child on authTimeout, the
 * word at entry offset 8 of the 32-bit vendor layout ("60: tadds r2,#188"
 * against a table that starts at g_zb_neighborTbl+180).  nwk_join_accept() has
 * to arm that same word; arming timeoutCnt instead left the counter at zero and
 * the first one-second tick after the association response deleted the joining
 * child.  The offsets are written against the two list pointers so that the
 * same contract holds on a 64-bit host.
 */
static void test_neighbor_timer_fields(void)
{
	const size_t after_links = 2U * sizeof(void *);

	EXPECT_EQ(OFFSETOF(tl_zb_normal_neighbor_entry_t, authTimeout), after_links);
	EXPECT_EQ(OFFSETOF(tl_zb_normal_neighbor_entry_t, timeoutCnt), after_links + 4U);
	EXPECT_EQ(OFFSETOF(tl_zb_normal_neighbor_entry_t, devTimeout), after_links + 8U);
}

/*
 * aps_command_handle() discards a command this device sent itself: the vendor
 * compares aps_data_ind_t+20 (the source short address) with g_zbInfo+26
 * (g_zbMacPib.shortAddress). Comparing the destination instead dropped every
 * command addressed to this device, the Transport-Key included.
 */
static void test_aps_command_self_check_fields(void)
{
	/* asdu is the first pointer in the indication; the source address follows
	 * it and the receive timestamp, with no padding of its own. */
	EXPECT_EQ(OFFSETOF(aps_data_ind_t, src_short_addr),
		  OFFSETOF(aps_data_ind_t, asdu) + sizeof(void *) + sizeof(u32));
	EXPECT_EQ(OFFSETOF(aps_data_ind_t, rx_tick),
		  OFFSETOF(aps_data_ind_t, asdu) + sizeof(void *));
	EXPECT_EQ(OFFSETOF(tl_zb_mac_pib_t, extAddress), 12);
	EXPECT_EQ(OFFSETOF(tl_zb_mac_pib_t, shortAddress), 26);
	EXPECT_EQ(OFFSETOF(zb_info_t, macPib), 0);
}

/*
 * ss_apsmeTransportKeyReq() writes the two security flags at cmdReq+18 and
 * cmdReq+19 ("c4: tstorerb r3,[r5,#18]", "5c: tstorerb r3,[r5,#19]").
 */
static void test_aps_cmd_send_req_layout(void)
{
	const size_t addr_mode = 2U * sizeof(void *) + EXT_ADDR_LEN;

	EXPECT_EQ(OFFSETOF(aps_cmd_send_req_t, addrMode), addr_mode);
	EXPECT_EQ(OFFSETOF(aps_cmd_send_req_t, aduLen), addr_mode + 1U);
	EXPECT_EQ(OFFSETOF(aps_cmd_send_req_t, secure), addr_mode + 2U);
	EXPECT_EQ(OFFSETOF(aps_cmd_send_req_t, secureNwkLayer), addr_mode + 3U);
}

/*
 * ss_apsSecureFrame() and ss_apsDecryptFrame() gate on ss_ib+80, which is
 * tcLinkKeyType, not preConfiguredKeyType at ss_ib+64.  The two fields hold
 * different enumerations, so reading the wrong one silently inverts the
 * trust-center key policy.
 */
static void test_security_ib_key_policy_fields(void)
{
	/* preConfiguredKeyType sits one byte past the key sequence number that
	 * follows the trust-center address; tcLinkKeyType is three fields and two
	 * pointers further on.  On the 32-bit target those are ss_ib+64 and
	 * ss_ib+80, the two offsets the disassembly distinguishes. */
	EXPECT_EQ(OFFSETOF(ss_info_base_t, preConfiguredKeyType),
		  OFFSETOF(ss_info_base_t, trust_center_address) + EXT_ADDR_LEN + 2U);
	EXPECT_EQ(OFFSETOF(ss_info_base_t, tcLinkKeyType),
		  OFFSETOF(ss_info_base_t, preConfiguredKeyType) + 1U +
			  sizeof(ss_tcPolicy_t) + 2U * sizeof(void *));
	/* A zeroed ss_ib means "unique link keys", which is why a trust center
	 * that never ran bdb_linkKeyCfg() refused to encrypt any key. */
	EXPECT_EQ(SS_UNIQUE_LINK_KEY, 0);
	EXPECT_EQ(SS_GLOBAL_LINK_KEY, 1);
}

/*
 * nwk_nlmeJoinCnf() clears the high nibble of g_zbNwkCtx+47 and keeps the low
 * one ("36: tloadrb r1,[r2,r3]; 38: tmovs r0,#15; 3a: tands r1,r0").  The low
 * nibble is user_state: a device stays NLME_JOINING until the trust center
 * authenticates it, and the network layer drops inbound frames when it is not.
 */
static void test_nwk_ctx_state_nibbles(void)
{
	nwk_ctx_t ctx;
	const unsigned char *raw = (const unsigned char *)&ctx;
	/* The three flag bytes end where leaveRejoin begins. */
	const size_t states = OFFSETOF(nwk_ctx_t, leaveRejoin) - 1U;
	const size_t flags = states - 2U;

	memset(&ctx, 0, sizeof(ctx));
	ctx.user_state = NLME_JOINING;
	ctx.state = NLME_STATE_ROUTER_START;

	EXPECT_EQ(raw[states] & 0x0fU, NLME_JOINING);
	EXPECT_EQ((raw[states] >> 4) & 0x0fU, NLME_STATE_ROUTER_START);

	/* Clearing the NLME state must leave the user state alone. */
	ctx.state = NLME_STATE_IDLE;
	EXPECT_EQ(ctx.user_state, NLME_JOINING);

	/* joined is bit 2 of the first flag byte, as the same routine reads it. */
	memset(&ctx, 0, sizeof(ctx));
	ctx.joined = 1;
	EXPECT_EQ(raw[flags], 0x04U);
}

/*
 * bdb_coordinatorStart() sets aps_ib+12, and the beacon builder turns that into
 * the PAN coordinator bit of the superframe specification.  nwk_discovery.c
 * reads the same bit back as the device type of a potential parent, so a
 * coordinator that leaves it clear is filed as a plain router: the joining
 * router then takes the distributed-security branch for its own children and
 * never asks the trust center to authenticate them.
 */
static void test_beacon_pan_coordinator_bit(void)
{
	/* superframe specification as the builder writes it, low byte first */
	unsigned char superframe[2];
	unsigned int spec;

	EXPECT_EQ(OFFSETOF(aps_pib_attributes_t, aps_designated_coordinator),
		  sizeof(u32) + EXT_ADDR_LEN);

	superframe[0] = 0xffU;                /* beacon order | superframe order */
	superframe[1] = (unsigned char)(0x0fU | (1U << 7)); /* CAP slot | assoc permit */
	spec = (unsigned int)superframe[0] | ((unsigned int)superframe[1] << 8);
	EXPECT_EQ(spec & 0x4000U, 0U);

	superframe[1] |= 0x40U;               /* designated coordinator */
	spec = (unsigned int)superframe[0] | ((unsigned int)superframe[1] << 8);
	EXPECT_EQ(spec & 0x4000U, 0x4000U);
}

/*
 * A MAC frame's sequence number is its third byte.  tl_zbMacTx() records it
 * from the frame it queues, so every caller must hand over the frame start and
 * not the pointer the header builder returns.
 */
static void test_mac_sequence_number_position(void)
{
	static const unsigned char assoc_response[] = {
		0x63, 0xcc,             /* frame control */
		0x62,                   /* sequence number */
		0x36, 0xdc,             /* destination PAN */
	};

	EXPECT_EQ(assoc_response[2], 0x62U);
	EXPECT_EQ(OFFSETOF(zb_buf_t, hdr), ZB_BUF_SIZE);
}

/*
 * ss_apsUpdateDeviceCmdHandle() builds an APSME-UPDATE-DEVICE.indication over
 * the very buffer the command arrived in, and the ASDU it parses is reached
 * through a pointer stored in that buffer.  The indication's own fields land on
 * top of that pointer, so every value has to be taken out of the frame before
 * the first one is written back.  The vendor gets away with reading afterwards
 * only because its compiler happens to keep the pointer in a register.
 */
static void test_update_device_indication_overlay(void)
{
	const size_t asdu_first = OFFSETOF(aps_data_ind_t, asdu);
	const size_t asdu_last = asdu_first + sizeof(u8 *) - 1U;
	const size_t ind_first = OFFSETOF(ss_apsmeUpdateDeviceInd_t, devAddr);
	const size_t ind_last = OFFSETOF(ss_apsmeUpdateDeviceInd_t, status);

	EXPECT_EQ(ind_first <= asdu_last && ind_last >= asdu_first, 1);
	/* The source address the same handler resolves survives the write. */
	EXPECT_EQ(OFFSETOF(aps_data_ind_t, src_short_addr) > ind_last, 1);
}

int main(void)
{
	test_neighbor_timer_fields();
	test_aps_command_self_check_fields();
	test_aps_cmd_send_req_layout();
	test_security_ib_key_policy_fields();
	test_nwk_ctx_state_nibbles();
	test_beacon_pan_coordinator_bit();
	test_mac_sequence_number_position();
	test_update_device_indication_overlay();

	if (failures != 0) {
		printf("zigbee host_join_regressions: %d failure(s)\n", failures);
		return 1;
	}

	printf("zigbee host_join_regressions: all checks passed\n");
	return 0;
}

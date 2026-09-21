#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Regression net for the secure-join path (coordinator -> router -> end
# device).  Reproducing any of these on the running stack needs three
# native_sim nodes and a medium daemon; each check here costs a second.
#
# The compiled part pins the structure layouts and wire bits the fixes rest
# on.  The source checks below pin the behaviour itself: every one of them
# guards a line that a fresh vendor import, or a well-meaning cleanup, would
# otherwise take back out.

set -eu

cd "$(dirname "$0")/../../../.."

here=tests/subsys/zigbee/host_join_regressions
out="${TMPDIR:-/tmp}/zephyr-zigbee-host-join-regressions"
cc="${CC:-cc}"
fail=0

report() {
	echo "FAIL: $1" >&2
	fail=1
}

"$cc" -std=c17 -Wall -Wextra -Werror -Wno-unknown-pragmas \
	-I"$here/include" \
	-Isubsys/zigbee \
	-Isubsys/zigbee/include \
	-Isubsys/zigbee/common \
	-Isubsys/zigbee/common/includes \
	-Isubsys/zigbee/os \
	-Isubsys/zigbee/mac/includes \
	-Isubsys/zigbee/nwk/includes \
	-Isubsys/zigbee/aps \
	-Isubsys/zigbee/ss \
	-Isubsys/zigbee/platform/zephyr \
	"$here/main.c" \
	-o "$out"

"$out" || fail=1

# An unauthenticated child is aged on authTimeout, so that is the field the
# join has to arm.  Arming timeoutCnt deleted the child one second after the
# association response and the trust center never saw a join indication.
if ! rg -q 'neighbor\.authTimeout = NWK_UNAUTH_CHILD_TABLE_LIFE_TIME' \
	subsys/zigbee/nwk/nwk_join.c; then
	report "nwk_join_accept() must arm authTimeout for an unauthenticated child"
fi

# The join stays NLME_JOINING until the trust center authenticates it.
if rg -q 'g_zbNwkCtx\.user_state = NLME_IDLE' subsys/zigbee/nwk/nwk_join.c; then
	report "nwk_nlmeJoinCnf() must clear the NLME state, not the user state"
fi

# A command whose source is this device is discarded; one addressed to it is not.
if ! rg -q 'src = ind->src_short_addr' subsys/zigbee/aps/aps.c; then
	report "aps_command_handle() must discard on the source address"
fi

# APS security for a Transport-Key follows the destination, not the
# preconfigured key type, which the vendor never reads on this path.
if rg -q 'preConfiguredKeyType == SS_PRECONFIGURED_GLOBALLINKKEY' \
	subsys/zigbee/ss/ss_apsSecurityME.c; then
	report "ss_apsmeTransportKeyReq() must not gate APS security on preConfiguredKeyType"
fi
if ! rg -q 'req->nwkSecurity == 0U && !req->relayByParent' \
	subsys/zigbee/ss/ss_apsSecurityME.c; then
	report "ss_apsmeTransportKeyReq() must clear network security for a direct key"
fi

# A key that is not for this device goes to the child lookup unconditionally.
if rg -q '!aps_ib\.aps_authenticated && \(ind->security_status & SECURITY_IN_APSLAYER\)' \
	subsys/zigbee/ss/ss_apsSecurityME.c; then
	report "ss_apsTransportKeyCmdHandle() must relay to a child regardless of its own state"
fi

# The trust-center encryption gate reads tcLinkKeyType and demands a unique
# pair only when the trust center is configured for unique link keys.
if rg -q 'ss_ib\.preConfiguredKeyType == SS_PRECONFIGURED_NOKEY' \
	subsys/zigbee/ss/ss_apsEnDecrypt.c; then
	report "ss_apsSecureFrame() must gate on tcLinkKeyType, not preConfiguredKeyType"
fi

# The retained join confirmation is the buffer that gets released, not the
# indication the network key arrived in.
if rg -q 'savedBuf != NULL \{?[^}]*zb_buf_free\(\(zb_buf_t \*\)arg\)' \
	--multiline subsys/zigbee/ss/ss_zdoSecurityME.c; then
	report "ss_zdoTransportKeyIndHandle() must free savedBuf, not the indication"
fi

# The network layer hands APS the plaintext length.
if ! rg -q 'ind->msduLength = payloadTotalLen' subsys/zigbee/nwk/nwk_data.c; then
	report "tl_zbMacMcpsDataIndicationHandler() must store the decrypted length"
fi

# Address 0 is the coordinator, not a missing route.  The source-routed branch
# above keeps the vendor's own next-hop test; only the neighbour/route decision
# had to stop reading address 0 as "nothing found".
if ! rg -q 'if \(\(neighbor == NULL && route == NULL\) \|\| nextHop == MAC_ADDR_USE_EXT\)' \
	subsys/zigbee/nwk/nwk_data.c; then
	report "nwk_fwdPacket() must not treat short address 0 as an unresolved next hop"
fi

# A unicast to a neighbour that sleeps is held for its next data request.
if ! rg -q 'neighbor->rxOnWhileIdle == 0U\) \? 1U : 0U' subsys/zigbee/nwk/nwk_data.c; then
	report "nwk_fwdPacket() must ask for an indirect transmission to a sleeping neighbour"
fi
if rg -q 'u8 ack, u8 \*payload' subsys/zigbee/nwk/nwk_data.c; then
	report "nwk_tx()'s fourth argument selects indirect transmission, not acknowledgement"
fi

# The MAC keeps the whole frame, header first.
if rg -q 'tl_bufInitalloc\(txBuf, psduLen\)' subsys/zigbee/mac/mac_trx.c; then
	report "tl_zbMacTx() must queue the caller's frame, not a recomputed allocation"
fi
if ! rg -q 'entry->seqNum = txData\[2\]' subsys/zigbee/mac/mac_trx.c; then
	report "tl_zbMacTx() must take the sequence number from the queued frame"
fi
for f in mac_scan.c mac_associate.c mac_indirect_data.c; do
	if ! rg -q 'frameStart' "subsys/zigbee/mac/$f"; then
		report "subsys/zigbee/mac/$f must hand tl_zbMacTx() the frame start"
	fi
done

# Only the beacon path shortens the recorded payload length; the data and
# command paths subtract the header themselves.
if rg -q 'phy_ind_payload_len_set\(arg, \(u8\)\(phy_ind_payload_len_get\(arg\) - hdrLen\)\)' \
	subsys/zigbee/mac/mac.c; then
	report "phy_ind_payload_advance() must not shorten the recorded payload length"
fi

# A designated coordinator marks the beacon and still carries its payload.
if ! rg -q 'aps_ib\.aps_designated_coordinator' subsys/zigbee/platform/zephyr/zb_bdb_bootstrap.c; then
	report "the coordinator bootstrap must set aps_designated_coordinator"
fi
if rg -qU --pcre2 'aps_designated_coordinator\) \{\n\t\tpayload\[1\] \|= 0x40U;\n\t\} else' \
	subsys/zigbee/mac/mac_cr_coordinator.c; then
	report "the beacon payload must be copied for a designated coordinator too"
fi

# A pooled buffer reads as in use; the network layer drops frames whose buffer
# does not.
if ! rg -q 'hdr\.used = 1' subsys/zigbee/platform/zephyr/zb_buffer_zephyr.c; then
	report "zb_buf_allocate() must mark the buffer in use"
fi

# The receive length already counts the frame check sequence.
if rg -q 'mac_len = \(uint8_t\)\(mac_len \+ 2U\)' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	report "the native socket receive path must not add the FCS length twice"
fi

# The acknowledgement the MAC waits for carries the sequence number that went
# on air.
if ! rg -q 'g_radio\.last_tx_seq = \(frame != NULL\) \? frame\[2\]' \
	subsys/zigbee/platform/zephyr/drv_radio_zephyr.c; then
	report "the synthesized acknowledgement must use the transmitted sequence number"
fi

# A relayed frame points into the radio's receive ring, not into the buffer
# the APS transmit path copies.
if ! rg -q 'nsduOffset >= 0 && \(size_t\)nsduOffset < sizeof\(zb_buf_t\)' \
	subsys/zigbee/aps/aps_data.c; then
	report "apsTxDataSendStart() must carry over a payload outside the source buffer"
fi

if [ "$fail" -ne 0 ]; then
	exit 1
fi

echo "zigbee host_join_regressions: source checks passed"

#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-sim"
cc="${CC:-cc}"

if rg -n '#include <zephyr/drivers/ieee802154/tlsr8258_zigbee_bridge\.h>' \
	subsys/zigbee \
	-g '*.c' -g '*.h' \
	-g '!subsys/zigbee/platform/zephyr/zb_radio_port_tlsr8258.c'; then
	echo "Generic Zigbee subsystem files must use zb_radio_port.h, not tlsr8258_zigbee_bridge.h" >&2
	exit 1
fi

if rg -n 'S_TIMER_CLOCK_1US[[:space:]]+24|sysTimerPerUs[[:space:]]*=[[:space:]]*24' \
	subsys/zigbee/platform/zephyr subsys/zigbee/mac; then
	echo "Zigbee timer conversion must use Zephyr clock conversion, not a fixed 24 MHz assumption" >&2
	exit 1
fi

if ! rg -q 'nwk_ed_minimal_rx_evt_drop_record' subsys/zigbee/nwk/nwk_ed_minimal.c; then
	echo "nwk_ed_minimal RX event queue drops must be counted and logged" >&2
	exit 1
fi

leave_success_line="$(rg -n 'rsp\[1\][[:space:]]*=[[:space:]]*ZDO_SUCCESS' \
	subsys/zigbee/mac/mac_trx_compat.c | head -n 1 | cut -d: -f1 || true)"
leave_clear_line="$(rg -n 'zb_platform_clear_persistent_state' \
	subsys/zigbee/mac/mac_trx_compat.c | head -n 1 | cut -d: -f1 || true)"
if [ -n "$leave_success_line" ] && [ -n "$leave_clear_line" ] &&
	[ "$leave_success_line" -lt "$leave_clear_line" ]; then
	echo "MGMT_LEAVE_RSP success must be sent only after local leave/reset is applied" >&2
	exit 1
fi

"$cc" -std=c17 -Wall -Wextra -Werror \
	tests/subsys/zigbee/host_sim/main.c \
	-o "$out"

"$out"

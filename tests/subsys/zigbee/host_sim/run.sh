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

"$cc" -std=c17 -Wall -Wextra -Werror \
	tests/subsys/zigbee/host_sim/main.c \
	-o "$out"

"$out"

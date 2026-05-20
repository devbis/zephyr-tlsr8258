#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-host-radio-map"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/platform/zephyr \
	tests/subsys/zigbee/host_radio_map/main.c \
	subsys/zigbee/platform/zephyr/drv_radio_map.c \
	-o "$out"

"$out"

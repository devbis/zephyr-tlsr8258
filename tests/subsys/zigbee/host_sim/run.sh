#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-sim"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	tests/subsys/zigbee/host_sim/main.c \
	-o "$out"

"$out"

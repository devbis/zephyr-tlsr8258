#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-socket-medium"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/include \
	tests/subsys/zigbee/host_socket_medium/main.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium.c \
	-o "$out"

"$out"

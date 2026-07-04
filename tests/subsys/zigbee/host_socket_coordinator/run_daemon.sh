#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-socket-coordinator-daemon"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/include \
	-Itests/subsys/zigbee/host_socket_coordinator \
	tests/subsys/zigbee/host_socket_coordinator/daemon_main.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_logic.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_ccm.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium_model.c \
	-o "$out"

exec "$out" "$@"

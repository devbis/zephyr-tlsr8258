#!/bin/sh
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

# Build and start the host Zigbee coordinator: a localhost UDP medium for the
# native_sim socket radio that also answers a single joiner as coordinator and
# trust center. With --relay-only it only relays frames between native_sim
# peers, for a network whose coordinator is a native_sim build as well.
#
#   run_daemon.sh --bind-port 19011 [--relay-only]

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-socket-coordinator-daemon"
cc="${CC:-cc}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/include -Itests/subsys/zigbee/common \
	-Itests/subsys/zigbee/host_socket_coordinator \
	tests/subsys/zigbee/host_socket_coordinator/daemon_main.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_logic.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_ccm.c \
	tests/subsys/zigbee/common/zb_native_sim_socket_medium.c \
	tests/subsys/zigbee/common/zb_native_sim_socket_medium_model.c \
	-o "$out"

exec "$out" "$@"

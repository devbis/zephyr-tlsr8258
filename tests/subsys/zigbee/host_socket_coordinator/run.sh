#!/bin/sh
# SPDX-License-Identifier: Apache-2.0

set -eu

cd "$(dirname "$0")/../../../.."

out="${TMPDIR:-/tmp}/zephyr-zigbee-host-socket-coordinator"
daemon_out="${TMPDIR:-/tmp}/zephyr-zigbee-host-socket-coordinator-daemon"
cc="${CC:-cc}"
port="${PORT:-19023}"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/include \
	-Itests/subsys/zigbee/host_socket_coordinator \
	tests/subsys/zigbee/host_socket_coordinator/main.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_logic.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium_model.c \
	-o "$out"

"$out"

"$cc" -std=c17 -Wall -Wextra -Werror \
	-Isubsys/zigbee/include \
	-Itests/subsys/zigbee/host_socket_coordinator \
	tests/subsys/zigbee/host_socket_coordinator/daemon_main.c \
	tests/subsys/zigbee/host_socket_coordinator/coord_logic.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium.c \
	subsys/zigbee/platform/common/zb_native_sim_socket_medium_model.c \
	-o "$daemon_out"

"$daemon_out" --bind-port "$port" >/tmp/zephyr-zigbee-host-socket-coordinator-daemon.log 2>&1 &
daemon_pid=$!

cleanup() {
	kill "$daemon_pid" 2>/dev/null || true
	wait "$daemon_pid" 2>/dev/null || true
}

trap cleanup EXIT INT TERM
sleep 1

python3 -c '
import socket
import struct
import sys

magic = 0x4D535A42
version = 1
msg_status = 5
req = struct.pack(
    "<IBBBBHHHbbBBH8s",
    magic, version, msg_status, 0, 11,
    0x2202, 0xFFFF, 0xFFFF,
    0, 0, 0, 0, 1, b"\x00" * 8,
) + b"\x01"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(1.0)
sock.sendto(req, ("127.0.0.1", int(sys.argv[1])))
data, _ = sock.recvfrom(256)

if len(data) != 30:
    raise SystemExit(f"unexpected STATUS reply length: {len(data)}")

fields = struct.unpack("<IBBBBHHHbbBBH8s2B", data)
_, ver, msg_type, flags, channel, node_id, pan_id, short_addr, tx_dbm, rssi_dbm, lqi, reserved, payload_len, ieee, op, busy = fields

if ver != 1 or msg_type != msg_status or flags != 0 or channel != 11 or node_id != 0x2202:
    raise SystemExit(f"unexpected STATUS header: {fields!r}")
if payload_len != 2 or op != 0x02 or busy not in (0, 1):
    raise SystemExit(f"unexpected STATUS payload: {fields!r}")
if rssi_dbm not in (-96, -60):
    raise SystemExit(f"unexpected STATUS rssi: {rssi_dbm}")
' "$port"

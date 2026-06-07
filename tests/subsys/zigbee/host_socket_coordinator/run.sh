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

python3 -c '
import socket
import struct
import sys

MAGIC = 0x4D535A42
VER = 1
MSG_HELLO = 1
MSG_FILTER = 2
MSG_TX = 3
MSG_RX = 4
PORT = int(sys.argv[1])
PAN = 0x5B27

def encode(msg_type, node_id, short_addr, ieee_tail, psdu=b"", channel=11, rx_on=True, tx_dbm=0):
    ieee = b"\x00\x00\x00\x00\x00\x00" + bytes([ieee_tail, 0xA4])
    return struct.pack(
        "<IBBBBHHHbbBBH8s",
        MAGIC, VER, msg_type, 1 if rx_on else 0, channel,
        node_id, PAN, short_addr,
        tx_dbm, 0, 0, 0, len(psdu), ieee
    ) + psdu

def decode(pkt):
    header = struct.unpack("<IBBBBHHHbbBBH8s", pkt[:28])
    return {
        "msg_type": header[2],
        "channel": header[4],
        "node_id": header[5],
        "short_addr": header[7],
        "rssi_dbm": header[9],
        "psdu_len": header[12],
        "psdu": pkt[28:28 + header[12]],
    }

def make_broadcast_psdu(seq, filler_len):
    base = bytes([0x63, 0x88, seq, 0x27, 0x5B, 0xFF, 0xFF, 0x34, 0x12, 0x99])
    return base + bytes([0xEE]) * filler_len

sock_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_c = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for s in (sock_a, sock_b, sock_c):
    s.settimeout(0.5)

sock_a.sendto(encode(MSG_HELLO, 0x2202, 0x1001, 0x02), ("127.0.0.1", PORT))
sock_a.sendto(encode(MSG_FILTER, 0x2202, 0x1001, 0x02), ("127.0.0.1", PORT))
sock_b.sendto(encode(MSG_HELLO, 0x2203, 0x1002, 0x03), ("127.0.0.1", PORT))
sock_b.sendto(encode(MSG_FILTER, 0x2203, 0x1002, 0x03), ("127.0.0.1", PORT))
sock_c.sendto(encode(MSG_HELLO, 0x2204, 0x1003, 0x04), ("127.0.0.1", PORT))
sock_c.sendto(encode(MSG_FILTER, 0x2204, 0x1003, 0x04), ("127.0.0.1", PORT))

probe_psdu = make_broadcast_psdu(0x41, 8)
sock_a.sendto(encode(MSG_TX, 0x2202, 0x1001, 0x02, probe_psdu, rx_on=False, tx_dbm=8), ("127.0.0.1", PORT))
fanout = decode(sock_b.recvfrom(256)[0])
if fanout["msg_type"] != MSG_RX or fanout["node_id"] != 0x2203 or fanout["psdu"] != probe_psdu:
    raise SystemExit(f"unexpected peer fanout: {fanout!r}")
if fanout["rssi_dbm"] != -32:
    raise SystemExit(f"unexpected peer RSSI: {fanout!r}")

collision_a = make_broadcast_psdu(0x51, 90)
collision_c = make_broadcast_psdu(0x52, 90)
sock_a.sendto(encode(MSG_TX, 0x2202, 0x1001, 0x02, collision_a, rx_on=False), ("127.0.0.1", PORT))
sock_c.sendto(encode(MSG_TX, 0x2204, 0x1003, 0x04, collision_c, rx_on=False), ("127.0.0.1", PORT))
sock_b.settimeout(0.2)
try:
    pkt = sock_b.recvfrom(256)[0]
    raise SystemExit(f"unexpected RX after collision: {decode(pkt)!r}")
except TimeoutError:
    pass
' "$port"

python3 -c '
import socket
import struct
import sys

MAGIC = 0x4D535A42
VER = 1
MSG_HELLO = 1
MSG_FILTER = 2
MSG_TX = 3
MSG_STATUS = 5
PORT = int(sys.argv[1])
PAN = 0x5B27
STATUS_TX_RESULT_RSP = 0x03
TX_RESULT_OK = 0
TX_RESULT_COLLISION = 1

def encode(msg_type, node_id, short_addr, ieee_tail, psdu=b"", channel=11, rx_on=True):
    ieee = b"\x00\x00\x00\x00\x00\x00" + bytes([ieee_tail, 0xA4])
    return struct.pack(
        "<IBBBBHHHbbBBH8s",
        MAGIC, VER, msg_type, 1 if rx_on else 0, channel,
        node_id, PAN, short_addr,
        0, 0, 0, 0, len(psdu), ieee
    ) + psdu

def decode(pkt):
    header = struct.unpack("<IBBBBHHHbbBBH8s", pkt[:28])
    return {
        "msg_type": header[2],
        "channel": header[4],
        "node_id": header[5],
        "psdu": pkt[28:28 + header[12]],
    }

def make_broadcast_psdu(seq, filler_len):
    base = bytes([0x63, 0x88, seq, 0x27, 0x5B, 0xFF, 0xFF, 0x34, 0x12, 0x99])
    return base + bytes([0xEE]) * filler_len

def expect_tx_status(sock, node_id, expected):
    pkt = decode(sock.recvfrom(256)[0])
    if pkt["msg_type"] != MSG_STATUS or pkt["node_id"] != node_id:
        raise SystemExit(f"unexpected TX status header: {pkt!r}")
    if len(pkt["psdu"]) != 2 or pkt["psdu"][0] != STATUS_TX_RESULT_RSP or pkt["psdu"][1] != expected:
        raise SystemExit(f"unexpected TX status payload: {pkt!r}")

sock_ok = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_ok.settimeout(1.0)
sock_ok.sendto(encode(MSG_HELLO, 0x2210, 0x1010, 0x10), ("127.0.0.1", PORT))
sock_ok.sendto(encode(MSG_FILTER, 0x2210, 0x1010, 0x10), ("127.0.0.1", PORT))
sock_ok.sendto(encode(MSG_TX, 0x2210, 0x1010, 0x10, make_broadcast_psdu(0x61, 4), rx_on=False),
               ("127.0.0.1", PORT))
expect_tx_status(sock_ok, 0x2210, TX_RESULT_OK)

sock_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for s in (sock_a, sock_b):
    s.settimeout(1.0)

sock_a.sendto(encode(MSG_HELLO, 0x2211, 0x1011, 0x11), ("127.0.0.1", PORT))
sock_a.sendto(encode(MSG_FILTER, 0x2211, 0x1011, 0x11), ("127.0.0.1", PORT))
sock_b.sendto(encode(MSG_HELLO, 0x2212, 0x1012, 0x12), ("127.0.0.1", PORT))
sock_b.sendto(encode(MSG_FILTER, 0x2212, 0x1012, 0x12), ("127.0.0.1", PORT))
sock_a.sendto(encode(MSG_TX, 0x2211, 0x1011, 0x11, make_broadcast_psdu(0x71, 90), rx_on=False),
               ("127.0.0.1", PORT))
sock_b.sendto(encode(MSG_TX, 0x2212, 0x1012, 0x12, make_broadcast_psdu(0x72, 90), rx_on=False),
               ("127.0.0.1", PORT))
expect_tx_status(sock_a, 0x2211, TX_RESULT_COLLISION)
expect_tx_status(sock_b, 0x2212, TX_RESULT_COLLISION)
' "$port"

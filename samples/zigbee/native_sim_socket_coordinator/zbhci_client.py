#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Minimal ZBHCI (Zigbee Host Command Interface) client for the
native_sim_socket_coordinator sample's UART transport
(subsys/zigbee/platform/zephyr/zb_zbhci_uart.c / zb_zbhci_cmd.c).

Framing: 0x55 <type:u16 BE> <len:u16 BE> <crc8> <payload> 0xAA
No third-party dependencies: uses tty/termios directly instead of pyserial,
since only raw byte I/O over a PTY path is needed.

Usage:
    ./zbhci_client.py /dev/ttysNNN permit-join --duration 60
    ./zbhci_client.py /dev/ttysNNN formation
    ./zbhci_client.py /dev/ttysNNN get-child-nodes
    ./zbhci_client.py /dev/ttysNNN active-ep --target 0x0000
    ./zbhci_client.py /dev/ttysNNN simple-desc --target 0x0000 --endpoint 1
    ./zbhci_client.py /dev/ttysNNN af-data-send --dst 0x91f2 --src-ep 1 \\
        --dst-ep 1 --cluster 0x0000 --payload 00
"""
import argparse
import os
import struct
import sys
import termios
import tty

START_FLAG = 0x55
END_FLAG = 0xAA

CMD_BDB_COMMISSION_FORMATION = 0x0001
CMD_ACKNOWLEDGE = 0x8000
CMD_DISCOVERY_ACTIVE_EP_REQ = 0x0015
CMD_DISCOVERY_ACTIVE_EP_RSP = 0x8015
CMD_DISCOVERY_SIMPLE_DESC_REQ = 0x0013
CMD_DISCOVERY_SIMPLE_DESC_RSP = 0x8013
CMD_MGMT_PERMIT_JOIN_REQ = 0x0034
CMD_GET_CHILD_NODES_REQ = 0x0046
CMD_GET_CHILD_NODES_RSP = 0x8046
CMD_AF_DATA_SEND_TEST_REQ = 0x0044

RSP_NAMES = {
    CMD_ACKNOWLEDGE: "ACKNOWLEDGE",
    CMD_DISCOVERY_ACTIVE_EP_RSP: "ACTIVE_EP_RSP",
    CMD_DISCOVERY_SIMPLE_DESC_RSP: "SIMPLE_DESC_RSP",
    CMD_GET_CHILD_NODES_RSP: "GET_CHILD_NODES_RSP",
}


def crc8(msg_type, payload):
    crc = msg_type & 0xFF
    crc ^= (msg_type >> 8) & 0xFF
    crc ^= len(payload) & 0xFF
    crc ^= (len(payload) >> 8) & 0xFF
    for b in payload:
        crc ^= b
    return crc & 0xFF


def frame(msg_type, payload=b""):
    return bytes([START_FLAG]) + struct.pack(">HH", msg_type, len(payload)) + \
        bytes([crc8(msg_type, payload)]) + payload + bytes([END_FLAG])


def open_raw(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def send(fd, msg_type, payload=b""):
    os.write(fd, frame(msg_type, payload))


def read_frame(fd, timeout_s=2.0):
    """Blocking read of one ZBHCI frame (best-effort: no interleaving)."""
    import select

    state = "wait_start"
    hdr = b""
    msg_type = 0
    msg_len = 0
    payload = b""

    while True:
        r, _, _ = select.select([fd], [], [], timeout_s)
        if not r:
            return None
        chunk = os.read(fd, 256)
        if not chunk:
            return None
        for byte in chunk:
            if state == "wait_start":
                if byte == START_FLAG:
                    hdr = b""
                    state = "hdr"
            elif state == "hdr":
                hdr += bytes([byte])
                if len(hdr) == 5:
                    msg_type, msg_len = struct.unpack(">HH", hdr[0:4])
                    payload = b""
                    state = "wait_end" if msg_len == 0 else "payload"
            elif state == "payload":
                payload += bytes([byte])
                if len(payload) == msg_len:
                    state = "wait_end"
            elif state == "wait_end":
                if byte == END_FLAG:
                    return msg_type, payload
                state = "wait_start"


def describe(msg_type, payload):
    name = RSP_NAMES.get(msg_type, f"0x{msg_type:04x}")
    print(f"<- {name} len={len(payload)} payload={payload.hex()}")
    if msg_type == CMD_ACKNOWLEDGE and len(payload) >= 3:
        orig = struct.unpack(">H", payload[0:2])[0]
        print(f"   orig_cmd=0x{orig:04x} status={payload[2]}")


def cmd_formation(fd, args):
    send(fd, CMD_BDB_COMMISSION_FORMATION)
    describe(*read_frame(fd))


def cmd_permit_join(fd, args):
    payload = struct.pack(">HBB", args.target, args.duration, args.tc_significance)
    send(fd, CMD_MGMT_PERMIT_JOIN_REQ, payload)
    describe(*read_frame(fd))


def cmd_get_child_nodes(fd, args):
    send(fd, CMD_GET_CHILD_NODES_REQ, bytes([args.start_index]))
    result = read_frame(fd)
    if result is None:
        print("(no response)")
        return
    msg_type, payload = result
    describe(msg_type, payload)
    if msg_type == CMD_GET_CHILD_NODES_RSP and len(payload) >= 4:
        status, total, start, count = payload[0:4]
        print(f"   status={status} total={total} start={start} count={count}")
        off = 4
        for _ in range(count):
            ext = payload[off:off + 8]
            nwk = struct.unpack(">H", payload[off + 8:off + 10])[0]
            print(f"   child ext={ext.hex()} nwk=0x{nwk:04x}")
            off += 10


def cmd_active_ep(fd, args):
    payload = struct.pack(">HH", args.target, args.target)
    send(fd, CMD_DISCOVERY_ACTIVE_EP_REQ, payload)
    describe(*read_frame(fd))


def cmd_simple_desc(fd, args):
    payload = struct.pack(">HHB", args.target, args.target, args.endpoint)
    send(fd, CMD_DISCOVERY_SIMPLE_DESC_REQ, payload)
    describe(*read_frame(fd))


def cmd_af_data_send(fd, args):
    data = bytes.fromhex(args.payload)
    payload = struct.pack(">HBBHH", args.dst, args.src_ep, args.dst_ep,
                           args.cluster, len(data)) + data
    send(fd, CMD_AF_DATA_SEND_TEST_REQ, payload)
    describe(*read_frame(fd))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="PTY path printed by zephyr.exe at startup, e.g. /dev/ttys010")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("formation", help="Start BDB network formation")

    p = sub.add_parser("permit-join", help="Mgmt_Permit_Joining_req")
    p.add_argument("--target", type=lambda x: int(x, 0), default=0xFFFC)
    p.add_argument("--duration", type=int, default=60)
    p.add_argument("--tc-significance", type=int, default=1)

    p = sub.add_parser("get-child-nodes", help="Query the child/neighbor table")
    p.add_argument("--start-index", type=int, default=0)

    p = sub.add_parser("active-ep", help="ZDO Active_EP_req")
    p.add_argument("--target", type=lambda x: int(x, 0), required=True)

    p = sub.add_parser("simple-desc", help="ZDO Simple_Desc_req")
    p.add_argument("--target", type=lambda x: int(x, 0), required=True)
    p.add_argument("--endpoint", type=int, required=True)

    p = sub.add_parser("af-data-send", help="AF data send test")
    p.add_argument("--dst", type=lambda x: int(x, 0), required=True)
    p.add_argument("--src-ep", type=int, required=True)
    p.add_argument("--dst-ep", type=int, required=True)
    p.add_argument("--cluster", type=lambda x: int(x, 0), required=True)
    p.add_argument("--payload", default="", help="hex-encoded payload bytes")

    args = parser.parse_args()
    fd = open_raw(args.port)
    try:
        {
            "formation": cmd_formation,
            "permit-join": cmd_permit_join,
            "get-child-nodes": cmd_get_child_nodes,
            "active-ep": cmd_active_ep,
            "simple-desc": cmd_simple_desc,
            "af-data-send": cmd_af_data_send,
        }[args.command](fd, args)
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Request the nice!nano UF2 bootloader over its ZNP port."""

import argparse
import time

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="ZNP serial port")
    args = parser.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.1) as port:
        port.dtr = True
        port.reset_input_buffer()
        port.write(bytes.fromhex("fe 04 21 f0 55 46 32 21 d5"))
        deadline = time.monotonic() + 3
        data = bytearray()
        while time.monotonic() < deadline:
            data.extend(port.read(64))
            if bytes.fromhex("fe 01 61 f0 00 90") in data:
                print("UF2 bootloader requested")
                return
        raise SystemExit("No successful ZNP response")


if __name__ == "__main__":
    main()

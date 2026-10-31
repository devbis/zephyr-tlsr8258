#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Wrap a signed MCUboot image into a Zigbee OTA file (.ota).

The output carries one "upgrade image" sub-element and no optional header
fields. image_type, manufacturer and file_version must match the
ota_preamble_t of the running firmware (see samples/zigbee/zigbee_shell).
"""

import argparse
import struct
import sys

OTA_MAGIC = 0x0BEEF11E
OTA_HEADER_VERSION = 0x0100
OTA_HEADER_LEN = 56
OTA_ZIGBEE_STACK_PRO = 0x0002
OTA_TAG_UPGRADE_IMAGE = 0x0000
MCUBOOT_MAGIC = 0x96F3B83D


def pack(image, manufacturer, image_type, file_version, header_string):
    if len(image) < 4 or struct.unpack_from("<I", image)[0] != MCUBOOT_MAGIC:
        raise ValueError("input is not an MCUboot image (bad magic)")

    text = header_string.encode("utf-8")
    if len(text) > 32:
        raise ValueError("header string is longer than 32 bytes")

    total = OTA_HEADER_LEN + 6 + len(image)
    header = struct.pack(
        "<IHHHHHIH32sI",
        OTA_MAGIC,
        OTA_HEADER_VERSION,
        OTA_HEADER_LEN,
        0,
        manufacturer,
        image_type,
        file_version,
        OTA_ZIGBEE_STACK_PRO,
        text.ljust(32, b"\0"),
        total,
    )
    assert len(header) == OTA_HEADER_LEN
    return header + struct.pack("<HI", OTA_TAG_UPGRADE_IMAGE, len(image)) + image


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", help="signed MCUboot image (zephyr.signed.bin)")
    parser.add_argument("output", help="output .ota file")
    parser.add_argument("--manufacturer", type=lambda x: int(x, 0), default=0x1141)
    parser.add_argument("--image-type", type=lambda x: int(x, 0), default=0x0001)
    parser.add_argument("--file-version", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--header-string", default="Zephyr Zigbee OTA")
    args = parser.parse_args()

    with open(args.image, "rb") as f:
        image = f.read()

    try:
        ota = pack(image, args.manufacturer, args.image_type, args.file_version,
                   args.header_string)
    except ValueError as err:
        sys.exit(f"error: {err}")

    with open(args.output, "wb") as f:
        f.write(ota)
    print(f"{args.output}: {len(ota)} bytes")


if __name__ == "__main__":
    main()

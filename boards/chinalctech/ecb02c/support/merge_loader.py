#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Join the MCUboot image of the ECB02C and a signed application into one image.

The image is for the first flashing through the boot ROM and holds everything from the flash offset
0x2000, the table of the boot ROM in front of the loader, to the end of the application:

    rdwr_phy62x2.py -p /dev/ttyUSB0 -r we 0x11002000 <image>

The application goes to its slot at 0x18000, the gap between the loader and the slot is 0xff. Later
applications are uploaded to the serial recovery of MCUboot alone, as the signed image.

usage: merge_loader.py [--slot0-offset N --slot0-size N] <loader.phy62x2.bin> <application.signed.bin> <image>

The PB-03F has slot 0 at 0x18000 of 0x32000 bytes: --slot0-size 0x32000.
"""

import argparse
from pathlib import Path

# The flash offset that the image starts at: the table of the boot ROM
IMAGE_OFFSET = 0x2000
# slot0_partition of ecb02c.dts
SLOT0_OFFSET = 0x18000
SLOT0_SIZE = 0x25000
# IMAGE_MAGIC of an MCUboot image, little endian
MCUBOOT_MAGIC = bytes.fromhex("3db8f396")
# The initial stack pointer in SRAM at the start of the XIP image of the loader, after the table
STACK_OFFSET = 0x100
SRAM_START = 0x1FFF0000


def merge(loader: bytes, app: bytes, slot0_offset: int = SLOT0_OFFSET,
          slot0_size: int = SLOT0_SIZE) -> bytes:
    """Return the image: the loader, the gap and the application in its slot."""
    slot = slot0_offset - IMAGE_OFFSET
    if len(loader) > slot:
        raise ValueError(f"the loader of {len(loader)} bytes does not fit before the slot at {slot:#x}")
    if app[:4] != MCUBOOT_MAGIC:
        raise ValueError("the application is not an MCUboot image")
    if len(app) > slot0_size:
        raise ValueError(f"the application of {len(app)} bytes does not fit the slot of {slot0_size:#x}")
    if loader[:4] == MCUBOOT_MAGIC:
        raise ValueError("the first file is an application, the loader comes first")
    return loader + b"\xff" * (slot - len(loader)) + app


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("loader", type=Path, help="zephyr.phy62x2.bin of MCUboot")
    parser.add_argument("app", type=Path, help="zephyr.signed.bin of the application")
    parser.add_argument("image", type=Path, help="the image to write")
    parser.add_argument("--slot0-offset", type=lambda v: int(v, 0), default=SLOT0_OFFSET,
                        help="flash offset of slot 0 (default: ECB02C, 0x18000)")
    parser.add_argument("--slot0-size", type=lambda v: int(v, 0), default=SLOT0_SIZE,
                        help="size of slot 0 (default: ECB02C, 0x25000)")
    args = parser.parse_args()

    try:
        image = merge(args.loader.read_bytes(), args.app.read_bytes(), args.slot0_offset,
                      args.slot0_size)
    except ValueError as error:
        raise SystemExit(f"merge_loader.py: {error}") from error
    args.image.write_bytes(image)


if __name__ == "__main__":
    main()

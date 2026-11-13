#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Join the MCUboot image of the ECB02C and a signed application into one image.

The image is for the first flashing through the boot ROM and holds everything from the flash offset
0x2000, the table of the boot ROM in front of the loader, to the end of the application:

    rdwr_phy62x2.py -p /dev/ttyUSB0 -r we 0x11002000 <image>

The application goes to its slot at 0x18000, the gap between the loader and the slot is 0xff. Later
applications are uploaded to the serial recovery of MCUboot alone, as the signed image.

usage: merge_loader.py <loader.phy6252.bin> <application.signed.bin> <image>
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


def merge(loader: bytes, app: bytes) -> bytes:
    """Return the image: the loader, the gap and the application in its slot."""
    slot = SLOT0_OFFSET - IMAGE_OFFSET
    if len(loader) > slot:
        raise ValueError(f"the loader of {len(loader)} bytes does not fit before the slot at {slot:#x}")
    if app[:4] != MCUBOOT_MAGIC:
        raise ValueError("the application is not an MCUboot image")
    if len(app) > SLOT0_SIZE:
        raise ValueError(f"the application of {len(app)} bytes does not fit the slot of {SLOT0_SIZE:#x}")
    if loader[:4] == MCUBOOT_MAGIC:
        raise ValueError("the first file is an application, the loader comes first")
    return loader + b"\xff" * (slot - len(loader)) + app


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("loader", type=Path, help="zephyr.phy6252.bin of MCUboot")
    parser.add_argument("app", type=Path, help="zephyr.signed.bin of the application")
    parser.add_argument("image", type=Path, help="the image to write")
    args = parser.parse_args()

    try:
        image = merge(args.loader.read_bytes(), args.app.read_bytes())
    except ValueError as error:
        raise SystemExit(f"merge_loader.py: {error}") from error
    args.image.write_bytes(image)


if __name__ == "__main__":
    main()

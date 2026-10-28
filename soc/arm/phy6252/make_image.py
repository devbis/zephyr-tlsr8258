#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Package a PHY6252 XIP ELF with a ROM-loader segment table."""

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile

FLASH_BASE = 0x11010100
FLASH_ORIGIN = 0x11000000
FLASH_END = 0x11030000
IMAGE_OFFSET = 0x2000
TABLE_SIZE = 0x100
RUN_ADDRESS = 0x1FFF1838
RAM_START = 0x1FFF1840
RAM_END = 0x20000000


def validate_ram_functions(elf):
    """Ensure the flash controller's cache-bypass code is loaded into SRAM."""
    symbols = elf.get_section_by_name(".symtab")
    if symbols is None:
        raise ValueError("ELF has no symbol table")
    names = ("phy6252_flash_command", "phy6252_flash_wait")
    found = [symbols.get_symbol_by_name(name) or [] for name in names]
    if symbols.get_symbol_by_name("phy6252_flash_api") and not all(found):
        raise ValueError("flash driver is missing SRAM cache-bypass code")
    if any(found) and not all(found):
        raise ValueError("incomplete flash cache-bypass code")
    for name, matches in zip(names, found, strict=True):
        if matches:
            symbol = matches[0]
            start = symbol["st_value"] & ~1
            end = start + symbol["st_size"]
            if not RAM_START <= start < end <= RAM_END:
                raise ValueError(f"{name} is not entirely in SRAM")
            if not any(seg["p_type"] == "PT_LOAD" and seg["p_filesz"] and
                       seg["p_vaddr"] <= start and
                       end <= seg["p_vaddr"] + seg["p_filesz"]
                       for seg in elf.iter_segments()):
                raise ValueError(f"{name} is not in an initialized load segment")


def make_image(elf):
    """Collect XIP data and build ROM load records for initialized segments."""
    segments = sorted(
        (seg for seg in elf.iter_segments()
         if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]),
        key=lambda seg: seg["p_paddr"],
    )
    if not segments or segments[0]["p_paddr"] != FLASH_BASE:
        raise ValueError("first load segment must start at the XIP address")

    flash = bytearray()
    for seg in segments:
        load_address = seg["p_paddr"]
        address = seg["p_vaddr"]
        size = seg["p_filesz"]
        if not FLASH_ORIGIN <= load_address < FLASH_END or load_address + size > FLASH_END:
            raise ValueError("load segment is outside the application flash slot")
        if FLASH_BASE <= address < FLASH_END:
            if load_address != address or address + seg["p_memsz"] > FLASH_END:
                raise ValueError("invalid XIP load segment")
        elif RAM_START <= address < RAM_END:
            if address + seg["p_memsz"] > RAM_END:
                raise ValueError("RAM load segment exceeds SRAM")
        else:
            raise ValueError("load segment destination is outside XIP and SRAM")

        offset = load_address - FLASH_BASE
        if offset < len(flash):
            raise ValueError("overlapping or out-of-range flash load segment")
        flash.extend(b"\xff" * (offset - len(flash)))
        flash.extend(seg.data())

    xip_segments = [seg for seg in segments if FLASH_BASE <= seg["p_vaddr"] < FLASH_END]
    if not xip_segments or xip_segments[0]["p_vaddr"] != FLASH_BASE:
        raise ValueError("missing XIP vectors at the start of the application slot")

    if len(flash) < 8:
        raise ValueError("missing Cortex-M initial stack and reset vectors")
    stack, reset = struct.unpack_from("<II", flash)
    if not 0x1FFF1840 <= stack <= 0x20000000 or stack & 7:
        raise ValueError("initial stack is outside SRAM or is not aligned")
    if (not any(seg["p_vaddr"] <= (reset & ~1) < seg["p_vaddr"] + seg["p_filesz"]
                for seg in xip_segments) or not reset & 1):
        raise ValueError("reset vector is not a Thumb address in the loaded XIP image")

    descriptor = struct.pack("<II", stack, reset)
    image = bytearray(b"\xff" * (FLASH_BASE - FLASH_ORIGIN - IMAGE_OFFSET))
    image.extend(flash)
    image.extend(b"\xff" * (-len(image) % 4))
    descriptor_offset = IMAGE_OFFSET + len(image)
    image.extend(descriptor)
    if IMAGE_OFFSET + len(image) > 0x30000:
        raise ValueError("image and descriptor exceed the application slot")

    struct.pack_into("<I", image, 0, len(segments) + 1)
    struct.pack_into("<I", image, 8, RUN_ADDRESS)
    for index, seg in enumerate(segments):
        load_address = seg["p_paddr"]
        struct.pack_into("<IIII", image, TABLE_SIZE + index * 16,
                         load_address - FLASH_ORIGIN, seg["p_filesz"], seg["p_vaddr"],
                         0xFFFFFFFF)
    struct.pack_into("<IIII", image, TABLE_SIZE + len(segments) * 16,
                     descriptor_offset, len(descriptor), RUN_ADDRESS, 0xFFFFFFFF)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.elf.open("rb") as source:
        elf = ELFFile(source)
        validate_ram_functions(elf)
        image = make_image(elf)
    args.output.write_bytes(image)


if __name__ == "__main__":
    main()

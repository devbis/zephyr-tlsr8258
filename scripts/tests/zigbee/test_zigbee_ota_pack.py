# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""Tests for scripts/zigbee_ota_pack.py."""

import importlib.util
import struct
from pathlib import Path

import pytest

SCRIPT = Path(__file__).parents[2] / "zigbee_ota_pack.py"
spec = importlib.util.spec_from_file_location("zigbee_ota_pack", SCRIPT)
ota_pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota_pack)

IMAGE = struct.pack("<I", ota_pack.MCUBOOT_MAGIC) + bytes(range(100))


def test_header_fields():
    ota = ota_pack.pack(IMAGE, 0x1141, 0x0002, 7, "fw")

    magic, hdr_ver, hdr_len, _, manuf, img_type, file_ver, stack = struct.unpack_from(
        "<IHHHHHIH", ota
    )
    assert magic == 0x0BEEF11E
    assert hdr_ver == 0x0100
    assert hdr_len == 56
    assert (manuf, img_type, file_ver) == (0x1141, 0x0002, 7)
    assert stack == 0x0002


def test_total_size_and_element():
    ota = ota_pack.pack(IMAGE, 0x1141, 1, 1, "fw")

    (total,) = struct.unpack_from("<I", ota, 52)
    assert total == len(ota) == 56 + 6 + len(IMAGE)

    tag, length = struct.unpack_from("<HI", ota, 56)
    assert (tag, length) == (0, len(IMAGE))
    assert ota[62:] == IMAGE


def test_rejects_non_mcuboot_image():
    with pytest.raises(ValueError):
        ota_pack.pack(b"\x00" * 64, 0x1141, 1, 1, "fw")


def test_rejects_long_header_string():
    with pytest.raises(ValueError):
        ota_pack.pack(IMAGE, 0x1141, 1, 1, "x" * 33)

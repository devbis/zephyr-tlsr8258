#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARDS = (
    ROOT / "boards/telink/tlsr8258_generic/tlsr8258_generic.dts",
    ROOT / "boards/telink/tlsr8258_tb03f/tlsr8258_tb03f.dts",
)
RESET_S = ROOT / "arch/tc32/core/reset.S"


def _code_partition_offset(dts_path: Path) -> int:
    text = dts_path.read_text()
    chosen = re.search(r"zephyr,code-partition\s*=\s*&([A-Za-z0-9_]+)\s*;", text)
    if not chosen:
        raise AssertionError(f"missing zephyr,code-partition in {dts_path}")

    label = re.escape(chosen.group(1))
    partition = re.search(
        rf"{label}\s*:\s*partition@[0-9a-fA-F]+\s*\{{.*?reg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>;",
        text,
        re.S,
    )
    if not partition:
        raise AssertionError(
            f"missing partition definition for {chosen.group(1)} in {dts_path}"
        )
    return int(partition.group(1), 16)


class TestTlsr8258DirectBoot(unittest.TestCase):
    def test_boards_use_direct_boot_code_partition(self):
        offsets = {path.name: _code_partition_offset(path) for path in BOARDS}
        self.assertEqual(
            offsets,
            {
                "tlsr8258_generic.dts": 0x0,
                "tlsr8258_tb03f.dts": 0x0,
            },
        )

    def test_reset_copies_init_sections_from_flash_lma(self):
        text = RESET_S.read_text()

        rodata = re.search(r"\.Lrodata_lma_start:\s*\n\s*\.word\s+([^\n]+)", text)
        data = re.search(r"\.Ldata_lma_start:\s*\n\s*\.word\s+([^\n]+)", text)

        self.assertIsNotNone(rodata, "missing .Lrodata_lma_start definition")
        self.assertIsNotNone(data, "missing .Ldata_lma_start definition")
        self.assertNotIn("0x00840000", rodata.group(1))
        self.assertNotIn("0x00840000", data.group(1))


if __name__ == "__main__":
    unittest.main()

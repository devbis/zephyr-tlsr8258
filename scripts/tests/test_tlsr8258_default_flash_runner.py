#!/usr/bin/env python3

# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOARD_CMAKES = (
    ROOT / "boards/telink/tlsr8258_generic/board.cmake",
    ROOT / "boards/telink/tlsr8258_tb03f/board.cmake",
)


class TestTlsr8258DefaultFlashRunner(unittest.TestCase):
    def test_boards_default_to_probe_rs(self):
        for path in BOARD_CMAKES:
            text = path.read_text()
            self.assertIn("boards/common/probe-rs.board.cmake", text, path.name)
            self.assertIn('board_runner_args(probe-rs', text, path.name)
            self.assertIn('"--chip=TLSR8258"', text, path.name)
            self.assertIn('"--file-type=bin"', text, path.name)
            self.assertIn('"--dt-flash=y"', text, path.name)
            self.assertNotIn("tlsrpgm.board.cmake", text, path.name)
            self.assertNotIn("board_runner_args(tlsrpgm", text, path.name)


if __name__ == "__main__":
    unittest.main()

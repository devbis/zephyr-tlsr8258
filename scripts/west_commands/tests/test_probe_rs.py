# Copyright (c) 2026
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from runners.core import FileType, RunnerConfig
from runners.probe_rs import ProbeRsBinaryRunner

RC_BUILD_DIR = "/test/build-dir"
RC_BOARD_DIR = "/test/zephyr/boards/telink/tlsr8258_generic"
RC_KERNEL_ELF = "test-zephyr.elf"
RC_KERNEL_EXE = "test-zephyr.exe"
RC_KERNEL_HEX = None
RC_KERNEL_BIN = "test-zephyr.bin"
RC_KERNEL_UF2 = None
RC_KERNEL_MOT = None
RC_GDB = "test-none-gdb"
RC_OPENOCD = "test-openocd"
RC_OPENOCD_SEARCH = ["/test/openocd/search"]


def require_patch(program):
    if program != "probe-rs":
        raise AssertionError(f"unexpected required program: {program}")
    return program


class TestProbeRsRunner(unittest.TestCase):
    def setUp(self):
        self.tmpdir = tempfile.TemporaryDirectory()
        Path(self.tmpdir.name, RC_KERNEL_ELF).write_text("")
        Path(self.tmpdir.name, RC_KERNEL_BIN).write_text("")
        self.runner_cfg = RunnerConfig(
            RC_BUILD_DIR,
            RC_BOARD_DIR,
            RC_KERNEL_ELF,
            RC_KERNEL_EXE,
            RC_KERNEL_HEX,
            RC_KERNEL_BIN,
            RC_KERNEL_UF2,
            RC_KERNEL_MOT,
            None,
            FileType.OTHER,
            gdb=RC_GDB,
            openocd=RC_OPENOCD,
            openocd_search=RC_OPENOCD_SEARCH,
        )
        self.cwd = Path.cwd()
        self._old_cwd = None

    def tearDown(self):
        self.tmpdir.cleanup()

    @patch("runners.probe_rs.ProbeRsBinaryRunner.check_call")
    @patch("runners.core.ZephyrBinaryRunner.require", side_effect=require_patch)
    def test_flash_defaults_to_elf_when_file_type_is_unknown(self, require, check_call):
        runner = ProbeRsBinaryRunner(self.runner_cfg, "TLSR8258")
        old_cwd = Path.cwd()
        try:
            import os
            os.chdir(self.tmpdir.name)
            runner.run("flash")
        finally:
            os.chdir(old_cwd)

        check_call.assert_called_once_with(
            [
                "probe-rs",
                "download",
                "--chip",
                "TLSR8258",
                "--binary-format",
                "elf",
                RC_KERNEL_ELF,
            ]
        )

    @patch("runners.probe_rs.ProbeRsBinaryRunner.check_call")
    @patch("runners.core.ZephyrBinaryRunner.require", side_effect=require_patch)
    def test_flash_bin_uses_base_address(self, require, check_call):
        runner_cfg = self.runner_cfg._replace(file_type=FileType.BIN)
        runner = ProbeRsBinaryRunner(runner_cfg, "TLSR8258", flash_addr=0x0)

        old_cwd = Path.cwd()
        try:
            import os
            os.chdir(self.tmpdir.name)
            runner.run("flash")
        finally:
            os.chdir(old_cwd)

        check_call.assert_called_once_with(
            [
                "probe-rs",
                "download",
                "--chip",
                "TLSR8258",
                "--binary-format",
                "bin",
                "--base-address",
                "0x0",
                RC_KERNEL_BIN,
            ]
        )


if __name__ == "__main__":
    unittest.main()

# Copyright (c) 2026
#
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import importlib
import sys
import unittest
from pathlib import Path
from unittest.mock import call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from runners.core import FileType, RunnerConfig

TEST_PROBE = "tcp://192.168.70.44:55555"
TEST_TOOL = "/opt/telink/TlsrPgm.py"
RC_BUILD_DIR = "/test/build-dir"
RC_BOARD_DIR = "/test/zephyr/boards/telink/tlsr8258_generic"
RC_KERNEL_ELF = "test-zephyr.elf"
RC_KERNEL_EXE = "test-zephyr.exe"
RC_KERNEL_HEX = "test-zephyr.hex"
RC_KERNEL_BIN = "test-zephyr.bin"
RC_KERNEL_MOT = "test-zephyr.mot"
RC_GDB = "test-none-gdb"
RC_OPENOCD = "test-openocd"
RC_OPENOCD_SEARCH = ["/test/openocd/search"]


def require_patch(program):
    if program not in ["python3", TEST_TOOL]:
        raise AssertionError(f"unexpected required program: {program}")
    return program


class TestTlsrPgmRunner(unittest.TestCase):
    def setUp(self):
        self.runner_cfg = RunnerConfig(
            RC_BUILD_DIR,
            RC_BOARD_DIR,
            RC_KERNEL_ELF,
            RC_KERNEL_EXE,
            RC_KERNEL_HEX,
            RC_KERNEL_BIN,
            RC_KERNEL_MOT,
            None,
            FileType.OTHER,
            gdb=RC_GDB,
            openocd=RC_OPENOCD,
            openocd_search=RC_OPENOCD_SEARCH,
        )

    def _runner_cls(self):
        module = importlib.import_module("runners.tlsrpgm")
        return module.TlsrPgmBinaryRunner

    def test_runner_name(self):
        self.assertEqual(self._runner_cls().name(), "tlsrpgm")

    @patch("runners.core.ZephyrBinaryRunner.require", side_effect=require_patch)
    @patch("runners.core.ZephyrBinaryRunner.check_call")
    @patch("runners.tlsrpgm.os.path.exists", return_value=True)
    def test_flash_command(self, exists, check_call, require):
        runner = self._runner_cls()(
            self.runner_cfg,
            tool=TEST_TOOL,
            probe=TEST_PROBE,
            delay="20",
            timeout="500",
            activate="500",
            address="0x0",
            erase=False,
            reset=False,
        )

        runner.run("flash")

        self.assertEqual(
            check_call.call_args_list,
            [
                call(
                    [
                        "python3",
                        TEST_TOOL,
                        "-p",
                        TEST_PROBE,
                        "-d",
                        "20",
                        "-t",
                        "500",
                        "-a",
                        "500",
                        "we",
                        "0x0",
                        RC_KERNEL_BIN,
                    ]
                )
            ],
        )

    @patch("runners.tlsrpgm.BuildConfiguration")
    def test_create_uses_dt_flash_offset(self, build_conf):
        class DummyBuildConf:
            def getboolean(self, key):
                return key == "CONFIG_HAS_FLASH_LOAD_OFFSET"

            def __getitem__(self, key):
                return {
                    "CONFIG_FLASH_BASE_ADDRESS": 0,
                    "CONFIG_FLASH_LOAD_OFFSET": 0,
                }[key]

        build_conf.return_value = DummyBuildConf()
        parser = argparse.ArgumentParser(allow_abbrev=False)
        self._runner_cls().add_parser(parser)
        args = parser.parse_args(["--probe", TEST_PROBE, "--dt-flash=y"])

        runner = self._runner_cls().create(self.runner_cfg, args)

        self.assertEqual(runner.addr, "0x0")


if __name__ == "__main__":
    unittest.main()

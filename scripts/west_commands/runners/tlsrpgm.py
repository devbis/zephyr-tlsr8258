# Copyright (c) 2026
#
# SPDX-License-Identifier: Apache-2.0

'''Runner for TlsrPgm.py.'''

from __future__ import annotations

import os
from pathlib import Path

from runners.core import BuildConfiguration, RunnerCaps, ZephyrBinaryRunner


def _default_tlsrpgm() -> str:
    tool = Path(__file__).resolve().parents[4] / 'TlsrPgm.py'
    return str(tool) if tool.exists() else 'TlsrPgm.py'


class TlsrPgmBinaryRunner(ZephyrBinaryRunner):
    '''Runner front-end for TlsrPgm.py.'''

    def __init__(self, cfg, tool, probe, delay, timeout, activate, address,
                 erase=False, reset=False, python='python3'):
        super().__init__(cfg)
        self.tool = tool
        self.probe = probe
        self.delay = str(delay)
        self.timeout = str(timeout)
        self.activate = str(activate)
        self.addr = str(address)
        self.erase = erase
        self.reset = reset
        self.python = python

    @classmethod
    def name(cls):
        return 'tlsrpgm'

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={'flash'}, erase=True, reset=True, flash_addr=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument('--tool', default=_default_tlsrpgm(),
                            help='path to TlsrPgm.py')
        parser.add_argument('--probe', required=True,
                            help='probe endpoint, e.g. tcp://192.168.70.44:55555')
        parser.add_argument('--delay', default='20',
                            help='SWire transfer rate divisor passed to TlsrPgm.py')
        parser.add_argument('--timeout', default='500',
                            help='hard reset time in milliseconds passed as -t')
        parser.add_argument('--activate', default='500',
                            help='activation time in milliseconds passed as -a')
        parser.add_argument('--python', default='python3',
                            help='python interpreter used to run TlsrPgm.py')
        parser.add_argument('--address', default='0x0',
                            help='start flash address when --dt-flash is not used')

    @classmethod
    def do_create(cls, cfg, args):
        if args.dt_flash:
            build_conf = BuildConfiguration(cfg.build_dir)
            address = hex(ZephyrBinaryRunner.flash_address_from_build_conf(build_conf))
        else:
            address = args.address

        return TlsrPgmBinaryRunner(
            cfg,
            tool=args.tool,
            probe=args.probe,
            delay=args.delay,
            timeout=args.timeout,
            activate=args.activate,
            address=address,
            erase=args.erase,
            reset=args.reset,
            python=args.python,
        )

    def _base_cmd(self):
        python = self.require(self.python)
        if os.path.sep in self.tool and not os.path.exists(self.tool):
            raise FileNotFoundError(self.tool)

        cmd = [
            python,
            self.tool,
            '-p',
            self.probe,
            '-d',
            self.delay,
            '-t',
            self.timeout,
            '-a',
            self.activate,
        ]

        if self.reset:
            cmd.append('-r')

        return cmd

    def do_run(self, command, **kwargs):
        if command != 'flash':
            raise ValueError(f'runner {self.name()} does not implement command {command}')

        if self.erase:
            self.check_call(self._base_cmd() + ['ea'])

        self.check_call(self._base_cmd() + ['we', self.addr, self.cfg.bin_file])

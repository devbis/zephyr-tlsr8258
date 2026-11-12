#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""rdwr_phy62x2.py for the ECB02C without a power cycle.

The running application is rebooted with its shell (kernel reboot cold: the reset cause makes the
boot ROM open its short UART window) and the tool syncs at 9600 baud at once. If the chip already is
in the boot ROM it is used as it is. The boot ROM listens on P9/P10 and the shell is on UART1 (P2/P3)
by default, so with two adapters give the port of the shell in the environment variable SHELL_PORT; it
defaults to the port of the tool. Run it where rdwr_phy62x2.py of the PHY62x2 utilities is, the
arguments are those of that tool, for example:

    SHELL_PORT=/dev/ttyUSB1 flash_rom.py -p /dev/ttyUSB0 -r we 0x11002000 loader.phy6252.bin
"""

import os
import sys
import time

import rdwr_phy62x2 as m

def connect(self, baud=m.DEF_RUN_BAUD):
    p = self._port
    got = None
    # already in the boot ROM (command mode at 115200)?
    p.baudrate = m.DEF_RUN_BAUD
    p.timeout = 0.3
    p.reset_input_buffer()
    if self.ReadRevision():
        got = b"already in the boot ROM"
        return finish(self, baud, got)
    p.baudrate = 115200
    p.timeout = 0.3
    p.reset_input_buffer()
    shell_port = os.environ.get("SHELL_PORT")
    if shell_port:
        import serial
        with serial.Serial(shell_port, 115200) as sh:
            sh.write(b"\r\nkernel reboot cold\r\n")
            sh.flush()
    else:
        p.write(b"\r\nkernel reboot cold\r\n")   # ignored by the ROM
        p.flush()
    time.sleep(0.05)
    p.baudrate = m.START_BAUD
    p.timeout = 0.02
    p.reset_input_buffer()
    t0 = time.time()
    while time.time() - t0 < 12:
        p.write(b"UXTDWU")
        p.flush()
        r = p.read(12)
        if b"cmd>>:" in r or b"fct>>:" in r:
            got = r
            break
    if got is None:
        print("Chip Reset error! No answer of the boot ROM")
        p.close()
        sys.exit(4)
    print("Chip Reset Ok. Response: %s (after %.2f s)" % (got, time.time() - t0))
    time.sleep(0.1)
    p.baudrate = m.DEF_RUN_BAUD
    p.timeout = 0.2
    p.reset_input_buffer()
    return finish(self, baud, got)

def finish(self, baud, got):
    p = self._port
    if b"fct>>:" in got:
        print("Chip in FCT mode!")
        return False
    if not self.ReadRevision():
        p.close()
        sys.exit(4)
    if not self.FlashUnlock():
        p.close()
        sys.exit(4)
    if not self.tg7100:
        for reg in (0x4000f054, 0x4000f140, 0x4000f144):
            if not self.write_reg(reg, 0):
                print("PHY62x2 - Error init!")
                p.close()
                sys.exit(4)
    print(self.chip, "- connected Ok")
    return self.SetBaud(baud)

m.phyflasher.Connect = connect
sys.argv[0] = "rdwr_phy62x2.py"
m.main()

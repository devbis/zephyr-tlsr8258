.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

Ai-Thinker PB-03F
#################

The PB-03F module contains a Phyplus PHY6252 Cortex-M0 SoC with 64 KiB SRAM
and 256 KiB flash. This port is experimental and has not been tested on a
module. It supports the Cortex-M system timer, two hardware counters, GPIO,
a UART console on P9 (TX) and P10 (RX), and an experimental IEEE 802.15.4
radio driver. The radio setup is adapted from the PHY6222 backend in
``zigbee-rs`` at commit ``76d184caa4756a69eb17ee4fd74244cf6ed62806``. It
supports channel selection, TX, RX, CCA, software address filtering and
waiting for TX acknowledgements. Automatic ACK generation on received frames
is not implemented. Radio timing and operation have not been verified on
PHY6252 hardware.

The ROM clock entry points and the 16 MHz crystal setting follow the
PHY62XX SDK 3.1.1. Their behavior, the peripheral clock and the UART pins need
hardware verification. The ROM interrupt dispatch table occupies the first
1 KiB of SRAM; additional SRAM below ``0x1fff1838`` is reserved for ROM.

The GPIO controller uses hardware indices rather than the pad numbers printed
in the PHY6252 documentation. Indices 0 through 22 map respectively to pads
P0, P1, P2, P3, P7, P9, P10, P11, P14, P15, P16, P17, P18, P20, P23, P24,
P25, P26, P27, P31, P32, P33 and P34. Indices 5 and 6 are reserved for the
console, 10 and 11 for the crystal, and 1 and 16 through 18 for pads not listed
for PHY6252. The module does not necessarily expose every pad. The driver
supports input, output, pulls and single-edge or level interrupts; both-edge
interrupts and open-drain output are not supported.

The optional ``CONFIG_FLASH_PHY6252`` driver uses ROM flash functions and
executes its cache-bypass path from SRAM. The ``storage_partition`` occupies
the last three 4 KiB sectors. Program and erase lock interrupts while the flash
is busy, which can delay interrupts and affect timekeeping. The ROM calls,
cache restoration, flash protection and reset recovery have not been tested on
hardware; enable the driver only for development and validate it on the device.
The driver waits for the controller and flash to become idle before returning
to XIP code; a hardware failure can therefore leave it waiting indefinitely.
Program and erase operations check the resulting flash contents.

Build
*****

For example, to build :zephyr:code-sample:`hello_world`:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: pb_03f
   :goals: build

The build produces ``zephyr.phy6252.bin``. Its first byte corresponds to flash
offset ``0x2000``, with the ROM segment table at that offset, application XIP
code beginning at ``0x10100``, load records for initialized SRAM data, and an
SRAM startup descriptor. The image is limited to flash offsets below
``0x30000``. It does not include flash contents below ``0x2000``. The image
layout follows the PHY62xx serial utility and has not been tested with PB-03F
ROM.

The serial utility `rdwr_phy62x2.py`_ can write a binary at a specified flash
offset. The command form is:

.. code-block:: console

   python3 rdwr_phy62x2.py --port <port> we 0x2000 zephyr.phy6252.bin

The ``we`` operation erases and replaces the sectors it covers, including any
existing firmware and settings. The UART voltage, boot-mode wiring and recovery
procedure for a specific board must be verified before flashing.

.. _rdwr_phy62x2.py: https://github.com/pvvx/PHY62x2/blob/master/Utils/rdwr_phy62x2.py

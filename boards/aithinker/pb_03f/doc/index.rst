.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

Ai-Thinker PB-03F
#################

The PB-03F module contains a Phyplus PHY62x2 Cortex-M0 SoC with 64 KiB SRAM
and 512 KiB flash (the boot ROM reports flash ID ``1340c4``, revision ``6222M005``). This port is experimental and has not been tested on a
module. It supports the Cortex-M system timer, two hardware counters, GPIO,
a UART console on P9 (TX) and P10 (RX), and an experimental IEEE 802.15.4
radio driver. The radio setup is adapted from the PHY62x2 backend in
``zigbee-rs`` at commit ``76d184caa4756a69eb17ee4fd74244cf6ed62806``. It
supports channel selection, TX, RX, CCA, software address filtering and
waiting for TX acknowledgements. Automatic ACK generation on received frames
is not implemented. Radio timing and operation have not been verified on
PHY62x2 hardware.

The ROM clock entry points and the 16 MHz crystal setting follow the
PHY62XX SDK 3.1.1. Their behavior, the peripheral clock and the UART pins need
hardware verification. The ROM interrupt dispatch table occupies the first
1 KiB of SRAM; additional SRAM below ``0x1fff1838`` is reserved for ROM.

The GPIO controller uses hardware indices rather than the pad numbers printed
in the PHY62x2 documentation. Indices 0 through 22 map respectively to pads
P0, P1, P2, P3, P7, P9, P10, P11, P14, P15, P16, P17, P18, P20, P23, P24,
P25, P26, P27, P31, P32, P33 and P34. Indices 5 and 6 are reserved for the
console, 10 and 11 for the crystal, and 1 and 16 through 18 for pads not listed
for PHY62x2. The module does not necessarily expose every pad. The driver
supports input, output, pulls and edge or level interrupts; open-drain output is not supported.
The controller has one polarity per pin, so an interrupt on both edges flips the polarity after
every edge in the interrupt handler. An edge that comes within a few instructions of the handler
reading the pin level can be merged with the previous one; read the pin in the callback instead of
counting callbacks. Level interrupts cannot use both edges. Wakeup from sleep through the pins
is not implemented.

The optional ``CONFIG_FLASH_PHY62X2`` driver uses ROM flash functions and
executes its cache-bypass path from SRAM. The ``storage_partition`` occupies
the last four 4 KiB sectors. Program and erase lock interrupts while the flash
is busy, which can delay interrupts and affect timekeeping. The ROM calls,
cache restoration, flash protection and reset recovery have not been tested on
hardware; enable the driver only for development and validate it on the device.
The driver waits for the controller and flash to become idle before returning
to XIP code; a hardware failure can therefore leave it waiting indefinitely.
Program and erase operations check the resulting flash contents.

Watchdog
********

The watchdog of the chip is the DesignWare one on the 32 kHz RC oscillator, as on the ECB02C.
``CONFIG_WATCHDOG=y`` enables it, and ``CONFIG_PHY62X2_WATCHDOG`` then starts it and feeds it from
a thread of the lowest priority, so a hang that stops the scheduler resets the chip. It is off by
default and has not been tried on this module.

Flash layout
************

========== ========= ==========================================================
Offset     Size      Contents
========== ========= ==========================================================
0x002000   0x400     Segment table of the boot ROM
0x010100   0x7f00    MCUboot, XIP from 0x11010100
0x018000   0x32000   Slot 0, the running application (MCUboot header of 0x200 bytes)
0x04a000   0x32000   Slot 1, the new image received by serial recovery or OTA
0x07c000   0x4000    ``storage_partition``
========== ========= ==========================================================

MCUboot swaps the slots with ``CONFIG_BOOT_SWAP_USING_MOVE``, which needs no scratch area. The
loader sits in the segment table of the boot ROM and is the image written at the flash offset
``0x2000``. An image XIP below ``0x10000`` does not start. The layout is the one of
``boot/zephyr/boards/pb_03f.overlay`` in the ``mcuboot`` module; change both together. The
512 KiB layout has not been tried on a module: the ECB02C, with the same loader geometry and one
slot, has.

Build
*****

For example, to build :zephyr:code-sample:`hello_world` as a plain image for the boot ROM:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: pb_03f
   :goals: build

The build produces ``zephyr.phy62x2.bin``. Its first byte corresponds to flash offset ``0x2000``,
with the ROM segment table at that offset, application XIP code beginning at ``0x10100``, load
records for initialized SRAM data, and an SRAM startup descriptor. The image is limited to flash
offsets below ``0x7c000``, where the storage partition begins. It does not include flash contents
below ``0x2000``.

MCUboot
*******

Build the loader from the ``mcuboot`` module, and the application for slot 0 with
``pb_03f_mcuboot.conf`` and ``pb_03f_mcuboot.overlay`` of the ``zigbee_shell`` sample:

.. code-block:: console

   west build -b pb_03f -d build-boot ../bootloader/mcuboot/boot/zephyr
   west build -b pb_03f -d build-app samples/zigbee/zigbee_shell -- \
       -DEXTRA_CONF_FILE="router.conf;pb_03f_mcuboot.conf" \
       -DEXTRA_DTC_OVERLAY_FILE=$ZEPHYR_BASE/samples/zigbee/zigbee_shell/pb_03f_mcuboot.overlay

``boards/chinalctech/ecb02c/support/merge_loader.py --slot0-size 0x32000`` joins
``build-boot/zephyr/zephyr.phy62x2.bin`` and ``build-app/zephyr/zephyr.signed.bin`` into one image
for the first flashing at ``0x11002000``. The loader waits 3 s for a serial recovery command on
UART0 (P9/P10) after each reset, then starts slot 0, or the new image if slot 1 holds one.

The serial utility `rdwr_phy62x2.py`_ can write a binary at a specified flash
offset. The command form is:

.. code-block:: console

   python3 rdwr_phy62x2.py --port <port> we 0x2000 zephyr.phy62x2.bin

The ``we`` operation erases and replaces the sectors it covers, including any
existing firmware and settings. The UART voltage, boot-mode wiring and recovery
procedure for a specific board must be verified before flashing.

.. _rdwr_phy62x2.py: https://github.com/pvvx/PHY62x2/blob/master/Utils/rdwr_phy62x2.py

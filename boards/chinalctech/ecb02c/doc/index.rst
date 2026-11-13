.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

ChinaLCTech ECB02C
##################

The ECB02C module contains a Phyplus PHY62x2 Cortex-M0 SoC with 64 KiB SRAM and 256 KiB flash. The
chip reports itself as PHY6222 (revision ``6222M005``); the port is the one of the PHY6252 (see the
PB-03F board). Checked on a module: the core at 48 MHz, the UART console at 115200 baud on P2
and P3 (UART1, USB-UART of the module, with TX on P3), the flash driver, the XIP cache, the watchdog, flashing through the boot ROM and
MCUboot with its serial recovery, and the IEEE 802.15.4 radio (the ``zigbee_shell`` router joins a
Zigbee2MQTT network and answers its interview). The GPIO driver has not been tried on this module.

Clock
*****

The core runs from the 48 MHz DLL. The devicetree gives the frequency in the CPU
``timebase-frequency`` and in the ``clock-frequency`` of the UARTs, and the SoC code selects the
clock from it. At 16 MHz the divider of the UART gives 111111 baud for 115200 and the receiver loses
characters of long lines.

The boot ROM leaves the cache of the XIP flash bypassed. The SoC code enables it at the start of the
system initialisation; without the cache the code runs about a hundred times slower.

Watchdog
********

The watchdog of the chip is enabled with ``CONFIG_WATCHDOG=y``. ``CONFIG_PHY6252_WATCHDOG`` starts
it with a timeout of 8 s and feeds it from a thread of the lowest priority, so a hang that stops the
scheduler resets the chip. MCUboot feeds the watchdog itself.

Flash layout
************

========== ========= ==========================================================
Offset     Size      Contents
========== ========= ==========================================================
0x002000   0x400     Segment table of the boot ROM
0x010100   0x7f00    MCUboot, XIP from 0x11010100
0x018000   0x25000   Slot of the application (MCUboot header of 0x200 bytes)
0x03d000   0x3000    ``storage_partition``
========== ========= ==========================================================

MCUboot runs from the segment table of the boot ROM; its image is built with the board and written
at the flash offset ``0x2000``. The application is signed for the single slot and replaces the
previous one through the serial recovery of MCUboot. A single application slot has no room for a
second one, and an image XIP below ``0x10000`` does not start.

UART pins
*********

UART1 is the console by default. The USB-UART of the module is wired to P2 and P3 the other way
round (TX of the chip on P3, RX on P2), which ``CONFIG_PHY6252_UART1_SWAP`` selects; P2 and P3 are the
pads of the debug interface and the SoC code gives them to the UART. P9 and P10 are the pins of the
boot ROM loader, which listens at 9600 baud for a synchronisation string for a short time after a
reset: keep them free of the console to flash through the boot ROM with a second USB-UART.
``ecb02c_uart0_p9p10.overlay`` moves the console, the shell and the MCUmgr transport to UART0 on
P9 (TX) and P10 (RX), for a module that is wired there. Pass the file with
``-DEXTRA_DTC_OVERLAY_FILE`` to the application and to MCUboot.

Flashing
********

The serial utility `rdwr_phy62x2.py`_ writes ``zephyr.phy6252.bin`` through the boot ROM; connect
it to P9/P10 and give the offset ``0x11002000``. An application that has a shell reboots into the
window of the boot ROM with ``kernel reboot cold``; ``support/flash_rom.py`` does that and then
runs the tool within the 50 ms the window needs, without a power cycle. The shell is on UART1 and
the boot ROM on P9/P10, so the script takes the port of the shell in ``SHELL_PORT`` when two
adapters are used. The reboot of the SoC code sets the reset cause that opens the window.

The release builds of the Zigbee firmware have ``ecb02c-coordinator.bin``, MCUboot and the ZNP
coordinator in one image for the first flashing. Connect a USB-UART to P9 and P10 and write it at
``0x11002000``: ``rdwr_phy62x2.py -p <port> -r we 0x11002000 ecb02c-coordinator.bin``. The erase of
a sector can take longer than the 0.5 s of the tool; raise its timeout if the write fails. The
image is made by ``support/merge_loader.py`` from the loader of the build and the signed
application. ``ecb02c-router-app.signed.bin``, ``ecb02c-end-device-app.signed.bin`` and
``ecb02c-coordinator-app.signed.bin`` are the applications alone, for the serial recovery.

An application is replaced through the serial recovery of MCUboot. ``support/phyflash.sh`` reboots
the application, catches the window MCUboot waits for a command in and uploads the signed image
with ``cmcumgr``; the client has to repeat a command after a timeout. The sample
:zephyr:code-sample:`chinalctech_ecb02c_shell` is such an application. A new loader is written
through the boot ROM, or by the application of
:zephyr:code-sample:`chinalctech_ecb02c_loader_update` that carries it. If the power fails while a
loader is written the chip has none and only the boot ROM on P9/P10 helps.

.. _rdwr_phy62x2.py: https://github.com/pvvx/PHY62x2/blob/master/Utils/rdwr_phy62x2.py

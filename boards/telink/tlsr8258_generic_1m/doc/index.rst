.. zephyr:board:: tlsr8258_generic_1m

Overview
********

The ``tlsr8258_generic_1m`` board target describes a bare Telink TLSR8258 SoC
module with the TC32 core and no board-specific peripherals. Zephyr boots
directly from flash address ``0x0``; the TLSR8258 boot SRAM mirror holds only
the early startup code.

Hardware
********

The TLSR8258 integrates a TC32 core at 24 MHz, 64 KB of SRAM, 1 MB of
flash, GPIO, a system timer, UART, an AES engine, a watchdog and an
IEEE 802.15.4 radio.

Supported Features
==================

.. zephyr:board-supported-hw::

Flash Layout
============

- ``0x00000`` - ``0xf9fff``: application image
- ``0xfa000`` - ``0xfdfff``: ``zigbee_nv`` storage partition
- ``0xff000`` - ``0xfffff``: ``ieee-addr`` factory data, where the vendor tools write
  the MAC address on 1 MB parts

MCUboot
*******

The :ref:`tlsr8258-mcuboot <snippet-tlsr8258-mcuboot>` snippet builds an
application for slot 0 of the flash layout that the TLSR8258 port of MCUboot uses.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

The TC32 toolchain is LLVM based; build with:

.. code-block:: console

   west build -b tlsr8258_generic_1m samples/hello_world -- \
     -DZEPHYR_TOOLCHAIN_VARIANT=host -DTOOLCHAIN_VARIANT_COMPILER=llvm \
     -DLLVM_TOOLCHAIN_PATH=<tc32-llvm>

Flashing
========

The default runner is ``probe-rs`` with a Telink SWS probe:

.. code-block:: console

   west flash --dev-id 'sws:tcp://<probe-host>:<port>'

The ``tlsrpgm`` runner flashes through ``TlsrPgm.py`` instead:

.. code-block:: console

   west flash --runner tlsrpgm -- --probe tcp://<probe-host>:<port>

RTT Console
===========

The console can use SEGGER RTT (the ``segger`` module) over the SWS debug
interface instead of the UART:

.. code-block:: cfg

   CONFIG_USE_SEGGER_RTT=y
   CONFIG_RTT_CONSOLE=y
   CONFIG_UART_CONSOLE=n

Read it with ``probe-rs attach --chip TLSR8258 <zephyr.elf>``.

Power Management
================

:zephyr:code-sample:`telink_tlsr8258_pm_timer` suspends the SoC and wakes it
with the 32 kHz timer. Suspend is blocked while the radio is running.

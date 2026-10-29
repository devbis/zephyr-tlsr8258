:orphan:

..
  See
  https://docs.zephyrproject.org/latest/releases/index.html#migration-guides
  for details of what is supposed to go into this document.

.. _migration_4.6:

Migration guide to Zephyr v4.6.0 (Working Draft)
################################################

This document describes the changes required when migrating your application from Zephyr v4.5.0 to
Zephyr v4.6.0.

Any other changes (not directly related to migrating applications) can be found in
the :ref:`release notes<zephyr_4.6>`.

.. contents::
    :local:
    :depth: 2

Common
******

Build System
************

Kernel
******

Boards
******

Telink
======

* The TLSR8258 boards run at a system clock of 24 MHz by default, selected by the SoC at start
  from the ``sys_clk`` devicetree node. Images used to run at 16 MHz after the system timer driver
  had changed the clock. Set ``clock-frequency`` of ``sys_clk`` to 16, 24 or 48 MHz in an overlay
  to choose another frequency.
* The UART baud rate of the TLSR8258 boards is now the configured one. It was 2/3 of it
  (``115200`` came out as ``76800``), because the UART divided 24 MHz while the clock was 16 MHz.
  Adjust a terminal that was set to the old rate.
* The ``clock-frequency`` property is removed from the ``telink,tlsr8258-uart``,
  ``telink,tlsr8258-spi`` and ``telink,tlsr8258-pwm`` bindings. These nodes take the clock from
  their ``clocks`` property, which points at ``sys_clk``.

Device Drivers and Devicetree
*****************************

.. Only place contents common to all device drivers here. Contents specific to one driver subsystem
   goes into its own subsection, below.

.. Group contents in this section by subsystem, e.g.:
..
.. ADC
.. ===
..
.. ...

.. zephyr-keep-sorted-start re(^\w) ignorecase

.. zephyr-keep-sorted-stop

Bluetooth
*********

Networking
**********

Other subsystems
****************

Modules
*******

Architectures
*************

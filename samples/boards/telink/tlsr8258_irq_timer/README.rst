.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: telink_tlsr8258_irq_timer
   :name: TLSR8258 System Timer IRQ Smoke

   Validate TLSR8258 system timer IRQ fire/disable/re-enable behavior.

Overview
********

This sample validates the TLSR8258 system timer IRQ flow in three stages:

1. Confirm timer IRQs fire while enabled.
2. Confirm timer IRQs stop while the IRQ line is disabled.
3. Confirm timer IRQs resume after re-enabling the IRQ line.

The sample updates ``tlsr_irq_marker`` for board-level automation:

* ``0x82580000``: success
* ``0x8258e001``: IRQ did not fire initially
* ``0x8258e002``: IRQ still fired while disabled
* ``0x8258e003``: IRQ did not resume after re-enable

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/telink/tlsr8258_irq_timer
   :board: tlsr8258_generic
   :goals: build flash
   :compact:

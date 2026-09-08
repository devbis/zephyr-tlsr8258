.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

.. zephyr:code-sample:: telink_tlsr8258_pm_timer
   :name: TLSR8258 PM Timer Smoke

   Validate suspend-to-idle entry on TLSR8258 using the RC32K PM backend.

Overview
********

This sample forces ``PM_STATE_SUSPEND_TO_IDLE`` for a timed sleep and checks
that the wake reason reported by the TLSR8258 PM backend is the 32 kHz timer.

The sample updates ``tlsr_pm_marker`` for board-level automation:

* ``0x8258aa00``: success
* ``0x8258e101``: suspend API returned error
* ``0x8258e102``: wake reason was not timer

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/telink/tlsr8258_pm_timer
   :board: tlsr8258_generic
   :goals: build flash
   :compact:

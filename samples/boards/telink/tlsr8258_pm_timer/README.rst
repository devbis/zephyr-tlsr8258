.. zephyr:code-sample:: telink_tlsr8258_pm_timer
   :name: TLSR8258 suspend with timer wakeup

   Suspend a TLSR8258 and wake it with the 32 kHz timer.

Overview
********

This sample suspends the SoC for 100 ms with
``tlsr8258_pm_suspend_for_ms()`` and prints the wakeup reason reported by the
TLSR8258 power management backend.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/boards/telink/tlsr8258_pm_timer
   :board: tlsr8258_generic
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

   TLSR8258 suspend with timer wakeup
   woke up, reason timer

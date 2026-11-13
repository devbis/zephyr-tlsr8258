.. zephyr:code-sample:: zigbee_switch
   :name: Zigbee on/off switch

   A Zigbee end device acting as an on/off switch.

Overview
********

This sample builds a Zigbee end device that exposes an On/Off Switch (0x0000)
on endpoint 1. Pressing ``sw0`` broadcasts On/Off Toggle to all
rx-on-when-idle devices, and ``sw1`` (when the board has it) broadcasts a Move
to Level command that advances by 50 levels per press. No binding is needed.

The sample shares its bootstrap, Basic and Identify clusters, ``led0`` and button
handling with the other device samples in ``../common``, and its commissioning
policy with ``zigbee_shell``. Holding ``sw0`` for 5 seconds resets the device
to factory defaults. It does not implement scenes, color control or poll
control.

Requirements
************

Built for ``tlsr8258_tb03f`` and ``dt_xt_zb1_devkit``.

The ``tlsr8258_tb03f`` build has not been run on hardware. Its board overlay
enables the button on PA7, which shares the SWS debug line; remove the overlay
when a debugger must stay attached.

On ``dt_xt_zb1_devkit`` (BL702) the button connects GPIO9 to the ground. GPIO9
is the only pin of the module that ends the hibernate of the SoC.

Battery powered switch
**********************

``sleepy_deep.conf`` makes the switch a sleepy end device that enters deep sleep
between the parent polls. The firmware restarts on every wakeup: from the timer
for the next poll, or from ``sw0`` when its ``gpio-keys`` node has the
``wakeup-source`` property. A press that wakes the device is reported like any
other press, so the command leaves a few tens of milliseconds after it.

.. code-block:: console

   west build -b dt_xt_zb1_devkit samples/zigbee/switch -- -DEXTRA_CONF_FILE=sleepy_deep.conf

The device does not sleep while ``sw0`` is held.

Building
********

.. code-block:: console

   west build -b tlsr8258_tb03f/tlsr8258 samples/zigbee/switch

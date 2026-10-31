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
to factory defaults. It does not implement scenes, color control, poll control
or a sleepy configuration.

Requirements
************

Only built for ``tlsr8258_tb03f``; it has not been run on hardware. The board
overlay enables the button on PA7, which shares the SWS debug line; remove the
overlay when a debugger must stay attached.

Building
********

.. code-block:: console

   west build -b tlsr8258_tb03f/tlsr8258 samples/zigbee/switch

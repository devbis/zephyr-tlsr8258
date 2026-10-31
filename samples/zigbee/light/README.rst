.. zephyr:code-sample:: zigbee_light
   :name: Zigbee dimmable light

   A Zigbee router acting as a dimmable light.

Overview
********

This sample builds a Zigbee router that exposes a Dimmable Light (0x0101) on
endpoint 1 with the Basic, Identify, Groups, On/Off and Level Control
clusters. ``led0`` is the load: it is lit while the light is on at a level above
zero. The LED has no PWM channel on the supported board, so brightness is
tracked and reported but not rendered.

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

   west build -b tlsr8258_tb03f/tlsr8258 samples/zigbee/light

.. zephyr:code-sample:: zigbee_contact_sensor
   :name: Zigbee contact sensor

   A Zigbee end device acting as an IAS contact sensor.

Overview
********

This sample builds a Zigbee end device that exposes an IAS Zone device (0x0402)
of zone type contact switch on endpoint 1. Pressing ``sw0`` raises ALARM1 and
releasing it clears the alarm; each change is sent to the coordinator in a Zone
Status Change Notification once the zone is enrolled. Enrollment follows the
auto-enroll request method: when a CIE writes IAS_CIE_Address, the sensor sends
an Enroll Request and stores the Zone ID of the Enroll Response.

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

   west build -b tlsr8258_tb03f/tlsr8258 samples/zigbee/contact_sensor

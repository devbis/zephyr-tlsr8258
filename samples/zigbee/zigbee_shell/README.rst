.. zephyr:code-sample:: zigbee_node
   :name: Zigbee end device or router

   Join a Zigbee network as an end device or a router.

Overview
********

This sample builds a Zigbee node with a Basic and an Identify cluster on
endpoint 1. On start it restores its network from flash, or, when it has no
network yet, steers into one that permits joining on channel 11. It then
announces itself and answers the coordinator's interview.

The default configuration is a sleepy end device; ``router.conf`` builds a
router, which also accepts children.

Requirements
************

A board with an IEEE 802.15.4 radio supported by the Zigbee platform layer,
for example ``tlsr8258_tb03f``, ``nrf52840dk/nrf52840``,
``esp32c6_devkitc/esp32c6/hpcore`` or ``esp32h2_devkitm/esp32h2``, and a
Zigbee coordinator with permit join open. The ESP32-C6 and ESP32-H2 board
configurations scan channel 11.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/zigbee/zigbee_shell
   :board: esp32c6_devkitc/esp32c6/hpcore
   :goals: build flash
   :compact:

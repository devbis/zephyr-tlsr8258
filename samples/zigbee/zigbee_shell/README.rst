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
router, which also accepts children. ``coordinator_znp.conf`` builds a
coordinator that a Z-Stack host (Zigbee2MQTT with ``adapter: zstack``, or ZHA)
drives over the UART that the board overlay names as ``zigbee,ncp-uart``; the
ESP32-C6 overlay uses its USB serial/JTAG port. The host forms the network, so
the node does not start commissioning on its own.

``on_off.conf`` adds the On/Off cluster server on endpoint 1 and drives the ``led0`` alias
(tested to build on ``tlsr8258_tb03f`` only). ``ota.conf`` adds the OTA client, see the
``tlsr8258-mcuboot`` snippet.

Requirements
************

A board with an IEEE 802.15.4 radio supported by the Zigbee platform layer,
for example ``tlsr8258_tb03f``, ``nrf52840dk/nrf52840``,
``esp32c6_devkitc/esp32c6/hpcore``, ``esp32h2_devkitm/esp32h2`` or
``slwrb4180a`` (EFR32MG21) or ``weact_stm32wb55_core`` (see below), and a Zigbee
coordinator with permit join open.
The ESP32-C6 and ESP32-H2 board configurations scan channel 11.

WeAct STM32WB55 core board
**************************

The radio of the STM32WB55 is driven by CPU2, which has to run the Thread RCP
firmware of STM32CubeWB (``stm32wb5x_Thread_RCP_fw.bin``, installed with the FUS at
0x080DF000 on the 1 MiB parts). The board configuration puts the console and the shell
on the first USB CDC ACM port and mcumgr on the second one. The coordinator build,
``coordinator_znp.conf`` with ``coordinator_znp_weact_stm32wb55_core.conf`` and
``-DEXTRA_DTC_OVERLAY_FILE=coordinator_znp_weact_stm32wb55_core.overlay``, uses the
first port for the Z-Stack host instead, so the board is a USB coordinator.

The application is built for MCUboot, which has to be built with the same flash map,
the serial recovery over USB and the flash state fix of ``mcuboot/wbfix``:

.. code-block:: console

   west build -b weact_stm32wb55_core bootloader/mcuboot/boot/zephyr -d build-boot -- \
     -DEXTRA_CONF_FILE=$PWD/samples/zigbee/zigbee_shell/mcuboot/weact_stm32wb55_core.conf \
     -DEXTRA_DTC_OVERLAY_FILE=$PWD/samples/zigbee/zigbee_shell/mcuboot/weact_stm32wb55_core.overlay \
     -DZEPHYR_EXTRA_MODULES=$PWD/samples/zigbee/zigbee_shell/mcuboot/wbfix

Flash it at 0x08000000 through the ROM DFU bootloader (BOOT button held while resetting)
and the signed application at 0x08014000. After that the board is updated without the
buttons: the loader waits 5 s for an upload on its USB serial port after every reset, and
the hardware watchdog resets an application that hangs. The images are signed with the
development key of MCUboot. The M4 must keep to the flash below 0x080CE000, see the
flash map of the board overlay.

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/zigbee/zigbee_shell
   :board: esp32c6_devkitc/esp32c6/hpcore
   :goals: build flash
   :compact:

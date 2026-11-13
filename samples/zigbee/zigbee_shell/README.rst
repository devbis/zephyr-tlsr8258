.. zephyr:code-sample:: zigbee_node
   :name: Zigbee end device or router

   Join a Zigbee network as an end device or a router.

Overview
********

This sample builds a Zigbee node with a Basic and an Identify cluster on
endpoint 1. On start it restores its network from flash, or, when it has no
network yet, steers into one that permits joining on channel 11. It then
announces itself and answers the coordinator's interview.

The default configuration builds an end device; it is sleepy when the board
supports and enables the required sleep configuration. ``router.conf`` builds a
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

The nRF52840 end device uses System ON sleep with RAM retained. The kernel
uses RTC1 to wake for timeouts, including parent polls. System OFF cannot be
used for this because it does not support RTC wakeup. Actual power consumption
also depends on the board, connected peripherals, and radio activity.

ESP32-H2 and ESP32-C6 end devices can use timed deep sleep, which restarts the
firmware on every wake. Build with ``sleepy_deep.conf`` and the matching overlay
to enable the SoC's soft-off power state:

.. code-block:: console

   west build -b esp32h2_devkitm/esp32h2 samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE=sleepy_deep.conf \
     -DEXTRA_DTC_OVERLAY_FILE=sleepy_deep_esp32h2.overlay

   west build -b esp32c6_devkitc/esp32c6/hpcore samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE=sleepy_deep.conf \
     -DEXTRA_DTC_OVERLAY_FILE=sleepy_deep_esp32c6.overlay

The board overlays enable the RTC timer and select retained memory for the
Zigbee security frame counter. ESP32-H2 reserves the last 8 bytes of LP SRAM for
the counter record and keeps the adjacent RTC timer data outside the linker
region. The 7-second parent poll interval includes a full boot on each cycle.

nice!nano without SoftDevice
***************************

For a nice!nano whose UF2 bootloader reports ``SoftDevice: not found``, use
``promicro_nosd.overlay`` to place the application at 0x1000. The following
build enables a USB shell on the router:

.. code-block:: console

   west build -b promicro_nrf52840/nrf52840/uf2 samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE="router.conf;promicro_usb_shell.conf" \
     -DEXTRA_DTC_OVERLAY_FILE=promicro_nosd.overlay -DCONFIG_ZIGBEE_STACK_SIZE=4096

The shell command ``uf2`` reboots into the UF2 bootloader for updates over USB.
Copy the resulting ``zephyr/zephyr.uf2`` file to the bootloader's NICENANO disk.
The USB shell configuration is intended for development and power consumption
with USB enabled is not representative of a battery-powered sleepy device.

For a sleepy end device with USB shell and CPU load logging, use:

.. code-block:: console

   west build -b promicro_nrf52840/nrf52840/uf2 samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE="promicro_usb_shell.conf;promicro_ed_usb.conf" \
     -DEXTRA_DTC_OVERLAY_FILE="promicro_nosd.overlay;promicro_ed_usb.overlay"

The log reports CPU load every 10 seconds. The idle fraction does not measure
radio or USB power. The end-device storage is separate from router and coordinator
storage.

To build a coordinator with one USB ZNP port instead, use:

.. code-block:: console

   west build -b promicro_nrf52840/nrf52840/uf2 samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE="coordinator_znp.conf;promicro_znp_usb.conf" \
     -DEXTRA_DTC_OVERLAY_FILE="promicro_nosd.overlay;promicro_znp_usb.overlay"

This build keeps the coordinator's Zigbee storage separate from the router's.
To enter the UF2 bootloader, stop the ZNP host so it releases the serial port,
then run ``python3 promicro_znp_uf2.py /dev/ttyACM0`` from this sample directory.
The script requires pyserial. It sends the board-specific ZNP SYS command 0xF0
with payload ``UF2!``; this is an extension, not a standard ZNP reset command.

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

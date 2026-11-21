.. zephyr:board:: gtw360_shield

Overview
********

The nRF52840 module of the GTW360 gateway. The gateway host talks to the module over UART1
(P0.14 TX, P0.16 RX). A SKY66112 front end is switched by P1.04 (power amplifier) and P1.02
(low noise amplifier), and the module runs from the internal LDO regulator. UART0 (P0.06 RX,
P0.08 TX) is the console. The board has no buttons and no LEDs.

Flash map
*********

The map is that of the MCUboot already programmed on the gateway:

======== ======== ========
Start    Size     Use
======== ======== ========
0x00000  64 KiB   MCUboot
0x10000  448 KiB  Application slot
0x80000  416 KiB  Update slot
0xe8000  32 KiB   Zigbee storage
0xf0000  64 KiB   Swap scratch area
======== ======== ========

The Zigbee storage lies in the last part of the update slot. An image of up to 416 KiB does not
reach it.

Building
********

The Zigbee shell sample builds a router or a Z-Stack compatible coordinator for the board. Use the
MCUboot configuration of the sample to get the signed image the loader accepts:

.. code-block:: console

   west build -b gtw360_shield/nrf52840 samples/zigbee/zigbee_shell -- \
     -DEXTRA_CONF_FILE="coordinator_znp.conf;coordinator_znp_gtw360_shield.conf;gtw360_shield_mcuboot.conf"

The update image is ``zephyr.signed.bin``.

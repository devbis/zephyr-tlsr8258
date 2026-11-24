.. zephyr:board:: pb_03f_kit

Overview
********

The Ai-Thinker PB-03F-Kit is the :zephyr:board:`pb_03f` module on a kit board with an RGB LED, two white
LEDs, a user button and the CH340 USB-UART on UART0 (P9 TX, P10 RX). Everything about the module, its
flash layout, MCUboot and the flashing is in the page of the PB-03F; this board adds the devicetree of the kit.

Hardware
********

========== ====== ========== ========================================================
Function   Pad    GPIO index Devicetree
========== ====== ========== ========================================================
Red LED    P7     4          ``led0``, active high
Green LED  P11    7          ``led1``, active high
Blue LED   P18    12         ``led2``, active high
Cool white P34    22         ``led3``, active high
Warm white P0     0          ``led4``, active high
User button P15   9          ``sw0``, active high, pull-down of the chip
========== ====== ========== ========================================================

The button joins the pad to 3.3 V and the board has no pull-down. Without the pull of the chip the pad
floats: it picks up the mains and stays high after a release, so a ``gpio-keys`` input reports a button held
down. The devicetree names the pads with ``PHY62X2_Pn`` (``dt-bindings/gpio/phy62x2-gpio.h``): the GPIO controller
counts hardware indices, not the pad numbers of the module.

Programming and debugging
*************************

.. zephyr-app-commands::
   :zephyr-app: samples/basic/button
   :board: pb_03f_kit
   :goals: build

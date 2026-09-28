.. zephyr:board:: tlsr8258_tb03f

Overview
********

The Telink ``tlsr8258_tb03f`` board target covers TLSR8258-based TB03F
hardware using the TC32 core.

It extends ``tlsr8258_generic`` with TB03F-specific GPIO naming for the
discrete white/yellow LEDs, the RGB LED lines, and the default UART routing.
Like the generic TLSR8258 target, it boots Zephyr directly from flash address ``0x0``.

Hardware
********

Board-local signals currently described in the DTS:

- white LED on ``PB5``
- yellow LED on ``PB4``
- RGB red on ``PC2``
- RGB green on ``PC3``
- RGB blue on ``PC4``
- UART0 TX on ``PB1``
- UART0 RX on ``PA0``

Button / SWS Caveat
===================

The physical button is on ``PA7``, but that line is intentionally not enabled
in the base DTS. On TB03F it collides with SWS debug usage, and enabling it by
default breaks ``TlsrPgm.py`` / ``probe-rs`` attach.

The DTS keeps the button definition only as a commented, disabled example.

Programming and Debugging
*************************

.. zephyr:board-supported-runners::

Building, flashing and the RTT console work as described for
:zephyr:board:`tlsr8258_generic`, with ``-b tlsr8258_tb03f``. The flash
layout is the same.

The RGB LED is driven as three GPIOs; there is no PWM support, and the
``PA7`` button stays disabled as described above.

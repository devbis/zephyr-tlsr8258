.. zephyr:board:: tlsr8258_tb03f

Overview
********

The Telink ``tlsr8258_tb03f`` board target covers TLSR8258-based TB03F
hardware using the TC32 core.

This board definition is derived from the local TLSR8258 bring-up target, but
adds TB03F-specific GPIO naming for the discrete white/yellow LEDs, the RGB
LED lines, and the default UART routing.

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

Building
********

Build with the local LLVM TC32 toolchain:

.. code-block:: console

   ZEPHYR_BASE=$PWD/zephyr cmake \
     -S zephyr/samples/hello_world \
     -B /tmp/zephyr-tc32-tb03f \
     -GNinja \
     -DBOARD=tlsr8258_tb03f \
     -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm \
     -DLLVM_TOOLCHAIN_PATH=$PWD/toolchains/tc32-stage2/llvm \
     -DPython3_EXECUTABLE=$PWD/.venv-zephyr/bin/python \
     -DUSER_CACHE_DIR=/tmp/zephyr-tc32-cache
   cmake --build /tmp/zephyr-tc32-tb03f -v

Current Scope
*************

This board target currently provides:

- TLSR8258 memory/flash wiring
- TB03F-specific LED aliases
- TB03F RGB GPIO aliases
- default UART console routing

It does not yet add board-specific PWM policy for the RGB LED or enable the
``PA7`` button by default.

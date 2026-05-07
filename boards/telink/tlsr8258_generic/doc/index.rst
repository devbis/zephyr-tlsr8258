.. zephyr:board:: tlsr8258_generic

Overview
********

The TLSR8258 Generic board target is a bring-up target for Telink TLSR8258
SoCs using the TC32 core.

This target is currently intended for low-level port validation. It uses the
local LLVM-based TC32 toolchain and ``TlsrPgm.py`` for flashing and hardware
inspection.

Hardware
********

TLSR8258 integrates a TC32 MCU core, 64 KB SRAM, on-chip flash mapping, GPIO,
system timer, UART, and 2.4 GHz radio blocks. The current Zephyr port validates
the core boot path, interrupt dispatch, GPIO, system timer, and polling UART
console.

Supported Features
==================

.. list-table::
   :header-rows: 1

   * - Feature
     - Status
   * - TC32 boot
     - supported
   * - System timer
     - supported
   * - IRQ dispatch
     - supported
   * - GPIO
     - supported
   * - UART console
     - polling console supported
   * - Flash driver
     - not implemented
   * - Power management
     - not implemented
   * - Radio
     - not implemented

Default Configuration
=====================

The default board configuration enables:

- 24 MHz CPU clock
- TLSR8258 system timer
- GPIO port A
- UART0 polling console at 115200 baud
- UART0 TX on PA2

Building
********

Build with the local LLVM TC32 toolchain:

.. code-block:: console

   ZEPHYR_BASE=$PWD/zephyr cmake \
     -S zephyr/samples/hello_world \
     -B /tmp/zephyr-tc32-uart \
     -GNinja \
     -DBOARD=tlsr8258_generic \
     -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm \
     -DLLVM_TOOLCHAIN_PATH=$PWD/toolchains/tc32-stage2/llvm \
     -DPython3_EXECUTABLE=$PWD/.venv-zephyr/bin/python \
     -DUSER_CACHE_DIR=/tmp/zephyr-tc32-cache
   cmake --build /tmp/zephyr-tc32-uart

Flashing
********

Flash with ``TlsrPgm.py``. For a TCP-connected SWS probe:

.. code-block:: console

   python3 TlsrPgm.py \
     -p tcp://192.168.70.44:55555 \
     -d 20 \
     -t 500 \
     -a 500 \
     -s we 0 /tmp/zephyr-tc32-uart/zephyr/zephyr.bin

If the command uses ``-s``, the CPU is left halted. Resume or reboot explicitly:

.. code-block:: console

   python3 TlsrPgm.py -p tcp://192.168.70.44:55555 -d 20 -r

Debug Inspection
****************

RAM markers used by board-local smoke samples can be inspected with ``ds``:

.. code-block:: console

   python3 TlsrPgm.py -p tcp://192.168.70.44:55555 -d 20 ds <address> <size>

Useful low-level checks:

- UART0 registers: ``ds 0x800090 0x10``
- PA mux register: ``ds 0x8005a8 0x08``
- Breakpoint on UART output: ``bkp <tlsr8258_uart_poll_out address>``

Known Limitations
*****************

.. list-table::
   :header-rows: 1

   * - Area
     - Limitation
   * - UART
     - polling console only; interrupt-driven UART is not implemented
   * - GPIO IRQ
     - core GPIO IRQ is supported; RISC0/RISC1 are smoke-tested separately
   * - Flash
     - no Zephyr flash driver yet
   * - Power management
     - sleep and retention modes are not validated
   * - Debug
     - use ``TlsrPgm.py`` directly; no Zephyr runner integration yet

.. zephyr:board:: tlsr8258_generic

Overview
********

The TLSR8258 Generic board target is a bring-up target for Telink TLSR8258
SoCs using the TC32 core.

This target is currently intended for low-level port validation. It uses the
local LLVM-based TC32 toolchain and ``TlsrPgm.py`` for flashing and hardware
inspection. The board boots Zephyr directly from flash address ``0x0``; the
TLSR8258 boot SRAM mirror is only used for the minimal early startup prefix.

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
   * - RTT console
     - probe-rs-compatible bring-up console supported
   * - Flash driver
     - not implemented
   * - Power management
     - experimental explicit suspend-to-idle via RC32K timer wake
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
- direct application boot from flash offset ``0x0``

Flash Layout
============

The default flash partition layout is:

- application image at ``0x0`` with size ``0x7e000``
- NVS storage at ``0x7e000`` with size ``0x2000``

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

RTT Console
***********

The board also has a TLSR8258-specific RTT-compatible RAM console for SWS
debugging with the local ``probe-rs`` tree. It exposes the standard
``_SEGGER_RTT`` control block in SRAM, but does not require the external SEGGER
module sources.

Build the RTT smoke sample:

.. code-block:: console

   ZEPHYR_BASE=$PWD/zephyr cmake \
     -S zephyr/samples/boards/telink/tlsr8258_rtt_console \
     -B /tmp/zephyr-tc32-rtt \
     -GNinja \
     -DBOARD=tlsr8258_generic \
     -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm \
     -DLLVM_TOOLCHAIN_PATH=$PWD/toolchains/tc32-stage2/llvm \
     -DPython3_EXECUTABLE=$PWD/.venv-zephyr/bin/python \
     -DUSER_CACHE_DIR=/tmp/zephyr-tc32-cache
   cmake --build /tmp/zephyr-tc32-rtt -v

Attach to a running image with the ELF so ``probe-rs`` can use the exact RTT
control block address:

.. code-block:: console

   probe-rs/target/debug/probe-rs attach \
     --chip TLSR8258 \
     --probe sws:tcp://192.168.70.44:55555 \
     /tmp/zephyr-tc32-rtt/zephyr/zephyr.elf

Without an ELF, scan the TLSR8258 SRAM range:

.. code-block:: console

   probe-rs/target/debug/probe-rs attach \
     --chip TLSR8258 \
     --probe sws:tcp://192.168.70.44:55555 \
     --scan-region 0x00840000..0x00850000

Power Management Smoke
**********************

Build the suspend-to-idle smoke sample:

.. code-block:: console

   ZEPHYR_BASE=$PWD/zephyr cmake \
     -S zephyr/samples/boards/telink/tlsr8258_pm_timer \
     -B /tmp/zephyr-tc32-pm-timer \
     -GNinja \
     -DBOARD=tlsr8258_generic \
     -DZEPHYR_TOOLCHAIN_VARIANT=host/llvm \
     -DLLVM_TOOLCHAIN_PATH=$PWD/toolchains/tc32-stage2/llvm \
     -DPython3_EXECUTABLE=$PWD/.venv-zephyr/bin/python \
     -DUSER_CACHE_DIR=/tmp/zephyr-tc32-cache
   cmake --build /tmp/zephyr-tc32-pm-timer -v

The sample explicitly requests suspend-to-idle, wakes by the 32 kHz timer,
and leaves a RAM marker for post-run inspection:

- ``0x8258aa00``: timer wakeup path completed
- ``0x8258e101``: suspend API returned an error
- ``0x8258e102``: wakeup reason was not timer

Flashing
********

The board uses ``probe-rs`` as the default west flash runner. Flash with a
Telink SWS probe selected through ``--dev-id``:

.. code-block:: console

   west flash -d /tmp/zephyr-tc32-uart \
     --dev-id 'sws:tcp://192.168.70.44:55555'

Add ``--erase`` for a full chip erase before programming, or ``--reset`` to
issue a final target reset after flashing.

The explicit ``tlsrpgm`` runner remains available when needed:

.. code-block:: console

   west flash -d /tmp/zephyr-tc32-uart --runner tlsrpgm -- \
     --probe tcp://192.168.70.44:55555 \
     --python $PWD/.venv-zephyr/bin/python

Manual ``TlsrPgm.py`` flashing also remains available:

.. code-block:: console

   $PWD/.venv-zephyr/bin/python TlsrPgm.py \
     -p tcp://192.168.70.44:55555 \
     -d 20 \
     -t 500 \
     -a 500 \
     -s we 0 /tmp/zephyr-tc32-uart/zephyr/zephyr.bin

If the command uses ``-s``, the CPU is left halted. Resume or reboot explicitly:

.. code-block:: console

   $PWD/.venv-zephyr/bin/python TlsrPgm.py -p tcp://192.168.70.44:55555 -d 20 -r

Debug Inspection
****************

RAM markers used by board-local smoke samples can be inspected with ``ds``:

.. code-block:: console

   $PWD/.venv-zephyr/bin/python TlsrPgm.py -p tcp://192.168.70.44:55555 -d 20 ds <address> <size>

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
     - explicit suspend-to-idle is experimental; automatic idle PM, deep retention, and shutdown are not wired yet
   * - Debug
     - ``west flash`` defaults to ``probe-rs``; the SWS probe selector must still be supplied explicitly

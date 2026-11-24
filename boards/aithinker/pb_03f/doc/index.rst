.. SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
.. SPDX-License-Identifier: Apache-2.0

Ai-Thinker PB-03F
#################

The PB-03F module contains a Phyplus PHY62x2 Cortex-M0 SoC with 64 KiB SRAM
and 512 KiB flash (the boot ROM reports flash ID ``1340c4``, revision ``6222M005``). This port is experimental and has not been tested on a
module. It supports the Cortex-M system timer, two hardware counters, GPIO,
a UART console on P9 (TX) and P10 (RX), and an experimental IEEE 802.15.4
radio driver. The radio setup is adapted from the PHY62x2 backend in
``zigbee-rs`` at commit ``76d184caa4756a69eb17ee4fd74244cf6ed62806``. It
supports channel selection, TX, RX, CCA, software address filtering and
waiting for TX acknowledgements. Automatic ACK generation on received frames
is not implemented. Radio timing and operation have not been verified on
PHY62x2 hardware.

The ROM clock entry points and the 16 MHz crystal setting follow the
PHY62XX SDK 3.1.1. Their behavior, the peripheral clock and the UART pins need
hardware verification. The ROM interrupt dispatch table occupies the first
1 KiB of SRAM; additional SRAM below ``0x1fff1838`` is reserved for ROM.

The GPIO controller uses hardware indices rather than the pad numbers printed
in the PHY62x2 documentation. Indices 0 through 22 map respectively to pads
P0, P1, P2, P3, P7, P9, P10, P11, P14, P15, P16, P17, P18, P20, P23, P24,
P25, P26, P27, P31, P32, P33 and P34. Indices 5 and 6 are reserved for the
console, 10 and 11 for the crystal, and 1 and 16 through 18 for pads not listed
for PHY62x2. The module does not necessarily expose every pad. The driver
supports input, output, pulls and edge or level interrupts; open-drain output is not supported.
The controller has one polarity per pin, so an interrupt on both edges flips the polarity after
every edge in the interrupt handler. An edge that comes within a few instructions of the handler
reading the pin level can be merged with the previous one; read the pin in the callback instead of
counting callbacks. Level interrupts cannot use both edges. Wakeup from sleep through the pins
is not implemented.

The optional ``CONFIG_FLASH_PHY62X2`` driver uses ROM flash functions and
executes its cache-bypass path from SRAM. The ``storage_partition`` occupies
the last four 4 KiB sectors. Program and erase lock interrupts while the flash
is busy, which can delay interrupts and affect timekeeping. The ROM calls,
cache restoration, flash protection and reset recovery have not been tested on
hardware; enable the driver only for development and validate it on the device.
The driver waits for the controller and flash to become idle before returning
to XIP code; a hardware failure can therefore leave it waiting indefinitely.
Program and erase operations check the resulting flash contents.

I2C
***

``i2c0`` and ``i2c1`` are the DesignWare I2C controllers, served by the generic ``i2c_dw`` driver, and
are disabled by default. Give the pads in a pinctrl state with the ``PHY62X2_FMUX_I2C0_SCL`` and
``PHY62X2_FMUX_I2C0_SDA`` functions (``I2C1`` for the second controller), with the pull-up. The
controllers run from the core clock, which ``CONFIG_I2C_DW_CLOCK_SPEED`` follows (16 or 48 MHz); the
SDK sets the timing counts of the clock divider by hand, and the driver computes them. The
controllers have not been tried on this module.

.. code-block:: devicetree

   &pinctrl {
           i2c0_scl: i2c0_scl {
                   pinmux = <PHY62X2_PINMUX(8, PHY62X2_FMUX_I2C0_SCL)>;
                   bias-pull-up;
           };

           i2c0_sda: i2c0_sda {
                   pinmux = <PHY62X2_PINMUX(9, PHY62X2_FMUX_I2C0_SDA)>;
                   bias-pull-up;
           };
   };

   &i2c0 {
           pinctrl-0 = <&i2c0_scl &i2c0_sda>;
           pinctrl-names = "default";
           status = "okay";
   };

SPI
***

``spi0`` and ``spi1`` are the DesignWare SPI controllers, served by the generic ``spi_dw`` driver, and
are disabled by default. Give the pads in a pinctrl state with the ``PHY62X2_FMUX_SPI0_SCK``,
``_SSN``, ``_TX`` and ``_RX`` functions (``SPI1`` for the second controller). The start-up code
selects the master mode of an enabled controller in ``AP_COM->PERI_MASTER_SELECT`` unless the node has
``serial-target``. The clock is the core clock. The depth of the FIFOs is not in the SDK, 8 is
assumed from its interrupt threshold of 4; the real depth is in the component parameter register of the
controller. The controllers have not been tried on this module.

RTC counter
***********

``rtc0`` is the always-on RTC, a free-running 32 kHz counter served by the ``counter`` API, with
``clock-frequency`` and ``prescaler`` in its node. It counts while the chip sleeps. The driver follows
the ``rtc_*`` functions of the boot ROM for the run bit, the clear bit and the prescaler, which were
read from a dump of the ROM. The comparators and the interrupt of the RTC are not used and there are no
alarms: no SDK and no ROM function handles them, only ``config_RTC`` of the ROM arms the first
comparator to wake the chip, which the power management does. The width of the counter, 24 bits, is
assumed and has not been measured.

DMA
***

``dma0`` is the DMA controller, disabled by default, with its own driver: Zephyr's DesignWare driver
sets the handshake interfaces in the layout of the Intel variant, and this controller has the
classic one. Four channels move one block per transfer, memory to memory or between memory and a
peripheral; the slots are the ``PHY62X2_DMA_SLOT_*`` of ``dt-bindings/dma/phy62x2-dma.h``. The
channel 0 moves up to 2047 transfers of the source width, the others up to 31. Linked blocks, the
transfers between two peripherals and the handling of the flash as a destination, which the SDK
does by lifting the write protection and the cache, are not supported. The peripheral drivers of
the tree do not use the DMA. It has not been tried on this module.

Quadrature decoder
******************

``qdec0`` is the quadrature decoder, disabled by default, with the sensor driver
``phyplus,phy62x2-qdec``. It uses one of the channels X, Y and Z (``channel``) and reports
``SENSOR_CHAN_ROTATION``, the rotation since the last fetch, from ``steps`` and
``quadrature-mode``. Give the pads in a pinctrl state with the ``PHY62X2_FMUX_QDEC_CHAX`` and
``_CHBX`` functions (``Y`` and ``Z`` for the other channels), with a pull-down as the SDK sets. The
driver polls the counter and clears it with the interrupt flag, as the interrupt handler of the SDK
does; the index input, the interrupts and the wakeup from sleep are not supported. It has not been
tried on this module.

PWM
***

The six PWM channels are in ``pwm0``, disabled by default. Enable it in an overlay and give the pads
of the channels in a pinctrl state, with the ``PHY62X2_FMUX_PWM0`` to ``PHY62X2_FMUX_PWM5``
functions. The counters run from an assumed 16 MHz clock, divided by 2 to the power of
``prescaler``; the SDK does not give the clock and the period of a channel has not been measured.
All channels share the prescaler.

.. code-block:: devicetree

   &pinctrl {
           pwm_ch2: pwm_ch2 {
                   pinmux = <PHY62X2_PINMUX(7, PHY62X2_FMUX_PWM2)>;
           };
   };

   &pwm0 {
           pinctrl-0 = <&pwm_ch2>;
           pinctrl-names = "default";
           status = "okay";
   };

ADC
***

``adc0`` is disabled by default. The inputs are numbered as in the SDK: the channels 2 to 7 are the
single-ended inputs on P11, P23, P24, P14, P15 and P20, and the pairs 1 (P18, P25), 3, 5 and 7 are
differential. The driver gives 12 bits of the inputs below 1 V, the internal reference is taken as
800 mV, and a differential reading is signed around zero. The values are the raw average of 29
hardware samples; the SDK corrects them with the calibration words in the flash at ``0x1001000`` and
with a constant per input, which the driver does not. It polls for the end of a conversion, reads one
differential pair at a time, and has not been tried on this module.

Power off and the reset cause
*****************************

``sys_poweroff()`` (``CONFIG_POWEROFF``) powers the chip off as ``hal_pwrmgr_poweroff()`` of the SDK
does: the RAM is not retained and the chip starts from the boot ROM when a pad changes its level. The
pads are the ``gpios`` of an enabled ``phyplus,phy62x2-poweroff-wakeup`` node: a pad that is active
low wakes the chip on the falling edge and gets the strong pull-up, a pad that is active high wakes it
on the rising edge and gets the pull-down. The pads are given as indices of the GPIO controller.

.. code-block:: devicetree

   / {
           poweroff-wakeup {
                   compatible = "phyplus,phy62x2-poweroff-wakeup";
                   gpios = <&gpio0 8 GPIO_ACTIVE_LOW>;
           };
   };

``hwinfo_get_reset_cause()`` reads ``g_system_reset_cause``, the variable in which the boot ROM keeps
the cause of the start: the ROM takes it from ``AON->SLEEP_R[0]`` and writes 1 back into the register,
so the register itself is of no use after the start. A cold start is ``RESET_POR``, a warm start is
``RESET_SOFTWARE``, and the power off and the sleep are ``RESET_LOW_POWER_WAKE``. Zephyr's reboot
writes the cold start into the register, so that the boot ROM opens its loader window, and a software
reboot is therefore reported as ``RESET_POR``; after a cold start the ROM leaves the variable as it
was, so a stale value can survive in the RAM. The values were read from the boot ROM; none of this has
been tried on the module.

Deep sleep with retained RAM
****************************

``CONFIG_PHY62X2_DEEP_SLEEP=y`` provides ``phy62x2_deep_sleep(timeout_ms)`` (``soc.h``). It follows
``hal_pwrmgr_enter_sleep_rtc_reset()`` of the SDK: the RTC is configured through the ROM
(``config_RTC``), the chip is put to sleep through the ROM (``enter_sleep_off_mode``) and starts from
the boot ROM when the timeout ends or a wake pad changes its level, as after a reset; the registers and
the RAM that is not retained are lost. The SDK clears the retention of the SRAM there; this function
keeps the blocks of the ``zephyr,retained-ram`` nodes, so that the ``retained_mem`` driver keeps its
data across the sleep. The retention is in blocks of the SRAM: 32 KiB at ``0x1fff0000`` and 16 KiB each
at ``0x1fff8000`` and ``0x1fffc000``, and a region keeps every block that it touches. The wake pads
are those of the ``phyplus,phy62x2-poweroff-wakeup`` nodes and the pads that have an interrupt
enabled in the GPIO controller, on the edge of the interrupt. Flush the console before the call.

.. code-block:: devicetree

   &sram0 {
           reg = <0x1fff1840 0xe3c0>;
   };

   / {
           sram_retained: memory@1ffffc00 {
                   compatible = "zephyr,memory-region", "mmio-sram";
                   reg = <0x1ffffc00 0x400>;
                   zephyr,memory-region = "RetainedMem";

                   retained_ram: retainedmem {
                           compatible = "zephyr,retained-ram";
                           #address-cells = <1>;
                           #size-cells = <1>;

                           retention0: retention@0 {
                                   compatible = "zephyr,retention";
                                   reg = <0x0 0x100>;
                           };
                   };
           };
   };

Only the code is written; nothing of it has been run on the module. It is not known whether the boot
ROM leaves the retained RAM alone, which reset cause it expects (the function writes the one of the
SDK's RTC reset, 4, which the ROM turns into the cause 5 and skips its loader window), whether the flash needs its deep sleep (``spif_set_deep_sleep`` of the ROM, which
the SDK uses under ``CFG_FLASH_ENABLE_DEEP_SLEEP``) and what the wake pads do to the pull-ups of the
boot ROM pins. The sleep does not return and does not resume: it is a reset that keeps some RAM, not
the sleep of the SDK that resumes the program.

Suspend to RAM
**************

``CONFIG_PHY62X2_SUSPEND=y`` provides ``phy62x2_suspend(rtc_ticks)`` (``soc.h``): the chip sleeps with
all of its SRAM retained until the RTC reaches the time or a wake pad changes its level, and then the
function returns and the program goes on. The sequence comes from a dump of the boot ROM and from the
SDK. The program saves its registers like ``setjmp()``, arms the wake pads and the first comparator of
the RTC, installs a function in the slot 62 of the jump table (``WAKEUP_PROCESS``), which the boot ROM
runs instead of the wakeup of the BLE controller when the chip wakes with the RAM kept, and enters the
sleep through ``enter_sleep_off_mode()`` of the ROM. The function in the slot, which runs from the RAM,
selects the core clock again, sets the clock gates, the cache of the flash, the NVIC and the SysTick as
they were, and returns to ``phy62x2_suspend()`` with the registers of the program restored.

The registers of the other peripherals are lost in the sleep: the UART, the GPIO, the radio and the
rest have to be set up again by their drivers, and none of them does it yet. The pull of the pads, the
retention of the outputs, the flash in its deep sleep and the time that the kernel clock loses are not
handled either. Nothing of this has been run on the module.

Power management
****************

``CONFIG_PHY62X2_PM=y`` puts the suspend to RAM under the power management of Zephyr: the state
``suspend-to-ram`` of the CPU node, with ``pm_state_set()`` calling ``phy62x2_suspend()``. It needs
``CONFIG_PM``, ``CONFIG_PM_DEVICE``, ``CONFIG_TICKLESS_KERNEL`` and
``CONFIG_SYSTEM_TIMER_LPM_COMPANION_HOOKS=y``: the hooks arm the wake time on the RTC, which keeps
counting during the sleep, and give the elapsed time back to the kernel clock, and the SysTick, which
stops with the core, is restarted by the kernel (``CONFIG_SYSTEM_TIMER_RESET_BY_LPM``). The sleep is
limited to half of the watchdog timeout when ``CONFIG_PHY62X2_WATCHDOG`` is on, and to half of the range
of the RTC. ``min-residency-us`` and ``exit-latency-us`` of the state are guesses.

The devices that get power management actions are the GPIO controller, which keeps its registers, the
radio, which is set up again as at the start and refuses to sleep while it is started, and the UART,
which sets its pins and registers again (``CONFIG_UART_NS16550_PM_RECONFIGURE``) with the FIFO of the
console enabled afterwards. The rest, the I2C, SPI, PWM, ADC, QDEC and DMA, have none and lose their
registers in the sleep. Nothing of the power management has been run on the module; the wake path of the
boot ROM was read from a dump, not observed.

Pins
****

The function of a pad comes from the ``pinctrl`` node. A state lists nodes with the pad, as its
index in the GPIO controller, the function and the pull; ``PHY62X2_FMUX_NONE`` leaves a pad a GPIO
and sets only the pull. The console is UART0 on P9 and P10, the pins of the boot ROM loader.

Watchdog
********

The watchdog of the chip is the DesignWare one on the 32 kHz RC oscillator, as on the ECB02C.
``CONFIG_WATCHDOG=y`` enables it, and ``CONFIG_PHY62X2_WATCHDOG`` then starts it and feeds it from
a thread of the lowest priority, so a hang that stops the scheduler resets the chip. It is off by
default and has not been tried on this module.

Flash layout
************

========== ========= ==========================================================
Offset     Size      Contents
========== ========= ==========================================================
0x002000   0x400     Segment table of the boot ROM
0x010100   0x7f00    MCUboot, XIP from 0x11010100
0x018000   0x32000   Slot 0, the running application (MCUboot header of 0x200 bytes)
0x04a000   0x32000   Slot 1, the new image received by serial recovery or OTA
0x07c000   0x4000    ``storage_partition``
========== ========= ==========================================================

MCUboot swaps the slots with ``CONFIG_BOOT_SWAP_USING_MOVE``, which needs no scratch area. The
loader sits in the segment table of the boot ROM and is the image written at the flash offset
``0x2000``. An image XIP below ``0x10000`` does not start. The layout is the one of
``boot/zephyr/boards/pb_03f.overlay`` in the ``mcuboot`` module; change both together. The
512 KiB layout has not been tried on a module: the ECB02C, with the same loader geometry and one
slot, has.

Build
*****

For example, to build :zephyr:code-sample:`hello_world` as a plain image for the boot ROM:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: pb_03f
   :goals: build

The build produces ``zephyr.phy62x2.bin``. Its first byte corresponds to flash offset ``0x2000``,
with the ROM segment table at that offset, application XIP code beginning at ``0x10100``, load
records for initialized SRAM data, and an SRAM startup descriptor. The image is limited to flash
offsets below ``0x7c000``, where the storage partition begins. It does not include flash contents
below ``0x2000``.

MCUboot
*******

Build the loader from the ``mcuboot`` module, and the application for slot 0 with
``pb_03f_mcuboot.conf`` and ``pb_03f_mcuboot.overlay`` of the ``zigbee_shell`` sample:

.. code-block:: console

   west build -b pb_03f -d build-boot ../bootloader/mcuboot/boot/zephyr
   west build -b pb_03f -d build-app samples/zigbee/zigbee_shell -- \
       -DEXTRA_CONF_FILE="router.conf;pb_03f_mcuboot.conf" \
       -DEXTRA_DTC_OVERLAY_FILE=$ZEPHYR_BASE/samples/zigbee/zigbee_shell/pb_03f_mcuboot.overlay

``boards/chinalctech/ecb02c/support/merge_loader.py --slot0-size 0x32000`` joins
``build-boot/zephyr/zephyr.phy62x2.bin`` and ``build-app/zephyr/zephyr.signed.bin`` into one image
for the first flashing at ``0x11002000``. The loader waits 3 s for a serial recovery command on
UART0 (P9/P10) after each reset, then starts slot 0, or the new image if slot 1 holds one.

The serial utility `rdwr_phy62x2.py`_ can write a binary at a specified flash
offset. The command form is:

.. code-block:: console

   python3 rdwr_phy62x2.py --port <port> we 0x2000 zephyr.phy62x2.bin

The ``we`` operation erases and replaces the sectors it covers, including any
existing firmware and settings. The UART voltage, boot-mode wiring and recovery
procedure for a specific board must be verified before flashing.

.. _rdwr_phy62x2.py: https://github.com/pvvx/PHY62x2/blob/master/Utils/rdwr_phy62x2.py

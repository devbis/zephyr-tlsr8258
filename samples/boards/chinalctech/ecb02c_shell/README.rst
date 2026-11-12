.. zephyr:code-sample:: chinalctech_ecb02c_shell
   :name: ECB02C shell for MCUboot

   A shell on the console UART of the ECB02C, an application for MCUboot with a single slot.

Overview
********

The sample is the application that is started by MCUboot (see the ``ecb02c.conf`` of MCUboot) and
keeps a shell on the console UART. ``kernel reboot cold`` reboots the chip into MCUboot, whose
serial recovery takes a new image, or into the window of the boot ROM in which a flash tool can
connect, see the flashing scripts of the board. ``romdump <address> <length>`` prints memory, for
example the boot ROM at ``0x0``, and ``hang [lock]`` hangs the system to try the watchdog: it resets
the chip only with ``lock``, because a thread of the same priority as the feeder of the watchdog
still gets its time slices.

Building and Running
********************

The application is signed for MCUboot and written with the serial recovery of MCUboot:

.. zephyr-app-commands::
   :zephyr-app: samples/boards/chinalctech/ecb02c_shell
   :board: ecb02c
   :goals: build
   :compact:

The signed image is ``zephyr.signed.bin`` in the build directory.

Sample Output
=============

.. code-block:: console

   ECB02C shell: kernel reboot cold, romdump, hang
   uart:~$

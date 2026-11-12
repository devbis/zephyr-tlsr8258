.. zephyr:code-sample:: chinalctech_ecb02c_loader_update
   :name: ECB02C loader update

   Rewrite the MCUboot loader of the ECB02C from an application that MCUboot starts.

Overview
********

MCUboot of the ECB02C replaces only the application, the serial recovery and the slot can not hold a
new loader. This application carries a new loader as data. At the start it compares the sectors of
the loader in the flash with its own copy, writes those that differ and reads them back, then
reboots. The sector with the table of the boot ROM, at the flash offset ``0x2000``, is written last.
When the loader is the same the application does nothing, so it can stay in the slot until a normal
application replaces it. It has the shell of :zephyr:code-sample:`chinalctech_ecb02c_shell`, ``kernel
reboot cold`` returns to MCUboot.

If the power fails while the loader is written the chip has no loader. It can be written again
through the boot ROM on P9/P10, see the board documentation.

Building and Running
********************

Build MCUboot for the board first, then the sample with the image of that loader:

.. code-block:: console

   west build -b ecb02c -d build-loader bootloader/mcuboot/boot/zephyr
   west build -b ecb02c -d build-update samples/boards/chinalctech/ecb02c_loader_update -- \
       -DLOADER_IMAGE=build-loader/zephyr/zephyr.phy6252.bin

Upload ``build-update/zephyr/zephyr.signed.bin`` through the serial recovery of MCUboot, see the
flashing scripts of the board.

Sample Output
=============

.. code-block:: console

   loader update: 1 of 7 sectors differ, writing
   loader update: 0x10000 done
   ECB02C loader update: loader_update() = 1
   the new loader is in the flash, rebooting
   loader update: the loader is up to date (7 sectors)
   ECB02C loader update: loader_update() = 0

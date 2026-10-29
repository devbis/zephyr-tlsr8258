.. _snippet-tlsr8258-mcuboot:

TLSR8258 MCUboot slot (tlsr8258-mcuboot)
########################################

.. code-block:: console

   west build -b tlsr8258_tb03f/tlsr8258 -S tlsr8258-mcuboot samples/zigbee/zigbee_shell

Overview
********

Builds an application for slot 0 of the flash layout the MCUboot port for the
TLSR8258 uses. The loader is at flash address ``0x0``, where the boot ROM looks
for the ``KNLT`` boot mark, and the application is linked for its slot: the image
head (the vectors and the code the loader copies to SRAM) at address 0, the rest at
its flash address behind the MCUboot header (see ``CONFIG_BOOTLOADER_MCUBOOT`` in
``include/zephyr/arch/tc32/linker.ld``).

The snippet selects ``slot0_partition`` as the code partition and sets
``CONFIG_BOOTLOADER_MCUBOOT`` and ``CONFIG_USE_DT_CODE_PARTITION``. The layouts are:

.. list-table::
   :header-rows: 1

   * - Board
     - MCUboot
     - Slot 0
     - Slot 1
   * - ``tlsr8258_tb03f``, ``tlsr8258_generic`` (512 KB)
     - ``0x00000``-``0x07fff``
     - ``0x08000``-``0x3efff``
     - ``0x3f000``-``0x75fff``
   * - ``tlsr8258_generic_1m``
     - ``0x00000``-``0x07fff``
     - ``0x08000``-``0x80fff``
     - ``0x81000``-``0xf9fff``

The IEEE address and Zigbee NV partitions of the board are unchanged. The same
layout is in the ``mcuboot`` module, in ``boot/zephyr/boards``; change both together.

Signing
*******

The loader checks the image hash, there is no signature. Sign the built image with
``imgtool`` from the ``mcuboot`` module (``pip install -r
bootloader/mcuboot/scripts/requirements.txt``):

.. code-block:: console

   python bootloader/mcuboot/scripts/imgtool.py sign --header-size 0x200 \
       --pad-header --align 4 --slot-size 0x37000 --version 0.0.1 \
       build/zephyr/zephyr.bin signed.bin

The slot size is ``0x37000`` for the 512 KB boards and ``0x79000`` for the 1 MB one.

Loading
*******

The loader receives the image over UART0 (TX on PB1, RX on PA0) with mcumgr serial
recovery, after a reset: it listens for a command for 200 ms before it checks the
application. The image has to be uploaded to the primary slot, which is where serial
recovery writes.

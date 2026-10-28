.. zephyr:code-sample:: ieee802154-sniffer
   :name: IEEE 802.15.4 sniffer

   Print the frames of one IEEE 802.15.4 channel on the console UART.

Overview
********

The application puts the radio into promiscuous mode and prints every received frame as one line::

   received: <hex PSDU without FCS> power: <dBm> lqi: <0..255> time: <us>

The shell understands the commands of the nRF 802.15.4 sniffer, so ``nrf_zigbee_sniffer.py`` works
with it unchanged:

* ``channel <11..26>`` selects the channel, 11 until then
* ``receive`` starts the capture
* ``sleep`` stops it

``kernel reboot cold`` restarts the board, which lets a flashing script reach the bootloader
without a power cycle.

Building and running
********************

.. code-block:: console

   west build -b ecb02c samples/zigbee/ieee802154_sniffer

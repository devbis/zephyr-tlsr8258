.. _zigbee:

Zigbee
######

Zephyr runs the Telink Zigbee stack on top of an IEEE 802.15.4 radio driver.
The stack provides the MAC, network, APS, security, ZDO and BDB layers and the
Zigbee Cluster Library; the port adds a platform layer that maps the stack's
timers, task queues, buffers, non-volatile storage and radio access onto
Zephyr.

Enable it with :kconfig:option:`CONFIG_ZIGBEE` and select the device role with
:kconfig:option:`CONFIG_ZIGBEE_ED`, :kconfig:option:`CONFIG_ZIGBEE_ROUTER` or
:kconfig:option:`CONFIG_ZIGBEE_COORDINATOR`. The stack keeps its network state
in the ``zigbee_nv_partition`` flash partition, or in ``storage_partition``
when no other storage backend uses it.

Radios
******

The stack runs its own MAC and sends raw frames through the radio driver API:

- the TLSR8258 radio (``telink,tlsr8258-zb``), which hands received frames to
  the stack directly;
- any other IEEE 802.15.4 driver, which delivers received frames through a
  dedicated L2 (:kconfig:option:`CONFIG_NET_L2_CUSTOM_IEEE802154`);
- the ``zephyr,native-sim-socket-ieee802154`` driver, which connects
  ``native_sim`` nodes through a medium on a localhost socket.

Samples
*******

- :zephyr:code-sample:`zigbee_node` joins a network as an end device or a
  router.
- :zephyr:code-sample:`zigbee_native_sim_socket` runs an end device, router or
  coordinator on ``native_sim``.

Imported sources
****************

``subsys/zigbee`` imports two upstreams unchanged with
``scripts/zigbee/import_vendor.sh``:

- the open part of the Telink Zigbee SDK (``tl_zigbee_sdk``): the cluster
  library, Green Power, BDB, OTA and the SDK's common and OS headers;
- ``libzigbee``, a reconstruction of the MAC, network, APS, security and ZDO
  layers that the SDK ships as object files only. It is kept equivalent to the
  vendor objects.

The port replaces the vendor buffer pool, task queues, timers and NV driver
with its own implementations under ``platform/zephyr`` and adapts the SDK
sources in a single commit. The following changes to the reconstructed layers
are deliberate departures from the vendor behavior:

.. list-table::
   :header-rows: 1
   :widths: 25 75

   * - Function
     - Change
   * - ``apsTxDataSendStart()``
     - Copies a payload that does not lie inside the buffer being duplicated.
       A router relays received frames from a capture buffer rather than from
       the stack's buffer pool.
   * - ``tl_zbMlmeCmdBeaconReqRecvd()``
     - Releases the beacon request buffer after answering; the answer uses the
       MAC transmit buffer.
   * - ``tl_zbMlmeCmdAssociateRespRecvd()``
     - Accepts the response while the association request is outstanding;
       the poll completion that enters the indirect-data wait is handled on
       the Zigbee thread and can come after the response.
   * - ``mac_csmaStart()``
     - Transmits with interrupts enabled: the platform sends synchronously and
       waits for the radio interrupt.
   * - ``zb_macDataRecvHandler()``
     - Takes a stack buffer only for frames other than acknowledgments.
   * - ``tl_zbNwkNibInit()``
     - Takes the stack profile from the build configuration also when the
       network state is restored.
   * - ``zdo_nlme_join_indication()``
     - Records the child's address pair before authentication, and on a
       router sends the transport key through the parent.

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

Channels
********

By default a router or end device scans all channels (11 to 26) when it
joins, and a coordinator picks the least busy of them when it forms a network.
Restrict this with :kconfig:option:`CONFIG_ZIGBEE_CHANNEL_MASK`, a bit mask
where bit N selects channel N. A single bit pins the node to one channel. An
application can override the mask at run time through
``zb_platform_app_get_join_profile()``. A node that is already
joined first rejoins on its stored channel.

Radios
******

The stack runs its own MAC and sends raw frames through the radio driver API:

- the TLSR8258 radio (``telink,tlsr8258-zb``), which hands received frames to
  the stack directly;
- any other IEEE 802.15.4 driver, which delivers received frames through a
  dedicated L2 (:kconfig:option:`CONFIG_NET_L2_CUSTOM_IEEE802154`);
- the ``zephyr,native-sim-socket-ieee802154`` driver, which connects
  ``native_sim`` nodes through a medium on a localhost socket.

Host interface
**************

A coordinator can be driven by a host over a UART, as a network co-processor
(NCP). :kconfig:option:`CONFIG_ZIGBEE_NCP` is on by default for the
coordinator role, and the protocol is chosen with the options below:

- :kconfig:option:`CONFIG_ZIGBEE_NCP_ZSTACK` (the default) emulates a Z-Stack
  3.0.x ZNP coordinator, which Zigbee2MQTT (``adapter: zstack``) and ZHA
  (zigpy-znp) drive;
- :kconfig:option:`CONFIG_ZIGBEE_NCP_TELINK` speaks a subset of Telink's ZBHCI
  protocol.

The application attaches the UART with ``zb_znp_init()`` (or
``zb_zbhci_uart_init()``) and calls ``zb_znp_bdb_commissioning_status()`` from
``zb_platform_app_bdb_commissioning_status()``. The host forms the network,
registers the endpoints it serves and interviews the devices, so the
application must not start commissioning or register those endpoints itself.
Nothing else may write to the UART: turn the console and logging off.
``zigbee_shell`` shows this with ``coordinator_znp.conf`` and the board
overlays, which name the UART as the ``zigbee,ncp-uart`` chosen node.

The ZNP interface keeps the network, the host's configuration items and the
devices across a restart, answers the commands those hosts use to start and
resume a network, join and interview devices, change the channel, add install
codes, send data and read the device tables, and restores a backup written by
the host. It is polled from the Zigbee thread, so it needs no UART interrupts.

The following are not supported:

- keys installed from install codes are not restored from a backup, and a
  restored associated device is taken as a sleepy end device until it
  announces itself;
- InterPAN, ``AF_DATA_STORE`` and Green Power frames are not passed to the
  host, and ``ZDO_SERVER_DISC_REQ`` is not implemented;
- source-routed sends go out as ordinary unicasts, because the stack builds
  its own source routes;
- hosts must be Z-Stack 3.0.x aware: the interface reports that version and
  the 3.30 extended NV commands are not implemented.

Samples
*******

- :zephyr:code-sample:`zigbee_node` joins a network as an end device or a
  router.
- :zephyr:code-sample:`zigbee_native_sim_socket` runs an end device, router or
  coordinator on ``native_sim``, the coordinator with either host protocol.

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

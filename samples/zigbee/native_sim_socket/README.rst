.. zephyr:code-sample:: zigbee_native_sim_socket
   :name: Zigbee on a simulated IEEE 802.15.4 medium

   Run a Zigbee end device, router or coordinator on native_sim.

Overview
********

This sample runs one Zigbee node on ``native_sim``. The node's radio is the
``zephyr,native-sim-socket-ieee802154`` driver, which exchanges frames with a
medium daemon over a localhost socket, so several node processes form one
network. The node exposes a Basic and an Identify cluster on endpoint 1.

The role is selected at build time:

- end device: the default configuration
- router: ``router.conf`` and ``router.overlay``
- coordinator: ``coordinator.conf`` and ``coordinator.overlay``; the
  coordinator also interviews every device that announces itself and speaks
  the Telink ZBHCI protocol on ``uart1`` (see ``zbhci_client.py``)

Building and Running
********************

Build an end device:

.. zephyr-app-commands::
   :zephyr-app: samples/zigbee/native_sim_socket
   :board: native_sim/native/64
   :goals: build
   :compact:

Build a router:

.. zephyr-app-commands::
   :zephyr-app: samples/zigbee/native_sim_socket
   :board: native_sim/native/64
   :gen-args: -DEXTRA_CONF_FILE=router.conf -DEXTRA_DTC_OVERLAY_FILE=router.overlay
   :goals: build
   :compact:

Start the medium daemon; ``--relay-only`` makes it forward frames without
acting as a coordinator itself:

.. code-block:: console

   tests/subsys/zigbee/host_socket_coordinator/run_daemon.sh --bind-port 19011

Then start each node. The medium address and the node id can be overridden
on the command line:

.. code-block:: console

   zephyr.exe --ieee802154-medium-host=127.0.0.1 --ieee802154-medium-port=19011 \
     --ieee802154-node-id=0x2202

The end device joins with channel 11, node id ``0x2202`` and IEEE address
``a4:c1:38:e0:50:02:00:02``; the router uses ``0x2203`` and ``...:03``, the
coordinator ``0x0001`` and ``...:01``.

The same configurations also run on the ``nrf52_bsim`` BabbleSim board, see
``scripts/bsim/run_zigbee_join.sh``.

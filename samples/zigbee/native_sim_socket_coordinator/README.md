# Native Sim Socket Zigbee Coordinator

Experimental `native_sim/native/64` Zigbee coordinator sample for the localhost
socket-backed IEEE 802.15.4 medium. This is the first sample to build
`CONFIG_ZIGBEE_COORDINATOR`.

Build from the Zephyr tree on macOS with the explicit opt-in switch:

```sh
ZEPHYR_BASE=$PWD ../.venv-zephyr/bin/west build -b native_sim/native/64 \
  samples/zigbee/native_sim_socket_coordinator \
  -d /tmp/zephyr-native-sim-zigbee-coordinator \
  -- -DNATIVE_SIM_EXPERIMENTAL_MACOS=ON -DZEPHYR_TOOLCHAIN_VARIANT=host
```

Start the host medium daemon in relay-only mode (no coordinator application
logic of its own — the network's coordinator/Trust-Center behavior now comes
from this sample):

```sh
tests/subsys/zigbee/host_socket_coordinator/run_daemon.sh --bind-port 19011 --relay-only
```

Then run the Zephyr binary. The sample defaults:

- channel `11`
- node id `0x0001`
- medium `127.0.0.1:19011`
- IEEE `a4:c1:38:e0:50:02:00:01`

Runtime command-line overrides exposed by the radio driver (native_sim needs
`--opt=value`, not `--opt value`):

```sh
./build/zephyr/zephyr.exe \
  --zigbee-medium-host=127.0.0.1 \
  --zigbee-medium-port=19011 \
  --zigbee-node-id=0x0001
```

Pair with `samples/zigbee/native_sim_socket_router` and/or
`samples/zigbee/native_sim_socket_ed` (each with its own node id / IEEE low
byte) pointed at the same relay-only daemon to form a real multi-node network
entirely in simulation. The daemon's `--block-link <id>:<id>` option can drop
RF delivery between two specific node ids, e.g. to force an end device to
join through a router instead of hearing the coordinator directly.

## ZBHCI over UART

`CONFIG_ZIGBEE_ZBHCI_UART=y` (default in this sample's `prj.conf`) exposes a
narrow subset of Telink's ZBHCI protocol on `uart1` — see
`subsys/zigbee/include/zephyr/zigbee/zb_zbhci.h` and
`subsys/zigbee/platform/zephyr/zb_zbhci_{uart,cmd}.c`. `uart0` stays the
plain debug console.

On startup the binary prints the PTY path for each UART, e.g.:

```
uart_1 connected to pseudotty: /dev/ttys010
```

Drive it with the bundled client (stdlib only, no pyserial needed):

```sh
./zbhci_client.py /dev/ttys010 permit-join --target 0xfffc --duration 60
./zbhci_client.py /dev/ttys010 get-child-nodes
./zbhci_client.py /dev/ttys010 active-ep --target 0x91f2
./zbhci_client.py /dev/ttys010 af-data-send --dst 0x91f2 --src-ep 1 --dst-ep 1 \
  --cluster 0x0000 --payload 00
```

Verified against a joined router: `permit-join`, `formation`,
`get-child-nodes`, `active-ep` (full ZDO round trip — the router's real
Active_EP_rsp comes back over the serial link), and `af-data-send` all work
end-to-end. `simple-desc` transmits its Simple_Desc_req correctly but the
router does not answer it — a ZDO-layer gap to chase separately, unrelated to
this transport.

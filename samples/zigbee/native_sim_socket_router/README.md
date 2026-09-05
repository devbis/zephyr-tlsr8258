# Native Sim Socket Zigbee Router

Experimental `native_sim/native/64` Zigbee router sample for the localhost
socket-backed IEEE 802.15.4 medium. Companion to
`samples/zigbee/native_sim_socket_coordinator` and
`samples/zigbee/native_sim_socket_ed` — build all three against one relay-only
medium daemon to form a real coordinator-router-end-device network entirely
in simulation.

Build from the Zephyr tree on macOS with the explicit opt-in switch:

```sh
ZEPHYR_BASE=$PWD ../.venv-zephyr/bin/west build -b native_sim/native/64 \
  samples/zigbee/native_sim_socket_router \
  -d /tmp/zephyr-native-sim-zigbee-router \
  -- -DNATIVE_SIM_EXPERIMENTAL_MACOS=ON -DZEPHYR_TOOLCHAIN_VARIANT=host
```

Start the host medium daemon in relay-only mode:

```sh
tests/subsys/zigbee/host_socket_coordinator/run_daemon.sh --bind-port 19011 --relay-only
```

Then run the coordinator (see its own README), and this binary. The sample
defaults:

- channel `11`
- node id `0x2203`
- medium `127.0.0.1:19011`
- IEEE `a4:c1:38:e0:50:02:00:03`

Runtime command-line overrides exposed by the radio driver (native_sim needs
`--opt=value`, not `--opt value`):

```sh
./build/zephyr/zephyr.exe \
  --zigbee-medium-host=127.0.0.1 \
  --zigbee-medium-port=19011 \
  --zigbee-node-id=0x2203
```

This router joins the coordinator via normal BDB network steer (active scan +
association), the same path already verified against the host coordinator
daemon (`samples/zigbee/native_sim_socket_ed` + `router.conf`).

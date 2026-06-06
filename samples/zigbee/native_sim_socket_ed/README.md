# Native Sim Socket Zigbee ED

Experimental `native_sim/native/64` Zigbee end-device sample for the localhost
socket-backed IEEE 802.15.4 medium.

Build from the Zephyr tree on macOS with the explicit opt-in switch:

```sh
ZEPHYR_BASE=$PWD ../.venv-zephyr/bin/west build -b native_sim/native/64 \
  samples/zigbee/native_sim_socket_ed \
  -d /tmp/zephyr-native-sim-zigbee-ed \
  -- -DNATIVE_SIM_EXPERIMENTAL_MACOS=ON -DZEPHYR_TOOLCHAIN_VARIANT=host
```

Start the host coordinator daemon:

```sh
tests/subsys/zigbee/host_socket_coordinator/run_daemon.sh --bind-port 19011
```

Then run the Zephyr binary. The sample defaults match the daemon:

- channel `11`
- node id `0x2202`
- medium `127.0.0.1:19011`
- IEEE `a4:c1:38:e0:50:02:00:02`

Runtime command-line overrides exposed by the radio driver:

```sh
./build/zephyr/zephyr.exe \
  --zigbee-medium-host 127.0.0.1 \
  --zigbee-medium-port 19011 \
  --zigbee-node-id 0x2202
```

Current scope is a single sleepy end device talking to the host daemon over the
experimental native_sim macOS path.

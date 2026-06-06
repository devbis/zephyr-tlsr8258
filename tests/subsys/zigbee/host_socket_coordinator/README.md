# Zigbee Host Socket Coordinator

This directory contains two host-side artifacts for the native-sim Zigbee
socket path:

- `run.sh`: unit-style regression for the coordinator state machine.
- `run_daemon.sh`: localhost UDP coordinator daemon for a single end device.

Run from the Zephyr tree:

```sh
tests/subsys/zigbee/host_socket_coordinator/run.sh
tests/subsys/zigbee/host_socket_coordinator/run_daemon.sh --bind-port 19011
```

The daemon reuses the same frame/state logic as the regression test and is
intended for the `samples/zigbee/native_sim_socket_ed` sample.

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

Pass `--relay-only` to run the daemon as a pure RF-medium simulator (peer
table, airtime/collision model, TX fan-out) with no coordinator/Trust-Center
application logic. Use this mode when a real native_sim build plays the
coordinator role (`CONFIG_ZIGBEE_COORDINATOR=y`) and only needs the daemon to
relay frames between it and other native_sim peers (router, end device).

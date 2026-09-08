# Zigbee Host Scenario Simulator

This simulator runs on macOS/Linux as a normal host binary. It does not boot
Zephyr and does not require a TLSR8258 device.

It models the first integration scenarios needed for sleepy end-device work. The
device and coordinator exchange encoded byte frames; receivers decode the wire
payload before dispatching scenario events.

- permit-join association succeeds;
- permit-join disabled rejects association;
- joined sleepy device polls coordinator indirect frames;
- coordinator interview reaches Basic/modelId read response;
- restored joined device resumes polling without a fresh association.

Run from the Zephyr tree:

```sh
tests/subsys/zigbee/host_sim/run.sh
```

This is intentionally a protocol scenario harness, not a replacement for
Zephyr `native_sim`. The current byte format covers the key command IDs,
addresses, endpoints, clusters, and Basic/modelId read response payload. The
next useful step is to replace the local encoders with shared helpers from the
minimal Zigbee implementation where practical.

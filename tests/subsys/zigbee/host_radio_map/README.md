# Zigbee Host Radio Map Test

This is a small macOS/Linux host test for Zigbee radio mapping helpers. It does
not boot Zephyr or require a TLSR8258 device.

Run from the Zephyr tree:

```sh
tests/subsys/zigbee/host_radio_map/run.sh
```

`native_sim` on macOS is still experimental. The current opt-in command reaches
normal compilation but is blocked by Mach-O iterable sections and macOS
`va_list` ABI differences:

```sh
ZEPHYR_BASE=$PWD ZEPHYR_TOOLCHAIN_VARIANT=host \
  ../.venv-zephyr/bin/west build -b native_sim \
  tests/subsys/zigbee/radio_adapter_map \
  -d /private/tmp/zephyr-zigbee-radio-adapter-map \
  -- -DNATIVE_SIM_EXPERIMENTAL_MACOS=ON
```

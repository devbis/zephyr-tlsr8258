#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Drive the native_sim_socket coordinator built with CONFIG_ZIGBEE_NCP_ZSTACK
using zigpy-znp, the Z-Stack driver of ZHA.

The script forms a network on channel 11, permits joining and waits for a device
to join and be interviewed, then reads its Basic cluster.

Usage:
    ./znp_check.py /dev/ttysNNN [wait-seconds]
"""

import asyncio
import sys

import zigpy_znp.config as conf
from zigpy_znp.zigbee.application import ControllerApplication


async def main(port: str, wait: int) -> None:
    app = await ControllerApplication.new(
        {
            conf.CONF_DEVICE: {conf.CONF_DEVICE_PATH: port, "baudrate": 115200},
            "database_path": None,
            "network": {"channel": 11},
        },
        auto_form=True,
        start_radio=True,
    )
    initialized = asyncio.Event()

    class Listener:
        def device_initialized(self, device):
            print("initialized", device.ieee, flush=True)
            initialized.set()

        def __getattr__(self, name):
            return lambda *args, **kwargs: None

    app.add_listener(Listener())
    print("network formed, channel", app.state.network_info.channel, flush=True)

    await app.permit(250)
    print("permit join open", flush=True)

    await asyncio.wait_for(initialized.wait(), wait)
    device = next(d for d in app.devices.values() if d.nwk != 0)
    endpoint = next(e for n, e in device.endpoints.items() if n != 0)
    read, _ = await endpoint.basic.read_attributes(["manufacturer", "model", "zcl_version"])
    print("basic", read, flush=True)

    await app.shutdown()


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 120))

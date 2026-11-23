#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0
"""Drive the native_sim_socket coordinator built with CONFIG_ZIGBEE_NCP_EZSP
using bellows, the EZSP driver of ZHA.

The script forms a network on channel 15 (or finds the one that exists),
asks the coordinator itself for its node descriptor over the air interface, opens
permit join and, with a wait time, waits for a device to join and be interviewed
and reads its Basic cluster.

Usage:
    ./ezsp_check.py /dev/ttysNNN [wait-seconds]
"""

import asyncio
import sys

import zigpy.config as zconf
from bellows.zigbee.application import ControllerApplication


async def main(port: str, wait: int) -> None:
    app = await ControllerApplication.new(
        {
            zconf.CONF_DEVICE: {zconf.CONF_DEVICE_PATH: port, zconf.CONF_DEVICE_BAUDRATE: 115200},
            zconf.CONF_NWK: {zconf.CONF_NWK_CHANNEL: 15},
            "database_path": None,
        },
        auto_form=True,
        start_radio=True,
    )
    network = app.state.network_info
    print("network up, channel", network.channel, "pan", hex(network.pan_id), flush=True)

    # A message to the node itself goes out through sendUnicast and comes back as an
    # incomingMessageHandler callback, so this checks both directions.
    me = app.get_device(nwk=0x0000)
    node_desc = await asyncio.wait_for(me.zdo.Node_Desc_req(0x0000), 10)
    print("node descriptor", node_desc[2].logical_type, flush=True)

    await app.permit(250)
    print("permit join open", flush=True)

    if wait > 0:
        initialized = asyncio.Event()

        class Listener:
            def device_initialized(self, device):
                print("initialized", device.ieee, flush=True)
                initialized.set()

            def __getattr__(self, name):
                return lambda *args, **kwargs: None

        app.add_listener(Listener())
        await asyncio.wait_for(initialized.wait(), wait)
        device = next(d for d in app.devices.values() if d.nwk != 0)
        endpoint = next(e for n, e in device.endpoints.items() if n != 0)
        read, _ = await endpoint.basic.read_attributes(["manufacturer", "model", "zcl_version"])
        print("basic", read, flush=True)

    await app.shutdown()


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 0))

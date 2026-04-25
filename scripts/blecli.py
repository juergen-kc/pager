#!/usr/bin/env python3
"""More thorough BLE NUS client to isolate which direction is breaking."""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

NAME_PREFIX = "Claude-Pager-"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    print("[cli] scanning for Pager…", flush=True)
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for addr, (dev, adv) in devices.items():
        name = dev.name or adv.local_name or ""
        if name.startswith(NAME_PREFIX):
            target = (addr, dev, adv)
            break
    if target is None:
        print("[cli] no Pager found"); sys.exit(1)
    addr, dev, adv = target
    print(f"[cli] found {dev.name or adv.local_name} at {addr} RSSI={adv.rssi}", flush=True)

    async with BleakClient(addr) as client:
        print("[cli] connected, MTU =", client.mtu_size, flush=True)

        # List services / characteristics so we can see the GATT structure
        # that the host actually got.
        print("[cli] discovered GATT structure:", flush=True)
        for svc in client.services:
            print(f"  service {svc.uuid}", flush=True)
            for ch in svc.characteristics:
                props = ",".join(ch.properties)
                print(f"    char {ch.uuid} handle={ch.handle} props=[{props}]", flush=True)

        # Subscribe (notification handler)
        rx_count = [0]
        def handle_notify(_handle, data: bytearray):
            rx_count[0] += 1
            try:
                s = data.decode("utf-8", errors="replace").rstrip()
                print(f"[notify #{rx_count[0]}] {s}", flush=True)
            except Exception as e:
                print(f"[notify err] {e} raw={bytes(data)!r}", flush=True)

        await client.start_notify(NUS_TX_UUID, handle_notify)
        print("[cli] subscribed to TX, waiting 3s for any unsolicited notify…", flush=True)
        await asyncio.sleep(3.0)
        print(f"[cli] notifications received so far: {rx_count[0]}", flush=True)

        # Send a tiny status query
        print("[cli] writing {\"cmd\":\"status\"}…", flush=True)
        try:
            await client.write_gatt_char(NUS_RX_UUID, b'{"cmd":"status"}\n', response=True)
            print("[cli] write returned cleanly", flush=True)
        except Exception as e:
            print(f"[cli] write failed: {e}", flush=True)

        await asyncio.sleep(5.0)
        print(f"[cli] total notifications: {rx_count[0]}", flush=True)

        await client.stop_notify(NUS_TX_UUID)
        print("[cli] done", flush=True)


asyncio.run(main())

#!/usr/bin/env python3
"""Pretend to be Claude desktop and feed the Pager a heartbeat with a fake
permission prompt, then watch for the device's approve/deny reply. Lets us
verify the approval-takeover UI without needing a real Claude Code session
to trip a real permission prompt."""
import asyncio
import json
import sys
from bleak import BleakClient, BleakScanner

NAME_PREFIX = "Claude-Pager-"
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    print("[fake] scanning…", flush=True)
    devs = await BleakScanner.discover(timeout=8.0, return_adv=True)
    target = None
    for addr, (d, adv) in devs.items():
        n = d.name or adv.local_name or ""
        if n.startswith(NAME_PREFIX):
            target = (addr, d, adv); break
    if not target:
        print("[fake] no Pager"); sys.exit(1)
    addr, d, adv = target
    print(f"[fake] connecting to {d.name or adv.local_name}", flush=True)

    decision_seen = asyncio.Event()
    decision_payload = {}

    async with BleakClient(addr) as client:
        print(f"[fake] connected, MTU={client.mtu_size}", flush=True)

        def on_notify(_h, data: bytearray):
            line = data.decode("utf-8", errors="replace").rstrip()
            print(f"[rx] {line}", flush=True)
            try:
                obj = json.loads(line)
                if obj.get("cmd") == "permission":
                    decision_payload.update(obj)
                    decision_seen.set()
            except Exception:
                pass

        await client.start_notify(NUS_TX_UUID, on_notify)
        print("[fake] subscribed", flush=True)
        await asyncio.sleep(1.0)

        # Heartbeat with a fake prompt
        prompt_id = "req_fake_test_001"
        snapshot = {
            "total": 1,
            "running": 0,
            "waiting": 1,
            "msg": "Bash permission",
            "entries": [],
            "tokens": 0,
            "tokens_today": 0,
            "prompt": {
                "id": prompt_id,
                "tool": "Bash",
                "hint": "rm -rf /tmp/test-folder",
            },
        }
        line = (json.dumps(snapshot) + "\n").encode("utf-8")
        print(f"[tx] sending heartbeat with prompt id={prompt_id}", flush=True)
        await client.write_gatt_char(NUS_RX_UUID, line, response=False)

        print("[fake] waiting up to 60s for the user to tap on the device…",
              flush=True)
        try:
            await asyncio.wait_for(decision_seen.wait(), timeout=60.0)
            print(f"[fake] DECISION RECEIVED: {decision_payload}", flush=True)
            if decision_payload.get("id") == prompt_id:
                print(f"[fake] OK — id matches, decision={decision_payload.get('decision')}",
                      flush=True)
            else:
                print("[fake] WARN — id mismatch", flush=True)
        except asyncio.TimeoutError:
            print("[fake] TIMEOUT — no decision came back", flush=True)

        await client.stop_notify(NUS_TX_UUID)


asyncio.run(main())

"""End-to-end check of the firmware on a board: this computer plays the phone over Bluetooth,
reads the board's serial log to confirm each result, and drives the USB console.

    python tools/test_ble.py --port COM4
    python tools/test_ble.py --port COM4 --show ff0000 [--stale]
    python tools/test_ble.py --port COM4 --live      (reads the public CheerLights feed once)

Written for an ESP32-S3 board. It changes the colors shown while it runs and puts back the brightness cap
it changes. It only talks to the board and never sets the public CheerLights color.
Needs: pip install bleak pyserial
"""
import argparse
import asyncio
import json
import os
import re
import struct
import sys
import threading
import time
import urllib.request

import serial
from bleak import BleakClient, BleakScanner

SERVICE = "b73a0a04-bdb7-4051-9135-815aac2abaa3"
COLOR = "0fc3891d-11a7-408c-9283-b14dffc5bcff"
INFO = "3554cfbc-0bcc-47f2-9af2-3b313c97a59d"
LIGHT = "0a8345b7-afa9-47df-9d63-e98130514e19"
EFFECTS = "c4fa31c4-e81e-49d2-be0c-530c01865776"
UNREAD_WRITES = 60  # each logs about 28 bytes, enough to overflow the board's 1024-byte log buffer
UNREAD_MAX_MS = 1000
HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = os.path.join(HERE, "..", "docs")


class SerialLog:
    """Collects the board's log lines without resetting it."""

    def __init__(self, port):
        self.lines = []
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.1
        self.s.dtr = False
        self.s.rts = False
        self.open()

    def open(self):
        """Opens the port, or reopens it after close, keeping the lines seen so far."""
        self.stop = False
        self.s.open()
        threading.Thread(target=self._run, daemon=True).start()

    def send(self, command):
        self.s.write((command + "\n").encode())

    def _run(self):
        buf = b""
        while not self.stop:
            try:
                buf += self.s.read(512)
            except serial.SerialException:
                # A restart can drop a native USB port for a moment; reopen it.
                time.sleep(0.5)
                try:
                    self.s.close()
                    self.s.open()
                except serial.SerialException:
                    pass
                continue
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                self.lines.append(line.decode("utf-8", "replace").strip())

    async def wait_for(self, pattern, timeout=3.0, seen=None):
        """Wait for a matching line, counting only lines after index `seen`."""
        start = time.time()
        seen = len(self.lines) if seen is None else seen
        while time.time() - start < timeout:
            for line in self.lines[seen:]:
                if re.search(pattern, line):
                    return line
            await asyncio.sleep(0.02)
        return None

    def close(self):
        if self.stop:
            return
        self.stop = True
        time.sleep(0.2)
        self.s.close()


class Results:
    def __init__(self):
        self.failed = 0
        self.total = 0

    def check(self, name, ok, detail=""):
        self.total += 1
        self.failed += 0 if ok else 1
        print(f"{'ok  ' if ok else 'FAIL'} {name}{'  ' + str(detail) if detail else ''}", flush=True)


def build_frame(rgb_hex, stale=False, index=0xFF, sequence=1, age=0):
    rgb = bytes.fromhex(rgb_hex)
    return bytes([0x10 | (1 if stale else 0), index]) + rgb + struct.pack("<HH", sequence, age) + b"\x00"


async def find_board(timeout=12.0):
    found = await BleakScanner.find_device_by_filter(
        lambda d, adv: SERVICE in [u.lower() for u in adv.service_uuids], timeout=timeout
    )
    return found


async def write_and_expect(client, log, frame, response=True, char=COLOR, pattern=r"^(frame|ignored) "):
    pending = asyncio.create_task(log.wait_for(pattern))
    await asyncio.sleep(0)
    await client.write_gatt_char(char, frame, response=response)
    return await pending


async def full_test(port):
    r = Results()
    log = SerialLog(port)
    with open(os.path.join(SPEC, "ble-vectors.json"), encoding="utf-8") as f:
        spec_vectors = json.load(f)
    vectors = spec_vectors["frame"]
    try:
        scanner_seen = {}
        def on_adv(device, adv):
            if SERVICE in [u.lower() for u in adv.service_uuids]:
                scanner_seen[device.address] = (device, adv)
        async with BleakScanner(on_adv):
            await asyncio.sleep(6)
        r.check("advertises the service UUID", bool(scanner_seen))
        if not scanner_seen:
            return r
        device, adv = next(iter(scanner_seen.values()))
        name = adv.local_name or device.name or ""
        r.check("name is 8 characters or fewer", 0 < len(name) <= 8, name)
        print("     address", device.address, "rssi", adv.rssi)

        before_connect = len(log.lines)
        async with BleakClient(device) as client:
            connected = await log.wait_for(r"^connected", 2.0, seen=before_connect)
            r.check("connects, and the board logs it", client.is_connected and connected is not None, connected)

            service = client.services.get_service(SERVICE)
            props = sorted(service.get_characteristic(COLOR).properties)
            r.check("Color is read, write, and write without response",
                    {"read", "write", "write-without-response"} <= set(props), props)
            r.check("Info is read only", sorted(service.get_characteristic(INFO).properties) == ["read"])

            info = await client.read_gatt_char(INFO)
            r.check("Info is 5 bytes: protocol 1, fw 0.4.0, display, off control, and light, set up",
                    bytes(info) == bytes([1, 0, 4, 0, 7]), bytes(info).hex())

            r.check("Light is read, write, and write without response",
                    {"read", "write", "write-without-response"} <= set(service.get_characteristic(LIGHT).properties))
            r.check("Effects is read only", sorted(service.get_characteristic(EFFECTS).properties) == ["read"])
            effects = bytes(await client.read_gatt_char(EFFECTS))
            want_effects = bytes.fromhex(spec_vectors["effects"][0]["bytes"])
            r.check("Effects matches the published vector, read in full", effects == want_effects, effects.hex())

            for v in spec_vectors["light"]:
                raw = bytes.fromhex(v["bytes"])
                line = await write_and_expect(client, log, raw, char=LIGHT, pattern=r"^light brightness")
                want = f"light brightness={v['brightness']} effect={v['effect']} speed={v['speed']}"
                r.check(f"light vector: {v['name']}", line == want, line)
                r.check("  reads back the same value", bytes(await client.read_gatt_char(LIGHT)) == raw)
            for v in spec_vectors["lightClamped"]:
                line = await write_and_expect(client, log, bytes.fromhex(v["bytes"]), char=LIGHT, pattern=r"^light brightness")
                want = f"light brightness={v['brightness']} effect={v['effect']} speed={v['speed']}"
                r.check(f"light clamps: {v['name']}", line == want, line)
            for v in spec_vectors["lightRejected"]:
                line = await write_and_expect(client, log, bytes.fromhex(v["bytes"]), char=LIGHT, pattern=r"^ignored reason=light|^light brightness")
                r.check(f"light ignores: {v['name']}", line is not None and "reason=light" in line, line)
            for v in spec_vectors["lightNewer"]:
                raw = bytes.fromhex(v["bytes"])
                line = await write_and_expect(client, log, raw, char=LIGHT, pattern=r"^ignored reason=light|^light brightness")
                want = f"light brightness={v['brightness']} effect={v['effect']} speed={v['speed']}"
                r.check(f"light from a newer phone: {v['name']}", line == want, line)
                r.check("  reads back the fields this firmware knows",
                        bytes(await client.read_gatt_char(LIGHT)) == raw[:4])
            # Back to solid at full brightness, so the frame checks below see plain colors.
            await write_and_expect(client, log, bytes.fromhex("01ff0080"), char=LIGHT, pattern=r"^light brightness")

            for v in vectors:
                raw = bytes.fromhex(v["bytes"])
                line = await write_and_expect(client, log, raw)
                want = (f"frame seq={v['sequence']} idx={v['paletteIndex']} rgb={v['rgb']} "
                        f"age={v['ageMinutes']} stale={1 if v['stale'] else 0}")
                r.check(f"vector: {v['name']}", line is not None and line.startswith(want), line)
                back = bytes(await client.read_gatt_char(COLOR))
                r.check("  reads back the same frame", back == raw, back.hex())

            last = bytes.fromhex(vectors[-1]["bytes"])
            line = await write_and_expect(client, log, bytes.fromhex("20" + "00" * 9))
            r.check("unknown version is ignored", line is not None and "reason=version" in line, line)
            r.check("  read still returns the last good frame",
                    bytes(await client.read_gatt_char(COLOR)) == last)

            line = await write_and_expect(client, log, b"\x10\x09\xff")
            r.check("short write is ignored", line is not None and "reason=length" in line, line)
            r.check("  read still returns the last good frame",
                    bytes(await client.read_gatt_char(COLOR)) == last)
            for v in spec_vectors["frameNewer"]:
                raw = bytes.fromhex(v["bytes"])
                line = await write_and_expect(client, log, raw)
                want = (f"frame seq={v['sequence']} idx={v['paletteIndex']} rgb={v['rgb']} "
                        f"age={v['ageMinutes']} stale={1 if v['stale'] else 0}")
                r.check(f"frame from a newer phone: {v['name']}", line is not None and line.startswith(want), line)
                back = bytes(await client.read_gatt_char(COLOR))
                r.check("  reads back the 10 bytes this firmware knows", back == raw[:10], back.hex())

            fast = build_frame("00ffff", index=3, sequence=1000)
            line = await write_and_expect(client, log, fast, response=False)
            r.check("write without response works", line is not None and "seq=1000" in line, line)

            wrap = build_frame("ff00ff", index=7, sequence=0)
            line = await write_and_expect(client, log, wrap)
            r.check("sequence wrap to 0 still shows", line is not None and "seq=0 " in line, line)

            start, count = time.time(), 0
            for i in range(40):
                await client.write_gatt_char(COLOR, build_frame("0000ff", index=2, sequence=2000 + i), response=True)
                count += 1
            burst = time.time() - start
            await asyncio.sleep(0.5)
            newest = [l for l in log.lines if l.startswith("frame seq=2039 ")]
            r.check("after a 40-write burst, the newest frame wins", bool(newest), f"{burst:.1f}s")

        # Windows can hold the radio link open after the app lets go, so give it time.
        gone = await log.wait_for(r"^disconnected", 45.0, seen=before_connect)
        r.check("disconnect is logged", gone is not None, gone)

        again = await find_board()
        r.check("advertises again after a disconnect", again is not None)
        if again is not None:
            async with BleakClient(again) as client:
                back = bytes(await client.read_gatt_char(COLOR))
                r.check("after reconnecting, it still holds the last frame",
                        back == build_frame("0000ff", index=2, sequence=2039), back.hex())
                line = await write_and_expect(client, log, build_frame("ffa500", index=9, sequence=3000))
                r.check("after reconnecting, a new color applies", line is not None and "rgb=ffa500" in line, line)

                # The board is usually powered from a PC that opened the port once and stopped reading.
                # Logging used to wait 2 s per line then, which held up every write and every color.
                log.close()
                times = []
                for _ in range(UNREAD_WRITES):
                    start = time.perf_counter()
                    await client.write_gatt_char(COLOR, b"\x10\x09\xff", response=True)
                    times.append((time.perf_counter() - start) * 1000)
                r.check("with the port open but unread, writes stay fast after the log buffer fills",
                        max(times) < UNREAD_MAX_MS, f"max {max(times):.0f} ms over {UNREAD_WRITES} logged writes")
                log.open()
                await asyncio.sleep(0.3)
                line = await write_and_expect(client, log, b"\x10\x09\xff")
                r.check("  logging resumes when the port is read again", line is not None and "reason=length" in line, line)

        await console_checks(r, log)
    finally:
        log.close()
    print("\nboard log, connection lines:")
    for line in log.lines:
        if line.startswith(("connected", "disconnected", "ready")):
            print("  ", line)
    print(f"\n{r.total - r.failed} of {r.total} checks passed")
    errors = [l for l in log.lines if re.search(r"(?i)guru|panic|abort|backtrace", l)]
    print("board crash lines:", errors or "none")
    r.failed += len(errors)
    return r


async def console(log, command, pattern, timeout=3.0):
    seen = len(log.lines)
    log.send(command)
    return await log.wait_for(pattern, timeout, seen=seen)


async def console_checks(r, log):
    """The USB console: changes wait for save, bad pins are refused, and a save survives the restart.
    It changes only the brightness cap, and puts the board's own value back."""
    shown = await console(log, "show", r"^In use: ")
    r.check("console: show lists the settings in use", shown is not None, shown)
    if shown is None:
        return
    cap = re.search(r"brightness cap (\d+)", shown)
    pin = re.search(r"on GPIO (\d+)", shown)
    r.check("console: the settings name a pin and a brightness cap", cap is not None and pin is not None, shown)
    if cap is None or pin is None:
        return
    cap, pin = int(cap.group(1)), int(pin.group(1))

    line = await console(log, "pixels 26 10", r"cannot drive|LEDs on GPIO")
    r.check("console: a flash pin is refused on an ESP32-S3", line is not None and "cannot drive" in line, line)
    await console(log, f"pixels {pin} 7 0", r"LEDs on GPIO")
    line = await console(log, "show", r"^Waiting for save: ")
    r.check("console: a change waits for save", line is not None and f"7 LEDs on GPIO {pin} in a strip" in line, line)
    line = await console(log, "cancel", r"^Changes dropped")
    shown_again = await console(log, "show", r"^In use: |^Waiting")
    r.check("console: cancel drops it", line is not None and shown_again == shown, shown_again)

    new_cap = 9 if cap != 9 else 10
    await console(log, f"max {new_cap}", r"^Brightness cap")
    before_save = len(log.lines)
    line = await console(log, "save", r"^settings .*", timeout=15.0)
    r.check("console: save restarts with the new setting", line is not None and f"brightness cap {new_cap}" in line, line)
    ready = await log.wait_for(r"^ready name=", 10.0, seen=before_save)
    r.check("  and Bluetooth starts again", ready is not None, ready)
    await console(log, f"max {cap}", r"^Brightness cap")
    before_save = len(log.lines)
    line = await console(log, "save", r"^settings .*", timeout=15.0)
    r.check("  the original cap is back", line is not None and f"brightness cap {cap}" in line, line)
    await log.wait_for(r"^ready name=", 10.0, seen=before_save)


async def show(port, rgb_hex, stale, index, sequence):
    log = SerialLog(port)
    try:
        device = await find_board()
        if device is None:
            print("board not found")
            return 1
        async with BleakClient(device) as client:
            line = await write_and_expect(client, log, build_frame(rgb_hex, stale, index, sequence))
            print(line)
        return 0 if line else 1
    finally:
        log.close()


def live_color():
    with urllib.request.urlopen("https://api.thingspeak.com/channels/1417/feeds/last.json", timeout=15) as f:
        last = json.load(f)
    with open(os.path.join(SPEC, "colors.json"), encoding="utf-8") as f:
        colors = json.load(f)
    entries = colors["colors"] if isinstance(colors, dict) else colors
    name = (last.get("field1") or "").strip().lower()
    hex_value = (last.get("field2") or "").strip().lstrip("#").lower()
    index = next((c.get("index", i) for i, c in enumerate(entries) if c.get("name") == name), 0xFF)
    return name, hex_value, index, last.get("entry_id", 0) & 0xFFFF


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True)
    ap.add_argument("--show")
    ap.add_argument("--stale", action="store_true")
    ap.add_argument("--live", action="store_true")
    a = ap.parse_args()
    if a.live:
        name, hex_value, index, seq = live_color()
        print("live color:", name, hex_value, "index", index)
        sys.exit(asyncio.run(show(a.port, hex_value, False, index, seq)))
    if a.show:
        sys.exit(asyncio.run(show(a.port, a.show, a.stale, 0xFF, 1)))
    sys.exit(1 if asyncio.run(full_test(a.port)).failed else 0)


if __name__ == "__main__":
    main()

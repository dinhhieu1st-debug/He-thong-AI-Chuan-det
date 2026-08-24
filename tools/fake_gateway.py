#!/usr/bin/env python3
"""Stands in for the Raspberry Pi gateway so the web console can be tested
without a Pi, a Zigbee coordinator or a board.

It speaks the same TCP protocol the real gateway does: one connection to the
HIS server, a `device_announce` line per device, then one JSON reading per line.
Commands the server writes back are printed.

The important part is that ONE connection carries SEVERAL beds, exactly as a
real Pi does. Three of the bugs this was written to check only appear that way:

  * a "new device joined" toast on nearly every reading,
  * a bed command published to whichever device reported last,
  * every bed except the most recent one refusing commands with 409.

Usage
-----
    python3 tools/fake_gateway.py                       # 3 beds, one gateway
    python3 tools/fake_gateway.py --beds BED-101,BED-201
    python3 tools/fake_gateway.py --host 127.0.0.1 --port 5000

While it runs, type at the prompt:

    unplug BED-101     stop sending for that bed (watch it go Offline)
    plug BED-101       resume
    announce           re-announce every device, as if the gateway reconnected
    status             what the simulator currently believes
    quit

Nothing here is a test framework. It is a hand-driven rig for looking at the
real UI, which is what the offline/room/toast behaviour has to be judged in.
"""

from __future__ import annotations

import argparse
import json
import math
import random
import socket
import sys
import threading
import time

DEFAULT_BEDS = ["BED-101", "BED-102", "BED-201"]

# The room the gateway claims. Deliberately the same useless default the real
# gateway ships with (BED_ROOM in gateway.conf), because "does an administrator's
# room rename survive the next reading?" is one of the things being tested.
GATEWAY_ROOM = "Unknown room"


class Device:
    """One XG26 chip reporting to one bed."""

    def __init__(self, bed_id: str) -> None:
        self.bed_id = bed_id
        self.device_id = f"XG26-{bed_id}"
        self.plugged = True

        # Every value the chip would hold across readings.
        self.target_drops_per_min = 20
        self.monitoring = True
        self.vitals_test_mode = 0
        self.hr_baseline = 78.0
        self.spo2_baseline = 97.0
        self.phase = random.random() * math.tau
        self.samples = 0

    # -- what the sensors would read -------------------------------------
    def real_vitals(self) -> tuple[int, int]:
        """A gentle wander around the baseline, so charts have something to
        draw and nothing ever looks like a flat synthetic line."""
        t = time.time() + self.phase
        hr = self.hr_baseline + 6.0 * math.sin(t / 7.0) + random.uniform(-1.5, 1.5)
        spo2 = self.spo2_baseline + 1.2 * math.sin(t / 11.0) + random.uniform(-0.4, 0.4)
        return int(round(hr)), int(round(min(100.0, spo2)))

    def ai_inputs(self, hr: int, spo2: int) -> tuple[int, int]:
        """What the chip feeds its AI branch.

        Mirrors app.c: test mode 2 scales the heart rate to 0.83 of baseline and
        leaves SpO2 alone, mode 3 scales both to 0.75. Reproduced here because
        the console's "Real vs AI test" pairing is the only visible proof that a
        command travelled all the way to a chip - if the simulator just echoed
        the real values, that check would pass while testing nothing.
        """
        if self.vitals_test_mode == 2:
            return int(round(self.hr_baseline * 0.83)), int(round(self.spo2_baseline))
        if self.vitals_test_mode == 3:
            return (int(round(self.hr_baseline * 0.75)),
                    int(round(self.spo2_baseline * 0.75)))
        return hr, spo2

    def reading(self) -> dict:
        hr, spo2 = self.real_vitals()
        ai_hr, ai_spo2 = self.ai_inputs(hr, spo2)
        self.samples = min(self.samples + 1, 64)

        drops = self.target_drops_per_min + random.choice([-1, 0, 0, 0, 1])
        armed = self.samples >= 64 and self.monitoring

        # Level 2 whenever the AI branch is being fed something abnormal, which
        # is what the test modes exist to produce.
        vitals_level = 1
        if armed and self.vitals_test_mode == 2:
            vitals_level = 2
        elif armed and self.vitals_test_mode == 3:
            vitals_level = 3

        drip_level = 1
        final = 1 if (vitals_level == 1 and drip_level == 1) else (
            3 if (vitals_level == 3 and drip_level == 3) else 2)

        return {
            "bedId": self.bed_id,
            "room": GATEWAY_ROOM,
            "deviceId": self.device_id,
            "heartRate": hr,
            "spo2": spo2,
            "dripRate": int(round(drops * 100 / max(1, self.target_drops_per_min))),
            "drops_per_min": drops,
            "target_drops_per_min": self.target_drops_per_min,
            "weight_g": 480,
            "hr_signal": True,
            "spo2_signal": True,
            "flow_signal": True,
            "drops_signal": True,
            "monitoring": self.monitoring,
            "drop_training_samples": min(self.samples, 20),
            "vitals_training_samples": self.samples,
            "alerts_armed": armed,
            "final_alert_level": final if armed else None,
            "vitals_level": vitals_level if armed else None,
            "server_drop_level": drip_level if armed else None,
            "ai_input_heart_rate": ai_hr,
            "ai_input_spo2": ai_spo2,
            "vitals_test_mode": self.vitals_test_mode,
            "linkquality": random.randint(80, 140),
        }


# Fields that only a CURRENT converter publishes. A Pi still running an older
# external converter sends none of them, and the point of --legacy is to prove
# the server and the console degrade quietly instead of showing a zero, a NaN,
# or an "undefined" where a clinical number belongs.
NEW_CONVERTER_FIELDS = (
    "ai_input_heart_rate",
    "ai_input_spo2",
    "vitals_level",
    "server_drop_level",
    "vitals_test_mode",
    "final_alert_level",
    "alerts_armed",
    "drop_training_samples",
    "vitals_training_samples",
)


class FakeGateway:
    def __init__(self, host: str, port: int, bed_ids: list[str],
                 legacy: bool = False) -> None:
        self.host = host
        self.port = port
        self.legacy = legacy
        self.devices = {b: Device(b) for b in bed_ids}
        self.by_device = {d.device_id: d for d in self.devices.values()}
        self.sock: socket.socket | None = None
        self.lock = threading.Lock()
        self.running = True

        # Which devices have been announced on the CURRENT connection, matching
        # the fix in server_client.c. A single slot here would re-announce on
        # every reading, which is the behaviour being replaced.
        self.announced: set[str] = set()

        # The last device to publish. Only consulted when a command arrives with
        # no deviceId - which is exactly the dangerous fallback in
        # publish_command(), reproduced so it can be seen happening.
        self.last_published: str | None = None

    # -- wire ------------------------------------------------------------
    def connect(self) -> None:
        while self.running:
            try:
                s = socket.create_connection((self.host, self.port), timeout=10)
                s.settimeout(None)
                with self.lock:
                    self.sock = s
                    self.announced.clear()
                print(f"[gw] connected to HIS {self.host}:{self.port}")
                return
            except OSError as exc:
                print(f"[gw] cannot reach HIS ({exc}); retrying in 3s")
                time.sleep(3)

    def send(self, obj: dict) -> bool:
        line = (json.dumps(obj) + "\n").encode()
        with self.lock:
            if self.sock is None:
                return False
            try:
                self.sock.sendall(line)
                return True
            except OSError:
                self.sock = None
                return False

    def announce(self, device: Device) -> None:
        self.send({"type": "device_announce",
                   "deviceId": device.device_id,
                   "friendlyName": device.device_id})
        self.announced.add(device.device_id)
        print(f"[gw] announced {device.device_id}")

    # -- command path (server -> gateway -> chip) ------------------------
    def handle_command(self, line: str) -> None:
        try:
            cmd = json.loads(line)
        except json.JSONDecodeError:
            print(f"[cmd] not JSON, ignored: {line!r}")
            return

        name = cmd.get("cmd")
        device_id = cmd.get("deviceId")

        if device_id:
            target = self.by_device.get(device_id)
            origin = "addressed"
            if target is None:
                print(f"[cmd] !! {name} names unknown device {device_id}")
                return
        else:
            # The real gateway silently falls back here. Shouted about rather
            # than handled quietly, because a command with no device named is
            # a command that can land on the wrong patient's pump.
            target = self.by_device.get(self.last_published or "")
            origin = "GUESSED from last publisher"
            if target is None:
                print(f"[cmd] !! {name} has no deviceId and nothing has published yet")
                return
            print(f"[cmd] !! WARNING: {name} carried NO deviceId - "
                  f"falling back to {target.device_id}. On real hardware this "
                  f"is how a command reaches the wrong bed.")

        if name == "set_target_drops_per_min":
            target.target_drops_per_min = int(cmd.get("value", 20))
        elif name == "set_monitoring":
            target.monitoring = bool(cmd.get("value", 1))
        elif name == "set_vitals_test_mode":
            target.vitals_test_mode = int(cmd.get("value", 0))
        elif name in ("reset_tare", "recalibrate_hr_baseline", "rescan_devices",
                      "ota_check", "ota_update", "set_target_flow_ml_h",
                      "set_drop_level"):
            pass
        else:
            print(f"[cmd] unknown command {name!r}")
            return

        print(f"[cmd] {name} -> {target.device_id} ({target.bed_id}) [{origin}]"
              + (f" value={cmd.get('value')}" if "value" in cmd else ""))

    def reader(self) -> None:
        buf = b""
        while self.running:
            with self.lock:
                s = self.sock
            if s is None:
                time.sleep(0.2)
                continue
            try:
                chunk = s.recv(4096)
            except OSError:
                chunk = b""
            if not chunk:
                with self.lock:
                    self.sock = None
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if text:
                    self.handle_command(text)

    # -- main loop -------------------------------------------------------
    def run(self) -> None:
        self.connect()
        threading.Thread(target=self.reader, daemon=True).start()

        while self.running:
            for device in self.devices.values():
                if not self.running:
                    break
                if not device.plugged:
                    continue

                with self.lock:
                    connected = self.sock is not None
                if not connected:
                    self.connect()

                # Legacy gateways kept ONE announce slot, so a second device
                # displaced the first and both re-announced forever. Reproduced
                # exactly, because "does the toast storm stop with only the
                # server updated?" is the question --legacy exists to answer.
                if self.legacy:
                    if self.last_published != device.device_id:
                        self.announce(device)
                elif device.device_id not in self.announced:
                    self.announce(device)

                payload = device.reading()
                if self.legacy:
                    for field in NEW_CONVERTER_FIELDS:
                        payload.pop(field, None)

                if self.send(payload):
                    self.last_published = device.device_id
                time.sleep(1.0 / max(1, len(self.devices)))

    # -- console ---------------------------------------------------------
    def console(self) -> None:
        help_text = ("commands: unplug <BED>, plug <BED>, announce, status, quit")

        # No console at all when stdin is not a terminal - started from a
        # script, or with stdin redirected from /dev/null. Reading it would hit
        # EOF on the first line and shut the simulator down immediately, which
        # is not what "run it in the background and drive the browser" means.
        if not sys.stdin.isatty():
            print("[gw] stdin is not a terminal - running without the command "
                  "console. Stop with Ctrl+C or by killing the process.")
            while self.running:
                time.sleep(0.5)
            return

        print(help_text)
        for line in sys.stdin:
            parts = line.split()
            if not parts:
                continue
            verb = parts[0].lower()

            if verb in ("quit", "exit"):
                self.running = False
                return
            if verb == "status":
                for d in self.devices.values():
                    print(f"  {d.bed_id:10s} {d.device_id:16s} "
                          f"{'plugged' if d.plugged else 'UNPLUGGED':10s} "
                          f"target={d.target_drops_per_min:3d} dpm  "
                          f"test_mode={d.vitals_test_mode}  samples={d.samples}")
                print(f"  last publisher: {self.last_published}")
                continue
            if verb == "announce":
                self.announced.clear()
                print("[gw] will re-announce every device on its next reading")
                continue
            if verb in ("unplug", "plug") and len(parts) == 2:
                bed = parts[1].upper()
                device = self.devices.get(bed)
                if device is None:
                    print(f"  no such bed: {bed} (have {', '.join(self.devices)})")
                    continue
                device.plugged = (verb == "plug")
                print(f"  {bed} {'plugged in' if device.plugged else 'UNPLUGGED'}"
                      + ("" if device.plugged
                         else " - it should go Offline once the threshold passes"))
                continue

            print(help_text)


def main() -> int:
    # Line-buffered, because the useful way to run this is with its output
    # redirected to a file while you drive the UI in a browser - and a block
    # buffer means the command trace only shows up after the run is over, which
    # is exactly when it has stopped being useful.
    try:
        sys.stdout.reconfigure(line_buffering=True)
    except AttributeError:
        pass

    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--beds", default=",".join(DEFAULT_BEDS),
                        help="comma-separated bed ids (default: %(default)s)")
    parser.add_argument("--legacy", action="store_true",
                        help="behave like a Pi still running the OLD converter and "
                             "gateway: omit every field added since, and re-announce "
                             "the way the single-slot gateway did")
    args = parser.parse_args()

    beds = [b.strip().upper() for b in args.beds.split(",") if b.strip()]
    if not beds:
        print("need at least one bed", file=sys.stderr)
        return 2

    gw = FakeGateway(args.host, args.port, beds, legacy=args.legacy)
    print(f"[gw] one gateway carrying {len(beds)} bed(s): {', '.join(beds)}")
    if args.legacy:
        print("[gw] LEGACY mode: pretending to be a Pi that has not been updated - "
              "no AI-input/level/training fields, old announce behaviour")
    if len(beds) == 1:
        print("[gw] note: with a single bed the multi-device bugs cannot appear - "
              "use two or more to test command targeting")

    threading.Thread(target=gw.run, daemon=True).start()
    try:
        gw.console()
    except KeyboardInterrupt:
        pass
    gw.running = False
    print("\n[gw] stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

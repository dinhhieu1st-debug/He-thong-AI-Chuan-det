#!/usr/bin/env python3
"""Fake bedside devices for demoing the dashboard without real hardware.

Opens one persistent TCP connection per bed to the HIS Server's ingestion
port and streams newline-delimited JSON readings, matching the wire format
in server/src/HisServer/Ingestion/BedDataParser.cs. Not part of the shipped
system - only for local demo/dev use.

Usage:
    python3 tools/mock_gateway.py [host] [port]
"""
import json
import math
import random
import socket
import sys
import threading
import time

HOST = sys.argv[1] if len(sys.argv) > 1 else "localhost"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 5000

BEDS = [
    {"bedId": "BED-101", "room": "ICU-1", "scenario": "normal"},
    {"bedId": "BED-102", "room": "ICU-1", "scenario": "normal"},
    {"bedId": "BED-103", "room": "ICU-1", "scenario": "desat"},   # slides into SpO2 warning/critical
    {"bedId": "BED-201", "room": "ICU-2", "scenario": "normal"},
    {"bedId": "BED-202", "room": "ICU-2", "scenario": "line_blocked"},
    {"bedId": "BED-203", "room": "ICU-2", "scenario": "normal"},
    {"bedId": "BED-301", "room": "Ward-A", "scenario": "normal"},
    {"bedId": "BED-302", "room": "Ward-A", "scenario": "normal"},
]


def make_reading(bed, t):
    spo2 = 97 + round(math.sin(t / 20 + hash(bed["bedId"]) % 10) * 1.2)
    hr = 76 + round(math.sin(t / 15) * 4)
    drip = 20 + round(math.sin(t / 30) * 2)
    weight = 480 - int(t * 0.05) % 400
    line_blocked = False

    if bed["scenario"] == "desat":
        # Slow, steady desaturation starting a couple minutes in, then a partial
        # recovery, so the demo shows Warning -> Critical -> recovering.
        cycle = t % 240
        if cycle < 60:
            spo2 = 97
        elif cycle < 150:
            spo2 = max(84, 97 - int((cycle - 60) / 4))
        else:
            spo2 = min(97, 84 + int((cycle - 150) / 5))
        hr = hr + max(0, (97 - spo2)) * 2

    if bed["scenario"] == "line_blocked":
        if (t % 180) > 90:
            line_blocked = True
            drip = 0

    spo2 = max(70, min(100, spo2 + random.randint(-1, 1)))
    hr = max(45, min(160, hr + random.randint(-2, 2)))
    drip = max(0, drip + random.randint(-1, 1))

    return {
        "bedId": bed["bedId"],
        "room": bed["room"],
        "spo2": spo2,
        "heartRate": hr,
        "dripRate": drip,
        "flowRate": drip,
        "weightG": weight,
        "dropsPerMin": drip,
        "targetFlowMlH": 100,
        "lineBlocked": line_blocked,
        "aeAlarm": False,
        "hrSignal": True,
        "spo2Signal": True,
        "flowSignal": True,
        "dropsSignal": True,
        "linkQuality": random.randint(150, 220),
    }


def run_bed(bed):
    while True:
        try:
            with socket.create_connection((HOST, PORT), timeout=5) as sock:
                print(f"[{bed['bedId']}] connected")
                t = 0
                while True:
                    line = json.dumps(make_reading(bed, t)) + "\n"
                    sock.sendall(line.encode("utf-8"))
                    t += 1
                    time.sleep(1)
        except (ConnectionRefusedError, OSError) as e:
            print(f"[{bed['bedId']}] {e}, retrying in 3s")
            time.sleep(3)


def main():
    print(f"Streaming mock vitals to {HOST}:{PORT} for {len(BEDS)} beds. Ctrl+C to stop.")
    threads = [threading.Thread(target=run_bed, args=(bed,), daemon=True) for bed in BEDS]
    for th in threads:
        th.start()
    for th in threads:
        th.join()


if __name__ == "__main__":
    main()

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

# Must match LINE_DROP_FACTOR_GTT_PER_ML in firmware/line_rules.h - it is the
# constant that converts a drop rate into a weight-loss rate, and the two
# sides of that conversion have to agree or the load cell and the drop
# sensor never will either.
LINE_DROP_FACTOR_GTT_PER_ML = 20.0

BEDS = [
    {"bedId": "BED-101", "room": "ICU-1", "scenario": "normal"},
    {"bedId": "BED-102", "room": "ICU-1", "scenario": "normal"},
    {"bedId": "BED-103", "room": "ICU-1", "scenario": "desat"},   # slides into SpO2 warning/critical
    {"bedId": "BED-201", "room": "ICU-2", "scenario": "normal"},
    {"bedId": "BED-202", "room": "ICU-2", "scenario": "line_blocked"},
    {"bedId": "BED-203", "room": "ICU-2", "scenario": "normal"},
    {"bedId": "BED-301", "room": "Ward-A", "scenario": "normal"},
    {"bedId": "BED-302", "room": "Ward-A", "scenario": "normal"},
    # Cycles standby -> armed every 3 minutes, so the nurse can demo "Start
    # monitoring" on a fresh cycle without restarting the script.
    {"bedId": "BED-401", "room": "ICU-2", "scenario": "standby_arm"},
    # Cycles normal -> line-only warning -> patient critical -> BOTH at once
    # (device AlertLevel 3), so the dual "CRITICAL (line + patient)" banner
    # and its distinct buzzer cadence can be demoed on request.
    {"bedId": "BED-402", "room": "ICU-1", "scenario": "critical_dual"},
]

# deviceId -> bedId, matching the devices created via the admin API during the
# demo setup (DEV-101 assigned to BED-101, etc). Only beds with an entry here
# can answer ota_check/ota_update - real hardware would be the same: only a
# device that has actually announced itself is reachable.
DEVICE_BED = {
    "DEV-101": "BED-101",
    "DEV-103": "BED-103",
    "DEV-202": "BED-202",
    "DEV-401": "BED-401",
    "DEV-402": "BED-402",
}
BED_DEVICE = {bed_id: dev_id for dev_id, bed_id in DEVICE_BED.items()}
OTA_INSTALLED_VERSION = 8
OTA_LATEST_VERSION = 9


# Manual override from the "Pause monitoring" / "Start monitoring" button,
# which sends {"cmd":"set_monitoring","value":0|1} down the same socket real
# firmware listens on. None = no override, follow the scripted scenario.
manual_monitoring = {}
manual_monitoring_lock = threading.Lock()

# Per-bed state driven by the "Doctor-configurable settings" panel: tare
# (Reset scale), HR baseline recalibration (60s), and the AI drop-rate/flow
# targets. Mirrors what real firmware would report back over the same wire
# format - see BedTcpIngestionService, which keys off the *_EventCount
# fields (not the one-shot *_just_completed flags) so a completion can never
# be missed even if this mock's tick is slow.
bed_settings_lock = threading.Lock()
bed_settings = {}

# Bag weight, one running float per bed instead of a function of t. A real
# load cell only ever loses weight as fluid leaves (or jumps back up to a
# full bag on tare, i.e. a fresh bag being hung) - it never has a reason to
# reset itself mid-infusion. `weight = 480 - int(t*0.05) % 400` used to do
# exactly that every ~133 minutes, and its slope was a constant 3 g/min with
# no relationship to the bed's actual drip rate, so the load-cell numbers
# and the drop-sensor numbers disagreed with each other by construction -
# which is what line_rules.c cross-checks them for in the first place.
BAG_FULL_G = 500.0
bag_weight_lock = threading.Lock()
bag_weight = {}


def _weight_for(bed_id):
    with bag_weight_lock:
        return bag_weight.setdefault(bed_id, BAG_FULL_G)


def _drain_weight(bed_id, drops_per_min, dt_s):
    """Advance this bed's bag weight by dt_s seconds at the rate the counted
    drops imply (1 mL/20 gtt ~= 1 g/20 gtt), same conversion firmware uses
    in line_rules.c. Standing still when drops_per_min is 0 mirrors a real
    bag: no drops, no weight loss - that agreement is what lets the
    occlusion rule tell "blocked" apart from "just measuring noise"."""
    with bag_weight_lock:
        g_per_s = (drops_per_min / LINE_DROP_FACTOR_GTT_PER_ML) / 60.0
        w = bag_weight.get(bed_id, BAG_FULL_G) - g_per_s * dt_s
        bag_weight[bed_id] = max(0.0, w)
        return bag_weight[bed_id]


def _reset_weight(bed_id):
    with bag_weight_lock:
        bag_weight[bed_id] = BAG_FULL_G


def _settings_for(bed_id):
    with bed_settings_lock:
        return bed_settings.setdefault(bed_id, {
            "tare_started_at": None, "tare_event_count": 0,
            "hr_baseline_started_at": None, "hr_baseline_event_count": 0,
            "target_drops": None, "target_flow": 100,
        })


def send_line(sock, send_lock, obj):
    line = (json.dumps(obj) + "\n").encode("utf-8")
    with send_lock:
        sock.sendall(line)


def ota_status(device_id, state, **fields):
    msg = {"type": "ota_status", "deviceId": device_id, "state": state}
    msg.update(fields)
    return msg


def run_ota_check(sock, send_lock, device_id):
    """Fakes what zigbee2mqtt would eventually report back for a check -
    no real .ota file is needed since the server only forwards the command
    and trusts whatever ota_status the gateway reports."""
    time.sleep(1.5)
    send_line(sock, send_lock, ota_status(
        device_id, "available",
        installedVersion=OTA_INSTALLED_VERSION, latestVersion=OTA_LATEST_VERSION,
        message="Update available"))
    print(f"[{device_id}] ota_check -> available (v{OTA_INSTALLED_VERSION} -> v{OTA_LATEST_VERSION})")


def run_ota_update(sock, send_lock, device_id):
    """Fakes a full transfer: starting -> updating (0-100%) -> done, at
    roughly the cadence a real transfer reports progress."""
    send_line(sock, send_lock, ota_status(device_id, "starting", message="Update started"))
    time.sleep(1)
    total = 12
    for i in range(total + 1):
        pct = round(i * 100 / total)
        send_line(sock, send_lock, ota_status(
            device_id, "updating", progress=pct, remainingSeconds=(total - i) * 2,
            message="Installing firmware - do not disconnect power."))
        time.sleep(2)
    send_line(sock, send_lock, ota_status(
        device_id, "done",
        installedVersion=OTA_LATEST_VERSION, latestVersion=OTA_LATEST_VERSION,
        message=f"Updated: v{OTA_INSTALLED_VERSION} to v{OTA_LATEST_VERSION}"))
    print(f"[{device_id}] ota_update -> done")


def read_commands(sock, send_lock, bed_id):
    """Background reader: applies set_monitoring commands, and fakes OTA
    check/update responses, so the dashboard's buttons work against the
    mock the same way they would against a real gateway on this socket."""
    device_id = BED_DEVICE.get(bed_id)
    buf = b""
    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                return
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if not line.strip():
                    continue
                try:
                    cmd = json.loads(line)
                except json.JSONDecodeError:
                    continue

                if cmd.get("cmd") == "set_monitoring":
                    enabled = bool(cmd.get("value"))
                    with manual_monitoring_lock:
                        manual_monitoring[bed_id] = enabled
                    print(f"[{bed_id}] set_monitoring -> {enabled} (from dashboard)")

                elif cmd.get("cmd") == "reset_tare":
                    s = _settings_for(bed_id)
                    with bed_settings_lock:
                        s["tare_started_at"] = time.time()
                    print(f"[{bed_id}] reset_tare -> taring for 3s")

                elif cmd.get("cmd") == "recalibrate_hr_baseline":
                    s = _settings_for(bed_id)
                    with bed_settings_lock:
                        s["hr_baseline_started_at"] = time.time()
                    print(f"[{bed_id}] recalibrate_hr_baseline -> measuring for 60s")

                elif cmd.get("cmd") == "set_target_drops_per_min":
                    s = _settings_for(bed_id)
                    with bed_settings_lock:
                        s["target_drops"] = cmd.get("value")
                    print(f"[{bed_id}] set_target_drops_per_min -> {cmd.get('value')}")

                elif cmd.get("cmd") == "set_target_flow_ml_h":
                    s = _settings_for(bed_id)
                    with bed_settings_lock:
                        s["target_flow"] = cmd.get("value")
                    print(f"[{bed_id}] set_target_flow_ml_h -> {cmd.get('value')}")

                elif cmd.get("cmd") == "ota_check" and cmd.get("deviceId") == device_id:
                    threading.Thread(target=run_ota_check, args=(sock, send_lock, device_id), daemon=True).start()

                elif cmd.get("cmd") == "ota_update" and cmd.get("deviceId") == device_id:
                    threading.Thread(target=run_ota_update, args=(sock, send_lock, device_id), daemon=True).start()
    except OSError:
        return


def make_reading(bed, t):
    spo2 = 97 + round(math.sin(t / 20 + hash(bed["bedId"]) % 10) * 1.2)
    hr = 76 + round(math.sin(t / 15) * 4)
    drip = 20 + round(math.sin(t / 30) * 2)
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

    # Standby/armed toggle. Vitals still flow the whole time (matches real
    # firmware behaviour - only the AI/alarm judgement is withheld while
    # monitoring=False), only the "monitoring" flag cycles.
    monitoring = True
    if bed["scenario"] == "standby_arm":
        cycle = t % 180
        monitoring = cycle >= 60   # 0-59s STANDBY, 60-179s MONITORING

    with manual_monitoring_lock:
        override = manual_monitoring.get(bed["bedId"])
    if override is not None:
        monitoring = override

    # Device-reported AlertLevel: 0 stable, 1 line-only warning, 2 patient
    # critical, 3 both at once. Sent explicitly so the dual-critical banner
    # and buzzer cadence can be demoed without waiting on hysteresis.
    alert_level = None
    if bed["scenario"] == "critical_dual":
        cycle = t % 200
        if cycle < 50:
            alert_level = 0
        elif cycle < 100:
            alert_level = 1                       # line only: yellow, silent
            line_blocked = True
            drip = 0
        elif cycle < 150:
            alert_level = 2                       # patient only: red, buzzer
            spo2 = 82
        else:
            alert_level = 3                       # BOTH: dual critical banner
            spo2 = 82
            line_blocked = True
            drip = 0

    spo2 = max(70, min(100, spo2 + random.randint(-1, 1)))
    hr = max(45, min(160, hr + random.randint(-2, 2)))
    drip = max(0, drip + random.randint(-1, 1))

    # --- Tare / HR baseline / targets, driven by the settings panel's
    # buttons via read_commands() above. ---------------------------------
    s = _settings_for(bed["bedId"])
    now = time.time()
    tare_in_progress = False
    tare_just_completed = False
    with bed_settings_lock:
        if s["tare_started_at"] is not None:
            if now - s["tare_started_at"] < 3:
                tare_in_progress = True
            else:
                s["tare_started_at"] = None
                s["tare_event_count"] += 1
                tare_just_completed = True

        hr_baseline_seconds_remaining = None
        hr_baseline_bpm = None
        hr_baseline_just_completed = False
        if s["hr_baseline_started_at"] is not None:
            remaining = 60 - int(now - s["hr_baseline_started_at"])
            if remaining > 0:
                hr_baseline_seconds_remaining = remaining
            else:
                s["hr_baseline_started_at"] = None
                s["hr_baseline_event_count"] += 1
                hr_baseline_just_completed = True
        if s["hr_baseline_event_count"] > 0:
            hr_baseline_bpm = hr

        tare_event_count = s["tare_event_count"]
        hr_baseline_event_count = s["hr_baseline_event_count"]
        target_drops = s["target_drops"]
        target_flow = s["target_flow"]

    # --- Infusion line (load cell) verdict -----------------------------
    # 0 ok, 1 bag running low, 2 blocked, 3 free-flow, 4 sensor fault, 5 empty.
    # Real firmware only reports this once it has watched the weight trend
    # for a while; mimic that so the panel's empty state is demoed too.
    if tare_just_completed:
        _reset_weight(bed["bedId"])   # fresh bag hung and tared
    weight = _drain_weight(bed["bedId"], 0.0 if line_blocked else drip, dt_s=1.0)

    line_ready = t >= 20
    if not line_ready:
        line_state = None
    elif line_blocked:
        line_state = 2
    elif weight < 60:
        line_state = 5
    elif weight < 120:
        line_state = 1
    else:
        line_state = 0
    weight = round(weight)          # wire format is an integer field
    remaining_ml = weight
    remaining_min = int(remaining_ml // max(drip, 1)) if line_ready and drip > 0 else None

    # --- AI forecast (on-chip): needs the same 64s history window as the
    # real firmware before it has anything to say. ------------------------
    ts_ready = t >= 64
    hr_forecast = hr + random.randint(-2, 2)
    spo2_forecast = spo2 + random.randint(-1, 1)
    drops_forecast = drip + random.randint(-1, 1)
    hr_trend_rate = random.randint(-3, 3)
    drops_trend_rate = random.randint(-2, 2)
    ts_trend = 1 if hr_trend_rate > 1 else (2 if hr_trend_rate < -1 else 0)
    drops_trend = 2 if drops_trend_rate < -1 else (1 if drops_trend_rate > 1 else 0)
    anomaly_score = random.randint(20, 90)
    ts_anomaly = False
    ts_early_warning = False

    if bed["scenario"] == "desat" and spo2 < 90:
        anomaly_score = random.randint(560, 780)
        ts_anomaly = True
        ts_trend = 2  # SpO2 falling shows as a worrying HR-forecast-panel trend too
        hr_trend_rate = random.randint(4, 9)
    elif bed["scenario"] == "desat" and spo2 < 94:
        ts_early_warning = True

    if bed["scenario"] == "critical_dual" and alert_level in (2, 3):
        anomaly_score = random.randint(560, 780)
        ts_anomaly = True

    reading = {
        "bedId": bed["bedId"],
        "room": bed["room"],
        "spo2": spo2,
        "heartRate": hr,
        "dripRate": drip,
        "flowRate": drip,
        "weightG": weight,
        "dropsPerMin": drip,
        "targetFlowMlH": target_flow,
        "targetDropsPerMin": target_drops,
        "lineBlocked": line_blocked,
        "aeAlarm": False,
        "hrSignal": True,
        "spo2Signal": True,
        "flowSignal": True,
        "dropsSignal": True,
        "linkQuality": random.randint(150, 220),
        "lineState": line_state if line_state is not None else -1,
        "remainingMl": remaining_ml if line_ready else -1,
        "remainingMin": remaining_min if remaining_min is not None else -1,
        "tsReady": ts_ready,
        "tsAnomaly": ts_anomaly,
        "tsEarlyWarning": ts_early_warning,
        "tsTrend": ts_trend,
        "hrForecast16s": hr_forecast,
        "spo2Forecast16s": spo2_forecast,
        "hrTrendBpmPerMin": hr_trend_rate,
        "tsAnomalyScore": anomaly_score,
        "dropsTrend": drops_trend,
        "dropsTrendDpmPerMin": drops_trend_rate,
        "dropsForecast16s": drops_forecast,
        "dripAnomaly": bed["scenario"] == "line_blocked" and line_blocked,
        "vitalsAnomaly": ts_anomaly,
        "monitoring": monitoring,
        "tareInProgress": tare_in_progress,
        "tareJustCompleted": tare_just_completed,
        "tareEventCount": tare_event_count,
        "hrBaselineSecondsRemaining": hr_baseline_seconds_remaining,
        "hrBaselineJustCompleted": hr_baseline_just_completed,
        "hrBaselineBpm": hr_baseline_bpm,
        "hrBaselineEventCount": hr_baseline_event_count,
    }
    if alert_level is not None:
        reading["alertLevel"] = alert_level
    return reading


def run_bed(bed):
    while True:
        try:
            with socket.create_connection((HOST, PORT), timeout=5) as sock:
                print(f"[{bed['bedId']}] connected")
                sock.settimeout(None)
                send_lock = threading.Lock()
                threading.Thread(target=read_commands, args=(sock, send_lock, bed["bedId"]), daemon=True).start()
                t = 0
                while True:
                    send_line(sock, send_lock, make_reading(bed, t))
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

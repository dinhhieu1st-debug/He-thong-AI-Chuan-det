"""Log valid ESP8266 drop records to a session-based RAW CSV file."""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import serial
from serial import SerialException


RAW_HEADER = (
    "timestamp_ms",
    "drop_number",
    "target_interval_ms",
    "actual_interval_ms",
)
RAW_PATTERN = re.compile(r"^\s*(\d+),(\d+),(\d+),(\d+)\s*$")
SESSION_PATTERN = re.compile(r"^session_(\d+)$")
PRESET_TARGETS_MS = {"slow": 1500, "normal": 1000, "fast": 750}


@dataclass(frozen=True)
class RawRecord:
    timestamp_ms: int
    drop_number: int
    target_interval_ms: int
    actual_interval_ms: int

    def as_row(self) -> tuple[int, int, int, int]:
        return (
            self.timestamp_ms,
            self.drop_number,
            self.target_interval_ms,
            self.actual_interval_ms,
        )


def utc_now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def console_safe(text: str) -> str:
    encoding = sys.stdout.encoding or "utf-8"
    return text.encode(encoding, errors="backslashreplace").decode(encoding)


def parse_raw_line(line: str) -> RawRecord | None:
    match = RAW_PATTERN.fullmatch(line)
    if not match:
        return None

    record = RawRecord(*(int(value) for value in match.groups()))
    if (
        record.timestamp_ms <= 0
        or record.drop_number < 2
        or record.target_interval_ms <= 0
        or record.actual_interval_ms <= 0
    ):
        return None
    return record


def next_session_id(raw_dir: Path, metadata_dir: Path) -> str:
    used_numbers: set[int] = set()
    for directory, suffix in ((raw_dir, ".csv"), (metadata_dir, ".json")):
        if not directory.exists():
            continue
        for path in directory.glob(f"session_*{suffix}"):
            match = SESSION_PATTERN.fullmatch(path.stem)
            if match:
                used_numbers.add(int(match.group(1)))

    number = max(used_numbers, default=0) + 1
    return f"session_{number:03d}"


def validate_session_id(session_id: str) -> str:
    if not SESSION_PATTERN.fullmatch(session_id):
        raise ValueError("session_id must have the form session_001")
    return session_id


def write_metadata(path: Path, metadata: dict[str, object]) -> None:
    temporary_path = path.with_suffix(".json.tmp")
    temporary_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary_path.replace(path)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Record clean RAW drop data from ESP8266 Serial."
    )
    parser.add_argument("--port", default="COM5", help="Serial port (default: COM5)")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument(
        "--session-id",
        help="Explicit ID such as session_001; omitted means next available ID.",
    )
    parser.add_argument("--target-interval-ms", type=int, default=1000)
    parser.add_argument(
        "--preset",
        choices=("slow", "normal", "fast", "custom"),
        default="custom",
    )
    parser.add_argument(
        "--drop-factor-gtt-per-ml",
        type=int,
        help="Drop factor printed on the tubing package; omit if unknown.",
    )
    parser.add_argument("--condition", default="unspecified")
    parser.add_argument("--notes", default="")
    parser.add_argument("--reconnect-delay", type=float, default=2.0)
    parser.add_argument(
        "--max-records",
        type=int,
        help="Stop automatically after this many valid records.",
    )
    parser.add_argument(
        "--duration",
        type=float,
        help="Stop after this many seconds, including reconnect time.",
    )
    parser.add_argument(
        "--show-debug",
        action="store_true",
        help="Also print ignored firmware debug lines.",
    )
    return parser


def run(args: argparse.Namespace) -> int:
    project_root = Path(__file__).resolve().parents[1]
    raw_dir = project_root / "data" / "raw"
    metadata_dir = project_root / "data" / "sessions"
    raw_dir.mkdir(parents=True, exist_ok=True)
    metadata_dir.mkdir(parents=True, exist_ok=True)

    session_id = (
        validate_session_id(args.session_id)
        if args.session_id
        else next_session_id(raw_dir, metadata_dir)
    )
    csv_path = raw_dir / f"{session_id}.csv"
    metadata_path = metadata_dir / f"{session_id}.json"
    if csv_path.exists() or metadata_path.exists():
        raise FileExistsError(f"Session already exists: {session_id}")

    started_monotonic = time.monotonic()
    metadata: dict[str, object] = {
        "session_id": session_id,
        "source_type": "real_sensor",
        "synthetic": False,
        "usable_for_training": True,
        "port": args.port,
        "baudrate": args.baudrate,
        "target_interval_ms": args.target_interval_ms,
        "target_drops_per_minute": round(60000 / args.target_interval_ms, 3),
        "preset": args.preset,
        "drop_factor_gtt_per_ml": args.drop_factor_gtt_per_ml,
        "start_time": utc_now(),
        "end_time": None,
        "test_condition": args.condition,
        "notes": args.notes,
        "status": "recording",
        "csv_file": str(csv_path.relative_to(project_root)).replace("\\", "/"),
        "records_written": 0,
        "ignored_lines": 0,
        "connection_errors": 0,
        "target_mismatch_records": 0,
        "first_timestamp_ms": None,
        "last_timestamp_ms": None,
        "first_drop_number": None,
        "last_drop_number": None,
    }
    write_metadata(metadata_path, metadata)

    print(f"[SESSION] {session_id}")
    print(f"[CSV]     {csv_path}")
    print(f"[META]    {metadata_path}")
    print(f"[SERIAL]  {args.port} @ {args.baudrate}")
    print("[STOP]    Press Ctrl+C")

    serial_port: serial.Serial | None = None
    exit_status = "completed"

    try:
        with csv_path.open("x", newline="", encoding="utf-8") as csv_file:
            writer = csv.writer(csv_file, lineterminator="\n")
            writer.writerow(RAW_HEADER)
            csv_file.flush()
            os.fsync(csv_file.fileno())

            while True:
                if args.duration is not None:
                    if time.monotonic() - started_monotonic >= args.duration:
                        break
                if args.max_records is not None:
                    if metadata["records_written"] >= args.max_records:
                        break

                if serial_port is None or not serial_port.is_open:
                    try:
                        serial_port = serial.Serial(
                            port=args.port,
                            baudrate=args.baudrate,
                            timeout=1.0,
                            write_timeout=1.0,
                            rtscts=False,
                            dsrdtr=False,
                        )
                        serial_port.dtr = False
                        serial_port.rts = False
                        print(f"[CONNECTED] {args.port}")
                    except (SerialException, OSError) as exc:
                        metadata["connection_errors"] += 1
                        write_metadata(metadata_path, metadata)
                        print(f"[RECONNECT] {exc}", file=sys.stderr)
                        time.sleep(max(args.reconnect_delay, 0.1))
                        continue

                try:
                    raw_bytes = serial_port.readline()
                    if not raw_bytes:
                        continue
                    line = raw_bytes.decode("utf-8", errors="replace").strip()
                except (SerialException, OSError) as exc:
                    metadata["connection_errors"] += 1
                    write_metadata(metadata_path, metadata)
                    print(f"[DISCONNECTED] {exc}", file=sys.stderr)
                    try:
                        serial_port.close()
                    except (SerialException, OSError):
                        pass
                    serial_port = None
                    time.sleep(max(args.reconnect_delay, 0.1))
                    continue

                record = parse_raw_line(line)
                if record is None:
                    metadata["ignored_lines"] += 1
                    if args.show_debug and line:
                        print(f"[DEBUG] {console_safe(line)}")
                    continue

                writer.writerow(record.as_row())
                csv_file.flush()
                os.fsync(csv_file.fileno())

                metadata["records_written"] += 1
                if record.target_interval_ms != args.target_interval_ms:
                    metadata["target_mismatch_records"] += 1
                    print(
                        "[WARNING] Firmware target does not match session target: "
                        f"{record.target_interval_ms} != {args.target_interval_ms}",
                        file=sys.stderr,
                    )
                if metadata["first_timestamp_ms"] is None:
                    metadata["first_timestamp_ms"] = record.timestamp_ms
                    metadata["first_drop_number"] = record.drop_number
                metadata["last_timestamp_ms"] = record.timestamp_ms
                metadata["last_drop_number"] = record.drop_number
                write_metadata(metadata_path, metadata)
                print(
                    f"[RAW {metadata['records_written']:04d}] "
                    + ",".join(str(value) for value in record.as_row())
                )
    except KeyboardInterrupt:
        print("\n[STOPPED] Ctrl+C received")
    except Exception:
        exit_status = "error"
        raise
    finally:
        if serial_port is not None and serial_port.is_open:
            serial_port.close()
        metadata["status"] = exit_status
        metadata["end_time"] = utc_now()
        write_metadata(metadata_path, metadata)
        print(f"[SAVED] {metadata['records_written']} records -> {csv_path}")

    return 0


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.target_interval_ms <= 0:
        parser.error("--target-interval-ms must be positive")
    expected_target = PRESET_TARGETS_MS.get(args.preset)
    if expected_target is not None and args.target_interval_ms != expected_target:
        parser.error(
            f"preset {args.preset!r} requires --target-interval-ms {expected_target}"
        )
    if args.drop_factor_gtt_per_ml is not None and args.drop_factor_gtt_per_ml <= 0:
        parser.error("--drop-factor-gtt-per-ml must be positive")
    if args.max_records is not None and args.max_records <= 0:
        parser.error("--max-records must be positive")
    if args.duration is not None and args.duration <= 0:
        parser.error("--duration must be positive")

    try:
        return run(args)
    except (FileExistsError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

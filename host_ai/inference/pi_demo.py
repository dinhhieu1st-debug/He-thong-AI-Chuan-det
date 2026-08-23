"""Raspberry Pi shadow demo for MLP + LSTM IV-drip inference.

This is a research prototype. AI predictions are displayed and logged only; a
separate elapsed-time watchdog handles the absence of a next drop.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import queue
import sys
import threading
import time
from collections import deque
from pathlib import Path
from typing import Iterator

import numpy as np
import onnxruntime as ort


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MLP = ROOT / "models" / "mlp_baseline" / "model_numpy.json"
DEFAULT_LSTM = ROOT / "models" / "lstm" / "model.onnx"
DEFAULT_LSTM_CONFIG = ROOT / "models" / "lstm" / "inference_config.json"
LABEL_NAMES = {1: "NORMAL", 2: "ATTENTION", 3: "WARNING"}
PRESET_TARGETS_MS = {"slow": 1500, "normal": 1000, "fast": 750}
PRESET_KEYS = {
    "1": ("slow", 1500),
    "2": ("normal", 1000),
    "3": ("fast", 750),
}


def print_system_output(level: int, reason: str, source: str) -> None:
    print("=" * 72)
    print(f"[SYSTEM OUTPUT] LEVEL {level} - {LABEL_NAMES[level]}")
    print(f"[SOURCE] {source}")
    print(f"[REASON] {reason}")
    print("=" * 72)


def print_preset_menu() -> None:
    print("=" * 72)
    print(" CHINH GIOT TRUOC, SAU DO CHON CHE DO BAT KY LUC NAO")
    print("  1) CHAM        - 1500 ms/giot - 40 giot/phut")
    print("  2) BINH THUONG - 1000 ms/giot - 60 giot/phut")
    print("  3) NHANH       -  750 ms/giot - 80 giot/phut")
    print(" Go 1, 2 hoac 3 roi nhan Enter. Go q roi Enter de dung.")
    print("=" * 72)


def input_worker(commands: queue.Queue[str]) -> None:
    while True:
        line = sys.stdin.readline()
        if line == "":
            return
        commands.put(line.strip().lower())


def softmax(logits: np.ndarray) -> np.ndarray:
    shifted = logits - np.max(logits, axis=-1, keepdims=True)
    values = np.exp(shifted)
    return values / np.sum(values, axis=-1, keepdims=True)


class NumpyMLP:
    def __init__(self, path: Path):
        artifact = json.loads(path.read_text(encoding="utf-8"))
        if artifact.get("format") != "iv_drip_numpy_mlp_v1":
            raise ValueError(f"Unsupported MLP artifact: {path}")
        self.classes = np.asarray(artifact["classes"], dtype=np.int64)
        self.mean = np.asarray(artifact["scaler_mean"], dtype=np.float32)
        self.scale = np.asarray(artifact["scaler_scale"], dtype=np.float32)
        self.weights = [np.asarray(value, dtype=np.float32) for value in artifact["weights"]]
        self.biases = [np.asarray(value, dtype=np.float32) for value in artifact["biases"]]

    def predict(self, sequence: np.ndarray) -> tuple[int, np.ndarray]:
        # Training CSV order is feature-major, not row-major flattening.
        flat = np.concatenate([sequence[:, index] for index in range(3)]).astype(np.float32)
        value = (flat - self.mean) / self.scale
        for layer, (weights, bias) in enumerate(zip(self.weights, self.biases)):
            value = value @ weights + bias
            if layer < len(self.weights) - 1:
                value = np.maximum(value, 0.0)
        probabilities = softmax(value)
        winner = int(self.classes[int(np.argmax(probabilities))])
        return winner, probabilities


class OnnxLSTM:
    def __init__(self, model_path: Path, config_path: Path):
        config = json.loads(config_path.read_text(encoding="utf-8"))
        self.mean = np.asarray(config["feature_mean"], dtype=np.float32)
        self.std = np.asarray(config["feature_std"], dtype=np.float32)
        self.input_name = config["onnx_input"]
        self.output_name = config["onnx_output"]
        session_options = ort.SessionOptions()
        session_options.log_severity_level = 3
        self.session = ort.InferenceSession(
            str(model_path),
            sess_options=session_options,
            providers=["CPUExecutionProvider"],
        )

    def predict(self, sequence: np.ndarray) -> tuple[int, np.ndarray]:
        normalized = ((sequence - self.mean) / self.std)[None, :, :].astype(np.float32)
        logits = self.session.run([self.output_name], {self.input_name: normalized})[0][0]
        probabilities = softmax(logits)
        return int(np.argmax(probabilities)) + 1, probabilities


def build_sequence(ratios: deque[float]) -> np.ndarray:
    values = np.asarray(ratios, dtype=np.float32)
    if len(values) != 20:
        raise ValueError("Exactly 20 ratios are required")
    errors = (values - 1.0) * 100.0
    deltas = np.concatenate([np.zeros(1, dtype=np.float32), np.diff(values)])
    return np.stack([values, errors, deltas], axis=1)


def parse_raw_line(line: str) -> tuple[int, int, int, int] | None:
    parts = [part.strip() for part in line.strip().split(",")]
    if len(parts) != 4:
        return None
    try:
        values = tuple(int(part) for part in parts)
    except ValueError:
        return None
    timestamp_ms, drop_number, target_ms, actual_ms = values
    if min(values) < 0 or target_ms == 0 or drop_number == 0:
        return None
    return timestamp_ms, drop_number, target_ms, actual_ms


def replay_rows(path: Path) -> Iterator[tuple[int, int, int, int]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        required = {"timestamp_ms", "drop_number", "target_interval_ms", "actual_interval_ms"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(f"Replay CSV missing columns: {sorted(required)}")
        for row in reader:
            yield (
                int(row["timestamp_ms"]),
                int(row["drop_number"]),
                int(row["target_interval_ms"]),
                int(row["actual_interval_ms"]),
            )


def format_prediction(name: str, label: int, probabilities: np.ndarray) -> str:
    confidence = float(probabilities[label - 1]) * 100.0
    return f"{name}={label}:{LABEL_NAMES[label]}({confidence:5.1f}%)"


def handle_drop(
    row: tuple[int, int, int, int],
    ratios: deque[float],
    current_target: int | None,
    mlp: NumpyMLP,
    lstm: OnnxLSTM,
    watchdog_multiplier: float,
) -> int:
    _, drop_number, target_ms, actual_ms = row
    if current_target is not None and target_ms != current_target:
        ratios.clear()
        print(f"[SET_CHANGE] {current_target} -> {target_ms} ms; reset 20-drop buffer")
    ratio = actual_ms / target_ms
    ratios.append(ratio)
    watchdog = actual_ms >= watchdog_multiplier * target_ms
    prefix = (
        f"drop={drop_number:04d} set={target_ms}ms actual={actual_ms}ms "
        f"ratio={ratio:.3f}"
    )
    if watchdog:
        print(f"[WATCHDOG WARNING] {prefix} threshold={watchdog_multiplier:.1f}x")
        print_system_output(
            3,
            f"Khoang cach {actual_ms} ms >= {watchdog_multiplier:.1f} x target {target_ms} ms",
            "WATCHDOG (uu tien cao hon AI)",
        )
    if len(ratios) < 20:
        print(
            f"[WARMUP {len(ratios):02d}/20] {prefix} | "
            "AI=CHUA_DU_20_GIOT"
        )
        return target_ms

    sequence = build_sequence(ratios)
    mlp_label, mlp_prob = mlp.predict(sequence)
    lstm_label, lstm_prob = lstm.predict(sequence)
    agreement = "AGREE" if mlp_label == lstm_label else "DISAGREE"
    print(
        f"[{agreement}] {prefix} | "
        f"{format_prediction('MLP', mlp_label, mlp_prob)} | "
        f"{format_prediction('LSTM', lstm_label, lstm_prob)}"
    )
    system_label = max(mlp_label, lstm_label)
    print_system_output(
        system_label,
        f"MLP={LABEL_NAMES[mlp_label]}, LSTM={LABEL_NAMES[lstm_label]}",
        "AI SHADOW - chon muc cao hon de demo an toan",
    )
    return target_ms


def selected_target(args: argparse.Namespace, source_target: int) -> int:
    if args.target_ms is not None:
        return args.target_ms
    if args.preset is not None:
        return PRESET_TARGETS_MS[args.preset]
    return source_target


def run_replay(args: argparse.Namespace, mlp: NumpyMLP, lstm: OnnxLSTM) -> None:
    ratios: deque[float] = deque(maxlen=20)
    target = None
    count = 0
    started = time.perf_counter()
    for row in replay_rows(args.replay):
        row = (row[0], row[1], selected_target(args, row[2]), row[3])
        target = handle_drop(row, ratios, target, mlp, lstm, args.watchdog_multiplier)
        count += 1
        if args.max_records and count >= args.max_records:
            break
    elapsed = time.perf_counter() - started
    print(f"[REPLAY COMPLETE] records={count} elapsed={elapsed:.3f}s")


def run_serial(args: argparse.Namespace, mlp: NumpyMLP, lstm: OnnxLSTM) -> None:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required for --port mode") from exc
    ratios: deque[float] = deque(maxlen=20)
    target = None
    last_drop_monotonic = None
    watchdog_reported = False
    configured_target = None if args.interactive else selected_target(args, 1000)
    selected_preset = None if configured_target is None else (args.preset or "custom")
    commands: queue.Queue[str] = queue.Queue()
    if args.interactive:
        threading.Thread(target=input_worker, args=(commands,), daemon=True).start()
        print_preset_menu()
    print(f"[SERIAL] {args.port} @ {args.baudrate}; Ctrl+C to stop")
    if configured_target is not None:
        print(
            f"[SELECTED SET] target={configured_target} ms/drop "
            f"({60000/configured_target:.1f} drops/min)"
        )
    with serial.Serial(args.port, args.baudrate, timeout=0.20) as connection:
        while True:
            while True:
                try:
                    command = commands.get_nowait()
                except queue.Empty:
                    break
                if command == "q":
                    print("[STOPPED BY USER]")
                    return
                if command in PRESET_KEYS:
                    selected_preset, configured_target = PRESET_KEYS[command]
                    ratios.clear()
                    target = configured_target
                    last_drop_monotonic = None
                    watchdog_reported = False
                    print("=" * 72)
                    print(
                        f"[SELECTED SET] {selected_preset.upper()} - "
                        f"target={configured_target} ms/giot - "
                        f"{60000/configured_target:.1f} giot/phut"
                    )
                    print("[AI BUFFER RESET] Bat dau thu 20 giot moi tu giot ke tiep.")
                    print("=" * 72)
                elif command:
                    print("[INVALID INPUT] Hay go 1, 2, 3 hoac q roi Enter.")

            raw = connection.readline().decode("utf-8", errors="ignore")
            row = parse_raw_line(raw)
            now = time.monotonic()
            if row is not None:
                if configured_target is None:
                    _, drop_number, _, actual_ms = row
                    rate = 60000.0 / actual_ms if actual_ms > 0 else 0.0
                    print(
                        f"[CHINH VAT LY] drop={drop_number:04d} "
                        f"actual={actual_ms} ms = {actual_ms/1000.0:.3f} giay "
                        f"= {rate:.1f} giot/phut | CHUA_CHON_CHE_DO"
                    )
                    continue
                # The firmware's third CSV field is its compile-time display
                # target. Pi inference intentionally uses the operator-selected
                # target so all three research presets share one firmware.
                row = (row[0], row[1], configured_target, row[3])
                target = handle_drop(
                    row, ratios, target, mlp, lstm, args.watchdog_multiplier
                )
                last_drop_monotonic = now
                watchdog_reported = False
            elif (
                target is not None
                and last_drop_monotonic is not None
                and not watchdog_reported
                and now - last_drop_monotonic >= args.watchdog_multiplier * target / 1000.0
            ):
                print(
                    f"[WATCHDOG WARNING] no drop for {now-last_drop_monotonic:.2f}s "
                    f"(set={target}ms)"
                )
                print_system_output(
                    3,
                    f"Khong co giot moi trong {now-last_drop_monotonic:.2f} giay",
                    "WATCHDOG THOI GIAN THUC (uu tien cao hon AI)",
                )
                watchdog_reported = True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--replay", type=Path)
    source.add_argument("--port")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--max-records", type=int, default=0)
    parser.add_argument("--watchdog-multiplier", type=float, default=2.0)
    parser.add_argument("--preset", choices=sorted(PRESET_TARGETS_MS))
    parser.add_argument("--target-ms", type=int)
    parser.add_argument("--interactive", action="store_true")
    parser.add_argument("--mlp", type=Path, default=DEFAULT_MLP)
    parser.add_argument("--lstm", type=Path, default=DEFAULT_LSTM)
    parser.add_argument("--lstm-config", type=Path, default=DEFAULT_LSTM_CONFIG)
    args = parser.parse_args()
    if not math.isfinite(args.watchdog_multiplier) or args.watchdog_multiplier <= 1.0:
        raise SystemExit("--watchdog-multiplier must be finite and greater than 1")
    if args.target_ms is not None and args.target_ms <= 250:
        raise SystemExit("--target-ms must be greater than 250 ms")
    if args.target_ms is not None and args.preset is not None:
        raise SystemExit("Use either --target-ms or --preset, not both")
    if args.interactive and (args.target_ms is not None or args.preset is not None):
        raise SystemExit("--interactive cannot be combined with --target-ms or --preset")

    print("[RESEARCH PROTOTYPE] Not for clinical decisions")
    mlp = NumpyMLP(args.mlp)
    lstm = OnnxLSTM(args.lstm, args.lstm_config)
    if args.replay:
        run_replay(args, mlp, lstm)
    else:
        run_serial(args, mlp, lstm)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[STOPPED]")
        sys.exit(0)

"""Generate reproducible synthetic IV-drop time series for pipeline development.

Synthetic files are never written into data/raw, which is reserved for sensor data.
The generator uses a fixed seed and session-level train/validation/test splits.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


GENERATOR_VERSION = "synthetic_v1"
DEFAULT_SEED = 20260813
PRESETS = {"slow": 1500, "normal": 1000, "fast": 750}
SCENARIOS = (
    "stable",
    "gradually_slowing",
    "gradually_speeding",
    "rapid_change",
    "temporary_disturbance",
    "missing_drop",
    "recovery",
    "irregular",
)
SEQUENCE_LENGTH = 20
FUTURE_HORIZON = 5
LABEL_POLICY_VERSION = "future_state_v1_prototype"


@dataclass(frozen=True)
class Calibration:
    stable_noise_ratio: float
    stable_drift_ratio_per_drop: float
    gradual_slowing_ratio_per_drop: float
    missing_drop_ratio_median: float
    real_sessions_used: tuple[str, ...]


def read_csv_intervals(path: Path) -> list[int]:
    with path.open(newline="", encoding="utf-8") as handle:
        return [int(row["actual_interval_ms"]) for row in csv.DictReader(handle)]


def derive_calibration(project_root: Path) -> Calibration:
    raw_dir = project_root / "data" / "raw"
    stable_paths = [raw_dir / "session_005.csv", raw_dir / "session_006.csv"]
    stable_ratios: list[float] = []
    real_sessions: list[str] = []
    drift_estimates: list[float] = []

    for path in stable_paths:
        if not path.exists():
            continue
        values = read_csv_intervals(path)
        if len(values) < 20:
            continue
        real_sessions.append(path.stem)
        median_value = statistics.median(values)
        stable_ratios.extend((value - median_value) / 1000.0 for value in values)
        drift_estimates.append((statistics.mean(values[-20:]) - statistics.mean(values[:20])) / 1000.0 / (len(values) - 1))

    stable_noise = statistics.pstdev(stable_ratios) if stable_ratios else 0.004
    stable_noise = min(max(stable_noise, 0.002), 0.012)
    stable_drift = statistics.median(drift_estimates) if drift_estimates else 0.00005

    slowing_path = raw_dir / "session_003.csv"
    slowing_slope = 0.0002
    if slowing_path.exists():
        values = read_csv_intervals(slowing_path)
        if len(values) >= 40:
            real_sessions.append(slowing_path.stem)
            slowing_slope = (statistics.mean(values[-20:]) - statistics.mean(values[:20])) / 1000.0 / (len(values) - 1)
    slowing_slope = min(max(slowing_slope, 0.0001), 0.0015)

    missing_ratios: list[float] = []
    for name in ("session_009", "session_010"):
        path = raw_dir / f"{name}.csv"
        if not path.exists():
            continue
        values = read_csv_intervals(path)
        if values:
            real_sessions.append(name)
            missing_ratios.append(max(values) / 1000.0)
    missing_ratio = statistics.median(missing_ratios) if missing_ratios else 20.0

    return Calibration(
        stable_noise_ratio=stable_noise,
        stable_drift_ratio_per_drop=stable_drift,
        gradual_slowing_ratio_per_drop=slowing_slope,
        missing_drop_ratio_median=missing_ratio,
        real_sessions_used=tuple(dict.fromkeys(real_sessions)),
    )


def smoothstep(value: float) -> float:
    value = min(max(value, 0.0), 1.0)
    return value * value * (3.0 - 2.0 * value)


def add_correlated_noise(
    ratios: list[float], rng: random.Random, sigma: float
) -> list[float]:
    state = rng.gauss(0.0, sigma)
    noisy: list[float] = []
    for ratio in ratios:
        state = 0.65 * state + rng.gauss(0.0, sigma * math.sqrt(1 - 0.65**2))
        noisy.append(max(0.34, ratio + state))
    return noisy


def generate_ratios(
    scenario: str,
    count: int,
    rng: random.Random,
    calibration: Calibration,
) -> tuple[list[float], dict[str, object]]:
    session_bias = rng.uniform(-0.025, 0.025)
    base = 1.0 + session_bias
    ratios = [base] * count
    details: dict[str, object] = {}

    if scenario == "stable":
        drift = rng.uniform(-0.00015, 0.00015)
        ratios = [min(max(base + drift * i, 0.92), 1.08) for i in range(count)]
        details["drift_ratio_per_drop"] = drift

    elif scenario == "gradually_slowing":
        start = rng.randint(25, 45)
        end_ratio = rng.uniform(1.35, 1.55)
        duration = count - start - rng.randint(5, 20)
        for i in range(start, count):
            progress = smoothstep((i - start) / max(duration, 1))
            ratios[i] = base + (end_ratio - base) * progress
        details.update(start_record=start + 1, end_ratio=end_ratio)

    elif scenario == "gradually_speeding":
        start = rng.randint(25, 45)
        end_ratio = rng.uniform(0.62, 0.78)
        duration = count - start - rng.randint(5, 20)
        for i in range(start, count):
            progress = smoothstep((i - start) / max(duration, 1))
            ratios[i] = base + (end_ratio - base) * progress
        details.update(start_record=start + 1, end_ratio=end_ratio)

    elif scenario == "rapid_change":
        event = rng.randint(65, 110)
        end_ratio = rng.choice((rng.uniform(1.42, 1.65), rng.uniform(0.55, 0.72)))
        transition = rng.randint(2, 6)
        for i in range(event, count):
            progress = smoothstep((i - event + 1) / transition)
            ratios[i] = base + (end_ratio - base) * progress
        details.update(event_record=event + 1, end_ratio=end_ratio)

    elif scenario == "temporary_disturbance":
        event = rng.randint(65, 110)
        width = rng.randint(3, 7)
        peak = rng.choice((rng.uniform(1.35, 1.75), rng.uniform(0.52, 0.72)))
        recovery = rng.randint(5, 12)
        for offset in range(width):
            index = event + offset
            if index < count:
                ratios[index] = peak + rng.gauss(0.0, 0.025)
        for offset in range(recovery):
            index = event + width + offset
            if index < count:
                progress = smoothstep((offset + 1) / recovery)
                ratios[index] = peak + (base - peak) * progress
        details.update(event_record=event + 1, peak_ratio=peak, event_width=width)

    elif scenario == "missing_drop":
        event = rng.randint(65, 110)
        missing_ratio = max(4.0, rng.gauss(calibration.missing_drop_ratio_median, 3.0))
        ratios[event] = missing_ratio
        recovery = rng.randint(8, 16)
        recovery_start = rng.uniform(1.25, 1.65)
        for offset in range(1, recovery + 1):
            index = event + offset
            if index < count:
                progress = smoothstep(offset / recovery)
                ratios[index] = recovery_start + (base - recovery_start) * progress
        details.update(event_record=event + 1, missing_ratio=missing_ratio)

    elif scenario == "recovery":
        initial = rng.choice((rng.uniform(1.38, 1.62), rng.uniform(0.55, 0.72)))
        end = rng.randint(75, 120)
        for i in range(count):
            progress = smoothstep(i / end)
            ratios[i] = initial + (base - initial) * progress
        details.update(initial_ratio=initial, recovery_end_record=end + 1)

    elif scenario == "irregular":
        anchors = [base]
        while len(anchors) < count:
            if rng.random() < 0.18:
                next_ratio = rng.choice(
                    (rng.uniform(0.55, 0.82), rng.uniform(1.20, 1.60))
                )
            else:
                next_ratio = rng.uniform(0.87, 1.13)
            anchors.extend([next_ratio] * rng.randint(2, 8))
        ratios = anchors[:count]
        for i in range(1, count):
            ratios[i] = 0.55 * ratios[i - 1] + 0.45 * ratios[i]
        details["regime_changes"] = True

    else:
        raise ValueError(f"Unknown scenario: {scenario}")

    sigma = calibration.stable_noise_ratio * (2.5 if scenario == "irregular" else 1.0)
    return add_correlated_noise(ratios, rng, sigma), details


def future_label(future_ratios: list[float]) -> int:
    mean_ratio = statistics.mean(future_ratios)
    if max(future_ratios) >= 2.0:
        return 3
    if mean_ratio <= 0.75 or mean_ratio >= 1.30:
        return 3
    if min(future_ratios) >= 0.90 and max(future_ratios) <= 1.10:
        return 1
    return 2


def split_for_replicate(replicate: int) -> str:
    if replicate <= 7:
        return "train"
    if replicate == 8:
        return "validation"
    return "test"


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def window_header() -> list[str]:
    header = [
        "session_id",
        "split",
        "preset",
        "scenario",
        "window_end_drop_number",
        "future_start_drop_number",
        "future_end_drop_number",
        "label",
    ]
    for feature in ("ratio", "error_percent", "delta_ratio"):
        header.extend(f"{feature}_t_minus_{offset}" for offset in range(SEQUENCE_LENGTH - 1, -1, -1))
    return header


def build_window_row(
    session_id: str,
    split: str,
    preset: str,
    scenario: str,
    ratios: list[float],
    end_index: int,
) -> list[object]:
    start_index = end_index - SEQUENCE_LENGTH + 1
    sequence = ratios[start_index : end_index + 1]
    future = ratios[end_index + 1 : end_index + 1 + FUTURE_HORIZON]
    deltas = [0.0]
    deltas.extend(sequence[i] - sequence[i - 1] for i in range(1, len(sequence)))
    return [
        session_id,
        split,
        preset,
        scenario,
        end_index + 2,
        end_index + 3,
        end_index + 2 + FUTURE_HORIZON,
        future_label(future),
        *[round(value, 6) for value in sequence],
        *[round((value - 1.0) * 100.0, 4) for value in sequence],
        *[round(value, 6) for value in deltas],
    ]


def generate(args: argparse.Namespace) -> dict[str, object]:
    project_root = Path(__file__).resolve().parents[1]
    synthetic_root = project_root / "data" / "synthetic"
    raw_dir = synthetic_root / "raw"
    session_dir = synthetic_root / "sessions"
    processed_dir = project_root / "data" / "processed"
    for directory in (raw_dir, session_dir, processed_dir):
        directory.mkdir(parents=True, exist_ok=True)

    existing = list(raw_dir.glob("syn_*.csv")) + list(session_dir.glob("syn_*.json"))
    output_files = [
        synthetic_root / "manifest.csv",
        synthetic_root / "all_sessions_raw.csv",
        synthetic_root / "dataset_summary.json",
        synthetic_root / "label_policy.json",
        processed_dir / "synthetic_windows.csv",
        processed_dir / "synthetic_train.csv",
        processed_dir / "synthetic_validation.csv",
        processed_dir / "synthetic_test.csv",
    ]
    existing.extend(path for path in output_files if path.exists())
    if existing and not args.force:
        raise FileExistsError(
            "Synthetic outputs already exist. Re-run with --force to replace generated files."
        )
    for path in existing:
        path.unlink()

    calibration = derive_calibration(project_root)
    manifest_path = synthetic_root / "manifest.csv"
    all_raw_path = synthetic_root / "all_sessions_raw.csv"
    windows_path = processed_dir / "synthetic_windows.csv"
    split_paths = {
        "train": processed_dir / "synthetic_train.csv",
        "validation": processed_dir / "synthetic_validation.csv",
        "test": processed_dir / "synthetic_test.csv",
    }

    manifest_header = [
        "session_id",
        "source_type",
        "preset",
        "scenario",
        "split",
        "target_interval_ms",
        "records",
        "seed",
        "generator_version",
        "raw_file",
    ]
    raw_header = [
        "session_id",
        "source_type",
        "preset",
        "scenario",
        "split",
        "timestamp_ms",
        "drop_number",
        "target_interval_ms",
        "actual_interval_ms",
    ]
    per_session_raw_header = [
        "timestamp_ms",
        "drop_number",
        "target_interval_ms",
        "actual_interval_ms",
    ]
    windows_header = window_header()

    label_counts: Counter[int] = Counter()
    split_label_counts: dict[str, Counter[int]] = defaultdict(Counter)
    scenario_counts: Counter[str] = Counter()
    preset_counts: Counter[str] = Counter()
    split_session_counts: Counter[str] = Counter()
    raw_records = 0
    window_records = 0

    with (
        manifest_path.open("w", newline="", encoding="utf-8") as manifest_handle,
        all_raw_path.open("w", newline="", encoding="utf-8") as all_raw_handle,
        windows_path.open("w", newline="", encoding="utf-8") as windows_handle,
        split_paths["train"].open("w", newline="", encoding="utf-8") as train_handle,
        split_paths["validation"].open("w", newline="", encoding="utf-8") as validation_handle,
        split_paths["test"].open("w", newline="", encoding="utf-8") as test_handle,
    ):
        manifest_writer = csv.writer(manifest_handle, lineterminator="\n")
        all_raw_writer = csv.writer(all_raw_handle, lineterminator="\n")
        windows_writer = csv.writer(windows_handle, lineterminator="\n")
        split_writers = {
            "train": csv.writer(train_handle, lineterminator="\n"),
            "validation": csv.writer(validation_handle, lineterminator="\n"),
            "test": csv.writer(test_handle, lineterminator="\n"),
        }
        manifest_writer.writerow(manifest_header)
        all_raw_writer.writerow(raw_header)
        windows_writer.writerow(windows_header)
        for writer in split_writers.values():
            writer.writerow(windows_header)

        session_number = 0
        for preset, target_ms in PRESETS.items():
            for scenario in SCENARIOS:
                for replicate in range(1, args.sessions_per_group + 1):
                    session_number += 1
                    session_id = f"syn_{session_number:04d}"
                    split = split_for_replicate(replicate)
                    session_seed = args.seed + session_number * 1009
                    rng = random.Random(session_seed)
                    ratios, scenario_details = generate_ratios(
                        scenario, args.records_per_session, rng, calibration
                    )
                    intervals = [max(250, round(target_ms * ratio)) for ratio in ratios]
                    timestamp = rng.randint(1200, 3500)
                    raw_path = raw_dir / f"{session_id}.csv"

                    with raw_path.open("w", newline="", encoding="utf-8") as raw_handle:
                        raw_writer = csv.writer(raw_handle, lineterminator="\n")
                        raw_writer.writerow(per_session_raw_header)
                        for index, interval in enumerate(intervals):
                            timestamp += interval
                            drop_number = index + 2
                            row = [timestamp, drop_number, target_ms, interval]
                            raw_writer.writerow(row)
                            all_raw_writer.writerow(
                                [
                                    session_id,
                                    "synthetic",
                                    preset,
                                    scenario,
                                    split,
                                    *row,
                                ]
                            )

                    raw_records += len(intervals)
                    scenario_counts[scenario] += 1
                    preset_counts[preset] += 1
                    split_session_counts[split] += 1
                    manifest_writer.writerow(
                        [
                            session_id,
                            "synthetic",
                            preset,
                            scenario,
                            split,
                            target_ms,
                            len(intervals),
                            session_seed,
                            GENERATOR_VERSION,
                            str(raw_path.relative_to(project_root)).replace("\\", "/"),
                        ]
                    )

                    metadata = {
                        "session_id": session_id,
                        "source_type": "synthetic",
                        "synthetic": True,
                        "generator_version": GENERATOR_VERSION,
                        "root_seed": args.seed,
                        "session_seed": session_seed,
                        "preset": preset,
                        "scenario": scenario,
                        "split": split,
                        "target_interval_ms": target_ms,
                        "target_drops_per_minute": round(60000 / target_ms, 3),
                        "records": len(intervals),
                        "sequence_length": SEQUENCE_LENGTH,
                        "future_horizon": FUTURE_HORIZON,
                        "label_policy_version": LABEL_POLICY_VERSION,
                        "scenario_details": scenario_details,
                        "calibration_real_sessions": calibration.real_sessions_used,
                        "raw_file": str(raw_path.relative_to(project_root)).replace("\\", "/"),
                        "research_only": True,
                    }
                    write_json(session_dir / f"{session_id}.json", metadata)

                    normalized = [interval / target_ms for interval in intervals]
                    for end_index in range(
                        SEQUENCE_LENGTH - 1,
                        len(normalized) - FUTURE_HORIZON,
                    ):
                        window_row = build_window_row(
                            session_id,
                            split,
                            preset,
                            scenario,
                            normalized,
                            end_index,
                        )
                        label = int(window_row[7])
                        windows_writer.writerow(window_row)
                        split_writers[split].writerow(window_row)
                        label_counts[label] += 1
                        split_label_counts[split][label] += 1
                        window_records += 1

    label_policy = {
        "version": LABEL_POLICY_VERSION,
        "research_only": True,
        "not_a_medical_threshold": True,
        "future_horizon": FUTURE_HORIZON,
        "classes": {
            "1": "NORMAL",
            "2": "ATTENTION",
            "3": "WARNING",
        },
        "rules": {
            "warning": "max future ratio >= 2.0, or future mean ratio <= 0.75 or >= 1.30",
            "normal": "all five future ratios are within 0.90 to 1.10",
            "attention": "all remaining transition or moderate-deviation windows",
        },
        "note": "Prototype ground-truth policy for synthetic pipeline development; revise with domain review and real validation data.",
    }
    write_json(synthetic_root / "label_policy.json", label_policy)

    summary = {
        "generator_version": GENERATOR_VERSION,
        "seed": args.seed,
        "research_only": True,
        "synthetic_sessions": sum(split_session_counts.values()),
        "raw_records": raw_records,
        "window_samples": window_records,
        "records_per_session": args.records_per_session,
        "sessions_per_preset_scenario": args.sessions_per_group,
        "sequence_length": SEQUENCE_LENGTH,
        "future_horizon": FUTURE_HORIZON,
        "preset_targets_ms": PRESETS,
        "scenario_session_counts": dict(scenario_counts),
        "preset_session_counts": dict(preset_counts),
        "split_session_counts": dict(split_session_counts),
        "label_counts": {str(key): label_counts[key] for key in (1, 2, 3)},
        "split_label_counts": {
            split: {str(key): counts[key] for key in (1, 2, 3)}
            for split, counts in split_label_counts.items()
        },
        "calibration": {
            "stable_noise_ratio": calibration.stable_noise_ratio,
            "stable_drift_ratio_per_drop": calibration.stable_drift_ratio_per_drop,
            "gradual_slowing_ratio_per_drop": calibration.gradual_slowing_ratio_per_drop,
            "missing_drop_ratio_median": calibration.missing_drop_ratio_median,
            "real_sessions_used": calibration.real_sessions_used,
        },
        "outputs": {
            "manifest": str(manifest_path.relative_to(project_root)).replace("\\", "/"),
            "raw_all": str(all_raw_path.relative_to(project_root)).replace("\\", "/"),
            "windows_all": str(windows_path.relative_to(project_root)).replace("\\", "/"),
            **{
                f"windows_{split}": str(path.relative_to(project_root)).replace("\\", "/")
                for split, path in split_paths.items()
            },
        },
    }
    write_json(synthetic_root / "dataset_summary.json", summary)
    return summary


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--sessions-per-group", type=int, default=10)
    parser.add_argument("--records-per-session", type=int, default=180)
    parser.add_argument("--force", action="store_true")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.sessions_per_group != 10:
        raise SystemExit("sessions-per-group must be 10 for the fixed 7/1/2 split")
    if args.records_per_session < SEQUENCE_LENGTH + FUTURE_HORIZON + 20:
        raise SystemExit("records-per-session is too small for sequence windows")
    try:
        summary = generate(args)
    except FileExistsError as exc:
        raise SystemExit(str(exc)) from exc
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

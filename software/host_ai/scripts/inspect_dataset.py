"""Validate the generated synthetic dataset and emit an auditable QC report."""

from __future__ import annotations

import csv
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path

from generate_synthetic_dataset import (
    FUTURE_HORIZON,
    PRESETS,
    SEQUENCE_LENGTH,
    future_label,
    window_header,
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_csv(path: Path) -> tuple[list[str], list[dict[str, str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        return list(reader.fieldnames or []), list(reader)


def main() -> int:
    project_root = Path(__file__).resolve().parents[1]
    synthetic_root = project_root / "data" / "synthetic"
    processed_dir = project_root / "data" / "processed"
    errors: list[str] = []
    warnings: list[str] = []

    manifest_header, manifest = read_csv(synthetic_root / "manifest.csv")
    expected_manifest_header = [
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
    if manifest_header != expected_manifest_header:
        errors.append("manifest header mismatch")
    session_ids = [row["session_id"] for row in manifest]
    if len(session_ids) != len(set(session_ids)):
        errors.append("duplicate session_id in manifest")
    if len(manifest) != 240:
        errors.append(f"expected 240 sessions, found {len(manifest)}")

    session_split: dict[str, str] = {}
    session_context: dict[str, tuple[str, str]] = {}
    raw_ratios: dict[str, list[float]] = {}
    raw_records = 0
    scenario_counts: Counter[str] = Counter()
    preset_counts: Counter[str] = Counter()
    split_sessions: Counter[str] = Counter()

    for item in manifest:
        session_id = item["session_id"]
        preset = item["preset"]
        scenario = item["scenario"]
        split = item["split"]
        target = int(item["target_interval_ms"])
        raw_path = project_root / item["raw_file"]
        metadata_path = synthetic_root / "sessions" / f"{session_id}.json"
        session_split[session_id] = split
        session_context[session_id] = (preset, scenario)
        scenario_counts[scenario] += 1
        preset_counts[preset] += 1
        split_sessions[split] += 1

        if item["source_type"] != "synthetic":
            errors.append(f"{session_id}: source_type is not synthetic")
        if PRESETS.get(preset) != target:
            errors.append(f"{session_id}: preset target mismatch")
        if not raw_path.exists() or not metadata_path.exists():
            errors.append(f"{session_id}: missing raw or metadata file")
            continue

        raw_header, rows = read_csv(raw_path)
        if raw_header != [
            "timestamp_ms",
            "drop_number",
            "target_interval_ms",
            "actual_interval_ms",
        ]:
            errors.append(f"{session_id}: raw header mismatch")
        if len(rows) != int(item["records"]):
            errors.append(f"{session_id}: raw record count mismatch")
        raw_records += len(rows)

        ratios: list[float] = []
        previous_timestamp: int | None = None
        previous_drop: int | None = None
        for row_index, row in enumerate(rows, start=1):
            timestamp = int(row["timestamp_ms"])
            drop = int(row["drop_number"])
            row_target = int(row["target_interval_ms"])
            interval = int(row["actual_interval_ms"])
            if row_target != target or interval < 250:
                errors.append(f"{session_id}: invalid target/interval at row {row_index}")
            if previous_drop is not None and drop != previous_drop + 1:
                errors.append(f"{session_id}: non-consecutive drop at row {row_index}")
            if previous_timestamp is not None and timestamp - previous_timestamp != interval:
                errors.append(f"{session_id}: timestamp mismatch at row {row_index}")
            previous_timestamp = timestamp
            previous_drop = drop
            ratios.append(interval / target)
        raw_ratios[session_id] = ratios

        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        if not metadata.get("synthetic") or metadata.get("source_type") != "synthetic":
            errors.append(f"{session_id}: metadata synthetic marker missing")
        if metadata.get("split") != split or metadata.get("scenario") != scenario:
            errors.append(f"{session_id}: metadata context mismatch")

    windows_header, windows = read_csv(processed_dir / "synthetic_windows.csv")
    if windows_header != window_header():
        errors.append("synthetic_windows header mismatch")

    label_counts: Counter[int] = Counter()
    split_label_counts: dict[str, Counter[int]] = defaultdict(Counter)
    window_sessions_by_split: dict[str, set[str]] = defaultdict(set)
    sample_counts_by_scenario: Counter[str] = Counter()
    sample_counts_by_preset: Counter[str] = Counter()

    for row_number, row in enumerate(windows, start=2):
        session_id = row["session_id"]
        split = row["split"]
        preset = row["preset"]
        scenario = row["scenario"]
        if session_id not in raw_ratios:
            errors.append(f"windows row {row_number}: unknown session")
            continue
        if split != session_split[session_id]:
            errors.append(f"windows row {row_number}: split mismatch")
        if (preset, scenario) != session_context[session_id]:
            errors.append(f"windows row {row_number}: context mismatch")
        window_sessions_by_split[split].add(session_id)

        end_drop = int(row["window_end_drop_number"])
        end_index = end_drop - 2
        start_index = end_index - SEQUENCE_LENGTH + 1
        ratios = raw_ratios[session_id]
        sequence = ratios[start_index : end_index + 1]
        future = ratios[end_index + 1 : end_index + 1 + FUTURE_HORIZON]
        label = int(row["label"])
        if len(sequence) != SEQUENCE_LENGTH or len(future) != FUTURE_HORIZON:
            errors.append(f"windows row {row_number}: invalid sequence bounds")
            continue
        if label != future_label(future):
            errors.append(f"windows row {row_number}: label mismatch")

        for offset, expected in zip(range(SEQUENCE_LENGTH - 1, -1, -1), sequence):
            actual = float(row[f"ratio_t_minus_{offset}"])
            if abs(actual - round(expected, 6)) > 1e-9:
                errors.append(f"windows row {row_number}: ratio feature mismatch")
                break

        expected_deltas = [0.0]
        expected_deltas.extend(sequence[i] - sequence[i - 1] for i in range(1, len(sequence)))
        for offset, expected in zip(range(SEQUENCE_LENGTH - 1, -1, -1), expected_deltas):
            actual = float(row[f"delta_ratio_t_minus_{offset}"])
            if abs(actual - round(expected, 6)) > 1e-9:
                errors.append(f"windows row {row_number}: delta feature mismatch")
                break

        label_counts[label] += 1
        split_label_counts[split][label] += 1
        sample_counts_by_scenario[scenario] += 1
        sample_counts_by_preset[preset] += 1

    split_files = {
        "train": processed_dir / "synthetic_train.csv",
        "validation": processed_dir / "synthetic_validation.csv",
        "test": processed_dir / "synthetic_test.csv",
    }
    split_file_counts: dict[str, int] = {}
    for split, path in split_files.items():
        header, rows = read_csv(path)
        if header != windows_header:
            errors.append(f"{split}: split file header mismatch")
        if any(row["split"] != split for row in rows):
            errors.append(f"{split}: row assigned to wrong split file")
        split_file_counts[split] = len(rows)

    split_sets = list(window_sessions_by_split.values())
    for index, left in enumerate(split_sets):
        for right in split_sets[index + 1 :]:
            overlap = left & right
            if overlap:
                errors.append(f"session leakage across splits: {sorted(overlap)[:3]}")

    if raw_records != 43200:
        errors.append(f"expected 43200 raw records, found {raw_records}")
    if len(windows) != 37440:
        errors.append(f"expected 37440 windows, found {len(windows)}")
    if any(label_counts[label] == 0 for label in (1, 2, 3)):
        errors.append("one or more output classes are empty")
    class_one_fraction = label_counts[1] / max(len(windows), 1)
    if class_one_fraction > 0.75:
        warnings.append("NORMAL class exceeds 75%; consider class weighting")

    files_for_checksums = [
        synthetic_root / "manifest.csv",
        synthetic_root / "all_sessions_raw.csv",
        synthetic_root / "dataset_summary.json",
        synthetic_root / "label_policy.json",
        processed_dir / "synthetic_windows.csv",
        *split_files.values(),
    ]
    checksums = {str(path.relative_to(project_root)).replace("\\", "/"): sha256(path) for path in files_for_checksums}
    checksum_path = synthetic_root / "checksums.sha256"
    checksum_path.write_text(
        "".join(f"{digest}  {path}\n" for path, digest in sorted(checksums.items())),
        encoding="utf-8",
    )

    report = {
        "status": "PASS" if not errors else "FAIL",
        "errors": errors,
        "warnings": warnings,
        "synthetic_sessions": len(manifest),
        "raw_records": raw_records,
        "window_samples": len(windows),
        "manifest_header_valid": manifest_header == expected_manifest_header,
        "windows_header_valid": windows_header == window_header(),
        "session_leakage_detected": any("leakage" in error for error in errors),
        "scenario_session_counts": dict(scenario_counts),
        "preset_session_counts": dict(preset_counts),
        "split_session_counts": dict(split_sessions),
        "split_window_counts": split_file_counts,
        "label_counts": {str(label): label_counts[label] for label in (1, 2, 3)},
        "split_label_counts": {
            split: {str(label): counts[label] for label in (1, 2, 3)}
            for split, counts in split_label_counts.items()
        },
        "sample_counts_by_scenario": dict(sample_counts_by_scenario),
        "sample_counts_by_preset": dict(sample_counts_by_preset),
        "checksums": checksums,
    }
    (synthetic_root / "validation_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    summary_rows = [
        ["metric", "value"],
        ["status", report["status"]],
        ["synthetic_sessions", len(manifest)],
        ["raw_records", raw_records],
        ["window_samples", len(windows)],
        ["train_sessions", split_sessions["train"]],
        ["validation_sessions", split_sessions["validation"]],
        ["test_sessions", split_sessions["test"]],
        ["class_1_normal", label_counts[1]],
        ["class_2_attention", label_counts[2]],
        ["class_3_warning", label_counts[3]],
        ["session_leakage_detected", report["session_leakage_detected"]],
        ["error_count", len(errors)],
        ["warning_count", len(warnings)],
    ]
    with (synthetic_root / "validation_summary.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        csv.writer(handle, lineterminator="\n").writerows(summary_rows)

    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())

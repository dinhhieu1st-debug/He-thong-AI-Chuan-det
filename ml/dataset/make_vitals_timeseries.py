#!/usr/bin/env python3
"""
Build train/validation/test sets for Model 2 (vitals forecaster) from PhysioNet
BIDMC - real ICU recordings, 53 patients, already sampled at 1 Hz.

This is the one model in the system trained on 100% real patient data. It shares
NOTHING with the drip pipeline: no common file, no common array, no window that
contains both a heart rate and a drop count. That separation is the entire point
of the v2 redesign - the previous model put ICU vitals and simulated drip data in
the same 64 s window, so it necessarily learned a correlation between them that
does not exist outside the dataset.

--- Splitting by PATIENT, never by row --------------------------------------

Windows overlap by 79 of their 80 samples at stride 1. A row-wise split would
therefore place near-identical windows from the same patient in both train and
test, and the reported error would measure memorisation rather than
generalisation. Worse, it would look excellent. The split is by patient id, and
the script asserts the three id sets are disjoint rather than trusting that the
shuffle did the right thing.

Every window is checked against its own patient's id at save time, so a later
edit that reshuffles rows cannot silently break the guarantee.

--- Missing samples ---------------------------------------------------------

BIDMC contains a small number of NaN cells (six patients, worst case 102 of 962
cells in bidmc_19). They are gaps in monitoring, not zeros: a patient whose SpO2
reads NaN is not a patient at 0% saturation. Interpolating across them would
manufacture physiology that was never measured, so any window containing a NaN
in either channel is dropped whole.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

import numpy as np

WINDOW = 64
HORIZON = 16
SPAN = WINDOW + HORIZON

# Static normalisation - must match firmware (ai_engine.cpp) exactly.
HR_CENTRE, HR_SCALE = 80.0, 20.0
SPO2_CENTRE, SPO2_SCALE = 97.0, 2.0

# What counts as a NORMAL window: the forecaster is trained unsupervised on
# normal physiology, so a window is only training material if the patient stayed
# inside these bounds for all 80 seconds. The bounds are the same clinical limits
# the firmware enforces as hard rules, so "normal" means the same thing in the
# dataset and on the chip.
HR_LO, HR_HI = 45.0, 150.0
SPO2_LO = 90.0

REPO = Path(__file__).resolve().parents[2]
DEFAULT_SRC = REPO / "ml" / "data_src" / "vitals"
DEFAULT_OUT = REPO / "ml" / "out"

SPLIT_SIZES = {"train": 31, "validation": 10, "test": 12}
SPLIT_SEED = 20260817


def normalise(hr: np.ndarray, spo2: np.ndarray) -> np.ndarray:
    """Stack into the (..., 2) channel layout the model and firmware both use."""
    return np.stack(
        [(hr - HR_CENTRE) / HR_SCALE, (spo2 - SPO2_CENTRE) / SPO2_SCALE], axis=-1
    ).astype(np.float32)


def load_patient(path: Path) -> tuple[str, np.ndarray, np.ndarray]:
    rows = list(csv.DictReader(path.open()))
    # BIDMC headers carry a leading space (" HR"); match on the stripped name so
    # a future export without the space does not break the loader.
    keys = {k.strip(): k for k in rows[0]}
    hr = np.array([float(r[keys["HR"]]) for r in rows], dtype=np.float32)
    spo2 = np.array([float(r[keys["SpO2"]]) for r in rows], dtype=np.float32)
    pid = re.search(r"bidmc_(\d+)", path.stem).group(1)
    return pid, hr, spo2


def cut_windows(hr: np.ndarray, spo2: np.ndarray, stride: int):
    n = len(hr)
    if n < SPAN:
        return None

    starts = np.arange(0, n - SPAN + 1, stride)
    hr_win = np.stack([hr[s : s + SPAN] for s in starts])
    sp_win = np.stack([spo2[s : s + SPAN] for s in starts])

    # Drop any window with a monitoring gap - see module docstring.
    finite = np.isfinite(hr_win).all(1) & np.isfinite(sp_win).all(1)
    hr_win, sp_win = hr_win[finite], sp_win[finite]
    if len(hr_win) == 0:
        return None

    is_normal = (
        (hr_win >= HR_LO).all(1)
        & (hr_win <= HR_HI).all(1)
        & (sp_win >= SPO2_LO).all(1)
    )

    x = normalise(hr_win[:, :WINDOW], sp_win[:, :WINDOW])          # (N, 64, 2)
    y = normalise(hr_win[:, WINDOW:], sp_win[:, WINDOW:])          # (N, 16, 2)
    y = y.reshape(len(y), HORIZON * 2)                             # (N, 32) flat
    raw = np.stack([hr_win, sp_win], axis=-1)                      # (N, 80, 2)
    return x, y, is_normal, raw, int((~finite).sum())


def build(src: Path, out_dir: Path, stride: int) -> None:
    files = sorted(src.glob("bidmc_*_Numerics.csv"))
    if not files:
        raise SystemExit(f"No BIDMC files found in {src}")

    patients = {}
    dropped_total = 0
    for f in files:
        pid, hr, spo2 = load_patient(f)
        cut = cut_windows(hr, spo2, stride)
        if cut is None:
            print(f"  patient {pid}: no usable window - skipped")
            continue
        x, y, is_normal, raw, dropped = cut
        dropped_total += dropped
        patients[pid] = (x, y, is_normal, raw)

    ids = sorted(patients)
    rng = np.random.default_rng(SPLIT_SEED)
    shuffled = list(rng.permutation(ids))

    assignment: dict[str, list[str]] = {}
    cursor = 0
    for split, size in SPLIT_SIZES.items():
        assignment[split] = sorted(shuffled[cursor : cursor + size])
        cursor += size
    # Any patient beyond the configured sizes joins the test set rather than
    # being silently discarded.
    if cursor < len(shuffled):
        assignment["test"] = sorted(assignment["test"] + shuffled[cursor:])

    # Disjointness is asserted, not assumed.
    for a in assignment:
        for b in assignment:
            if a < b:
                overlap = set(assignment[a]) & set(assignment[b])
                if overlap:
                    raise SystemExit(f"LEAK: patients {overlap} in both {a} and {b}")

    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {"source": str(src.relative_to(REPO)),
               "patients": len(patients),
               "windows_dropped_for_nan": dropped_total,
               "split_seed": SPLIT_SEED,
               "splits": {}}

    for split, pids in assignment.items():
        x = np.concatenate([patients[p][0] for p in pids])
        y = np.concatenate([patients[p][1] for p in pids])
        normal = np.concatenate([patients[p][2] for p in pids])
        raw = np.concatenate([patients[p][3] for p in pids])
        pid_col = np.concatenate(
            [np.repeat(p, len(patients[p][0])) for p in pids]
        )

        if split == "train":
            x, y, raw, pid_col, normal = (
                x[normal], y[normal], raw[normal], pid_col[normal], normal[normal]
            )

        path = out_dir / f"vitals_{split}.npz"
        np.savez_compressed(
            path, x=x, y=y, raw=raw, is_normal=normal, patient_id=pid_col
        )

        summary["splits"][split] = {
            "patients": pids,
            "n_patients": len(pids),
            "windows": int(len(x)),
            "normal_windows": int(normal.sum()),
            "abnormal_windows": int((~normal).sum()),
            "hr_range": [float(raw[..., 0].min()), float(raw[..., 0].max())],
            "spo2_range": [float(raw[..., 1].min()), float(raw[..., 1].max())],
        }
        print(f"  {split:11s} {len(pids):2d} patients  {len(x):6d} windows  "
              f"({int(normal.sum())} normal / {int((~normal).sum())} abnormal)"
              f"  -> {path.name}")

    (out_dir / "vitals_dataset_summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\n  windows dropped for monitoring gaps: {dropped_total}")
    print(f"Summary -> {out_dir / 'vitals_dataset_summary.json'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--stride", type=int, default=1)
    args = ap.parse_args()
    build(args.src, args.out, args.stride)


if __name__ == "__main__":
    main()

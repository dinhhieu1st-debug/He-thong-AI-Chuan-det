#!/usr/bin/env python3
"""
Build the TRAIN and VALIDATION sets for Model 1 (drip forecaster).

Source: the synthetic sessions from `AI-nho-giot` (240 sessions x 180 drops,
8 clinical scenarios x 3 infusion presets), vendored at
`ml/data_src/drip/all_sessions_raw.csv`. Those sessions are simulated, but they
are NOT invented from nothing: per that project's DATASET_CARD, the generator was
numerically calibrated against five real recordings (sessions 003, 005, 006, 009,
010), so the noise level and drift behaviour come from the actual sensor.

The test set deliberately does NOT come from here - it is built from untouched
real recordings by `make_drip_realtest.py`. Keeping the two generators separate
is what makes it impossible to accidentally evaluate on simulated data.

Split policy: the generator already assigned every session a split, and this
script honours it rather than reshuffling. Splitting is by SESSION, never by row
- windows overlap by 79 of 80 samples at stride 1, so a row-wise split would put
near-identical windows in both train and validation and report a score that means
nothing.

Only NORMAL windows are written to the training file (the forecaster learns
normal dynamics unsupervised; forecast error is the anomaly signal). The
validation file keeps abnormal windows too, flagged, because the false-alarm rate
can only be measured against windows that are genuinely abnormal.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

import numpy as np

from drip_common import (
    HORIZON,
    WINDOW,
    cut_windows,
    load_synthetic_sessions,
    normalise_drip,
)

REPO = Path(__file__).resolve().parents[2]
DEFAULT_SRC = REPO / "ml" / "data_src" / "drip" / "all_sessions_raw.csv"
DEFAULT_OUT = REPO / "ml" / "out"


def build(src: Path, out_dir: Path, stride: int) -> None:
    sessions = load_synthetic_sessions(src)
    print(f"Loaded {len(sessions)} synthetic sessions from {src.name}")

    buckets: dict[str, dict[str, list]] = {}
    scenario_counts: dict[str, Counter] = {}

    for s in sessions:
        split = s.split or "train"
        hist, fut, is_normal = cut_windows(s, stride=stride)
        if len(hist) == 0:
            continue

        b = buckets.setdefault(split, {"hist": [], "fut": [], "normal": [], "sid": []})
        b["hist"].append(hist)
        b["fut"].append(fut)
        b["normal"].append(is_normal)
        b["sid"].extend([s.session_id] * len(hist))

        scenario_counts.setdefault(split, Counter())[s.scenario] += 1

    # A session must never appear in two splits. Checked rather than assumed:
    # the whole point of a session-wise split is defeated by a single overlap.
    seen: dict[str, str] = {}
    for split, b in buckets.items():
        for sid in set(b["sid"]):
            if sid in seen:
                raise SystemExit(
                    f"LEAK: session {sid} is in both '{seen[sid]}' and '{split}'"
                )
            seen[sid] = split

    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {}

    for split, b in buckets.items():
        hist = np.concatenate(b["hist"])
        fut = np.concatenate(b["fut"])
        normal = np.concatenate(b["normal"])
        sid = np.array(b["sid"])

        if split == "train":
            # Unsupervised on normal dynamics only - see module docstring.
            keep = normal
            hist, fut, normal, sid = hist[keep], fut[keep], normal[keep], sid[keep]

        # The generator's own "test" split is still synthetic. It is written
        # under an unambiguous name so it can never be mistaken for the real
        # test set that `make_drip_realtest.py` produces.
        name = "synthetic_test" if split == "test" else split
        path = out_dir / f"drip_{name}.npz"
        np.savez_compressed(
            path,
            x=normalise_drip(hist).astype(np.float32),
            y=normalise_drip(fut).astype(np.float32),
            x_raw=hist,
            y_raw=fut,
            is_normal=normal,
            session_id=sid,
        )

        summary[split] = {
            "windows": int(len(hist)),
            "sessions": int(len(set(sid.tolist()))),
            "normal_windows": int(normal.sum()),
            "abnormal_windows": int((~normal).sum()),
            "ratio_min": float(hist.min()),
            "ratio_max": float(hist.max()),
            "ratio_mean": float(hist.mean()),
            "scenarios": dict(scenario_counts[split]),
        }
        print(
            f"  {split:11s} {len(hist):6d} windows  "
            f"({len(set(sid.tolist()))} sessions, "
            f"{int(normal.sum())} normal / {int((~normal).sum())} abnormal)  "
            f"-> {path.name}"
        )

    meta = {
        "source": str(src.relative_to(REPO)),
        "window": WINDOW,
        "horizon": HORIZON,
        "stride": stride,
        "note": (
            "Synthetic sessions calibrated from real sessions 003/005/006/009/010. "
            "Test set is built separately from untouched real recordings."
        ),
        "splits": summary,
    }
    (out_dir / "drip_dataset_summary.json").write_text(json.dumps(meta, indent=2))
    print(f"\nSummary -> {out_dir / 'drip_dataset_summary.json'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--stride", type=int, default=1)
    args = ap.parse_args()
    build(args.src, args.out, args.stride)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Build the TEST set for Model 1 from untouched real sensor recordings.

Kept separate from `make_drip_timeseries.py` on purpose: that script only ever
reads simulated sessions, this one only ever reads real ones, so no future edit
can quietly mix simulated data into the reported test score.

--- The leakage boundary, and an honest problem with it ---------------------

The synthetic generator was numerically calibrated against five real sessions
(003, 005, 006, 009, 010). Those five did not contribute training SAMPLES, but
they did shape the distribution the training samples were drawn from. Treating
them as a clean test set would be a soft form of leakage.

The remaining real sessions are 001, 002, 004, 007, 008 and 011. Session 011 is
unusable - detection stopped after the second drop, per its own log - which
leaves five.

Here is the problem: every one of those five is ANOMALOUS. Read their own
metadata:

    001  missing drop pattern with recovery
    002  drift to the upper boundary with a single delay
    004  manual recovery to stable
    007  manual speeding with irregular transitions
    008  manual irregular recovery

The two genuinely stable recordings, 005 and 006, are exactly the two that were
used for calibration. So the real data supports measuring RECALL cleanly, but
there is no leak-free real data for measuring the FALSE-ALARM rate.

Rather than hide that, this script emits two clearly separated files:

  drip_realtest_heldout.npz     sessions 001/002/004/007/008
                                Leak-free. The headline test set.

  drip_realtest_calibrated.npz  sessions 003/005/006/009/010
                                CALIBRATION-INFLUENCED. Contains the only real
                                stable recordings (005, 006), so it is the only
                                place a false-alarm rate can be measured on real
                                data - always with the caveat, never as a
                                headline number.

`evaluate.py` reads the `leakage` field in each file and refuses to merge them.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

from drip_common import (
    HORIZON,
    WINDOW,
    cut_windows,
    load_real_sessions,
    normalise_drip,
)

REPO = Path(__file__).resolve().parents[2]
DEFAULT_SRC = REPO / "ml" / "data_src" / "drip" / "real"
DEFAULT_OUT = REPO / "ml" / "out"

# Sessions the synthetic generator was calibrated against, per the upstream
# DATASET_CARD. Anything listed here is NOT leak-free test data.
CALIBRATION_SESSIONS = {"session_003", "session_005", "session_006",
                        "session_009", "session_010"}


def build(src: Path, out_dir: Path, stride: int) -> None:
    sessions = load_real_sessions(src)
    print(f"Loaded {len(sessions)} usable real sessions from {src}")

    groups: dict[str, dict] = {
        "heldout": {"leakage": "none", "sessions": [], "desc":
                    "Real recordings the synthetic generator never saw."},
        "calibrated": {"leakage": "calibration-influenced", "sessions": [], "desc":
                   "Real recordings that were used to calibrate the synthetic "
                   "generator. Reported with that caveat, never without it."},
    }

    for s in sessions:
        group = "calibrated" if s.session_id in CALIBRATION_SESSIONS else "heldout"
        hist, fut, is_normal = cut_windows(s, stride=stride)
        if len(hist) == 0:
            print(f"  {s.session_id}: only {len(s)} s of data, "
                  f"shorter than one {WINDOW}+{HORIZON} s window - skipped")
            continue
        groups[group]["sessions"].append((s, hist, fut, is_normal))

    out_dir.mkdir(parents=True, exist_ok=True)
    summary = {}

    for name, g in groups.items():
        if not g["sessions"]:
            continue
        hist = np.concatenate([h for _, h, _, _ in g["sessions"]])
        fut = np.concatenate([f for _, _, f, _ in g["sessions"]])
        normal = np.concatenate([n for _, _, _, n in g["sessions"]])
        sid = np.concatenate(
            [np.repeat(s.session_id, len(h)) for s, h, _, _ in g["sessions"]]
        )

        path = out_dir / f"drip_realtest_{name}.npz"
        np.savez_compressed(
            path,
            x=normalise_drip(hist).astype(np.float32),
            y=normalise_drip(fut).astype(np.float32),
            x_raw=hist,
            y_raw=fut,
            is_normal=normal,
            session_id=sid,
            leakage=g["leakage"],
        )

        per_session = {
            s.session_id: {
                "windows": int(len(h)),
                "seconds": int(len(s)),
                "target_dpm": round(s.target_dpm, 2),
                "logged_condition": s.scenario,
                "normal_windows": int(n.sum()),
            }
            for s, h, _, n in g["sessions"]
        }
        summary[name] = {
            "leakage": g["leakage"],
            "description": g["desc"],
            "windows": int(len(hist)),
            "normal_windows": int(normal.sum()),
            "abnormal_windows": int((~normal).sum()),
            "per_session": per_session,
        }

        print(f"\n  [{name}] leakage={g['leakage']}  {len(hist)} windows "
              f"-> {path.name}")
        for sid_, info in per_session.items():
            print(f"      {sid_}  {info['seconds']:4d}s  {info['windows']:3d} win  "
                  f"{info['normal_windows']:3d} normal  "
                  f"tgt={info['target_dpm']:.0f}dpm  {info['logged_condition']}")

    (out_dir / "drip_realtest_summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\nSummary -> {out_dir / 'drip_realtest_summary.json'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--stride", type=int, default=1)
    args = ap.parse_args()
    build(args.src, args.out, args.stride)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Shared logic for turning DROP EVENTS into the 1 Hz series the chip actually sees.

The drip sensor does not produce a sample per second - it produces one row per
drop, and drops arrive roughly once per second only when flow happens to be at
the prescribed rate. Everything downstream (the 64 s window, the forecaster, the
firmware ring buffer) is defined on a fixed 1 Hz grid, so the conversion done
here is the single place where "event time" becomes "wall-clock time". Getting it
wrong silently shifts every window in the dataset, so it lives in one file that
both the synthetic and the real-sensor loaders call.

Two decisions in here matter more than the rest:

RATE ratio, not INTERVAL ratio. `AI-nho-giot` works in interval ratio
(actual_interval / target_interval), which goes UP when flow slows down. The
firmware and the model specification work in drops-per-minute, so the ratio has
to be inverted:

    drops_ratio = dpm_actual / dpm_target = target_interval / actual_interval

which goes DOWN when flow slows down. Both conventions are defensible; mixing
them flips the sign of every occlusion in the dataset, so the conversion happens
exactly once, here.

A GAP is evidence, not missing data. Between two drop events the naive approach
is zero-order hold: keep reporting the last measured rate until the next drop
arrives. That is wrong in the one case that matters most. If a drop was due one
second ago and has not come, flow has ALREADY slowed - the longer the silence,
the slower it is. This is the earliest signature of a forming occlusion, and
zero-order hold erases it completely. So the rate at grid time t uses whichever
interval is longer: the last completed one, or the time elapsed since the last
drop.

    effective_interval(t) = max(last_completed_interval, t - t_last_drop)

The rate therefore decays smoothly through a gap and snaps back when the next
drop lands, which is what a nurse watching the chamber would also perceive.
"""

from __future__ import annotations

import csv
import json
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

# --- Window geometry. Must stay identical in firmware (ai_fusion.c) ----------
WINDOW = 64          # seconds of history fed to the model
HORIZON = 16         # seconds forecast ahead
SPAN = WINDOW + HORIZON

# --- Static normalisation. Must stay identical in firmware (ai_engine.cpp) ---
# Chosen, not fitted: a scaler fitted on the dataset would silently change every
# time the dataset is regenerated, and the firmware constant would drift out of
# sync with no build error to catch it.
DRIP_CENTRE = 1.0
DRIP_SCALE = 0.35


def normalise_drip(ratio: np.ndarray) -> np.ndarray:
    return (ratio - DRIP_CENTRE) / DRIP_SCALE


def denormalise_drip(norm: np.ndarray) -> np.ndarray:
    return norm * DRIP_SCALE + DRIP_CENTRE


# A window counts as NORMAL only if the ratio stays inside this band across all
# 80 samples (history AND horizon). The forecasters are trained unsupervised on
# normal dynamics only, so a single abnormal sample anywhere in the span
# disqualifies the window - otherwise the model learns to predict occlusions as
# if they were ordinary behaviour, and the forecast error stops being a signal.
NORMAL_LO = 0.90
NORMAL_HI = 1.10


@dataclass
class DripSession:
    """One recording, already resampled onto the 1 Hz grid."""

    session_id: str
    source_type: str            # "synthetic" | "real"
    ratio: np.ndarray           # drops_ratio, one sample per second
    target_dpm: float
    scenario: str = ""          # synthetic: the generator scenario
    split: str = ""             # synthetic: the split assigned by the generator
    meta: dict = field(default_factory=dict)

    def __len__(self) -> int:
        return len(self.ratio)


def resample_drops_to_1hz(
    timestamp_ms: np.ndarray,
    actual_interval_ms: np.ndarray,
    target_interval_ms: np.ndarray,
) -> tuple[np.ndarray, float]:
    """Turn per-drop events into a 1 Hz series of drops_ratio.

    Returns (ratio_per_second, target_dpm).
    """
    if len(timestamp_ms) < 2:
        return np.empty(0, dtype=np.float32), 0.0

    target_ms = float(np.median(target_interval_ms))
    target_dpm = 60_000.0 / target_ms

    # 1 Hz grid spanning the recording. Starting at the SECOND drop, because the
    # first row's interval refers to a drop that was never recorded.
    t0 = float(timestamp_ms[0])
    t1 = float(timestamp_ms[-1])
    grid = np.arange(t0, t1 + 1.0, 1000.0)

    # For each grid point, index of the most recent drop at or before it.
    idx = np.searchsorted(timestamp_ms, grid, side="right") - 1
    idx = np.clip(idx, 0, len(timestamp_ms) - 1)

    last_interval = actual_interval_ms[idx].astype(np.float64)
    since_last_drop = grid - timestamp_ms[idx].astype(np.float64)

    # The gap-as-evidence rule described in the module docstring.
    effective_interval = np.maximum(last_interval, since_last_drop)
    effective_interval = np.maximum(effective_interval, 1.0)  # guard /0

    ratio = target_ms / effective_interval
    return ratio.astype(np.float32), target_dpm


def load_real_sessions(real_dir: Path) -> list[DripSession]:
    """Load the untouched sensor recordings, each with its logged metadata."""
    sessions: list[DripSession] = []
    for csv_path in sorted(real_dir.glob("session_*.csv")):
        rows = list(csv.DictReader(csv_path.open()))
        if len(rows) < 2:
            # session_011 stopped after drop 2 (detection failed, per its own
            # notes). Kept on disk as an honest record, skipped here rather than
            # padded into something that looks like data.
            continue

        ts = np.array([int(r["timestamp_ms"]) for r in rows], dtype=np.int64)
        act = np.array([int(r["actual_interval_ms"]) for r in rows], dtype=np.int64)
        tgt = np.array([int(r["target_interval_ms"]) for r in rows], dtype=np.int64)

        meta_path = csv_path.with_suffix(".json")
        meta = json.loads(meta_path.read_text()) if meta_path.exists() else {}

        ratio, target_dpm = resample_drops_to_1hz(ts, act, tgt)
        sessions.append(
            DripSession(
                session_id=csv_path.stem,
                source_type="real",
                ratio=ratio,
                target_dpm=target_dpm,
                scenario=meta.get("observed_condition") or meta.get("test_condition", ""),
                meta=meta,
            )
        )
    return sessions


def load_synthetic_sessions(csv_path: Path) -> list[DripSession]:
    """Load the generated sessions, preserving the generator's own split column."""
    grouped: dict[str, list[dict]] = {}
    with csv_path.open() as fd:
        for row in csv.DictReader(fd):
            grouped.setdefault(row["session_id"], []).append(row)

    sessions: list[DripSession] = []
    for session_id, rows in grouped.items():
        ts = np.array([int(r["timestamp_ms"]) for r in rows], dtype=np.int64)
        act = np.array([int(r["actual_interval_ms"]) for r in rows], dtype=np.int64)
        tgt = np.array([int(r["target_interval_ms"]) for r in rows], dtype=np.int64)

        ratio, target_dpm = resample_drops_to_1hz(ts, act, tgt)
        sessions.append(
            DripSession(
                session_id=session_id,
                source_type="synthetic",
                ratio=ratio,
                target_dpm=target_dpm,
                scenario=rows[0]["scenario"],
                split=rows[0]["split"],
                meta={"preset": rows[0]["preset"]},
            )
        )
    return sessions


def cut_windows(session: DripSession, stride: int = 1):
    """Slice one session into (history, horizon, is_normal) windows.

    Windows never cross a session boundary: two recordings joined end to end
    would produce a discontinuity that no real infusion can exhibit, and the
    model would learn to forecast it.
    """
    n = len(session)
    if n < SPAN:
        return (
            np.empty((0, WINDOW), np.float32),
            np.empty((0, HORIZON), np.float32),
            np.empty(0, bool),
        )

    starts = np.arange(0, n - SPAN + 1, stride)
    hist = np.stack([session.ratio[s : s + WINDOW] for s in starts])
    fut = np.stack([session.ratio[s + WINDOW : s + SPAN] for s in starts])

    span = np.concatenate([hist, fut], axis=1)
    is_normal = np.all((span >= NORMAL_LO) & (span <= NORMAL_HI), axis=1)

    return hist.astype(np.float32), fut.astype(np.float32), is_normal

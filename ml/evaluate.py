#!/usr/bin/env python3
"""
Measure what the fusion layer will actually do, second by second, on the int8
models that ship - not the float ones.

The number this exists to produce is the false-alarm rate BEFORE and AFTER the
K=11 persistence filter. Everything else in the AI is justified by detection
accuracy; the persistence filter is justified by how many alarms it removes
without losing the ones that matter, and that can only be measured by replaying
recordings at 1 Hz the way the chip will see them.

--- Causal scoring -----------------------------------------------------------

The AUC figures in the training scripts compare a forecast against the whole
16 s horizon that followed. That is fine for ranking models offline, but the chip
cannot see the future. Here the score is strictly causal and matches the
firmware: at each second, take the forecast the model made ONE SECOND AGO for
NOW, and compare it against what was actually measured. That is the only residual
the device can compute in real time.

--- Thresholds ---------------------------------------------------------------

Each model's threshold is the 98th percentile of its causal residual on NORMAL
VALIDATION windows, computed from the validation .npz - never from the sessions
being replayed. An earlier version of this script derived the threshold from the
same recordings it then scored, which quietly guarantees a 2% flag rate and makes
the "before persistence" figure an artefact of the threshold rather than a
measurement.

--- Two different questions for the two forecasters -------------------------

For the DRIP model, "did it alarm on this recording" is the right question: an
occluding line is a whole-recording condition.

For the VITALS model it is not. A desaturation is an EVENT inside an otherwise
normal recording, so detection is scored per event: an event is a contiguous run
of seconds outside the clinical bounds, and it counts as detected if the alarm
rises at any point from 60 s before it starts until it ends.

Note what the vitals model is and is not being credited with here. SpO2 crossing
90% is caught instantly by the hard clinical rule, with no AI involved and no
persistence filter in the way. The forecaster earns its place only if it raises
the alarm BEFORE that crossing. This measurement is therefore about lead time,
not about whether the crossing is noticed at all.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "ml" / "out"
MODELS = REPO / "ml" / "models"

WINDOW, HORIZON = 64, 16
K_PERSISTENCE = 11
PERCENTILE = 98.0


class Int8Model:
    """The exported model, run exactly as TFLM will run it on the chip."""

    def __init__(self, path: Path):
        self.it = tf.lite.Interpreter(
            model_path=str(path),
            experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF,
        )
        self.it.allocate_tensors()
        self.inp = self.it.get_input_details()[0]
        self.out = self.it.get_output_details()[0]
        self.si, self.zi = self.inp["quantization"]
        self.so, self.zo = self.out["quantization"]

    def __call__(self, x: np.ndarray) -> np.ndarray:
        q = np.clip(np.round(x / self.si) + self.zi, -128, 127).astype(np.int8)
        self.it.set_tensor(self.inp["index"], q.reshape(self.inp["shape"]))
        self.it.invoke()
        return (self.it.get_tensor(self.out["index"]).astype(np.float32).ravel()
                - self.zo) * self.so


def causal_residuals(model: Int8Model, series: np.ndarray, n_ch: int) -> np.ndarray:
    """One residual per second, from the forecast made one second earlier.

    `series` is (T, n_ch) normalised. Returns residuals for t = WINDOW .. T-1,
    each the max across channels of |predicted - actual| for that single second.
    """
    res = []
    for t in range(WINDOW, len(series)):
        window = series[t - WINDOW : t].reshape(1, 1, WINDOW, n_ch)
        pred = model(window).reshape(HORIZON, n_ch)[0]      # first step ahead
        res.append(float(np.max(np.abs(pred - series[t]))))
    return np.array(res)


def persistence_filter(flags: np.ndarray, k: int = K_PERSISTENCE) -> np.ndarray:
    """Raise only after k consecutive flagged seconds. Mirrors ai_fusion.c."""
    out = np.zeros(len(flags), bool)
    count = 0
    for i, f in enumerate(flags):
        count = count + 1 if f else 0
        out[i] = count >= k
    return out


def alarm_episodes(raised: np.ndarray) -> int:
    """Count distinct alarm events, not alarming seconds.

    A 40-second alarm is one alarm to the nurse, not forty. Counting seconds
    would make the persistence filter look far better than it is, because it
    mostly shortens episodes rather than removing them.
    """
    return int(np.sum(raised & ~np.concatenate([[False], raised[:-1]])))


def drip_sessions() -> list[tuple[str, np.ndarray, bool]]:
    """Real recordings as continuous 1 Hz series, with a normal/abnormal label."""
    import sys
    sys.path.insert(0, str(REPO / "ml" / "dataset"))
    from drip_common import load_real_sessions, normalise_drip

    CALIB = {"session_003", "session_005", "session_006",
             "session_009", "session_010"}
    STABLE = {"session_005", "session_006"}

    out = []
    for s in load_real_sessions(REPO / "ml" / "data_src" / "drip" / "real"):
        if len(s) < WINDOW + 2:
            continue
        out.append((s.session_id,
                    normalise_drip(s.ratio).reshape(-1, 1).astype(np.float32),
                    s.session_id in STABLE))
    return out


def vitals_patients(split: str) -> list[tuple[str, np.ndarray, bool]]:
    """Reconstruct each test patient's continuous series from its windows."""
    d = np.load(OUT / f"vitals_{split}.npz", allow_pickle=True)
    pid, raw, normal = d["patient_id"], d["raw"], d["is_normal"]

    out = []
    for p in np.unique(pid):
        sel = pid == p
        w = raw[sel]
        # Consecutive stride-1 windows: the full series is the first window plus
        # the last sample of each subsequent one.
        series = np.concatenate([w[0], w[1:, -1, :]], axis=0)
        norm = np.stack([(series[:, 0] - 80.0) / 20.0,
                         (series[:, 1] - 97.0) / 2.0], axis=-1).astype(np.float32)
        out.append((f"patient_{p}", norm, bool(normal[sel].all())))
    return out


def validation_threshold(model: Int8Model, npz_name: str, n_ch: int) -> float:
    """98th percentile of the one-step-ahead residual on NORMAL validation windows.

    Uses the validation split, never the recordings being replayed - otherwise the
    threshold and the measurement are the same data and the result is circular.
    """
    d = np.load(OUT / npz_name, allow_pickle=True)
    x = d["x"].reshape(-1, WINDOW, n_ch)[d["is_normal"]]
    y = d["y"].reshape(-1, HORIZON, n_ch)[d["is_normal"]]

    res = []
    for xi, yi in zip(x, y):
        pred = model(xi.reshape(1, 1, WINDOW, n_ch)).reshape(HORIZON, n_ch)[0]
        res.append(float(np.max(np.abs(pred - yi[0]))))
    return float(np.percentile(res, PERCENTILE))


def evaluate(name: str, model: Int8Model, sessions, n_ch: int,
             threshold: float, session_recall: bool = True) -> dict:
    scored = [(sid, causal_residuals(model, series, n_ch), is_normal)
              for sid, series, is_normal in sessions]

    rows, totals = [], {"normal_seconds": 0, "raw": 0, "filtered": 0,
                        "abnormal_sessions": 0, "abnormal_detected": 0,
                        "latencies": []}

    for sid, res, is_normal in scored:
        if len(res) == 0:
            continue
        flags = res > threshold
        raised = persistence_filter(flags)
        n_raw, n_filt = alarm_episodes(flags), alarm_episodes(raised)

        row = {"session": sid, "normal": is_normal, "seconds": len(res),
               "alarms_raw": n_raw, "alarms_filtered": n_filt}

        if is_normal:
            totals["normal_seconds"] += len(res)
            totals["raw"] += n_raw
            totals["filtered"] += n_filt
        else:
            totals["abnormal_sessions"] += 1
            if raised.any():
                totals["abnormal_detected"] += 1
                latency = int(np.argmax(raised) - np.argmax(flags))
                row["detect_latency_s"] = latency
                totals["latencies"].append(latency)
        rows.append(row)

    hours = totals["normal_seconds"] / 3600.0
    result = {
        "threshold": threshold,
        "k_persistence": K_PERSISTENCE,
        "normal_hours": hours,
        "false_alarms_per_hour_raw": totals["raw"] / hours if hours else float("nan"),
        "false_alarms_per_hour_filtered": totals["filtered"] / hours if hours else float("nan"),
        "abnormal_sessions": totals["abnormal_sessions"],
        "abnormal_detected": totals["abnormal_detected"],
        "median_detect_latency_s": (float(np.median(totals["latencies"]))
                                    if totals["latencies"] else None),
        "per_session": rows,
    }

    print(f"\n=== {name} ===")
    print(f"  threshold {threshold:.4f} (98th pct of normal validation residual)")
    print(f"  normal data: {hours:.2f} h")
    print(f"  FALSE ALARMS/HOUR   before K={K_PERSISTENCE}: "
          f"{result['false_alarms_per_hour_raw']:.1f}"
          f"   after: {result['false_alarms_per_hour_filtered']:.1f}")
    if totals["abnormal_sessions"] and session_recall:
        print(f"  RECALL  {totals['abnormal_detected']}/{totals['abnormal_sessions']} "
              f"abnormal recordings raised an alarm")
        if totals["latencies"]:
            print(f"  median extra latency from the filter: "
                  f"{np.median(totals['latencies']):.0f} s")
    return result


def vitals_event_detection(model: Int8Model, threshold: float,
                           lead_window: int = 60) -> dict:
    """Per-EVENT detection with lead time, for the unseen test patients.

    An event is a contiguous run of seconds where the patient is outside the
    clinical bounds the firmware enforces (SpO2 < 90, HR < 45 or > 150). It counts
    as detected if the persistence-filtered alarm is up at any point from
    `lead_window` seconds before it begins until it ends.

    Lead time is the figure that matters: the hard rule catches the crossing
    itself instantly and without the AI, so the forecaster is only worth its flash
    if it gets there first. A negative lead means the AI was late and the hard
    rule did the work.
    """
    d = np.load(OUT / "vitals_test.npz", allow_pickle=True)
    pid, raw = d["patient_id"], d["raw"]

    # Events are categorised rather than simply counted. Measured on BIDMC, the
    # two clinical breaches in the whole test set are:
    #
    #   patient 32  SpO2 84% in the very FIRST second of the recording
    #   patient 45  HR 44 for a SINGLE second, SpO2 100% throughout
    #
    # Neither is a missed detection. Nothing can warn ahead of a condition that
    # predates the recording, and a one-second dip is exactly what the K=11 filter
    # exists to suppress - firing on it would be the bug.
    #
    # The real finding is about the DATASET: BIDMC excerpts are ~8 minutes of
    # largely stable ICU numerics and contain no gradual deterioration, so the
    # vitals path's RECALL cannot be measured here at all. What can be measured on
    # it, and is, is the false-alarm rate over 1.14 h of normal physiology.
    # Recall for vitals remains unproven on data, which is why the hard clinical
    # rules stay the primary safety net rather than a backstop.
    events, detected, leads = 0, 0, []
    unmeasurable = {"breach_at_recording_start": 0, "shorter_than_filter": 0}
    for p in np.unique(pid):
        sel = pid == p
        w = raw[sel]
        series = np.concatenate([w[0], w[1:, -1, :]], axis=0)
        hr, spo2 = series[:, 0], series[:, 1]
        out_of_bounds = (spo2 < 90.0) | (hr < 45.0) | (hr > 150.0)
        if not out_of_bounds.any():
            continue

        norm = np.stack([(hr - 80.0) / 20.0, (spo2 - 97.0) / 2.0], -1).astype(np.float32)
        raised = persistence_filter(causal_residuals(model, norm, 2) > threshold)
        # residuals start at t = WINDOW, so align the alarm track to wall time
        alarm = np.concatenate([np.zeros(WINDOW, bool), raised])[: len(hr)]

        edges = np.diff(np.concatenate([[False], out_of_bounds, [False]]).astype(int))
        starts = np.where(edges == 1)[0]
        ends = np.where(edges == -1)[0]

        for a, b in zip(starts, ends):
            if a < WINDOW:
                # The model has no window yet; the hard rule covers this second.
                unmeasurable["breach_at_recording_start"] += 1
                continue
            if b - a < K_PERSISTENCE:
                # Too brief for any persistence-filtered detector, by design.
                unmeasurable["shorter_than_filter"] += 1
                continue
            events += 1
            lo = max(0, a - lead_window)
            if alarm[lo:b].any():
                detected += 1
                first = lo + int(np.argmax(alarm[lo:b]))
                leads.append(int(a - first))    # positive = warned early

    return {
        "measurable_events": events,
        "detected": detected,
        "recall": detected / events if events else None,
        "median_lead_seconds": float(np.median(leads)) if leads else None,
        "leads": leads,
        "unmeasurable": unmeasurable,
        "note": ("BIDMC contains no gradual deterioration event in the test "
                 "split, so vitals recall is not measurable on this dataset. "
                 "The false-alarm figure above IS measurable and is reported."),
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=OUT / "evaluation.json")
    args = ap.parse_args()

    report = {}

    drip = Int8Model(MODELS / "drip_forecaster_int8.tflite")
    t_drip = validation_threshold(drip, "drip_validation.npz", 1)
    report["drip"] = evaluate("DRIP (real recordings, 1 Hz replay)",
                              drip, drip_sessions(), 1, t_drip)

    vitals = Int8Model(MODELS / "vitals_forecaster_int8.tflite")
    t_vitals = validation_threshold(vitals, "vitals_validation.npz", 2)
    # Session-level recall is meaningless for vitals: a patient is not "abnormal"
    # the way a line is: deterioration is an event inside an otherwise normal
    # recording. Scored per event below instead.
    report["vitals"] = evaluate("VITALS (unseen BIDMC patients, 1 Hz replay)",
                                vitals, vitals_patients("test"), 2, t_vitals,
                                session_recall=False)
    report["vitals"]["events"] = vitals_event_detection(vitals, t_vitals)

    args.out.write_text(json.dumps(report, indent=2))
    print(f"\nReport -> {args.out}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Train Model 2: the vitals forecaster. 64 s of (HR, SpO2) in, 16 s of both out.

This is the only model in the system trained entirely on real patient data -
PhysioNet BIDMC, split by patient so no patient appears in two sets. It shares no
tensor, no file and no window with the drip pipeline. That separation is the
point of v2: in v1 a single network consumed vitals and drip together, and
perturbing only the drip channels moved the heart-rate forecast by about 2 bpm.

Loss is Huber (delta=1.0). ICU numerics contain monitor artefacts - a probe
briefly reporting a spurious value - and MSE would square those into the dominant
gradient term.

Headline metric is DETECTION AUC of the forecast residual, not forecast MAE, for
the reason set out at length in train_drip_forecaster.py: a model trained on
normal physiology only is deliberately a bad forecaster of abnormal physiology,
and that is exactly what makes the residual a usable alarm signal.

--- Per-patient level jitter -------------------------------------------------

A resting heart rate of 55 and one of 95 are both perfectly normal, for different
people. Trained without augmentation the model learns the population mean and
pulls every forecast toward it, which manufactures a residual for any patient
whose baseline is unusual - a false alarm caused purely by that patient being
themselves. Each training window is therefore offset by a small random amount per
channel, so the model must read the operating level from the window rather than
from its own bias. This mirrors what the firmware already does clinically: it
alarms on deviation from the patient's OWN baseline, not from a textbook number.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf

from common import (
    build_forecaster,
    check_envelope,
    detection_auc,
    export_int8,
    persistence_baseline,
    sweep_calibration_windows,
    tflite_report,
)

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "ml" / "out"
MODELS = REPO / "ml" / "models"

WINDOW, HORIZON, N_CH = 64, 16, 2
HR_SCALE, SPO2_SCALE = 20.0, 2.0

# Normalised units. HR jitter of 0.75 is 15 bpm of baseline spread; SpO2 gets far
# less, because a healthy patient's saturation genuinely does sit within a couple
# of points of 97 - the individual variation HR shows simply is not there.
JITTER = np.array([0.75, 0.25], np.float32)


def load(split: str):
    d = np.load(OUT / f"vitals_{split}.npz", allow_pickle=True)
    x = d["x"].reshape(-1, 1, WINDOW, N_CH).astype(np.float32)
    return x, d["y"].astype(np.float32), d


def mae_report(model, x, y, tag: str) -> dict:
    pred = model.predict(x, verbose=0)
    base = persistence_baseline(x.reshape(len(x), WINDOW, N_CH), HORIZON, N_CH)

    # Per channel, in clinical units, because "MAE 0.04" means nothing at a
    # bedside and "0.8 bpm" means everything.
    p = pred.reshape(-1, HORIZON, N_CH)
    t = y.reshape(-1, HORIZON, N_CH)
    hr_mae = float(np.abs(p[..., 0] - t[..., 0]).mean() * HR_SCALE)
    spo2_mae = float(np.abs(p[..., 1] - t[..., 1]).mean() * SPO2_SCALE)

    m_model = float(np.abs(pred - y).mean())
    m_base = float(np.abs(base - y).mean())
    print(f"  {tag:26s} HR {hr_mae:5.2f} bpm   SpO2 {spo2_mae:4.2f} %   "
          f"(vs persistence {100 * (1 - m_model / m_base):+.1f}%)")
    return {"hr_mae_bpm": hr_mae, "spo2_mae_pct": spo2_mae,
            "model_mae": m_model, "baseline_mae": m_base}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--epochs", type=int, default=150)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--seed", type=int, default=20260817)
    args = ap.parse_args()

    tf.keras.utils.set_random_seed(args.seed)

    xtr, ytr, _ = load("train")
    xva, yva, dva = load("validation")
    nva = dva["is_normal"]
    xva_n, yva_n = xva[nva], yva[nva]
    print(f"train {len(xtr)} windows   validation {len(xva_n)} normal (of {len(xva)})")

    jitter = tf.constant(JITTER)

    def augment(x, y):
        # One offset per channel, applied identically to history and horizon so
        # only the level moves and the dynamics are untouched.
        off = tf.random.uniform([N_CH], -1.0, 1.0) * jitter
        return x + off, y + tf.tile(off, [HORIZON])

    train_ds = (
        tf.data.Dataset.from_tensor_slices((xtr, ytr))
        .shuffle(len(xtr), seed=args.seed)
        .map(augment, num_parallel_calls=tf.data.AUTOTUNE)
        .batch(args.batch)
        .prefetch(tf.data.AUTOTUNE)
    )

    model = build_forecaster(N_CH, HORIZON, WINDOW, name="vitals_forecaster")
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3),
                  loss=tf.keras.losses.Huber(delta=1.0), metrics=["mae"])
    model.fit(
        train_ds, validation_data=(xva_n, yva_n),
        epochs=args.epochs, verbose=2,
        callbacks=[
            tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=15,
                                             restore_best_weights=True),
            tf.keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.5,
                                                 patience=6, min_lr=1e-5),
        ],
    )

    print("\nDETECTION (AUC of the forecast residual, label = window abnormal):")
    print(f"  {'dataset':26s} {'CNN':>8s} {'persist':>9s}")
    metrics = {"detection": {}}
    for split in ("validation", "test"):
        x, y, d = load(split)
        abnormal = ~d["is_normal"]
        if abnormal.sum() == 0:
            continue
        s_cnn = np.abs(model.predict(x, verbose=0) - y).mean(1)
        s_per = np.abs(
            persistence_baseline(x.reshape(len(x), WINDOW, N_CH), HORIZON, N_CH) - y
        ).mean(1)
        a_cnn, a_per = detection_auc(s_cnn, abnormal), detection_auc(s_per, abnormal)
        metrics["detection"][split] = {"cnn_auc": a_cnn, "persistence_auc": a_per,
                                       "abnormal_windows": int(abnormal.sum())}
        print(f"  {split:26s} {a_cnn:8.4f} {a_per:9.4f}"
              + ("   <- headline (unseen patients)" if split == "test" else ""))

    print("\nForecast error (diagnostic):")
    metrics["mae"] = {"validation_normal": mae_report(model, xva_n, yva_n,
                                                      "validation (normal)")}
    xte, yte, dte = load("test")
    nte = dte["is_normal"]
    metrics["mae"]["test_normal"] = mae_report(model, xte[nte], yte[nte],
                                               "test (normal patients)")

    MODELS.mkdir(parents=True, exist_ok=True)
    model.save(OUT / "vitals_forecaster.keras")

    calib = sweep_calibration_windows(["hr", "spo2"], WINDOW)
    blob = export_int8(model, calib, MODELS / "vitals_forecaster_int8.tflite",
                       (1, 1, WINDOW, N_CH))
    rep = tflite_report(blob)
    ok, msg = check_envelope(rep, ["hr", "spo2"])

    print(f"\nExported {rep['bytes']} bytes, {rep['n_operators']} operators: "
          f"{', '.join(rep['operators'])}")
    print(f"  {msg}")
    if not ok:
        raise SystemExit("REFUSING to ship: int8 input range does not cover the "
                         "design envelope")
    print("  envelope check: OK")

    (OUT / "vitals_forecaster_report.json").write_text(
        json.dumps({"metrics": metrics, "tflite": rep}, indent=2))
    print(f"Report -> {OUT / 'vitals_forecaster_report.json'}")


if __name__ == "__main__":
    main()

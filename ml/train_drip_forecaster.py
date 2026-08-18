#!/usr/bin/env python3
"""
Train Model 1: the drip forecaster. 64 s of drops_ratio in, 16 s out.

Trained unsupervised on normal windows only. The model therefore learns what
normal flow does next; the gap between its forecast and what actually happened is
the anomaly signal. Train it on occlusions too and that gap closes - the model
starts predicting occlusions accurately, and the detector goes quiet.

Loss is Huber (delta=1.0) rather than MSE. Drip data contains genuine single-
sample spikes (a drop detected late, then the next one on time), and MSE squares
those into the dominant term of the gradient, dragging the whole forecast toward
outliers. Huber is linear beyond delta, so a spike costs a bounded amount.

--- The headline metric is DETECTION, not forecast error --------------------

Measured against a persistence baseline (forecast = last value), this model has
WORSE forecast error on normal windows - about 8% worse on synthetic validation,
20% worse on real recordings. Taken alone that reads like a failed model.

It is not, and the reason is the mechanism the whole design rests on. The model
is trained on normal dynamics only, so it refuses to follow an abnormal one: when
flow starts collapsing it keeps predicting normal flow, and the residual
explodes. Persistence follows anything by construction - during a slow occlusion
it tracks the collapse happily and its residual stays small. Being a docile
forecaster makes persistence a poor detector.

Measured as a detector on the leak-free real recordings:

    forecast-residual AUC     CNN 0.9889     persistence 0.8388

So forecast MAE is the wrong yardstick for this model's job and is reported below
only as a diagnostic. AUC is the number that decides whether it ships.

--- Level augmentation, and the measurement that forced it ------------------

First training run: +11.3% against baseline on synthetic validation, but FIVE
TIMES WORSE than baseline on real recordings. Diagnosis: 93% of the error on real
data was a constant offset, not a dynamics failure.

    synthetic normal windows   mean ratio 0.9984
    real normal windows        mean ratio 0.9490

The simulated infusions sit on target; the real bench rig runs about 5% slow -
roller clamp setting, tubing, head height, none of it a fault. The model had
quietly learned "normal means 1.0" and dragged every forecast toward it.

Absolute level is not the forecaster's job. Deciding whether 0.95 is acceptable
belongs to the clinical rules, which compare against the prescribed rate; the
forecaster only has to answer "given how this line is behaving, what happens
next". So each training window is shifted by a random offset applied identically
to history and horizon. The model can no longer read the level off its own bias
and must take it from the window - which is what makes it transfer from simulated
data to real hardware at all.
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

WINDOW, HORIZON, N_CH = 64, 16, 1
DRIP_SCALE = 0.35   # to convert normalised error back into ratio units

# Random level offset applied during training, in NORMALISED units. 1.0 here is
# 0.35 in ratio units - comfortably wider than the 0.05 gap measured between the
# simulator and the bench rig, so the invariance is not tuned to that one number.
LEVEL_JITTER = 1.0


def load(split: str):
    d = np.load(OUT / f"drip_{split}.npz", allow_pickle=True)
    x = d["x"].reshape(-1, 1, WINDOW, N_CH).astype(np.float32)
    y = d["y"].astype(np.float32)
    return x, y, d


def mae_report(model, x, y, tag: str) -> dict:
    pred = model.predict(x, verbose=0)
    base = persistence_baseline(x.reshape(len(x), WINDOW, N_CH), HORIZON, N_CH)

    m_model = float(np.abs(pred - y).mean())
    m_base = float(np.abs(base - y).mean())
    # Same numbers in drops-per-minute-ratio units, which is what a person can
    # actually judge.
    print(f"  {tag:26s} model MAE {m_model:.4f}  baseline {m_base:.4f}  "
          f"({100 * (1 - m_model / m_base):+.1f}% vs baseline)")
    return {"model_mae": m_model, "baseline_mae": m_base,
            "model_mae_ratio_units": m_model * DRIP_SCALE,
            "improvement_pct": 100 * (1 - m_model / m_base)}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--epochs", type=int, default=120)
    ap.add_argument("--batch", type=int, default=128)
    ap.add_argument("--seed", type=int, default=20260817)
    args = ap.parse_args()

    tf.keras.utils.set_random_seed(args.seed)

    xtr, ytr, _ = load("train")
    xva, yva, dva = load("validation")

    # Validation for EARLY STOPPING must be normal-only for the same reason the
    # training set is: stopping on a loss that includes occlusions would select
    # the checkpoint that predicts occlusions best.
    nva = dva["is_normal"]
    xva_n, yva_n = xva[nva], yva[nva]
    print(f"train {len(xtr)} windows   validation {len(xva_n)} normal "
          f"(of {len(xva)})")

    # Level augmentation - see module docstring. The offset is drawn per sample
    # per epoch and applied identically to the history and the horizon, so the
    # dynamics are untouched and only the operating level moves.
    def augment(x, y):
        offset = tf.random.uniform([], -LEVEL_JITTER, LEVEL_JITTER)
        return x + offset, y + offset

    train_ds = (
        tf.data.Dataset.from_tensor_slices((xtr, ytr))
        .shuffle(len(xtr), seed=args.seed)
        .map(augment, num_parallel_calls=tf.data.AUTOTUNE)
        .batch(args.batch)
        .prefetch(tf.data.AUTOTUNE)
    )

    model = build_forecaster(N_CH, HORIZON, WINDOW, name="drip_forecaster")
    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-3),
        loss=tf.keras.losses.Huber(delta=1.0),
        metrics=["mae"],
    )

    model.fit(
        train_ds,
        validation_data=(xva_n, yva_n),
        epochs=args.epochs, verbose=2,
        callbacks=[
            tf.keras.callbacks.EarlyStopping(
                monitor="val_loss", patience=15, restore_best_weights=True),
            tf.keras.callbacks.ReduceLROnPlateau(
                monitor="val_loss", factor=0.5, patience=6, min_lr=1e-5),
        ],
    )

    # --- Headline: detection. See the module docstring for why this, not MAE.
    print("\nDETECTION (AUC of the forecast residual, label = window abnormal):")
    print(f"  {'dataset':34s} {'CNN':>8s} {'persist':>9s}")
    metrics = {"detection": {}}

    for name in ("drip_validation", "drip_synthetic_test",
                 "drip_realtest_heldout", "drip_realtest_calibrated"):
        d = np.load(OUT / f"{name}.npz", allow_pickle=True)
        x = d["x"].reshape(-1, 1, WINDOW, N_CH).astype(np.float32)
        y = d["y"]
        abnormal = ~d["is_normal"]
        if abnormal.sum() == 0 or (~abnormal).sum() == 0:
            continue

        s_cnn = np.abs(model.predict(x, verbose=0) - y).mean(1)
        s_per = np.abs(
            persistence_baseline(x.reshape(len(x), WINDOW, N_CH), HORIZON, N_CH) - y
        ).mean(1)
        a_cnn, a_per = detection_auc(s_cnn, abnormal), detection_auc(s_per, abnormal)

        leak = str(d["leakage"]) if "leakage" in d else "n/a"
        metrics["detection"][name] = {"cnn_auc": a_cnn, "persistence_auc": a_per,
                                      "leakage": leak}
        print(f"  {name:34s} {a_cnn:8.4f} {a_per:9.4f}"
              + ("   <- headline (leak-free real)"
                 if name == "drip_realtest_heldout" else ""))

    # --- Diagnostic only.
    print("\nForecast error (diagnostic, normalised units):")
    metrics["mae"] = {"validation_normal": mae_report(model, xva_n, yva_n,
                                                      "validation (normal)")}
    for name in ("drip_realtest_heldout", "drip_realtest_calibrated"):
        d = np.load(OUT / f"{name}.npz", allow_pickle=True)
        x = d["x"].reshape(-1, 1, WINDOW, N_CH).astype(np.float32)
        n = d["is_normal"]
        if n.sum() > 0:
            metrics["mae"][f"{name}_normal"] = mae_report(
                model, x[n], d["y"][n],
                f"{name.replace('drip_realtest_', 'real ')} normal")

    MODELS.mkdir(parents=True, exist_ok=True)
    model.save(OUT / "drip_forecaster.keras")

    calib = sweep_calibration_windows(["drip"], WINDOW)
    blob = export_int8(model, calib, MODELS / "drip_forecaster_int8.tflite",
                       (1, 1, WINDOW, N_CH))
    rep = tflite_report(blob)
    ok, msg = check_envelope(rep, ["drip"])

    print(f"\nExported {rep['bytes']} bytes, {rep['n_operators']} operators: "
          f"{', '.join(rep['operators'])}")
    print(f"  {msg}")
    if not ok:
        raise SystemExit("REFUSING to ship: int8 input range does not cover the "
                         "design envelope - the model would be blind to extremes")
    print("  envelope check: OK")

    (OUT / "drip_forecaster_report.json").write_text(
        json.dumps({"metrics": metrics, "tflite": rep}, indent=2))
    print(f"Report -> {OUT / 'drip_forecaster_report.json'}")


if __name__ == "__main__":
    main()

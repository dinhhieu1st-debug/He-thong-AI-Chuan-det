#!/usr/bin/env python3
"""
Train Model 3: the vitals autoencoder over (hr_deviation, spo2).

Its role in the system is to answer one question, independently of everything
else: IS THIS PATIENT'S CURRENT PHYSIOLOGICAL STATE NORMAL? The fusion layer then
combines that answer with the drip side using explicit rules, so an occluded line
and a deteriorating patient stay distinguishable all the way to the nurse.

It complements rather than duplicates the vitals forecaster: the forecaster
judges DYNAMICS over 64 s, this judges the instantaneous OPERATING POINT. A
patient can sit at a steady, unremarkable-looking-per-channel but jointly
abnormal state that the forecaster finds perfectly predictable.

--- What the threshold is, and is not ----------------------------------------

Set at the 98th percentile of reconstruction error on NORMAL validation
snapshots. That is deliberately loose: a flag from this model does not raise an
alarm on its own, it feeds the fusion layer and still has to survive the K=11
persistence filter. A threshold tight enough to be trusted alone would miss the
marginal combinations this model exists to find.

Measured on the float model, then RE-measured on the int8 model that actually
ships, because a threshold calibrated against the wrong numerics is a silent
mis-calibration of the whole detector.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import tensorflow as tf

from common import (
    build_vitals_autoencoder,
    check_envelope,
    detection_auc,
    envelope_norm,
    export_int8,
    tflite_report,
)

REPO = Path(__file__).resolve().parents[1]
OUT = REPO / "ml" / "out"
MODELS = REPO / "ml" / "models"

N_CH = 2
CHANNELS = ["hr_dev", "spo2"]
PERCENTILE = 98.0
HR_SCALE, SPO2_CENTRE, SPO2_SCALE = 20.0, 97.0, 2.0


def load(split: str):
    d = np.load(OUT / f"vitals_ae_{split}.npz", allow_pickle=True)
    return d["x"].astype(np.float32), d["is_normal"]


def sweep_calibration(n: int = 2000, seed: int = 20260817) -> np.ndarray:
    rng = np.random.default_rng(seed)
    lo = np.array([envelope_norm(c)[0] for c in CHANNELS], np.float32)
    hi = np.array([envelope_norm(c)[1] for c in CHANNELS], np.float32)
    return (lo + (hi - lo) * rng.random((n, N_CH))).astype(np.float32)


def manifold_probe(err_fn) -> list[dict]:
    """Characterise what the model learned, on points chosen by hand.

    This is a probe, not a dataset result. Its purpose is to show whether the
    model captured a JOINT structure or merely two independent ranges - the whole
    justification for using a network here rather than two thresholds. The
    interesting row is the last one: each channel individually unremarkable, the
    combination not.
    """
    points = [
        ("resting, well saturated",      0.0,  98.0),
        ("mild tachycardia, well sat",  25.0,  98.0),
        ("resting, mild desaturation",   0.0,  93.0),
        ("bradycardia, well saturated", -25.0, 98.0),
        ("tachycardia WITH desaturation", 25.0, 93.0),
    ]
    rows = []
    for label, hr_dev, spo2 in points:
        x = np.array([[hr_dev / HR_SCALE, (spo2 - SPO2_CENTRE) / SPO2_SCALE]], np.float32)
        rows.append({"case": label, "hr_deviation": hr_dev, "spo2": spo2,
                     "recon_error": float(err_fn(x)[0])})
    return rows


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--batch", type=int, default=1024)
    ap.add_argument("--seed", type=int, default=20260817)
    args = ap.parse_args()

    tf.keras.utils.set_random_seed(args.seed)

    xtr, _ = load("train")
    xva, nva = load("validation")
    xva_n = xva[nva]
    print(f"train {len(xtr)} snapshots   validation {len(xva_n)} normal (of {len(xva)})")

    model = build_vitals_autoencoder()
    model.compile(optimizer=tf.keras.optimizers.Adam(1e-3), loss="mse")
    model.fit(
        xtr, xtr, validation_data=(xva_n, xva_n),
        epochs=args.epochs, batch_size=args.batch, verbose=2,
        callbacks=[
            tf.keras.callbacks.EarlyStopping(monitor="val_loss", patience=8,
                                             restore_best_weights=True),
            tf.keras.callbacks.ReduceLROnPlateau(monitor="val_loss", factor=0.5,
                                                 patience=4, min_lr=1e-5),
        ],
    )

    def err_float(x):
        return np.mean((model.predict(x, verbose=0) - x) ** 2, axis=1)

    threshold = float(np.percentile(err_float(xva_n), PERCENTILE))
    print(f"\nThreshold ({PERCENTILE:.0f}th pct of normal validation error): "
          f"{threshold:.6f}")

    metrics = {"threshold_float": threshold, "percentile": PERCENTILE, "splits": {}}
    for split in ("validation", "test"):
        x, normal = load(split)
        err = err_float(x)
        abnormal = ~normal
        auc = detection_auc(err, abnormal)
        fpr = float((err[normal] > threshold).mean())
        tpr = float((err[abnormal] > threshold).mean()) if abnormal.sum() else float("nan")
        metrics["splits"][split] = {"auc": auc, "false_positive_rate": fpr,
                                    "true_positive_rate": tpr,
                                    "abnormal_seconds": int(abnormal.sum())}
        print(f"  {split:11s} AUC {auc:.4f}   flags {fpr * 100:5.2f}% of normal, "
              f"{tpr * 100:5.2f}% of abnormal")

    MODELS.mkdir(parents=True, exist_ok=True)
    model.save(OUT / "vitals_ae.keras")

    blob = export_int8(model, sweep_calibration(),
                       MODELS / "vitals_ae_int8.tflite", (1, N_CH))
    rep = tflite_report(blob)
    ok, msg = check_envelope(rep, CHANNELS)
    print(f"\nExported {rep['bytes']} bytes, {rep['n_operators']} operators: "
          f"{', '.join(rep['operators'])}")
    print(f"  {msg}")
    if not ok:
        raise SystemExit("REFUSING to ship: int8 input range does not cover the "
                         "design envelope")
    print("  envelope check: OK")

    it = tf.lite.Interpreter(
        model_content=blob,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF)
    it.allocate_tensors()
    inp, out = it.get_input_details()[0], it.get_output_details()[0]
    si, zi = inp["quantization"]
    so, zo = out["quantization"]

    def err_int8(x):
        errs = []
        for s in x:
            q = np.clip(np.round(s / si) + zi, -128, 127).astype(np.int8)
            it.set_tensor(inp["index"], q.reshape(inp["shape"]))
            it.invoke()
            r = (it.get_tensor(out["index"]).astype(np.float32).ravel() - zo) * so
            errs.append(float(np.mean((r - s) ** 2)))
        return np.array(errs)

    threshold_int8 = float(np.percentile(err_int8(xva_n), PERCENTILE))
    xte, nte = load("test")
    # Subsample for the int8 pass: 300k single-tensor invocations is slow and the
    # estimate is already tight at this size.
    idx = np.random.default_rng(args.seed).choice(len(xte), 30_000, replace=False)
    e_te = err_int8(xte[idx])
    auc_int8 = detection_auc(e_te, ~nte[idx])
    fpr_int8 = float((e_te[nte[idx]] > threshold_int8).mean())

    print(f"\n  int8 threshold {threshold_int8:.6f} (float {threshold:.6f})")
    print(f"  int8 test AUC  {auc_int8:.4f} (float "
          f"{metrics['splits']['test']['auc']:.4f}),  flags "
          f"{fpr_int8 * 100:.2f}% of normal")

    metrics["threshold_int8"] = threshold_int8
    metrics["test_auc_int8"] = auc_int8
    metrics["test_fpr_int8"] = fpr_int8
    metrics["manifold_probe"] = manifold_probe(err_int8)

    print("\nWhat the model learned (probe points, int8 reconstruction error):")
    for r in metrics["manifold_probe"]:
        flag = "FLAG" if r["recon_error"] > threshold_int8 else "ok"
        print(f"  {r['case']:32s} HRdev {r['hr_deviation']:+6.1f}  "
              f"SpO2 {r['spo2']:5.1f}   err {r['recon_error']:.5f}  {flag}")

    (OUT / "vitals_ae_report.json").write_text(
        json.dumps({"metrics": metrics, "tflite": rep}, indent=2))
    print(f"\nReport -> {OUT / 'vitals_ae_report.json'}")


if __name__ == "__main__":
    main()

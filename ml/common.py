#!/usr/bin/env python3
"""
Shared model construction and int8 export for the three Smart IV models.

Two things in this file are load-bearing and easy to get wrong silently.

--- 1. Why Conv2D with a 1xk kernel instead of Conv1D ------------------------

The series is one-dimensional, so Conv1D is the obvious choice. It is also the
wrong one here. TFLite expands every Keras Conv1D into
EXPAND_DIMS -> CONV_2D -> RESHAPE, which turns a 6-operator model into a
15-operator one, and most of the added operators are not accelerated by the MVP
on the EFR32MG26. Writing the same computation as Conv2D with kernel (1, k) over
an input shaped (1, 64, C) produces exactly one CONV_2D per layer, all of them
MVP-accelerated, for identical arithmetic.

Also deliberately avoided: LSTM/GRU (present in TFLM, not MVP-accelerated) and
dilated convolutions (the MVP does not support dilation at all).

--- 2. Why the quantisation calibration set is NOT the training set ----------

The forecasters are trained on normal windows only - that is what makes forecast
error meaningful as an anomaly signal. But if the same normal-only data is used
as the `representative_dataset` for int8 conversion, TFLite measures the input
range as roughly +/-0.29 normalised units and sets the int8 scale accordingly.
Every abnormal input then saturates to the same clipped value, and the chip
becomes unable to distinguish "slightly slow" from "completely occluded".

Nothing catches this. The model converts cleanly, runs fast, reports sensible
numbers on normal data, and is blind exactly when it matters.

So calibration uses a DESIGN ENVELOPE: the full range each sensor can physically
report, swept explicitly. That range is a property of the hardware and the
clinical limits, declared below - it is not measured from the test set, so no
test data influences the exported model.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import tensorflow as tf

# --- Design envelope, in NORMALISED units ------------------------------------
# Derived from the sensors' physical range and the firmware's clinical limits,
# NOT from any dataset. Widen these if the hardware changes; never narrow them to
# fit observed data.
DRIP_ENVELOPE = (0.0, 3.5)      # ratio: 0 = fully occluded, 3.5 = free flow
HR_ENVELOPE = (30.0, 200.0)     # bpm, MAX30102 reporting range
# Model 3 sees HR as a deviation from the patient's own baseline, so its envelope
# is a spread, not an absolute range: +/-70 bpm covers any excursion the sensor
# can report from any plausible baseline.
HR_DEV_ENVELOPE = (-70.0, 70.0)
SPO2_ENVELOPE = (70.0, 100.0)   # %

DRIP_CENTRE, DRIP_SCALE = 1.0, 0.35
HR_CENTRE, HR_SCALE = 80.0, 20.0
SPO2_CENTRE, SPO2_SCALE = 97.0, 2.0


def _norm(v, centre, scale):
    return (np.asarray(v, np.float32) - centre) / scale


def envelope_norm(kind: str) -> tuple[float, float]:
    if kind == "drip":
        return tuple(_norm(DRIP_ENVELOPE, DRIP_CENTRE, DRIP_SCALE))
    if kind == "hr":
        return tuple(_norm(HR_ENVELOPE, HR_CENTRE, HR_SCALE))
    if kind == "hr_dev":
        return tuple(_norm(HR_DEV_ENVELOPE, 0.0, HR_SCALE))
    if kind == "spo2":
        return tuple(_norm(SPO2_ENVELOPE, SPO2_CENTRE, SPO2_SCALE))
    raise ValueError(kind)


def build_forecaster(n_channels: int, horizon: int, window: int = 64,
                     name: str = "forecaster") -> tf.keras.Model:
    """The 1D-CNN forecaster, written as Conv2D so the MVP can run every layer."""
    inp = tf.keras.Input(shape=(1, window, n_channels), name="window")
    x = tf.keras.layers.Conv2D(16, (1, 5), strides=(1, 2), padding="same",
                               activation="relu")(inp)
    x = tf.keras.layers.Conv2D(32, (1, 5), strides=(1, 2), padding="same",
                               activation="relu")(x)
    x = tf.keras.layers.Conv2D(32, (1, 3), strides=(1, 2), padding="same",
                               activation="relu")(x)
    # Static shape. A dynamic Flatten would emit SHAPE/PACK operators that TFLM
    # has to evaluate at runtime and the MVP cannot touch.
    x = tf.keras.layers.Reshape((int(window / 8) * 32,))(x)
    x = tf.keras.layers.Dense(32, activation="relu")(x)
    out = tf.keras.layers.Dense(horizon * n_channels, activation="linear",
                                name="forecast")(x)
    return tf.keras.Model(inp, out, name=name)


def build_vitals_autoencoder(name: str = "vitals_ae") -> tf.keras.Model:
    """Autoencoder over (hr_deviation, spo2) - the one genuinely joint pair.

    The bottleneck is ONE unit for two inputs. Normal cardiorespiratory states do
    not fill the HR/SpO2 plane; they lie close to a curve, and a single latent is
    enough to trace it while being far too small to memorise anything off it. Two
    latents for two inputs would be an identity map and would reconstruct
    anomalies just as happily as normal states, which is the classic way to build
    an autoencoder that detects nothing.
    """
    inp = tf.keras.Input(shape=(2,), name="vitals")
    x = tf.keras.layers.Dense(4, activation="relu")(inp)
    x = tf.keras.layers.Dense(1, activation="relu")(x)
    x = tf.keras.layers.Dense(4, activation="relu")(x)
    out = tf.keras.layers.Dense(2, activation="linear", name="recon")(x)
    return tf.keras.Model(inp, out, name=name)


def build_autoencoder(n_channels: int = 3, name: str = "snapshot_ae") -> tf.keras.Model:
    """Bottleneck autoencoder over one instant. The 2-unit layer is the point:
    it is too small to memorise, so reconstruction only succeeds for operating
    points that resemble the normal ones."""
    inp = tf.keras.Input(shape=(n_channels,), name="snapshot")
    x = tf.keras.layers.Dense(3, activation="relu")(inp)
    x = tf.keras.layers.Dense(2, activation="relu")(x)
    x = tf.keras.layers.Dense(3, activation="relu")(x)
    out = tf.keras.layers.Dense(n_channels, activation="linear", name="recon")(x)
    return tf.keras.Model(inp, out, name=name)


def sweep_calibration_windows(channel_kinds: list[str], window: int,
                              n: int = 400, seed: int = 20260817) -> np.ndarray:
    """Synthetic inputs sweeping the declared design envelope.

    Not data - a deliberate probe of the input space, so the int8 scale covers
    everything the sensors can report rather than only what normal patients did.
    """
    rng = np.random.default_rng(seed)
    lo = np.array([envelope_norm(k)[0] for k in channel_kinds], np.float32)
    hi = np.array([envelope_norm(k)[1] for k in channel_kinds], np.float32)

    out = []
    for _ in range(n):
        # Flat lines at random levels, plus ramps spanning the envelope, plus
        # noise: between them these reach both extremes on every channel.
        mode = rng.integers(0, 3)
        if mode == 0:
            level = lo + (hi - lo) * rng.random(len(lo))
            w = np.tile(level, (window, 1))
        elif mode == 1:
            a = lo + (hi - lo) * rng.random(len(lo))
            b = lo + (hi - lo) * rng.random(len(lo))
            t = np.linspace(0, 1, window)[:, None]
            w = a + (b - a) * t
        else:
            w = lo + (hi - lo) * rng.random((window, len(lo)))
        out.append(w)

    return np.array(out, np.float32)


def _rebuild_with_fixed_batch(model: tf.keras.Model, input_shape) -> tf.keras.Model:
    """Clone `model` with its batch dimension fixed, carrying the weights over."""
    inp = tf.keras.Input(batch_shape=tuple(int(v) for v in input_shape))
    x = inp
    for layer in model.layers[1:]:          # skip the original InputLayer
        x = layer(x)
    clone = tf.keras.Model(inp, x, name=f"{model.name}_serving")
    clone.set_weights(model.get_weights())
    return clone


def export_int8(model: tf.keras.Model, calibration: np.ndarray, out_path: Path,
                input_shape) -> bytes:
    """Convert to int8 TFLite with a fixed batch of 1.

    Batch is pinned because a dynamic batch dimension makes TFLite emit SHAPE and
    PACK operators to compute tensor shapes at runtime - extra operators the MVP
    cannot accelerate, in a model whose whole design goal was to have six.
    """
    def representative_dataset():
        for sample in calibration:
            yield [sample.reshape(input_shape).astype(np.float32)]

    # Converting the trained model directly leaves the batch dimension dynamic,
    # and the Reshape before the dense head then has to be computed at runtime:
    # the exported graph gains SHAPE, STRIDED_SLICE and PACK operators and grows
    # from 6 operators to 11.
    #
    # Tracing a tf.function instead pins the batch but captures the Keras weights
    # as resource variables, and the export then fails at runtime with
    # READ_VARIABLE. The approach that gives a static shape AND a plain graph is
    # to rebuild the same architecture with the batch baked into the Input layer
    # and copy the trained weights across.
    serving = _rebuild_with_fixed_batch(model, input_shape)
    conv = tf.lite.TFLiteConverter.from_keras_model(serving)
    conv.optimizations = [tf.lite.Optimize.DEFAULT]
    conv.representative_dataset = representative_dataset
    conv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    conv.inference_input_type = tf.int8
    conv.inference_output_type = tf.int8
    blob = conv.convert()

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(blob)
    return blob


def tflite_report(blob: bytes) -> dict:
    """Operators, tensor quantisation and input coverage of the exported model.

    `input_covers_envelope` is the check that matters: if the int8 input range
    fails to span the design envelope, the model is blind to extremes and must
    not be shipped.
    """
    # The reference op resolver keeps XNNPACK from fusing the graph behind a
    # DELEGATE node, so the operator list below is the one TFLM will actually
    # have to resolve on the chip.
    it = tf.lite.Interpreter(
        model_content=blob,
        experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF,
    )
    it.allocate_tensors()

    inp = it.get_input_details()[0]
    out = it.get_output_details()[0]
    si, zi = inp["quantization"]
    so, zo = out["quantization"]

    ops = sorted({d["op_name"] for d in it._get_ops_details()})
    return {
        "bytes": len(blob),
        "operators": ops,
        "n_operators": len(it._get_ops_details()),
        "input_shape": [int(v) for v in inp["shape"]],
        "output_shape": [int(v) for v in out["shape"]],
        "input_scale": float(si),
        "input_zero_point": int(zi),
        "input_range_norm": [float((-128 - zi) * si), float((127 - zi) * si)],
        "output_scale": float(so),
        "output_zero_point": int(zo),
        "output_range_norm": [float((-128 - zo) * so), float((127 - zo) * so)],
    }


def check_envelope(report: dict, channel_kinds: list[str]) -> tuple[bool, str]:
    lo_needed = min(envelope_norm(k)[0] for k in channel_kinds)
    hi_needed = max(envelope_norm(k)[1] for k in channel_kinds)
    lo_got, hi_got = report["input_range_norm"]
    # Tolerance is relative: the int8 grid is 256 steps wide, so demanding an
    # exact match would fail on a single quantisation step of rounding.
    tol = (hi_needed - lo_needed) / 256.0
    ok = lo_got <= lo_needed + tol and hi_got >= hi_needed - tol
    msg = (f"int8 input covers {lo_got:+.2f}..{hi_got:+.2f}, "
           f"design envelope needs {lo_needed:+.2f}..{hi_needed:+.2f}")
    return ok, msg


def persistence_baseline(x: np.ndarray, horizon: int, n_channels: int) -> np.ndarray:
    """Forecast = repeat the last observed sample.

    The bar any forecaster must clear. On slowly varying physiology this baseline
    is genuinely strong, which is why quoting a model's MAE without it says
    almost nothing.
    """
    last = x[:, -1, :] if x.ndim == 3 else x[:, -1:]
    last = last.reshape(len(x), 1, n_channels)
    return np.repeat(last, horizon, axis=1).reshape(len(x), horizon * n_channels)


def detection_auc(score: np.ndarray, is_abnormal: np.ndarray) -> float:
    """Rank-based AUC. 1.0 separates perfectly, 0.5 is a coin flip."""
    order = np.argsort(score)
    lab = np.asarray(is_abnormal)[order]
    n_pos, n_neg = int(lab.sum()), int((~lab).sum())
    if n_pos == 0 or n_neg == 0:
        return float("nan")
    ranks = np.arange(1, len(lab) + 1)
    return float((ranks[lab].sum() - n_pos * (n_pos + 1) / 2) / (n_pos * n_neg))

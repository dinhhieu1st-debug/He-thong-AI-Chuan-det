"""Export the scikit-learn MLP to a small NumPy-only JSON artifact."""

from __future__ import annotations

import json
from pathlib import Path

import joblib


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "models" / "mlp_baseline" / "model.joblib"
OUTPUT = ROOT / "models" / "mlp_baseline" / "model_numpy.json"


def main() -> None:
    pipeline = joblib.load(SOURCE)
    scaler = pipeline.named_steps["scaler"]
    classifier = pipeline.named_steps["classifier"]
    artifact = {
        "format": "iv_drip_numpy_mlp_v1",
        "input_shape": [20, 3],
        "flatten_order": [
            "all 20 ratio values",
            "all 20 error_percent values",
            "all 20 delta_ratio values",
        ],
        "classes": [int(value) for value in classifier.classes_],
        "scaler_mean": scaler.mean_.tolist(),
        "scaler_scale": scaler.scale_.tolist(),
        "weights": [matrix.tolist() for matrix in classifier.coefs_],
        "biases": [vector.tolist() for vector in classifier.intercepts_],
        "activation": "relu",
        "output": "softmax",
    }
    OUTPUT.write_text(json.dumps(artifact, separators=(",", ":")), encoding="utf-8")
    print(f"Saved {OUTPUT} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

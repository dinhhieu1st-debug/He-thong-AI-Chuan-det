"""Train and evaluate the reproducible MLP baseline.

The test split is evaluated only after the single predefined model has fitted.
No test samples are used for fitting, early stopping, or hyperparameter choice.
"""

from __future__ import annotations

import argparse
import csv
import json
import platform
import sys
from pathlib import Path

import joblib
import numpy as np
import sklearn
from sklearn.metrics import (
    accuracy_score,
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
)
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.utils.class_weight import compute_sample_weight


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRAIN = ROOT / "data" / "processed" / "synthetic_train.csv"
DEFAULT_VALIDATION = ROOT / "data" / "processed" / "synthetic_validation.csv"
DEFAULT_TEST = ROOT / "data" / "processed" / "synthetic_test.csv"
DEFAULT_OUTPUT = ROOT / "models" / "mlp_baseline"
LABEL_NAMES = {1: "NORMAL", 2: "ATTENTION", 3: "WARNING"}
SEED = 20260813


def load_split(path: Path) -> tuple[np.ndarray, np.ndarray, list[dict[str, str]], list[str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"Missing CSV header: {path}")
        feature_names = [
            name
            for name in reader.fieldnames
            if name.startswith("ratio_t_minus_")
            or name.startswith("error_percent_t_minus_")
            or name.startswith("delta_ratio_t_minus_")
        ]
        if len(feature_names) != 60:
            raise ValueError(f"Expected 60 features, found {len(feature_names)} in {path}")

        rows = list(reader)

    x = np.asarray([[float(row[name]) for name in feature_names] for row in rows], dtype=np.float32)
    y = np.asarray([int(row["label"]) for row in rows], dtype=np.int64)
    metadata = [
        {
            "session_id": row["session_id"],
            "preset": row["preset"],
            "scenario": row["scenario"],
            "window_end_drop_number": row["window_end_drop_number"],
        }
        for row in rows
    ]
    if not np.isfinite(x).all():
        raise ValueError(f"Non-finite features found in {path}")
    if set(np.unique(y)) != {1, 2, 3}:
        raise ValueError(f"Expected labels 1,2,3 in {path}; found {sorted(set(y))}")
    return x, y, metadata, feature_names


def load_session_ids(path: Path) -> set[str]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return {row["session_id"] for row in csv.DictReader(handle)}


def evaluate(y_true: np.ndarray, y_pred: np.ndarray) -> dict:
    labels = [1, 2, 3]
    report = classification_report(
        y_true,
        y_pred,
        labels=labels,
        target_names=[LABEL_NAMES[label] for label in labels],
        output_dict=True,
        zero_division=0,
    )
    normal_mask = y_true == 1
    non_warning_mask = y_true != 3
    return {
        "samples": int(len(y_true)),
        "accuracy": float(accuracy_score(y_true, y_pred)),
        "balanced_accuracy": float(balanced_accuracy_score(y_true, y_pred)),
        "macro_f1": float(report["macro avg"]["f1-score"]),
        "warning_recall": float(report["WARNING"]["recall"]),
        "attention_recall": float(report["ATTENTION"]["recall"]),
        "normal_recall": float(report["NORMAL"]["recall"]),
        "any_alert_false_alarm_on_normal": float(np.mean(y_pred[normal_mask] != 1)),
        "warning_false_alarm_rate": float(np.mean(y_pred[non_warning_mask] == 3)),
        "confusion_matrix_rows_actual_cols_predicted": confusion_matrix(
            y_true, y_pred, labels=labels
        ).tolist(),
        "classification_report": report,
    }


def write_predictions(
    path: Path,
    metadata: list[dict[str, str]],
    y_true: np.ndarray,
    y_pred: np.ndarray,
    probabilities: np.ndarray,
) -> None:
    fieldnames = [
        "session_id",
        "preset",
        "scenario",
        "window_end_drop_number",
        "actual_label",
        "predicted_label",
        "prob_normal",
        "prob_attention",
        "prob_warning",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for meta, actual, predicted, probs in zip(metadata, y_true, y_pred, probabilities):
            writer.writerow(
                {
                    **meta,
                    "actual_label": int(actual),
                    "predicted_label": int(predicted),
                    "prob_normal": f"{probs[0]:.8f}",
                    "prob_attention": f"{probs[1]:.8f}",
                    "prob_warning": f"{probs[2]:.8f}",
                }
            )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--train", type=Path, default=DEFAULT_TRAIN)
    parser.add_argument("--validation", type=Path, default=DEFAULT_VALIDATION)
    parser.add_argument("--test", type=Path, default=DEFAULT_TEST)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    np.random.seed(SEED)
    x_train, y_train, _, feature_names = load_split(args.train)
    x_validation, y_validation, validation_meta, validation_features = load_split(args.validation)
    x_test, y_test, test_meta, test_features = load_split(args.test)
    if feature_names != validation_features or feature_names != test_features:
        raise ValueError("Feature columns differ between dataset splits")

    train_sessions = load_session_ids(args.train)
    validation_sessions = load_session_ids(args.validation)
    test_sessions = load_session_ids(args.test)
    if train_sessions & validation_sessions or train_sessions & test_sessions or validation_sessions & test_sessions:
        raise ValueError("Session leakage detected between train/validation/test")

    model = Pipeline(
        steps=[
            ("scaler", StandardScaler()),
            (
                "classifier",
                MLPClassifier(
                    hidden_layer_sizes=(64, 32),
                    activation="relu",
                    solver="adam",
                    alpha=1e-4,
                    batch_size=256,
                    learning_rate_init=1e-3,
                    max_iter=120,
                    shuffle=True,
                    random_state=SEED,
                    early_stopping=False,
                    tol=1e-5,
                    n_iter_no_change=15,
                    verbose=True,
                ),
            ),
        ]
    )
    sample_weight = compute_sample_weight(class_weight="balanced", y=y_train)
    model.fit(x_train, y_train, classifier__sample_weight=sample_weight)

    validation_pred = model.predict(x_validation)
    validation_prob = model.predict_proba(x_validation)
    test_pred = model.predict(x_test)
    test_prob = model.predict_proba(x_test)

    args.output.mkdir(parents=True, exist_ok=True)
    metrics = {
        "model": "MLPClassifier baseline",
        "research_only": True,
        "seed": SEED,
        "architecture": {
            "input_features": 60,
            "sequence_interpretation": "20 drops x 3 features, flattened",
            "hidden_layers": [64, 32],
            "output_classes": LABEL_NAMES,
        },
        "dataset": {
            "train_samples": int(len(y_train)),
            "validation_samples": int(len(y_validation)),
            "test_samples": int(len(y_test)),
            "train_sessions": len(train_sessions),
            "validation_sessions": len(validation_sessions),
            "test_sessions": len(test_sessions),
            "test_used_for_model_selection": False,
        },
        "validation": evaluate(y_validation, validation_pred),
        "test": evaluate(y_test, test_pred),
        "training": {
            "iterations": int(model.named_steps["classifier"].n_iter_),
            "final_loss": float(model.named_steps["classifier"].loss_),
            "loss_curve": [float(value) for value in model.named_steps["classifier"].loss_curve_],
        },
        "runtime": {
            "python": sys.version,
            "platform": platform.platform(),
            "numpy": np.__version__,
            "scikit_learn": sklearn.__version__,
            "joblib": joblib.__version__,
        },
        "metric_definitions": {
            "any_alert_false_alarm_on_normal": "fraction of actual NORMAL predicted ATTENTION or WARNING",
            "warning_false_alarm_rate": "fraction of actual NORMAL/ATTENTION predicted WARNING",
        },
    }
    joblib.dump(model, args.output / "model.joblib")
    (args.output / "feature_names.json").write_text(
        json.dumps(feature_names, indent=2), encoding="utf-8"
    )
    (args.output / "metrics.json").write_text(
        json.dumps(metrics, indent=2), encoding="utf-8"
    )
    write_predictions(
        args.output / "validation_predictions.csv",
        validation_meta,
        y_validation,
        validation_pred,
        validation_prob,
    )
    write_predictions(
        args.output / "test_predictions.csv", test_meta, y_test, test_pred, test_prob
    )

    print(json.dumps({"validation": metrics["validation"], "test": metrics["test"]}, indent=2))
    print(f"Saved MLP baseline to {args.output}")


if __name__ == "__main__":
    main()

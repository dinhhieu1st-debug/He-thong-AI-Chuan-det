"""Train, evaluate, and export the IV-drip LSTM research prototype."""

from __future__ import annotations

import argparse
import copy
import csv
import json
import os
import platform
import random
import sys
from pathlib import Path

# Must be set before CUDA is initialized to make cuBLAS training reproducible.
os.environ.setdefault("CUBLAS_WORKSPACE_CONFIG", ":4096:8")

import numpy as np
import onnx
import onnxruntime as ort
import sklearn
import torch
from sklearn.metrics import (
    accuracy_score,
    balanced_accuracy_score,
    classification_report,
    confusion_matrix,
    f1_score,
)
from torch import nn
from torch.utils.data import DataLoader, TensorDataset

from lstm_model import DripLSTM


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TRAIN = ROOT / "data" / "processed" / "synthetic_train.csv"
DEFAULT_VALIDATION = ROOT / "data" / "processed" / "synthetic_validation.csv"
DEFAULT_TEST = ROOT / "data" / "processed" / "synthetic_test.csv"
DEFAULT_OUTPUT = ROOT / "models" / "lstm"
LABEL_NAMES = {1: "NORMAL", 2: "ATTENTION", 3: "WARNING"}
FEATURE_NAMES = ["ratio", "error_percent", "delta_ratio"]
SEQUENCE_LENGTH = 20
SEED = 20260813


def set_reproducible_seed() -> None:
    random.seed(SEED)
    np.random.seed(SEED)
    torch.manual_seed(SEED)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(SEED)
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False
    torch.use_deterministic_algorithms(True, warn_only=True)


def ordered_columns(prefix: str) -> list[str]:
    return [f"{prefix}_t_minus_{offset}" for offset in range(19, -1, -1)]


def load_split(path: Path) -> tuple[np.ndarray, np.ndarray, list[dict[str, str]], set[str]]:
    expected = {name for prefix in FEATURE_NAMES for name in ordered_columns(prefix)}
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None or not expected.issubset(reader.fieldnames):
            missing = sorted(expected - set(reader.fieldnames or []))
            raise ValueError(f"Missing sequence feature columns in {path}: {missing}")
        rows = list(reader)

    channels = []
    for prefix in FEATURE_NAMES:
        names = ordered_columns(prefix)
        channels.append(np.asarray([[float(row[name]) for name in names] for row in rows], dtype=np.float32))
    x = np.stack(channels, axis=2)
    y = np.asarray([int(row["label"]) - 1 for row in rows], dtype=np.int64)
    metadata = [
        {
            "session_id": row["session_id"],
            "preset": row["preset"],
            "scenario": row["scenario"],
            "window_end_drop_number": row["window_end_drop_number"],
        }
        for row in rows
    ]
    sessions = {row["session_id"] for row in rows}
    if x.shape[1:] != (SEQUENCE_LENGTH, len(FEATURE_NAMES)):
        raise ValueError(f"Unexpected input shape {x.shape}")
    if not np.isfinite(x).all() or set(np.unique(y)) != {0, 1, 2}:
        raise ValueError(f"Invalid features or labels in {path}")
    return x, y, metadata, sessions


def normalize(
    x_train: np.ndarray, x_validation: np.ndarray, x_test: np.ndarray
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    mean = x_train.mean(axis=(0, 1), dtype=np.float64).astype(np.float32)
    std = x_train.std(axis=(0, 1), dtype=np.float64).astype(np.float32)
    if np.any(std < 1e-8):
        raise ValueError(f"Near-zero feature standard deviation: {std}")
    return (
        (x_train - mean) / std,
        (x_validation - mean) / std,
        (x_test - mean) / std,
        mean,
        std,
    )


def make_loader(x: np.ndarray, y: np.ndarray, batch_size: int, shuffle: bool) -> DataLoader:
    generator = torch.Generator().manual_seed(SEED)
    dataset = TensorDataset(torch.from_numpy(x), torch.from_numpy(y))
    return DataLoader(
        dataset,
        batch_size=batch_size,
        shuffle=shuffle,
        num_workers=0,
        generator=generator,
        pin_memory=torch.cuda.is_available(),
    )


@torch.inference_mode()
def predict(model: nn.Module, loader: DataLoader, device: torch.device) -> tuple[np.ndarray, np.ndarray, float]:
    model.eval()
    probabilities = []
    labels = []
    loss_total = 0.0
    criterion = nn.CrossEntropyLoss(reduction="sum")
    for inputs, target in loader:
        inputs = inputs.to(device, non_blocking=True)
        target = target.to(device, non_blocking=True)
        logits = model(inputs)
        loss_total += float(criterion(logits, target).item())
        probabilities.append(torch.softmax(logits, dim=1).cpu().numpy())
        labels.append(target.cpu().numpy())
    probs = np.concatenate(probabilities)
    truth = np.concatenate(labels)
    return truth, probs, loss_total / len(truth)


def evaluate(y_zero_based: np.ndarray, probabilities: np.ndarray) -> dict:
    y_true = y_zero_based + 1
    y_pred = probabilities.argmax(axis=1) + 1
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
        "macro_f1": float(f1_score(y_true, y_pred, average="macro")),
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
    y_zero_based: np.ndarray,
    probabilities: np.ndarray,
) -> None:
    predicted = probabilities.argmax(axis=1) + 1
    actual = y_zero_based + 1
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
        for meta, truth, prediction, probs in zip(metadata, actual, predicted, probabilities):
            writer.writerow(
                {
                    **meta,
                    "actual_label": int(truth),
                    "predicted_label": int(prediction),
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
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--patience", type=int, default=8)
    args = parser.parse_args()

    set_reproducible_seed()
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    x_train, y_train, _, train_sessions = load_split(args.train)
    x_validation, y_validation, validation_meta, validation_sessions = load_split(args.validation)
    x_test, y_test, test_meta, test_sessions = load_split(args.test)
    if train_sessions & validation_sessions or train_sessions & test_sessions or validation_sessions & test_sessions:
        raise ValueError("Session leakage detected")
    x_train, x_validation, x_test, feature_mean, feature_std = normalize(
        x_train, x_validation, x_test
    )

    train_loader = make_loader(x_train, y_train, args.batch_size, shuffle=True)
    validation_loader = make_loader(x_validation, y_validation, args.batch_size, shuffle=False)
    test_loader = make_loader(x_test, y_test, args.batch_size, shuffle=False)

    counts = np.bincount(y_train, minlength=3).astype(np.float32)
    weights = len(y_train) / (3.0 * counts)
    criterion = nn.CrossEntropyLoss(weight=torch.tensor(weights, device=device))
    model = DripLSTM().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-3, weight_decay=1e-4)

    best_state = None
    best_validation_f1 = -1.0
    best_epoch = 0
    epochs_without_improvement = 0
    history = []
    for epoch in range(1, args.epochs + 1):
        model.train()
        train_loss_sum = 0.0
        for inputs, target in train_loader:
            inputs = inputs.to(device, non_blocking=True)
            target = target.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            logits = model(inputs)
            loss = criterion(logits, target)
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), max_norm=1.0)
            optimizer.step()
            train_loss_sum += float(loss.item()) * len(target)

        validation_truth, validation_prob, validation_loss = predict(model, validation_loader, device)
        validation_f1 = float(
            f1_score(validation_truth, validation_prob.argmax(axis=1), average="macro")
        )
        epoch_record = {
            "epoch": epoch,
            "train_loss": train_loss_sum / len(y_train),
            "validation_loss": validation_loss,
            "validation_macro_f1": validation_f1,
        }
        history.append(epoch_record)
        print(
            f"epoch={epoch:02d} train_loss={epoch_record['train_loss']:.6f} "
            f"val_loss={validation_loss:.6f} val_macro_f1={validation_f1:.6f}",
            flush=True,
        )

        if validation_f1 > best_validation_f1 + 1e-5:
            best_validation_f1 = validation_f1
            best_epoch = epoch
            best_state = copy.deepcopy(model.state_dict())
            epochs_without_improvement = 0
        else:
            epochs_without_improvement += 1
            if epochs_without_improvement >= args.patience:
                print(f"early_stopping epoch={epoch}", flush=True)
                break

    if best_state is None:
        raise RuntimeError("Training produced no checkpoint")
    model.load_state_dict(best_state)
    validation_truth, validation_prob, _ = predict(model, validation_loader, device)
    test_truth, test_prob, _ = predict(model, test_loader, device)
    validation_metrics = evaluate(validation_truth, validation_prob)
    test_metrics = evaluate(test_truth, test_prob)

    args.output.mkdir(parents=True, exist_ok=True)
    cpu_model = model.to("cpu").eval()
    checkpoint = {
        "state_dict": cpu_model.state_dict(),
        "architecture": {"input_size": 3, "hidden_size": 32, "num_classes": 3},
        "feature_mean": feature_mean,
        "feature_std": feature_std,
        "sequence_length": SEQUENCE_LENGTH,
        "feature_names": FEATURE_NAMES,
        "label_names": LABEL_NAMES,
        "seed": SEED,
    }
    torch.save(checkpoint, args.output / "model_checkpoint.pt")

    example = torch.zeros(1, SEQUENCE_LENGTH, len(FEATURE_NAMES), dtype=torch.float32)
    scripted = torch.jit.trace(cpu_model, example)
    scripted.save(str(args.output / "model_torchscript.pt"))
    torch.onnx.export(
        cpu_model,
        example,
        args.output / "model.onnx",
        input_names=["sequence"],
        output_names=["logits"],
        dynamic_axes={"sequence": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=17,
    )
    onnx.checker.check_model(onnx.load(args.output / "model.onnx"))

    ort_session = ort.InferenceSession(
        str(args.output / "model.onnx"), providers=["CPUExecutionProvider"]
    )
    verification_input = x_test[:64].astype(np.float32)
    with torch.inference_mode():
        torch_logits = cpu_model(torch.from_numpy(verification_input)).numpy()
    onnx_logits = ort_session.run(["logits"], {"sequence": verification_input})[0]
    max_export_difference = float(np.max(np.abs(torch_logits - onnx_logits)))
    export_predictions_match = bool(
        np.array_equal(torch_logits.argmax(axis=1), onnx_logits.argmax(axis=1))
    )
    if not export_predictions_match or max_export_difference > 1e-4:
        raise RuntimeError(
            f"ONNX verification failed: match={export_predictions_match}, "
            f"max_diff={max_export_difference}"
        )

    metrics = {
        "model": "DripLSTM",
        "research_only": True,
        "seed": SEED,
        "device_used_for_training": str(device),
        "architecture": {
            "input_shape": [SEQUENCE_LENGTH, len(FEATURE_NAMES)],
            "feature_names": FEATURE_NAMES,
            "lstm_hidden_size": 32,
            "dense_hidden_size": 16,
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
        "normalization": {
            "mean": feature_mean.tolist(),
            "std": feature_std.tolist(),
            "fit_on": "training split only",
        },
        "training": {
            "epochs_completed": len(history),
            "best_epoch": best_epoch,
            "best_validation_macro_f1": best_validation_f1,
            "history": history,
        },
        "validation": validation_metrics,
        "test": test_metrics,
        "export_verification": {
            "onnx_checker": "PASS",
            "sample_count": 64,
            "predictions_match": export_predictions_match,
            "max_absolute_logit_difference": max_export_difference,
        },
        "runtime": {
            "python": sys.version,
            "platform": platform.platform(),
            "numpy": np.__version__,
            "scikit_learn": sklearn.__version__,
            "torch": torch.__version__,
            "onnx": onnx.__version__,
            "onnxruntime": ort.__version__,
        },
        "metric_definitions": {
            "any_alert_false_alarm_on_normal": "fraction of actual NORMAL predicted ATTENTION or WARNING",
            "warning_false_alarm_rate": "fraction of actual NORMAL/ATTENTION predicted WARNING",
        },
    }
    (args.output / "metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    (args.output / "inference_config.json").write_text(
        json.dumps(
            {
                "sequence_length": SEQUENCE_LENGTH,
                "feature_names": FEATURE_NAMES,
                "feature_mean": feature_mean.tolist(),
                "feature_std": feature_std.tolist(),
                "classes": LABEL_NAMES,
                "onnx_input": "sequence",
                "onnx_output": "logits",
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    write_predictions(
        args.output / "validation_predictions.csv",
        validation_meta,
        validation_truth,
        validation_prob,
    )
    write_predictions(
        args.output / "test_predictions.csv", test_meta, test_truth, test_prob
    )
    print(json.dumps({"validation": validation_metrics, "test": test_metrics}, indent=2))
    print(f"Saved LSTM artifacts to {args.output}")


if __name__ == "__main__":
    main()

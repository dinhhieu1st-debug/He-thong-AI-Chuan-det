#!/usr/bin/env python3
"""
Build train/validation/test sets for Model 3, the VITALS autoencoder.

    [hr_deviation, spo2_norm]  ->  2 -> 4 -> 1 -> 4 -> 2  ->  reconstruction

--- Why this model sees vitals only, and not the drip channel ----------------

The first version of Model 3 took three channels, drip included, and combined
them inside the network. That was wrong twice over.

First, it had nothing to learn. No recording exists in which the same patient's
heart rate and the same patient's drip rate were measured together, so the two
sources had to be paired at random - and randomly paired channels are
independent by construction. The autoencoder was spending a 2-unit bottleneck
modelling three unrelated marginal distributions, which single thresholds already
cover.

Second, and worse, it reintroduced the exact defect v2 exists to remove. With the
drip channel inside the network, an occluded line raises the reconstruction error
and therefore contaminates the model's verdict on the PATIENT. That is the v1
coupling again, in miniature.

HR and SpO2, by contrast, are genuinely co-measured on the same patient in the
same second. That is a real joint distribution, and it is the only one available
anywhere in this system. So this model learns it, and nothing else.

The combination with the drip side happens afterwards, in explicit fusion logic:

    drip abnormal + vitals AE normal   -> line problem, patient fine   (YELLOW)
    vitals abnormal + drip normal      -> patient problem              (RED)
    both                               -> suspected fluid overload     (CRITICAL)

Written as rules, that logic is auditable and can be explained to a nurse. Learnt
as weights, it could not be.

--- Heart rate as a deviation ------------------------------------------------

HR enters as deviation from the patient's own baseline. Fed as an absolute, an
earlier version flagged 33% of NORMAL snapshots from unseen patients: it had
learned the training patients' resting rates and treated any other baseline as
an anomaly - detecting "unfamiliar person", not "patient in trouble". SpO2 stays
absolute, because 97% means the same thing in everyone.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]
DEFAULT_OUT = REPO / "ml" / "out"

HR_SCALE = 20.0
SPO2_CENTRE, SPO2_SCALE = 97.0, 2.0


def build(out_dir: Path) -> None:
    summary = {"channels": ["hr_deviation", "spo2_norm"],
               "source": "PhysioNet BIDMC only - genuinely co-measured",
               "splits": {}}

    for split in ("train", "validation", "test"):
        v = np.load(out_dir / f"vitals_{split}.npz", allow_pickle=True)
        hr_abs = v["raw"][:, :64, 0]
        spo2 = v["raw"][:, :64, 1]
        pid = v["patient_id"]
        window_normal = v["is_normal"]

        # Per-patient baseline from that patient's own normal windows - the
        # offline equivalent of the 60 s baseline the firmware locks in at the
        # bedside.
        hr_dev = np.empty_like(hr_abs)
        for p in np.unique(pid):
            sel = pid == p
            ref = hr_abs[sel & window_normal] if (sel & window_normal).any() else hr_abs[sel]
            hr_dev[sel] = hr_abs[sel] - float(np.median(ref))

        # One snapshot per second. Windows overlap heavily, so sampling per
        # window would count the same second up to 64 times.
        x = np.stack([hr_dev.reshape(-1) / HR_SCALE,
                      (spo2.reshape(-1) - SPO2_CENTRE) / SPO2_SCALE],
                     axis=-1).astype(np.float32)

        # Labelled per SECOND against the same clinical bounds the firmware
        # enforces, not per window - a window is abnormal if any of its 80
        # seconds is, which would mislabel 79 healthy snapshots.
        hr_now = hr_abs.reshape(-1)
        spo2_now = spo2.reshape(-1)
        is_normal = (hr_now >= 45.0) & (hr_now <= 150.0) & (spo2_now >= 90.0)
        patient = np.repeat(pid, 64)

        if split == "train":
            x, is_normal, patient = x[is_normal], is_normal[is_normal], patient[is_normal]

        path = out_dir / f"vitals_ae_{split}.npz"
        np.savez_compressed(path, x=x, is_normal=is_normal, patient_id=patient)

        summary["splits"][split] = {
            "snapshots": int(len(x)),
            "normal": int(is_normal.sum()),
            "abnormal": int((~is_normal).sum()),
            "patients": int(len(np.unique(patient))),
            "hr_dev_range": [float(x[:, 0].min() * HR_SCALE),
                             float(x[:, 0].max() * HR_SCALE)],
            "spo2_range": [float(x[:, 1].min() * SPO2_SCALE + SPO2_CENTRE),
                           float(x[:, 1].max() * SPO2_SCALE + SPO2_CENTRE)],
        }
        print(f"  {split:11s} {len(x):7d} snapshots  "
              f"({int(is_normal.sum())} normal / {int((~is_normal).sum())} abnormal)"
              f"  {len(np.unique(patient))} patients  -> {path.name}")

    (out_dir / "vitals_ae_dataset_summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\nSummary -> {out_dir / 'vitals_ae_dataset_summary.json'}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    build(ap.parse_args().out)


if __name__ == "__main__":
    main()

# Raspberry Pi 4 deployment status

## Target

- Host: `192.168.137.227`
- OS: Debian 12 Bookworm, aarch64, 64-bit
- RAM: 4 GB
- Remote directory: `/home/pi/iv_drip_ai`
- Password is not stored in this repository or the deployed application.

## Runtime

The Pi already contained these packages, so deployment did not install or
upgrade system packages:

- NumPy 2.2.6
- ONNX Runtime 1.23.2
- pyserial 3.5

## Deployed inference

- NumPy-only MLP artifact.
- ONNX LSTM artifact.
- Twenty-drop input buffer.
- Three outputs: NORMAL, ATTENTION, WARNING.
- Shadow comparison with model confidence and agreement/disagreement.
- Independent wall-clock no-drop watchdog for live serial mode.

## Verification

- Local and remote SHA-256 hashes matched for program, models, configuration,
  and replay data.
- Pi replay processed 180 records in approximately 0.107 seconds.
- Missing-drop replay raised the watchdog and both models reached WARNING during
  the abnormal/recovery sequence.
- Live serial verification is pending because no `/dev/ttyUSB*` or
  `/dev/ttyACM*` device was connected during deployment.

## Commands on the Pi

Live hardware:

```bash
~/iv_drip_ai/run_live.sh
```

The launcher opens serial immediately in physical-adjustment mode. Each detected
drop is displayed with milliseconds, seconds, and drops/minute before a preset is
selected. The operator can then enter one of these choices without stopping the
serial display:

- `1`: slow, 1500 ms/drop, 40 drops/minute
- `2`: normal, 1000 ms/drop, 60 drops/minute
- `3`: fast, 750 ms/drop, 80 drops/minute

The Pi-selected target overrides the firmware's compile-time target field for
feature calculation and watchdog timing. Presets can also be changed while the
monitor is running; each change intentionally resets the 20-drop AI buffer.

Replay self-test:

```bash
~/iv_drip_ai/run_replay.sh
```

This remains a research prototype and must not be used for clinical decisions.

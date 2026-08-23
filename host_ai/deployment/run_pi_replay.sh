#!/usr/bin/env bash
set -euo pipefail

cd /home/pi/iv_drip_ai
exec python3 -u inference/pi_demo.py --replay data/replay_missing_drop.csv

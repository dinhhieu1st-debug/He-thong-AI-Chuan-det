#!/usr/bin/env bash
set -euo pipefail

cd /home/pi/iv_drip_ai

port="${1:-}"
if [[ -z "$port" ]]; then
  for candidate in /dev/ttyUSB0 /dev/ttyACM0 /dev/ttyUSB1 /dev/ttyACM1; do
    if [[ -e "$candidate" ]]; then
      port="$candidate"
      break
    fi
  done
fi

if [[ -z "$port" ]]; then
  echo "[WAITING FOR HARDWARE] No /dev/ttyUSB* or /dev/ttyACM* device found."
  echo "Connect the NodeMCU USB cable to the Raspberry Pi, then run this command again."
  exit 2
fi

echo "[LIVE DEMO] Serial device: $port"
exec python3 -u inference/pi_demo.py \
  --port "$port" \
  --baudrate 115200 \
  --interactive

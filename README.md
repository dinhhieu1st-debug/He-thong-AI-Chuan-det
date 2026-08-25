# Smart IV Monitor – AI-based IV Drip Monitoring System

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![Platform: EFR32xG26](https://img.shields.io/badge/MCU-EFR32xG26-informational)](firmware/PIN_MAP.md)
[![Backend: .NET 8](https://img.shields.io/badge/Backend-.NET%208-512BD4)](software/server/README.md)

A student capstone project that monitors patients on an IV drip. A bedside
device measures heart rate, SpO2, drip rate, and remaining bag volume; data
is streamed to a web dashboard for nurses to watch in real time. When it
detects an abnormality, the G26 device raises a local alert via LED and
buzzer while sending the alert level and cause to the server.

> **Note:** This is a research/demo prototype, not a certified medical
> device. Do not use it as a substitute for certified clinical monitoring
> equipment.

## Table of contents

- [What the system does](#what-the-system-does)
- [Repository layout](#repository-layout)
- [Documentation](#documentation)
- [Where to start](#where-to-start)
- [Architecture overview](#architecture-overview)
- [Hardware and pinout](#hardware-and-pinout)
- [Zigbee protocol](#zigbee-protocol)
- [Operating sequence](#operating-sequence)
- [Alert logic](#alert-logic)
- [Web test mode](#web-test-mode)
- [Windows setup](#windows-setup)
- [Build and flash G26](#build-and-flash-g26)
- [Install MySQL and run the HIS Server](#install-mysql-and-run-the-his-server)
- [Run the gateway on Raspberry Pi](#run-the-gateway-on-raspberry-pi)
- [Startup order for every session](#startup-order-for-every-session)
- [Testing and troubleshooting](#testing-and-troubleshooting)
- [Pre-handoff build check](#pre-handoff-build-check)
- [License](#license)
- [Conclusion](#conclusion)

## What the system does

- Reads HR and SpO2 from the MAX30102.
- Detects individual drops, and computes drop interval and drops/minute.
- Measures the IV bag's remaining mass with a loadcell and HX711.
- Displays live readings on a 0.96", 128×64 OLED.
- Learns a vitals and drip-interval baseline before enabling alerts.
- Runs time-series AI directly on the G26 device.
- Fuses vitals and drip alerts into a single three-level severity.
- Transfers data bidirectionally over Zigbee ZCL Attributes; G26 never builds JSON itself.
- Shows bed status, charts, state, and alert cause on the HIS web UI.
- Lets staff set the target drip rate, tare the scale, calibrate HR, and enable test mode from the web.

Full detail on the data flow, Zigbee protocol, and alert logic is in
[`docs/architecture.md`](docs/architecture.md).

## Repository layout

The repo has two groups of directories: the Simplicity Studio (SLC)
firmware project skeleton — fixed in place, cannot be moved — and the
software the team wrote by hand.

### Required at the repo root (SLC/firmware toolchain)

| Directory / file | Contents |
|---|---|
| `firmware/` | G26 firmware: sensors, OLED, AI, alerts, and Zigbee (paths declared in `smart-iv-monitor.slcp`) |
| `firmware/models/` | TFLite Micro model embedded in the firmware |
| `firmware/PIN_MAP.md` | Full G26 pinout reference |
| `main.c`, `*.slcp`, `*.slps`, `*.pintool` | Entry point and Simplicity Studio project files |
| `autogen/`, `config/`, `cmake_gcc/`, `bootloader/` | Generated/managed by SLC — rebuild with `tools/build_firmware.ps1` |
| `simplicity_sdk_2025.12.3/`, `aiml_2.2.2/` | Vendor SDK and AI libraries, not edited directly |
| `tools/` | Build, flash, firewall, and Pi launch scripts (invoked relative to the repo root) |

### Team-written software

| Directory | Contents |
|---|---|
| `software/gateway-pi/` | MQTT ↔ TCP gateway source and deployment config for the Pi |
| `software/server/` | HIS Server .NET 8, web UI, API, and database |
| `software/host_ai/` | Dataset, training code, and documentation for the drip AI |

### Documentation

All system-level documentation lives under [`docs/`](docs/README.md) — see
the index there to find the right file (architecture, setup guide, common
issues, firmware↔HIS integration).

## Where to start

- Setting up the system for the first time: [`docs/setup-guide.md`](docs/setup-guide.md)
- Understanding the architecture/protocol: [`docs/architecture.md`](docs/architecture.md)
- Hit an error: [`docs/troubleshooting.md`](docs/troubleshooting.md)
- Updating the firmware↔HIS alert contract or redeploying the Pi converter: [`docs/system-integration.md`](docs/system-integration.md)
- Pre-submission build check: see the end of [`docs/setup-guide.md`](docs/setup-guide.md#pre-submission-build-check)

## Architecture overview

```text
MAX30102 + drop sensor + HX711/loadcell
                    |
                    v
        EFR32xG26 / BRD2709A
  sensors + AI + OLED + LED + buzzer
                    |
                    | Zigbee ZCL Attribute
                    v
       Zigbee coordinator on Raspberry Pi
                    |
                    v
       Zigbee2MQTT -> MQTT -> TCP gateway
                    |
                    | TCP 5000
                    v
          HIS Server .NET 8 + MySQL
                    |
                    v
             Web UI + SignalR realtime
```

Commands flow the other way:

```text
HIS Web -> HIS Server -> TCP Gateway -> MQTT
        -> Zigbee2MQTT -> ZCL Attribute -> G26
```

G26 is where sensors are read, AI runs, and the final alert level is
decided. The server receives that result to store and display it — it does
not reimplement the device's vitals alert algorithm.

## Repository structure

| Path | Contents |
|---|---|
| `firmware/` | Sensors, OLED, AI, and alert control on G26 |
| `firmware/models/` | TFLite Micro model embedded in the firmware |
| `config/zcl/` | Custom Zigbee cluster and attributes |
| `gateway-pi/` | MQTT ↔ HIS TCP gateway and Pi configuration |
| `server/` | HIS Server .NET 8, database, frontend, and tests |
| `host_ai/` | Dataset, training code, and AI tooling |
| `tools/` | Firmware build/flash scripts and system launch scripts |
| `bootloader/` | G26 bootloader project |
| `cmake_gcc/` | Firmware build configuration |

The official server lives under `server/`; the project no longer uses a
`demo1/` folder.

## Hardware and pinout

| Block | Signal | G26 pin |
|---|---|---|
| Drop sensor | D0/OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 and OLED | I2C SCL | PC05 |
| MAX30102 and OLED | I2C SDA | PC07 |
| Green LED | Output | PA07 |
| Yellow LED | Output | PA04 |
| Red LED | Output | PA05 |
| Active-low buzzer | Output | PC06 |
| Tare button | BTN0 | PB00 |

All modules use 3.3 V logic and share a common GND. The OLED and MAX30102
share the same I2C bus, so keep the wiring short, use appropriate pull-up
resistors, and keep the I2C lines away from the buzzer. PC06 LOW sounds the
buzzer; HIGH turns it off.

## Zigbee protocol

G26 reports data via ZCL Attributes:

- Endpoint: `2`
- Cluster: `0xFC01`
- Manufacturer Code: `0x1049`
- Cluster Name: `Smart IV Vitals`
- Model: `SmartIV-Sensor`

| Attribute | Contents |
|---:|---|
| `0x0000–0x0006` | HR, SpO2, flow, drop ratio, alarm, weight, and drops/minute |
| `0x0007–0x000A` | Target rate, tare command, and HR calibration |
| `0x000B–0x000E` | Baseline and event counters |
| `0x000F–0x0015` | AI flags, forecast, and trend |
| `0x0016–0x0017` | Remaining volume and remaining time |
| `0x0018–0x001B` | Monitoring, training progress, and alerts-enabled state |
| `0x001C–0x001E` | Drop interval, drop counter, and server-set drip target |
| `0x001F–0x0022` | Test mode, AI input data, and vitals level |

Zigbee2MQTT converts these attributes into MQTT JSON. The Pi gateway sends
that JSON to the HIS Server and routes commands from the server back to the
correct attribute on G26.

## Operating sequence

1. Power on G26 and the sensors.
2. G26 tares the loadcell; do not hang the bag during this step.
3. The device reports sensor status so it can be verified on the web.
4. Hang the bag and set the target drip rate on the HIS Web UI.
5. Once the target is received, G26 collects `20/20` drop intervals and `64/64` vitals samples.
6. AI alerts stay disabled while sampling, to avoid false alarms at startup.
7. Once enough data is collected, `alerts_armed` turns on and monitoring begins.
8. G26 continuously sends readings, alert level, and alert cause to the server.

## Alert logic

### Vitals

- The first 60 valid samples build the HR/SpO2 baseline.
- 64 samples provide enough history for the time-series AI.
- Deviation under 15%: level 1.
- Deviation from 15% to under 20%: level 2.
- Deviation of 20% or more: level 3.
- HR below 45, HR above 150, or SpO2 below 90: level 3.
- When the finger leaves the MAX30102, the data is marked "no signal" instead of holding the old value indefinitely.

### Drip rate

- Deviation within `±200 ms`: level 1.
- Deviation over `200 ms` up to `800 ms`: level 2.
- Deviation over `800 ms`: level 3.
- A watchdog detects an excessively long gap with no drop, for blockage/no-drop alerts.
- The drip AI uses a 20-interval window to analyze trend.

### Final alert fusion

| Vitals | Drip | Output |
|---:|---:|---:|
| 1 | 1 | 1 – normal |
| 3 | 3 | 3 – critical |
| Any other combination | | 2 – attention |

### LED and buzzer

- Level 1: green LED, buzzer off.
- Level 2: yellow LED; buzzer beeps for 0.5 s, rests for 3 s.
- Level 3: red LED blinking; buzzer toggles every 0.25 s.

## Web test mode

- `Real data`: uses the real sensor.
- `Test HR L2`: generates an HR deviation of about 17% to test level 2.
- `Test HR+O2 L3`: generates HR and SpO2 deviations of about 25% to test level 3.

Real sensor data and AI input data are shown separately. Turning test mode
off restores the firmware's saved real history, so it does not need to
relearn from scratch.

## Windows setup

You'll need Git, .NET 8 SDK, MySQL 8.x, Simplicity Studio 6/Silicon Labs
Commander, and PuTTY/Plink.

```powershell
git --version
dotnet --list-sdks
plink -V
```

Clone the main branch:

```powershell
cd C:\Users\<USER>\Documents
git clone --branch main --single-branch https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

## Build and flash G26

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

The firmware image ends up at `cmake_gcc/build/base/smart-iv-monitor.hex`.

If the board has no bootloader yet, or the whole chip was just erased:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_bootloader_and_app.ps1
```

## Install MySQL and run the HIS Server

Start MySQL from an administrator PowerShell:

```powershell
Get-Service MySQL*
Start-Service MySQL84
```

The service name may differ from `MySQL84`. Create the database the first time:

```powershell
Get-Content -Raw .\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Then run the files under `server/database/migrations/` in name order. Never
commit MySQL credentials to Git; configure them with user-secrets instead:

```powershell
cd .\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MYSQL_PASSWORD>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
dotnet run --launch-profile http
```

A correct startup logs:

```text
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:3000
```

Open [http://localhost:3000](http://localhost:3000).

| Demo role | Username | Password |
|---|---|---|
| Nurse | `yta` | `YTaDemo@2026` |
| Technician | `kythuat` | `KyThuat@2026` |

## Run the gateway on Raspberry Pi

The ARM64 Pi needs the runtime bundle, e.g.:

```text
/home/iotchallenge/gateway/
├── bin/gateway
├── bin/mosquitto
├── config/
├── run.sh
├── run-stack.sh
├── runtime/node/
└── zigbee2mqtt/
```

The launcher auto-detects the coordinator and opens Mosquitto on port
`1885`, the Zigbee2MQTT web UI on port `8080`, and the gateway.

### Direct LAN connection

On Windows, as Administrator:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

On the Pi:

```bash
cd ~/gateway
bash run.sh <WINDOWS_IP>
```

### SSH reverse tunnel

On Windows, keep this terminal running:

```powershell
plink -ssh -N -R 15000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

In the Pi configuration, set `HIS_SERVER_HOST=127.0.0.1` and
`HIS_SERVER_PORT=15000`, then:

```bash
cd ~/gateway
bash run.sh 127.0.0.1
```

Do not run two gateways at once, or a service and `run.sh` at the same
time — the processes will fight over the Zigbee coordinator.

## Startup order for every session

1. Power up G26, the sensors, and the coordinator.
2. Start MySQL.
3. Run the HIS Server in `server/src/HisServer`.
4. Open the LAN connection or the reverse tunnel.
5. Run `bash run.sh` on the Pi.
6. Check Zigbee2MQTT at `http://<PI_IP>:8080`.
7. Open the HIS Web UI at `http://localhost:3000`.
8. Wait for taring, hang the bag, and set the target drip rate.
9. Wait for a full 20 drop samples and 64 vitals samples before testing alerts.

## Testing and troubleshooting

On Windows:

```powershell
Test-NetConnection 127.0.0.1 -Port 3000
Test-NetConnection 127.0.0.1 -Port 5000
Get-Process mysqld,HisServer,plink -ErrorAction SilentlyContinue
```

On the Pi:

```bash
ss -ltn | grep -E ':15000|:1885|:8080'
tail -n 50 ~/gateway/logs/zigbee2mqtt.log
tail -n 50 ~/gateway/logs/manual-stack.log
```

### Server reports `SocketException (10048)`

Port 3000 or 5000 is held by a stale process:

```powershell
netstat -ano | findstr ":3000 :5000"
Stop-Process -Id <PID>
```

### A bed shows `OFFLINE`

- Check that G26 has joined the Zigbee network.
- Check that Zigbee2MQTT has a fresh payload.
- Check the gateway, tunnel, and the device's EUI64.

### HR/SpO2 is unstable

- Keep the finger steady and avoid ambient light.
- Check 3.3 V, GND, SDA on PC07, SCL on PC05, and the pull-up resistors.
- Keep the buzzer wire away from the I2C wires.
- After placing the finger back, wait for the filter window to fill with samples.

### Commands from the web don't reach G26

Check the direction HIS Server → gateway → MQTT → Zigbee2MQTT → G26. A web
toast only confirms the server received the request; a response from G26
is what confirms the command actually arrived at the device.

## Pre-handoff build check

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\server\HisServer.sln
dotnet run --project .\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

Detailed technical documentation:

- `firmware/PIN_MAP.md`
- `gateway-pi/README.md`
- `server/README.md`
- `SYSTEM_INTEGRATION.md`
- `host_ai/README.md`

## License

This project's own source (`firmware/`, `software/`, `docs/`, `tools/`) is
licensed under the [Apache License 2.0](LICENSE). Vendored components —
the Simplicity SDK, AI/ML libraries under `simplicity_sdk_*/` and
`aiml_*/`, and third-party packages under `node_modules/` — keep their own
original licenses; see each vendor's `LICENSE` file.

## Conclusion

Through this project the team connected a bedside embedded device, a
Zigbee gateway on Raspberry Pi, and the HIS Server into a complete
bidirectional pipeline. The hard part wasn't just reading sensors — it was
making the data stable, unifying the alert logic, and making sure commands
from the web reliably reach G26.

The system is currently suited for demonstration, further dataset
collection, and continued algorithm experimentation. Turning it into a real
product would require sensor certification, electrical safety review,
security hardening, encrypted connections, and evaluation against medical
device regulations.

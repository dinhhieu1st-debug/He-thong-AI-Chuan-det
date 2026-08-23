# Smart IV integrated runtime

This branch uses the XG26 firmware as the single authority for alert severity.

## Alert contract

| G26 final level | HIS status | Meaning |
|---|---|---|
| 1 | Stable | Both vitals and drip monitoring are normal |
| 2 | Warning | At least one side requires attention, but the fused result is not level 3 |
| 3 | Critical | Both sides reached level 3 |

`line_branch`, `patient_branch`, `spo2_low`, `heart_rate_abnormal`,
`line_blocked`, and `ae_alarm` explain the cause. They do not calculate a new
severity on the server.

Training is also end-to-end: `drop_training_samples` is `0..20`,
`vitals_training_samples` is `0..64`, and `alerts_armed` becomes true only
when G26 says its alert decision is ready. HIS displays these values but never
uses them to reproduce the firmware algorithm.

The firmware stores green/yellow/red as `0/1/2` in `TsFlags`. The Zigbee2MQTT
converter publishes that compatibility value as `alert_level` and publishes
the application-level value as `final_alert_level` (`1/2/3`).

## Data path

```text
XG26 EP2 / 0xFC01
  -> Zigbee2MQTT external converter
  -> MQTT JSON
  -> gateway-pi TCP bridge
  -> HIS TCP port 5000
  -> MySQL + SignalR
  -> HIS web port 5194
```

## Run HIS on Windows

Apply the database update once (existing databases only):

```powershell
$env:MYSQL_PWD = '<password of his_app>'
& 'C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe' -u his_app his_server -e "source demo1/server/database/migrations/2026-08-23_g26_alert_contract.sql"
Remove-Item Env:\MYSQL_PWD
```

Then restart HIS so it loads the new binary and schema:

```powershell
cd demo1\server\src\HisServer
dotnet run --launch-profile http
```

Open `http://localhost:5194`.

## Flash the XG26 sensor board

Build and flash only the Smart IV sensor board. Do not flash the Raspberry Pi
or the Zigbee coordinator with this image.

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

The image is `cmake_gcc/build/base/smart-iv-monitor.hex`.

## Deploy the converter to Raspberry Pi

From the repository root on Windows:

```powershell
scp demo1\gateway\zigbee2mqtt_smart_iv_converter.js iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/zigbee2mqtt/data/external_converters/
```

## Run the Pi gateway

```bash
cd ~/pi-aarch64
bash run.sh 172.172.13.136
```

The healthy startup log contains both:

```text
Da ket noi MQTT
Da ket noi HIS server 172.172.13.136:5000
```

The server and Pi addresses can change with DHCP. Check the current Windows
IPv4 address with `ipconfig` and pass that address to `run.sh`.

After copying the converter, stop the old `run.sh` with Ctrl+C and run it
again. Re-pairing the G26 is normally unnecessary; open the device in
Zigbee2MQTT and verify these keys arrive: `final_alert_level`,
`drop_training_samples`, `vitals_training_samples`, and `alerts_armed`.

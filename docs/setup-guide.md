# Full setup guide for a new machine

This section is written in the exact order the team followed in practice.
Newcomers should follow it step by step, moving to the next step only after
the previous one succeeds.

## Step 1 – Prepare the Windows machine

Install Git, the .NET 8 SDK, MySQL Community Server 8.x, Silicon Labs
Tools/Simplicity Studio 6, and PuTTY.

```powershell
git --version
dotnet --list-sdks
plink -V
```

Clone the correct branch:

```powershell
cd C:\Users\<USER>\Documents
git clone --branch smart-iv-end-to-end-2026-08-24 --single-branch `
  https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

## Step 2 – Build and flash the G26 firmware

Plug in the BRD2709A over USB, then run from the repository root:

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

If multiple kits are plugged in:

```powershell
.\tools\flash_firmware.ps1 -SerialNo <DEBUG_ADAPTER_SERIAL>
```

## Step 3 – Create the MySQL database

Start MySQL from an administrator PowerShell:

```powershell
Get-Service MySQL*
Start-Service MySQL84
```

The service name may differ from `MySQL84`; use whatever `Get-Service` returns.

Load the schema:

```powershell
Get-Content -Raw .\software\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Then run **every** file under `software/server/database/migrations` in name
order. The PowerShell snippet below concatenates them so MySQL only asks for
the password once:

```powershell
$migrationSql = Get-ChildItem .\software\server\database\migrations\*.sql |
  Sort-Object Name | ForEach-Object { Get-Content -Raw $_.FullName }
$migrationSql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p his_server
```

| Demo role | Username | Password |
|---|---|---|
| Nurse | `yta` | `YTaDemo@2026` |
| Technician | `kythuat` | `KyThuat@2026` |

These accounts are for demo purposes only and must be changed before any
real deployment.

## Step 4 – Configure and run the HIS Server

Never commit MySQL credentials to Git. Use .NET user-secrets instead:

```powershell
cd .\software\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MYSQL_PASSWORD>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
```

Run the server and keep the terminal open:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det\software\server\src\HisServer
dotnet run --launch-profile http
```

Expected output:

```text
Restored ... bed(s) from the database.
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:5194
```

Open `http://localhost:5194` and try logging in before connecting the Pi.

## Step 5 – Prepare the Raspberry Pi

The Pi needs Linux ARM64, SSH, and USB access to the coordinator. The
repository holds the gateway and converter source, but the ARM64
Node/Mosquitto/Zigbee2MQTT binaries are large enough that they are not kept
in Git in full. You need the `pi-aarch64` runtime bundle from a release, or
copied from a reference Pi.

```text
/home/iotchallenge/pi-aarch64/
├── bin/gateway
├── bin/mosquitto
├── config/gateway.conf
├── config/mosquitto.conf
├── config/runtime.conf
├── run.sh
├── runtime/node/bin/node
└── zigbee2mqtt/
```

Update the gateway source, converter, and launcher from Windows:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
scp -r .\software\gateway-pi\src iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\software\gateway-pi\Makefile iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\software\gateway-pi\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\tools\run-stack.pi.sh iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/run.sh
```

> `zigbee2mqtt_smart_iv_converter.js` is not currently in the repo — see the
> note in [`docs/system-integration.md`](system-integration.md#deploy-the-converter-to-raspberry-pi).

On the Pi:

```bash
sudo usermod -aG dialout iotchallenge
```

Log out and back in after adding the group. The launcher auto-discovers
`/dev/serial/by-id/*`, `/dev/ttyACM*`, and `/dev/ttyUSB*`.

## Step 6 – Choose how the Pi connects to the server

### Option A: direct connection on the same LAN

On Windows, run an administrator PowerShell:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

On the Pi:

```bash
nc -vz <WINDOWS_IP> 5000
cd ~/pi-aarch64 && bash run.sh <WINDOWS_IP>
```

### Option B: SSH reverse tunnel — the team's stable setup

On Windows, open a dedicated PowerShell window and keep it running:

```powershell
plink -ssh -N -R 5000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

The first time `plink` runs it will ask you to verify the host key
fingerprint before confirming. Never write the SSH password into a script or
commit it to Git.

SSH into the Pi:

```powershell
ssh iotchallenge@<PI_IP>
```

In the Pi terminal:

```bash
cd ~/pi-aarch64 && bash run.sh 127.0.0.1
```

Do not run a direct connection and a reverse tunnel for the same gateway at
the same time — the server could receive duplicate messages.

## Step 7 – Make the Pi stack start on boot

Copy the sample service file:

```powershell
scp .\tools\smart-iv-stack.service iotchallenge@<PI_IP>:/tmp/
```

On the Pi:

```bash
sudo cp /tmp/smart-iv-stack.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now smart-iv-stack.service
sudo systemctl status smart-iv-stack.service
journalctl -u smart-iv-stack.service -f
```

The sample service uses `run.sh 127.0.0.1`, matching the reverse-tunnel
setup. Do not run the service and a manual `bash run.sh` at the same
time — the two processes will fight over the coordinator.

## Step 8 – Startup order for every session

Following the tested reverse-tunnel setup:

1. Power up G26, the sensors, and the Zigbee coordinator.
2. Start MySQL on Windows.
3. Run the HIS Server with `dotnet run --launch-profile http`.
4. Open `plink -R 5000:127.0.0.1:5000` from Windows to the Pi.
5. Run `bash run.sh 127.0.0.1` on the Pi, or check the `smart-iv-stack` service.
6. Open `http://<PI_IP>:8080` and confirm `SmartIV-Sensor` is online.
7. Open `http://localhost:5194`.
8. Wait for G26 to finish taring, hang the bag, and set the target drip rate on the web UI.
9. Wait for a full 20 drop samples and 64 vitals samples before judging the AI alerts.

## Step 9 – Verify connectivity

On Windows:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 3306,5000,5194
Get-NetTCPConnection -State Established -LocalPort 5000
Get-Process mysqld,HisServer,plink
```

On the Pi:

```bash
ss -tn | grep ':5000'
tail -n 50 ~/pi-aarch64/logs/gateway.log
tail -n 50 ~/pi-aarch64/logs/zigbee2mqtt.log
```

The system is wired up correctly when MySQL is listening on 3306, HIS is
listening on 5000/5194, TCP 5000 shows `ESTABLISHED`, Zigbee2MQTT is
receiving fresh payloads, and BED-01 is no longer `OFFLINE`.

## Pre-submission build check

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\software\server\HisServer.sln
dotnet run --project .\software\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

If you hit an error during setup, see
[`docs/troubleshooting.md`](troubleshooting.md).

# Zigbee HIS Gateway

Gateway nhan du lieu thiet bi tu Zigbee2MQTT qua MQTT, gui sinh hieu len HIS
server bang TCP, va chuyen lenh tu server nguoc ve Zigbee2MQTT. Du an khong
con web dashboard hay web OTA cuc bo; web frontend cua Zigbee2MQTT van co o
port 8080.

## Cau truc

```text
src/                     ma nguon C
config/                  cau hinh dung chung
scripts/windows/         launcher va build cho Windows
bin/windows/             gateway.exe
deploy/pi-aarch64/       goi offline day du cho Raspberry Pi ARM64
```

## Windows

```powershell
.\run_windows.cmd
```

Ghi de IP HIS server ngay khi chay:

```powershell
.\run_windows.cmd 192.168.1.20
```

## Raspberry Pi aarch64

Thu muc `deploy/pi-aarch64` da gom san:

- Gateway Linux ARM64.
- Mosquitto broker ARM64 lien ket tinh.
- Node.js 22 ARM64.
- Zigbee2MQTT 2.13.0 da build.
- Toan bo production `node_modules`, gom native serial binding ARM64.
- Cau hinh MQTT, Zigbee2MQTT va script khoi dong/dung toan bo stack.

Khong can chay lenh cai dat hay bien dich nao tren Pi.

Sua IP HIS server trong `deploy/pi-aarch64/config/gateway.conf`:

```ini
HIS_SERVER_HOST=127.0.0.1
```

Neu server nam tren may khac, thay `127.0.0.1` bang IP LAN cua may server.
Co the khong sua file va truyen IP khi chay.

Sua cong coordinator trong `deploy/pi-aarch64/config/runtime.conf`:

```ini
COORDINATOR_PORT=/dev/ttyACM0
```

File de chuyen sang Pi duoc tao tai `release/gateway-pi-aarch64.tar.gz`.
Sau khi chep file nay sang Pi, giai nen va chay:

```bash
tar -xzf gateway-pi-aarch64.tar.gz
cd pi-aarch64
bash run.sh
```

Hoac truyen IP HIS server truc tiep:

```bash
bash run.sh 192.168.1.20
```

IP truyen vao duoc dung cho ca ket noi HIS TCP va OTA index. Vi du lenh tren
se cau hinh Zigbee2MQTT doc OTA tai
`http://192.168.1.20:5194/api/ota/index.json`. Co the doi port hoac dat URL
day du trong `config/runtime.conf`.

Launcher se kiem tra kien truc, file runtime, Node va coordinator; sau do tu
khoi dong Mosquitto port 1885, Zigbee2MQTT va gateway. Nhan `Ctrl+C` de dung
toan bo. Web Zigbee2MQTT truy cap tai `http://<IP-cua-Pi>:8080`.

Neu Node khong chay tren he dieu hanh Pi, coordinator khong ton tai, khong co
quyen serial, hoac mot tien trinh dung bat thuong, launcher se bao loi va ghi
chi tiet trong `deploy/pi-aarch64/logs`.

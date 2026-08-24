# Hướng dẫn cài đặt toàn bộ trên một hệ thống mới

Phần này được nhóm viết theo đúng thứ tự đã chạy thực tế. Người mới nên làm lần lượt, bước trước thành công mới chuyển sang bước sau.

## Bước 1 – Chuẩn bị máy Windows

Cài Git, .NET 8 SDK, MySQL Community Server 8.x, Silicon Labs Tools/Simplicity Studio 6 và PuTTY.

```powershell
git --version
dotnet --list-sdks
plink -V
```

Clone đúng nhánh:

```powershell
cd C:\Users\<USER>\Documents
git clone --branch smart-iv-end-to-end-2026-08-24 --single-branch `
  https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

## Bước 2 – Build và nạp firmware G26

Cắm BRD2709A vào USB rồi chạy tại thư mục gốc repository:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

Firmware nằm tại `cmake_gcc/build/base/smart-iv-monitor.hex`.

Nếu bo chưa có bootloader hoặc đã erase toàn chip:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_bootloader_and_app.ps1
```

Nếu cắm nhiều kit:

```powershell
.\tools\flash_firmware.ps1 -SerialNo <DEBUG_ADAPTER_SERIAL>
```

## Bước 3 – Tạo MySQL database

Khởi động MySQL bằng PowerShell Administrator:

```powershell
Get-Service MySQL*
Start-Service MySQL84
```

Tên service có thể khác `MySQL84`; dùng tên mà `Get-Service` trả về.

Nạp schema:

```powershell
Get-Content -Raw .\software\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Sau đó chạy **toàn bộ** file trong `software/server/database/migrations` theo thứ tự tên. Đoạn PowerShell dưới đây gộp chúng lại để MySQL chỉ hỏi mật khẩu một lần:

```powershell
$migrationSql = Get-ChildItem .\software\server\database\migrations\*.sql |
  Sort-Object Name | ForEach-Object { Get-Content -Raw $_.FullName }
$migrationSql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p his_server
```

| Vai trò demo | Username | Password |
|---|---|---|
| Y tá | `yta` | `YTaDemo@2026` |
| Kỹ thuật | `kythuat` | `KyThuat@2026` |

Chỉ dùng các tài khoản này để demo và phải đổi khi triển khai thật.

## Bước 4 – Cấu hình và chạy HIS Server

Không ghi mật khẩu MySQL vào Git. Dùng .NET user-secrets:

```powershell
cd .\software\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU_MYSQL>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
```

Chạy server và giữ terminal mở:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det\software\server\src\HisServer
dotnet run --launch-profile http
```

Kết quả đúng:

```text
Restored ... bed(s) from the database.
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:5194
```

Mở `http://localhost:5194` và thử đăng nhập trước khi nối Pi.

## Bước 5 – Chuẩn bị Raspberry Pi

Pi cần Linux ARM64, SSH và quyền truy cập USB coordinator. Repository chứa source gateway và converter, nhưng các binary Node/Mosquitto/Zigbee2MQTT ARM64 khá lớn nên không lưu toàn bộ trong Git. Cần có gói runtime `pi-aarch64` từ bản phát hành hoặc chép từ Pi mẫu.

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

Cập nhật source gateway, converter và launcher từ Windows:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
scp -r .\software\gateway-pi\src iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\software\gateway-pi\Makefile iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\software\gateway-pi\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\tools\run-stack.pi.sh iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/run.sh
```

> File `zigbee2mqtt_smart_iv_converter.js` hiện chưa có trong repo — xem ghi chú
> ở [`docs/system-integration.md`](system-integration.md#deploy-the-converter-to-raspberry-pi).

Trên Pi:

```bash
sudo usermod -aG dialout iotchallenge
```

Đăng xuất/đăng nhập lại sau khi thêm group. Launcher tự dò `/dev/serial/by-id/*`, `/dev/ttyACM*`, `/dev/ttyUSB*`.

## Bước 6 – Chọn một cách kết nối Pi với Server

### Cách A: kết nối trực tiếp cùng mạng LAN

Trên Windows, chạy PowerShell Administrator:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

Trên Pi:

```bash
nc -vz <WINDOWS_IP> 5000
cd ~/pi-aarch64 && bash run.sh <WINDOWS_IP>
```

### Cách B: SSH reverse tunnel – cách nhóm đang dùng ổn định

Trên Windows, mở một PowerShell riêng và giữ chạy:

```powershell
plink -ssh -N -R 5000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

Lần đầu `plink` hỏi host key thì phải đối chiếu fingerprint rồi mới xác nhận. Không ghi mật khẩu SSH vào script hoặc Git.

SSH vào Pi:

```powershell
ssh iotchallenge@<PI_IP>
```

Trong terminal Pi:

```bash
cd ~/pi-aarch64 && bash run.sh 127.0.0.1
```

Không chạy đồng thời cả kết nối trực tiếp và reverse tunnel cho cùng một gateway vì server có thể nhận bản tin trùng.

## Bước 7 – Cho stack Pi tự chạy khi khởi động

Chép service mẫu:

```powershell
scp .\tools\smart-iv-stack.service iotchallenge@<PI_IP>:/tmp/
```

Trên Pi:

```bash
sudo cp /tmp/smart-iv-stack.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now smart-iv-stack.service
sudo systemctl status smart-iv-stack.service
journalctl -u smart-iv-stack.service -f
```

Service mẫu dùng `run.sh 127.0.0.1`, phù hợp với reverse tunnel. Không vừa chạy service vừa chạy thủ công `bash run.sh` vì hai tiến trình sẽ tranh coordinator.

## Bước 8 – Thứ tự khởi động mỗi lần sử dụng

Theo cách reverse tunnel đã kiểm thử:

1. Cấp nguồn G26, cảm biến và Zigbee coordinator.
2. Khởi động MySQL trên Windows.
3. Chạy HIS Server bằng `dotnet run --launch-profile http`.
4. Mở `plink -R 5000:127.0.0.1:5000` từ Windows tới Pi.
5. Chạy `bash run.sh 127.0.0.1` trên Pi hoặc kiểm tra service `smart-iv-stack`.
6. Mở `http://<PI_IP>:8080` và kiểm tra `SmartIV-Sensor` online.
7. Mở `http://localhost:5194`.
8. Chờ G26 trừ bì xong, treo bịch và đặt tốc độ giọt trên web.
9. Chờ đủ 20 mẫu giọt và 64 mẫu sinh hiệu trước khi đánh giá cảnh báo AI.

## Bước 9 – Kiểm tra kết nối

Trên Windows:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 3306,5000,5194
Get-NetTCPConnection -State Established -LocalPort 5000
Get-Process mysqld,HisServer,plink
```

Trên Pi:

```bash
ss -tn | grep ':5000'
tail -n 50 ~/pi-aarch64/logs/gateway.log
tail -n 50 ~/pi-aarch64/logs/zigbee2mqtt.log
```

Hệ thống nối đúng khi MySQL nghe 3306, HIS nghe 5000/5194, TCP 5000 là `ESTABLISHED`, Zigbee2MQTT nhận payload mới và BED-01 không còn `OFFLINE`.

## Build kiểm tra trước khi nộp

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\software\server\HisServer.sln
dotnet run --project .\software\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

Nếu gặp lỗi trong lúc cài đặt, xem [`docs/troubleshooting.md`](troubleshooting.md).

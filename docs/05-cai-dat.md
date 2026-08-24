# 5. Cài đặt từ đầu trên một hệ thống mới

Viết theo đúng thứ tự nhóm đã chạy thực tế. **Làm lần lượt, bước trước chạy
được mới sang bước sau.** Nhảy cóc thì lỗi ở bước 7 sẽ trông y hệt lỗi ở bước 3.

Ký hiệu dùng trong tài liệu:

| Ký hiệu | Thay bằng |
|---|---|
| `<USER>` | Tên user Windows |
| `<PI_IP>` | Địa chỉ IP của Raspberry Pi |
| `<WINDOWS_IP>` | Địa chỉ IPv4 của máy chạy HIS Server |
| `<REPO>` | `C:\Users\<USER>\Documents\He-thong-AI-Chuan-det` |

---

## Bước 1 — Chuẩn bị máy Windows

Cài: **Git**, **.NET 8 SDK**, **MySQL Community Server 8.x**, **Simplicity
Studio 6 / Silicon Labs Tools**, **PuTTY**.

Kiểm tra:

```powershell
git --version
dotnet --list-sdks
plink -V
```

Clone đúng nhánh:

```powershell
cd C:\Users\<USER>\Documents
git clone --branch server-chuan-2026-08-24 --single-branch `
  https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

---

## Bước 2 — Build và nạp firmware G26

Cắm BRD2709A vào USB, chạy tại **thư mục gốc repository**:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

Ảnh firmware nằm ở `cmake_gcc/build/base/smart-iv-monitor.hex`.

Nếu board chưa có bootloader hoặc vừa bị erase toàn chip:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_bootloader_and_app.ps1
```

Nếu đang cắm nhiều kit cùng lúc, chỉ đích danh board cần nạp:

```powershell
.\tools\flash_firmware.ps1 -SerialNo <DEBUG_ADAPTER_SERIAL>
```

> Chỉ nạp ảnh này cho **board cảm biến Smart IV**. Không nạp nó cho Zigbee
> coordinator — coordinator dùng firmware NCP nguyên bản của Silicon Labs.

---

## Bước 3 — Tạo MySQL database

Mở PowerShell **Administrator**:

```powershell
Get-Service MySQL*
Start-Service MySQL84
```

Tên service có thể không phải `MySQL84`; dùng đúng tên mà `Get-Service` trả về.

Nạp schema:

```powershell
Get-Content -Raw .\He-thong-AI-Chuan-det-server\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Sau đó chạy **toàn bộ** file trong `He-thong-AI-Chuan-det-server\server\database\migrations`
**theo thứ tự tên**. Đoạn dưới gộp chúng lại để MySQL chỉ hỏi mật khẩu một lần:

```powershell
$migrationSql = Get-ChildItem .\He-thong-AI-Chuan-det-server\server\database\migrations\*.sql |
  Sort-Object Name | ForEach-Object { Get-Content -Raw $_.FullName }
$migrationSql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p his_server
```

Migration cuối cùng tạo sẵn hai tài khoản demo:

| Vai trò | Username | Password |
|---|---|---|
| Y tá | `yta` | `YTaDemo@2026` |
| Kỹ thuật | `kythuat` | `KyThuat@2026` |

> Hai tài khoản này **chỉ để demo**. Phải đổi mật khẩu trước khi triển khai thật.

---

## Bước 4 — Cấu hình và chạy HIS Server

**Không ghi mật khẩu MySQL vào Git.** Dùng .NET user-secrets:

```powershell
cd .\He-thong-AI-Chuan-det-server\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU_MYSQL>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
```

Chạy server và **giữ terminal mở**:

```powershell
dotnet run --launch-profile http
```

Log đúng phải có đủ ba dòng:

```text
Restored ... bed(s) from the database.
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:5194
```

Mở `http://localhost:5194` và **đăng nhập thử trước khi nối Pi**. Nếu bước này
chưa chạy được thì đừng đụng tới Pi — sẽ không phân biệt được lỗi ở đâu.

Các biến cấu hình khác: xem
[`He-thong-AI-Chuan-det-server/server/README.md`](../He-thong-AI-Chuan-det-server/server/README.md).

---

## Bước 5 — Chuẩn bị Raspberry Pi

Pi cần Linux ARM64, bật SSH và có quyền truy cập USB coordinator.

Repository chứa **mã nguồn** gateway và converter, nhưng các binary ARM64
(Node.js, Mosquitto, Zigbee2MQTT) khá lớn nên không lưu trong Git. Cần lấy gói
runtime `pi-aarch64` từ bản phát hành hoặc chép từ một Pi đã dựng sẵn.

Cấu trúc gói runtime trên Pi:

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

Đẩy mã nguồn, converter và launcher từ Windows sang:

```powershell
cd <REPO>
scp -r .\gateway-pi\src iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\gateway-pi\Makefile iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\gateway-pi\zigbee2mqtt\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/zigbee2mqtt/data/external_converters/
scp .\tools\run-stack.pi.sh iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/run.sh
```

Trên Pi, cấp quyền cổng serial:

```bash
sudo usermod -aG dialout iotchallenge
```

**Phải đăng xuất rồi đăng nhập lại** thì group mới có hiệu lực. Launcher tự dò
`/dev/serial/by-id/*`, `/dev/ttyACM*`, `/dev/ttyUSB*` nên thường không phải khai
cổng bằng tay.

> Sau khi thay converter, dừng `run.sh` cũ bằng `Ctrl+C` rồi chạy lại. Thường
> **không cần pair lại** G26.

---

## Bước 6 — Chọn một cách nối Pi với Server

Chỉ chọn **một trong hai**. Chạy đồng thời cả hai thì server nhận bản tin trùng.

### Cách A — Cùng mạng LAN

Trên Windows, PowerShell **Administrator**:

```powershell
cd <REPO>
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

Trên Pi:

```bash
nc -vz <WINDOWS_IP> 5000
cd ~/pi-aarch64 && bash run.sh <WINDOWS_IP>
```

### Cách B — SSH reverse tunnel (cách nhóm đang dùng, ổn định hơn)

Mở một cửa sổ PowerShell riêng và **giữ chạy suốt phiên**:

```powershell
plink -ssh -N -R 5000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

Lần đầu `plink` sẽ hỏi host key — **đối chiếu fingerprint rồi mới xác nhận**.
Không nhúng mật khẩu SSH vào script hay commit vào Git.

SSH vào Pi ở một cửa sổ khác:

```powershell
ssh iotchallenge@<PI_IP>
```

Trên Pi:

```bash
cd ~/pi-aarch64 && bash run.sh 127.0.0.1
```

---

## Bước 7 — Cho stack Pi tự chạy khi khởi động

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

Service mẫu gọi `run.sh 127.0.0.1`, tức là **hợp với cách B (reverse tunnel)**.
Dùng cách A thì phải sửa `ExecStart` cho đúng IP.

> Không vừa bật service vừa chạy tay `bash run.sh`. Hai tiến trình sẽ tranh
> nhau cổng coordinator và cả hai cùng hỏng.

---

## Bước 8 — Kiểm tra trước khi nộp

```powershell
cd <REPO>
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\He-thong-AI-Chuan-det-server\server\HisServer.sln
dotnet run --project .\He-thong-AI-Chuan-det-server\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

---

Cài xong rồi thì đọc tiếp [`06-van-hanh.md`](06-van-hanh.md) để biết thứ tự
khởi động mỗi lần dùng.

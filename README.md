# Hệ thống AI Chuẩn Đét

Smart IV Monitor là hệ thống thử nghiệm giám sát truyền dịch theo chuỗi:

```text
Cảm biến -> EFR32xG26 -> Zigbee ZCL -> Zigbee2MQTT trên Raspberry Pi
         -> Gateway TCP -> HIS Server -> giao diện web thời gian thực
```

Hệ thống theo dõi HR, SpO2, tốc độ/khoảng cách giọt, khối lượng bịch dịch,
AI chuỗi thời gian và cảnh báo ba mức. Đây là prototype nghiên cứu, chưa phải
thiết bị y tế đã kiểm định và không thay thế giám sát lâm sàng.

## 1. Thành phần và giao thức

| Thành phần | Vai trò |
|---|---|
| EFR32xG26 / BRD2709A | Đọc cảm biến, AI, OLED, LED/buzzer và Zigbee |
| MAX30102 | HR và SpO2 |
| Photodiode | Phát hiện giọt dịch |
| HX711 + loadcell | Khối lượng bịch dịch |
| Raspberry Pi ARM64 | Mosquitto, Zigbee2MQTT, coordinator và gateway |
| HIS Server .NET 8 | Nhận TCP `5000`, lưu dữ liệu, phục vụ web `5194` |

G26 không gửi JSON trực tiếp. Firmware report ZCL Attribute tại Endpoint 2,
custom cluster `0xFC01`, manufacturer code `0x1049`. Converter trên Pi chuyển
attribute thành MQTT JSON; gateway chuyển dữ liệu tới HIS Server.

## 2. Sơ đồ chân G26

| Thiết bị | Tín hiệu | Chân |
|---|---|---|
| Cảm biến giọt | OUT | PD02 |
| HX711 | DOUT / SCK | PC01 / PC03 |
| MAX30102 + OLED | SCL / SDA | PC05 / PC07 |
| LED xanh / vàng / đỏ | OUT | PA07 / PA04 / PA05 |
| Buzzer active-low | OUT | PC06 |
| Nút trừ bì | INPUT | PB00 |

Các module dùng logic 3.3 V và chung GND. Nên kéo lên SDA/SCL khoảng `4.7 kΩ`,
dùng dây I2C ngắn và đặt xa dây buzzer.

## 3. Chuẩn bị và tải source

Máy Windows cần Git, .NET 8 SDK, MySQL 8 và Silicon Labs
Tools/Simplicity Studio (CMake + Commander). Pi cần có runtime
`~/pi-aarch64`, coordinator tại `/dev/ttyACM0` và quyền serial.

```powershell
cd C:\Users\<USER>\Documents
git clone --branch smart-iv-end-to-end-2026-08-24 --single-branch `
  https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

## 4. Build và nạp firmware G26

Kết nối BRD2709A bằng USB, mở PowerShell tại thư mục repository:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

Firmware được tạo tại `cmake_gcc/build/base/smart-iv-monitor.hex`.

Nếu kit chưa có Gecko Bootloader hoặc vừa erase toàn chip:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_bootloader_and_app.ps1
```

Nếu cắm nhiều kit:

```powershell
.\tools\flash_firmware.ps1 -SerialNo <DEBUG_ADAPTER_SERIAL>
```

Reset G26 sau khi nạp. Không treo bịch trong lúc OLED báo đang trừ bì.

## 5. Cấu hình và chạy HIS Server

Không ghi mật khẩu database vào Git. Cấu hình lần đầu bằng user-secrets:

```powershell
cd .\demo1\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=localhost;Port=3306;Database=his_server;Uid=<DB_USER>;Pwd=<DB_PASSWORD>;SslMode=None;AllowPublicKeyRetrieval=True;"
```

Nếu database chưa có schema, chạy `demo1/server/database/schema.sql`, sau đó
các file trong `demo1/server/database/migrations` theo thứ tự tên.

Mỗi lần khởi động, mở một PowerShell riêng và giữ nó chạy:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det\demo1\server\src\HisServer
dotnet run --launch-profile http
```

Kết quả đúng:

```text
Bed vitals TCP ingestion listening on port 5000
Now listening on: http://0.0.0.0:5194
```

Mở `http://localhost:5194`. Tài khoản demo nằm trong migration
`2026-08-21_seed_demo_accounts.sql`; phải đổi mật khẩu khi triển khai thật.

Nếu gặp `SocketException (10048)`, kiểm tra tiến trình đang giữ cổng:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194 |
  Select-Object LocalAddress,LocalPort,OwningProcess
```

### Mở firewall cho Pi (chạy một lần)

Mở PowerShell bằng **Run as administrator**:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

Ghi lại IPv4 của card mạng cùng mạng với Pi; đó là `<WINDOWS_IP>`.

## 6. Khởi động Raspberry Pi qua SSH reverse tunnel

Trong cấu hình đã kiểm thử, Windows Firewall không cho Pi kết nối trực tiếp
tới cổng TCP 5000. Vì vậy cần tạo reverse tunnel trước, rồi để gateway trên Pi
kết nối tới `127.0.0.1:5000`.

### 6.1 Mở reverse tunnel trên Windows

Mở một PowerShell Windows riêng và giữ cửa sổ này chạy:

```powershell
plink -ssh -N `
  -hostkey "SHA256:LMfiR+UyuVTLJe99kB8dipYuZdAoKXK4RulRsRiEUkw" `
  -R 5000:127.0.0.1:5000 `
  iotchallenge@<PI_IP>
```

Nhập mật khẩu SSH khi được hỏi. Không thêm mật khẩu vào script hoặc Git.
Nếu chưa có `plink`, cài PuTTY hoặc dùng SSH client có hỗ trợ remote port
forwarding tương đương.

### 6.2 Chạy stack trên Pi

Từ một PowerShell Windows khác:

```powershell
ssh iotchallenge@<PI_IP>
```

Trong terminal Pi:

```bash
cd ~/pi-aarch64 && bash run.sh 127.0.0.1
```

Launcher tự dò coordinator theo thứ tự `/dev/serial/by-id/*`, `/dev/ttyACM*`,
`/dev/ttyUSB*` và tự dừng phiên Smart IV cũ đang khóa cổng serial.

Giữ cả cửa sổ tunnel và terminal Pi chạy. Kết quả đúng phải lần lượt khởi
động Mosquitto, Zigbee2MQTT, gateway và hiện `Đã kết nối MQTT`.

- Zigbee2MQTT: `http://<PI_IP>:8080`.
- Nhấn `Ctrl+C` tại terminal Pi để dừng stack đúng cách.
- Nếu tunnel bị đóng, gateway mất đường tới HIS Server và BED-01 sẽ chuyển
  `OFFLINE` sau thời gian timeout dù G26/Zigbee vẫn đang hoạt động.

Chỉ dùng `bash run.sh <WINDOWS_IP>` khi firewall đã thật sự cho phép Pi kết
nối trực tiếp tới `<WINDOWS_IP>:5000` và kiểm tra `nc -vz <WINDOWS_IP> 5000`
thành công.

## 7. Thứ tự khởi động toàn hệ thống

1. Cấp nguồn cảm biến, G26 và coordinator.
2. Chạy HIS Server trên Windows.
3. Mở reverse tunnel `plink -R 5000:127.0.0.1:5000` trên Windows.
4. Chạy `bash run.sh 127.0.0.1` trên Pi.
5. Kiểm tra Zigbee2MQTT thấy `SmartIV-Sensor` online.
6. Mở `http://localhost:5194`, đăng nhập và vào `BED-01`.
7. Chờ trừ bì xong, treo bịch rồi đặt tốc độ giọt trên server.
8. Nhấn `Start monitoring` sau khi lắp cảm biến đúng vị trí.
9. Chờ đủ 20 mẫu giọt và 64 mẫu sinh hiệu trước khi đánh giá AI.

## 8. Kiểm tra kết nối

Trên Windows:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194
Get-NetTCPConnection -State Established -LocalPort 5000
```

Trên Pi:

```bash
ss -tn | grep ':5000'
tail -n 50 ~/pi-aarch64/logs/gateway.log
tail -n 50 ~/pi-aarch64/logs/zigbee2mqtt.log
```

Hệ thống đúng khi cổng 5194 mở được, cổng 5000 có kết nối `ESTABLISHED`,
Zigbee2MQTT nhận payload mới và `BED-01` cập nhật theo thời gian thực.

## 9. Logic cảnh báo

### Nhỏ giọt

- `±200 ms`: mức 1.
- Trên `200 ms` đến `800 ms`: mức 2.
- Trên `800 ms` hoặc mất giọt: mức 3.
- Mốc động chỉ bám thay đổi nhỏ hợp lệ; xung nhiễu, sai lệch lớn và mất giọt
  không được học theo.

### Sinh hiệu

- Học 60 mẫu tạo baseline, nạp đủ lịch sử 64 mẫu cho AI.
- Lệch dưới 15%: mức 1.
- Từ 15% đến dưới 20%: mức 2.
- Từ 20% trở lên: mức 3.
- HR dưới 45, HR trên 150 hoặc SpO2 dưới 90: mức 3.

### Ghép mức cuối

| Sinh hiệu | Nhỏ giọt | Đầu ra |
|---:|---:|---:|
| 1 | 1 | 1 |
| 3 | 3 | 3 |
| mọi tổ hợp còn lại | | 2 |

- Mức 1: LED xanh, buzzer tắt.
- Mức 2: LED vàng; buzzer kêu 0.5 giây, nghỉ 3 giây.
- Mức 3: LED đỏ nhấp nháy; buzzer đảo mỗi 0.25 giây.
- Buzzer active-low: PC06 LOW kêu, HIGH tắt.

## 10. Kiểm thử sinh hiệu trên web

- `Real data`: dùng cảm biến thật và phục hồi lịch sử AI.
- `Fake HR L2`: tạo HR lệch khoảng 17% để kiểm tra mức 2.
- `Fake HR+O2 L3`: tạo HR/SpO2 lệch khoảng 25% để kiểm tra mức 3.

`Real HR/SpO2` vẫn là dữ liệu cảm biến; `AI test HR/SpO2` là dữ liệu nhánh test
trên G26. Tắt fake không bắt thiết bị học lại.

## 11. Xử lý lỗi nhanh

- Web không có BED-01 hoặc báo `OFFLINE`: kiểm tra HIS Server, tiến trình
  `plink`, kết nối TCP 5000 và gateway trên Pi.
- HR/SpO2 là `--`: kiểm tra tay, nguồn 3.3 V, GND, PC05/PC07 và pull-up I2C.
- Bỏ tay mà hiện `--` là đúng; firmware không được giữ số cũ để gửi đi.
- Nút điều khiển timeout: gateway TCP chưa kết nối; kiểm tra cổng 5000.
- UI giữ nguyên BED-01 sau refresh và không khóa vĩnh viễn nút điều khiển.

## 12. Build kiểm tra trước phát hành

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\demo1\server\HisServer.sln
dotnet run --project .\demo1\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

Tài liệu sâu hơn nằm trong `demo1/docs`, `gateway-pi/README.md` và
`demo1/server/README.md`.

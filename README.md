# Smart IV Monitor – Hệ thống AI giám sát truyền dịch

Đây là đồ án do nhóm sinh viên xây dựng để theo dõi bệnh nhân đang truyền dịch. Thiết bị tại giường đo nhịp tim, SpO2, tốc độ nhỏ giọt và khối lượng bịch dịch; dữ liệu được gửi lên web để y tá theo dõi theo thời gian thực. Khi phát hiện bất thường, G26 cảnh báo tại chỗ bằng LED và buzzer, đồng thời gửi mức cảnh báo cùng nguyên nhân lên server.

> **Lưu ý:** Đây là mô hình nghiên cứu và trình diễn, chưa phải thiết bị y tế đã được kiểm định. Không dùng hệ thống để thay thế thiết bị theo dõi lâm sàng.

## 1. Hệ thống làm được gì?

- Đọc HR và SpO2 từ MAX30102.
- Phát hiện từng giọt, tính khoảng cách giọt và số giọt/phút.
- Đo khối lượng bịch dịch bằng loadcell và HX711.
- Hiển thị thông số trên OLED 0.96 inch, 128×64 pixel.
- Học baseline sinh hiệu và cửa sổ nhỏ giọt trước khi bật cảnh báo.
- Chạy AI chuỗi thời gian trực tiếp trên G26.
- Ghép cảnh báo sinh hiệu và nhỏ giọt thành ba mức.
- Truyền dữ liệu hai chiều bằng Zigbee ZCL Attribute; G26 không tạo JSON.
- Hiển thị giường, biểu đồ, trạng thái và nguyên nhân cảnh báo trên HIS Web.
- Cho phép đặt tốc độ giọt, trừ bì, hiệu chỉnh HR và bật chế độ test từ web.

## 2. Kiến trúc tổng thể

```text
MAX30102 + cảm biến giọt + HX711/loadcell
                    |
                    v
        EFR32xG26 / BRD2709A
  cảm biến + AI + OLED + LED + buzzer
                    |
                    | Zigbee ZCL Attribute
                    v
       Zigbee coordinator trên Raspberry Pi
                    |
                    v
       Zigbee2MQTT -> MQTT -> Gateway TCP
                    |
                    | TCP 5000
                    v
          HIS Server .NET 8 + MySQL
                    |
                    v
             Web + SignalR realtime
```

Chiều điều khiển đi ngược lại:

```text
HIS Web -> HIS Server -> TCP Gateway -> MQTT
        -> Zigbee2MQTT -> ZCL Attribute -> G26
```

G26 là nơi đọc cảm biến, chạy AI và quyết định mức cảnh báo cuối. Server nhận kết quả để lưu và hiển thị, không tự viết lại thuật toán cảnh báo sinh hiệu của thiết bị.

## 3. Cấu trúc repository

| Đường dẫn | Nội dung |
|---|---|
| `firmware/` | Cảm biến, OLED, AI và điều khiển cảnh báo trên G26 |
| `firmware/models/` | Model TFLite Micro nhúng trong firmware |
| `config/zcl/` | Custom Zigbee cluster và attribute |
| `gateway-pi/` | Gateway MQTT ↔ HIS TCP và cấu hình Pi |
| `server/` | HIS Server .NET 8, database, frontend và test |
| `host_ai/` | Dataset, mã huấn luyện và công cụ AI |
| `tools/` | Script build/nạp firmware và chạy hệ thống |
| `bootloader/` | Project bootloader G26 |
| `cmake_gcc/` | Cấu hình build firmware |

Server chính thức nằm trong `server/`; dự án không còn dùng thư mục `demo1/`.

## 4. Phần cứng và sơ đồ chân

| Khối | Tín hiệu | Chân G26 |
|---|---|---|
| Cảm biến giọt | D0/OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 và OLED | I2C SCL | PC05 |
| MAX30102 và OLED | I2C SDA | PC07 |
| LED xanh | Output | PA07 |
| LED vàng | Output | PA04 |
| LED đỏ | Output | PA05 |
| Buzzer active-low | Output | PC06 |
| Nút trừ bì | BTN0 | PB00 |

Các module dùng logic 3.3 V và nối chung GND. OLED và MAX30102 dùng chung I2C nên cần dây ngắn, điện trở kéo lên phù hợp và nên tách dây I2C khỏi buzzer. PC06 LOW là buzzer kêu, HIGH là tắt.

## 5. Giao thức Zigbee

G26 report dữ liệu qua ZCL Attribute:

- Endpoint: `2`
- Cluster: `0xFC01`
- Manufacturer Code: `0x1049`
- Cluster Name: `Smart IV Vitals`
- Model: `SmartIV-Sensor`

| Attribute | Nội dung |
|---:|---|
| `0x0000–0x0006` | HR, SpO2, flow, drop ratio, alarm, cân và giọt/phút |
| `0x0007–0x000A` | Mốc đặt, lệnh trừ bì và hiệu chỉnh HR |
| `0x000B–0x000E` | Baseline và bộ đếm sự kiện |
| `0x000F–0x0015` | Cờ AI, forecast và trend |
| `0x0016–0x0017` | Dịch và thời gian còn lại |
| `0x0018–0x001B` | Monitoring, tiến độ học và trạng thái bật cảnh báo |
| `0x001C–0x001E` | Khoảng giọt, bộ đếm giọt và mức giọt từ server |
| `0x001F–0x0022` | Chế độ test, dữ liệu vào AI và mức sinh hiệu |

Zigbee2MQTT đổi các attribute thành MQTT JSON. Gateway Pi gửi JSON tới HIS Server và chuyển lệnh từ server ngược về đúng attribute trên G26.

## 6. Trình tự hoạt động

1. Cấp nguồn G26 và cảm biến.
2. G26 trừ bì loadcell; chưa treo bịch trong bước này.
3. Thiết bị report trạng thái cảm biến để kiểm tra trên web.
4. Treo bịch và đặt tốc độ giọt mục tiêu trên HIS Web.
5. Khi nhận mốc, G26 thu thập `20/20` khoảng giọt và `64/64` mẫu sinh hiệu.
6. Cảnh báo AI được giữ tắt trong lúc lấy mẫu để tránh báo giả khi khởi động.
7. Khi đủ dữ liệu, `alerts_armed` được bật và hệ thống bắt đầu giám sát.
8. G26 gửi thông số, mức và nguyên nhân cảnh báo lên server liên tục.

## 7. Logic cảnh báo

### Sinh hiệu

- 60 mẫu hợp lệ đầu tạo baseline HR/SpO2.
- 64 mẫu tạo đủ history cho AI chuỗi thời gian.
- Lệch dưới 15%: mức 1.
- Lệch từ 15% đến dưới 20%: mức 2.
- Lệch từ 20% trở lên: mức 3.
- HR dưới 45, HR trên 150 hoặc SpO2 dưới 90: mức 3.
- Khi bỏ tay khỏi MAX30102, dữ liệu được đánh dấu không có tín hiệu thay vì giữ vô hạn giá trị cũ.

### Nhỏ giọt

- Sai lệch không quá `±200 ms`: mức 1.
- Sai lệch trên `200 ms` đến `800 ms`: mức 2.
- Sai lệch trên `800 ms`: mức 3.
- Watchdog phát hiện quá lâu không có giọt để cảnh báo tắc/mất giọt.
- AI nhỏ giọt dùng cửa sổ 20 khoảng giọt để phân tích xu hướng.

### Ghép cảnh báo cuối

| Sinh hiệu | Nhỏ giọt | Đầu ra |
|---:|---:|---:|
| 1 | 1 | 1 – bình thường |
| 3 | 3 | 3 – nguy hiểm |
| Các tổ hợp khác | | 2 – chú ý |

### LED và buzzer

- Mức 1: LED xanh, buzzer tắt.
- Mức 2: LED vàng; buzzer kêu 0.5 giây, nghỉ 3 giây.
- Mức 3: LED đỏ nhấp nháy; buzzer bật/tắt mỗi 0.25 giây.

## 8. Chế độ test trên web

- `Real data`: dùng cảm biến thật.
- `Test HR L2`: tạo HR lệch khoảng 17% để kiểm tra mức 2.
- `Test HR+O2 L3`: tạo HR và SpO2 lệch khoảng 25% để kiểm tra mức 3.

Dữ liệu cảm biến thật và dữ liệu vào AI được hiển thị riêng. Khi tắt test, firmware phục hồi history thật đã lưu nên không phải học lại từ đầu.

## 9. Cài đặt trên Windows

Cần cài Git, .NET 8 SDK, MySQL 8.x, Simplicity Studio 6/Silicon Labs Commander và PuTTY/Plink.

```powershell
git --version
dotnet --list-sdks
plink -V
```

Clone nhánh chính:

```powershell
cd C:\Users\<USER>\Documents
git clone --branch main --single-branch https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd .\He-thong-AI-Chuan-det
```

## 10. Build và nạp G26

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

Firmware nằm tại `cmake_gcc/build/base/smart-iv-monitor.hex`.

Nếu bo chưa có bootloader hoặc vừa erase toàn chip:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_bootloader.ps1
powershell -ExecutionPolicy Bypass -File .\tools\flash_bootloader_and_app.ps1
```

## 11. Cài MySQL và chạy HIS Server

Khởi động MySQL bằng PowerShell Administrator:

```powershell
Get-Service MySQL*
Start-Service MySQL84
```

Tên service có thể khác `MySQL84`. Tạo database lần đầu:

```powershell
Get-Content -Raw .\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Chạy tiếp các file trong `server/database/migrations/` theo thứ tự tên. Không ghi mật khẩu MySQL vào Git; cấu hình bằng user-secrets:

```powershell
cd .\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU_MYSQL>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
dotnet run --launch-profile http
```

Server đúng sẽ báo:

```text
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:3000
```

Mở [http://localhost:3000](http://localhost:3000).

| Vai trò demo | Username | Password |
|---|---|---|
| Y tá | `yta` | `YTaDemo@2026` |
| Kỹ thuật | `kythuat` | `KyThuat@2026` |

## 12. Chạy gateway trên Raspberry Pi

Pi ARM64 cần có gói runtime, ví dụ:

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

Launcher tự tìm coordinator, mở Mosquitto cổng `1885`, Zigbee2MQTT web cổng `8080` và gateway.

### Kết nối trực tiếp trong LAN

Trên Windows Administrator:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\configure_pi_firewall.ps1
ipconfig
```

Trên Pi:

```bash
cd ~/gateway
bash run.sh <WINDOWS_IP>
```

### Kết nối bằng SSH reverse tunnel

Trên Windows, giữ terminal này chạy:

```powershell
plink -ssh -N -R 15000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

Trong cấu hình Pi đặt `HIS_SERVER_HOST=127.0.0.1` và `HIS_SERVER_PORT=15000`, sau đó:

```bash
cd ~/gateway
bash run.sh 127.0.0.1
```

Không chạy đồng thời hai gateway hoặc vừa chạy service vừa chạy `run.sh`, vì các tiến trình sẽ tranh Zigbee coordinator.

## 13. Thứ tự khởi động mỗi lần sử dụng

1. Cấp nguồn G26, cảm biến và coordinator.
2. Khởi động MySQL.
3. Chạy HIS Server trong `server/src/HisServer`.
4. Mở kết nối LAN hoặc reverse tunnel.
5. Chạy `bash run.sh` trên Pi.
6. Kiểm tra Zigbee2MQTT tại `http://<PI_IP>:8080`.
7. Mở HIS Web tại `http://localhost:3000`.
8. Chờ trừ bì, treo bịch và đặt tốc độ giọt.
9. Chờ đủ 20 mẫu giọt và 64 mẫu sinh hiệu trước khi test cảnh báo.

## 14. Kiểm tra và xử lý lỗi

Trên Windows:

```powershell
Test-NetConnection 127.0.0.1 -Port 3000
Test-NetConnection 127.0.0.1 -Port 5000
Get-Process mysqld,HisServer,plink -ErrorAction SilentlyContinue
```

Trên Pi:

```bash
ss -ltn | grep -E ':15000|:1885|:8080'
tail -n 50 ~/gateway/logs/zigbee2mqtt.log
tail -n 50 ~/gateway/logs/manual-stack.log
```

### Server báo `SocketException (10048)`

Cổng 3000 hoặc 5000 đang bị tiến trình cũ chiếm:

```powershell
netstat -ano | findstr ":3000 :5000"
Stop-Process -Id <PID>
```

### Giường báo `OFFLINE`

- Kiểm tra G26 đã join Zigbee.
- Kiểm tra Zigbee2MQTT có payload mới.
- Kiểm tra gateway, tunnel và EUI64 thiết bị.

### HR/SpO2 chập chờn

- Giữ ngón tay ổn định và hạn chế ánh sáng ngoài.
- Kiểm tra 3.3 V, GND, SDA PC07, SCL PC05 và điện trở kéo lên.
- Tách dây buzzer khỏi dây I2C.
- Khi đặt tay lại, chờ cửa sổ lọc thu đủ mẫu.

### Lệnh từ web không tới G26

Kiểm tra chiều HIS Server → gateway → MQTT → Zigbee2MQTT → G26. Toast trên web chỉ xác nhận server đã nhận yêu cầu; dữ liệu phản hồi từ G26 mới xác nhận lệnh đã tới thiết bị.

## 15. Build kiểm tra trước khi bàn giao

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\server\HisServer.sln
dotnet run --project .\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

Tài liệu kỹ thuật chi tiết:

- `firmware/PIN_MAP.md`
- `gateway-pi/README.md`
- `server/README.md`
- `SYSTEM_INTEGRATION.md`
- `host_ai/README.md`

## 16. Kết luận

Qua đồ án, nhóm đã kết nối được thiết bị nhúng tại giường, gateway Zigbee trên Raspberry Pi và HIS Server thành một luồng hai chiều hoàn chỉnh. Phần khó không chỉ là đọc cảm biến mà còn là làm dữ liệu ổn định, thống nhất cảnh báo và đảm bảo lệnh từ web đi đúng tới G26.

Hệ thống hiện phù hợp cho trình diễn, thu thập thêm dataset và tiếp tục thử nghiệm thuật toán. Nếu phát triển thành sản phẩm thực tế, cần bổ sung kiểm định cảm biến, an toàn điện, bảo mật, mã hóa kết nối và đánh giá theo quy định thiết bị y tế.

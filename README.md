# Hệ thống AI Chuẩn Đét – Smart IV Monitor

Đây là đồ án sinh viên xây dựng một hệ thống giám sát bệnh nhân đang truyền dịch. Mục tiêu của nhóm là gom dữ liệu sinh hiệu, tốc độ nhỏ giọt và khối lượng bịch dịch về một giao diện web để y tá dễ theo dõi, đồng thời phát cảnh báo ngay tại giường bằng LED và buzzer.

> **Lưu ý:** đây là mô hình nghiên cứu và trình diễn, chưa phải thiết bị y tế đã được kiểm định. Không dùng hệ thống này để thay thế thiết bị theo dõi lâm sàng.

## 1. Hệ thống làm được gì?

- Đo nhịp tim (HR) và SpO2.
- Đếm giọt, tính khoảng cách giữa hai giọt và tốc độ giọt/phút.
- Đo khối lượng bịch dịch bằng loadcell.
- Học mốc sinh hiệu và mốc nhỏ giọt trước khi bật cảnh báo.
- Chạy AI chuỗi thời gian trực tiếp trên G26.
- Ghép cảnh báo sinh hiệu và nhỏ giọt thành ba mức.
- Hiển thị số liệu trên OLED 0.96 inch và web HIS Server.
- Cho phép đặt tốc độ giọt, trừ bì và bật dữ liệu fake từ web.
- Truyền hai chiều qua Zigbee; G26 không tự tạo JSON.

Luồng dữ liệu của hệ thống:

```text
MAX30102 + photodiode + HX711
              |
              v
       EFR32xG26 (firmware + AI + OLED + cảnh báo)
              |
              | Zigbee ZCL Attribute
              v
    Zigbee coordinator + Zigbee2MQTT trên Raspberry Pi
              |
              | MQTT JSON nội bộ
              v
        Gateway TCP trên Raspberry Pi
              |
              | TCP 5000 (trực tiếp hoặc SSH reverse tunnel)
              v
      HIS Server .NET 8 + MySQL + giao diện web 5194
```

## 2. Các thư mục chính

| Thư mục | Nội dung |
|---|---|
| `firmware/` | Firmware G26: cảm biến, OLED, AI, cảnh báo và Zigbee |
| `firmware/models/` | Model TFLite Micro đã nhúng vào firmware |
| `gateway-pi/` | Source gateway MQTT ↔ TCP và cấu hình triển khai Pi |
| `demo1/gateway/` | Converter Zigbee2MQTT cho custom cluster Smart IV |
| `demo1/server/` | HIS Server .NET 8, giao diện web, API và database |
| `host_ai/` | Dataset, mã huấn luyện và tài liệu AI nhỏ giọt |
| `tools/` | Script build, nạp firmware, firewall và khởi động Pi |

## 3. Phần cứng và sơ đồ chân G26

Bo mạch đang sử dụng là EFR32xG26/BRD2709A.

| Khối | Tín hiệu | Chân G26 |
|---|---|---|
| Cảm biến giọt | Digital OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 và OLED | SCL | PC05 |
| MAX30102 và OLED | SDA | PC07 |
| LED xanh | OUT | PA07 |
| LED vàng | OUT | PA04 |
| LED đỏ | OUT | PA05 |
| Buzzer active-low | OUT | PC06 |
| Nút trừ bì | INPUT | PB00 |

Các module dùng mức logic 3.3 V và phải nối chung GND. Với bus I2C dùng chung OLED và MAX30102, nhóm khuyên dùng điện trở kéo lên khoảng `4.7 kΩ`, dây ngắn và tách dây I2C khỏi buzzer.

## 4. Giao thức Zigbee của G26

G26 **không gửi JSON trực tiếp**. Firmware ghi dữ liệu vào ZCL Attribute và report qua Zigbee:

- Endpoint: `2`
- Cluster ID: `0xFC01`
- Manufacturer Code: `0x1049`
- Model: `SmartIV-Sensor`
- Cluster name: `Smart IV Vitals`

| Attribute | Tên | Kiểu | Đơn vị |
|---:|---|---|---|
| `0x0000` | HeartRate | int16s | bpm |
| `0x0001` | Spo2 | int16u | % |
| `0x0002` | FlowRatio | int16u | % |
| `0x0003` | DropRatio | int16s | % |
| `0x0004` | AlarmBitmap | bitmap16 | bit |
| `0x0005` | WeightG | int16u | gram |
| `0x0006` | DropsPerMin | int16u | giọt/phút |
| `0x0007` | TargetFlowMlH | int16u | ml/h |
| `0x0008` | TargetDropsPerMin | int16u | giọt/phút |
| `0x000F` | TsFlags | bitmap16 | bit |
| `0x0010` | HrForecast16s | int16u | bpm |
| `0x0011` | Spo2Forecast16s | int16u | % |
| `0x0012` | HrTrendBpmPerMin | int16s | bpm/phút |
| `0x0013` | TsAnomalyScoreX100 | int16u | điểm ×100 |
| `0x0014` | DropsForecast16s | int16u | giọt/phút |
| `0x0015` | DropsTrendDpmPerMin | int16s | dpm/phút |
| `0x0016` | RemainingMl | int16u | ml |
| `0x0017` | RemainingMin | int16u | phút |
| `0x0018` | MonitoringActive | int8u | 0/1 |

Converter tại `demo1/gateway/zigbee2mqtt_smart_iv_converter.js` đổi các attribute thành MQTT JSON. Gateway chuyển JSON này tới server và chuyển lệnh từ server về lại đúng attribute trên endpoint 2.

Các trường forecast/remaining không còn là placeholder: firmware dùng cửa sổ
20 khoảng giọt để tính xu hướng tuyến tính và dự báo tốc độ sau 16 giây.
`RemainingMl` lấy khối lượng sau trừ bì, trừ 20 g bao bì rỗng và quy đổi gần
đúng 1 g/mL; `RemainingMin` dùng tốc độ đo hiện tại quy đổi theo cặp mục tiêu
dpm/ml-h. Đây là số hỗ trợ theo dõi, không thay thế kiểm tra trực tiếp của y tá.
Mọi attribute telemetry, kể cả `0x0014..0x0017` và `0x001C..0x001E`, đều được
cấu hình report tối đa mỗi 60 giây và report sớm khi giá trị thay đổi.

## 5. Trình tự hoạt động

1. G26 khởi động và trừ bì loadcell trong 10 giây. Không treo bịch ở bước này.
2. OLED yêu cầu mở HIS web để đặt tốc độ giọt.
3. Cảm biến vẫn được đọc và gửi lên web để người dùng kiểm tra kết nối.
4. Treo bịch, nhập tốc độ mục tiêu trên web và xác nhận.
5. Lệnh đi theo chiều Server → TCP gateway → MQTT → Zigbee2MQTT → ZCL → G26.
6. G26 bắt đầu lấy 20 mẫu nhỏ giọt và 64 mẫu sinh hiệu.
7. Khi đủ dữ liệu, AI và cảnh báo được kích hoạt.
8. Mức cảnh báo cuối được gửi ngược lên server kèm nguyên nhân.

## 6. Xử lý HR và SpO2

Cảm biến được đọc mỗi `250 ms`. Bốn lần đọc tạo thành một nhịp cập nhật, nên AI nhận đúng `1 mẫu/giây` và 64 mẫu tương ứng khoảng 64 giây.

Để hạn chế HR nhảy loạn, firmware hiện dùng:

- Cửa sổ trượt 12 lần đọc, cần ít nhất 8 lần hợp lệ.
- Median để loại xung bất thường.
- HR lệch lớn hơn 20 BPM phải lặp lại ba lần mới được chấp nhận.
- HR đã lọc thay đổi tối đa 4 BPM mỗi giây.
- SpO2 cũng có cửa sổ lọc, xác nhận biến động và giới hạn bước thay đổi.
- Mất tín hiệu quá 3 giây sẽ trả về trạng thái không có dữ liệu và xóa cửa sổ cũ.
- Dữ liệu giữ tạm để hiển thị không được tính thành mẫu AI mới.
- Chế độ fake chỉ đi vào nhánh kiểm thử AI; số cảm biến thật vẫn được giữ riêng để đối chiếu.

Không tăng tốc 64 mẫu bằng cách lặp lại cùng một giá trị, vì làm như vậy model sẽ hiểu sai khoảng thời gian của history, trend và forecast 16 giây.

## 7. Logic cảnh báo

### 7.1. Nhỏ giọt

Firmware theo dõi mốc động để chấp nhận việc chai dịch chậm dần một cách tự nhiên. Mốc chỉ bám theo những thay đổi nhỏ còn nằm trong vùng an toàn; xung nhiễu, mất giọt và sai lệch lớn không được học theo.

- Sai lệch `±200 ms`: mức 1 – bình thường.
- Sai lệch trên `200 ms` đến `800 ms`: mức 2 – chú ý.
- Sai lệch trên `800 ms`: mức 3 – cảnh báo.
- Watchdog phát hiện quá lâu không có giọt và đưa nhánh nhỏ giọt lên mức 3.
- MLP và LSTM dùng cửa sổ 20 giọt để phân tích xu hướng; dải vật lý vẫn là lớp bảo vệ chính.

### 7.2. Sinh hiệu

- 60 mẫu đầu tạo baseline HR/SpO2.
- Tiếp tục đủ history 64 mẫu cho AI sinh hiệu.
- Lệch dưới 15% so với baseline: mức 1.
- Lệch từ 15% đến dưới 20%: mức 2.
- Lệch từ 20% trở lên: mức 3.
- Ngưỡng cứng HR dưới 45, HR trên 150 hoặc SpO2 dưới 90 tạo mức 3.

### 7.3. Ghép cảnh báo cuối

| Sinh hiệu | Nhỏ giọt | Mức cuối |
|---:|---:|---:|
| 1 | 1 | 1 |
| 3 | 3 | 3 |
| Mọi tổ hợp còn lại | | 2 |

### 7.4. LED và buzzer

- Mức 1: LED xanh, buzzer tắt.
- Mức 2: LED vàng; buzzer kêu 0.5 giây, nghỉ 3 giây.
- Mức 3: LED đỏ nhấp nháy; buzzer bật/tắt mỗi 0.25 giây.
- Buzzer active-low: PC06 LOW là kêu, HIGH là tắt.

## 8. Kiểm thử trên giao diện web

Web có ba nút kiểm thử sinh hiệu:

- `Real data`: dùng cảm biến thật.
- `Fake HR L2`: tạo HR lệch khoảng 17% so với baseline.
- `Fake HR+O2 L3`: tạo HR và SpO2 lệch khoảng 25%.

`Real HR/SpO2` là dữ liệu thật. `AI test HR/SpO2` là dữ liệu được đưa vào nhánh AI khi test. Khi quay về `Real data`, firmware phục hồi history thật nên không phải học lại từ đầu.

---

# 9. Hướng dẫn cài đặt toàn bộ trên một hệ thống mới

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
Get-Content -Raw .\demo1\server\database\schema.sql |
  & "C:\Program Files\MySQL\MySQL Server 8.4\bin\mysql.exe" -u root -p
```

Sau đó chạy **toàn bộ** file trong `demo1/server/database/migrations` theo thứ tự tên. Đoạn PowerShell dưới đây gộp chúng lại để MySQL chỉ hỏi mật khẩu một lần:

```powershell
$migrationSql = Get-ChildItem .\demo1\server\database\migrations\*.sql |
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
cd .\demo1\server\src\HisServer
dotnet user-secrets set "ConnectionStrings:MySql" `
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU_MYSQL>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet restore
dotnet build
```

Chạy server và giữ terminal mở:

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det\demo1\server\src\HisServer
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
scp -r .\gateway-pi\src iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\gateway-pi\Makefile iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\demo1\gateway\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/smartiv-update/
scp .\tools\run-stack.pi.sh iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/run.sh
```

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

## Bước 10 – Lỗi thường gặp

### `SocketException (10048)` hoặc file HisServer bị khóa

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194 |
  Select-Object LocalPort,OwningProcess
Stop-Process -Id <PID>
```

### Đăng nhập báo `Sign-in failed`

- Kiểm tra MySQL đang chạy.
- Kiểm tra schema và migration tài khoản đã được nạp.
- Chạy `dotnet user-secrets list` trong đúng thư mục `HisServer`.
- Khởi động lại HIS Server sau khi MySQL sẵn sàng.

### BED-01 báo `OFFLINE`

- Kiểm tra G26 có nguồn và đã join Zigbee.
- Kiểm tra Zigbee2MQTT có payload mới.
- Kiểm tra gateway và TCP 5000 `ESTABLISHED`.
- Nếu dùng tunnel, kiểm tra `plink` còn chạy.

### Zigbee2MQTT báo `Failed to init port`

Cổng coordinator đang bị tiến trình khác khóa:

```bash
sudo systemctl stop zigbee2mqtt.service gateway.service 2>/dev/null || true
sudo systemctl restart smart-iv-stack.service
```

### HR/SpO2 hiện `--` hoặc đồ thị có khoảng trống

- Đặt ngón tay ổn định, không ép quá mạnh và tránh ánh sáng ngoài.
- Kiểm tra 3.3 V, GND, SDA PC07, SCL PC05 và điện trở kéo lên.
- Không để dây buzzer sát dây I2C.
- Bỏ tay thì dữ liệu về không có tín hiệu là đúng.
- Đặt tay lại cần khoảng hai giây để cửa sổ lọc đủ mẫu.

### Nút đặt tốc độ hoặc fake không tác động tới G26

Đây là lệnh hai chiều Server → gateway → Zigbee. Kiểm tra gateway TCP, MQTT, thiết bị Zigbee online và converter đúng phiên bản. Toast `enabled` chỉ xác nhận server đã nhận yêu cầu; trường `AI test HR/SpO2` thay đổi mới xác nhận lệnh đã tới G26.

## 10. Build kiểm tra trước khi nộp

```powershell
cd C:\Users\<USER>\Documents\He-thong-AI-Chuan-det
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\demo1\server\HisServer.sln
dotnet run --project .\demo1\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

Khi server mới khởi động, `SmartIvTelemetrySchemaMigrator` tự bổ sung các cột
forecast, trạng thái đường truyền, lượng dịch còn lại và mức cảnh báo vào bảng
`vital_samples`. Tài khoản MySQL phải có quyền `ALTER` trong lần chạy đầu sau
bản cập nhật; các lần chạy sau chỉ kiểm tra schema và không sửa lại dữ liệu cũ.

Tài liệu sâu hơn nằm trong `firmware/PIN_MAP.md`, `gateway-pi/README.md`, `demo1/server/README.md`, `SYSTEM_INTEGRATION.md` và `host_ai/README.md`.

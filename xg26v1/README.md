# Hệ thống AI Chuẩn đét

> Smart IV Monitor — hệ thống thử nghiệm giám sát bệnh nhân đang truyền dịch bằng EFR32xG26, cảm biến nhỏ giọt, loadcell, HR/SpO2 và AI.

Project kết hợp firmware nhúng và ứng dụng máy tính để theo dõi đồng thời:

- nhịp tim (HR);
- nồng độ oxy máu (SpO2);
- khối lượng bịch dịch;
- khoảng cách và tốc độ nhỏ giọt;
- cảnh báo ba mức bằng OLED, LED và buzzer;
- AI MLP/LSTM phân tích chuỗi 20 khoảng giọt;
- AI sinh hiệu chạy trực tiếp trên G26 bằng TensorFlow Lite Micro.

> **Lưu ý:** đây là prototype nghiên cứu/học tập, chưa được kiểm định để thay thế thiết bị y tế hoặc quyết định lâm sàng.

## 1. Hệ thống hoạt động như thế nào?

```text
MAX30102 ── HR/SpO2 ───────┐
Photodiode ── khoảng giọt ─┼─> EFR32xG26 ─> OLED/LED/Buzzer
HX711 ── khối lượng ───────┘       │
                                   ├─ USB COM ─> GUI Python ─> MLP + LSTM
                                   └─ Zigbee ZCL EP2 ─> Gateway/Zigbee2MQTT
```

1. G26 tự trừ bì loadcell trong 10 giây. Trong thời gian này không treo bịch.
2. OLED báo sẵn sàng; người dùng treo bịch dịch và mở ứng dụng máy tính.
3. Người dùng chọn khoảng đặt hoặc tốc độ giọt.
4. Firmware đọc cảm biến, cập nhật OLED và ghi dữ liệu vào Zigbee ZCL Attribute.
5. GUI phân tích chuỗi nhỏ giọt bằng MLP/LSTM và gửi mức nhỏ giọt về G26.
6. G26 kết hợp mức sinh hiệu và mức nhỏ giọt để điều khiển cảnh báo cuối.

## 2. Phần cứng

### Bo mạch

- Silicon Labs BRD2709A / EFR32xG26.
- OLED SSD1306 0.96 inch, 128×64, địa chỉ I²C `0x3C`.
- Cảm biến HR/SpO2 MAX30102, địa chỉ I²C `0x57`.
- Photodiode/module cảm biến giọt có ngõ ra số.
- Loadcell và HX711.
- Ba LED xanh/vàng/đỏ.
- Buzzer active-low, điều khiển qua transistor.

### Sơ đồ chân cố định

| Thiết bị | Tín hiệu | Chân G26 |
|---|---|---|
| Cảm biến giọt | D0/OUT | PD02 |
| HX711 | DOUT | PC01 |
| HX711 | SCK | PC03 |
| MAX30102 + OLED | SCL | PC05 |
| MAX30102 + OLED | SDA | PC07 |
| LED xanh | OUT | PA07 |
| LED vàng | OUT | PA04 |
| LED đỏ | OUT | PA05 |
| Buzzer active-low | OUT | PC06 |
| Nút trừ bì BTN0 | INPUT | PB00 |

Tất cả module dùng logic **3.3 V** và phải chung GND. SDA/SCL nên có điện trở kéo lên 3.3 V khoảng `4.7 kΩ`; dây I²C nên ngắn và tránh dây buzzer.

## 3. Cài đặt nhanh

### Yêu cầu

- Windows 10/11.
- Silicon Labs Tools/Simplicity Studio có compiler, CMake và Commander.
- Python 3.10 trở lên.
- Cáp USB hỗ trợ truyền dữ liệu.

### Tải source

```powershell
git clone https://github.com/dinhhieu1st-debug/He-thong-AI-Chuan-det.git
cd He-thong-AI-Chuan-det
```

### Cài thư viện Python

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install --upgrade pip
pip install -r host_ai\requirements.txt
```

### Build firmware

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

File tạo ra:

```text
cmake_gcc/build/base/smart-iv-monitor.hex
```

### Nạp firmware

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\flash_firmware.ps1
```

Nếu có nhiều kit, truyền serial number:

```powershell
.\tools\flash_firmware.ps1 -SerialNo <SERIAL_NUMBER>
```

### Mở ứng dụng giám sát

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_ai_gui.ps1
```

Sau đó:

1. chọn đúng cổng COM;
2. nhấn **Kết nối**;
3. chờ G26 trừ bì xong;
4. treo bịch dịch;
5. chọn preset hoặc nhập khoảng tùy chỉnh;
6. nhấn **Xác nhận tốc độ**;
7. theo dõi tab **Giám sát AI**.

## 4. Đọc giao diện OLED

| Nhãn | Ý nghĩa | Đơn vị |
|---|---|---|
| `HR` | Nhịp tim hiện tại | BPM |
| `O2` | SpO2 hiện tại | % |
| `DR` | Tốc độ giọt thực tế | giọt/phút |
| `GP` | Khoảng cách giữa hai giọt | giây |
| `BG` | Khối lượng bịch dịch | kg |
| `ST` | Tốc độ giọt đã đặt | giọt/phút |

Hai dòng cuối hiển thị trạng thái như `NORMAL`, `L2: CAUTION`, `L3 CRITICAL` và nguyên nhân `HR`, `SPO2`, `DROP` hoặc tổ hợp của chúng.

Khi MAX30102 không còn trả về mẫu hợp lệ trong 3 giây, OLED hiển thị `--` thay vì giữ số HR/SpO2 cũ. Baseline và lịch sử AI vẫn được giữ để cảm biến có thể tiếp tục mà không phải học lại.

## 5. Logic cảnh báo

### Nhỏ giọt

- Sai lệch được so với **mốc động** thay vì chỉ dùng mốc đặt cố định.
- Mốc động chỉ đi chậm dần khi từng thay đổi nhỏ vẫn nằm trong vùng bình thường.
- Tốc độ bám tối đa mỗi giọt và tổng độ trôi đều bị giới hạn.
- Xung đột ngột, mất giọt và sai lệch lớn không được học theo.
- `±200 ms`: bình thường.
- Trên `200 ms` đến `800 ms`: chú ý.
- Trên `800 ms`: cảnh báo.
- Watchdog firmware phát hiện khi quá lâu không có giọt.

MLP và LSTM dùng cửa sổ 20 giọt. Kết quả AI được hiển thị để phân tích xu hướng; cơ cấu chấp hành nhỏ giọt vẫn theo dải an toàn vật lý đã cấu hình.

### Sinh hiệu

- Hệ thống học 60 mẫu để tạo HR/SpO2 nền.
- Tiếp tục nạp đủ lịch sử 64 mẫu cho AI sinh hiệu.
- Lệch dưới 15%: mức 1.
- Lệch từ 15% đến dưới 20%: mức 2.
- Lệch từ 20% trở lên: mức 3.
- Ngưỡng cứng vẫn được áp dụng: HR dưới 45, HR trên 150 hoặc SpO2 dưới 90 tạo mức 3.

### Ghép cảnh báo cuối

| Sinh hiệu | Nhỏ giọt | Đầu ra cuối |
|---:|---:|---:|
| 1 | 1 | 1 |
| 3 | 3 | 3 |
| Các tổ hợp còn lại | | 2 |

### LED và buzzer

- Mức 1: LED xanh, buzzer tắt.
- Mức 2: LED vàng; buzzer kêu 0.5 giây, nghỉ 3 giây.
- Mức 3: LED đỏ nhấp nháy; buzzer bật/tắt nhanh 0.25 giây.
- Buzzer active-low: PC06 LOW là kêu, HIGH là tắt.

## 6. Chế độ kiểm thử sinh hiệu

Tab giám sát có ba lựa chọn:

- **Tắt fake**: dùng cảm biến thật và khôi phục lịch sử AI trước khi test.
- **Fake mức 2**: tạo HR lệch khoảng 17% so với baseline.
- **Fake mức 3**: tạo HR lệch khoảng 25% so với baseline.

HR/SpO2 thô vẫn hiển thị dữ liệu cảm biến thật để đối chiếu. Khi tắt fake, baseline và lịch sử AI thật được phục hồi, không phải học lại từ đầu.

## 7. Dữ liệu và mô hình AI

Schema dữ liệu giọt:

```text
timestamp_ms,drop_number,target_interval_ms,actual_interval_ms
```

Feature AI:

```text
ratio = actual_interval / expected_interval
error_percent = (ratio - 1) × 100
delta_ratio = ratio hiện tại - ratio trước
```

- MLP NumPy: `host_ai/models/mlp_baseline/`.
- LSTM ONNX: `host_ai/models/lstm/`.
- Dataset thật: `host_ai/data/raw/`.
- Dataset mô phỏng: `host_ai/data/synthetic/`.
- Thông tin dataset: `host_ai/DATASET_CARD.md`.
- Model card: `host_ai/models/*/MODEL_CARD.md`.

Dataset mô phỏng được tách riêng với dữ liệu thật và có các tình huống stable, gradually slowing/speeding, rapid change, disturbance, missing drop, recovery và irregular.

## 8. Giao thức Serial chính

Baudrate: `115200`.

| Dòng/lệnh | Ý nghĩa |
|---|---|
| `AI_READY` | G26 đã trừ bì và chờ tốc độ |
| `SET,<ms>` | GUI gửi khoảng đặt |
| `AI_SET_OK,<ms>` | G26 xác nhận mốc |
| `<time>,<count>,<target>,<actual>` | Bản ghi một giọt |
| `LEVEL,<1..3>` | GUI gửi mức nhỏ giọt |
| `DROP_TIMEOUT,<ms>` | Firmware báo quá lâu không có giọt |
| `VITAL_RAW,<time>,<hr>,<spo2>` | Dữ liệu sinh hiệu thô |
| `TELEMETRY,...` | Toàn bộ thông số hiển thị |
| `FAKE_VITAL,<0|2|3>` | Chọn chế độ test sinh hiệu |

## 9. Zigbee ZCL Attribute

XG26 **không tạo hoặc gửi JSON trực tiếp**. Firmware ghi dữ liệu vào custom ZCL cluster; gateway và converter Zigbee2MQTT chịu trách nhiệm chuyển attribute thành JSON.

| Thành phần | Giá trị |
|---|---|
| Endpoint | `2` |
| Cluster | `0xFC01` — Smart IV Vitals |
| Manufacturer Code | `0x1049` |
| Model Identifier | `SmartIV-Sensor` |
| Reporting | tối thiểu 1 giây, tối đa 60 giây |

| Attribute | Tên | Kiểu | Nội dung |
|---:|---|---|---|
| `0x0000` | HeartRate | int16s | BPM; `0` khi mất tín hiệu |
| `0x0001` | Spo2 | int16u | %; `0` khi mất tín hiệu |
| `0x0002` | FlowRatio | int16u | tốc độ thực tế/mốc đặt, % |
| `0x0003` | DropRatio | int16s | tốc độ thực tế/mốc đặt, % |
| `0x0004` | AlarmBitmap | bitmap16 | trạng thái cảnh báo |
| `0x0005` | WeightG | int16u | gram |
| `0x0006` | DropsPerMin | int16u | giọt/phút |
| `0x0007` | TargetFlowMlH | int16u | ml/h; `0xFFFF` nếu chưa có nguồn dữ liệu |
| `0x0008` | TargetDropsPerMin | int16u | giọt/phút |
| `0x000F` | TsFlags | bitmap16 | trạng thái AI chuỗi thời gian |
| `0x0010` | HrForecast16s | int16u | HR dự báo sau 16 giây |
| `0x0011` | Spo2Forecast16s | int16u | SpO2 dự báo sau 16 giây |
| `0x0012` | HrTrendBpmPerMin | int16s | xu hướng HR mỗi phút |
| `0x0013` | TsAnomalyScoreX100 | int16u | điểm bất thường ×100 |
| `0x0014` | DropsForecast16s | int16u | dự báo giọt; `0xFFFF` nếu chưa khả dụng |
| `0x0015` | DropsTrendDpmPerMin | int16s | xu hướng giọt mỗi phút |
| `0x0016` | RemainingMl | int16u | ml còn lại; `0xFFFF` nếu chưa khả dụng |
| `0x0017` | RemainingMin | int16u | phút còn lại; `0xFFFF` nếu chưa khả dụng |
| `0x0018` | MonitoringActive | int8u | `1` đang giám sát, `0` chưa giám sát |

`AlarmBitmap` dùng bit 0 cho mức chú ý, bit 1 cho mức đỏ, bit 2 cho sinh hiệu và bit 3 cho nhỏ giọt. Khi mất tín hiệu HR/SpO2, forecast tương ứng cũng chuyển sang `0xFFFF`.

Ví dụ HR 75 BPM, SpO2 98%, tỷ lệ dòng/giọt 100%, cân 496 g và 20 giọt/phút được report trên EP2/`0xFC01` như sau:

```text
0x0000 = 75
0x0001 = 98
0x0002 = 100
0x0003 = 100
0x0005 = 496
0x0006 = 20
```

## 10. Cấu trúc source

```text
firmware/                 Driver cảm biến, OLED, cảnh báo và AI sinh hiệu
firmware/models/          Model TFLite Micro đã nhúng
host_ai/inference/        GUI desktop và inference realtime
host_ai/models/           MLP/LSTM và cấu hình inference
host_ai/data/             Dataset thật, processed và synthetic
host_ai/training/         Mã huấn luyện mô hình
host_ai/scripts/          Logger và công cụ dataset
tools/                    Build, flash, serial monitor và chạy GUI
autogen/, config/         Thành phần sinh bởi Silicon Labs
cmake_gcc/                Cấu hình build GCC
bootloader/               Bootloader project
```

## 11. Xử lý lỗi thường gặp

### GUI không có dữ liệu

- Chọn đúng COM và đóng Serial Monitor khác.
- Kiểm tra baudrate 115200.
- Chờ `AI_READY`, sau đó xác nhận tốc độ.
- Nạp đúng firmware mới nhất.

### HR/SpO2 lúc có lúc không

Firmware hiện hỗ trợ I²C clock stretching, tự phục hồi bus sau lỗi giao tiếp và đọc lại tối đa ba lần nếu gặp một khung HR/SpO2 tạm thời không hợp lệ. Không nên tăng thời gian giữ số để che lỗi, vì khi người dùng bỏ tay hệ thống phải xóa dữ liệu cũ.

- Dùng nguồn 3.3 V ổn định và chung GND.
- Dùng pull-up SDA/SCL khoảng 4.7 kΩ.
- Rút ngắn dây PC05/PC07 và tránh dây buzzer.
- Giữ ngón tay cố định, che ánh sáng ngoài.
- Kiểm tra log `VITAL_RAW`: nếu log vẫn chạy nhưng GUI mất số thì kiểm tra phần mềm; nếu `VITAL_RAW` dừng quá 3 giây khi tay vẫn giữ nguyên thì kiểm tra nguồn, SDA/SCL và pull-up của bus dùng chung OLED/MAX30102.
- Khi bỏ tay thật, OLED và GUI hiện `--`; ZCL `HeartRate`/`Spo2` về `0`.

### Nhỏ giọt xuất hiện khoảng cực ngắn

- Kiểm tra log firmware có dòng `Anti-double-pulse gap`.
- Kiểm tra dây photodiode, mức logic 3.3 V và nhiễu nguồn.
- Không đặt dây tín hiệu sát dây buzzer.
- So sánh khoảng thực tế với `moc dong` trong log GUI.

### Buzzer kêu ngược

Buzzer của hệ thống là active-low: LOW kêu, HIGH tắt. Không đảo lại logic nếu không thay phần cứng kích transistor.

## 12. Kiểm tra trước khi commit

```powershell
python -m py_compile host_ai\inference\desktop_gui.py
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

## 13. Giới hạn an toàn

- Các model hiện tại là prototype nghiên cứu.
- Dataset synthetic không thay thế kiểm định trên nhiều bộ dây truyền và điều kiện thực tế.
- Cần kiểm định độc lập, phân tích rủi ro và chứng nhận phù hợp trước mọi ứng dụng y tế.
- Không dựa duy nhất vào hệ thống này để theo dõi hoặc điều trị bệnh nhân.

## Nguồn tham khảo của module AI nhỏ giọt

Pipeline AI nhỏ giọt ban đầu được phát triển tại [AI-nho-giot](https://github.com/dinhhieu1st-debug/AI-nho-giot) và đã được tích hợp, điều chỉnh cho EFR32xG26 trong repository này.

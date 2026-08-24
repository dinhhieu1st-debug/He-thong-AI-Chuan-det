# 3. Giao thức Zigbee và hợp đồng dữ liệu

G26 **không gửi JSON**. Firmware chỉ ghi giá trị vào ZCL attribute rồi để
Zigbee report. Việc biến chúng thành JSON là của converter zigbee2mqtt.

## Định danh cluster

| | Giá trị |
|---|---|
| Endpoint | `2` |
| Cluster ID | `0xFC01` |
| Cluster name | `Smart IV Vitals` |
| Manufacturer code | `0x1049` |
| Model | `SmartIV-Sensor` |

Nguồn sự thật: `firmware/app.c` (khối `#define SMART_IV_*` và `ATTR_*`) và
`gateway-pi/zigbee2mqtt/zigbee2mqtt_smart_iv_converter.js`. **Hai file này
phải sửa cùng nhau** — thêm attribute ở một bên mà quên bên kia thì dữ liệu
im lặng biến mất chứ không báo lỗi.

## Bảng attribute

### Chỉ số lâm sàng

| ID | Tên | Kiểu | Đơn vị | Khoá JSON |
|---|---|---|---|---|
| `0x0000` | HeartRate | int16s | bpm | `heart_rate` |
| `0x0001` | Spo2 | int16u | % | `spo2` |
| `0x0002` | FlowRatio | int16u | % so với mục tiêu | `flow` |
| `0x0003` | DropRatio | int16s | % so với mục tiêu | `drop_rate` |
| `0x0005` | WeightG | int16u | gram | `weight_g` |
| `0x0006` | DropsPerMin | int16u | giọt/phút | `drops_per_min` |

`FlowRatio` và `DropRatio` là **phần trăm** (tỉ số × 100), không phải phân số.

### Mục tiêu và lệnh từ web (ghi được)

| ID | Tên | Kiểu | Ý nghĩa |
|---|---|---|---|
| `0x0007` | TargetFlowMlH | int16u | Tốc độ mục tiêu, ml/h |
| `0x0008` | TargetDropsPerMin | int16u | **Tốc độ mục tiêu, giọt/phút — chấp nhận 1..240** |
| `0x0009` | TareCommand | int8u | Ghi để trừ bì loadcell từ xa |
| `0x000A` | HrRecalibrate | int8u | Ghi để học lại baseline HR |
| `0x001F` | VitalsTestMode | int8u | Chế độ fake sinh hiệu, xem [`04-canh-bao.md`](04-canh-bao.md) |

### Trạng thái học và vận hành

| ID | Tên | Kiểu | Ý nghĩa | Khoá JSON |
|---|---|---|---|---|
| `0x000B` | HrBaselineSecondsRemaining | int8u | Còn bao nhiêu giây nữa xong baseline HR | `hr_baseline_seconds_remaining` |
| `0x000C` | HrBaselineBpm | int16u | Baseline HR đã học được | `hr_baseline_bpm` |
| `0x000D` | TareEventCount | int8u | Số lần đã trừ bì | `tare_event_count` |
| `0x000E` | HrBaselineEventCount | int8u | Số lần đã học lại baseline | `hr_baseline_event_count` |
| `0x0018` | MonitoringActive | int8u | 0/1 | |
| `0x0019` | DropTrainingSamples | int8u | `0..20` | `drop_training_samples` |
| `0x001A` | VitalsTrainingSamples | int8u | `0..64` | `vitals_training_samples` |
| `0x001B` | AlertsArmed | int8u | 0/1 — **cảnh báo đã bật hay chưa** | `alerts_armed` |
| `0x001C` | DropIntervalMs | int16u | Khoảng giọt đo được gần nhất | |
| `0x001D` | DropEventCount | int16u | Tổng số giọt đã đếm | |

### Đầu ra AI và chẩn đoán

| ID | Tên | Kiểu | Ý nghĩa |
|---|---|---|---|
| `0x0010` | HrForecast16s | int16u | Dự báo HR sau 16 giây |
| `0x0011` | Spo2Forecast16s | int16u | Dự báo SpO2 sau 16 giây |
| `0x0012` | HrTrendBpmPerMin | int16s | Xu hướng HR, bpm/phút |
| `0x0013` | TsAnomalyScoreX100 | int16u | Điểm bất thường × 100 |
| `0x0014` | DropsForecast16s | int16u | Dự báo tốc độ giọt |
| `0x0015` | DropsTrendDpmPerMin | int16s | Xu hướng tốc độ giọt |
| `0x0016` | RemainingMl | int16u | Còn bao nhiêu ml |
| `0x0017` | RemainingMin | int16u | Còn bao nhiêu phút |
| `0x001E` | ServerDropLevel | int8u | Mức nhánh nhỏ giọt, 1/2/3 |
| `0x0020` | AiInputHeartRate | int16s | HR **thực sự đưa vào AI** (khác HR thật khi đang test) |
| `0x0021` | AiInputSpo2 | int16u | SpO2 thực sự đưa vào AI |
| `0x0022` | VitalsLevel | int8u | Mức nhánh sinh hiệu, 1/2/3 |

`0xFFFF` nghĩa là **"chưa ước lượng được"**, không phải số 0. Converter đổi
nó thành `null`. Đây là chủ ý: báo "còn 0 phút" cho một bình chưa treo là thứ
khiến y tá thôi tin cái máy.

### Bitmap

| ID | Tên | Kiểu |
|---|---|---|
| `0x0004` | AlarmBitmap | bitmap16 |
| `0x000F` | TsFlags | bitmap16 |

**AlarmBitmap** — bit nguyên nhân, các bit `0x0001..0x0010` chỉ được đặt khi
`alerts_armed` đã bật:

| Bit | Khoá JSON | Nghĩa |
|---|---|---|
| `0x0001` | `signal_lost` | Mất tín hiệu sinh hiệu |
| `0x0002` | `spo2_low` | SpO2 dưới 90 hoặc lệch ≥15% so với baseline |
| `0x0004` | `heart_rate_abnormal` | HR ngoài 45..150 hoặc lệch ≥15% so với baseline |
| `0x0008` | `line_blocked` | Nhánh nhỏ giọt vượt mức 1 |
| `0x0010` | `ae_alarm` | Autoencoder báo bất thường |
| `0x0020` / `0x0040` | `hr_signal`, `spo2_signal` | Kênh đang có tín hiệu |
| `0x0080` | `flow_signal` | Loadcell đã kết nối và đã trừ bì |
| `0x0100` | `drops_signal` | Đã đếm được ít nhất một giọt |
| `0x0200` | `tare_in_progress` | Đang trừ bì |
| `0x0400` | `tare_just_completed` | Vừa trừ bì xong |
| `0x0800` | `hr_baseline_just_completed` | Vừa học xong baseline HR |

**TsFlags** — trạng thái AI và mức cảnh báo:

| Bit | Khoá JSON | Nghĩa |
|---|---|---|
| `0x0001` | `ts_ready` | Đủ history cho AI |
| `0x0002` | `ts_anomaly` | AI báo bất thường |
| `0x0004` | `ts_early_warning` | Dự báo vượt ngưỡng sớm |
| bit 3–4 | `ts_trend` | Xu hướng HR: 0 ổn định, 1 tăng, 2 giảm |
| bit 5–6 | `drops_trend` | Xu hướng tốc độ giọt, cùng cách mã hoá |
| `0x0080` | `hr_forecast_trusted` | Dự báo HR đáng tin |
| `0x0100` | `drops_forecast_trusted` | Dự báo giọt đáng tin |
| bit 9–10 | `alert_level` | Mức LED thô: **0 xanh, 1 vàng, 2 đỏ** |
| `0x0800` | `line_branch` | Nhánh đường truyền đang bất thường |
| `0x1000` | `patient_branch` | Nhánh bệnh nhân đang bất thường |
| bit 13–15 | `line_state` | Phán quyết loadcell, gửi dạng `state+1`; `0` = chưa kết luận được → `null` |

## Hai cách đánh số mức — đừng nhầm

Firmware lưu **enum LED `0/1/2`** trong TsFlags để tương thích ngược. Converter
publish thêm khoá riêng ở thang **`1/2/3`** mà HIS và OLED dùng:

| `alert_level` (thô) | `final_alert_level` (HIS) | Ý nghĩa |
|---|---|---|
| 0 | **1** | Stable — cả sinh hiệu lẫn nhỏ giọt đều bình thường |
| 1 | **2** | Warning — một trong hai nhánh cần chú ý |
| 2 | **3** | Critical — cả hai nhánh đều ở mức 3 |

`line_branch`, `patient_branch`, `spo2_low`, `heart_rate_abnormal`,
`line_blocked` và `ae_alarm` chỉ **giải thích nguyên nhân**. Server hiển thị
chúng và **không** dùng chúng để tính lại mức nào cả.

Trạng thái học (`drop_training_samples`, `vitals_training_samples`,
`alerts_armed`) cũng vậy: HIS hiển thị nguyên xi, không tái tạo thuật toán
firmware.

## Trạng thái triển khai trên chip

Bốn attribute dưới đây từng chỉ tồn tại trên giấy — cluster/converter/server đã
map sẵn, nhưng firmware ghi cứng sentinel "chưa có" mỗi chu kỳ thay vì tính
thật. Đã có logic thật trong `firmware/app.c` (nhánh
`feature/complete-smart-iv-telemetry`, hàm `publish_zigbee_attributes`):

| Khoá JSON | Trước đây | Bây giờ | Hàm chịu trách nhiệm |
|---|---|---|---|
| `drops_forecast_16s`, `drops_trend_dpm_per_min` | luôn `0xFFFF` / `0` | hồi quy tuyến tính trên 20 khoảng giọt gần nhất | `update_drop_forecast()` |
| `remaining_ml`, `remaining_min` | luôn `0xFFFF` | từ khối lượng loadcell trừ khối lượng bao bì rỗng, chia tốc độ tiêu hao đo được | `calculate_line_state()` + khối tính trong `publish_zigbee_attributes()` |
| `line_state` (bit 13–15 của `TsFlags`) | không bao giờ được set → luôn `null` | phán quyết ok / sắp hết / tắc / chảy tự do / lỗi cảm biến / hết dịch | `calculate_line_state()` |
| `drops_forecast_trusted` (bit `0x0100` của `TsFlags`) | không bao giờ được set → luôn `false` | set khi đã đủ 20 mẫu **và** đường truyền không đang timeout/bất thường | `publish_zigbee_attributes()` |
| `ts_anomaly_score` | chỉ nhị phân `0`/`100` | điểm lỗi forecast liên tục ×100 (`vitals_ai.anomaly_score`) | `vitals_ai_step()` trong `vitals_ai.cpp` |

`calculate_line_state()` chỉ trả kết quả khi loadcell đã kết nối **và** đã
tare — nếu không, `line_state` vẫn là `null` một cách hợp lệ (chưa đủ thông
tin), không phải bug.

Một thiết bị chưa OTA lên bản firmware này sẽ tiếp tục gửi các sentinel cũ ở
trên cho tới khi được cập nhật — server/UI đã xử lý `null`/`0xFFFF` như "chưa
có dữ liệu" từ trước, nên không cần chờ đồng loạt mọi giường lên firmware mới
mới bật được các card này.

## Định dạng TCP về server

Gateway mở TCP tới HIS `:5000` và gửi **mỗi dòng một object JSON**:

```json
{"bedId":"BED-01","room":"ICU-1","spo2":98,"heartRate":75,"dripRate":20}
```

Tên trường khớp không phân biệt hoa thường và có nhiều alias giữ lại cho
firmware cũ — danh sách đầy đủ ở
`He-thong-AI-Chuan-det-server/server/src/HisServer/Ingestion/BedDataParser.cs`.

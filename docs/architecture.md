# Kiến trúc hệ thống

## Luồng dữ liệu

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

## Phần cứng và sơ đồ chân G26

Bo mạch đang sử dụng là EFR32xG26/BRD2709A. Chi tiết đầy đủ nằm ở
[`firmware/PIN_MAP.md`](../firmware/PIN_MAP.md); tóm tắt:

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

## Giao thức Zigbee của G26

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

Converter Zigbee2MQTT đổi các attribute thành MQTT JSON (xem
[`docs/system-integration.md`](system-integration.md) về vị trí file converter
hiện đang thiếu trong repo). Gateway chuyển JSON này tới server và chuyển lệnh
từ server về lại đúng attribute trên endpoint 2.

## Trình tự hoạt động

1. G26 khởi động và trừ bì loadcell trong 10 giây. Không treo bịch ở bước này.
2. OLED yêu cầu mở HIS web để đặt tốc độ giọt.
3. Cảm biến vẫn được đọc và gửi lên web để người dùng kiểm tra kết nối.
4. Treo bịch, nhập tốc độ mục tiêu trên web và xác nhận.
5. Lệnh đi theo chiều Server → TCP gateway → MQTT → Zigbee2MQTT → ZCL → G26.
6. G26 bắt đầu lấy 20 mẫu nhỏ giọt và 64 mẫu sinh hiệu.
7. Khi đủ dữ liệu, AI và cảnh báo được kích hoạt.
8. Mức cảnh báo cuối được gửi ngược lên server kèm nguyên nhân.

## Xử lý HR và SpO2

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

## Logic cảnh báo

### Nhỏ giọt

Firmware theo dõi mốc động để chấp nhận việc chai dịch chậm dần một cách tự nhiên. Mốc chỉ bám theo những thay đổi nhỏ còn nằm trong vùng an toàn; xung nhiễu, mất giọt và sai lệch lớn không được học theo.

- Sai lệch `±200 ms`: mức 1 – bình thường.
- Sai lệch trên `200 ms` đến `800 ms`: mức 2 – chú ý.
- Sai lệch trên `800 ms`: mức 3 – cảnh báo.
- Watchdog phát hiện quá lâu không có giọt và đưa nhánh nhỏ giọt lên mức 3.
- MLP và LSTM dùng cửa sổ 20 giọt để phân tích xu hướng; dải vật lý vẫn là lớp bảo vệ chính.

### Sinh hiệu

- 60 mẫu đầu tạo baseline HR/SpO2.
- Tiếp tục đủ history 64 mẫu cho AI sinh hiệu.
- Lệch dưới 15% so với baseline: mức 1.
- Lệch từ 15% đến dưới 20%: mức 2.
- Lệch từ 20% trở lên: mức 3.
- Ngưỡng cứng HR dưới 45, HR trên 150 hoặc SpO2 dưới 90 tạo mức 3.

### Ghép cảnh báo cuối

| Sinh hiệu | Nhỏ giọt | Mức cuối |
|---:|---:|---:|
| 1 | 1 | 1 |
| 3 | 3 | 3 |
| Mọi tổ hợp còn lại | | 2 |

Xem thêm quy ước severity giữa firmware và HIS ở
[`docs/system-integration.md`](system-integration.md#alert-contract).

### LED và buzzer

- Mức 1: LED xanh, buzzer tắt.
- Mức 2: LED vàng; buzzer kêu 0.5 giây, nghỉ 3 giây.
- Mức 3: LED đỏ nhấp nháy; buzzer bật/tắt mỗi 0.25 giây.
- Buzzer active-low: PC06 LOW là kêu, HIGH là tắt.

## Kiểm thử trên giao diện web

Web có ba nút kiểm thử sinh hiệu:

- `Real data`: dùng cảm biến thật.
- `Fake HR L2`: tạo HR lệch khoảng 17% so với baseline.
- `Fake HR+O2 L3`: tạo HR và SpO2 lệch khoảng 25%.

`Real HR/SpO2` là dữ liệu thật. `AI test HR/SpO2` là dữ liệu được đưa vào nhánh AI khi test. Khi quay về `Real data`, firmware phục hồi history thật nên không phải học lại từ đầu.

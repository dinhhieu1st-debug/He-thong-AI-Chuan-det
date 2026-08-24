# 2. Phần cứng và sơ đồ chân

Bo mạch: **EFR32xG26 / BRD2709A**.

Sơ đồ chân này được giữ cố định trong suốt quá trình phát triển; firmware được
build lại và kiểm thử từng ngoại vi một trên đúng bảng chân này. Đổi chân là
phải sửa cả `firmware/` lẫn `smart-iv-monitor.slcp`.

## Bảng chân

| Khối | Tín hiệu | Chân G26 |
|---|---|---|
| Cảm biến giọt (photodiode) | D0 / OUT | `PD02` |
| HX711 (loadcell) | DOUT | `PC01` |
| HX711 (loadcell) | SCK | `PC03` |
| MAX30102 + OLED | I2C SCL | `PC05` |
| MAX30102 + OLED | I2C SDA | `PC07` |
| LED xanh | Output | `PA07` |
| LED vàng | Output | `PA04` |
| LED đỏ | Output | `PA05` |
| Buzzer (mikroBUS RST) | Output | `PC06` |
| Nút trừ bì | BTN0, input | `PB00` |

## Lưu ý đấu nối

- Mọi module dùng mức logic **3.3 V** và phải **nối chung GND** với board.
- Bus I2C dùng chung cho OLED và MAX30102: dùng điện trở kéo lên khoảng
  **4.7 kΩ**, dây càng ngắn càng tốt.
- **Tách dây I2C khỏi dây buzzer.** Buzzer đóng cắt gây nhiễu, đi sát dây I2C
  là HR/SpO2 nhảy loạn hoặc mất hẳn.
- Buzzer **active-low**: `PC06` mức LOW là kêu, mức HIGH là im.

## Cảm biến

| Cảm biến | Đo gì | Ghi chú |
|---|---|---|
| MAX30102 | Nhịp tim, SpO2 | Đọc mỗi 250 ms; 4 lần đọc gộp thành 1 mẫu AI (1 mẫu/giây) |
| Photodiode + laser | Từng giọt rơi | Firmware đo khoảng thời gian giữa hai giọt, không đếm tổng |
| HX711 + loadcell | Khối lượng bịch dịch | Trừ bì tự động 10 giây khi khởi động |

## Trình tự khởi động phần cứng

1. Cấp nguồn G26. OLED hiện `STARTING / DO NOT HANG BAG / TARE IN 10 SEC`.
2. **Không treo bịch dịch trong 10 giây này** — chip đang trừ bì loadcell.
3. Trừ bì xong, OLED yêu cầu mở HIS web để đặt tốc độ giọt mục tiêu.
4. Treo bịch, đặt tốc độ trên web, xác nhận. Chip bắt đầu học.

Có thể trừ bì lại bất cứ lúc nào bằng nút `PB00` hoặc lệnh tare từ web
(attribute `0x0009`).

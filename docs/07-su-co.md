# 7. Lỗi thường gặp

Sắp theo nơi triệu chứng xuất hiện. Trước khi tra ở đây, chạy phần
[kiểm tra kết nối](06-van-hanh.md#62-kiểm-tra-kết-nối) — phần lớn sự cố lộ ra
ngay ở đó.

---

## HIS Server

### `SocketException (10048)` hoặc file `HisServer.exe` bị khoá

Một tiến trình HIS cũ chưa tắt hẳn và vẫn giữ cổng.

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194 |
  Select-Object LocalPort,OwningProcess
Stop-Process -Id <PID>
```

### Đăng nhập báo `Sign-in failed`

Kiểm tra theo thứ tự:

1. MySQL có đang chạy không (`Get-Service MySQL*`).
2. Đã nạp `schema.sql` **và toàn bộ** migration chưa — tài khoản demo nằm trong
   migration `2026-08-21_seed_demo_accounts.sql`, không nằm trong schema.
3. `dotnet user-secrets list` — chạy **trong đúng thư mục `src/HisServer`**,
   chạy sai thư mục sẽ ra rỗng và trông như chưa cấu hình.
4. Khởi động lại HIS **sau khi** MySQL đã sẵn sàng. HIS fail-fast lúc startup
   nếu không có connection string.

### Server chạy nhưng dashboard trống

Server không sinh dữ liệu; nó chờ dữ liệu từ TCP `5000`. Xem mục *Gateway* bên dưới.

---

## Gateway và Zigbee

### `BED-01` báo `OFFLINE`

Đi ngược chuỗi từ giường về server, dừng ở mắt xích đầu tiên hỏng:

1. G26 có nguồn và đã join Zigbee chưa? (`http://<PI_IP>:8080`)
2. Zigbee2MQTT có nhận payload mới không? Nếu không → vấn đề ở Zigbee.
3. Gateway log có dòng `Da ket noi HIS server ...` không?
4. `ss -tn | grep ':5000'` trên Pi có kết nối `ESTAB` không?
5. Nếu dùng tunnel: cửa sổ `plink` còn chạy không?

HIS đánh dấu giường Offline khi **90 giây** không có dữ liệu
(`Offline:ThresholdSeconds`).

### Zigbee2MQTT báo `Failed to init port`

Cổng coordinator đang bị tiến trình khác giữ — hầu như luôn là do vừa chạy
service vừa chạy tay `run.sh`.

```bash
sudo systemctl stop zigbee2mqtt.service gateway.service 2>/dev/null || true
sudo systemctl restart smart-iv-stack.service
```

### Thiết bị online nhưng thiếu trường dữ liệu

Converter trên Pi cũ hơn firmware. Đẩy lại converter theo
[mục 6.5](06-van-hanh.md#65-thay-converter-zigbee2mqtt).

Dấu hiệu nhận biết: `heart_rate` và `spo2` vẫn về, nhưng `final_alert_level`,
`drop_training_samples`, `vitals_training_samples` hoặc `alerts_armed` không có.

---

## Cảm biến

### HR/SpO2 hiện `--` hoặc đồ thị có khoảng trống

`--` là **trạng thái đúng** khi không có tín hiệu, không phải lỗi hiển thị. Bỏ
tay ra khỏi cảm biến thì phải ra `--`.

Nếu đang đặt tay mà vẫn `--`:

- Đặt ngón tay ổn định, **không ép quá mạnh**, tránh ánh sáng ngoài chiếu vào.
- Kiểm tra 3.3 V, GND, `SDA=PC07`, `SCL=PC05` và điện trở kéo lên ~4.7 kΩ.
- **Tách dây buzzer khỏi dây I2C.** Đây là nguyên nhân phổ biến nhất.
- Đặt tay lại cần khoảng **2 giây** để cửa sổ lọc gom đủ 8/12 mẫu hợp lệ.

### Tốc độ giọt hiện 0 hoặc nhảy loạn

- Kiểm tra `PD02` và căn lại vị trí laser/photodiode so với buồng nhỏ giọt.
- Số hiển thị lấy median 7 khoảng gần nhất, nên sau khi chỉnh phải chờ vài giọt
  mới ổn định lại.
- Chưa treo bịch hoặc chưa có giọt nào thì `drops_signal` bằng `false` — đúng.

### Cân sai hoặc âm

Trừ bì lại: nhấn nút `PB00`, hoặc gửi lệnh tare từ web. Lúc trừ bì phải để
**móc treo trống**.

---

## Lệnh từ web không tác động tới G26

Nút đặt tốc độ giọt và nút fake là **lệnh hai chiều**:
`web → server → TCP → gateway → MQTT → zigbee2mqtt → ZCL write → G26`.

Toast `enabled` trên web **chỉ xác nhận server đã nhận yêu cầu** — nó không
chứng minh gì về phần còn lại của chuỗi.

Cách xác nhận thật sự:

- Đặt tốc độ giọt → trường `target_drops_per_min` trên dashboard phải đổi.
- Bấm nút fake → trường **`AI test HR/SpO2`** phải đổi. Nếu `Real HR/SpO2` đổi
  mà `AI test` không đổi thì lệnh chưa tới chip.

Nếu chưa tới, kiểm tra: gateway TCP còn kết nối, MQTT còn sống, thiết bị Zigbee
online, và converter đúng phiên bản.

---

## Firmware

### Không nạp được / không thấy board

- Cắm nhiều kit thì phải chỉ đích danh:
  `.\tools\flash_firmware.ps1 -SerialNo <DEBUG_ADAPTER_SERIAL>`.
- Board vừa bị erase toàn chip thì phải nạp **bootloader trước**:
  `.\tools\flash_bootloader_and_app.ps1`.

### Vừa nạp xong, chip mất hết baseline và tare

Đúng như vậy — nạp lại chip giữa ca truyền là mất baseline nhịp tim, mất tare
cân và mất kết nối Zigbee vài chục giây. Ngưỡng và mục tiêu thì được lưu NVM3
nên sống sót; baseline học từ bệnh nhân thì không. Tránh nạp lại chip giữa ca.

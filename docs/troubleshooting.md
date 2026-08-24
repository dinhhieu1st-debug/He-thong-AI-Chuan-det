# Lỗi thường gặp

### `SocketException (10048)` hoặc file HisServer bị khóa

```powershell
Get-NetTCPConnection -State Listen -LocalPort 5000,5194 |
  Select-Object LocalPort,OwningProcess
Stop-Process -Id <PID>
```

### Đăng nhập báo `Sign-in failed`

- Kiểm tra MySQL đang chạy.
- Kiểm tra schema và migration tài khoản đã được nạp.
- Chạy `dotnet user-secrets list` trong đúng thư mục `HisServer` (`software/server/src/HisServer`).
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

### Có một thư mục lạ (ví dụ `demo1/`) xuất hiện ở gốc repo, `git status` báo untracked

Đây thường là tàn dư của việc chạy `dotnet run` bằng một đường dẫn cũ (trước khi
`server/` được đổi thành `software/server/`). Kiểm tra không còn tiến trình
`dotnet`/`HisServer` nào chạy từ thư mục đó (`ps aux | grep dotnet`), dừng nó,
rồi xoá thư mục và tiếp tục làm việc từ `software/server/`.

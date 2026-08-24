# 6. Vận hành hằng ngày

Dành cho hệ thống **đã cài xong** theo [`05-cai-dat.md`](05-cai-dat.md).

---

## 6.1. Thứ tự khởi động

Thứ tự này quan trọng: mỗi bước cần bước trước đó đã sẵn sàng. Bản dưới đây
theo cách **reverse tunnel** (cách B).

| # | Ở đâu | Làm gì |
|---|---|---|
| 1 | Phần cứng | Cấp nguồn G26, cảm biến và Zigbee coordinator |
| 2 | Windows | Khởi động MySQL (`Start-Service MySQL84`) |
| 3 | Windows | `dotnet run --launch-profile http` trong `He-thong-AI-Chuan-det-server\server\src\HisServer` |
| 4 | Windows | `plink -ssh -N -R 5000:127.0.0.1:5000 iotchallenge@<PI_IP>` — giữ cửa sổ mở |
| 5 | Pi | `bash run.sh 127.0.0.1`, hoặc kiểm tra `systemctl status smart-iv-stack` |
| 6 | Trình duyệt | `http://<PI_IP>:8080` — xác nhận `SmartIV-Sensor` đang online |
| 7 | Trình duyệt | `http://localhost:5194` — đăng nhập HIS |
| 8 | Giường | Chờ G26 trừ bì xong (10 giây, **không treo bịch**), rồi treo bịch |
| 9 | Web | Đặt tốc độ giọt mục tiêu và xác nhận |
| 10 | Chờ | Đủ **20 mẫu giọt** và **64 mẫu sinh hiệu** thì cảnh báo mới bật |

Trước khi đủ mẫu, dashboard vẫn chạy và vẫn hiện số — nhưng `alerts_armed` là
`false` và sẽ **không có báo động nào**. Đó là hành vi đúng, không phải lỗi.

---

## 6.2. Kiểm tra kết nối

### Trên Windows

```powershell
Get-NetTCPConnection -State Listen -LocalPort 3306,5000,5194
Get-NetTCPConnection -State Established -LocalPort 5000
Get-Process mysqld,HisServer,plink
```

### Trên Pi

```bash
ss -tn | grep ':5000'
tail -n 50 ~/pi-aarch64/logs/gateway.log
tail -n 50 ~/pi-aarch64/logs/zigbee2mqtt.log
```

Log gateway khoẻ mạnh phải có **cả hai dòng**:

```text
Da ket noi MQTT
Da ket noi HIS server <IP>:5000
```

### Hệ thống nối đúng khi

- MySQL nghe `3306`;
- HIS nghe **cả** `5000` (TCP nhận dữ liệu) **và** `5194` (web);
- TCP `5000` ở trạng thái `ESTABLISHED`, không phải chỉ `LISTEN`;
- Zigbee2MQTT liên tục nhận payload mới từ `SmartIV-Sensor`;
- `BED-01` trên dashboard không còn `OFFLINE`.

---

## 6.3. Cổng dùng trong hệ thống

| Cổng | Ở đâu | Việc |
|---|---|---|
| `1885` | Pi | MQTT broker nội bộ (mosquitto) |
| `8080` | Pi | Web UI của Zigbee2MQTT |
| `3306` | Windows | MySQL |
| `5000` | Windows | **TCP nhận dữ liệu giường** — không phải web |
| `5194` | Windows | Web UI HIS |

`5000` và `5194` là hai thứ khác nhau. Đừng cho web nghe trùng `5000`.

---

## 6.4. Khi địa chỉ IP thay đổi

Máy dev dùng Wi-Fi DHCP nên IP đổi thường xuyên, và mỗi lần đổi là dashboard
**lặng lẽ ngừng cập nhật** chứ không báo lỗi.

- Lấy IP hiện tại: `ipconfig` trên Windows.
- Truyền IP đó cho `run.sh` trên Pi: `bash run.sh <WINDOWS_IP>`.
- Hoặc sửa `HIS_SERVER_HOST` trong `config/gateway.conf` trên Pi.

Dùng **reverse tunnel** thì Pi luôn gọi `127.0.0.1` nên không phải sửa gì khi
IP đổi — đây là lý do nhóm chọn cách đó.

---

## 6.5. Thay converter Zigbee2MQTT

Converter là file **duy nhất** biết cách dịch cluster `0xFC01` thành JSON. Sửa
attribute trong firmware mà quên đẩy converter mới sang Pi thì trường dữ liệu
đó biến mất, không có thông báo lỗi nào.

```powershell
scp .\gateway-pi\zigbee2mqtt\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/zigbee2mqtt/data/external_converters/
```

Sau đó `Ctrl+C` dừng `run.sh` cũ rồi chạy lại. Mở thiết bị trong Zigbee2MQTT và
kiểm tra bốn khoá này có về: `final_alert_level`, `drop_training_samples`,
`vitals_training_samples`, `alerts_armed`.

Thường **không cần pair lại** G26.

---

Gặp lỗi thì sang [`07-su-co.md`](07-su-co.md).

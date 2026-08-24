# Gateway Pi — Trạm 3 của hệ thống Smart IV

Cầu nối giữa mạng Zigbee và HIS Server. Gồm **hai phần chạy cùng nhau trên
Raspberry Pi**:

| Phần | File | Việc |
|---|---|---|
| Converter Zigbee2MQTT | `zigbee2mqtt/zigbee2mqtt_smart_iv_converter.js` | Dịch cluster `0xFC01` của G26 thành JSON có tên trường đọc được |
| Gateway C | `src/` | Nhận JSON qua MQTT, gửi lên HIS bằng TCP, và chuyển lệnh từ HIS ngược về MQTT |

Dự án **không còn** web dashboard hay web OTA cục bộ. Web frontend của
Zigbee2MQTT vẫn có ở cổng `8080`.

> **Code trong thư mục này chạy trên Pi, không phải trên máy bạn.** Sửa xong
> phải đẩy sang Pi và khởi động lại stack thì mới có tác dụng.

Tổng quan hệ thống: [`../docs/01-kien-truc.md`](../docs/01-kien-truc.md).
Bảng attribute mà converter dịch: [`../docs/03-zigbee.md`](../docs/03-zigbee.md).

---

## Cấu trúc

```text
src/                      Mã nguồn C của gateway
zigbee2mqtt/              Converter cho custom cluster Smart IV
config/                   Cấu hình dùng chung
scripts/windows/          Launcher và script build cho Windows
bin/windows/              gateway.exe (sinh ra khi build, không commit)
deploy/pi-aarch64/        Gói offline đầy đủ cho Raspberry Pi ARM64 (không commit)
```

`bin/` và `deploy/` không nằm trong Git vì các binary ARM64 (Node.js,
Mosquitto, Zigbee2MQTT) quá lớn. Lấy gói `pi-aarch64` từ bản phát hành hoặc chép
từ một Pi đã dựng sẵn.

---

## Converter Zigbee2MQTT

Đây là file **duy nhất** biết cách dịch cluster `0xFC01`. Nó và
`firmware/app.c` là một cặp: thêm hoặc đổi attribute ở một bên mà quên bên kia
thì trường dữ liệu đó **im lặng biến mất**, không có thông báo lỗi nào.

Đẩy sang Pi:

```powershell
scp .\gateway-pi\zigbee2mqtt\zigbee2mqtt_smart_iv_converter.js `
  iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/zigbee2mqtt/data/external_converters/
```

Rồi dừng `run.sh` cũ bằng `Ctrl+C` và chạy lại. Mở thiết bị trong Zigbee2MQTT và
xác nhận bốn khoá này có về: `final_alert_level`, `drop_training_samples`,
`vitals_training_samples`, `alerts_armed`.

Thường **không cần pair lại** G26.

---

## Chạy trên Raspberry Pi (aarch64)

Gói `deploy/pi-aarch64` đã gom sẵn mọi thứ — **không cần cài hay biên dịch gì
trên Pi**:

- Gateway đã build cho Linux ARM64
- Mosquitto broker ARM64 liên kết tĩnh
- Node.js 22 ARM64
- Zigbee2MQTT 2.13.0 đã build, kèm toàn bộ `node_modules` production và native
  serial binding ARM64
- Cấu hình MQTT, Zigbee2MQTT và script khởi động/dừng cả stack

### Cấu hình

Địa chỉ HIS Server, trong `deploy/pi-aarch64/config/gateway.conf`:

```ini
HIS_SERVER_HOST=127.0.0.1
```

Dùng `127.0.0.1` khi nối bằng SSH reverse tunnel. Nếu server nằm trên máy khác
trong cùng LAN thì thay bằng IP LAN của máy đó. Cũng có thể không sửa file mà
truyền IP trực tiếp khi chạy.

Cổng coordinator, trong `deploy/pi-aarch64/config/runtime.conf`:

```ini
COORDINATOR_PORT=/dev/ttyACM0
```

Launcher tự dò `/dev/serial/by-id/*`, `/dev/ttyACM*`, `/dev/ttyUSB*` nên thường
không phải khai bằng tay.

### Chạy

```bash
tar -xzf gateway-pi-aarch64.tar.gz
cd pi-aarch64
bash run.sh
```

Hoặc truyền thẳng địa chỉ HIS Server:

```bash
bash run.sh 192.168.1.20
```

IP truyền vào được dùng cho **cả** kết nối TCP tới HIS **và** OTA index. Ví dụ
lệnh trên sẽ cấu hình Zigbee2MQTT đọc OTA tại
`http://192.168.1.20:5194/api/ota/index.json`. Muốn đổi cổng hoặc đặt URL đầy đủ
thì sửa `config/runtime.conf`.

Launcher lần lượt kiểm tra kiến trúc, file runtime, Node và coordinator; sau đó
khởi động Mosquitto (cổng `1885`), Zigbee2MQTT và gateway. `Ctrl+C` để dừng toàn
bộ. Web Zigbee2MQTT ở `http://<PI_IP>:8080`.

Nếu Node không chạy được, coordinator không tồn tại, thiếu quyền serial, hoặc
một tiến trình chết bất thường, launcher sẽ báo lỗi và ghi chi tiết vào
`deploy/pi-aarch64/logs/`.

### Quyền serial

```bash
sudo usermod -aG dialout iotchallenge
```

**Phải đăng xuất rồi đăng nhập lại** thì group mới có hiệu lực.

### Tự chạy khi khởi động

Xem [`../docs/05-cai-dat.md`](../docs/05-cai-dat.md) bước 7 (systemd service).

> Không vừa bật service vừa chạy tay `bash run.sh` — hai tiến trình sẽ tranh
> nhau cổng coordinator và cả hai cùng hỏng.

---

## Chạy trên Windows (chỉ để thử gateway)

```powershell
.\run_windows.cmd
```

Ghi đè IP HIS Server ngay khi chạy:

```powershell
.\run_windows.cmd 192.168.1.20
```

---

## Xem log

```bash
tail -n 50 ~/pi-aarch64/logs/gateway.log
tail -n 50 ~/pi-aarch64/logs/zigbee2mqtt.log
```

Gateway khoẻ mạnh có **cả hai dòng**:

```text
Da ket noi MQTT
Da ket noi HIS server <IP>:5000
```

Thiếu dòng nào thì tra [`../docs/07-su-co.md`](../docs/07-su-co.md).

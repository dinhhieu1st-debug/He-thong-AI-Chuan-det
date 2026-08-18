# Cập nhật firmware từ xa (OTA)

Trang **kỹ thuật viên** có thể kiểm tra và nạp firmware mới cho thiết bị Zigbee
mà không cần cầm dây tới tận giường.

---

## 1. Đường đi của một lệnh

```
Trình duyệt (kythuat)
   │  POST /api/devices/{id}/ota/check   (quyền ManageDevices)
   ▼
HIS Server ──► gateway trên Pi  {"cmd":"ota_check","deviceId":…}
                   │
                   ▼  MQTT zigbee2mqtt/bridge/request/device/ota_update/check
              zigbee2mqtt ──► thiết bị (cluster OTA)
                   │
                   ▼  bridge/response/device/ota_update/*  +  bản tin thiết bị
              gateway ──► server  {"type":"ota_status", …}
                   │
                   ▼  SignalR OtaStatusChanged
              thanh tiến độ trên trang kỹ thuật
```

Trạng thái OTA giữ **trong bộ nhớ server**, cố ý. Một lần nạp kéo dài vài phút và
không sống sót qua lần khởi động lại server; hiện lại “đang nạp 40%” lấy từ CSDL
cho một tiến trình đã chết còn tệ hơn là không hiện gì.

Trang vừa mở thì gọi `GET /api/devices/ota` lấy toàn bộ trạng thái đang có, nên
tải lại trang giữa lúc đang nạp vẫn thấy đúng — và **không** mọc lại nút Cập nhật.

---

## 2. Những gì giao diện cố tình không cho làm

| Tình huống | Giao diện | Lý do |
|---|---|---|
| Chưa kiểm tra | chỉ có nút *Kiểm tra bản mới* | chưa biết có bản nào thì mời cập nhật là mời mù |
| Đang ở bản mới nhất | không có nút Cập nhật | |
| **Đang nạp** | **ẩn cả hai nút** | bấm Cập nhật lần hai = hai luồng ảnh chồng nhau = **nửa firmware trong máy đầu giường** |
| Đang nạp, chưa có % | ghi “đang chuẩn bị…” | thanh 0% không phân biệt được với thanh treo |
| Thất bại | hiện nguyên văn lý do + cho thử lại | |

Server cũng chặn ở tầng API: `POST …/ota/update` trả **409** nếu thiết bị đang
nạp dở, **503** nếu không gateway nào đang kết nối.

Trong lúc nạp luôn hiện dòng **“đừng rút nguồn thiết bị.”**

---

## 3. Cấu hình trên Pi

`~/zigbee2mqtt/data/configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: smart_iv_ota_index.json
  image_block_response_delay: 50
  default_maximum_data_size: 63
```

`63` là **trần cứng**, không phải con số chỉnh cho vui: firmware xG26 bỏ qua khối
lớn hơn, và OTA sẽ **đứng im không báo lỗi**.

Index hiện là `[]` — xem [`../gateway/ota/README.md`](../gateway/ota/README.md)
để biết vì sao rỗng lại là lựa chọn đúng lúc này, và luật khớp **mã nhà sản
xuất** trước khi thêm bất kỳ file `.ota` nào.

---

## 4. Trạng thái hiện tại: chưa nạp được cho máy đầu giường

Đã kiểm chứng cả chuỗi bằng thiết bị thật. Kết quả trả về:

```
state:   Failed
message: Failed to check if OTA update available for 'SmartIV-Sensor'
         (No endpoint found with OTA cluster support for 0x64028ffffe641802)
```

Đây là **câu trả lời đúng**, không phải lỗi hệ thống: firmware `smart-iv-monitor`
chưa có thành phần OTA client (`grep -i ota smart-iv-monitor.slcp` không ra gì).

Muốn dùng được, thiết bị cần **cả hai**:

1. **Gecko Bootloader** (đã dựng sẵn ở
   `bootloader-storage-internal-single-3200k/artifact/`),
2. `zigbee_ota_client` + `zigbee_ota_bootload` trong `smart-iv-monitor.slcp`.

Thêm chúng làm **đổi bố cục bộ nhớ** — ứng dụng phải dời lên trên bootloader —
nên lần đầu **bắt buộc nạp qua dây một lần**. Đó là đánh đổi cần người quyết:
một lần cầm dây bây giờ, đổi lấy việc không phải cầm dây về sau.

---

## 5. Kiểm thử

```bash
node tools/ota_render_check.js              # 12 phép thử giao diện
dotnet run --project server/tests/EvaluatorTests   # gồm 11 phép thử OTA
```

Các phép thử giao diện dựng thẳng HTML của thẻ thiết bị từ `devices.js`, nên
chúng bắt được đúng thứ nguy hiểm nhất: nút Cập nhật xuất hiện trong lúc đang nạp.

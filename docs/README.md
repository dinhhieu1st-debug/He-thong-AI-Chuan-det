# Mục lục tài liệu

Xếp theo **việc bạn đang định làm**, không theo tên file.

## Tôi mới vào dự án

Đọc theo thứ tự này:

1. [`../README.md`](../README.md) — hệ thống là gì, gồm những gì
2. [`01-kien-truc.md`](01-kien-truc.md) — bốn trạm, luồng dữ liệu, và **vì sao**
   thiết kế như vậy
3. [`04-canh-bao.md`](04-canh-bao.md) — phần lõi: chip quyết định cảnh báo thế nào

## Tôi cần dựng lại hệ thống từ máy trắng

[`05-cai-dat.md`](05-cai-dat.md) — 8 bước, làm tuần tự từ đầu tới hết.

## Tôi đã cài rồi, giờ muốn chạy

- [`06-van-hanh.md`](06-van-hanh.md) — thứ tự khởi động, cách kiểm tra kết nối,
  bảng cổng
- [`07-su-co.md`](07-su-co.md) — lỗi thường gặp, xếp theo nơi triệu chứng xuất hiện

## Tôi định sửa code

| Sắp sửa gì | Đọc trước |
|---|---|
| `firmware/` — cảm biến, lọc tín hiệu | [`02-phan-cung.md`](02-phan-cung.md), [`04-canh-bao.md`](04-canh-bao.md) |
| `firmware/` — thêm/đổi attribute Zigbee | [`03-zigbee.md`](03-zigbee.md) — **phải sửa converter cùng lúc** |
| `firmware/` — ngưỡng, logic cảnh báo | [`04-canh-bao.md`](04-canh-bao.md) |
| `gateway-pi/` | [`../gateway-pi/README.md`](../gateway-pi/README.md) — code này chạy trên Pi, không phải trên máy bạn |
| HIS Server | [`../He-thong-AI-Chuan-det-server/README.md`](../He-thong-AI-Chuan-det-server/README.md) |
| API, schema, biến cấu hình server | [`../He-thong-AI-Chuan-det-server/server/README.md`](../He-thong-AI-Chuan-det-server/server/README.md) |

## Danh sách đầy đủ

| File | Nội dung |
|---|---|
| [`01-kien-truc.md`](01-kien-truc.md) | Bốn trạm, luồng dữ liệu, nguyên tắc thiết kế |
| [`02-phan-cung.md`](02-phan-cung.md) | Sơ đồ chân BRD2709A, lưu ý đấu nối, cảm biến |
| [`03-zigbee.md`](03-zigbee.md) | Cluster `0xFC01`, bảng attribute đầy đủ, bitmap, định dạng TCP |
| [`04-canh-bao.md`](04-canh-bao.md) | Lọc HR/SpO2, nhánh nhỏ giọt, ghép mức, LED/buzzer, chế độ test |
| [`05-cai-dat.md`](05-cai-dat.md) | Cài từ đầu: Windows, firmware, MySQL, server, Pi, systemd |
| [`06-van-hanh.md`](06-van-hanh.md) | Thứ tự khởi động, kiểm tra kết nối, cổng, đổi IP, thay converter |
| [`07-su-co.md`](07-su-co.md) | Lỗi thường gặp và cách lần ngược chuỗi |

## Tài liệu nằm ngoài `docs/`

| Đường dẫn | Nội dung |
|---|---|
| [`../He-thong-AI-Chuan-det-server/README.md`](../He-thong-AI-Chuan-det-server/README.md) | Tổng quan HIS Server |
| [`../He-thong-AI-Chuan-det-server/server/README.md`](../He-thong-AI-Chuan-det-server/server/README.md) | Chi tiết kỹ thuật server: cấu trúc, cấu hình, wire format |
| [`../gateway-pi/README.md`](../gateway-pi/README.md) | Gateway MQTT ↔ TCP và cách triển khai lên Pi |
| [`../host_ai/README.md`](../host_ai/README.md) | **Nhánh nghiên cứu cũ (ESP8266)** — không thuộc hệ thống G26 đang chạy |

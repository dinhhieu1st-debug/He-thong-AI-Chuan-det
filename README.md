# Hệ thống AI Chuẩn Đét – Smart IV Monitor

Đây là đồ án sinh viên xây dựng một hệ thống giám sát bệnh nhân đang truyền dịch. Mục tiêu của nhóm là gom dữ liệu sinh hiệu, tốc độ nhỏ giọt và khối lượng bịch dịch về một giao diện web để y tá dễ theo dõi, đồng thời phát cảnh báo ngay tại giường bằng LED và buzzer.

> **Lưu ý:** đây là mô hình nghiên cứu và trình diễn, chưa phải thiết bị y tế đã được kiểm định. Không dùng hệ thống này để thay thế thiết bị theo dõi lâm sàng.

## Hệ thống làm được gì?

- Đo nhịp tim (HR) và SpO2.
- Đếm giọt, tính khoảng cách giữa hai giọt và tốc độ giọt/phút.
- Đo khối lượng bịch dịch bằng loadcell.
- Học mốc sinh hiệu và mốc nhỏ giọt trước khi bật cảnh báo.
- Chạy AI chuỗi thời gian trực tiếp trên G26.
- Ghép cảnh báo sinh hiệu và nhỏ giọt thành ba mức.
- Hiển thị số liệu trên OLED 0.96 inch và web HIS Server.
- Cho phép đặt tốc độ giọt, trừ bì và bật dữ liệu fake từ web.
- Truyền hai chiều qua Zigbee; G26 không tự tạo JSON.

Chi tiết luồng dữ liệu, giao thức Zigbee và logic cảnh báo: xem
[`docs/architecture.md`](docs/architecture.md).

## Cấu trúc thư mục

Repo gồm hai nhóm thư mục: khung dự án firmware do Simplicity Studio (SLC)
sinh ra — cố định vị trí, không di chuyển được — và phần mềm do nhóm tự viết.

### Bắt buộc ở gốc repo (SLC/firmware toolchain)

| Thư mục / file | Nội dung |
|---|---|
| `firmware/` | Firmware G26: cảm biến, OLED, AI, cảnh báo và Zigbee (đường dẫn được khai trong `smart-iv-monitor.slcp`) |
| `firmware/models/` | Model TFLite Micro đã nhúng vào firmware |
| `firmware/PIN_MAP.md` | Chi tiết sơ đồ chân G26 |
| `main.c`, `*.slcp`, `*.slps`, `*.pintool` | Entry point và project file Simplicity Studio |
| `autogen/`, `config/`, `cmake_gcc/`, `bootloader/` | Sinh ra/điều khiển bởi SLC — build lại bằng `tools/build_firmware.ps1` |
| `simplicity_sdk_2025.12.3/`, `aiml_2.2.2/` | SDK và thư viện AI vendor, không sửa trực tiếp |
| `tools/` | Script build, nạp firmware, firewall và khởi động Pi (gọi tương đối từ gốc repo) |

### Phần mềm của nhóm

| Thư mục | Nội dung |
|---|---|
| `software/gateway-pi/` | Source gateway MQTT ↔ TCP và cấu hình triển khai trên Pi |
| `software/server/` | HIS Server .NET 8, giao diện web, API và database |
| `software/host_ai/` | Dataset, mã huấn luyện và tài liệu AI nhỏ giọt |

### Tài liệu

Mọi tài liệu cấp hệ thống nằm trong [`docs/`](docs/README.md) — xem mục lục ở
đó để tới đúng file (kiến trúc, hướng dẫn cài đặt, lỗi thường gặp, tích hợp
firmware↔HIS).

## Bắt đầu từ đâu?

- Cài đặt hệ thống lần đầu: [`docs/setup-guide.md`](docs/setup-guide.md)
- Hiểu kiến trúc/giao thức: [`docs/architecture.md`](docs/architecture.md)
- Gặp lỗi: [`docs/troubleshooting.md`](docs/troubleshooting.md)
- Cập nhật alert contract firmware↔HIS hoặc deploy lại converter Pi: [`docs/system-integration.md`](docs/system-integration.md)
- Build kiểm tra trước khi nộp: xem cuối [`docs/setup-guide.md`](docs/setup-guide.md#build-kiểm-tra-trước-khi-nộp)

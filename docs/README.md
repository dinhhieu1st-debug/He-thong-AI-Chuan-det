# Mục lục tài liệu

Tài liệu cấp hệ thống (cross-component) nằm ở đây. Tài liệu riêng của từng
thành phần nằm ngay cạnh code của thành phần đó (xem cột "Vị trí").

| Tài liệu | Nội dung | Vị trí |
|---|---|---|
| [architecture.md](architecture.md) | Luồng dữ liệu, giao thức Zigbee, sơ đồ chân, logic xử lý HR/SpO2 và cảnh báo | `docs/` |
| [setup-guide.md](setup-guide.md) | Cài đặt toàn bộ hệ thống từ đầu, theo đúng thứ tự đã chạy thực tế | `docs/` |
| [troubleshooting.md](troubleshooting.md) | Lỗi thường gặp và cách khắc phục | `docs/` |
| [system-integration.md](system-integration.md) | Alert contract firmware ↔ HIS, cách deploy converter, cách chạy lại HIS/Pi sau khi đổi schema | `docs/` |
| PIN_MAP.md | Chi tiết đầy đủ sơ đồ chân G26 | `firmware/PIN_MAP.md` |
| README.md (gateway) | Build và chạy gateway TCP↔MQTT trên Pi | `software/gateway-pi/README.md` |
| README.md (server) | Kiến trúc HIS Server .NET 8 | `software/server/README.md` |
| README.md (host_ai) | Huấn luyện và triển khai model AI | `software/host_ai/README.md` |
| DATASET_CARD.md, DATASET_PLAN.md | Dataset dùng để huấn luyện AI nhỏ giọt/sinh hiệu | `software/host_ai/` |
| PI_DEPLOYMENT.md | Triển khai runtime AI trên Raspberry Pi | `software/host_ai/deployment/` |

Bắt đầu từ [README.md ở gốc repo](../README.md) nếu bạn mới vào dự án.

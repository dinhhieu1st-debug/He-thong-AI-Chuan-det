# Hệ thống AI Chuẩn Đét — Smart IV Monitor

Hệ thống giám sát bệnh nhân đang truyền dịch. Một thiết bị gắn ở đầu giường đo
nhịp tim, SpO2, tốc độ nhỏ giọt và khối lượng bịch dịch, chạy AI **ngay trên
chip** để quyết định mức cảnh báo, rồi gửi về giao diện web cho y tá qua Zigbee.
Khi có sự cố, thiết bị báo ở **cả hai nơi**: LED và buzzer tại giường, và
dashboard ở trạm điều dưỡng.

> **Đây là mô hình nghiên cứu và trình diễn, chưa phải thiết bị y tế được kiểm
> định.** Không dùng để thay thế thiết bị theo dõi lâm sàng.

**[→ Mục lục toàn bộ tài liệu](docs/README.md)** ·
**[→ Cài đặt từ đầu](docs/05-cai-dat.md)** ·
**[→ Vận hành hằng ngày](docs/06-van-hanh.md)**

---

## Hệ thống làm được gì

- Đo nhịp tim (HR) và SpO2, có lọc nhiễu nhiều tầng.
- Đếm giọt, đo khoảng cách giữa hai giọt và tính tốc độ giọt/phút.
- Đo khối lượng bịch dịch bằng loadcell, tự trừ bì khi khởi động.
- **Học mốc riêng cho từng bệnh nhân** trước khi bật cảnh báo.
- Chạy AI chuỗi thời gian trực tiếp trên chip G26, không cần mạng.
- Ghép cảnh báo sinh hiệu và cảnh báo nhỏ giọt thành ba mức.
- Hiển thị trên OLED 0.96 inch tại giường và trên web HIS.
- Cho phép đặt tốc độ giọt, trừ bì và bật chế độ test **từ web**.
- Truyền hai chiều qua Zigbee.

## Hệ thống gồm những gì

```text
Cam bien (HR/SpO2, giot, can)
        |
        v
  EFR32xG26  ---Zigbee--->  Coordinator  ---USB--->  Raspberry Pi
                                                          |
                                                     TCP :5000
                                                          v
                                                     HIS Server
                                                  MySQL + web :5194
```

Bốn trạm chạy độc lập, nối nhau bằng giao thức chuẩn — sửa một trạm không phải
đụng ba trạm kia. Chi tiết ở [`docs/01-kien-truc.md`](docs/01-kien-truc.md).

## Cấu trúc thư mục

| Thư mục | Nội dung |
|---|---|
| `firmware/` | **Trạm 1** — mã chạy trên chip G26: cảm biến, AI, cảnh báo, OLED, Zigbee |
| `firmware/models/` | Model TFLite Micro đã nhúng vào firmware |
| `gateway-pi/` | **Trạm 3** — gateway MQTT ↔ TCP (C) và converter Zigbee2MQTT, đều chạy trên Pi |
| `He-thong-AI-Chuan-det-server/` | **Trạm 4** — HIS Server .NET 8: API, web UI, database |
| `docs/` | Toàn bộ tài liệu hệ thống |
| `tools/` | Script build, nạp firmware, firewall và khởi động stack trên Pi |
| `host_ai/` | Nhánh nghiên cứu AI cũ trên ESP8266 — **không** thuộc hệ thống đang chạy |

Bốn thứ sau **bắt buộc nằm ở gốc repo**, không gom vào `firmware/` được:
`smart-iv-monitor.slcp`, `config/`, `autogen/`, `cmake_gcc/` và `main.c`.
Simplicity Studio và `slc` coi gốc repo là gốc project và tự sinh các thư mục đó
ở đúng vị trí; đẩy xuống thư mục con sẽ làm hỏng cả việc mở project trong Studio
lẫn lệnh `slc generate`. Thêm file `.c` mới vào `firmware/` thì **phải khai vào
`smart-iv-monitor.slcp`** rồi chạy lại `slc generate`.

`simplicity_sdk_*/`, `aiml_*/` và `bootloader/` là bản copy SDK, không phải mã
của nhóm.

## Chạy nhanh

Đã cài xong rồi thì mỗi lần dùng chỉ cần:

```powershell
# Windows
Start-Service MySQL84
cd .\He-thong-AI-Chuan-det-server\server\src\HisServer
dotnet run --launch-profile http
```

```powershell
# Windows, cửa sổ riêng - reverse tunnel tới Pi
plink -ssh -N -R 5000:127.0.0.1:5000 iotchallenge@<PI_IP>
```

```bash
# Pi
cd ~/pi-aarch64 && bash run.sh 127.0.0.1
```

Web HIS ở `http://localhost:5194`, web Zigbee2MQTT ở `http://<PI_IP>:8080`.

Thứ tự đầy đủ và cách kiểm tra: [`docs/06-van-hanh.md`](docs/06-van-hanh.md).

## Build và kiểm thử

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
dotnet build .\He-thong-AI-Chuan-det-server\server\HisServer.sln
dotnet run --project .\He-thong-AI-Chuan-det-server\server\tests\EvaluatorTests\EvaluatorTests.csproj
```

## Ba điều cần biết trước khi đọc code

**Chip là nơi duy nhất quyết định mức cảnh báo.** Server hiển thị và lưu, không
tính lại. Đừng thêm logic đánh giá mức ở phía server.

**Cảnh báo chỉ bật sau khi học xong** 20 mẫu giọt và 64 mẫu sinh hiệu. Trước đó
dashboard vẫn chạy nhưng `alerts_armed` là `false` — đó là hành vi đúng.

**"Chưa có dữ liệu" không phải là 0.** Kênh mất tín hiệu hiện `--`, giá trị chưa
ước lượng được gửi `0xFFFF` và hiển thị `null`. Báo một con số sai một cách tự
tin là thứ khiến y tá thôi tin cái máy.

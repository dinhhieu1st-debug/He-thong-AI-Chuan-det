# Build bằng Docker

Mục đích: **một lệnh cho ra kết quả giống hệt nhau trên Windows và Linux.**
Không phải cài .NET SDK hay `libmosquitto-dev` lên máy, và không còn cảnh
"máy tôi build được mà máy anh thì không".

Cần: Docker Desktop (Windows) hoặc Docker Engine (Linux). Không cần gì thêm.

## Ba lệnh

Chạy từ **thư mục gốc repository**:

```bash
# HIS Server: build solution + chạy EvaluatorTests
docker compose -f build/docker-compose.build.yml run --rm server

# Gateway cho máy x86 (để thử tại chỗ)
docker compose -f build/docker-compose.build.yml run --rm gateway

# Gateway cho Raspberry Pi ARM64
docker compose -f build/docker-compose.build.yml run --rm gateway-arm64
```

Trên PowerShell cú pháp y hệt, không đổi dấu gạch chéo.

Lần đầu mỗi lệnh mất vài phút để kéo image. Các lần sau nhanh vì cache NuGet
nằm trong Docker volume riêng (`nuget`), không nằm trong repo.

## Kết quả build ra đâu

| Lệnh | File sinh ra |
|---|---|
| `server` | `He-thong-AI-Chuan-det-server/server/src/HisServer/bin/` |
| `gateway` | `gateway-pi/bin/linux/gateway` (x86-64) |
| `gateway-arm64` | `gateway-pi/bin/linux/gateway` (ARM64, chép thẳng sang Pi được) |

Cả hai đường dẫn đều đã nằm trong `.gitignore`, build không làm bẩn `git status`.

> `gateway` và `gateway-arm64` ghi đè lên **cùng một** file. Build ARM64 rồi
> chạy tại chỗ trên máy x86 sẽ báo `Exec format error` — đó là đúng, không phải
> lỗi. Build lại bằng `gateway` để có bản chạy được trên máy bạn.

## Build ARM64 trên máy x86

Cần bật qemu một lần cho mỗi lần khởi động máy:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

Docker Desktop trên Windows đã bật sẵn, không cần chạy lệnh trên.

## Vì sao build thất bại

**Gateway**: Dockerfile thêm `-Werror`, nên **mọi cảnh báo trình biên dịch đều
làm hỏng build**. Cố ý như vậy: cảnh báo phải chặn CI, nhưng không nên chặn
người đang sửa dở trên máy mình — chạy `make` trực tiếp thì không có `-Werror`.

**Server**: `EvaluatorTests` là console app thuần, thoát với mã khác 0 khi có
kiểm thử hỏng, nên một test hỏng là hỏng cả lệnh. Không cần test runner.

Base image dùng `debian:bookworm-slim` là có chủ ý — đúng bản Raspberry Pi OS
mà gateway được triển khai, nên glibc và libmosquitto lúc build khớp với lúc
chạy trên Pi.

## Chạy thử web mà không cần Pi

Dựng cả MySQL lẫn HIS Server, không cần Pi, không cần coordinator, không cần board:

```bash
docker compose -f build/docker-compose.dev.yml up
```

Lần đầu MySQL tự nạp `schema.sql`, toàn bộ migration và `seed_demo.sql` — ra
**8 giường trên 3 phòng**, mỗi giường một thiết bị XG26.

Mở `http://localhost:5194`:

| Vai trò | Username | Password |
|---|---|---|
| Y tá | `yta` | `YTaDemo@2026` |
| Kỹ thuật | `kythuat` | `KyThuat@2026` |
| Quản trị | `admin` | `ChangeMe!123` |

Lúc này dashboard vẫn trống vì chưa có gì gửi dữ liệu. Chạy simulator ở terminal
khác — nó đóng vai Pi gateway:

```bash
python3 tools/fake_gateway.py
```

Trong lúc chạy, gõ lệnh vào simulator:

```text
unplug BED-101     ngừng gửi cho giường đó (xem nó chuyển Offline)
plug BED-101       gửi lại
announce           announce lại toàn bộ, như gateway vừa reconnect
status             simulator đang giữ trạng thái gì
quit
```

Simulator **in ra mọi lệnh nhận được từ server**, kèm nhãn `[addressed]` hay
`[GUESSED from last publisher]`. Đó là cách nhìn thấy lệnh có đi đúng thiết bị
hay không — nhãn `GUESSED` nghĩa là server quên gửi `deviceId` và trên phần cứng
thật lệnh có thể rơi vào giường khác.

> **Phải dùng từ hai giường trở lên.** Ba lỗi quan trọng nhất — toast "new device
> joined" dồn dập, lệnh tới nhầm thiết bị, và giường không phải giường báo cuối
> bị 409 — **không xuất hiện** khi chỉ có một thiết bị.

Muốn xem giường chuyển Offline nhanh hơn, hạ `Offline__ThresholdSeconds` trong
`docker-compose.dev.yml` xuống ~15 (mặc định để 90 cho khớp production).

Dọn sạch và làm lại từ đầu (xoá cả database):

```bash
docker compose -f build/docker-compose.dev.yml down -v
```

## Chạy với phần cứng thật

Không phải setup lại Pi. Toàn bộ runtime trên Pi — Node, mosquitto,
zigbee2mqtt, coordinator, pairing — giữ nguyên.

### Vì sao giai đoạn đầu không cần đụng binary trên Pi

Năm trong sáu lỗi được sửa hoàn toàn ở phía server. Riêng lệnh đích danh thiết
bị: server giờ gửi kèm `deviceId`, và bản gateway **cũ đã biết đọc trường đó**
(`json_string(line, "deviceId", ...)` trong `publish_command()` vốn đã có).

Còn lại đúng một thứ cần gateway mới: Pi cũ vẫn gửi announce thừa. Server không
còn hiện toast nữa nên bạn sẽ không thấy phiền, nhưng lưu lượng vẫn tốn. Deploy
gateway mới khi giai đoạn 1 đã chạy ổn.

### 1. Chạy server, mở ra LAN, database sạch

```bash
docker compose -f build/docker-compose.dev.yml down -v     # xoá database cũ
LOAD_DEMO_SEED=0 docker compose \
  -f build/docker-compose.dev.yml \
  -f build/docker-compose.hw.yml up
```

`LOAD_DEMO_SEED=0` bỏ qua 8 giường demo — với phần cứng thật chúng chỉ làm rối
tab Devices. `docker-compose.hw.yml` mở cổng ra LAN; mặc định stack chỉ bind
`127.0.0.1` và Pi sẽ không gọi vào được.

Lấy IP máy server: `hostname -I`. Nếu bật `ufw` thì mở cổng:

```bash
sudo ufw allow 5000/tcp comment 'HIS bed vitals ingestion'
sudo ufw allow 5194/tcp comment 'HIS web console'
```

### 2. Trỏ Pi về máy server

Trên Pi, sửa `config/gateway.conf`:

```ini
HIS_SERVER_HOST=<IP_MÁY_SERVER>
BED_ID=BED-101
BED_ROOM=ICU-1
```

`BED_ID` chỉ là giá trị dự phòng cho tới khi thiết bị được gán giường (bước 4);
sau đó server định tuyến theo thiết bị và bỏ qua nó. `BED_ROOM` **không còn ghi
đè** tên phòng admin đặt nữa.

Hoặc truyền thẳng khi chạy, không sửa file:

```bash
cd ~/pi-aarch64 && bash run.sh <IP_MÁY_SERVER>
```

Kiểm tra thông trước khi chạy: `nc -vz <IP_MÁY_SERVER> 5000`

### 3. Cập nhật converter trên Pi

Bắt buộc nếu Pi đang chạy bản cũ hơn repo này — thiếu nó thì
`ai_input_heart_rate`, `vitals_level`, `alerts_armed` không về, khối "AI test"
vô dụng (UI hiện `--`, không báo lỗi).

```bash
scp gateway-pi/zigbee2mqtt/zigbee2mqtt_smart_iv_converter.js \
  iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/zigbee2mqtt/data/external_converters/
```

Rồi `Ctrl+C` và chạy lại `run.sh`. Thường không cần pair lại.

### 4. Gán thiết bị vào giường — đừng bỏ bước này

Đây là chỗ dễ hụt nhất khi chuyển từ simulator sang thật.

Thiết bị thật có ID là **EUI64** (`0x0c4314fffe...`), không phải
`XG26-BED-101` như simulator. Lần đầu nó announce, server tạo bản ghi mới trạng
thái **Pending, chưa gán giường**. Khi đó:

- Dữ liệu đổ vào giường ghi trong `BED_ID`, không phải giường bạn muốn.
- Lệnh gửi xuống **không kèm `deviceId`** → gateway quay lại đoán theo thiết bị
  vừa publish, đúng cái lỗi vừa sửa.

Nên:

1. Đăng nhập `admin` → **Bed directory** → tạo giường thật (ví dụ `BED-101`,
   phòng `ICU-1`).
2. Đăng nhập `kythuat` → **Devices** → thấy thiết bị EUI64 đang Pending → gán
   vào giường vừa tạo.

Xong bước này định tuyến và lệnh mới chạy đúng.

### 5. Kiểm tra thông suốt

| Kiểm tra | Ở đâu | Đúng thì thấy |
|---|---|---|
| Pi nối được server | log gateway trên Pi | `Da ket noi HIS server <IP>:5000` |
| Server nhận dữ liệu | log server | không còn `Invalid bed data line` |
| Thiết bị lên mạng | tab Devices | EUI64 trạng thái Online, có link quality |
| Định tuyến đúng | dashboard | số hiện ở **đúng** giường đã gán |
| Đường phản hồi | chi tiết giường | `AI test HR/SpO2` có số, không phải `--` |
| Lệnh đích danh | đặt tốc độ giọt | `target_drops_per_min` chip báo lên đổi theo |

### 6. Deploy gateway mới (giai đoạn 2)

Chỉ làm sau khi giai đoạn 1 chạy ổn. Build bản ARM64 ngay trên máy này:

```bash
docker run --privileged --rm tonistiigi/binfmt --install arm64   # một lần
docker compose -f build/docker-compose.build.yml run --rm gateway-arm64
scp gateway-pi/bin/linux/gateway iotchallenge@<PI_IP>:/home/iotchallenge/pi-aarch64/bin/gateway
```

Dừng và chạy lại stack trên Pi. Kiểm chứng: chạy hai thiết bị 10 phút, log
gateway chỉ được có **một** dòng announce cho mỗi thiết bị.

> Sau khi build `gateway-arm64`, file `gateway-pi/bin/linux/gateway` là bản
> ARM64 — chạy trên máy x86 sẽ báo `Exec format error`. Build lại bằng
> `gateway` nếu cần bản chạy tại chỗ.

## Firmware không có ở đây

`firmware/` cần toolchain Silicon Labs, không đóng gói vào image được. Dùng:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_firmware.ps1
```

Xem [`../docs/05-cai-dat.md`](../docs/05-cai-dat.md) bước 2.

# Bàn giao đợt UI/UX — và cách cấu hình những phần chưa test được

Tài liệu này viết cho **người nhận việc tiếp theo**: bạn cầm về một nhánh đã
xong phần web, chưa từng chạm vào phần cứng thật, và cần biết **cái gì đã chắc
chắn đúng, cái gì mới chỉ đúng trên giả lập, và phải làm gì để nó đúng nốt.**

Nhánh: `ui-ux-and-ota-sync` · Đợt làm: 2026-08-21 → 2026-08-22

---

## 1. Đọc trong 30 giây

| | |
|---|---|
| **Đã làm gì** | 9 commit: sửa 3 lỗi thật, thêm 1 tính năng server, và dọn UI theo phản hồi người dùng |
| **Test bằng gì** | `tools/mock_gateway.py` + trình duyệt thật. **Không có board** trong suốt đợt này |
| **Cần ai làm tiếp** | Người có board xG26 + Raspberry Pi để xác nhận 2 mục ở [phần 4](#4-những-thứ-chưa-thể-xác-nhận-nếu-không-có-phần-cứng) |
| **Có phải nạp lại firmware không** | **Không.** Đợt này không sửa file nào trong `firmware/`, `config/zcl/`, `autogen/` |
| **Có phải deploy lại gateway/Pi không** | **Không.** Không đụng `gateway/main.c` hay converter zigbee2mqtt |

> **Vì sao hai dòng cuối quan trọng:** `docs/TRIEN_KHAI_PI.md` ghi lại một lần cả
> nhóm mất nhiều ngày vì sửa firmware + gateway trong repo rồi đinh ninh là xong,
> trong khi Pi vẫn chạy binary cũ. Đợt này **không rơi vào bẫy đó**, vì không có
> gì cần deploy — nhưng nếu bạn sửa tiếp mà có đụng hai thư mục đó thì phải đọc
> tài liệu kia trước.

---

## 2. Đã thay đổi những gì

### 2.1. Ba lỗi thật đã sửa

**Tên bệnh nhân biến mất khi đang gõ** — `wwwroot/js/beds.js`

Panel chi tiết giường tự dựng lại toàn bộ DOM khoảng 1 lần/giây để cập nhật sinh
hiệu. Cơ chế chống mất chữ đã có sẵn, nhưng nó **chỉ giữ ô đang có con trỏ**.
Nên: gõ xong Tên → Tab sang ô Mã bệnh nhân → đúng lúc đó có 1 tick → ô Tên (giờ
không còn focus) bị dựng lại từ dữ liệu server cũ, tức là **rỗng**.

Sửa bằng cách thêm hai ô bệnh nhân vào danh sách `captureFormState()` — cùng cơ
chế đã dùng sẵn cho ô "Target flow"/"Target drops" của bác sĩ, giữ **mọi** ô
trong danh sách bất kể ô nào đang focus.

**Bấm Save trên tab Devices xoá sạch dữ liệu sức khoẻ thiết bị** — `Api/DeviceEndpoints.cs`

`PUT /api/devices/{id}` dựng lại `DeviceRecord` từ đúng những trường form gửi
lên, rồi `UpsertAsync` ghi **toàn bộ** cột. Form không mang theo
`linkQuality`/`lastDataAt`/`channelsLost`/`lastSeenAt`, nên mỗi lần kỹ thuật
viên bấm Save là bốn cột đó **về `null`**.

Comment của `UpdateHealthAsync` đã ghi rõ ý định "đường ingestion không được
đụng vào trường kỹ thuật viên sở hữu" — nhưng chiều ngược lại thì chưa ai chặn.
Sửa bằng cách chép lại bốn cột từ bản ghi cũ trước khi upsert.

> Với thiết bị **đang sống** thì giá trị tự phục hồi sau ~1 giây (gói dữ liệu kế
> tiếp ghi đè lại), nên lỗi này rất dễ bị bỏ qua khi test nhanh. Với thiết bị
> **đã offline** thì nó xoá mất vĩnh viễn "lần cuối thấy nó là khi nào".

**Hash mật khẩu trong file seed không khớp mật khẩu ghi ngay trong file**
— `database/migrations/2026-08-21_seed_demo_accounts.sql`

Hai tài khoản demo `yta`/`kythuat` có `password_hash` không verify được với
`YTaDemo@2026`/`KyThuat@2026` — chính hai mật khẩu ghi trong comment phía trên.
Đã sinh lại hash đúng và **đối chiếu bằng chính `PasswordHasher.Verify` của C#**,
không chỉ bằng bản dựng lại trong Python (một lần lệch Python/C# đã tốn thời gian
debug thật trong đợt này).

### 2.2. Một tính năng mới ở server

**Thiết bị ngừng gửi dữ liệu giờ tự chuyển `Offline`** — `Domain/DeviceOfflineScanService.cs`

Trước đó trạng thái thiết bị **chỉ** được ghi bởi
`BedTcpIngestionService.UpdateDeviceHealthAsync`, mà hàm đó chỉ chạy **khi có gói
dữ liệu tới**. Rút điện một thiết bị thì không còn gì chạy đường đó nữa, và nó
**nằm ở `ONLINE` vĩnh viễn** — đúng cái lỗi mà comment của `DeviceHealthEvaluator`
nói tính năng này sinh ra để chống, chỉ là chưa đóng cho trường hợp "im hẳn".

Service mới là bản đối xứng của `OfflineScanService` (vốn đã làm việc này cho
**giường**), dùng chung `Offline:ThresholdSeconds`/`ScanIntervalSeconds`. Bỏ qua:

- `Pending` — chưa từng báo cáo lần nào, đó là "chưa kết nối", không phải "mất kết nối"
- `Gateway` — không bao giờ gửi `BedReading`, nên `LastDataAt` mãi mãi `null`;
  coi đó là offline sẽ đánh dấu **mọi** gateway ngay tick đầu tiên

> **Khoảng trống còn lại, cố ý:** sức khoẻ của gateway hiện **không có cách nào
> tự biết**. Muốn làm thì cần một tín hiệu riêng (heartbeat từ
> `zigbee2mqtt/bridge/state`), là một tính năng khác chưa dựng.

### 2.3. Dọn UI theo phản hồi

| Chỗ | Trước | Sau |
|---|---|---|
| Chi tiết thiết bị | Hiện `Battery` và `RSSI (dBm)` | Bỏ cả hai, thay bằng **Link Quality (LQI 0–255)** |
| Thêm giường (Admin) | Gõ tay tên phòng | Chọn từ danh sách phòng đã có, kèm mục **"+ New room…"** |
| Phân công trực (Admin) | Gõ tay `ICU-1` hoặc `BED-101` | Chọn từ danh sách thật, tự đổi theo Room/Bed |
| Tên bệnh nhân | Chỉ ở tab Beds và My shift | Thêm ở **Dashboard**, **banner CRITICAL**, **Alerts** |
| Tab "My shift" | Ba vai trò thấy y hệt nhau | Nurse giữ nguyên · Kỹ thuật viên đổi nhãn **"Equipment duty"** · Admin **bỏ hẳn** dòng đó |
| Vị trí tab "My shift" | Nằm giữa các tab admin | Xuống cuối, đồng nhất cho cả ba vai trò |
| Trang đăng nhập | Không có footer | Thêm badge ICTU · FPT · Silicon Labs + dòng "Developed by the ICTU team" |

**Vì sao bỏ Battery và RSSI:** grep toàn bộ đường dữ liệu (`firmware/` →
`gateway/main.c` → `Ingestion/BedDataParser.cs`) cho thấy **không nơi nào từng
gửi hai giá trị đó**. Chúng luôn hiện `--`. Trong khi `linkQuality` thì **có
thật, chạy đủ đường** từ gateway lên DB — chỉ là chưa bao giờ được hiện ra.

---

## 3. Chạy lại môi trường như lúc test

```bash
# 1. MySQL (container đã tồn tại sẵn, chỉ cần bật)
docker start his-mysql

# 2. HIS Server — cần .NET 8 SDK
cd server/src/HisServer
dotnet run --urls http://0.0.0.0:5100

# 3. Thiết bị giả lập — KHÔNG cần board, không cần Pi
python3 tools/mock_gateway.py localhost 5000
```

Mở `http://localhost:5100`. Tài khoản demo (từ file seed ở mục 2.1):

| Tài khoản | Mật khẩu | Vai trò |
|---|---|---|
| `yta` | `YTaDemo@2026` | Điều dưỡng |
| `kythuat` | `KyThuat@2026` | Kỹ thuật viên |

> Tài khoản `admin` mặc định là `ChangeMe!123` và **bắt buộc đổi ở lần đăng nhập
> đầu** (`2026-08-17_users_roles_assignments.sql`). Nếu ai đó đã đổi rồi mà không
> ai nhớ, cách khôi phục nằm ở [mục 5.3](#53-quên-mật-khẩu-admin).

**Nếu máy chưa cài .NET SDK**, chạy qua Docker được (đợt này làm vậy):

```bash
cd server
docker run --rm --network host -v "$(pwd):/app" -w /app/src/HisServer \
  -e ConnectionStrings__MySql="Server=127.0.0.1;Port=33060;Database=his_server;Uid=root;Pwd=demopass;SslMode=None;AllowPublicKeyRetrieval=True;" \
  mcr.microsoft.com/dotnet/sdk:8.0 dotnet run --urls http://0.0.0.0:5100
```

> Cổng `33060` là cổng container `his-mysql` map ra máy, không phải 3306.
> Kiểm tra bằng `docker port his-mysql` nếu máy bạn khác.

### ⚠ Một cái bẫy đã dính thật trong đợt này

`mock_gateway.py` giữ **kết nối TCP bền cho mỗi giường** và **không tự kết nối
lại**. Restart HIS Server là nó **chết im lặng** — không báo lỗi, dashboard chỉ
đơn giản ngừng cập nhật.

→ **Restart server thì restart luôn `mock_gateway.py`.** Kiểm tra bằng:

```bash
ps aux | grep mock_gateway | grep -v grep
```

---

## 4. Những thứ chưa thể xác nhận nếu không có phần cứng

Hai mục dưới đây **đã chạy đúng trên giả lập**, nhưng giả lập không tái hiện được
hết hành vi thật. Người có board cần xác nhận nốt.

### 4.1. `linkQuality` hiện đúng số thật từ Zigbee

- **Đã chắc:** server đọc, lưu, trả về API và UI hiện đúng — với số do
  `mock_gateway.py` sinh ngẫu nhiên trong khoảng 150–220.
- **Chưa chắc:** số từ **radio thật** có nằm đúng dải và đổi hợp lý theo khoảng
  cách không.
- **Cách kiểm:** cắm board thật, mở tab Devices → chọn thiết bị → xem ô
  **Link Quality**. Mang board ra xa dần coordinator, số phải **giảm**.
  Đối chiếu với số gateway thấy:
  ```bash
  ssh iotchallenge@raspberrypi.local \
    "mosquitto_sub -h localhost -t 'zigbee2mqtt/SmartIV-Sensor' -C 1" | grep linkquality
  ```

### 4.2. Thiết bị thật tự chuyển `Offline` sau 90 giây

- **Đã chắc:** logic scanner đúng — đã test bằng cách gửi 1 gói TCP tay rồi ngừng,
  bảng `device_events` ghi `ONLINE` rồi `OFFLINE` cách nhau đúng bằng ngưỡng cấu hình.
- **Chưa chắc:** với thiết bị Zigbee thật, `LastDataAt` có được cập nhật đều đặn
  như giả lập không (firmware báo cáo tối đa 60 giây/lần, ngưỡng là 90 giây — biên
  an toàn chỉ 30 giây).
- **Cách kiểm:** để board chạy ổn định 5 phút, xác nhận vẫn `ONLINE` (không được
  nhấp nháy sang Offline rồi về). Sau đó **rút nguồn board**, đếm giờ: trong vòng
  ~95 giây phải chuyển `OFFLINE`.
- **Nếu nhấp nháy sai:** ngưỡng nằm ở `appsettings.json` → `Offline.ThresholdSeconds`
  (hiện `90`). Ngưỡng này **dùng chung cho cả giường lẫn thiết bị** — đổi là đổi cả
  hai, đó là chủ ý để hai bên không bao giờ mâu thuẫn nhau.

---

## 5. Cấu hình các phần còn lại của hệ thống

Phần này để bạn dựng lại **toàn bộ** hệ thống, không chỉ phần web.

### 5.1. Thứ tự dựng

```
1. MySQL          → schema.sql, rồi các file trong migrations/ theo thứ tự ngày
2. HIS Server     → cần .NET 8, chuỗi kết nối MySQL (KHÔNG có giá trị mặc định)
3. Firmware       → build + nạp vào board xG26 (xem docs/HUONG_DAN_A_Z.md)
4. Raspberry Pi   → mosquitto + zigbee2mqtt + gateway_test (xem docs/TRIEN_KHAI_PI.md)
5. Ghép Zigbee    → permit_join, ép mask kênh (TRIEN_KHAI_PI.md mục 7)
6. OTA (tuỳ chọn) → cấu hình index trên Pi (TRIEN_KHAI_PI.md mục 6.5 + docs/OTA.md)
```

Mỗi bước có tài liệu riêng đã viết sẵn — **đừng đọc lại từ code**, các tài liệu
đó ghi cả những lần đi sai đã gặp thật.

### 5.2. Database

```bash
mysql -u <user> -p < server/database/schema.sql        # chạy 1 lần, an toàn khi chạy lại
```

Rồi chạy **lần lượt theo thứ tự ngày** các file trong `server/database/migrations/`.
Tất cả đều an toàn khi chạy lại (dùng `IF NOT EXISTS` hoặc `WHERE NOT EXISTS`).

```bash
# Dữ liệu demo (giường, phòng, thiết bị giả) — chỉ dùng cho dev/demo
mysql -u <user> -p his_server < server/database/seed_demo.sql
```

> **`2026-08-21_seed_demo_accounts.sql` chỉ dành cho quay video/demo** — file tự
> ghi rõ "do not merge to main". Nó tạo tài khoản với `must_change_password =
> FALSE` để luồng đăng nhập trên camera không bị chặn giữa chừng. **Đừng chạy nó
> trên hệ thống thật.**

### 5.3. Quên mật khẩu admin

Không có email khôi phục — đây là chủ ý. Cách khôi phục là ghi thẳng hash mới vào
DB. Sinh hash bằng đúng thuật toán của `Services/PasswordHasher.cs`
(PBKDF2-HMACSHA256, 210 000 vòng, `pbkdf2$<vòng>$<salt b64>$<hash b64>`):

```bash
python3 - <<'EOF'
import os, hashlib, base64
salt = os.urandom(16); iters = 210000
password = "MatKhauMoi@2026"          # đổi thành mật khẩu bạn muốn
dk = hashlib.pbkdf2_hmac('sha256', password.encode(), salt, iters, dklen=32)
print(f"pbkdf2${iters}${base64.b64encode(salt).decode()}${base64.b64encode(dk).decode()}")
EOF
```

Rồi `UPDATE users SET password_hash='<chuỗi vừa in>', must_change_password=TRUE
WHERE username='admin';`

> Đặt `must_change_password=TRUE`: mật khẩu bạn vừa đặt đã bị người khác biết
> (chính bạn), nên chủ tài khoản phải thay ở lần đăng nhập kế tiếp.

### 5.4. Cấu hình server

Không có giá trị mặc định nào được nhúng cứng cho chuỗi kết nối — app **fail
ngay lúc khởi động** nếu thiếu. Bảng đầy đủ nằm ở `server/README.md`; những mục
hay phải chỉnh:

| Cấu hình | Biến môi trường | Hiện tại | Ghi chú |
|---|---|---|---|
| Chuỗi kết nối MySQL | `ConnectionStrings__MySql` | *(bắt buộc)* | Không có mặc định |
| Cổng nhận dữ liệu TCP | `Tcp__Port` | `5000` | JSON mỗi dòng một bản ghi |
| Ngưỡng offline | `Offline__ThresholdSeconds` | `90` | **Dùng chung giường + thiết bị** |
| Chu kỳ quét offline | `Offline__ScanIntervalSeconds` | `5` | |

> **Cổng 5000 để riêng cho TCP.** Đừng cho web nghe trùng cổng đó — chạy web ở
> `5100` như lệnh ở mục 3.

---

## 6. Chạy lại toàn bộ kiểm thử

Không cần board, không cần Pi:

```bash
# Bộ đánh giá trạng thái giường (console app, không cần NuGet)
dotnet run --project server/tests/EvaluatorTests

# Kiểm tra render phía web
node tools/dashboard_render_check.js
node tools/ota_render_check.js

# Cú pháp các file đã sửa
node --check server/src/HisServer/wwwroot/js/beds.js
python3 -m py_compile tools/mock_gateway.py tools/serial_gateway.py

# Kiểm thử firmware chạy trên máy (không cần chip)
cc -I firmware -o /tmp/t tools/fusion_test.c firmware/ai_fusion.c firmware/line_rules.c -lm && /tmp/t
cc -I firmware -o /tmp/t tools/drop_filter_test.c firmware/drop_filter.c -lm && /tmp/t
cc -I firmware -o /tmp/t tools/oled_test.c firmware/oled_display.c && /tmp/t
```

Toàn bộ đều **PASS** tại thời điểm bàn giao.

---

## 7. Những khoảng trống đã biết

Ghi ra để người sau khỏi tưởng là bug mới phát hiện:

| Khoảng trống | Vì sao chưa làm |
|---|---|
| **Gateway không tự biết trạng thái** | Không có `BedReading` nào đi qua nó. Cần heartbeat riêng từ `zigbee2mqtt/bridge/state` — tính năng khác |
| **Trạng thái `Warning` set tay bị ghi đè** | Thiết bị đang sống: gói dữ liệu kế tiếp tính lại `Online`/`SensorFault` và ghi đè trong ~1 giây. Chỉ "dính" với thiết bị không có luồng dữ liệu (gateway, thiết bị chưa gán giường) |
| **Tên bệnh nhân ở tab Alerts tra cứu trực tiếp (live)** | Bản ghi cảnh báo **không lưu** tên bệnh nhân — nó là hồ sơ sinh hiệu, không phải hồ sơ ai nằm giường đó. Tên hiện ra là tên **hiện tại** của giường. Đủ tốt cho cảnh báo đang xử lý; nếu sau này cần đúng-tại-thời-điểm-cảnh-báo thì phải lưu tên vào bảng `alerts` lúc ghi |
| **Chưa có tính năng bàn giao ca riêng** | Hiện dựa vào tab **My shift**: ca mới đăng nhập là thấy ngay phòng/giường mình phụ trách. Không có ghi chú bàn giao hay ký nhận — xem `docs/PHAN_QUYEN_VA_VAI_TRO.md` tình huống TH1 |
| **Phân công KHÔNG chặn quyền xem** | Cố ý. Y tá vẫn mở được mọi giường trong khoa vì lúc cấp cứu, người chạy tới đầu tiên không nhất thiết là người được phân công giường đó — màn hình trống lúc ấy là nguy hiểm. Lập luận đầy đủ ở `docs/PHAN_QUYEN_VA_VAI_TRO.md` |
| **Logo đối tác đang là chữ, chưa phải ảnh** | Chưa có file logo chính thức của ICTU/FPT/Silicon Labs trong repo. Markup đã dựng sẵn để thay `<span>` bằng `<img>` mà không phải sửa layout |

---

## 8. Đọc tiếp ở đâu

| Cần gì | Đọc file nào |
|---|---|
| Hiểu toàn hệ thống từ chip tới web | [`HUONG_DAN_A_Z.md`](HUONG_DAN_A_Z.md) |
| Sửa gì trong `gateway/` | [`TRIEN_KHAI_PI.md`](TRIEN_KHAI_PI.md) — **đọc trước khi sửa** |
| Phát hành firmware qua OTA | [`OTA.md`](OTA.md) |
| Vì sao ba vai trò chia quyền như hiện tại | [`PHAN_QUYEN_VA_VAI_TRO.md`](PHAN_QUYEN_VA_VAI_TRO.md) |
| Riêng HIS Server: API, DB, cách chạy | [`../server/README.md`](../server/README.md) |
| Mục lục toàn bộ tài liệu | [`README.md`](README.md) |

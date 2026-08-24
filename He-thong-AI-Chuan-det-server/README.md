# HIS Server — Trạm 4 của hệ thống Smart IV

Đây là **cây mã nguồn độc lập của HIS Server**, tách riêng khỏi phần firmware và
gateway. Toàn bộ mã nằm trong [`server/`](server/).

| | |
|---|---|
| Công nghệ | ASP.NET Core 8 (Kestrel) + MySQL 8 + SignalR |
| Chạy được trên | Linux và Windows |
| Nhận dữ liệu | TCP `:5000`, mỗi dòng một object JSON |
| Phục vụ web | HTTP `:5194` |

Tổng quan cả hệ thống bốn trạm: [`../README.md`](../README.md) và
[`../docs/01-kien-truc.md`](../docs/01-kien-truc.md).
Chi tiết kỹ thuật server: [`server/README.md`](server/README.md).

---

## Server làm gì

- **Nhận** dữ liệu sinh hiệu từ gateway qua TCP, phân tích và lưu vào MySQL.
- **Tính trạng thái giường** (Stable / Warning / Critical / Offline) trong
  `Domain/VitalsStatusEvaluator.cs` — đây là **nơi duy nhất** trên server được
  quyết định trạng thái giường.
- **Đẩy realtime** lên trình duyệt qua SignalR, không cần polling.
- **Phục vụ web UI** cho y tá: dashboard, biểu đồ, lịch sử cảnh báo, quản lý
  thiết bị và tài khoản.
- **REST API** cho ứng dụng di động đi kèm.
- **Push FCM** cho cảnh báo mức Warning và Critical (tuỳ chọn, cần Firebase).

## Server **không** làm gì

Đây là ranh giới quan trọng nhất của kiến trúc:

**Server không tính lại mức cảnh báo.** Mức 1/2/3 do firmware G26 quyết định và
gửi lên nguyên vẹn qua trường `final_alert_level`. Server hiển thị và lưu nó.
Các trường `line_branch`, `patient_branch`, `spo2_low`, `heart_rate_abnormal`,
`line_blocked`, `ae_alarm` chỉ **giải thích nguyên nhân**, không dùng để suy ra
mức mới.

**Server không tái tạo thuật toán học của firmware.** `drop_training_samples`
(0..20), `vitals_training_samples` (0..64) và `alerts_armed` được hiển thị đúng
như chip gửi lên.

Lý do: trước đây cả chip lẫn server cùng tính mức cảnh báo, và hai bên ra hai
kết quả khác nhau trên cùng một bản ghi.

Riêng **trạng thái Offline** là do server tự quyết — chip không thể tự báo mình
mất kết nối. Không nhận được dữ liệu trong `Offline:ThresholdSeconds` (mặc định
90 giây) thì giường **và thiết bị của nó** bị đánh dấu Offline.

---

## Cấu trúc

```text
server/
  HisServer.sln
  src/HisServer/          Ứng dụng ASP.NET Core (backend + web tĩnh)
    Api/                  Định nghĩa REST endpoint
    Hubs/                 SignalR hub - đẩy realtime lên trình duyệt
    Ingestion/            TCP listener :5000 + parser wire format
    Domain/               Ngưỡng trạng thái, luật chuyển cảnh báo,
                          sức khoẻ thiết bị, hai scanner offline
    Data/                 Truy cập MySQL (Dapper + MySqlConnector)
    Services/             Throttle ghi vitals, push FCM, OTA, mật khẩu
    Models/               DTO dùng chung
    wwwroot/              Web frontend - HTML/CSS/JS thuần, không có bước build
      js/                 Mỗi tab một file, thêm charts.js (đồ thị SVG inline)
                          và critical-alarm.js (banner Critical toàn màn hình)
  database/schema.sql     Schema MySQL, chạy một lần trước khi khởi động
  database/migrations/    Migration theo ngày, chạy tuần tự theo tên file
  tests/EvaluatorTests/   Kiểm thử bộ đánh giá trạng thái giường (console app)
```

Web frontend **không có bước build**: sửa file trong `wwwroot/` là F5 thấy ngay.

---

## Chạy nhanh

Lần đầu, xem hướng dẫn đầy đủ ở [`../docs/05-cai-dat.md`](../docs/05-cai-dat.md)
(bước 3 và bước 4).

```bash
# Nạp schema + toàn bộ migration
mysql -u root -p < server/database/schema.sql
for f in server/database/migrations/*.sql; do mysql -u root -p his_server < "$f"; done

# Cấu hình chuỗi kết nối - không commit mật khẩu vào Git
cd server/src/HisServer
dotnet user-secrets set "ConnectionStrings:MySql" \
  "Server=127.0.0.1;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU>;SslMode=None;AllowPublicKeyRetrieval=True;"

dotnet run --launch-profile http
```

Khởi động đúng thì log có đủ ba dòng:

```text
Restored ... bed(s) from the database.
Bed vitals TCP ingestion listening on port 5000.
Now listening on: http://0.0.0.0:5194
```

Nghe `0.0.0.0` là **có chủ ý**: gateway trên Pi phải gọi vào được từ máy khác,
không chỉ localhost.

> `5000` và `5194` là hai cổng khác nhau: `5000` nhận dữ liệu TCP từ gateway,
> `5194` phục vụ web. Đừng cho web nghe trùng `5000`.

Tài khoản demo (từ migration `2026-08-21_seed_demo_accounts.sql`):

| Vai trò | Username | Password |
|---|---|---|
| Y tá | `yta` | `YTaDemo@2026` |
| Kỹ thuật | `kythuat` | `KyThuat@2026` |

Chỉ dùng để demo, phải đổi trước khi triển khai thật.

---

## Kiểm thử

```bash
dotnet build server/HisServer.sln
dotnet run --project server/tests/EvaluatorTests
```

`EvaluatorTests` là console app thuần, không cần NuGet package nào ngoài SDK.

---

## Phân quyền

Chặn ở **cả hai lớp**: giao diện xoá hẳn phần tử không thuộc quyền, và API trả
403. Kênh realtime SignalR cũng chia nhóm theo năng lực, nên phiên của kỹ thuật
viên không nhận gói dữ liệu bệnh nhân nào.

| | Điều dưỡng | Kỹ thuật viên | Quản trị viên |
|---|---|---|---|
| Dashboard, chỉ số, cảnh báo lâm sàng | ✅ | ❌ | ❌ |
| Đặt ngưỡng, tare cân, nhận/xuất bệnh nhân | ✅ | ❌ | ❌ |
| Báo hỏng thiết bị | ✅ tạo | ✅ xử lý | ❌ |
| Tình trạng thiết bị, gán giường | ❌ | ✅ | ❌ |
| Cập nhật firmware từ xa (OTA) | ❌ | ✅ | ✅ |
| Danh mục giường, System Log, tài khoản | ❌ | ❌ | ✅ |

---

## Quyết định thiết kế đáng lưu ý

Ghi lại để người sau khỏi "sửa lại cho gọn" đúng những chỗ đã cân nhắc kỹ.

**"Chưa có dữ liệu" không phải "ổn định", cũng không phải "nguy kịch".** Cảm
biến chưa cắm đọc ra 0; coi số 0 đó là chỉ số thật thì giường trống cũng kêu
"SpO2 0% — nguy kịch". Kênh mất tín hiệu hiện `--` và đẩy giường sang **Warning**
kèm lý do `No signal from: ...`.

**Chỉ một nơi quyết định trạng thái giường**: `Domain/VitalsStatusEvaluator.cs`.
Gateway chỉ chuyển tiếp dữ liệu thô và cờ, không tự suy luận.

**Cấu hình `Offline` trong `appsettings.json` mới là cái đang chạy**, không phải
default trong C#. `OfflineOptions` fallback 3s/1s nếu thiếu hẳn section, nhưng
file cấu hình đang ship là **90s/5s**. Khi thắc mắc "sao giường mất N giây mới
Offline", đọc `appsettings.json` chứ đừng đọc default trong code.

**Không có fallback cho chuỗi kết nối.** Thiếu `ConnectionStrings:MySql` là app
chết ngay lúc startup, không âm thầm chạy với database rỗng.

Danh sách biến cấu hình đầy đủ và wire format TCP:
[`server/README.md`](server/README.md).

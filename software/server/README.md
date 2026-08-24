# HIS Server — tham chiếu kỹ thuật

Tài liệu tra cứu cho người sửa code server: cấu hình, wire format, endpoint,
triển khai. Phần giới thiệu và các quyết định thiết kế nằm ở
[`../README.md`](../README.md).

## Yêu cầu

- .NET 8 SDK
- MySQL 8.x truy cập được từ máy chạy server
- *(Tuỳ chọn)* File JSON service account của Firebase, nếu cần push cho app di động

---

## Cấu hình

Dùng cơ chế cấu hình chuẩn của ASP.NET Core: `appsettings.json`, biến môi
trường, hoặc `dotnet user-secrets` khi phát triển cục bộ.

**Không có giá trị fallback cho chuỗi kết nối database.** Thiếu nó là app chết
ngay lúc startup thay vì âm thầm chạy với database rỗng.

| Cấu hình | Biến môi trường | Giá trị đang ship | Ý nghĩa |
|---|---|---|---|
| Chuỗi kết nối MySQL | `ConnectionStrings__MySql` | *(bắt buộc, không có mặc định)* | `Server=localhost;Port=3306;Database=his_server;Uid=...;Pwd=...;` |
| Cổng TCP nhận vitals | `Tcp__Port` | `5000` | JSON phân cách bằng newline, mỗi dòng một bản ghi |
| API key nhận vitals qua HTTPS | `HttpIngestion__ApiKey` | *(rỗng = tắt endpoint)* | Gửi bằng header `X-Ingestion-Key`; lưu bằng environment variable hoặc user-secrets |
| Chu kỳ ghi lịch sử vitals | `VitalsSave__IntervalSeconds` | `10` | Bao lâu ghi một lần vào bảng `vital_samples` |
| Ngưỡng đánh dấu Offline | `Offline__ThresholdSeconds` | `90` | Không có dữ liệu quá ngần này giây ⇒ giường **và thiết bị của nó** thành Offline |
| Chu kỳ quét Offline | `Offline__ScanIntervalSeconds` | `5` | Bao lâu hai scanner offline chạy một lần |
| Firebase project ID | `Firebase__ProjectId` | *(rỗng = tắt push)* | |
| Đường dẫn service account | `FPT_FIREBASE_SERVICE_ACCOUNT` | *(rỗng = tắt push)* | Chỉ đọc từ biến môi trường, không đọc từ `appsettings.json` |

> **"Giá trị đang ship" là giá trị trong `appsettings.json` của repo này**, không
> phải default trong lớp options C#. Hai thứ này **khác nhau** ở nhóm `Offline`:
> `OfflineOptions` fallback về 3s/1s nếu thiếu hẳn section, nhưng file cấu hình
> đang ship là 90s/5s. Khi cần biết vì sao một giường mất N giây mới chuyển
> Offline, đọc `appsettings.json` — đừng đọc default trong code.

Ví dụ cấu hình cục bộ:

```bash
cd src/HisServer
dotnet user-secrets set "ConnectionStrings:MySql" \
  "Server=localhost;Port=3306;Database=his_server;Uid=root;Pwd=<MAT_KHAU>;SslMode=None;AllowPublicKeyRetrieval=True;"
dotnet run --launch-profile http
```

Profile `http` bind `http://0.0.0.0:3000` cho web, còn TCP nhận vitals nghe cổng
`Tcp:Port` (mặc định `5000`). **Hai cổng phải khác nhau** — cho web nghe trùng
`5000` là hai listener tranh nhau cổng.

---

## Database

```bash
mysql -u <user> -p < database/schema.sql
```

Lệnh này tạo database `his_server` và toàn bộ bảng. Chạy lại nhiều lần vẫn an
toàn (`CREATE TABLE IF NOT EXISTS`).

Sau đó chạy **toàn bộ** file trong `database/migrations/` **theo thứ tự tên**:

```bash
for f in database/migrations/*.sql; do mysql -u root -p his_server < "$f"; done
```

| Migration | Thêm gì |
|---|---|
| `2026-07-29_vital_samples_smartiv_columns.sql` | Cột Smart IV cho `vital_samples` |
| `2026-07-30_null_out_impossible_vitals.sql` | Dọn giá trị sinh hiệu bất khả thi thành NULL |
| `2026-08-17_device_health_and_faults.sql` | Bảng sức khoẻ thiết bị và báo hỏng |
| `2026-08-17_users_roles_assignments.sql` | Tài khoản, vai trò, gán giường |
| `2026-08-18_alert_message_length.sql` | Nới độ dài nội dung cảnh báo |
| `2026-08-19_alert_ack_note.sql` | Ghi chú khi xác nhận cảnh báo |
| `2026-08-19_drop_dead_temperature_column.sql` | Bỏ cột nhiệt độ không dùng nữa |
| `2026-08-19_firmware_update_events.sql` | Nhật ký sự kiện cập nhật firmware |
| `2026-08-21_seed_demo_accounts.sql` | Hai tài khoản demo `yta` và `kythuat` |

---

## Wire format TCP

Gateway mở kết nối TCP tới cổng `Tcp:Port` và gửi **mỗi dòng một object JSON**:

```json
{"bedId":"BED-01","room":"ICU-1","spo2":98,"heartRate":75,"dripRate":20}
```

Tên trường khớp **không phân biệt hoa thường** và mỗi trường có nhiều alias, giữ
lại cho tương thích với firmware cũ. Danh sách alias đầy đủ nằm trong
[`src/HisServer/Ingestion/BedDataParser.cs`](src/HisServer/Ingestion/BedDataParser.cs) —
đó là nguồn sự thật, không phải tài liệu này.

Ý nghĩa các trường Smart IV (`final_alert_level`, `alerts_armed`,
`drop_training_samples`, `line_branch`, ...): xem
[`../../docs/03-zigbee.md`](../../docs/03-zigbee.md).

---

## Thành phần

| Thư mục | Việc |
|---|---|
| `Api/` | 12 nhóm REST endpoint: auth, bed, alert, device, patient, user, profile, settings, log, fault report, OTA image, mobile |
| `Hubs/` | SignalR hub — đẩy realtime lên trình duyệt, chia nhóm theo quyền |
| `Ingestion/` | `BedTcpIngestionService` (listener) và `BedDataParser` (wire format) |
| `Domain/` | `VitalsStatusEvaluator` (trạng thái giường), `AlertTransitionTracker`, `DeviceHealthEvaluator`, `BedStateStore`, `BedConnectionRegistry`, `Capabilities`, và hai scanner offline |
| `Data/` | 9 repository dùng Dapper + MySqlConnector |
| `Services/` | `VitalsPersistenceCoordinator` (throttle ghi), `FcmPushService`, `OtaImageStore`, `OtaStatusRegistry`, `PasswordHasher` |
| `Models/` | DTO dùng chung |
| `wwwroot/` | Frontend HTML/CSS/JS thuần — **không có bước build**, sửa xong F5 là thấy |

`VitalsStatusEvaluator.cs` là **nơi duy nhất** trên server quyết định trạng thái
giường. Đừng thêm logic đánh giá trạng thái ở chỗ khác.

Trạng thái OTA (`OtaStatusRegistry`) **cố ý không lưu vào database**: một lần
cập nhật firmware không sống sót qua restart server, nên bản ghi về nó cũng
không nên sống sót.

---

## Kiểm thử

```bash
dotnet build HisServer.sln
dotnet run --project tests/EvaluatorTests
```

`EvaluatorTests` là console app thuần, kiểm bộ đánh giá trạng thái giường,
không cần NuGet package nào ngoài SDK.

---

## Triển khai

Ứng dụng là **một tiến trình Kestrel duy nhất**, không tự sinh tiến trình tunnel
nào. Muốn truy cập từ ngoài mạng nội bộ thì đặt sau reverse proxy (nginx, Caddy,
IIS) hoặc tunnel ở mức hệ điều hành.

**Linux** — chạy bằng systemd:

```bash
dotnet src/HisServer/bin/Release/net8.0/HisServer.dll
```

**Windows** — chạy như Windows Service, hoặc `dotnet run`, hoặc file thực thi đã
publish. Không cần quyền admin cho cổng HTTP.

### Raspberry Pi gửi qua domain HTTPS

Pi vẫn chạy gateway binary với giao thức TCP nội bộ tại `127.0.0.1:15000`.
`tools/http_ingestion_bridge.py` chuyển mỗi dòng JSON sang:

```text
POST https://smartivcare.io.vn/api/ingestion/bed
X-Ingestion-Key: <secret ngoài source>
```

Server dùng chung parser và pipeline xử lý của listener TCP `5000`, vì vậy
telemetry, device announce và trạng thái OTA không bị tách thành hai logic khác
nhau. Lệnh điều khiển từ web được trả trong response HTTPS và bridge ghi ngược
vào socket local, giữ kênh hai chiều của gateway. API key không được ghi vào
`appsettings.json` hay commit vào Git.

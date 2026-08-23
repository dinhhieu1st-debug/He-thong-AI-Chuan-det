# Luồng "Đang thiết lập" và "Tạm ngưng theo dõi" — tài liệu thiết kế

> **Trạng thái tài liệu: THIẾT KẾ, CHƯA TRIỂN KHAI.** Mọi tên field/API/trạng
> thái dưới đây là đề xuất để thống nhất trước khi code, không phải mô tả
> code đang chạy. Phần 7 liệt kê rõ cái gì đã có sẵn và cái gì còn thiếu.

## 1. Vấn đề

[`PHAN_QUYEN_VA_VAI_TRO.md`](PHAN_QUYEN_VA_VAI_TRO.md) mục "TH2 — Nhận bệnh
nhân mới" mô tả luồng:

> Điều dưỡng mở giường, nhập tên + mã bệnh nhân, treo túi dịch, **tare cân**,
> kẹp cảm biến, **chờ 60 giây đo baseline nhịp tim**, nhập ngưỡng truyền theo
> y lệnh.

Tài liệu đó mô tả *thao tác của điều dưỡng*, nhưng không nói **hệ thống cảnh
báo làm gì trong 60 giây đó**. Thực tế hiện nay: **không làm gì đặc biệt cả.**
`VitalsStatusEvaluator.Evaluate()` (`server/src/HisServer/Domain/
VitalsStatusEvaluator.cs`) chấm điểm mọi reading đến ngay lập tức, không có
khái niệm "đang thiết lập" hay "tạm ngưng". Hệ quả:

| Lúc nào | Chuyện gì xảy ra hôm nay | Vì sao đó là vấn đề |
|---|---|---|
| Vừa kẹp cảm biến, SpO2/nhịp tim chưa ổn định, HR baseline đang đếm 60s | Vượt ngưỡng → **Warning/Critical ngay** | Baseline chưa xong nghĩa là hệ thống *chưa biết* nhịp tim bình thường của bệnh nhân này là bao nhiêu — cảnh báo "bất thường" lúc này là so với ngưỡng mặc định, không so với bệnh nhân thật |
| Vừa treo túi, đang tare cân | Nếu drop/flow tạm thời đọc lệch trong lúc tare | Có thể trôi qua ngưỡng LineBlocked/AeAlarm dù đây là thao tác bình thường |
| Y tá cố ý tháo cảm biến (đưa bệnh nhân đi chụp X-quang, thay đầu đo...) | Mất tín hiệu → **Warning "No signal from..."**, sau 90s → **Offline** | Đây là hành động **có chủ đích**, không phải sự cố — nhưng hệ thống không phân biệt được với dây tuột thật |

Kết quả thực tế: y tá vừa nhận bệnh nhân xong, màn hình đã nhấp nháy cảnh báo
trước khi họ kịp bước ra khỏi giường — đúng kiểu báo động giả làm người dùng
học cách lờ đi cảnh báo (alarm fatigue), thứ mà chính
`VitalsStatusEvaluator.cs` đã nói rõ ở phần hysteresis là điều cần tránh.

---

## 2. Hai kịch bản, không phải một

Ban đầu dễ gộp chung "đang thiết lập" và "tạm ngưng" làm một trạng thái, nhưng
chúng khác nhau ở nguồn gốc và ở việc ai/khi nào kết thúc:

| | **Đang thiết lập (Setup)** | **Tạm ngưng (Paused)** |
|---|---|---|
| Xảy ra khi nào | Ngay sau khi nhận bệnh nhân mới, hoặc khi hiệu chuẩn lại (tare/HR baseline) | Bất cứ lúc nào giữa ca, có chủ đích |
| Ai/cái gì bắt đầu | Firmware tự chạy (HR baseline 60s luôn chạy khi calibrate) | Điều dưỡng bấm nút — hành động chủ động |
| Ai/cái gì kết thúc | Tự hết sau 60s (đếm ngược có sẵn) | Điều dưỡng bấm Resume — không tự hết |
| Rủi ro nếu quên kết thúc | Không có — tự động, tối đa 60s | **Có** — quên bấm Resume thì giường "câm" cảnh báo vô thời hạn |
| Dữ liệu vẫn có ý nghĩa gì không | Có — vẫn hiển thị số đo, chỉ không dùng để BÁO ĐỘNG | Thường không — cảm biến đã tháo, số hiển thị là rác |

Vì rủi ro khác nhau (Setup tự giới hạn 60s; Paused có thể bị bỏ quên), hai
kịch bản này **không nên dùng chung một cơ chế tắt cảnh báo vô thời hạn**.

---

## 3. Thiết kế đề xuất — Setup (gắn với HR baseline có sẵn)

Firmware đã có sẵn cửa sổ 60 giây này (`firmware/app.c`, `HR_CALIB_MS`), và
giá trị đếm ngược đã chảy tới tận server:

```
firmware (app.c: app_hr_baseline_seconds_remaining())
  → Zigbee attribute HrBaselineSecondsRemaining
  → gateway → BedDataParser → BedReading.HrBaselineSecondsRemaining (int?)
  → BedState.HrBaselineSecondsRemaining
  → BedDto.HrBaselineSecondsRemaining (đã hiện trên UI, xem beds.js)
```

**Không cần thêm state mới ở firmware.** Việc còn thiếu chỉ nằm ở server: khi
`reading.HrBaselineSecondsRemaining` là số dương (đang đếm ngược), tạm hoãn
việc coi các chỉ số NHỊP TIM là "bất thường" — vì hệ thống chưa có baseline để
so sánh.

### Phạm vi hoãn — chỉ nhịp tim, không phải toàn bộ giường

Bảng dưới liệt kê từng nguồn cảnh báo trong `VitalsStatusEvaluator.Evaluate()`
và có nên hoãn trong lúc Setup hay không:

| Nguồn cảnh báo | Hoãn trong Setup? | Vì sao |
|---|---|---|
| `IsAbnormalHeartRate` (nhịp tim thấp/cao) | ✅ Có | Đúng thứ HR baseline đang đo — chưa có baseline thì "bất thường" so với cái gì? |
| `IsCriticalSpo2` / `IsWarningSpo2` | ❌ Không | SpO2 nguy kịch (< 90%) là ngưỡng tuyệt đối, không phụ thuộc baseline cá nhân — một bệnh nhân tụt oxy thật trong lúc đang gắn máy vẫn phải báo |
| `AeAlarm` (AI: tổ hợp HR+SpO2 bất thường) | ✅ Có | Mô hình AI dùng baseline cá nhân làm input — chạy trên baseline "giả" (còn đang tính) sẽ cho kết quả vô nghĩa |
| `reading.LineBlocked` / `deviceStatus` (line) | ❌ Không | Không liên quan gì tới HR baseline — tắc dây lúc đang thiết lập vẫn là tắc dây thật |
| `HasLostSignal` (mất tín hiệu kênh) | ❌ Không, **trừ kênh HR** | Mất SpO2/Flow/Drip lúc thiết lập vẫn đáng báo (cảm biến kẹp sai từ đầu); riêng kênh HR thì im lặng vì baseline vốn cũng cần HR có tín hiệu để chạy — không mất tín hiệu HR không nghĩa là báo được gì mới |
| `VitalsAnomaly` / `DripAnomaly` (bộ dự báo AI time-series) | ✅ Có | Cùng lý do AeAlarm — mô hình cần lịch sử ổn định, 60 giây đầu không có |

Nói cách khác: **Setup không phải "tắt hết cảnh báo 60 giây"**, mà là "đừng so
sánh nhịp tim với một baseline chưa tồn tại". SpO2 tụt thật và line tắc thật
vẫn phải báo ngay cả khi đang thiết lập — đây là điểm khác với "Paused" ở mục
4, nơi cảm biến coi như đã tháo hẳn.

### Chỗ cần sửa (nếu triển khai theo hướng này)

- `VitalsStatusEvaluator.Evaluate()` / `DescribeAlert()`: nhận thêm tham số
  (vd `bool isCalibratingHr`, suy ra từ `reading.HrBaselineSecondsRemaining >
  0`), bỏ qua `IsAbnormalHeartRate`, `AeAlarm`, `VitalsAnomaly`/`DripAnomaly`
  khi `true`.
- `BedTcpIngestionService.cs` quanh dòng gọi `VitalsStatusEvaluator.Evaluate`
  (~566): truyền cờ đó vào.
- Hiển thị: xem mục 5.

---

## 4. Thiết kế đề xuất — Tạm ngưng (Paused), thao tác thủ công

Khác với Setup, đây **chưa có mầm mống nào trong code hiện tại** — cần thêm
field mới.

### Đề xuất field

```
BedState (server)              : bool  IsPaused
                                  string? PausedBy
                                  DateTime? PausedAt
BedDto                         : (map 3 field trên ra JSON)
```

### API đề xuất

```
POST /api/beds/{bedId}/pause    RequireAuthorization(Capabilities.ControlBed)
POST /api/beds/{bedId}/resume   RequireAuthorization(Capabilities.ControlBed)
```

Dùng `ControlBed` (điều dưỡng đã có, xem `Capabilities.cs`) — đúng theo
nguyên tắc "quyền đi theo công việc" của `PHAN_QUYEN_VA_VAI_TRO.md`: người tháo
cảm biến ra là người bấm Pause, không cần gọi ai khác.

### Trong lúc Paused

- `VitalsStatusEvaluator.Evaluate()` trả thẳng một trạng thái riêng (xem mục
  5), **không chạy bất kỳ rule nào khác** — khác Setup ở chỗ này: Setup chỉ
  hoãn phần liên quan HR, Paused hoãn toàn bộ, vì cảm biến coi như không còn
  gắn trên bệnh nhân.
- Không tạo Alert mới (`AlertTransitionTracker.ShouldRaiseAlert` phải thấy
  trạng thái Paused và bỏ qua).
- **Không tự Resume.** Đây là quyết định thiết kế then chốt, không phải thiếu
  sót: xem mục 6 để rõ vì sao tự-hết-hạn nguy hiểm hơn hữu ích ở đây.

### Việc còn để ngỏ — cần quyết định trước khi code phần này

1. **Có cần cảnh báo riêng nếu một giường bị Paused quá lâu không?** (vd quá
   30 phút mà chưa Resume — có thể là quên, không phải cố ý.) Nếu có, cảnh
   báo đó nên ở kênh nào (không phải cảnh báo lâm sàng, có thể là một dòng
   "nhắc việc" khác màu).
2. Đổi ca giữa lúc một giường đang Paused: bàn giao có cần hiện rõ trong
   **My shift** (`ProfileTab`) không, để ca sau không bỏ sót?

---

## 5. Hiển thị trên dashboard

Hiện `BedStatus` có 4 giá trị: `Stable, Warning, Critical, Offline` — không
đủ diễn tả hai trạng thái mới, vì cả hai đều **không phải "ổn định"** (dữ
liệu chưa/không đáng tin) nhưng cũng **không phải "có sự cố"** (không cần ai
chạy tới). Gộp vào Stable thì y tá không biết baseline chưa xong; gộp vào
Warning thì đúng loại báo động giả mà tài liệu này tồn tại để xoá bỏ.

Đề xuất thêm:

```
enum BedStatus { Stable, Warning, Critical, Offline, Setup, Paused }
```

| Trạng thái | Màu gợi ý | Text | Khi nào |
|---|---|---|---|
| `Setup` | Xanh dương/trung tính, khác hẳn xanh lá Stable | "Đang thiết lập — còn {n}s" (dùng luôn `hrBaselineSecondsRemaining` đang có) | `HrBaselineSecondsRemaining > 0` |
| `Paused` | Xám, khác hẳn xám Offline | "Tạm ngưng theo dõi — bởi {y tá}, lúc {giờ}" | `IsPaused == true` |

Thứ tự ưu tiên khi nhiều điều kiện cùng đúng: **Paused thắng Setup thắng mọi
thứ khác** — nếu y tá chủ động bấm Pause giữa lúc đang Setup thì ý định "tạm
ngưng" rõ ràng hơn, nên hiển thị theo đó.

`DEVICE_STATUS_COLOR`/`DEVICE_SEVERITY_RANK` trong `devices.js` và
`UiUtils.statusColor()` cần thêm 2 khoá mới — đúng chỗ badge OTA/culprit từng
bị thêm thiếu một bên (xem comment đầu `dashboard_render_check.js`), nên khi
code phần này nhớ thêm test cho cả `dashboard.js` lẫn `beds.js` cùng lúc.

---

## 6. Vì sao "tự hết hạn" không dùng cho Paused

Cửa sổ Setup tự hết sau 60 giây là an toàn vì nó **tự giới hạn theo phần
cứng** (đúng bằng thời gian firmware thật sự cần để tính baseline) — không
ai phải nhớ tắt nó.

Paused thì khác: không có "thời gian tháo cảm biến chuẩn" nào cả — có thể là
2 phút (đổi đầu đo) hoặc 45 phút (đi chụp CT). Hai lựa chọn tồi nếu cho tự hết
hạn:

- **Hết hạn ngắn (vd 5 phút):** bệnh nhân đi chụp CT 20 phút, hết hạn giữa
  chừng, hệ thống lại bắt đầu báo "mất tín hiệu" cho một tình huống y tá đã
  chủ động xử lý — quay lại đúng vấn đề mục 1.
- **Hết hạn dài (vd 1 giờ):** nếu y tá quên Resume sau khi gắn lại cảm biến
  thật, giường "câm" cảnh báo tới 1 giờ dù bệnh nhân đang có sự cố thật —
  nguy hiểm hơn nhiều so với vài phút báo động giả.

Vì vậy Resume phải là **hành động thủ công, không có hạn**, đánh đổi lấy rủi
ro "quên Resume" — đó là lý do mục 4 để ngỏ câu hỏi 1 (cảnh báo nhắc việc nếu
Paused quá lâu): không phải để tự động tắt Paused, mà để **nhắc người**, việc
tắt vẫn luôn do người quyết.

---

## 7. Đã có sẵn vs. còn thiếu

| Thành phần | Trạng thái |
|---|---|
| Firmware đếm ngược 60s HR baseline, gửi `hrBaselineSecondsRemaining` | ✅ Đã có, đang chạy |
| Field đó chảy tới `BedState`/`BedDto`, hiển thị trên UI | ✅ Đã có |
| `VitalsStatusEvaluator` dùng field đó để hoãn cảnh báo HR | ❌ Chưa — đây là việc chính của mục 3 |
| `BedStatus.Setup` (trạng thái + màu + hiển thị) | ❌ Chưa |
| `BedState.IsPaused/PausedBy/PausedAt` | ❌ Chưa |
| API `POST /pause`, `POST /resume` | ❌ Chưa |
| Nút Pause/Resume trên `beds.js` (panel chi tiết giường) | ❌ Chưa |
| `VitalsStatusEvaluator` bỏ qua toàn bộ rule khi Paused | ❌ Chưa |
| `AlertTransitionTracker` không tạo alert khi Paused/Setup | ❌ Chưa |
| `BedStatus.Paused` (trạng thái + màu + hiển thị) | ❌ Chưa |

---

## 8. Câu hỏi còn mở (cần trả lời trước khi bắt đầu code)

1. Đồng ý cách chia phạm vi hoãn ở mục 3 (chỉ hoãn HR/AeAlarm/model-anomaly,
   giữ nguyên SpO2 tuyệt đối + line + mất-tín-hiệu-kênh-khác trong lúc Setup)
   hay muốn Setup tắt cảnh báo rộng hơn/hẹp hơn?
2. Cơ chế Paused: xác nhận dùng nút thủ công Pause/Resume như mục 4, hay có
   phương án khác (vd tự suy ra từ việc TẤT CẢ kênh cùng mất tín hiệu một
   lúc)?
3. Có cần cảnh báo "nhắc việc" khi một giường Paused quá lâu không (mục 4, ý
   1)? Nếu có, ngưỡng bao lâu?
4. Trong lúc Setup/Paused, giường hiện trạng thái riêng (`Setup`/`Paused`,
   mục 5) hay vẫn hiện `Stable` như bình thường và chỉ ẩn việc tạo alert?

Trả lời xong 4 câu này thì phần triển khai (đổi `VitalsStatusEvaluator`, thêm
API, thêm nút trên UI) là việc cơ học, không còn quyết định thiết kế nào phải
đoán.

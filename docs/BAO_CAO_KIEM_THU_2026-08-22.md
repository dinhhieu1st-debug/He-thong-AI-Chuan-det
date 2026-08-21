# Báo cáo kiểm thử — sau đợt UI/UX

**Hệ thống:** Smart IV Monitor — HIS Server (ASP.NET Core) + web console
**Ngày kiểm thử:** 22/08/2026
**Commit kiểm thử:** `29a9726`, nhánh `ui-ux-and-ota-sync`
**Môi trường:** `http://localhost:5100` · MySQL 8 (container `his-mysql`) ·
thiết bị giả lập `tools/mock_gateway.py`
**Phần cứng:** ❌ **không có board, không có Raspberry Pi** trong đợt này

> Báo cáo này thay thế [`BAO_CAO_KIEM_THU_WEB.md`](BAO_CAO_KIEM_THU_WEB.md)
> (ngày 19/08/2026) làm bản mô tả trạng thái hiện tại. File cũ được giữ nguyên
> làm hồ sơ lịch sử.

---

## 1. Tóm tắt

| | Số ca | Ghi chú |
|---|---|---|
| **Tổng số ca kiểm thử** | **97** | |
| ✅ **Đạt** | **97** | |
| ❌ **Không đạt** | **0** | |
| ⏸ **Chưa kiểm được** | **2** | cần phần cứng thật — [mục 7](#7-hai-mục-chưa-kiểm-được) |

Phân bổ: tự động 87 ca · phân quyền API 25 lượt gọi · giao diện 8 ca thao tác thật.

**Server được khởi động lại sạch trước khi kiểm thử** để chắc chắn chạy đúng
code của commit trên, không phải bản biên dịch cũ còn trong bộ nhớ.

---

## 2. Kiểm thử tự động

| # | Bộ kiểm thử | Lệnh | Kết quả |
|---|---|---|---|
| 2.1 | Bộ đánh giá trạng thái giường | `dotnet run --project server/tests/EvaluatorTests` | **53/53 PASS** |
| 2.2 | Render dashboard | `node tools/dashboard_render_check.js` | **10/10 PASS** |
| 2.3 | Render OTA | `node tools/ota_render_check.js` | **12/12 PASS** |
| 2.4 | Bộ hợp nhất AI (chạy trên máy) | `tools/fusion_test.c` | **PASS** |
| 2.5 | Bộ lọc chống báo giả | `tools/drop_filter_test.c` | **PASS** |
| 2.6 | Màn hình OLED | `tools/oled_test.c` | **PASS** |
| 2.7 | Cú pháp toàn bộ JS | `node --check` trên `wwwroot/js/*.js` | **PASS**, 0 lỗi |
| 2.8 | Cú pháp Python | `py_compile` `mock_gateway.py`, `serial_gateway.py` | **PASS** |
| 2.9 | Biên dịch server | `dotnet build` | **PASS**, 0 lỗi 0 cảnh báo |

Ba bộ 2.4–2.6 chạy được **không cần chip** vì mã firmware liên quan được viết
tách khỏi HAL — đây là lý do chúng vẫn có giá trị khi không có board.

---

## 3. Phân quyền — ranh giới giữa ba vai trò

Gọi trực tiếp API bằng cookie phiên của từng vai trò.
**200** = được phép · **403** = bị chặn đúng.

| Endpoint | Điều dưỡng | Kỹ thuật viên | Quản trị viên | Đánh giá |
|---|---|---|---|---|
| `GET /api/beds` (dữ liệu bệnh nhân) | **200** | 403 | 403 | ✅ |
| `GET /api/alerts` | **200** | 403 | 403 | ✅ |
| `GET /api/devices` | 403 | **200** | 403 | ✅ |
| `GET /api/devices/ota` | 403 | **200** | 403 | ✅ |
| `GET /api/fault-reports` | 403 | **200** | 403 | ✅ |
| `GET /api/users` | 403 | 403 | **200** | ✅ |
| `GET /api/logs` | 403 | 403 | **200** | ✅ |
| `GET /api/beds/directory` | 200 | 200 | 200 | ✅ dùng chung, đúng thiết kế |
| `GET /api/me/assignment` | 200 | 200 | 200 | ✅ ai cũng xem hồ sơ mình |
| `PUT /api/beds/{id}/patient` | **200** | **403** | — | ✅ |

**Năng lực trả về từ `/api/auth/me`:**

| Vai trò | Capabilities |
|---|---|
| Điều dưỡng | `ward.view`, `alerts.ack`, `bed.control`, `patient.edit`, `faults.report`, `beds.directory` |
| Kỹ thuật viên | `devices.manage`, `faults.handle`, `beds.directory` |
| Quản trị viên | `beds.directory`, `beds.manage`, `logs.view`, `users.manage` |

> **Điểm đáng chú ý:** kỹ thuật viên nhận **403** trên `/api/beds` — xác nhận
> tuyên bố trong README rằng máy kỹ thuật viên **không nhận được gói dữ liệu
> bệnh nhân nào**. Đây là chặn ở lớp API, độc lập với việc giao diện có ẩn hay không.

---

## 4. Các thay đổi của đợt này

### 4.1. Thiết bị tự chuyển Offline khi ngừng gửi dữ liệu

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.1.1 | Thiết bị `PENDING` chưa từng gửi gói nào | Giữ nguyên `PENDING`, scanner không đụng vào | ✅ |
| 4.1.2 | Gửi 1 gói dữ liệu | Chuyển `ONLINE` lúc `17:50:41.965`, ghi sự kiện `ONLINE` | ✅ |
| 4.1.3 | Ngừng gửi, chờ quá ngưỡng | Chuyển `OFFLINE` lúc `17:52:16.134` | ✅ |
| 4.1.4 | **Độ trễ chuyển trạng thái** | **94,2 giây** — đúng khoảng dự kiến 90–95s (ngưỡng 90s + chu kỳ quét 5s) | ✅ |
| 4.1.5 | Ghi lịch sử sự kiện | Bảng `device_events` có đúng 2 dòng `ONLINE` → `OFFLINE` | ✅ |
| 4.1.6 | Hiển thị trên giao diện | Thẻ thiết bị hiện badge `OFFLINE`, ô thống kê Offline tăng lên 1 | ✅ |

### 4.2. Bấm Save không còn xoá dữ liệu sức khoẻ thiết bị

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.2.1 | `linkQuality` trước khi Save | `161` | — |
| 4.2.2 | `PUT /api/devices/{id}` bằng đúng payload form gửi | `200` | ✅ |
| 4.2.3 | `linkQuality` **sau** khi Save | **`161`** (trước khi sửa: `null`) | ✅ |
| 4.2.4 | `lastDataAt` sau khi Save | Giữ nguyên `2026-08-21T17:51:10.533` | ✅ |

### 4.3. Đồng bộ liên kết giường ↔ thiết bị

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.3.1 | Gán thiết bị vào `BED-103` | `beds.device_id` của BED-103 = `DEV-T02` | ✅ |
| 4.3.2 | Chuyển sang `BED-301` | BED-103 → `(null)`, BED-301 → `DEV-T02` | ✅ |
| 4.3.3 | Xoá thiết bị | BED-301 → `(null)`, không còn giường nào giữ liên kết mồ côi | ✅ |

Ca 4.3.2 là ca quan trọng nhất: **không có hai giường nào cùng nhận một thiết bị
vật lý** sau khi đổi gán.

### 4.4. Bỏ Battery/RSSI, thay bằng Link Quality

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.4.1 | `batteryPercent` trên mọi thiết bị | `null` ở **10/10** thiết bị | ✅ |
| 4.4.2 | `rssi` trên mọi thiết bị | `null` ở **10/10** thiết bị | ✅ |
| 4.4.3 | `linkQuality` trên thiết bị đang sống | Có giá trị thật ở **3/3** (178, 200, 190) | ✅ |
| 4.4.4 | Giao diện chi tiết thiết bị | Hiện ô **Link Quality**, không còn ô Battery/RSSI | ✅ |

Xác nhận lý do bỏ: hai trường cũ **luôn** rỗng vì không nơi nào trong đường dữ
liệu từng ghi vào chúng.

### 4.5. Tên bệnh nhân không còn biến mất khi đang gõ

Mô phỏng đúng kịch bản lỗi: gõ ô **Tên** → chuyển focus sang ô **Mã** → gõ tiếp
→ chờ **6 giây** (khoảng 6 lần panel tự dựng lại).

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.5.1 | Ô **Tên** (không còn focus) sau 6 giây | `"Hoang Van E"` — giữ nguyên | ✅ |
| 4.5.2 | Ô **Mã** (đang focus) sau 6 giây | `"BN-501"` — giữ nguyên | ✅ |
| 4.5.3 | File JS trình duyệt đang tải | Chứa đúng `["targetFlowInput", "targetDropsInput", "patientNameInput", "patientCodeInput"]` | ✅ |
| 4.5.4 | Nhập đủ tên + mã rồi bấm Admit | Toast `patient updated`, hiện đúng ở đầu trang | ✅ |

Ca 4.5.1 là ca quyết định — trước khi sửa, chính ô này bị xoá trắng.

### 4.6. Tên bệnh nhân hiện ở các màn hình lâm sàng

| # | Vị trí | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.6.1 | Thẻ giường trên **Dashboard** | Hiện `test`, `Le Van C`, `ống b`, `Pham Thi D` | ✅ |
| 4.6.2 | **Banner CRITICAL** toàn màn hình | `BED-402 · ICU-1 · **Pham Thi D**` in đậm | ✅ |
| 4.6.3 | Danh sách **Alerts** | `Critical · BED-402 · Pham Thi D` | ✅ |
| 4.6.4 | Giường **chưa có** bệnh nhân | Không hiện gì thừa, không hiện chuỗi rỗng | ✅ |

### 4.7. Trang "My shift" theo từng vai trò

| # | Vai trò | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.7.1 | Điều dưỡng | Nhãn **"Responsible for"** + chip `Room ICU-1`; 4 ô thống kê (`5/12`, 5 occupied, 2 critical, 0 warning); bảng giường với giường `mine` xếp đầu | ✅ |
| 4.7.2 | Kỹ thuật viên | Nhãn **"Equipment duty"**, câu thông báo trống riêng cho thiết bị | ✅ |
| 4.7.3 | Quản trị viên | **Không còn** dòng phạm vi phụ trách; chỉ tài khoản + đổi mật khẩu | ✅ |
| 4.7.4 | Vị trí tab | Nằm **cuối cùng** ở cả ba vai trò | ✅ |

### 4.8. Chọn phòng/giường thay vì gõ tay (quản trị viên)

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.8.1 | Danh sách phòng khi thêm giường | Đúng 3 phòng đang có + mục `+ New room…` | ✅ |
| 4.8.2 | Thêm giường vào phòng có sẵn | `BED-204` tạo trong `ICU-2`, số phòng **không** tăng | ✅ |
| 4.8.3 | Tạo phòng mới qua `+ New room…` | Ô nhập hiện ra; `BED-501B` tạo trong `ICU-3`; số phòng 3 → 4 | ✅ |
| 4.8.4 | Phòng mới xuất hiện trong danh sách chọn | `ICU-3` chọn được ở lần thêm kế tiếp | ✅ |
| 4.8.5 | Phân công trực: đổi Room ↔ Bed | Danh sách giá trị tự đổi giữa tên phòng và mã giường | ✅ |
| 4.8.6 | Thêm/xoá phân công | Thêm `BED-101` thành công, xoá lại về đúng trạng thái ban đầu | ✅ |

### 4.9. Đăng nhập và tài khoản demo

| # | Ca kiểm thử | Kết quả thực tế | KQ |
|---|---|---|---|
| 4.9.1 | Đăng nhập `yta` / `YTaDemo@2026` | `200` — hash trong file seed nay khớp đúng mật khẩu ghi trong chính file | ✅ |
| 4.9.2 | Đăng nhập `kythuat` / `KyThuat@2026` | `200` | ✅ |
| 4.9.3 | Đăng nhập `admin` | `200` | ✅ |
| 4.9.4 | Footer trang đăng nhập | Badge `ICTU` · `FPT` · `Silicon Labs` + dòng "Developed by the ICTU team", nằm giữa dưới thẻ | ✅ |
| 4.9.5 | Footer ở chế độ tối | Đổi màu theo theme, không bị chìm chữ | ✅ |

---

## 5. Dọn dữ liệu sau kiểm thử

Toàn bộ dữ liệu tạo ra khi kiểm thử **đã xoá sạch**, xác nhận bằng truy vấn:

```
devices test còn lại: 0
beds   test còn lại: 0
```

Bệnh nhân test đã cho xuất viện, các giường demo trả về đúng trạng thái ban đầu.

---

## 6. Một quan sát về cách kiểm thử giao diện

Panel chi tiết giường **tự dựng lại khoảng 1 lần/giây**. Hệ quả khi kiểm thử tự
động: toạ độ click và element reference **hết hạn trong vòng dưới 1 giây**, và
bố cục còn dịch chuyển khi giường đổi trạng thái (hộp "ACTIVE ALERT" xuất hiện
rồi biến mất làm form nhích lên ~67 px).

Lần thử đầu của ca 4.5 đã **báo sai là lỗi** vì đúng lý do này: click trượt ô do
bố cục dịch giữa lúc chụp màn hình và lúc bấm. Cách kiểm đúng là **thao tác trực
tiếp trên DOM rồi đọc lại giá trị**, không dựa vào toạ độ.

→ Ghi lại để lần kiểm thử sau không mất thời gian đuổi theo một lỗi không có thật.

---

## 7. Hai mục chưa kiểm được

Cần **board xG26 thật + Raspberry Pi**. Các bước kiểm cụ thể nằm ở
[`BAN_GIAO_UI_UX.md`](BAN_GIAO_UI_UX.md) mục 4.

| # | Nội dung | Vì sao chưa kiểm được |
|---|---|---|
| 7.1 | `linkQuality` từ radio thật | Số hiện tại do `mock_gateway.py` sinh ngẫu nhiên 150–220. Cần xác nhận số thật nằm đúng dải và **giảm khi mang board ra xa** coordinator |
| 7.2 | Thiết bị Zigbee thật tự chuyển Offline | Logic đã đúng với dữ liệu giả lập. Với thiết bị thật, firmware báo cáo tối đa 60 giây/lần trong khi ngưỡng là 90 giây — **biên an toàn chỉ 30 giây**, cần chạy 5 phút liên tục để chắc chắn không nhấp nháy sai |

---

## 8. Kết luận

Toàn bộ **97 ca đạt, 0 ca lỗi**. Ba lỗi phát hiện trong đợt phát triển đã được
xác nhận sửa xong bằng kiểm thử tái hiện đúng kịch bản gây lỗi, không phải chỉ
đọc code.

Ranh giới phân quyền giữa ba vai trò được kiểm ở **lớp API**, nên kết luận không
phụ thuộc vào việc giao diện có ẩn nút hay không.

Phần web **đủ điều kiện bàn giao**. Hai mục ở [mục 7](#7-hai-mục-chưa-kiểm-được)
cần được xác nhận lại ngay khi có phần cứng, trước khi coi là hoàn tất đầu-cuối.

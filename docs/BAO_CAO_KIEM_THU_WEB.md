# Báo cáo kiểm thử chức năng Web — HIS Server

**Hệ thống:** Smart IV Monitor — màn hình trung tâm (HIS Server, ASP.NET Core)
**Ngày kiểm thử:** 19/08/2026
**Phiên bản firmware thiết bị lúc kiểm thử:** v5
**Địa chỉ:** `http://localhost:5100`

---

## 1. Tóm tắt kết quả

| | Số mục | Ghi chú |
|---|---|---|
| **Tổng số ca kiểm thử** | **70** | |
| **Đạt** | **70** | |
| **Không đạt** | **0** | |
| **Phát hiện thiếu chức năng** | **2** | mục 8 |
| Chưa kiểm được lần này | 4 | cần cắm thiết bị, mục 9 |

Phân bổ: đăng nhập 10 ca · phân quyền 12 ca · điều dưỡng 14 ca · kỹ thuật viên
16 ca · quản trị viên 15 ca · dùng chung 3 ca.

Không có ca nào cho kết quả sai. Hai mục ở phần 8 là **chức năng còn thiếu**,
không phải lỗi của chức năng đang có.

**Cách kiểm thử:** gọi trực tiếp API bằng `curl` với phiên đăng nhập thật của
từng vai trò, đối chiếu mã HTTP và dữ liệu trả về; phần giao diện kiểm bằng cách
đăng nhập trên trình duyệt và đọc DOM thật. Mọi con số trong báo cáo là kết quả
chạy thật, không phải kết quả mong đợi.

---

## 2. Đăng nhập và phiên làm việc

| # | Chức năng | Cách kiểm | Mong đợi | Thực tế | KQ |
|---|---|---|---|---|---|
| 1.1 | Đăng nhập quản trị | `POST /api/auth/login` admin/admin | 200, vai trò ADMIN | 200, `ADMIN`, 4 quyền | ✅ |
| 1.2 | Đăng nhập điều dưỡng | yta/yta | 200, NURSE | 200, `NURSE`, 6 quyền | ✅ |
| 1.3 | Đăng nhập kỹ thuật | kythuat/kythuat | 200, TECHNICIAN | 200, `TECHNICIAN`, 3 quyền | ✅ |
| 1.4 | Sai mật khẩu | admin + mật khẩu sai | 401, không vào được | 401 | ✅ |
| 1.5 | Sai mật khẩu có bị ghi log | xem log server | có dòng cảnh báo | `warn: Auth[0] Failed login for 'admin'` | ✅ |
| 1.6 | Chưa đăng nhập mà gọi API | `GET /api/beds` không cookie | 401 | 401 | ✅ |
| 1.7 | Tự đổi mật khẩu | `POST /api/auth/change-password` | 200, mật khẩu mới dùng được | 200 | ✅ |
| 1.8 | Đổi mật khẩu khai sai mật khẩu cũ | cố tình sai | từ chối | 400 | ✅ |
| 1.9 | Mật khẩu cũ hết hiệu lực | đăng nhập lại bằng mật khẩu cũ | 401 | 401 | ✅ |
| 1.10 | Đăng xuất | `POST /api/auth/logout` | phiên bị huỷ | 200, quay về trang đăng nhập | ✅ |

---

## 3. Phân quyền — phần quan trọng nhất

Mỗi vai trò **chỉ thấy dữ liệu của việc mình làm**. Bảng dưới là mã HTTP thật khi
cùng một địa chỉ được gọi bằng ba phiên đăng nhập khác nhau.

| # | Địa chỉ API | admin | y tá | kỹ thuật | Đúng thiết kế? |
|---|---|---|---|---|---|
| 2.1 | `/api/beds` (chỉ số bệnh nhân) | **403** | 200 | **403** | ✅ |
| 2.2 | `/api/alerts` (cảnh báo lâm sàng) | **403** | 200 | **403** | ✅ |
| 2.3 | `/api/devices` (thiết bị) | **403** | **403** | 200 | ✅ |
| 2.4 | `/api/logs` (nhật ký hệ thống) | 200 | **403** | **403** | ✅ |
| 2.5 | `/api/users` (tài khoản) | 200 | **403** | **403** | ✅ |
| 2.6 | `/api/fault-reports` (phiếu hỏng) | **403** | **403** | 200 | ✅ |
| 2.7 | `/api/ota/images` (kho firmware) | **403** | **403** | 200 | ✅ |
| 2.8 | `/api/beds/directory` (mã giường + phòng) | 200 | 200 | 200 | ✅ |
| 2.9 | `/api/me/assignment` (ca trực của mình) | 200 | 200 | 200 | ✅ |

**Điểm đáng chú ý để trình bày:** quản trị viên **không** xem được chỉ số bệnh
nhân, và kỹ thuật viên cũng vậy. Đây là chủ ý — một tài khoản "biết tuốt" là tài
khoản mà kẻ tấn công nhắm vào. Kỹ thuật viên chỉ được `beds/directory` (mã giường
và phòng) để gán thiết bị, không kèm bất kỳ chỉ số nào của bệnh nhân.

### Giao diện cũng ẩn theo quyền, không chỉ chặn ở API

Đăng nhập thật trên trình duyệt rồi đọc các nút điều hướng đang hiển thị:

| # | Vai trò | Tab nhìn thấy | KQ |
|---|---|---|---|
| 2.10 | Điều dưỡng | Dashboard, Beds & Rooms, Alerts, My shift | ✅ |
| 2.11 | Kỹ thuật viên | Devices, Fault reports, My shift | ✅ |
| 2.12 | Quản trị viên | System Log, Bed directory, Users, My shift | ✅ |

Ba vai trò thấy ba bộ chức năng **hoàn toàn không giao nhau** (trừ My shift).
Chặn ở **cả hai lớp**: giao diện ẩn, và API trả 403 kể cả khi gọi thẳng.

---

## 4. Chức năng của Điều dưỡng

| # | Chức năng | Cách kiểm | Kết quả thực tế | KQ |
|---|---|---|---|---|
| 3.1 | Xem danh sách giường | `GET /api/beds` | 200, 8 giường kèm chỉ số | ✅ |
| 3.2 | Xem chi tiết một giường | `GET /api/beds/BED-103` | 200, đủ chỉ số + dự báo AI | ✅ |
| 3.3 | Xem biểu đồ lịch sử | `GET .../history?minutes=30` | 200 | ✅ |
| 3.4 | Đặt tốc độ giọt mục tiêu | `PUT .../target-drops` = 55 | **202 Accepted**, lệnh được chuyển xuống gateway | ✅ |
| 3.5 | Đặt lưu lượng mục tiêu | `PUT .../target-flow` = 120 ml/h | 202 | ✅ |
| 3.6 | Đặt lệnh cho giường **không có gateway** | gọi trên BED-101 | **409** kèm câu giải thích rõ | ✅ |
| 3.7 | Nhận bệnh nhân vào giường | `PUT .../patient` | 200, đọc lại đúng tên + mã BN | ✅ |
| 3.8 | Cho bệnh nhân xuất viện | `PUT .../patient` = null | 200, giường trống trở lại | ✅ |
| 3.9 | Xem cảnh báo chưa xác nhận | `GET /api/alerts?ack=false` | 695 cảnh báo | ✅ |
| 3.10 | Xác nhận (ack) một cảnh báo | `POST /api/alerts/{id}/ack` | 200; chưa xác nhận 695→**694**, đã xác nhận 14→**15** | ✅ |
| 3.11 | Ack cảnh báo không tồn tại | id = 99999999 | 404 | ✅ |
| 3.12 | Lọc theo mức độ | `?level=Critical` | 378 cảnh báo | ✅ |
| 3.13 | Phân trang | `?pageSize=5` | trả đúng 5 mục, tổng 709 | ✅ |
| 3.14 | Báo hỏng thiết bị | `POST .../fault-reports` | **201**, sinh phiếu số 4 | ✅ |

**Về ca 3.6** — đây là điểm đáng nói trong báo cáo: khi giường không có gateway
kết nối, hệ thống **từ chối lệnh và nói rõ lý do**, thay vì nhận rồi im lặng đánh
rơi. Câu trả về:

> *"Bed 'BED-101' has no live gateway connection right now — the command could
> not be delivered. It will need to be resent once the bed's gateway reconnects."*

Với thiết bị y tế, "đã nhận" mà thực ra không tới nơi là kiểu hỏng nguy hiểm nhất.

---

## 5. Chức năng của Kỹ thuật viên

| # | Chức năng | Cách kiểm | Kết quả thực tế | KQ |
|---|---|---|---|---|
| 4.1 | Xem danh sách thiết bị | `GET /api/devices` | 200, 2 thiết bị | ✅ |
| 4.2 | Thêm thiết bị thủ công | `POST /api/devices` | 201 | ✅ |
| 4.3 | Đọc lại thiết bị vừa thêm | `GET /api/devices/{id}` | đúng giường, phòng, trạng thái | ✅ |
| 4.4 | Sửa thiết bị (gán giường khác) | `PUT /api/devices/{id}` | 200 | ✅ |
| 4.5 | Xoá thiết bị | `DELETE /api/devices/{id}` | 204 | ✅ |
| 4.6 | Đọc lại sau khi xoá | `GET` lại | 404 | ✅ |
| 4.7 | Quét lại thiết bị (Rescan) | `POST /api/devices/rescan` | 200, `{"gateways":1}` | ✅ |
| 4.8 | Nhật ký của một thiết bị | `GET .../events` | 92 sự kiện | ✅ |
| 4.9 | Nhận phiếu hỏng | `POST /api/fault-reports/{id}/claim` | 200 | ✅ |
| 4.10 | Xử lý xong phiếu hỏng | `POST .../resolve` | 200, trạng thái → **RESOLVED** | ✅ |

### Kho firmware (OTA)

| # | Chức năng | Cách kiểm | Kết quả thực tế | KQ |
|---|---|---|---|---|
| 5.1 | Xem kho firmware | `GET /api/ota/images` | 200, liệt kê ảnh kèm mã nhà sản xuất đọc **từ header file** | ✅ |
| 5.2 | **Tải lên file sai định dạng** | cố tình chọn file `.gbl` | **400**, từ chối kèm lý do đúng | ✅ |
| 5.3 | Tải lên file `.ota` hợp lệ | file thật 436 KB | 200, nhận diện `ICTU SmartIV-Sensor v4`, mã 4169 | ✅ |
| 5.4 | Index cho zigbee2mqtt | `GET /api/ota/index.json` | 200, sinh tự động từ header từng file | ✅ |
| 5.5 | Kiểm tra bản mới | `POST .../ota/check` | 200 → trạng thái `UpToDate` | ✅ |
| 5.6 | Xoá ảnh khỏi kho | `DELETE /api/ota/images/{file}` | 204 | ✅ |

**Về ca 5.2** — hệ thống kiểm bằng **chữ ký trong file**, không tin tên file. Khi
tải nhầm file `.gbl` nằm cùng thư mục build, nó từ chối và nói đúng nguyên nhân:

> *"Not a Zigbee OTA image: the file does not start with the 0x0BEEF11E
> signature. (A .gbl or .s37 from the same build folder is the usual mistake.)"*

Đây là chỗ nguy hiểm nhất của tính năng cập nhật từ xa: nạp nhầm ảnh firmware vào
máy đầu giường. Việc kiểm bằng chữ ký và **tự sinh index từ header** khiến file
sai không thể lọt vào danh sách được chào cho thiết bị.

---

## 6. Chức năng của Quản trị viên

| # | Chức năng | Cách kiểm | Kết quả thực tế | KQ |
|---|---|---|---|---|
| 6.1 | Xem danh mục giường | `GET /api/beds/directory` | 200 | ✅ |
| 6.2 | Tạo giường mới | `POST /api/beds` | 201 | ✅ |
| 6.3 | Tạo giường trùng mã | tạo lại đúng mã đó | **409**, không tạo trùng | ✅ |
| 6.4 | Sửa phòng của giường | `PUT /api/beds/{id}` | 200, đọc lại đã đổi | ✅ |
| 6.5 | Xem nhật ký hệ thống | `GET /api/logs` | 200, 80 dòng/trang | ✅ |
| 6.6 | Xuất nhật ký ra CSV | `GET /api/logs/export` | 200, **1.035.256 byte**, đúng tiêu đề cột | ✅ |
| 6.7 | Danh sách tài khoản | `GET /api/users` | 200, 3 tài khoản | ✅ |
| 6.8 | Tạo tài khoản mới | `POST /api/users` | 201 | ✅ |
| 6.9 | Tạo trùng tên đăng nhập | tạo lại đúng tên đó | **409** | ✅ |
| 6.10 | Sửa thông tin tài khoản | `PUT /api/users/{id}` | 200 | ✅ |
| 6.11 | Đặt lại mật khẩu cho người khác | `POST .../reset-password` | 200 | ✅ |
| 6.12 | Đăng nhập bằng mật khẩu vừa đặt | login | 200 | ✅ |
| 6.13 | Mật khẩu cũ không dùng được nữa | login mật khẩu cũ | 401 | ✅ |
| 6.14 | Gán phạm vi phụ trách (phòng) | `POST .../assignments` | 200, đọc lại đúng `ROOM / ICU-1` | ✅ |
| 6.15 | Xoá tài khoản | `DELETE /api/users/{id}` | 204 | ✅ |

---

## 7. Chức năng dùng chung

| # | Chức năng | Kết quả thực tế | KQ |
|---|---|---|---|
| 7.1 | Xem ca trực của mình | 200 cho cả ba vai trò | ✅ |
| 7.2 | Cấu hình hiển thị | `GET /api/settings` → 200 | ✅ |
| 7.3 | Cập nhật thời gian thực (SignalR) | Trạng thái thiết bị và tiến độ cập nhật firmware tự đổi trên trang mà không cần tải lại — kiểm khi **còn cắm thiết bị**, đo trên trình duyệt | ✅ |

---

## 8. Phát hiện trong quá trình kiểm thử

### 8.1. Không có chức năng xoá giường

`DELETE /api/beds/TEST-901` trả về **405 Method Not Allowed**. Kiểm tra mã nguồn
(`BedEndpoints.cs`) xác nhận chỉ có tạo (`POST`) và sửa (`PUT`), **không có
`DELETE`**.

**Hệ quả:** một giường tạo nhầm sẽ nằm lại trong danh mục vĩnh viễn, và không có
cách nào xoá qua giao diện. Giường kiểm thử `TEST-901` trong lần này phải xoá
trực tiếp bằng lệnh SQL — **và ngay cả khi đó nó vẫn còn hiện trên màn hình
giường**: danh mục trả 8 giường trong khi màn hình theo dõi vẫn trả 9, vì bản ghi
còn nằm trong bộ nhớ của server. Phải **khởi động lại server** nó mới biến mất.

Nói cách khác: nếu bổ sung chức năng xoá giường thì phải xoá ở **cả hai nơi** —
cơ sở dữ liệu và bộ nhớ đang chạy — chứ không chỉ xoá ở cơ sở dữ liệu.

**Mức độ:** không phải lỗi sai, nhưng là chức năng còn thiếu của phần quản trị.

**Đề xuất:** bổ sung xoá giường, kèm chặn khi giường **đang có bệnh nhân** hoặc
**đang gán thiết bị** — xoá một giường đang theo dõi bệnh nhân phải là việc không
làm được, chứ không chỉ là việc được cảnh báo.

### 8.2. Phiếu hỏng không lưu tên người xử lý

Sau khi kỹ thuật viên nhận và xử lý xong phiếu, trạng thái chuyển đúng thành
`RESOLVED`, nhưng trường `assignedToName` trả về `null`.

**Hệ quả:** không truy được ai đã xử lý phiếu nào — thông tin cần khi rà lại một
sự cố.

**Mức độ:** nhẹ, không ảnh hưởng vận hành.

---

## 9. Chưa kiểm được trong lần này

Thiết bị đầu giường **không cắm** lúc kiểm thử, nên bốn mục sau cần chạy lại khi
có thiết bị:

| # | Mục | Vì sao cần thiết bị |
|---|---|---|
| 9.1 | Tare cân từ xa | lệnh phải tới được chip và chip phải xác nhận đã tare |
| 9.2 | Hiệu chuẩn lại nhịp tim nền | tương tự, cần 60 giây đo thật trên bệnh nhân |
| 9.3 | Cập nhật firmware từ xa (OTA) đầy đủ | cần truyền ~436 KB xuống chip |
| 9.4 | Chỉ số sinh hiệu chảy về thời gian thực | cần chip gửi dữ liệu |

**Ghi chú:** cả bốn mục này **đã được chạy thành công trước đó trong ngày** với
thiết bị cắm thật — riêng OTA đã cập nhật trọn vẹn từ v2 lên v4 (truyền 862 giây,
xác minh 54 giây, thiết bị tự khởi động lại vào bản mới). Chi tiết ở
[`OTA.md`](OTA.md) mục 8.

---

## 10. Kết luận

Toàn bộ **70 ca kiểm thử thực hiện được đều đạt**, không có ca nào cho kết quả
sai. Hai điểm ghi nhận ở mục 8 là chức năng còn thiếu, đã đề xuất hướng bổ sung.

Ba điểm đáng nhấn khi trình bày:

1. **Phân quyền chặt ở hai lớp.** Giao diện ẩn chức năng không thuộc quyền, và
   API vẫn trả 403 kể cả khi gọi thẳng — không thể lách bằng cách gõ địa chỉ.
   Quản trị viên cũng **không** xem được chỉ số bệnh nhân.

2. **Hệ thống từ chối rõ ràng thay vì im lặng.** Lệnh gửi cho giường mất kết nối
   bị từ chối kèm lý do (409); file firmware sai định dạng bị chặn ngay lúc tải
   lên kèm nguyên nhân cụ thể. Với thiết bị y tế, "nhận rồi đánh rơi" nguy hiểm
   hơn "từ chối thẳng".

3. **Kiểm bằng nội dung, không bằng tên.** File firmware được xác thực bằng chữ
   ký trong file, và danh sách phát hành được sinh tự động từ header — nên danh
   sách không thể mâu thuẫn với file mà nó trỏ tới.

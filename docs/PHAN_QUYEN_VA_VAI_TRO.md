# Phân quyền theo vai trò — phân tích nghiệp vụ

Tài liệu này giải thích **vì sao** hệ thống chia quyền như trong Plan v2, đặt
trong bối cảnh một khoa điều trị thật: ai đứng ở đâu, làm gì, vào lúc nào.
Bảng quyền chỉ là kết quả; phần đáng đọc là lập luận dẫn tới nó.

Nguyên tắc xuyên suốt: **quyền đi theo công việc, không đi theo chức danh.**
Một thao tác được giao cho vai trò nào là do *người ở vị trí đó, tại thời điểm
đó, có mặt và có đủ thông tin để làm* — chứ không phải do họ "cấp cao hơn".

---

## 1. Ba vai trò là ba vị trí vật lý khác nhau

Điều dễ bỏ qua nhất khi thiết kế phân quyền là **ba người này không ngồi cùng
chỗ, không có mặt cùng lúc**:

| Vai trò | Ở đâu | Khi nào có mặt | Nhìn màn hình nào |
|---|---|---|---|
| **Điều dưỡng** | Cạnh giường bệnh + trạm điều dưỡng | Trực 24/7, theo ca | Console ở trạm, và màn hình OLED ngay đầu giường |
| **Kỹ thuật viên** | Phòng kỹ thuật, thường ở toà nhà khác | Giờ hành chính; ngoài giờ phải gọi | Máy tính của mình, không ở trong khoa |
| **Quản trị viên** | Phòng IT / phòng quản lý | Giờ hành chính | Máy văn phòng |

Ba hệ quả trực tiếp:

1. **Việc gì phải làm trong vài giây thì phải nằm trong tay điều dưỡng.** Tắc
   dây, tụt SpO2, thay túi dịch — không ai khác kịp có mặt.
2. **Kỹ thuật viên ra quyết định từ xa trước khi đi bộ tới khoa.** Cái họ cần
   không phải "chỉ số bệnh nhân", mà là *"máy nào, ở giường nào, hỏng bộ phận
   gì, có gấp không"*. Thiếu thông tin đó thì họ phải đi tới nơi mới biết có
   cần mang đồ thay thế hay không.
3. **Quản trị viên không tham gia ca lâm sàng.** Họ mở hệ thống khi có người
   mới vào làm, khi đổi ca trực, khi cần đối soát — không phải khi bệnh nhân
   đang truyền dịch.

---

## 2. Vì sao điều dưỡng được làm cả những việc "chỉnh máy"

Nhìn bảng quyền, điều dưỡng có vẻ được nhiều: đặt ngưỡng truyền, tare cân, hiệu
chuẩn lại nhịp tim nền. Lý do không phải "cho tiện", mà là **thứ tự vật lý của
thao tác**:

```
Y tá treo túi dịch  →  cân phải được tare  →  số đo lưu lượng mới đúng
Y tá kẹp cảm biến   →  đo baseline 60 giây →  ngưỡng nhịp tim mới đúng
```

Tare **phải xảy ra ngay sau khi treo túi**, do chính người vừa treo. Nếu thao
tác đó cần quyền cao hơn thì thực tế sẽ diễn ra một trong hai kịch bản, cả hai
đều tệ hơn:

- Y tá gọi điện cho người có quyền, chờ — trong khi ca truyền đã bắt đầu và số
  đo lưu lượng đang sai.
- Hoặc, phổ biến hơn: **cả khoa dùng chung một tài khoản có đủ quyền.** Lúc đó
  toàn bộ phân quyền trở thành vô nghĩa, và nhật ký "ai xác nhận cảnh báo" chỉ
  ghi được một cái tên chung chung.

Đây là bài học kinh điển của phân quyền trong y tế: **quyền quá chặt ở thao tác
thường ngày sẽ tự phá chính nó.** Vì vậy ranh giới được đặt ở chỗ khác:

| Điều dưỡng LÀM được | Điều dưỡng KHÔNG làm được | Vì sao |
|---|---|---|
| Tare cân, hiệu chuẩn baseline | — | Thao tác kỹ thuật gắn liền với động tác chăm sóc |
| Nhập ngưỡng truyền (y lệnh) | Tự quyết định y lệnh | Hệ thống ghi lại *ai nhập*; y lệnh vẫn là của bác sĩ (xem mục 6) |
| Xác nhận cảnh báo | Xoá cảnh báo khỏi lịch sử | Cảnh báo là hồ sơ, chỉ được ghi nhận đã xử lý |
| Nhận / xuất bệnh nhân | Sửa danh mục giường, phòng | Cấu hình khoa là việc quản trị, không phải việc ca trực |
| Báo hỏng thiết bị | Sửa trạng thái thiết bị | Người dùng báo triệu chứng; người sửa mới kết luận |

Dòng cuối là điểm thiết kế đáng chú ý nhất: **y tá mô tả triệu chứng, kỹ thuật
viên kết luận.** Y tá bấm "cảm biến SpO2 không lên số" — họ không biết và không
cần biết là do dây, do module, hay do kẹp sai ngón. Nếu để y tá tự đánh dấu
thiết bị "hỏng"/"đã sửa", danh sách thiết bị sẽ phản ánh phỏng đoán chứ không
phản ánh sự thật kỹ thuật.

---

## 3. Vì sao kỹ thuật viên không nhìn thấy chỉ số bệnh nhân

Đây là thay đổi lớn nhất so với bản đầu, và nó đến từ một câu hỏi rất đơn giản:
**kỹ thuật viên cần con số SpO2 để làm gì?**

Câu trả lời: không để làm gì. Việc của họ là *máy có chạy đúng không*, không
phải *bệnh nhân thế nào*. Cho nên thông tin họ nhận được đổi từ **giá trị** sang
**tình trạng kênh đo**:

| Kỹ thuật viên thấy | Kỹ thuật viên KHÔNG thấy |
|---|---|
| "BED-101: kênh SpO2 mất tín hiệu 12 phút" | "BED-101: SpO2 = 94%" |
| "Sóng Zigbee yếu (linkquality 38/255)" | Nhịp tim, biểu đồ, dự báo AI |
| "Không nhận gói nào 4 phút" | Tên bệnh nhân, mã bệnh nhân |
| "Cân báo 0 g suốt 20 phút dù đang truyền" | Cảnh báo lâm sàng, banner đỏ |

Ba lý do, xếp theo mức độ thuyết phục:

1. **Ít người chạm vào dữ liệu bệnh nhân thì ít chỗ rò.** Nguyên tắc tối thiểu
   quyền hạn; cũng là thứ mọi quy định về dữ liệu y tế đều đòi hỏi.
2. **Màn hình sạch thì chẩn đoán nhanh.** Một trang chỉ có thiết bị và tình
   trạng của chúng dễ đọc hơn nhiều so với trang trộn cả chỉ số sống.
3. **Tránh nhầm vai.** Kỹ thuật viên nhìn thấy "SpO2 88%" sẽ có phản xạ tự nhiên
   là báo động hoặc tự phán đoán — trong khi họ không được đào tạo lâm sàng và
   không ở cạnh bệnh nhân. Không hiển thị là cách chắc chắn nhất để đường xử lý
   lâm sàng đi qua đúng người.

Đổi lại, họ **được thêm** thứ trước đây không có: tình trạng thiết bị suy ra từ
dữ liệu thật (mục 5), thay vì một cột trạng thái do người gõ tay.

---

## 4. Vì sao quản trị viên cũng không nhìn dashboard

Phản xạ thông thường là "admin thấy hết". Trong hệ thống y tế thì đó là thiết
kế sai, vì hai chuyện bị gộp làm một:

- **Quản trị hệ thống**: tài khoản, vai trò, phân ca, danh mục giường phòng.
- **Theo dõi lâm sàng**: chỉ số bệnh nhân, cảnh báo, xu hướng.

Người làm việc thứ nhất không cần việc thứ hai để làm tốt việc của mình. Cho
admin xem toàn bộ chỉ số bệnh nhân chỉ tạo ra một tài khoản "biết tuốt" — đúng
loại tài khoản mà kẻ tấn công nhắm vào, và đúng loại tài khoản mà mọi cuộc kiểm
toán sẽ hỏi "vì sao người này xem được hồ sơ bệnh nhân?".

Admin vẫn có đủ công cụ để dựng và vận hành hệ thống:

| Admin làm | Ghi chú |
|---|---|
| Tạo tài khoản, đặt vai trò, khoá tài khoản | Không xem được mật khẩu ai (hệ thống chỉ lưu hash) |
| Phân công phòng/giường cho từng điều dưỡng | Phân ca, không phải phân quyền xem |
| Tạo giường, sửa phòng | Bảng danh mục: mã giường, phòng, thiết bị gán, có bệnh nhân hay không — **không có chỉ số** |
| Xem System Log | Nhật ký hệ thống; đây là chỗ duy nhất admin gặp dữ liệu lâm sàng, và có lý do (đối soát sự cố) |

---

## 5. "Thiết bị nào hỏng" được xác định thế nào

Yêu cầu "xem thiết bị nào hỏng, thiết bị nào ổn" nghe đơn giản, nhưng hệ thống
hiện tại **không trả lời được**: cột trạng thái trong bảng `devices` do người gõ
tay, không ai kiểm chứng. Vì vậy phần này định nghĩa trạng thái **suy ra từ
chính dòng dữ liệu đang chảy**:

| Trạng thái | Điều kiện | Kỹ thuật viên nên làm gì |
|---|---|---|
| 🟢 Online | Có dữ liệu ≤ 90 giây, mọi kênh đang đọc được | Không làm gì |
| 🟡 Sensor fault | Vẫn gửi dữ liệu, nhưng ≥ 1 kênh mất tín hiệu > 5 phút | Kiểm tra dây/đầu đo kênh đó — máy vẫn sống |
| 🔴 Offline | Không nhận gói nào > 90 giây | Mất nguồn, mất sóng, hoặc chip treo — phải tới nơi |
| ⚪ Pending | Đã join mạng, chưa gán giường | Gán giường cho thiết bị |

Phân biệt 🟡 và 🔴 là phần có giá trị thực tế nhất: **hai tình huống này cần hai
phản ứng khác nhau**. Vàng nghĩa là mang theo dây/cảm biến thay thế. Đỏ nghĩa là
mang theo cả bộ, hoặc kiểm tra nguồn điện và coordinator.

Ngưỡng 90 giây không tự nghĩ ra: nó khớp với `Offline.ThresholdSeconds` của
server, vốn đã khớp với chu kỳ báo cáo tối đa 60 giây của firmware. Một con số,
dùng nhất quán ở cả ba nơi.

---

## 6. Bảy tình huống thực tế

### TH1 — Bắt đầu ca trực
Điều dưỡng đăng nhập, mở **My shift**: mình phụ trách phòng nào, những giường
nào, giường nào đang có bệnh nhân, giường nào đang cảnh báo. Trước đây thông tin
này nằm trong đầu người bàn giao ca.

> Phân công là **bảng phân ca, không phải bộ lọc quyền xem**. Y tá vẫn mở được
> mọi giường trong khoa. Lý do: lúc cấp cứu, người chạy tới đầu tiên không nhất
> thiết là người được phân công giường đó — màn hình trống lúc ấy là nguy hiểm.

### TH2 — Nhận bệnh nhân mới
Điều dưỡng mở giường, nhập tên + mã bệnh nhân, treo túi dịch, **tare cân**, kẹp
cảm biến, chờ 60 giây đo baseline nhịp tim, nhập ngưỡng truyền theo y lệnh. Toàn
bộ chuỗi này một người làm liền mạch, không phải chờ ai.

### TH3 — Cảnh báo tắc dây lúc 2 giờ sáng
Banner đỏ toàn màn hình ở trạm điều dưỡng + đèn/còi ngay đầu giường. Điều dưỡng
tới nơi, xử lý, bấm xác nhận — hệ thống ghi **ai** xác nhận, lúc nào. Kỹ thuật
viên không liên quan và không bị đánh thức: đây là sự cố lâm sàng, không phải sự
cố thiết bị.

### TH4 — Cảm biến SpO2 hỏng thật
1. Kênh SpO2 mất tín hiệu; giường chuyển **Warning** kèm "No signal from: SpO2"
   (không phải "SpO2 0% — nguy kịch").
2. Điều dưỡng kiểm tra: kẹp lại, đổi ngón, vẫn không lên số → bấm **Report
   device problem**, chọn kênh SpO2, ghi "kẹp lại 2 lần vẫn không đọc".
3. Phiếu hiện **ngay lập tức** trong hàng đợi của kỹ thuật viên, kèm mã giường,
   mã thiết bị, và tình trạng máy tự đo được (kênh SpO2 im 12 phút, sóng tốt).
4. Kỹ thuật viên nhận phiếu → mang cảm biến thay thế đúng loại → sửa → đánh dấu
   đã xử lý.
5. Điều dưỡng thấy phiếu chuyển sang "đã xử lý".

> Bước 5 dễ bị coi là thừa nhưng **không được bỏ**: y tá báo xong mà không biết
> có ai nhận không thì lần sau họ sẽ nhấc điện thoại thay vì bấm nút, và hàng
> đợi này chết ngay trong tuần đầu.

### TH5 — Lắp thêm giường mới
Kỹ thuật viên cắm thiết bị, bật permit-join. Thiết bị **tự hiện** trong mục "New
devices waiting to be assigned" với địa chỉ IEEE thật. Chọn giường từ danh sách,
bấm Assign. Không ai gõ địa chỉ nên không ai gõ sai.

### TH6 — Mất điện toàn khoa rồi có điện lại
Mọi thiết bị join lại và tự thông báo. **Những thiết bị đã gán giường giữ nguyên
gán** — nếu bắt gán lại cả khoa sau mỗi lần mất điện thì tính năng này còn tệ
hơn danh sách gõ tay mà nó thay thế. Ngưỡng bác sĩ đặt cũng sống sót vì đã lưu
trong NVM3 của chip.

### TH7 — Nhân sự mới vào làm
Admin tạo tài khoản, chọn vai trò, phân công phòng. Mật khẩu ban đầu do admin
đặt, và **hệ thống bắt đổi ngay lần đăng nhập đầu** — vì mật khẩu admin biết thì
không còn là mật khẩu riêng của ai.

---

## 7. Những chỗ căng và cách xử lý

Một bản phân quyền không nêu chỗ căng là bản chưa dùng thật.

| Tình huống căng | Xử lý |
|---|---|
| **Ngoài giờ, kỹ thuật viên không có mặt, thiết bị hỏng** | Phiếu vẫn ở trạng thái OPEN; hệ thống *không* tự chuyển giường sang trạng thái an toàn giả. Giường vẫn hiện Warning để ca sau thấy. Chuyển giường sang máy khác là quyết định của điều dưỡng trưởng, không phải của phần mềm |
| **Admin cần kiểm chứng "hệ thống có chạy không" mà không được xem dashboard** | Dùng System Log và trang danh mục giường (thấy giường nào đang nhận dữ liệu, không thấy chỉ số) |
| **Tài khoản điều dưỡng bị khoá giữa ca** | Admin mở khoá được ngay; admin **không** thay thế được việc theo dõi, nên đây là thao tác phải làm trong vài phút, không phải vài giờ |
| **Một người kiêm hai vai (khoa nhỏ)** | Cấp hai tài khoản riêng, không cấp một tài khoản gộp quyền. Nhật ký mới nói đúng người làm việc gì |
| **Quyền quá chặt → dùng chung tài khoản** | Đây là rủi ro lớn nhất của mọi phân quyền. Cách phòng: mọi thao tác *thường ngày* của điều dưỡng đều nằm trong quyền điều dưỡng (mục 2) |

---

## 8. Vai trò Bác sĩ — nhận diện sớm một điểm chưa đúng nghiệp vụ

Hiện điều dưỡng nhập ngưỡng truyền dịch. Về nghiệp vụ, **ngưỡng truyền là y
lệnh của bác sĩ**; điều dưỡng là người *thực hiện* y lệnh, không phải người ra.
Hệ thống đang gộp hai việc đó vì chưa có vai trò Bác sĩ.

Chấp nhận được ở giai đoạn này (ngưỡng vẫn được ghi nhận *ai nhập, lúc nào*),
nhưng khi thêm vai trò Bác sĩ thì tách như sau:

| | Bác sĩ | Điều dưỡng |
|---|---|---|
| Ra y lệnh (ml/h, giọt/phút) | ✅ | ❌ |
| Thực hiện, tare, kẹp cảm biến | ❌ | ✅ |
| Xem chỉ số, biểu đồ, dự báo AI | ✅ | ✅ |
| Xác nhận cảnh báo | ✅ | ✅ |

Bảng năng lực trong `Domain/Capabilities.cs` đã thiết kế sẵn cho việc này: thêm
vai trò là thêm một dòng cho mỗi năng lực, ở một file duy nhất.

---

## 9. Tóm tắt: mỗi vai trò trả lời một câu hỏi

| Vai trò | Câu hỏi họ mở hệ thống để trả lời |
|---|---|
| **Điều dưỡng** | "Bệnh nhân của tôi có ổn không, và có gì cần xử lý ngay?" |
| **Kỹ thuật viên** | "Máy nào đang hỏng, hỏng bộ phận gì, có cần đi ngay không?" |
| **Quản trị viên** | "Ai được dùng hệ thống, với vai trò gì, phụ trách khu vực nào?" |

Nếu một màn hình không giúp trả lời câu hỏi của vai trò đang đăng nhập, nó không
nên xuất hiện trên màn hình đó.

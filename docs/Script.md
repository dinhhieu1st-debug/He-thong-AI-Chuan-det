# **SMART IV MONITORING SYSTEM**

## **“KHI CÔNG NGHỆ BIẾT QUAN SÁT”**

**Thời lượng:** 8–9 phút  
**Hình thức:** Phim ngắn kết hợp hoạt hình AI \+ quay thực tế sản phẩm \+ đồ họa công nghệ  
**Phong cách:** Điện ảnh, hiện đại, cảm xúc, công nghệ, dễ hiểu với giám khảo

### **Chú giải định dạng hình ảnh (áp cho từng đoạn bên dưới)**

| Ký hiệu | Nghĩa | Khi nào dùng |
|---|---|---|
| 🎨 **AI HOẠT HÌNH** | Dựng bằng AI tạo sinh | Chỉ cho phần **kể chuyện/cảm xúc**: bệnh viện, An, Lan, nhân vật IVI, các cảnh chuyển tiếp mang tính minh họa, không phải bằng chứng kỹ thuật |
| 🎥 **QUAY THẬT** | Camera quay người thật hoặc phần cứng/màn hình thật | Bắt buộc cho **mọi thứ chứng minh hệ thống hoạt động**: board, cảm biến, LED, terminal, dashboard, thao tác trên UI, thành viên team |
| 📊 **ĐỒ HỌA DỰNG TAY** | Motion graphics/diagram dựng thủ công (không AI) | Cho sơ đồ kỹ thuật (luồng dữ liệu, kiến trúc, pipeline AI, cluster Zigbee) — đây vẫn là nội dung kỹ thuật nên **không giao cho AI vẽ**, chỉ dựng bằng công cụ đồ họa/animation thông thường dựa trên số liệu thật |

**Nguyên tắc:** một khi lời thoại đang khẳng định điều gì đó về hệ thống (số liệu, trạng thái, kết nối), hình ảnh minh họa cho câu đó phải là 🎥 hoặc 📊 — không phải 🎨. AI hoạt hình chỉ phục vụ mạch cảm xúc của câu chuyện.

**Chỉ cần phần kỹ thuật để tự quay?** Xem `docs/Quay_Ky_Thuat.md` — shot list tách riêng 🎥+📊, bỏ hết nhân vật/hoạt hình, tổ chức theo buổi quay thực tế thay vì theo thứ tự cảnh phim.

---

# **I. Ý TƯỞNG TỔNG THỂ**

Câu chuyện xoay quanh **một bệnh nhân đang truyền dịch** và **một điều dưỡng phải theo dõi nhiều bệnh nhân cùng lúc**.

Ban đầu mọi thứ bình thường.

Nhưng khi điều dưỡng rời khỏi giường bệnh, một sự cố xảy ra: **tín hiệu cảm biến SpO₂ bị mất**.

Nếu không có hệ thống thông minh, điều dưỡng có thể không nhận biết ngay.

Smart IV Monitoring System xuất hiện như một “người trợ lý số”.

Hệ thống:

**CẢM NHẬN → PHÂN TÍCH → TRUYỀN DỮ LIỆU → CẢNH BÁO → HỖ TRỢ ĐIỀU DƯỠNG**

Toàn bộ câu chuyện được kể bằng **nhân vật hoạt hình**, sau đó chuyển sang **sản phẩm thật của Team ICTU** để chứng minh hệ thống thực sự hoạt động.

---

# **II. NHÂN VẬT**

### **Nhân vật 1 – AN**

Một bệnh nhân trẻ, thân thiện.

Vai trò: giúp khán giả nhìn thấy vấn đề từ góc độ người bệnh.

### **Nhân vật 2 – LAN**

Điều dưỡng.

Là nhân vật trung tâm của câu chuyện.

Cô phải theo dõi nhiều bệnh nhân cùng lúc.

### **Nhân vật 3 – IVI**

Trợ lý AI của Smart IV Monitoring System.

Có thể thiết kế như một nhân vật AI nhỏ xuất hiện trên màn hình/dashboard.

IVI không phải robot vật lý mà là **hình ảnh nhân hóa của hệ thống AI**.

### **Nhân vật 4 – TEAM ICTU**

Các thành viên thật của đội.

Xuất hiện ở đầu và cuối phim, đồng thời xuất hiện trong các cảnh chế tạo, lập trình và kiểm thử sản phẩm.

---

# **PHẦN I – DẪN CHUYỆN**

*Cảnh 1–4: mở vấn đề bằng nhân vật, sau đó bắc cầu sang Team ICTU thật.*

---

# **CẢNH 1 – MỞ ĐẦU: MỘT NGÀY BÌNH THƯỜNG**

**Thời lượng: 45 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (toàn cảnh — thuần cảm xúc/kể chuyện, chưa có số liệu cần chứng minh)**

### **Hình ảnh**

Hoạt hình AI.

Một bệnh viện hiện đại vào buổi sáng.

Camera đi qua hành lang.

Trong phòng bệnh có nhiều bệnh nhân.

Một bệnh nhân tên **AN** đang nằm trên giường.

Một túi truyền dịch đang nhỏ giọt.

Điều dưỡng **LAN** bước vào.

### **Lan**

> “Chào anh An. Tôi sẽ kiểm tra tình trạng truyền dịch của anh.”

Lan kiểm tra túi truyền.

Mọi thứ bình thường.

Trên màn hình nhỏ:

**HR: 78 BPM**

**SpO₂: 97%**

**DRIP: NORMAL**

Lan mỉm cười.

### **Lan**

> “Mọi thứ đều ổn.”

Lan đi sang giường bệnh khác.

Camera lùi ra xa.

Hiện cảnh Lan phải theo dõi nhiều bệnh nhân.

---

# **CẢNH 2 – VẤN ĐỀ XUẤT HIỆN**

**Thời lượng: 40–50 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (toàn cảnh — kịch tính hoá, "SpO₂: LOST" trên màn hình nhỏ chỉ là đạo cụ trong khung cảnh hoạt hình, không phải bằng chứng kỹ thuật nên vẫn để AI dựng)**

### **Hình ảnh**

An vẫn nằm trên giường.

Túi truyền tiếp tục nhỏ giọt.

Lan đang kiểm tra một bệnh nhân khác.

Đồng hồ chạy nhanh.

Hoạt hình mô phỏng thời gian trôi.

Bất ngờ:

**Tín hiệu SpO₂ bị gián đoạn.**

Màn hình nhỏ bên cạnh bệnh nhân nhấp nháy.

**SpO₂: LOST**

Nhưng Lan không nhìn thấy ngay.

Âm thanh cảnh báo rất nhỏ.

Camera zoom vào khuôn mặt An.

### **An**

> “Chị Lan ơi…”

Không có phản hồi ngay.

Màn hình chuyển sang màu đỏ.

Dừng hình.

### **Voice-over**

> “Trong môi trường bệnh viện, một điều dưỡng có thể phải theo dõi nhiều bệnh nhân cùng lúc.”

> “Một thay đổi nhỏ có thể xảy ra bất cứ lúc nào.”

> “Nhưng liệu chúng ta có thể phát hiện mọi bất thường ngay khi nó xảy ra?”

Màn hình tối.

Một câu xuất hiện:

# **“VẬY NẾU HỆ THỐNG CÓ THỂ TỰ PHÁT HIỆN?”**

---

# **CẢNH 3 – SMART IV MONITORING SYSTEM XUẤT HIỆN**

**Thời lượng: 35–40 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (IVI xuất hiện) → 🎥 QUAY THẬT/dựng logo (chuyển cảnh sang thật)**

### **Hình ảnh**

🎨 Một tia sáng công nghệ xuất hiện.

🎨 Nhân vật **IVI – trợ lý AI** xuất hiện.

🎨 IVI nhìn về phía An.

### **IVI**

> “Tôi có thể giúp.”

🎥 Màn hình chuyển từ hoạt hình sang hình ảnh thật.

Logo:

# **SMART IV MONITORING SYSTEM**

Sau đó hiện:

**Team ICTU**

### **Voice-over**

> “Đây là Smart IV Monitoring System – giải pháp giám sát truyền dịch thông minh được phát triển bởi Team ICTU.”

---

# **CẢNH 4 – GIỚI THIỆU TEAM ICTU**

**Thời lượng: 35–40 giây**

**Định dạng: 🎥 QUAY THẬT toàn cảnh — người thật, không dùng AI**

### **Hình ảnh thực tế**

Quay từng thành viên.

Không đứng đọc tên đơn thuần.

Mỗi thành viên gắn với một công việc:

* Lập trình.  
* Thiết kế phần cứng.  
* Kiểm tra cảm biến.  
* Phát triển AI.  
* Xây dựng dashboard.  
* Kiểm thử hệ thống.

### **Voice-over**

> “Chúng tôi là Team ICTU – một nhóm sinh viên cùng chung niềm đam mê với IoT, trí tuệ nhân tạo và công nghệ nhúng.”

Một thành viên nhìn camera:

> “Chúng tôi muốn tạo ra một sản phẩm không chỉ thông minh…”

Thành viên thứ hai:

> “…mà còn giải quyết một vấn đề thực tế.”

Cả đội:

> “Đó là Smart IV Monitoring System.”

---

# **PHẦN II – KỸ THUẬT: SƠ ĐỒ HỆ THỐNG, LUỒNG TÍN HIỆU & LUỒNG SỬ DỤNG**

*Cảnh 5–11: sơ đồ kiến trúc → cảm biến/AI edge → Zigbee → dashboard → cấu hình từ xa → OTA. Đây là phần "how it works", xen kẽ 📊 sơ đồ dựng tay và 🎥 demo thật.*

---

# **CẢNH 5 – HỆ THỐNG HOẠT ĐỘNG NHƯ THẾ NÀO?**

**Thời lượng: 55 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (IVI dẫn chuyện) + 📊 ĐỒ HỌA DỰNG TAY (sơ đồ kiến trúc BỆNH NHÂN→…→ĐIỀU DƯỠNG là nội dung kỹ thuật thật, không giao AI vẽ, dựng bằng motion graphics theo đúng kiến trúc trong `gateway/`, `server/`)**

### **Hình ảnh**

🎨 Quay trở lại hoạt hình.

🎨 IVI đứng giữa màn hình.

🎨 Bốn biểu tượng xuất hiện.

### **IVI**

> “Để giúp điều dưỡng, tôi cần biết điều gì đang xảy ra.”

Một cảm biến xuất hiện bên cạnh bệnh nhân.

### **IVI**

> “Đầu tiên, tôi cảm nhận.”

**SENSOR**

↓

### **IVI**

> “Sau đó, tôi phân tích.”

**EDGE AI**

↓

### **IVI**

> “Tiếp theo, tôi kết nối.”

**ZIGBEE**

↓

### **IVI**

> “Và cuối cùng, tôi thông báo.”

**DASHBOARD**

📊 Đồ họa (dựng tay, không AI):

**BỆNH NHÂN**

↓

**CẢM BIẾN**

↓

**xG26 \+ EDGE AI**

↓

**ZIGBEE**

↓

**RASPBERRY PI**

↓

**HIS SERVER**

↓

**ĐIỀU DƯỠNG**

### **Voice-over**

> “Từ giường bệnh đến màn hình điều dưỡng, toàn bộ dữ liệu được kết nối theo thời gian thực.”

---

# **CẢNH 6 – “TÔI CÓ THỂ NHÌN THẤY”**

**Thời lượng: 60 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (mở đầu, IVI dẫn) → 🎥 QUAY THẬT (toàn bộ phần cận cảnh phần cứng + LED — bắt buộc quay thật, đây là bằng chứng kỹ thuật)**

### **Hình ảnh hoạt hình** 🎨

IVI nhìn vào bệnh nhân An.

Các dữ liệu xuất hiện xung quanh:

**Heart Rate**

**SpO₂**

**Drip Rate**

IVI:

> “Tôi có thể theo dõi nhiều tín hiệu cùng lúc.”

Chuyển sang **hình ảnh sản phẩm thật**.

### **Quay cận** 🎥 QUAY THẬT

**EFR32xG26**

Sau đó:

**MAX30102**

Ngón tay đặt lên cảm biến.

Màn hình:

**HR \= 78**

**SpO₂ \= 97%**

Chuyển sang:

**Load Cell**

Túi truyền được treo lên.

Chuyển sang:

**Drop Sensor**

Màn hình:

**DROP RATE ≈ 98 giọt/phút**

Sau đó quay LED thật (xanh/vàng/đỏ trên board, theo đúng `firmware/sensor_hub.c` — không dựng LED bằng đồ họa).

### **Voice-over**

> “Hệ thống sử dụng EFR32xG26 làm bộ xử lý trung tâm.”

> “MAX30102 theo dõi nhịp tim và SpO₂.”

> “Load Cell theo dõi lượng và tốc độ truyền dịch.”

> “Drop Sensor theo dõi tốc độ nhỏ giọt — ngưỡng nhận diện được đo trực tiếp trên chính cảm biến, không sao chép từ tài liệu tham khảo.”

Ba LED xuất hiện:

**XANH – ỔN ĐỊNH**

**VÀNG – CẢNH BÁO ĐƯỜNG TRUYỀN**

**ĐỎ (đứng yên) – CẢNH BÁO BỆNH NHÂN**

**ĐỎ (nhấp nháy) – NGUY HIỂM (cả hai cùng lúc)**

Mỗi lúc chỉ một đèn sáng — mức nguy hiểm nhất không cộng thêm đèn, mà đổi nhịp nháy/còi để không bị đọc nhầm thành "hai lỗi riêng biệt" (`firmware/sensor_hub.c`).

### **IVI**

> “Tôi không chỉ nhìn thấy dữ liệu.”

> “Tôi còn biết khi nào dữ liệu trở nên bất thường.”

---

# **CẢNH 7 – “TÔI CÓ THỂ SUY NGHĨ”**

**Thời lượng: 70 giây**

**Định dạng: 📊 ĐỒ HỌA DỰNG TAY (biểu đồ HR + sơ đồ 3 mô hình AI — số liệu thật từ `firmware/ai_engine.h`/`ai_fusion.c`, không giao AI vẽ) → 🎥 QUAY THẬT (code, serial terminal, chip)**

### **Hình ảnh** 📊 (đồ họa dựng tay)

Dữ liệu HR chạy liên tục:

**76 → 77 → 79 → 81 → 84**

Các điểm dữ liệu tạo thành đường biểu đồ.

IVI nhìn biểu đồ.

### **IVI**

> “Nhịp tim đang thay đổi…”

Biểu đồ chuyển thành sơ đồ 3 mô hình AI độc lập.

### **Voice-over**

> “Đây là điểm khác biệt quan trọng của hệ thống.”

> “AI được đưa trực tiếp xuống thiết bị.”

Chuyển sang quay thật: 🎥 QUAY THẬT

* Code.  
* Serial Terminal.  
* Chip xG26.

### **Voice-over**

> “Mô hình AI chạy trực tiếp trên xG26 bằng TensorFlow Lite Micro và bộ tăng tốc phần cứng.”

> “Không phải một, mà là ba mô hình nhỏ chạy độc lập — mỗi mô hình một bộ nhớ riêng, để một mô hình lỗi không kéo sập cả hai mô hình còn lại.”

> “Hai mô hình 1D-CNN phân tích cửa sổ 64 giây — một cho nhịp tim và SpO₂, một cho tốc độ nhỏ giọt — và dự báo xu hướng 16 giây tiếp theo.”

> “Mô hình thứ ba nhỏ hơn nhiều, không cần cửa sổ dữ liệu — chỉ nhìn vào khoảnh khắc hiện tại để phát hiện điều gì đó bất thường mà hai mô hình kia bỏ sót.”

Hiện ba khả năng:

### **PHÁT HIỆN XU HƯỚNG** *(2 mô hình dự báo 64s→16s)*

### **CẢNH BÁO SỚM** *(cùng 2 mô hình dự báo)*

### **PHÁT HIỆN BẤT THƯỜNG** *(3 mô hình cộng lại — kể cả mô hình tức thời thứ ba)*

### **IVI**

> “Tôi không chờ sự cố xảy ra rồi mới phản ứng.”

> “Tôi tìm kiếm dấu hiệu của sự cố từ trước.”

---

# **CẢNH 8 – “TÔI CÓ THỂ KẾT NỐI”**

**Thời lượng: 50 giây**

**Định dạng: 📊 ĐỒ HỌA DỰNG TAY (sơ đồ tín hiệu xG26→Zigbee→RPi) → 🎥 QUAY THẬT (Raspberry Pi, terminal JSON thật) → 📊 ĐỒ HỌA DỰNG TAY (bảng attribute cluster, lấy đúng tên từ `autogen/zap-config.h`)**

### **Hình ảnh** 📊 (đồ họa dựng tay)

Từ board cảm biến xuất hiện tín hiệu không dây.

Tín hiệu truyền:

**xG26**

↓

**ZIGBEE**

↓

**RASPBERRY PI**

### **IVI**

> “Dữ liệu cần được truyền đi.”

> “Vì vậy, tôi sử dụng Zigbee.”

🎥 Chuyển sang quay thật Raspberry Pi.

Hiển thị terminal thật (không dựng lại bằng đồ họa — quay trực tiếp màn hình chạy `gateway/main.c`).

Dữ liệu chạy (JSON, mỗi bản tin một dòng, đúng như gateway thật in ra):

```
{"bedId":"BED-101","spo2":97,"heartRate":78,"dropsPerMin":98,
 "targetDropsPerMin":50,"aeAlarm":false}
```

### **Voice-over**

> “Hệ thống sử dụng Zigbee với khả năng tiết kiệm năng lượng và hỗ trợ mạng mesh.”

📊 Hiện Custom Cluster (đồ họa dựng tay, dữ liệu lấy từ code thật):

**SMART IV VITALS**

**0xFC01**

**HeartRate**

**SpO₂**

**DropsPerMin**

**TargetDropsPerMin**

**AlarmBitmap**

**… và hơn 20 attribute khác** (trọng lượng túi, dự báo AI, ngưỡng cấu hình…)

### **Voice-over**

> “Chúng tôi xây dựng một Custom Zigbee Cluster dành riêng cho dữ liệu của hệ thống Smart IV.”

---

# **CẢNH 9 – “TÔI CÓ THỂ BÁO CHO ĐIỀU DƯỠNG”**

**Thời lượng: 60 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (cảnh phòng điều dưỡng, chỉ mang tính kể chuyện) → 🎥 QUAY THẬT (toàn bộ phần Dashboard — đây là bằng chứng kỹ thuật, không được thay bằng mockup AI)**

### **Hình ảnh hoạt hình** 🎨

Một phòng điều dưỡng.

Màn hình lớn hiện nhiều giường bệnh:

🟢 BED 101 – STABLE  
🟢 BED 102 – STABLE  
🟡 BED 103 – WARNING  
🔴 BED 104 – CRITICAL

Điều dưỡng Lan nhìn màn hình.

### **Lan**

> “Tôi biết ngay giường nào cần được kiểm tra.”

🎥 Chuyển sang Dashboard thật (quay màn hình thật, không dựng lại giao diện).

Click **BED-101**.

Hiện:

* HR  
* SpO₂  
* Drip  
* biểu đồ  
* trạng thái cảm biến.

### **Voice-over**

> “HIS Dashboard cung cấp cho điều dưỡng một góc nhìn tổng thể về toàn bộ hệ thống.”

> “Điều dưỡng không cần kiểm tra từng cảm biến một cách thủ công.”

> “Chỉ với một màn hình, họ có thể biết giường nào ổn định, giường nào cần chú ý và giường nào đang ở trạng thái nguy hiểm.”

---

# **CẢNH 10 – KHẢ NĂNG TÙY CHỈNH**

**Thời lượng: 40 giây**

**Định dạng: 🎥 QUAY THẬT (thao tác Dashboard) → 📊 ĐỒ HỌA DỰNG TAY (sơ đồ đường đi lệnh xuống thiết bị)**

### **Hình ảnh** 🎥 QUAY THẬT

Quay Dashboard thật.

Điều dưỡng Lan chọn:

**Target Drops Per Minute**

Hiện:

**20**

Lan thay đổi:

**50**

Nhấn:

**SAVE**

### **Hình ảnh** 📊 (đồ họa dựng tay — không phải hoạt hình AI, vì đây là mô tả đúng đường đi lệnh thật trong hệ thống)

Một tín hiệu chạy:

**DASHBOARD**

↓

**SERVER**

↓

**GATEWAY**

↓

**ZIGBEE**

↓

**xG26**

### **Voice-over**

> “Hệ thống cũng cho phép thay đổi cấu hình từ xa.”

### **Lan**

> “Tôi thay đổi mục tiêu từ 20 thành 50 giọt mỗi phút.”

### **IVI**

> “Đã cập nhật.”

### **Voice-over**

> “Lệnh được truyền đến thiết bị mà không cần nạp lại firmware và không cần khởi động lại hệ thống.”

---

# **CẢNH 11 – CẬP NHẬT FIRMWARE TỪ XA (OTA)**

**Thời lượng: 55 giây**

**Định dạng: 🎥 QUAY THẬT toàn cảnh (thao tác web thật + board thật khởi động lại) — không có nhân vật hoạt hình, đây thuần là bằng chứng kỹ thuật**

### **Hình ảnh** 🎥 QUAY THẬT

Vẫn ở Dashboard thật, nhưng chuyển sang tài khoản **kỹ thuật viên** (`kythuat`).

Trang **Devices**, đầu trang có khối **Kho firmware (OTA)**: bảng File / Device / Version / Size, nút **Upload**.

Kỹ thuật viên chọn file `.ota` → **Upload**. Bảng cập nhật ngay dòng mới, kèm mã nhà sản xuất đọc trực tiếp từ file (không phải từ tên file).

Kéo xuống khối **FIRMWARE** của từng thiết bị.

Kỹ thuật viên bấm **Check for update**.

Vài giây sau, thẻ đổi trạng thái:

**Update available**

Bấm **Update** → hộp thoại xác nhận.

Thanh tiến độ chạy % + thời gian còn lại, kèm dòng nhắc không rút nguồn thiết bị.

*(Ghi chú quay: nạp thật mất khoảng 14 phút cho ảnh ~430KB — đo thật trên board, xem `docs/OTA.md` mục 8. Quay timelapse hoặc cắt nhiều đoạn ngắn rồi dựng nhanh, không dựng giả bằng cách tua nhanh toàn bộ thanh %.)*

Chuyển sang cận cảnh board thật: thiết bị **tự khởi động lại** (không rút nguồn), màn OLED sáng lại ở bản mới.

Cắt về Devices: khối **Device log** hiện dòng mới, đúng chữ hệ thống sinh ra:

**"Update started: v2 to v4"** → sau khi xong → **"Updated: v2 to v4"**

### **Voice-over**

> “Thiết bị đầu giường cũng có thể được cập nhật firmware từ xa — kể cả việc đưa bản firmware mới lên hệ thống.”

> “Kỹ thuật viên tải file firmware thẳng qua trang web, không cần SSH vào máy chủ, không cần đến tận giường bệnh.”

> “Server tự đọc file để xác minh đúng loại thiết bị trước khi chấp nhận — chọn nhầm file thì bị từ chối ngay lúc tải lên, không đợi đến lúc nạp mới báo lỗi.”

> “Toàn bộ quá trình nạp được theo dõi bằng thanh tiến độ thời gian thực và ghi lại thành lịch sử — kể cả những lần thất bại — để không có thiết bị nào bị nạp dở firmware mà không ai biết.”

### **IVI**

> “Ngay cả tôi cũng có thể được nâng cấp — mà không ai phải rời khỏi bàn làm việc.”

---

# **PHẦN III – DEMO THẬT: CAO TRÀO**

*Cảnh 12–13: toàn bộ chuỗi phản ứng thật của hệ thống trước một sự cố thật, không có số liệu dàn dựng.*

---

# **CẢNH 12 – THỬ THÁCH: NẾU CẢM BIẾN MẤT TÍN HIỆU?**

**Thời lượng: 70 giây**

Đây là **cao trào của bộ phim**.

**Định dạng: 🎨 AI HOẠT HÌNH (nhịp cảm xúc: An, Lan, khoảnh khắc ngón tay rời cảm biến) xen kẽ chặt với 🎥 QUAY THẬT (mọi con số/trạng thái/LED/Dashboard đều phải là bằng chứng thật, kể cả trong đoạn cao trào — không được dùng số liệu hoạt hình giả)**

### **Hình ảnh** 🎨 (nhân vật) / 🎥 (mọi con số + phần cứng)

🎨 Trở lại nhân vật An.

Mọi thứ bình thường.

Dashboard: 🎥 (màn hình dashboard thật, số liệu thật)

🟢 **BED-101 – STABLE**

HR: 78

SpO₂: 97

Drip: ~98 giọt/phút

### **Lan**

> “Mọi thứ đang ổn định.”

🎨 Lan rời khỏi giường.

🎨 Camera giữ lại An.

🎥 Sau đó **ngón tay rời khỏi MAX30102** (đây là thao tác thật trên phần cứng thật — bắt buộc quay thật để cảnh báo phía sau là bằng chứng, không phải kịch bản).

Không nhạc.

Không lời thoại.

Chỉ nghe tiếng máy.

### **3 giây sau** 🎥 QUAY THẬT (toàn bộ đoạn này, không hoạt hình)

Cận cảnh board thật.

LED thật:

🟢 → 🟡

Âm thanh thật:

**BEEP**

Cắt sang Dashboard thật.

**BED-101**

**STABLE → WARNING**

SpO₂:

**97 → LOST**

Alert xuất hiện (đúng chữ server sinh ra — `VitalsStatusEvaluator.cs`, mã `SENSOR_DISCONNECTED`):

**"No signal from: SpO2"**

### **IVI xuất hiện**

> “Phát hiện mất tín hiệu cảm biến.”

🎨 Cắt sang Lan.

🎥 Điện thoại hiện **push notification** (FCM — `server/src/HisServer/Services/FcmPushService.cs`, tính năng có thật), nội dung cảnh báo phải khớp đúng dữ liệu đang chạy thật trên Dashboard, không dựng riêng.

🎨 Lan quay lại.

### **Lan**

> “Tôi nhận được cảnh báo.”

Lan kiểm tra bệnh nhân.

> *Ghi chú kỹ thuật (không đưa vào lời thoại/demo):* kênh báo động qua **SMS hoặc gọi điện** khi cảnh báo kéo dài không được xác nhận là hướng mở rộng, **hệ thống hiện chưa làm** — chỉ nhắc ở phần "định hướng phát triển" (Cảnh 14) nếu cần, tuyệt đối không dựng cảnh giả lập SMS/cuộc gọi vì đây không phải tính năng thật.

🎥 Sau đó tín hiệu được khôi phục (đặt ngón tay lại — thật, đo lại thật).

Dashboard thật:

🟡 → 🟢

### **IVI**

> “Hệ thống đã trở lại trạng thái ổn định.”

---

# **CẢNH 13 – TOÀN BỘ HỆ THỐNG CÙNG PHẢN ỨNG**

**Thời lượng: 35–40 giây**

**Định dạng: 🎥 QUAY THẬT (montage cắt nhanh từ các footage phần cứng/dashboard đã quay ở Cảnh 6–12, dựng lại — không tạo mới bằng AI) + 📊 ĐỒ HỌA DỰNG TAY (nhãn tên khối và mũi tên nối giữa các đoạn footage)**

### **Hình ảnh** 🎥 + 📊

Dựng montage cực nhanh (cắt từ footage thật đã quay, không dựng cảnh mới):

**CẢM BIẾN**

→ LED đổi màu

→ **EDGE AI**

→ phát hiện bất thường

→ **ZIGBEE**

→ truyền dữ liệu

→ **RASPBERRY PI**

→ **HIS SERVER**

→ **DASHBOARD**

→ **ĐIỀU DƯỠNG**

Sau đó màn hình dừng.

Hiện bốn từ:

# **CẢM NHẬN**

# **SUY NGHĨ**

# **KẾT NỐI**

# **PHẢN HỒI**

### **Voice-over**

> “Một sự cố xảy ra tại giường bệnh.”

> “Cảm biến phát hiện.”

> “AI phân tích.”

> “Zigbee truyền dữ liệu.”

> “Server cập nhật.”

> “Dashboard cảnh báo.”

> “Và điều dưỡng phản ứng.”

---

# **PHẦN IV – CÂU CHUYỆN TIẾP TỤC**

*Cảnh 14–15: quay lại mạch cảm xúc, chốt giá trị sản phẩm và thông điệp của Team ICTU.*

---

# **CẢNH 14 – TEAM ICTU VÀ GIÁ TRỊ SẢN PHẨM**

**Thời lượng: 40–45 giây**

**Định dạng: 🎥 QUAY THẬT (chính — người thật, thao tác thật) xen kẽ 🎨 AI HOẠT HÌNH (chỉ đoạn "bệnh viện tương lai", vì đây là tầm nhìn/minh họa, không phải chứng minh kỹ thuật)**

### **Hình ảnh thực tế** 🎥 QUAY THẬT

Các thành viên:

* Lắp mạch.  
* Hàn dây.  
* Lập trình.  
* Kiểm tra cảm biến.  
* Kiểm thử AI.  
* Kiểm tra dashboard.  
* Trao đổi nhóm.

🎨 Xen kẽ một số cảnh hoạt hình về bệnh viện tương lai (minh họa tầm nhìn — không kèm số liệu/tính năng cụ thể để tránh gây hiểu nhầm là demo thật).

### **Voice-over**

> “Smart IV Monitoring System không chỉ là một thiết bị cảm biến.”

> “Đó là sự kết hợp giữa IoT, Edge AI, hệ thống nhúng, kết nối không dây và quản lý dữ liệu thời gian thực.”

Hiện:

**REAL-TIME**

**EDGE AI**

**ZIGBEE**

**SMART MONITORING**

**SCALABLE**

### **Voice-over**

> “Chúng tôi hướng tới một hệ thống có thể mở rộng, tùy biến và ứng dụng trong môi trường thực tế.”

*(Tuỳ chọn, chỉ nếu cần liệt kê roadmap — không bắt buộc):* “Trong tương lai, hệ thống có thể mở rộng thêm kênh cảnh báo qua SMS hoặc cuộc gọi khi cảnh báo không được xác nhận sau một khoảng thời gian.” — **đây là hướng phát triển, hệ thống hiện chưa có**, không được nói như tính năng đã hoàn thiện.

---

# **CẢNH 15 – THÔNG ĐIỆP CUỐI**

**Thời lượng: 35–40 giây**

**Định dạng: 🎨 AI HOẠT HÌNH (An, Lan, IVI) → 🎥 QUAY THẬT (Team ICTU + sản phẩm thật)**

### **Hình ảnh** 🎨 → 🎥

🎨 An nằm trên giường.

🎨 Lan kiểm tra hệ thống.

🎨 IVI đứng bên cạnh.

🎥 Sau đó chuyển sang Team ICTU.

🎥 Các thành viên đứng cùng sản phẩm thật.

Một thành viên bước lên:

> “Công nghệ không chỉ để thu thập dữ liệu.”

Thành viên thứ hai:

> “Công nghệ cần hiểu dữ liệu.”

Thành viên thứ ba:

> “Phát hiện vấn đề.”

Thành viên thứ tư:

> “Và hỗ trợ con người đưa ra quyết định tốt hơn.”

Cả đội cùng nói:

> **“Đó là lý do chúng tôi tạo ra Smart IV Monitoring System.”**

Màn hình đen.

Logo:

# **TEAM ICTU**

## **SMART IV MONITORING SYSTEM**

### **CẢM NHẬN – SUY NGHĨ – KẾT NỐI – PHẢN HỒI**

Fade out.

---

# **III. TỶ LỆ SỬ DỤNG AI VÀ QUAY THỰC TẾ**

### **Phần hoạt hình/AI**

Dùng AI để tạo:

* Bệnh viện.  
* Bệnh nhân.  
* Điều dưỡng.  
* Nhân vật IVI.  
* Minh họa tình huống.  
* Dòng dữ liệu.  
* Minh họa Edge AI.  
* Zigbee.  
* Luồng dữ liệu.  
* Bệnh viện tương lai.  
* Các cảnh chuyển tiếp.

### **Phần quay thực tế**

Bắt buộc quay thật:

* Team ICTU.  
* Board xG26.  
* MAX30102.  
* Load Cell.  
* Drop Sensor.  
* Raspberry Pi.  
* Serial Terminal.  
* Code.  
* MQTT.  
* Dashboard.  
* Thay đổi thông số.  
* Cảnh sensor mất tín hiệu.  
* LED chuyển màu (3 LED thật trên board — xanh PA07, vàng PA04, đỏ PA05).  
* Buzzer.  
* Dữ liệu thay đổi thực tế.  
* Trang OTA + board khởi động lại sau khi nạp firmware.

### **Phần đồ họa dựng tay (không AI)**

Cho các sơ đồ **kỹ thuật** — vẫn phải dựng thủ công bằng công cụ đồ họa/motion graphics thông thường, dữ liệu lấy đúng từ source, **không giao AI vẽ** vì bản thân sơ đồ là một phần của bằng chứng kỹ thuật:

* Sơ đồ kiến trúc BỆNH NHÂN → CẢM BIẾN → xG26 → ZIGBEE → RASPBERRY PI → HIS SERVER → ĐIỀU DƯỠNG (Cảnh 5).  
* Sơ đồ pipeline AI: 64 giây dữ liệu → 1D-CNN → dự báo 16 giây (Cảnh 7).  
* Sơ đồ tín hiệu Zigbee xG26 → Raspberry Pi và bảng attribute cluster `SMART IV VITALS` (0xFC01) (Cảnh 8).  
* Sơ đồ đường đi lệnh cấu hình DASHBOARD → SERVER → GATEWAY → ZIGBEE → xG26 (Cảnh 10).  
* Nhãn/mũi tên overlay trong montage cắt nhanh (Cảnh 13).

### **Nguyên tắc quan trọng**

**AI kể chuyện – Camera chứng minh.**

Không dùng AI để giả lập phần demo kỹ thuật.

Khi nói:

> “Hệ thống phát hiện mất tín hiệu.”

→ phải cho thấy **phần cứng thật chuyển trạng thái**.

Khi nói:

> “Dữ liệu được truyền tới server.”

→ phải cho thấy **dữ liệu thật trên terminal/dashboard**.

Khi nói:

> “AI chạy trên chip.”

→ phải cho thấy **board thật \+ log/code thật**.

Như vậy video vừa có **tính điện ảnh**, vừa có **độ tin cậy kỹ thuật**, rất phù hợp với một video dự thi IoT.


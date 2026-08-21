# **SMART IV MONITORING SYSTEM**

## **"KHI CÔNG NGHỆ BIẾT QUAN SÁT"**

| | |
|---|---|
| **Tên video:** | "KHI CÔNG NGHỆ BIẾT QUAN SÁT" — Smart IV Monitoring System |
| **Thời lượng:** | ~9:25 (mục tiêu: dưới 10 phút) |
| **Hình thức:** | Phim ngắn kết hợp hoạt hình AI + quay thực tế sản phẩm + đồ họa công nghệ |
| **Phong cách:** | Điện ảnh, hiện đại, cảm xúc, công nghệ, dễ hiểu với giám khảo |
| **Đối tượng:** | Ban giám khảo cuộc thi IoT, người đánh giá kỹ thuật, khán giả không chuyên |
| **Căn cứ kỹ thuật:** | `firmware/`, `gateway/`, `server/`, `autogen/` của Team ICTU — mọi số liệu/log/UI trong cột "Prompt tạo video" và "Lời thoại" bên dưới phải khớp đúng những gì hệ thống thật sinh ra, không dựng số liệu giả |
| **Mô-típ:** | Sự cố tại giường bệnh → hệ thống tự phát hiện → cảnh báo & hỗ trợ điều dưỡng → hệ thống trở lại ổn định |

### **Chú giải định dạng hình ảnh (áp cho từng đoạn bên dưới)**

| Ký hiệu | Nghĩa | Khi nào dùng |
|---|---|---|
| 🎨 **AI HOẠT HÌNH** | Dựng bằng AI tạo sinh | Chỉ cho phần **kể chuyện/cảm xúc**: bệnh viện, An, Lan, nhân vật IVI, các cảnh chuyển tiếp mang tính minh họa, không phải bằng chứng kỹ thuật |
| 🎥 **QUAY THẬT** | Camera quay người thật hoặc phần cứng/màn hình thật | Bắt buộc cho **mọi thứ chứng minh hệ thống hoạt động**: board, cảm biến, LED, terminal, dashboard, thao tác trên UI, thành viên team |
| 📊 **ĐỒ HỌA DỰNG TAY** | Motion graphics/diagram dựng thủ công (không AI) | Cho sơ đồ kỹ thuật (luồng dữ liệu, kiến trúc, pipeline AI, cluster Zigbee) — đây vẫn là nội dung kỹ thuật nên **không giao cho AI vẽ**, chỉ dựng bằng công cụ đồ họa/animation thông thường dựa trên số liệu thật |

**Nguyên tắc:** một khi lời thoại đang khẳng định điều gì đó về hệ thống (số liệu, trạng thái, kết nối), hình ảnh minh họa cho câu đó phải là 🎥 hoặc 📊 — không phải 🎨. AI hoạt hình chỉ phục vụ mạch cảm xúc của câu chuyện. Cột **"Prompt tạo video (AI)"** trong các bảng bên dưới **chỉ điền cho đoạn 🎨** — đoạn 🎥/📊 để trống vì đó là quay/dựng thật, không sinh bằng AI.

**Chỉ cần phần kỹ thuật để tự quay?** Xem `docs/Quay_Ky_Thuat.md` — shot list tách riêng 🎥+📊, bỏ hết nhân vật/hoạt hình, tổ chức theo buổi quay thực tế thay vì theo thứ tự cảnh phim.

---

# **I. Ý TƯỞNG TỔNG THỂ**

Câu chuyện xoay quanh **một bệnh nhân đang truyền dịch** và **một điều dưỡng phải theo dõi nhiều bệnh nhân cùng lúc**.

Ban đầu mọi thứ bình thường.

Nhưng khi điều dưỡng rời khỏi giường bệnh, một sự cố xảy ra: **tín hiệu cảm biến SpO₂ bị mất**.

Nếu không có hệ thống thông minh, điều dưỡng có thể không nhận biết ngay.

Smart IV Monitoring System xuất hiện như một "người trợ lý số".

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

# **🎬 PHẦN I – DẪN CHUYỆN** (Cảnh 1 → Cảnh 4) | 0:00 – 2:05

*Mở vấn đề bằng nhân vật, sau đó bắc cầu sang Team ICTU thật.*

| CẢNH | THỜI GIAN | HÌNH ẢNH & DIỄN XUẤT | CHỮ NHẤN MẠNH (CAPTION) | LỜI THOẠI / VOICE-OVER | PROMPT TẠO VIDEO (AI) |
|---|---|---|---|---|---|
| 🎨<br>**CẢNH 1**<br>MỞ ĐẦU: MỘT NGÀY BÌNH THƯỜNG | 0:00–0:30 | 🎨 AI HOẠT HÌNH (toàn cảnh — thuần cảm xúc/kể chuyện, chưa có số liệu cần chứng minh).<br>Một bệnh viện hiện đại vào buổi sáng. Camera đi qua hành lang. Trong phòng bệnh có nhiều bệnh nhân. Bệnh nhân **An** nằm trên giường, túi truyền dịch đang nhỏ giọt. Điều dưỡng **Lan** bước vào, kiểm tra túi truyền — mọi thứ bình thường. Lan mỉm cười, đi sang giường bệnh khác. Camera lùi ra xa, hiện cảnh Lan phải theo dõi nhiều bệnh nhân. | HR: 78 BPM<br>SpO₂: 97%<br>DRIP: NORMAL | **LAN:** "Chào anh An. Tôi sẽ kiểm tra tình trạng truyền dịch của anh."<br>**LAN:** "Mọi thứ đều ổn." | Phong cách hoạt hình 3D Pixar hiện đại, cinematic. Toàn cảnh một bệnh viện hiện đại buổi sáng, ánh nắng dịu nhẹ xuyên qua cửa sổ hành lang. Camera lia mượt dọc hành lang, tiến vào phòng bệnh có nhiều giường. Bệnh nhân An (nam, trẻ, thân thiện) nằm trên giường với túi truyền dịch đang nhỏ giọt đều. Điều dưỡng Lan (nữ, đồng phục y tế, dáng vẻ tận tâm) bước vào, kiểm tra túi truyền, mỉm cười ấm áp rồi rời sang giường khác. Camera lùi dần ra xa để lộ nhiều giường bệnh, gợi khối lượng công việc Lan phải quán xuyến. Ánh sáng ấm, tông màu xanh dương–trắng của bệnh viện, chuyển động camera êm, cinematic depth of field, Ultra HD, 4K, tỷ lệ 16:9. |
| 🎨<br>**CẢNH 2**<br>VẤN ĐỀ XUẤT HIỆN | 0:30–1:05 | 🎨 AI HOẠT HÌNH (toàn cảnh — kịch tính hoá; "SpO₂: LOST" trên màn hình nhỏ chỉ là đạo cụ trong khung cảnh hoạt hình, không phải bằng chứng kỹ thuật nên vẫn để AI dựng).<br>An vẫn nằm trên giường, túi truyền tiếp tục nhỏ giọt. Lan đang kiểm tra một bệnh nhân khác. Đồng hồ chạy nhanh — hoạt hình mô phỏng thời gian trôi. Bất ngờ tín hiệu SpO₂ bị gián đoạn, màn hình nhỏ bên cạnh bệnh nhân nhấp nháy. Lan không nhìn thấy ngay, âm thanh cảnh báo rất nhỏ. Camera zoom vào khuôn mặt An. Không có phản hồi ngay, màn hình chuyển sang màu đỏ, dừng hình. | SpO₂: LOST | **AN:** "Chị Lan ơi…"<br>**VOICE-OVER:** "Trong môi trường bệnh viện, một điều dưỡng có thể phải theo dõi nhiều bệnh nhân cùng lúc."<br>**VOICE-OVER:** "Một thay đổi nhỏ có thể xảy ra bất cứ lúc nào."<br>**VOICE-OVER:** "Nhưng liệu chúng ta có thể phát hiện mọi bất thường ngay khi nó xảy ra?"<br>Màn hình tối, hiện chữ: **"VẬY NẾU HỆ THỐNG CÓ THỂ TỰ PHÁT HIỆN?"** | Phong cách hoạt hình 3D Pixar hiện đại, nhịp điệu kịch tính dần tăng. Bệnh nhân An vẫn nằm yên, túi truyền tiếp tục nhỏ giọt trong khung cảnh yên tĩnh. Đồng hồ tường mờ nhòe thể hiện thời gian trôi nhanh. Camera cắt sang màn hình nhỏ cạnh giường: đèn nhấp nháy đỏ, dòng chữ "SpO₂: LOST" hiện lên. Lan ở xa, đang cúi xuống bệnh nhân khác, không nhận ra. Camera zoom chậm và siết dần vào khuôn mặt lo lắng của An, ánh sáng phòng chuyển tông lạnh, đỏ nhạt phủ lên khung hình. Âm thanh môi trường tắt dần tạo cảm giác hồi hộp, cinematic slow zoom, depth of field sâu, tông màu tối dần cuối cảnh, Ultra HD, 4K, tỷ lệ 16:9. |
| 🎨→🎥<br>**CẢNH 3**<br>SMART IV MONITORING SYSTEM XUẤT HIỆN | 1:05–1:35 | 🎨 AI HOẠT HÌNH (IVI xuất hiện) → 🎥 QUAY THẬT/dựng logo (chuyển cảnh sang thật).<br>Một tia sáng công nghệ xuất hiện. Nhân vật **IVI – trợ lý AI** xuất hiện, nhìn về phía An. Màn hình chuyển từ hoạt hình sang hình ảnh thật. Logo **SMART IV MONITORING SYSTEM** hiện lên, sau đó hiện chữ **Team ICTU**. | SMART IV MONITORING SYSTEM<br>Team ICTU | **IVI:** "Tôi có thể giúp."<br>**VOICE-OVER:** "Đây là Smart IV Monitoring System – giải pháp giám sát truyền dịch thông minh được phát triển bởi Team ICTU." | Phong cách hoạt hình 3D Pixar hiện đại chuyển dần sang phong cách công nghệ trừu tượng. Một tia sáng xanh công nghệ xuất hiện giữa bóng tối, dần kết tinh thành hình ảnh nhân vật IVI — một thực thể AI nhỏ, phát sáng nhẹ, không phải robot vật lý mà là biểu tượng nhân hóa của hệ thống, hiển thị dạng ánh sáng/hạt năng lượng trên nền màn hình. IVI xoay nhẹ, "nhìn" về phía An với cảm giác quan tâm, trấn an. Ánh sáng từ IVI lan tỏa dần, làm mờ dần khung cảnh hoạt hình để chuẩn bị chuyển cảnh (crossfade) sang hình ảnh thật. Tông màu xanh dương công nghệ, particle light, volumetric glow, cinematic transition, Ultra HD, 4K, tỷ lệ 16:9. |
| 🎥<br>**CẢNH 4**<br>GIỚI THIỆU TEAM ICTU | 1:35–2:05 | 🎥 QUAY THẬT toàn cảnh — người thật, không dùng AI.<br>Quay từng thành viên, không đứng đọc tên đơn thuần. Mỗi thành viên gắn với một công việc: lập trình, thiết kế phần cứng, kiểm tra cảm biến, phát triển AI, xây dựng dashboard, kiểm thử hệ thống. | TEAM ICTU | **VOICE-OVER:** "Chúng tôi là Team ICTU – một nhóm sinh viên cùng chung niềm đam mê với IoT, trí tuệ nhân tạo và công nghệ nhúng."<br>**Thành viên 1** (nhìn camera): "Chúng tôi muốn tạo ra một sản phẩm không chỉ thông minh…"<br>**Thành viên 2:** "…mà còn giải quyết một vấn đề thực tế."<br>**Cả đội:** "Đó là Smart IV Monitoring System." | — (quay thật, không dùng AI ở cảnh này) |

---

# **🎬 PHẦN II – KỸ THUẬT: SƠ ĐỒ HỆ THỐNG, LUỒNG TÍN HIỆU & LUỒNG SỬ DỤNG** (Cảnh 5 → Cảnh 11) | 2:05 – 7:05

*Sơ đồ kiến trúc → cảm biến/AI edge → Zigbee → dashboard → cấu hình từ xa → OTA. Đây là phần "how it works", xen kẽ 📊 sơ đồ dựng tay và 🎥 demo thật.*

| CẢNH | THỜI GIAN | HÌNH ẢNH & DIỄN XUẤT | CHỮ NHẤN MẠNH (CAPTION) | LỜI THOẠI / VOICE-OVER | PROMPT TẠO VIDEO (AI) |
|---|---|---|---|---|---|
| 🎨+📊<br>**CẢNH 5**<br>HỆ THỐNG HOẠT ĐỘNG NHƯ THẾ NÀO? | 2:05–2:45 | 🎨 AI HOẠT HÌNH (IVI dẫn chuyện) + 📊 ĐỒ HỌA DỰNG TAY (sơ đồ kiến trúc BỆNH NHÂN→…→ĐIỀU DƯỠNG là nội dung kỹ thuật thật, không giao AI vẽ, dựng bằng motion graphics theo đúng kiến trúc trong `gateway/`, `server/`).<br>Quay trở lại hoạt hình. IVI đứng giữa màn hình, bốn biểu tượng lần lượt xuất hiện khi IVI nói. Sau đó cắt sang 📊 sơ đồ dựng tay đầy đủ: BỆNH NHÂN → CẢM BIẾN → xG26 + EDGE AI → ZIGBEE → RASPBERRY PI → HIS SERVER → ĐIỀU DƯỠNG (dùng `docs/images/scene05_architecture.svg`). | SENSOR → EDGE AI → ZIGBEE → DASHBOARD | **IVI:** "Để giúp điều dưỡng, tôi cần biết điều gì đang xảy ra."<br>**IVI:** "Đầu tiên, tôi cảm nhận." (SENSOR)<br>**IVI:** "Sau đó, tôi phân tích." (EDGE AI)<br>**IVI:** "Tiếp theo, tôi kết nối." (ZIGBEE)<br>**IVI:** "Và cuối cùng, tôi thông báo." (DASHBOARD)<br>**VOICE-OVER:** "Từ giường bệnh đến màn hình điều dưỡng, toàn bộ dữ liệu được kết nối theo thời gian thực." | Phong cách hoạt hình 3D Pixar hiện đại, nền tối trừu tượng mang cảm giác công nghệ. Nhân vật IVI đứng giữa khung hình, phát sáng nhẹ. Khi IVI nói từng câu, một biểu tượng tương ứng lần lượt hiện ra và bay quanh IVI theo thứ tự: biểu tượng cảm biến (sóng nhịp tim), biểu tượng chip AI (mạch điện phát sáng), biểu tượng sóng không dây Zigbee, biểu tượng màn hình dashboard — mỗi biểu tượng nối với IVI bằng một đường sáng mảnh. Camera xoay nhẹ quanh IVI theo chuyển động cinematic chậm rãi, ánh sáng xanh dương công nghệ, particle light, volumetric glow, chuyển động mượt mà, Ultra HD, 4K, tỷ lệ 16:9. |
| 🎨+🎥<br>**CẢNH 6**<br>"TÔI CÓ THỂ NHÌN THẤY" | 2:45–3:35 | 🎨 AI HOẠT HÌNH (mở đầu, IVI dẫn) → 🎥 QUAY THẬT (toàn bộ phần cận cảnh phần cứng + LED — bắt buộc quay thật, đây là bằng chứng kỹ thuật).<br>🎨 IVI nhìn vào bệnh nhân An, dữ liệu (Heart Rate, SpO₂, Drip Rate) xuất hiện xung quanh. Chuyển sang 🎥 hình ảnh sản phẩm thật: EFR32xG26 toàn cảnh → MAX30102 (ngón tay đặt lên, HR=78, SpO₂=97%) → Load Cell (túi treo) → Drop Sensor (~98 giọt/phút) → 3 LED thật trên board (xanh PA07, vàng PA04, đỏ PA05), đúng `firmware/sensor_hub.c`. | EFR32xG26<br>MAX30102 → HR=78, SpO₂=97%<br>Load Cell<br>Drop Sensor → DROP RATE ≈ 98 giọt/phút<br>XANH – ỔN ĐỊNH / VÀNG – CẢNH BÁO ĐƯỜNG TRUYỀN (đèn sáng, còi im lặng) / ĐỎ đứng yên – CẢNH BÁO BỆNH NHÂN / ĐỎ nhấp nháy – NGUY HIỂM | **IVI:** "Tôi có thể theo dõi nhiều tín hiệu cùng lúc."<br>**VOICE-OVER:** "Hệ thống sử dụng EFR32xG26 làm bộ xử lý trung tâm."<br>**VOICE-OVER:** "MAX30102 theo dõi nhịp tim và SpO₂."<br>**VOICE-OVER:** "Load Cell theo dõi lượng và tốc độ truyền dịch."<br>**VOICE-OVER:** "Drop Sensor theo dõi tốc độ nhỏ giọt — ngưỡng nhận diện được đo trực tiếp trên chính cảm biến, không sao chép từ tài liệu tham khảo."<br>**IVI:** "Tôi không chỉ nhìn thấy dữ liệu."<br>**IVI:** "Tôi còn biết khi nào dữ liệu trở nên bất thường." | Phong cách hoạt hình 3D Pixar hiện đại, cận cảnh IVI (thực thể ánh sáng) hướng về phía bệnh nhân An. Ba dòng dữ liệu — Heart Rate, SpO₂, Drip Rate — hiện ra thành các nhãn phát sáng lơ lửng quanh giường bệnh, nhấp nháy nhẹ theo nhịp. Camera lia chậm từ IVI sang các nhãn dữ liệu, ánh sáng xanh dương dịu, particle nhỏ bay quanh, không khí công nghệ nhưng ấm áp, cinematic depth of field, chuyển động mượt mà. Đoạn này chỉ dùng cho phần mở đầu hoạt hình — không áp dụng cho đoạn cận cảnh phần cứng phía sau vì đó là footage quay thật. Ultra HD, 4K, tỷ lệ 16:9. |
| 📊+🎥<br>**CẢNH 7**<br>"TÔI CÓ THỂ SUY NGHĨ" | 3:35–4:25 | 📊 ĐỒ HỌA DỰNG TAY (biểu đồ HR + sơ đồ 3 mô hình AI — số liệu thật từ `firmware/ai_engine.h`/`ai_fusion.c`, không giao AI vẽ) → 🎥 QUAY THẬT (code, serial terminal, chip).<br>Dữ liệu HR chạy liên tục (76→77→79→81→84) tạo thành đường biểu đồ. Biểu đồ chuyển thành sơ đồ 3 mô hình AI độc lập (dùng `docs/images/scene07_ai_pipeline.svg`). Chuyển sang quay thật: code (`firmware/ai_engine.h`/`ai_fusion.c`), serial terminal, chip xG26. | 76 → 77 → 79 → 81 → 84<br>PHÁT HIỆN XU HƯỚNG (2 mô hình dự báo 64s→16s)<br>CẢNH BÁO SỚM (cùng 2 mô hình dự báo)<br>PHÁT HIỆN BẤT THƯỜNG (3 mô hình cộng lại) | **VOICE-OVER:** "Đây là điểm khác biệt quan trọng: AI được đưa trực tiếp xuống thiết bị, chạy bằng TensorFlow Lite Micro và bộ tăng tốc phần cứng."<br>**VOICE-OVER:** "Không phải một, mà là ba mô hình nhỏ chạy độc lập — mỗi mô hình một bộ nhớ riêng, để một mô hình lỗi không kéo sập hai mô hình còn lại."<br>**VOICE-OVER:** "Hai mô hình phân tích cửa sổ 64 giây để dự báo xu hướng 16 giây tiếp theo; mô hình thứ ba nhỏ hơn, không cần cửa sổ dữ liệu, chỉ nhìn khoảnh khắc hiện tại để bắt những bất thường mà hai mô hình kia bỏ sót."<br>**IVI:** "Tôi không chờ sự cố xảy ra rồi mới phản ứng — tôi tìm dấu hiệu của nó từ trước."<br>*(Cắt gọn lúc dựng: gộp câu như trên để vừa 50 giây; giữ nguyên toàn bộ số liệu 64s/16s, không cắt nội dung kỹ thuật.)* | — (cảnh này chỉ dùng 📊 đồ họa dựng tay + 🎥 quay thật, không có đoạn 🎨 hoạt hình) |
| 📊+🎥<br>**CẢNH 8**<br>"TÔI CÓ THỂ KẾT NỐI" | 4:25–5:05 | 📊 ĐỒ HỌA DỰNG TAY (sơ đồ tín hiệu xG26→Zigbee→RPi) → 🎥 QUAY THẬT (Raspberry Pi, terminal JSON thật) → 📊 ĐỒ HỌA DỰNG TAY (bảng attribute cluster, lấy đúng tên từ `autogen/zap-config.h`).<br>Tín hiệu truyền xG26 → ZIGBEE → RASPBERRY PI. Chuyển sang quay thật Raspberry Pi, hiển thị terminal thật chạy `gateway/main.c` — dòng JSON thật (`gateway/main.c:481-492`) có khoảng 25 field (room, dripRate, flowRate, các field `*Signal`, weightG, hrForecast16s…), dài hơn nhiều so với caption rút gọn bên cạnh; đừng cắt/dựng lại dòng JSON cho khớp caption, cứ để terminal hiện đúng như nó in ra. Sau đó hiện Custom Cluster (đồ họa dựng tay, dữ liệu từ code thật). | Rút gọn minh họa (JSON thật dài hơn — xem ghi chú cột bên trái):<br>```{"bedId":"BED-101","spo2":97,"heartRate":78,"dropsPerMin":98,"targetDropsPerMin":50,"aeAlarm":false}```<br>SMART IV VITALS — 0xFC01<br>HeartRate, SpO₂, DropsPerMin, TargetDropsPerMin, AlarmBitmap, … và hơn 20 attribute khác | **IVI:** "Dữ liệu cần được truyền đi."<br>**IVI:** "Vì vậy, tôi sử dụng Zigbee."<br>**VOICE-OVER:** "Hệ thống sử dụng Zigbee với khả năng tiết kiệm năng lượng và hỗ trợ mạng mesh."<br>**VOICE-OVER:** "Chúng tôi xây dựng một Custom Zigbee Cluster dành riêng cho dữ liệu của hệ thống Smart IV." | — (cảnh này chỉ dùng 📊 đồ họa dựng tay + 🎥 quay thật, không có đoạn 🎨 hoạt hình) |
| 🎨+🎥<br>**CẢNH 9**<br>"TÔI CÓ THỂ BÁO CHO ĐIỀU DƯỠNG" | 5:05–5:50 | 🎨 AI HOẠT HÌNH (cảnh phòng điều dưỡng, chỉ mang tính kể chuyện) → 🎥 QUAY THẬT (toàn bộ phần Dashboard — đây là bằng chứng kỹ thuật, không được thay bằng mockup AI).<br>Phòng điều dưỡng, màn hình lớn hiện nhiều giường bệnh. Lan nhìn màn hình. Chuyển sang Dashboard thật (quay màn hình thật, không dựng lại giao diện): click BED-101, hiện HR, SpO₂, Drip, biểu đồ, trạng thái cảm biến. Lưu ý: UI thật có 4 filter trạng thái — Stable/Warning/Critical **và cả Offline** (`beds.js:10`) — nếu lúc quay có giường đang Offline (mất kết nối, không phải cảnh báo sinh hiệu) thì cứ để nguyên, không cần né hay ẩn đi. | 🟢 BED 101 – STABLE<br>🟢 BED 102 – STABLE<br>🟡 BED 103 – WARNING<br>🔴 BED 104 – CRITICAL | **LAN:** "Tôi biết ngay giường nào cần được kiểm tra."<br>**VOICE-OVER:** "HIS Dashboard cho điều dưỡng một góc nhìn tổng thể toàn hệ thống — không cần kiểm tra từng cảm biến thủ công."<br>**VOICE-OVER:** "Chỉ với một màn hình, họ biết ngay giường nào ổn định, giường nào cần chú ý, giường nào đang nguy hiểm."<br>*(Cắt gọn lúc dựng: gộp 3 câu VO gốc thành 2 câu để vừa 45 giây.)* | Phong cách hoạt hình 3D Pixar hiện đại, phòng điều dưỡng hiện đại, sạch sẽ, ánh sáng dịu từ màn hình lớn treo tường. Một màn hình lớn hiển thị dạng khối các thẻ giường bệnh với các chấm màu (xanh/vàng/đỏ) phát sáng nhẹ — chỉ mang tính minh họa bố cục, không hiển thị số liệu cụ thể để tránh gây hiểu nhầm là dữ liệu thật. Điều dưỡng Lan đứng trước màn hình, ánh mắt tập trung, tự tin quét nhìn qua các thẻ giường. Camera lia nhẹ từ sau lưng Lan hướng lên màn hình, sau đó xoay sang cận cảnh gương mặt điềm tĩnh của Lan. Tông màu xanh dương công nghệ dịu mắt, cinematic depth of field, chuyển động mượt mà, Ultra HD, 4K, tỷ lệ 16:9. |
| 🎥+📊<br>**CẢNH 10**<br>KHẢ NĂNG TÙY CHỈNH | 5:50–6:20 | 🎥 QUAY THẬT (thao tác Dashboard) → 📊 ĐỒ HỌA DỰNG TAY (sơ đồ đường đi lệnh xuống thiết bị, `docs/images/scene10_config_path.svg`).<br>Quay Dashboard thật: Lan cuộn tới ô "Drop rate (AI alert target)", gõ số mới vào ô "New target" (đổi từ 20 thành 50), nhấn nút **Set** (đúng chữ trên UI — không phải "SAVE"). Sau đó 📊 sơ đồ tín hiệu chạy: DASHBOARD → SERVER → GATEWAY → ZIGBEE → xG26. | Drop rate (AI alert target): 20 → 50<br>DASHBOARD → SERVER → GATEWAY → ZIGBEE → xG26 | **VOICE-OVER:** "Hệ thống cũng cho phép thay đổi cấu hình từ xa."<br>**LAN:** "Tôi thay đổi mục tiêu từ 20 thành 50 giọt mỗi phút."<br>**IVI:** "Đã cập nhật."<br>**VOICE-OVER:** "Lệnh được truyền đến thiết bị mà không cần nạp lại firmware và không cần khởi động lại hệ thống." | — (cảnh này chỉ dùng 🎥 quay thật + 📊 đồ họa dựng tay, không có đoạn 🎨 hoạt hình) |
| 🎥<br>**CẢNH 11**<br>CẬP NHẬT FIRMWARE TỪ XA (OTA) | 6:20–7:05 | 🎥 QUAY THẬT toàn cảnh (thao tác web thật + board thật khởi động lại) — không có nhân vật hoạt hình, đây thuần là bằng chứng kỹ thuật.<br>Vẫn ở Dashboard thật, chuyển sang tài khoản kỹ thuật viên (`kythuat`). Trang Devices, khối "**Firmware library (OTA)**" (đúng chữ trên UI — UI đang là tiếng Anh, không dịch): bảng File/Device/Version/Size, nút Upload. Chọn file `.ota` → Upload, bảng cập nhật dòng mới với mã nhà sản xuất đọc từ file. Kéo xuống khối FIRMWARE của thiết bị, bấm "Check for update" → "Update available" → "Update" → hộp thoại xác nhận → thanh tiến độ chạy % + thời gian còn lại. *(Ghi chú quay: nạp thật mất khoảng 14 phút cho ảnh ~430KB — xem `docs/OTA.md` mục 8. Quay timelapse hoặc cắt nhiều đoạn ngắn, không tua giả toàn bộ thanh %.)* Cận cảnh board thật tự khởi động lại (không rút nguồn), màn OLED sáng lại ở bản mới. Cắt về Devices: khối Device log hiện dòng mới. | Firmware library (OTA)<br>Update available<br>"Update started: v2 to v4" → "Updated: v2 to v4" | **VOICE-OVER:** "Thiết bị đầu giường cũng có thể cập nhật firmware từ xa. Kỹ thuật viên tải file thẳng qua trang web — không cần SSH, không cần đến tận giường bệnh."<br>**VOICE-OVER:** "Server tự đọc file để xác minh đúng loại thiết bị — chọn nhầm file bị từ chối ngay lúc tải lên. Toàn bộ quá trình nạp được theo dõi và ghi lại thành lịch sử, kể cả những lần thất bại."<br>**IVI:** "Ngay cả tôi cũng có thể được nâng cấp — mà không ai phải rời khỏi bàn làm việc."<br>*(Cắt gọn lúc dựng: gộp 4 câu VO gốc thành 2 câu để vừa 45 giây.)* | — (quay thật toàn cảnh, không dùng AI ở cảnh này) |

---

# **🎬 PHẦN III – DEMO THẬT: CAO TRÀO** (Cảnh 12 → Cảnh 13) | 7:05 – 8:25

*Toàn bộ chuỗi phản ứng thật của hệ thống trước một sự cố thật, không có số liệu dàn dựng.*

| CẢNH | THỜI GIAN | HÌNH ẢNH & DIỄN XUẤT | CHỮ NHẤN MẠNH (CAPTION) | LỜI THOẠI / VOICE-OVER | PROMPT TẠO VIDEO (AI) |
|---|---|---|---|---|---|
| 🎨+🎥<br>**CẢNH 12**<br>THỬ THÁCH: NẾU CẢM BIẾN MẤT TÍN HIỆU? *(cao trào của bộ phim)* | 7:05–8:00 | 🎨 AI HOẠT HÌNH (nhịp cảm xúc: An, Lan, khoảnh khắc ngón tay rời cảm biến) xen kẽ chặt với 🎥 QUAY THẬT (mọi con số/trạng thái/LED/Dashboard đều phải là bằng chứng thật, kể cả trong đoạn cao trào — không được dùng số liệu hoạt hình giả).<br>🎨 Trở lại nhân vật An, mọi thứ bình thường. 🎥 Dashboard thật: BED-101 – STABLE, HR: 78, SpO₂: 97, Drip: ~98 giọt/phút. 🎨 Lan rời khỏi giường, camera giữ lại An. 🎥 Ngón tay rời khỏi MAX30102 (thao tác thật trên phần cứng thật). Không nhạc, không lời thoại, chỉ nghe tiếng máy. 3 giây sau (🎥 toàn bộ, không hoạt hình): cận cảnh board thật, LED thật đổi 🟢→🟡. **Ở mức cảnh báo đường truyền (vàng), buzzer im lặng theo đúng logic mới nhất của `firmware/sensor_hub.c` — chỉ đèn đổi màu, không có tiếng bíp.** Cắt sang Dashboard thật: BED-101 STABLE→WARNING, SpO₂: 97→LOST, alert đúng chữ server sinh ra (`VitalsStatusEvaluator.cs`, mã `SENSOR_DISCONNECTED`). 🎨 Cắt sang Lan, 🎥 điện thoại hiện push notification thật (FCM — `server/src/HisServer/Services/FcmPushService.cs`), nội dung phải khớp đúng dữ liệu đang chạy thật trên Dashboard. 🎨 Lan quay lại. 🎥 Sau đó tín hiệu được khôi phục (đặt ngón tay lại — thật, đo lại thật), Dashboard thật: 🟡→🟢. | 🟢 BED-101 – STABLE → HR: 78, SpO₂: 97, Drip: ~98 giọt/phút<br>🟡 STABLE → WARNING<br>SpO₂: 97 → LOST<br>"No signal from: SpO2" | **LAN:** "Mọi thứ đang ổn định."<br>*(không nhạc, không lời thoại — chỉ tiếng máy trong 3 giây LED chuyển màu, không có tiếng bíp vì mức cảnh báo đường truyền không kích hoạt buzzer)*<br>**IVI xuất hiện:** "Phát hiện mất tín hiệu cảm biến."<br>**LAN:** "Tôi nhận được cảnh báo."<br>**IVI:** "Hệ thống đã trở lại trạng thái ổn định."<br>*Ghi chú kỹ thuật (không đưa vào lời thoại/demo):* kênh báo động qua SMS hoặc gọi điện khi cảnh báo kéo dài không được xác nhận là hướng mở rộng, hệ thống hiện chưa làm — chỉ nhắc ở phần "định hướng phát triển" (Cảnh 14) nếu cần, tuyệt đối không dựng cảnh giả lập SMS/cuộc gọi vì đây không phải tính năng thật. | Phong cách hoạt hình 3D Pixar hiện đại, chỉ áp dụng cho các khoảnh khắc nhân vật (không áp dụng cho bất kỳ số liệu/LED/dashboard nào — toàn bộ phần đó phải là footage quay thật). Đoạn 1: An nằm yên trên giường trong ánh sáng buổi sáng bình yên, biểu cảm thư thái. Đoạn 2: Lan mỉm cười nhẹ, quay người rời khỏi giường bệnh, bước đi khuất dần khỏi khung hình; camera ở lại giữ khuôn mặt An, ánh sáng dịu chuyển chậm sang tông trung tính, không khí tĩnh lặng, chuẩn bị cho khoảnh khắc căng thẳng. Đoạn 3 (sau khi tín hiệu mất, cắt lại nhân vật): Lan quay đầu lại nhìn về phía điện thoại/màn hình với biểu cảm chú ý, lo lắng nhẹ rồi bình tĩnh bước nhanh trở lại giường bệnh. Cinematic slow pacing, ánh sáng tự nhiên, cảm xúc chân thật, không có bất kỳ số liệu/text kỹ thuật nào xuất hiện trong các đoạn hoạt hình này. Ultra HD, 4K, tỷ lệ 16:9. |
| 🎥+📊<br>**CẢNH 13**<br>TOÀN BỘ HỆ THỐNG CÙNG PHẢN ỨNG | 8:00–8:25 | 🎥 QUAY THẬT (montage cắt nhanh từ các footage phần cứng/dashboard đã quay ở Cảnh 6–12, dựng lại — không tạo mới bằng AI) + 📊 ĐỒ HỌA DỰNG TAY (nhãn tên khối và mũi tên nối giữa các đoạn footage).<br>Montage cực nhanh (cắt từ footage thật đã quay, không dựng cảnh mới): CẢM BIẾN → LED đổi màu → EDGE AI → phát hiện bất thường → ZIGBEE → truyền dữ liệu → RASPBERRY PI → HIS SERVER → DASHBOARD → ĐIỀU DƯỠNG. Màn hình dừng, hiện bốn từ: CẢM NHẬN / SUY NGHĨ / KẾT NỐI / PHẢN HỒI. | CẢM NHẬN — SUY NGHĨ — KẾT NỐI — PHẢN HỒI | **VOICE-OVER:** "Một sự cố xảy ra tại giường bệnh."<br>**VOICE-OVER:** "Cảm biến phát hiện."<br>**VOICE-OVER:** "AI phân tích."<br>**VOICE-OVER:** "Zigbee truyền dữ liệu."<br>**VOICE-OVER:** "Server cập nhật."<br>**VOICE-OVER:** "Dashboard cảnh báo."<br>**VOICE-OVER:** "Và điều dưỡng phản ứng." | — (montage dựng lại từ footage thật đã quay, không tạo cảnh mới bằng AI) |

---

# **🎬 PHẦN IV – CÂU CHUYỆN TIẾP TỤC** (Cảnh 14 → Cảnh 15) | 8:25 – 9:25

*Quay lại mạch cảm xúc, chốt giá trị sản phẩm và thông điệp của Team ICTU.*

| CẢNH | THỜI GIAN | HÌNH ẢNH & DIỄN XUẤT | CHỮ NHẤN MẠNH (CAPTION) | LỜI THOẠI / VOICE-OVER | PROMPT TẠO VIDEO (AI) |
|---|---|---|---|---|---|
| 🎥+🎨<br>**CẢNH 14**<br>TEAM ICTU VÀ GIÁ TRỊ SẢN PHẨM | 8:25–8:55 | 🎥 QUAY THẬT (chính — người thật, thao tác thật) xen kẽ 🎨 AI HOẠT HÌNH (chỉ đoạn "bệnh viện tương lai", vì đây là tầm nhìn/minh họa, không phải chứng minh kỹ thuật).<br>Các thành viên: lắp mạch, hàn dây, lập trình, kiểm tra cảm biến, kiểm thử AI, kiểm tra dashboard, trao đổi nhóm. Xen kẽ một số cảnh hoạt hình về bệnh viện tương lai (minh họa tầm nhìn — không kèm số liệu/tính năng cụ thể để tránh gây hiểu nhầm là demo thật). | REAL-TIME<br>EDGE AI<br>ZIGBEE<br>SMART MONITORING<br>SCALABLE | **VOICE-OVER:** "Smart IV Monitoring System không chỉ là một thiết bị cảm biến — đó là sự kết hợp giữa IoT, Edge AI, hệ thống nhúng, kết nối không dây và quản lý dữ liệu thời gian thực."<br>**VOICE-OVER:** "Chúng tôi hướng tới một hệ thống có thể mở rộng, tùy biến và ứng dụng trong môi trường thực tế."<br>*(Bỏ hẳn đoạn roadmap SMS/cuộc gọi để vừa 30 giây — nếu ban giám khảo hỏi thêm về hướng phát triển thì trả lời trực tiếp lúc Q&A, không cần đưa vào video.)* | Phong cách hoạt hình 3D Pixar hiện đại, chỉ dùng cho đoạn "bệnh viện tương lai" mang tính tầm nhìn — không kèm số liệu, giao diện hay tính năng cụ thể để tránh gây hiểu nhầm là demo thật. Khung cảnh một bệnh viện hiện đại, rộng rãi, tràn ngập ánh sáng tự nhiên, nhiều mảng xanh, các màn hình mờ ảo hiển thị biểu tượng kết nối (không phải số liệu cụ thể) trôi nhẹ nhàng trong không gian. Nhân viên y tế và bệnh nhân di chuyển an nhiên, không khí yên bình, công nghệ và con người hòa hợp. Camera cinematic lia rộng toàn cảnh, ánh sáng ấm, volumetric light, cảm giác lạc quan về tương lai y tế thông minh. Ultra HD, 4K, tỷ lệ 16:9. |
| 🎨→🎥<br>**CẢNH 15**<br>THÔNG ĐIỆP CUỐI | 8:55–9:25 | 🎨 AI HOẠT HÌNH (An, Lan, IVI) → 🎥 QUAY THẬT (Team ICTU + sản phẩm thật).<br>An nằm trên giường, Lan kiểm tra hệ thống, IVI đứng bên cạnh. Sau đó chuyển sang Team ICTU, các thành viên đứng cùng sản phẩm thật. | TEAM ICTU<br>SMART IV MONITORING SYSTEM<br>CẢM NHẬN – SUY NGHĨ – KẾT NỐI – PHẢN HỒI | **Thành viên 1:** "Công nghệ không chỉ để thu thập dữ liệu — công nghệ cần hiểu dữ liệu."<br>**Thành viên 2:** "Phát hiện vấn đề, và hỗ trợ con người đưa ra quyết định tốt hơn."<br>**Cả đội:** "Đó là lý do chúng tôi tạo ra Smart IV Monitoring System."<br>*(Cắt gọn lúc dựng: gộp 4 lời thoại gốc thành 2 để vừa 30 giây.)* | Phong cách hoạt hình 3D Pixar hiện đại, cảnh khép lại ấm áp. An nằm yên trên giường bệnh, gương mặt thư thái, an tâm. Lan đứng cạnh, nhẹ nhàng kiểm tra màn hình theo dõi, ánh mắt tận tâm. IVI (thực thể ánh sáng nhỏ) đứng lơ lửng bên cạnh màn hình, tỏa sáng dịu, như đang "quan sát" cùng Lan. Ba nhân vật cùng xuất hiện trong khung hình theo bố cục hài hòa, ánh sáng vàng ấm của hoàng hôn/bình minh chiếu qua cửa sổ, tạo cảm giác yên bình, kết thúc trọn vẹn. Camera lùi chậm ra xa, làm mờ dần (fade) để chuẩn bị chuyển cảnh sang Team ICTU thật. Cinematic warm lighting, volumetric light, chuyển động mượt mà, Ultra HD, 4K, tỷ lệ 16:9. |

Sau lời thoại cuối, màn hình đen, hiện logo:

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
* Buzzer (chỉ kêu ở mức đỏ/nguy kịch — mức vàng/cảnh báo đường truyền đèn sáng nhưng buzzer im lặng, theo bản cập nhật mới nhất của `firmware/sensor_hub.c`).
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

> "Hệ thống phát hiện mất tín hiệu."

→ phải cho thấy **phần cứng thật chuyển trạng thái**.

Khi nói:

> "Dữ liệu được truyền tới server."

→ phải cho thấy **dữ liệu thật trên terminal/dashboard**.

Khi nói:

> "AI chạy trên chip."

→ phải cho thấy **board thật + log/code thật**.

Như vậy video vừa có **tính điện ảnh**, vừa có **độ tin cậy kỹ thuật**, rất phù hợp với một video dự thi IoT.

---

# **IV. GHI CHÚ SẢN XUẤT**

* **Âm nhạc:** nhạc nền nhẹ nhàng, ấm áp trong Phần I → chuyển sang tiết tấu hồi hộp, tối giản (gần như im lặng) trong đoạn cao trào Cảnh 12 → nhạc tích cực, ấm áp trong Phần IV.
* **Màu sắc chủ đạo:** Xanh dương công nghệ (dashboard, IVI, dữ liệu) – Xanh lá/vàng/đỏ (đúng 3 LED thật trên board, không tự thêm màu khác) – tông ấm (vàng nhạt/cam) cho các cảnh cảm xúc gia đình/bệnh nhân.
* **Caption kỹ thuật:** mọi số liệu/chuỗi JSON/thông điệp alert hiển thị trong caption phải **sao chép nguyên văn** từ log/UI thật (`gateway/main.c`, `VitalsStatusEvaluator.cs`, Dashboard) — không viết lại theo ý riêng.
* **Đồng phục/hình ảnh nhân vật hoạt hình:** An – bệnh nhân trẻ, áo bệnh viện; Lan – điều dưỡng, đồng phục y tế; IVI – thực thể ánh sáng phi vật lý, không thiết kế thành robot có hình người.
* **Kết thúc video:** logo Team ICTU + Smart IV Monitoring System + slogan "CẢM NHẬN – SUY NGHĨ – KẾT NỐI – PHẢN HỒI".

---

# **GHI CHÚ THỜI LƯỢNG**

Bản gốc cộng dồn ra ~12:25 (dài hơn mục tiêu). Đã cắt lại còn **~9:25**, dưới ngưỡng 10 phút, bằng cách rút ngắn 5 cảnh dài nhất (Cảnh 7, 9, 11, 14, 15 — gộp câu thoại/voice-over, xem ghi chú "*(Cắt gọn lúc dựng...)*" ngay trong bảng ở mỗi cảnh đó) và nén nhẹ các cảnh còn lại bằng dựng phim (cắt nhanh hơn, không cắt nội dung). Không cảnh nào bị bỏ số liệu kỹ thuật — chỉ gộp câu để nói nhanh hơn. Cảnh 12 (cao trào) và Cảnh 6/8 (bằng chứng phần cứng) giữ gần như nguyên vẹn vì cắt ở đây sẽ ảnh hưởng tính thuyết phục kỹ thuật.

Vẫn còn ~35 giây đệm an toàn dưới mốc 10 phút — nếu dựng thực tế bị đội thời lượng (nhạc/chuyển cảnh dài hơn dự kiến), ưu tiên cắt tiếp ở Cảnh 1–4 (Phần dẫn chuyện) trước khi đụng vào Phần II/III kỹ thuật.

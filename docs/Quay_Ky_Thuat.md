# Hướng dẫn quay — phần kỹ thuật (Smart IV Monitoring System)

Tài liệu này tách riêng từ `docs/Script.md`, **chỉ giữ phần 🎥 quay thật và 📊 sơ đồ dựng tay** — bỏ hết nhân vật/hoạt hình AI. Tổ chức theo đúng số Cảnh trong `docs/Script.md` để đối chiếu qua lại cho dễ.

**Nguyên tắc duy nhất cần nhớ:** mọi số liệu lên hình phải là số thật từ hệ thống lúc đó. Nếu số đo được khác với số ví dụ trong tài liệu này (vd. không phải 98 giọt/phút) — cứ quay đúng số thật, đừng chỉnh lại cho khớp.

---

## Chuẩn bị trước khi quay

- [ ] Board xG26 đã flash firmware mới nhất, chạy ổn định, log qua serial console đọc được.
- [ ] MAX30102, Load Cell (HX711), Drop Sensor đã gắn và hoạt động — không có kênh nào báo `CH_DISABLED`.
- [ ] Raspberry Pi chạy `gateway/main.c`, đã kết nối Zigbee với board, terminal đang in JSON liên tục.
- [ ] HIS Server + Dashboard chạy được. Có sẵn 2 tài khoản: **y tá** (`yta`) và **kỹ thuật viên** (`kythuat`).
- [ ] Có sẵn 1 file `.ota` bản mới hơn trên máy/laptop dùng để quay — đúng mã nhà sản xuất xG26 (4169). Không cần SSH vào Pi, upload thẳng qua web (xem `docs/OTA.md` mục 4).
- [ ] Đã tính lịch: nạp firmware thật mất khoảng **14 phút** — sắp xếp đủ thời gian, hoặc chuẩn bị sẵn máy quay timelapse.
- [ ] Đèn phòng tắt được hoàn toàn (dùng cho cảnh LED).
- [ ] Điện thoại/máy quay có chế độ Macro + chân đế chống rung.
- [ ] Máy tính quay màn hình đã cài OBS Studio, test thử Display Capture/Window Capture chạy được.
- [ ] Trình duyệt mở Dashboard đã zoom 125–150% để chữ số dễ đọc trên video.

---

## Cảnh 5 — Sơ đồ hệ thống

Không cần quay. Đồ họa `docs/images/scene05_architecture.svg` đã dựng sẵn theo đúng kiến trúc thật (`gateway/`, `server/`), đưa thẳng cho bên dựng phim.

---

## Cảnh 6 — Cận cảnh phần cứng

Dựng một bàn quay riêng, ánh sáng khuếch tán, quay hết các mục dưới trong cùng một lần setup vì cùng ánh sáng/góc máy.

1. **Board EFR32xG26 toàn cảnh.** Macro, lấy đủ chip + phần mikroBUS đang gắn cảm biến. Nghiêng board so với đèn để tránh lóa.
2. **MAX30102 + ngón tay.** Đặt ngón tay lên, giữ yên ~10 giây để số liệu chạy ổn định (HR ≈ 78, SpO₂ ≈ 97%) rồi mới cắt cảnh — số này phải hiện đồng thời trên serial/OLED.
3. **Load Cell + túi treo.** Quay từ dưới lên, thấy rõ túi treo và dây nối HX711.
4. **Drop Sensor.** Macro cực gần khe quang. Nếu dòng chảy đủ chậm, cố quay thấy được giọt đi qua. Drop Rate hiện đồng thời trên log/dashboard (thường ~90–100 giọt/phút khi chảy tự do — xem `firmware/drop_filter.c`).
5. **3 LED — trạng thái NORMAL.** Tắt hết đèn phòng trước. Macro cực gần, nền tối để màu xanh nổi rõ.
6. **3 LED — 3 trạng thái cảnh báo (nên quay, không bắt buộc).** Ép lần lượt từng trạng thái bằng cách rút cảm biến hoặc giả lập lỗi:
   - LINE_WARNING → **vàng đứng yên**
   - VITALS_ALERT → **đỏ đứng yên**
   - CRITICAL → **đỏ nhấp nháy** *(mới đổi gần đây — trước kia là đỏ+vàng cùng lúc, giờ chỉ một đèn được sáng một lúc, xem `docs/AI_HOAT_DONG_THE_NAO.md`)*
   Quay riêng cả 3 để bên dựng phim có sẵn, khỏi phải tháo lắp lại nếu cần chèn thêm cảnh.
7. **Tự kiểm tra lúc khởi động (tuỳ chọn nhưng đáng quay).** Bật nguồn board và quay macro LED **ngay từ lúc cấp điện** — hệ thống tự sáng lần lượt xanh→vàng→đỏ rồi kêu 1 tiếng ngắn, chưa tới 1 giây nên dễ bỏ lỡ nếu bắt đầu quay trễ.

> Vì sao đèn CRITICAL đổi cách hiện: 4 mức cảnh báo nhưng chỉ có 3 đèn, nên mỗi lúc chỉ một đèn được phép sáng — CRITICAL và VITALS_ALERT đều đỏ, phân biệt nhau bằng đứng yên/nhấp nháy (và nhịp còi khác nhau: 0,3s vs 0,15s).

---

## Cảnh 7 — AI chạy trên chip

Không cần quay: đồ họa `docs/images/scene07_ai_pipeline.svg` (3 mô hình AI độc lập) đã dựng sẵn.

Phần cần quay thật:

1. **Serial console board xG26 lúc chạy AI.** Quay màn hình trực tiếp (ưu tiên) hoặc over-the-shoulder — thấy dòng log mỗi giây từ `ai_fusion_step()` (mức cảnh báo, nguyên nhân...).
2. **Code.** Mở sẵn `firmware/ai_engine.h` hoặc `ai_fusion.c`, quay vài giây — chỉ cần đủ để chứng minh có code thật, không cần đọc kỹ nội dung.

---

## Cảnh 8 — Kết nối Zigbee

Không cần quay: đồ họa `docs/images/scene08_zigbee_cluster.svg` (luồng Zigbee + bảng attribute cluster) đã dựng sẵn.

Phần cần quay thật:

1. **Raspberry Pi terminal chạy gateway.** Phóng to font terminal, quay dòng JSON thật đang chạy, ví dụ: `{"bedId":"BED-101","spo2":97,"heartRate":78,"dropsPerMin":98,...}`. Đây phải là terminal thật đang chạy — không dựng lại bằng đồ họa.

---

## Cảnh 9 — Dashboard (đăng nhập vai y tá)

Dùng screen recording (OBS), **không** quay màn hình bằng camera — dễ nhòe chữ.

1. **Trang tổng — danh sách giường.** Cần ít nhất 1 giường STABLE (xanh). Nếu ép được thêm 1 giường WARNING/CRITICAL thì càng tốt để demo đủ màu — có thể dùng 2 board hoặc 2 bed ảo nếu chỉ có 1 board thật.
2. **Click vào 1 giường.** Để dữ liệu chạy ổn định ~5 giây rồi mới bấm, tránh cảnh quay bị giật số lúc mở trang. Trang chi tiết cần thấy: HR, SpO₂, Drip, biểu đồ, trạng thái cảm biến (không còn "Flow" — hệ thống chỉ dùng Drip làm chỉ số đường truyền).

---

## Cảnh 10 — Cấu hình từ xa

Không cần quay: đồ họa `docs/images/scene10_config_path.svg` (đường đi lệnh cấu hình) đã dựng sẵn.

Phần cần quay thật:

1. **Sửa Target Drops Per Minute.** Đổi từ 20 → 50, bấm **SAVE**, quay chậm rãi thao tác gõ số + bấm nút để dễ cắt/zoom lúc dựng.

---

## Cảnh 11 — OTA (đăng nhập vai kỹ thuật viên)

Đây là cảnh lâu nhất vì có bước chờ thật (~14 phút).

1. **Vào trang Devices, khối "Kho firmware (OTA)" ở đầu trang.** Đây là tính năng mới — upload trực tiếp qua web, không cần SSH.
2. **Chọn file `.ota` → Upload.** Bảng thêm dòng mới ngay, hiện mã nhà sản xuất đọc từ chính file (không phải từ tên file). Nếu muốn, thử upload nhầm file `.gbl`/`.s37` để quay luôn cảnh bị từ chối — chứng minh việc kiểm tra là thật.
3. **Kéo xuống khối FIRMWARE của thiết bị, bấm "Check for update".** Trạng thái đổi sang "Update available".
4. **Bấm "Update" → xác nhận.** Quay thanh tiến độ % + dòng nhắc không rút nguồn thiết bị.
   > Nạp thật mất khoảng **14 phút** (đo thật trên board — `docs/OTA.md` mục 8). Quay timelapse hoặc cắt thành nhiều đoạn ngắn rồi dựng nhanh lúc hậu kỳ — đừng tua giả toàn bộ thanh tiến độ.
5. **Board tự khởi động lại** (không rút nguồn) — quay màn OLED sáng lại ở bản mới.
6. **Bấm vào thiết bị, xem khối Device log.** Quay đúng 2 dòng hệ thống tự sinh: `"Update started: v2 to v4"` rồi sau đó `"Updated: v2 to v4"`. Không tự dịch lại — UI hiện đang là tiếng Anh.

---

## Cảnh 12 — Cao trào: mất tín hiệu cảm biến

**Quay một lần liên tục từ đầu đến cuối, không cắt ghép hai đoạn riêng** — thời gian phản ứng thật (~3 giây) phải khớp với thực tế trong `firmware/ai_fusion.c`.

Cần ít nhất 2 góc máy chạy song song: Camera A quay Dashboard, Camera B quay board.

1. Dashboard đang STABLE, HR/SpO₂ chạy bình thường.
2. Rút ngón tay khỏi MAX30102 (Camera B bắt khoảnh khắc này).
3. ~3 giây sau: LED chuyển 🟢→🟡, có tiếng BEEP (chu kỳ 1 giây — mức LINE_WARNING vì đây là mất tín hiệu, chưa phải nguy kịch bệnh nhân). **Mic thu tiếng bíp thật, không lồng hậu kỳ.**
4. Dashboard đổi: STABLE → WARNING, SpO₂: 97 → LOST, hiện alert đúng chữ thật **"No signal from: SpO2"**.
5. *(Tuỳ chọn)* Điện thoại nhận push notification thật (Camera thứ 3 nếu có người quay).
6. Đặt lại ngón tay, đo lại.
7. Dashboard trở lại 🟡 → 🟢.

> Vì sao lên vàng chứ không phải đỏ: mất tín hiệu cảm biến rơi vào nhánh cảnh báo đường truyền, không phải nhánh bệnh nhân nguy kịch. Nếu quay ra kết quả khác (vd. lên thẳng đỏ vì đồng thời có lỗi khác) — cứ quay đúng thực tế, đừng ép về vàng.

> **Đừng quay/dựng cảnh SMS hay cuộc gọi khi cảnh báo.** Đó là hướng mở rộng, hệ thống hiện chưa làm — chỉ có push notification (bước 5) là tính năng thật.

---

## Cảnh 13 — Toàn hệ thống cùng phản ứng

Không cần quay riêng — dựng lại bằng cách cắt nhanh từ footage đã quay ở Cảnh 6–12: LED (Cảnh 6) → log AI (Cảnh 7) → terminal (Cảnh 8) → dashboard cảnh báo (Cảnh 9/12), theo đúng thứ tự cảm biến → LED đổi màu → log AI → terminal → dashboard.

---

## Bàn giao cho bên truyền thông

Gửi kèm:

1. Toàn bộ raw footage, đặt tên theo số Cảnh (vd. `canh06_board_xg26.mp4`, `canh12_mat_tin_hieu.mp4`).
2. 4 file `.svg` trong `docs/images/` (Cảnh 5, 7, 8, 10).
3. `docs/Script.md` — để họ biết đoạn nào ghép với đoạn hoạt hình nào (nhãn 🎨/🎥/📊 ở đầu mỗi cảnh).
4. Một dòng nhắc: **không tự thêm/sửa số liệu hiển thị trên dashboard/terminal khi dựng.** Được phép zoom, crop, tăng tốc thời gian — nhưng số liệu trong khung hình phải giữ nguyên, không chỉnh sửa.

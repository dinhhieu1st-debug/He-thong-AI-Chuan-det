# 4. Xử lý tín hiệu và thuật toán cảnh báo

Toàn bộ phần này chạy **trên chip G26**. Server không tính lại gì cả.

Nguồn sự thật: `firmware/app.c`, `firmware/vitals_ai.cpp`, `firmware/drop_sensor.c`.

---

## 4.1. Lọc HR và SpO2

Cảm biến MAX30102 được đọc mỗi **250 ms**. Bốn lần đọc gộp thành một nhịp cập
nhật, nên AI nhận đúng **1 mẫu/giây** — 64 mẫu tương ứng khoảng 64 giây thật.

Chuỗi lọc để HR không nhảy loạn:

| Bước | Tham số |
|---|---|
| Cửa sổ trượt | 12 lần đọc (`VITALS_FILTER_WINDOW`) |
| Số mẫu hợp lệ tối thiểu | 8 (`VITALS_FILTER_MIN_SAMPLES`) |
| Loại xung bất thường | Lấy median của cửa sổ |
| Xác nhận thay đổi lớn | HR lệch > 20 BPM phải lặp lại 3 lần mới được nhận |
| Giới hạn tốc độ đổi | HR đã lọc đổi tối đa 4 BPM mỗi giây |
| Mất tín hiệu | Quá 3 giây → trả về "không có dữ liệu" và xoá cửa sổ cũ |

SpO2 đi qua cùng kiểu xử lý: cửa sổ lọc, xác nhận biến động, giới hạn bước
thay đổi.

**Hai quy tắc quan trọng:**

Dữ liệu giữ tạm để hiển thị **không** được tính thành mẫu AI mới. Bỏ tay ra
rồi đặt lại không làm chip tưởng đã có thêm 10 giây dữ liệu.

Không được rút ngắn 64 mẫu bằng cách lặp lại cùng một giá trị. Model học theo
trục thời gian; nhồi giá trị trùng làm nó hiểu sai khoảng cách giữa các mẫu và
phá hỏng cả trend lẫn forecast 16 giây.

---

## 4.2. Nhánh nhỏ giọt

Firmware so **từng khoảng thời gian giữa hai giọt** với khoảng mục tiêu suy ra
từ tốc độ bác sĩ đặt. Ngưỡng là **sai lệch tuyệt đối**, không phải tỉ lệ:

| Sai lệch so với mục tiêu | Mức |
|---|---|
| ≤ 200 ms | **1** — bình thường |
| > 200 ms và ≤ 800 ms | **2** — chú ý |
| > 800 ms | **3** — cảnh báo |

Thêm hai lớp bảo vệ:

- **Watchdog**: quá lâu không có giọt nào thì đẩy thẳng nhánh này lên mức 3.
  Không có giọt là trường hợp mà phép so khoảng thời gian ở trên không bao giờ
  chạy tới.
- **MLP/LSTM** trên cửa sổ 20 giọt cho ý kiến riêng về xu hướng. Mức cuối của
  nhánh nhỏ giọt là **max(mức vật lý, mức AI)** — dải vật lý vẫn là lớp bảo vệ
  chính, AI chỉ được phép làm nghiêm hơn chứ không được nới lỏng.

**Hạ mức phải chờ.** Leo thang có hiệu lực ngay từ một khoảng giọt xấu. Nhưng
để về lại mức 1 cần **3 khoảng giọt bình thường liên tiếp**
(`DRIP_RECOVERY_STREAK_REQUIRED`).

**Số hiển thị dùng median 7 khoảng gần nhất** (`DISPLAY_MEDIAN_SAMPLES`) cho đỡ
nhảy. Phán quyết cảnh báo thì dùng khoảng giọt thô — median là để dễ đọc, không
được che một giọt đến muộn.

---

## 4.3. Nhánh sinh hiệu

**60 mẫu đầu** dùng để dựng baseline HR/SpO2 riêng cho bệnh nhân đó. Chưa đủ 60
mẫu thì mọi so sánh tương đối đều bị vô hiệu — chỉ còn ngưỡng cứng.

| Điều kiện | Mức |
|---|---|
| HR < 45, HR > 150, hoặc SpO2 < 90 | **3** (ngưỡng cứng, không cần baseline) |
| Lệch ≥ 20% so với baseline | **3** |
| Lệch ≥ 15% và < 20% so với baseline | **2** |
| AI báo bất thường (forecast hoặc autoencoder, có xác nhận liên tiếp) | **2** |
| Còn lại | **1** |

Ngưỡng cứng **không đi qua bộ lọc chống báo giả**: SpO2 tụt là kêu ngay.

Nếu không model AI nào nạp được, ngưỡng cứng và so sánh baseline vẫn gánh toàn
bộ việc cảnh báo. Thiết bị không bao giờ im lặng chỉ vì AI hỏng.

---

## 4.4. Ghép hai nhánh thành mức cuối

Đúng ba dòng, không có ngoại lệ:

| Sinh hiệu | Nhỏ giọt | Mức cuối |
|---|---|---|
| 1 | 1 | **1** |
| 3 | 3 | **3** |
| mọi tổ hợp còn lại | | **2** |

Nghĩa là **mức 3 chỉ xảy ra khi cả hai nhánh cùng ở mức 3**. Đây là chủ ý: tắc
dây truyền là vấn đề thật và phải có người đi xử lý, nhưng cho nó cùng màu cùng
tiếng còi với tụt oxy là cách nhanh nhất khiến cả khoa thôi phản ứng với màu đỏ.

---

## 4.5. LED và buzzer

| Mức | LED | Buzzer |
|---|---|---|
| 1 | Xanh, sáng liên tục | Tắt |
| 2 | Vàng | Kêu 0,5 s — nghỉ 3 s |
| 3 | Đỏ, nhấp nháy | Bật/tắt mỗi 0,25 s |

Buzzer **active-low**: `PC06` LOW là kêu, HIGH là im.

---

## 4.6. Chế độ test trên web

Ba nút trong khối **Vitals test mode** ở panel chi tiết giường, cho phép kiểm
thử nhánh sinh hiệu mà không cần bệnh nhân thật:

| Nút | Giá trị gửi | Firmware làm gì |
|---|---|---|
| `Real data` | `0` | Dùng số cảm biến thật |
| `Fake HR L2` | `2` | HR = baseline × **0,83** (lệch 17%), SpO2 giữ nguyên → kiểm mức 2 |
| `Fake HR+O2 L3` | `3` | HR và SpO2 đều = baseline × **0,75** (lệch 25%) → kiểm mức 3 |

Không có giá trị `1`: "ép về bình thường" không tồn tại, vì dữ liệu thật vốn đã
là mức 1. Gateway từ chối thẳng mọi giá trị khác `0/2/3`.

> **Chỉ có tác dụng sau khi đã đủ 60 mẫu baseline** và giường đang ở trạng thái
> monitoring. Bấm sớm hơn thì firmware bỏ qua, vì các số fake được tính theo
> baseline — chưa có baseline thì không có gì để nhân với 0,83.

### Cách đọc kết quả

Khối này hiện **hai cặp số song song**:

| Dòng | Ý nghĩa |
|---|---|
| `Real HR / SpO2` | Số cảm biến thật, **không bao giờ** bị chế độ test làm thay đổi |
| `AI test HR / SpO2` | Số thực sự được đưa vào nhánh AI (attribute `0x0020`, `0x0021`) |

Số thật vẫn được firmware giữ song song, nên khi bấm về `Real data` chip phục
hồi history thật và **không phải học lại từ đầu**.

### Đây là cách xác nhận lệnh hai chiều

Toast trên web chỉ nói **server** đã nhận yêu cầu. Nó không chứng minh gì về
phần còn lại của chuỗi `server → gateway → MQTT → zigbee2mqtt → ZCL → G26`.

Bằng chứng duy nhất là **hai dòng số trên tách nhau ra**. Nếu `Real` thay đổi
mà `AI test` đứng yên, lệnh đã dừng lại ở đâu đó giữa đường — tra
[`07-su-co.md`](07-su-co.md), mục *Lệnh từ web không tác động tới G26*.

Cùng cơ chế đó áp dụng cho nút đặt tốc độ giọt: trường `target_drops_per_min`
mà chip báo ngược lên mới là xác nhận, không phải toast.

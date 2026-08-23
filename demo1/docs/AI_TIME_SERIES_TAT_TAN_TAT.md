# AI v2 — tất tần tật: kiến trúc, huấn luyện, đánh giá, nhúng

Tài liệu kỹ thuật đầy đủ của hệ AI trên chip. Người đọc mục tiêu: người sẽ **sửa**
phần AI này.

* Giải thích cho người không đọc code → [`AI_HOAT_DONG_THE_NAO.md`](AI_HOAT_DONG_THE_NAO.md)
* Dữ liệu và cách chia tập → [`Dataset_va_Phuong_phap_AI_SmartIV.md`](Dataset_va_Phuong_phap_AI_SmartIV.md)
* Luồng `.tflite` → chip → [`MLTK_AUTOGEN.md`](MLTK_AUTOGEN.md)
* Lý do đằng sau từng quyết định, kèm cả những lần đi sai → [`AI_V2_PLAN.md`](AI_V2_PLAN.md)

---

## 1. Ba model

| | Model 1 — Drip | Model 2 — Vitals | Model 3 — Vitals AE |
|---|---|---|---|
| Việc | dự báo dòng chảy 16 s tới | dự báo sinh hiệu 16 s tới | trạng thái sinh lý *hiện tại* |
| Input | `(1,1,64,1)` int8 | `(1,1,64,2)` int8 | `(1,2)` int8 |
| Output | `(1,16)` int8 | `(1,32)` int8 | `(1,2)` int8 |
| Kích thước | 22.400 B | 23.520 B | 3.136 B |
| Operator | **6** | **6** | **4** |
| Arena cấp / dùng thật | 4096 / **2836** B | 4096 / **2868** B | 2048 / **1108** B |

Tổng flash cho model: 49.056 B. Tổng arena: 10.240 B cấp phát, 6.812 B dùng thật.

### Chuẩn hoá tĩnh

Phải **khớp tuyệt đối** giữa `ml/common.py` và `firmware/ai_engine.h`. Lệch một
bên là không có lỗi build nào báo, chỉ có câu trả lời sai trong im lặng.

```
drops_ratio → (ratio − 1,0) / 0,35
heart_rate  → (hr   − 80,0) / 20,0
spo2        → (spo2 − 97,0) / 2,0
hr_lệch     → (hr − nền_bệnh_nhân) / 20,0      ← chỉ Model 3
```

Các hằng số này là **chọn**, không phải fit từ dữ liệu: một scaler fit trên
dataset sẽ âm thầm đổi mỗi lần sinh lại dataset, và hằng số trong firmware sẽ
trôi lệch mà không có gì báo.

---

## 2. Kiến trúc

### Hai forecaster

```
Input(1, 64, C)
Conv2D(16, (1,5), strides=(1,2), same, relu)   → (1, 32, 16)
Conv2D(32, (1,5), strides=(1,2), same, relu)   → (1, 16, 32)
Conv2D(32, (1,3), strides=(1,2), same, relu)   → (1,  8, 32)
Reshape((256,))                                 ← kích thước TĨNH
Dense(32, relu)
Dense(16 × C, linear)
```

**Vì sao `Conv2D` kernel 1×k chứ không phải `Conv1D`.** Chuỗi là một chiều nên
`Conv1D` là lựa chọn hiển nhiên — và là lựa chọn sai ở đây. TFLite bung mỗi
`Conv1D` của Keras thành `EXPAND_DIMS → CONV_2D → RESHAPE`, biến model 6 operator
thành 15, và phần lớn operator thêm vào **không được MVP tăng tốc**. Viết đúng
phép tính đó bằng `Conv2D` kernel `(1, k)` trên input `(1, 64, C)` cho ra **đúng
một `CONV_2D` mỗi lớp**, tất cả đều MVP chạy được.

**Cũng cố ý tránh:** LSTM/GRU (có trong TFLM nhưng MVP không tăng tốc) và dilated
convolution (MVP không hỗ trợ dilation).

### Autoencoder

```
Input(2)  →  Dense(4, relu)  →  Dense(1, relu)  →  Dense(4, relu)  →  Dense(2, linear)
```

**Nút thắt MỘT chiều cho hai đầu vào** là cơ chế, không phải con số tuỳ tiện.
Trạng thái tim–phổi bình thường không lấp đầy mặt phẳng HR/SpO2 — chúng nằm gần
một đường cong, và một chiều đủ để vẽ đường cong đó nhưng **quá nhỏ để học thuộc**
bất cứ thứ gì nằm ngoài. Hai chiều cho hai đầu vào sẽ thành ánh xạ đồng nhất, tái
tạo bất thường ngon lành như bình thường — cách kinh điển để tạo ra một
autoencoder **không phát hiện được gì**.

---

## 3. Huấn luyện

Cả ba: **chỉ trên dữ liệu bình thường** (unsupervised). Hai forecaster dùng loss
**Huber (δ = 1,0)**, không phải MSE — dữ liệu giọt có gai một-mẫu thật (một giọt
bị đếm trễ, giọt sau đúng giờ) và MSE bình phương chúng thành thành phần trội của
gradient, kéo cả dự báo về phía ngoại lai. Số liệu ICU cũng có nhiễu đầu dò tương
tự.

Validation dùng cho early stopping cũng **chỉ chứa cửa sổ bình thường**: dừng
theo một loss có lẫn ca tắc nghẽn sẽ chọn ra checkpoint **dự báo tắc nghẽn giỏi
nhất** — tức là checkpoint tệ nhất cho việc phát hiện.

### Level augmentation — bắt buộc, không phải tuỳ chọn

Lần train đầu của Model 1:

| Tập | MAE model | MAE baseline (persistence) | |
|---|---:|---:|---|
| validation mô phỏng | 0,0082 | 0,0092 | +11,3% |
| **bản ghi thật** | **0,0512** | **0,0105** | **−390%** |

Tệ hơn baseline **5 lần** trên dữ liệu thật. Chẩn đoán: **93% sai số là độ lệch
hằng số**.

```
cửa sổ bình thường MÔ PHỎNG : tỉ số trung bình 0,9984
cửa sổ bình thường THẬT     : tỉ số trung bình 0,9490
```

Ca truyền mô phỏng nằm đúng y lệnh; bàn thử thật chạy chậm hơn ~5% — do khoá
lăn, do dây, do chiều cao treo, **không cái nào là sự cố**. Model đã âm thầm học
"bình thường nghĩa là 1,0" và kéo mọi dự báo về đó.

Mức vận hành tuyệt đối **không phải việc của forecaster**. Quyết định 0,95 có
chấp nhận được không là việc của luật lâm sàng, vốn so với y lệnh. Forecaster chỉ
phải trả lời *"với cách đường truyền này đang hành xử, tiếp theo sẽ ra sao"*.

Vì vậy mỗi cửa sổ huấn luyện được **dịch một lượng ngẫu nhiên**, áp **giống nhau**
cho cả phần lịch sử lẫn phần dự báo — động lực giữ nguyên, chỉ mức vận hành đổi.
Model không còn đọc được mức từ thiên lệch của chính nó và buộc phải lấy từ cửa
sổ. **Đây là thứ làm cho model chuyển được từ dữ liệu mô phỏng sang phần cứng
thật.**

Model 2 dùng cùng cơ chế, với lý do lâm sàng: nhịp nghỉ 55 và 95 đều bình thường,
khác người. Biên độ dịch: HR ±0,75 (15 bpm), SpO2 ±0,25 — SpO2 ít hơn nhiều vì
bão hoà của người khoẻ thật sự nằm trong vài điểm quanh 97, biến thiên cá thể như
HR **không tồn tại**.

---

## 4. Thước đo: vì sao KHÔNG phải MAE

Đo lại sau khi thêm augmentation, cả hai forecaster **vẫn tệ hơn** baseline
persistence (dự báo = giá trị hiện tại) trên dữ liệu bình thường — khoảng 8% trên
validation, 20% trên bản ghi thật. Nhìn riêng thì như model hỏng.

Không hỏng, và lý do chính là cơ chế mà cả thiết kế dựa vào: model **chỉ học động
lực bình thường**, nên nó **từ chối bám theo** động lực bất thường — khi dòng
chảy bắt đầu sụp nó vẫn dự báo dòng chảy bình thường, và **residual bùng lên**.
Persistence thì bám theo mọi thứ theo đúng cấu tạo: trong một ca tắc nghẽn chậm
nó theo sát, residual bé tí. **Làm forecaster ngoan khiến persistence thành
detector tồi.**

Vì vậy MAE chỉ là chỉ số chẩn đoán. **Thước đo quyết định là AUC phát hiện.**

### Kết quả (đo trên bản int8 sẽ nạp chip)

| Model | Tập | AUC model | AUC persistence |
|---|---|---:|---:|
| **Drip** | **bản ghi thật leak-free** | **0,948** | 0,839 |
| Drip | validation mô phỏng | 0,890 | 0,732 |
| Drip | test mô phỏng | 0,940 | 0,766 |
| Drip | bản ghi thật calibration-influenced | 0,813 | 0,873 |
| **Vitals** | **12 bệnh nhân chưa từng thấy** | **0,885** | 0,680 |
| Vitals AE | test theo từng giây | **0,9996** | — |

**Đọc AUC của AE cho đúng:** nhãn "bất thường" của nó được định nghĩa **bằng
chính** ngưỡng lâm sàng, nên AUC ≈ 1 phần lớn chỉ nói rằng AE **tái hiện được
ngưỡng**. Giá trị tăng thêm nằm ở mục 5, không ở con số này. **Đừng lấy 0,9996
làm điểm nhấn trong báo cáo.**

**Chi phí lượng tử hoá:** drip mất 0,033 AUC (0,981 float → 0,948 int8) — vẫn bỏ
xa baseline, nhưng là chi phí có thật. Vitals thì int8 **nhỉnh hơn** float (0,856
→ 0,885): nhiễu lượng tử triệt bớt các residual nhỏ.

---

## 5. Model 3 bắt được gì mà ngưỡng cứng bỏ sót

Quét lưới toàn bộ vùng **không luật cứng nào kích hoạt** (SpO2 ≥ 90, HR trong
45–150), nền HR = 80:

| HR | SpO2 | Luật cứng | AE (ngưỡng 3,46) |
|---:|---:|---|---:|
| 45 | 90,0 % | không báo | **8,91 → BÁO** |
| 45 | 93,0 % | không báo | **3,83 → BÁO** |
| 105 | 93,0 % | không báo | 3,04 (sát ngưỡng) |
| 80 | 98,0 % | không báo | 0,07 |

Đo cấu trúc mà model học được:

```
chỉ nhịp nhanh   (HRlệch +25, SpO2 98)  →  0,841
chỉ tụt oxy      (HRlệch   0, SpO2 93)  →  2,266
nền              (HRlệch   0, SpO2 98)  →  0,069
tổ hợp cả hai    (HRlệch +25, SpO2 93)  →  3,037
```

`0,841 + 2,266 − 0,069 = 3,038` so với **3,037** đo được — sai số của tổ hợp
đúng bằng **tổng** hai thành phần.

Ban đầu nhóm kết luận vội rằng "cộng tính ⇒ không học được tương tác ⇒ vô dụng".
**Sai.** Cộng tính theo *độ lệch* chính là điều cần: nó diễn đạt được *"hai sai
lệch đều dưới ngưỡng, cộng lại thành đáng báo"*. Cái sẽ vô dụng là nếu AE hành xử
như phép **OR/max** — khi đó nó chỉ lặp lại đúng hệ ngưỡng. Hai luật cứng OR với
nhau **không thể** biểu diễn được hàng đầu tiên của bảng trên.

Trên dữ liệu thật: gắn cờ **0,16%** số giây bình thường, bắt **99,75%** số giây
bất thường.

### Nền nhịp tim phải là số ĐO ĐƯỢC

Model 3 và luật cứng đều so với nền riêng của bệnh nhân, nên cơ chế chốt nền
phải đúng. Chạy trên phần cứng thật lộ ra hai lỗi:

1. Nền bị chốt **vô điều kiện** ở mốc 60 giây, kể cả khi chưa cắm cảm biến — lúc
   đó `sh_hr()` trả về giá trị mặc định. Log của board ghi
   `[AI] HR baseline locked at 80 bpm`, một con số không ai đo, rồi `calib_done`
   được đặt nên **không bao giờ thử lại**. Hệ quả đo được ngay trên bàn: một
   người thật 131 bpm trông như lệch **64%** khỏi "nền của họ" và làm nổ luật
   cứng. Y tá bật máy trước rồi mới kẹp cảm biến — thứ tự bình thường — sẽ dính
   lỗi này mọi lần.
2. Nó lấy **một mẫu tức thời** ở mốc 60 giây, trong khi tài liệu mô tả là trung
   vị cả cửa sổ. Một nhịp PPG nhiễu đủ để một khoảnh khắc xấu trở thành mốc tham
   chiếu cho cả ca truyền.

Nay: thu mẫu mỗi giây và **chỉ khi kênh thật sự có tín hiệu** (`CH_OK`), nền là
**trung vị** của chúng. Hết 60 giây mà chưa đủ 20 mẫu thật thì **khởi động lại
cửa sổ** thay vì bịa ra một con số:

```
[HR] Only 0/20 samples in 60s - sensor not attached? Restarting the baseline window.
```

Thiết bị vẫn chạy trên giá trị mặc định trong lúc chờ; thứ nó **không** được làm
là coi giá trị mặc định đó là nền đã đo của bệnh nhân này.

---

## 6. Bộ hợp nhất trên chip

`firmware/ai_fusion.c`. Mỗi giây, sau khi cả ba model chạy.

### Chấm điểm nhân quả

AUC ở mục 4 so dự báo với **toàn bộ** 16 giây theo sau. Hợp lý để xếp hạng model
ngoại tuyến, nhưng chip **không nhìn thấy tương lai**. Trên chip, residual là:
lấy dự báo mà model đưa ra **một giây trước** cho **bây giờ**, so với số vừa đo.
Đó là residual duy nhất tính được thời gian thực — chip chấm bài tập của hôm qua.

### Ngưỡng

Phân vị 98 của residual nhân quả trên dữ liệu **validation bình thường**, đo trên
**model int8**:

```c
#define AI_DRIP_RESIDUAL_THRESHOLD    0.0662f
#define AI_VITALS_RESIDUAL_THRESHOLD  0.5295f
#define AI_AE_THRESHOLD               3.460599f   // float cho 3.466246 — khác model, khác ngưỡng
```

Đây **không phải núm vặn** để chỉnh cho demo đẹp.

### Bộ lọc kéo dài K = 11

Tín hiệu do AI phát hiện phải giữ **11 giây liên tiếp** mới được nâng thành báo
động. Đo bằng cách phát lại bản ghi thật ở 1 Hz:

| | Báo giả/giờ trước K=11 | Sau K=11 | Dữ liệu bình thường |
|---|---:|---:|---|
| Drip | 29,0 | **0,0** | 0,03 h |
| Vitals | 47,6 | **0,0** | 1,14 h |

Recall drip: **6/8** bản ghi bất thường có báo động, độ trễ thêm trung vị 17 giây.

**Luật lâm sàng cứng KHÔNG đi qua bộ lọc.** SpO2 < 90% báo ngay tick nhìn thấy.
Lập luận chống báo động giả biện minh cho việc chờ hết một nháy thoáng qua; nó
**không** biện minh cho việc chờ hết một ca tụt oxy.

### Residual bắt gì và KHÔNG bắt gì

Hệ quả trực tiếp của level augmentation: khi sự cố ổn định vào một **trạng thái
dừng mới**, model dự báo trạng thái đó rất chuẩn và **residual về 0**.

Nên residual bắt **CHUYỂN BIẾN** — dây bắt đầu tắc, tốc độ bắt đầu chạy loạn — và
bắt sớm, đó là toàn bộ giá trị của nó. Nó **không** giữ báo động qua một sự cố
kéo dài, và chưa bao giờ định làm việc đó.

Giữ báo động là việc của những thứ nhìn **trạng thái hiện tại**: luật lâm sàng
cứng, luật cân↔giọt, và autoencoder. Logic hai nhánh OR chúng lại chính vì lý do
này — **bỏ đi một cái thì một sự cố đã ổn định sẽ âm thầm tắt chuông trong khi
bệnh nhân vẫn đang gặp sự cố**.

### Cảnh báo sớm theo dự báo

Cơ chế thứ ba, tồn tại vì một khoảng trống đo được: diễn biến **xấu dần từ từ**
rất dễ dự báo, nên model bám sát và residual không nhúc nhích — model đúng, bệnh
nhân vẫn đang chìm. Vì vậy kiểm tra thêm: **bản thân dự báo** có vượt ngưỡng lâm
sàng trong 16 giây tới không. Đây là cơ chế duy nhất ở đây có thể cảnh báo
**trước** một ca tụt oxy diễn ra chậm. Nó xếp vào mức cảnh báo chứ không phải báo
động — vì nó là dự đoán, chưa phải sự thật.

### Ma trận quyết định

```
nhánh DỊCH      = Model 1 bất thường  HOẶC  luật cân↔giọt kết luận tắc/chảy tự do
nhánh BỆNH NHÂN = Model 2 bất thường  HOẶC  Model 3 bất thường  HOẶC  luật cứng
```

| Nhánh dịch | Nhánh bệnh nhân | Cấp |
|---|---|---|
| — | — | 0 NORMAL |
| ✓ | — | 1 LINE_WARNING |
| — | ✓ | 2 VITALS_ALERT |
| ✓ | ✓ | **3 CRITICAL** |

Đây là chỗ việc tách ba model trả cổ tức: với một model gộp, cấp 1 và cấp 2
**không phân biệt được**, vì một điểm bất thường chung không nói được hỏng ở đâu.

---

## 7. Nhúng vào firmware

### Ba interpreter, ba arena

Cố ý, và tốn thêm vài KB RAM. Gộp vào một flatbuffer nghĩa là một
`AllocateTensors()` (all-or-nothing: thiếu bộ nhớ là mất cả ba) và một `Invoke()`
(dừng ở operator lỗi đầu tiên, nên trục trặc ở nhánh drip sẽ **âm thầm làm hỏng**
đầu ra của nhánh vitals). Hỏng độc lập đáng giá hơn vài KB.

Mọi hàm trả `bool`. Không chỗ nào trong `ai_engine.cpp` treo, assert hay lặp vô
hạn: model không nạp được thì thiết bị **vẫn phải đo và vẫn phải báo động bằng
luật lâm sàng**.

### Đối chiếu Python ↔ chip

Sau khi nạp, log khởi động in ra tham số lượng tử hoá mà chip đọc từ tensor:

```
[AI] drip   ready: arena 2836/4096 B, in scale 39214/1e6, zp -55
[AI] vitals ready: arena 2868/4096 B, in scale 76469/1e6, zp  49
[AI] ae     ready: arena 1108/2048 B, in scale 66618/1e6, zp  75
```

Đã đối chiếu với ba file `.tflite`: **khớp tuyệt đối cả ba**. Đây là bằng chứng
đầu-cuối rằng số liệu trong tài liệu này là số liệu của model đang chạy trên chip.

---

## 8. Chạy lại toàn bộ

```bash
# dataset
.venv-ai/bin/python ml/dataset/make_drip_timeseries.py
.venv-ai/bin/python ml/dataset/make_drip_realtest.py
.venv-ai/bin/python ml/dataset/make_vitals_timeseries.py
.venv-ai/bin/python ml/dataset/make_vitals_ae.py

# huấn luyện + xuất int8
.venv-ai/bin/python ml/train_drip_forecaster.py
.venv-ai/bin/python ml/train_vitals_forecaster.py
.venv-ai/bin/python ml/train_vitals_ae.py

# đánh giá (báo giả trước/sau K=11)
.venv-ai/bin/python ml/evaluate.py

# sinh code C bằng tool MLTK
.venv-ai/bin/python ml/export_c_headers.py

# kiểm thử logic hợp nhất trên máy, không cần chip
cc -I firmware -o /tmp/fusion_test tools/fusion_test.c \
   firmware/ai_fusion.c firmware/line_rules.c -lm && /tmp/fusion_test
```

Build và nạp firmware: xem [`MLTK_AUTOGEN.md`](MLTK_AUTOGEN.md) mục 10.

---

## 9. Giới hạn

Nhắc lại ở đây để không ai đọc mỗi tài liệu này rồi kết luận mạnh hơn sự thật.

1. **Recall của nhánh sinh hiệu không đo được trên BIDMC.** Cả tập test chỉ có 2
   lần vi phạm ngưỡng: một ca đã vi phạm từ **giây đầu tiên** của bản ghi (không
   thể cảnh báo sớm cho thứ có trước cả bản ghi), và một ca chỉ kéo dài **một
   giây** (K=11 cố ý không báo — báo mới là lỗi). Đo được: **tỉ lệ báo giả**.
   Không đo được: **recall**. Vì vậy luật lâm sàng cứng là lưới an toàn **chính**
   cho sinh hiệu.
2. **Dữ liệu drip bình thường chỉ 0,03 giờ** → con số "0 báo giả/giờ" của nhánh
   drip còn mỏng về thống kê.
3. **Model 1 huấn luyện phần lớn trên dữ liệu mô phỏng** (dù đã hiệu chỉnh từ
   session thật và **test hoàn toàn trên dữ liệu thật**), trong khi Model 2 và 3
   là 100% ICU thật. Sự bất đối xứng này không nên gộp chung thành câu "AI được
   huấn luyện trên dữ liệu thật".
4. **Lượng tử hoá int8 làm drip mất 0,033 AUC.**

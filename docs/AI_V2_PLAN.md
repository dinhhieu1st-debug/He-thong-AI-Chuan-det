# Kế hoạch nâng cấp AI v2 — Smart IV Monitor

Trạng thái: **đã duyệt, đang triển khai**. Ngày lập: 2026-08-17.

Tài liệu này là bản kế hoạch thi công, ghi cả *vì sao* mỗi quyết định được chọn,
để sau này đọc lại không sửa nhầm đúng những chỗ đã cân nhắc kỹ. Bản đặc tả gốc
là `AI_SYSTEM_SPECIFICATION.md`; chỗ nào bản kế hoạch này khác đặc tả gốc thì
**bản kế hoạch này thắng**, và lý do được ghi rõ ở mục 2.

---

## 1. Mục tiêu

AI hiện tại có hai model: một autoencoder 6 đặc trưng và một forecaster **gộp 4
kênh** (HR, SpO2, drops, weight) trong cùng một cửa sổ 64 giây.

**Vấn đề cốt lõi — đã đo được, không phải suy đoán.** Ban đầu bản kế hoạch này
lập luận rằng dataset v1 chứa "tương quan ảo" giữa sinh hiệu và giọt. Đo lại thì
**lập luận đó sai**: tương quan tuyến tính chéo nguồn trong
`iv_hybrid_1hz.csv` (75.036 dòng) vốn đã xấp xỉ 0:

```
corr(heart_rate, drops_per_min) = -0.0117
corr(heart_rate, weight_g)      = -0.0461
corr(spo2,       drops_per_min) = -0.0180
```

Khuyết điểm thật nằm ở **kiến trúc, không phải ở dữ liệu**. Đo trực tiếp trên
`ml/models/forecaster_int8.tflite` — đúng file đang chạy trên chip — bằng cách
giữ nguyên hai kênh sinh hiệu và **chỉ** thay đổi hai kênh đường truyền sang trạng
thái tắc nghẽn:

| Kênh đầu ra | Thay đổi dự báo | Quy ra đơn vị lâm sàng |
|---|---|---|
| HR | 0,0993 | **≈ 2,0 bpm** |
| SpO2 | 0,1642 | **≈ 0,33 %** |
| drops | 1,4132 | (kênh bị tác động — đúng) |
| weight | 1,7315 | (kênh bị tác động — đúng) |

> Nói cách khác: **tắc dây truyền làm dịch chuyển dự báo nhịp tim của bệnh nhân
> khoảng 2 bpm**, chỉ vì hai thứ dùng chung một mạng. Không có bệnh nhân nào có
> nhịp tim thay đổi vì dây truyền bị gập. Đây là hệ quả của việc một mạng dùng
> chung trọng số cho cả 4 kênh, không phải của việc dataset bị lệch.

Hệ quả thứ hai, và là hệ quả nghiêm trọng hơn về mặt lâm sàng: v1 chỉ có **một
điểm bất thường chung**, nên không trả lời được câu hỏi mà y tá cần trả lời đầu
tiên — *"hỏng ở dây truyền hay hỏng ở bệnh nhân?"*. Hai sự cố có cách xử trí hoàn
toàn khác nhau lại phát ra cùng một tín hiệu.

AI v2 giải quyết bằng cách **tách rời**: ba mạng riêng, ba điểm bất thường riêng,
nên (a) dữ liệu đường truyền không thể tác động vào dự báo sinh hiệu, và (b) hệ
thống quy được trách nhiệm cho đúng nguồn gây ra sự cố — cơ sở cho ma trận cảnh
báo 4 cấp ở mục 4.

Mục tiêu cần đạt (không phải cách làm — cách làm có thể đổi):

1. Không còn tương quan ảo giữa sinh hiệu và dữ liệu giọt ở bất kỳ đâu.
2. Cảnh báo 4 cấp, phân biệt được **sự cố đường truyền** với **sự cố bệnh nhân**.
3. Triệt tiêu báo động giả do nháy 2–6 giây (ho, cử động) bằng bộ lọc kéo dài.
4. Phân biệt được **hết dịch** với **tắc dây** — hai thứ nhìn từ số giọt là giống hệt nhau.
5. AI hỏng thì thiết bị **vẫn đo và vẫn báo động bằng luật lâm sàng**.

---

## 2. Bốn quyết định khác với đặc tả gốc

### 2.1. Ba model chạy riêng, KHÔNG gộp thành một flatbuffer

Đã cân nhắc gộp 3 nhánh vào 1 file `.tflite` để dùng trọn luồng autogen của
Silabs. **Bác bỏ**, vì gộp nghĩa là chỉ còn một `MicroInterpreter`, một
`AllocateTensors()`, một `Invoke()`:

* `AllocateTensors()` là all-or-nothing — cấp phát arena cho tensor của cả 3
  nhánh trong một lần; thiếu bộ nhớ là mất sạch cả ba.
* `Invoke()` chạy op tuần tự và **dừng ngay khi gặp lỗi**. Op của nhánh drip lỗi
  thì các op của nhánh vitals không bao giờ được chạy. Không có cách nào nói
  "nhánh này hỏng nhưng nhánh kia vẫn đúng".

Ba interpreter riêng, ba arena riêng: hỏng một model thì hai model kia vẫn chạy.
Đổi lại tốn thêm vài KB RAM — chấp nhận được trong ngân sách 256 KB.

### 2.2. TẮT `SL_TFLITE_MICRO_INTERPRETER_INIT_ENABLE`

Đọc `aiml_2.2.2/src/tflite/sl_tflite_micro_init.cc` thì thấy nó xử lý lỗi bằng
vòng lặp vô hạn:

```c
if (model->version() != TFLITE_SCHEMA_VERSION) { TF_LITE_REPORT_ERROR(...); while (1); }
if (interpreter->AllocateTensors() != kTfLiteOk) { TF_LITE_REPORT_ERROR(...); while (1); }
```

Hàm này được gọi ở `autogen/sl_event_handler.c:94`, tức là **trong khởi động hệ
thống, trước cả app**. Model lỗi → chip treo → không cảm biến, không OLED, không
Zigbee, không luật lâm sàng. Thiết bị đầu giường thành cục gạch im lặng.

Điều này **mâu thuẫn trực tiếp** với nguyên tắc thiết kế đã ghi trong README:
*"không load được model AI — thiết bị vẫn đo và vẫn báo động bằng luật lâm
sàng."* Trong thiết bị y tế, treo im lặng là kiểu hỏng tệ nhất có thể có.

Vì vậy đặt `SL_TFLITE_MICRO_INTERPRETER_INIT_ENABLE = 0` trong
`config/sl_tflite_micro_config.h`. `sl_tflite_micro_init()` thành hàm rỗng,
không còn `while(1)` nào. `ai_engine.cpp` tự dựng interpreter và **tự xử lý lỗi
bằng `return false`**.

### 2.3. Vẫn dùng tool MLTK của Silabs — cho cả 3 model

Cơ chế "bỏ `.tflite` vào `config/tflite/` → autogen tự sinh" chỉ nhận **một**
file (`flatbuffer_converter.py`, hàm `find_first_tflite_file`). Nhưng ta gọi
thẳng chính tool đó **3 lần**, mỗi lần cho một model, rồi đổi tên symbol. Cả 3
model vẫn được sinh bằng **tool chính hãng của Silabs**, giữ nguyên lợi ích:

* mảng byte sinh tự động, không phải commit header viết tay;
* **opcode resolver tự suy ra từ chính flatbuffer** — không còn nguy cơ quên
  `AddXxx()` khi đổi kiến trúc model. (Đã kiểm chứng: cho autoencoder vào, tool
  sinh đúng `MicroMutableOpResolver<1>` với duy nhất `AddFullyConnected()`.)

Cần vá 2 chỗ, làm trong `ml/export_c_headers.py`:

1. **Shim `imp`** — thư viện `flatbuffers` mà Silabs đóng gói kèm còn dùng
   `import imp`, mà Python 3.12 đã bỏ module này. Nó chỉ gọi đúng
   `imp.find_module('numpy')`, nên chèn một module `imp` giả có hàm đó là xong.
2. **Đổi tên symbol** — tool luôn sinh ra `sl_tflite_model_array` và macro
   `SL_TFLITE_MICRO_OPCODE_RESOLVER`. Ba model dùng chung tên sẽ đụng nhau, nên
   đổi thành `g_drip_model_array` / `DRIP_OPCODE_RESOLVER`, v.v.

Phụ thuộc Python cần thêm vào venv: `pyyaml`, `jinja2` (đã cài).

### 2.4. Loadcell RA KHỎI model AI, chuyển thành luật kiểm chứng chéo

Đặc tả gốc cho `relative_weight` làm kênh thứ 2 của Model 1. **Bác bỏ**, 3 lý do:

1. **Tái phạm đúng cái tội mà v2 sinh ra để diệt.** Không có một byte dữ liệu
   loadcell thật nào (`AI-nho-giot` chỉ có cảm biến giọt). Kênh cân sẽ hoàn toàn
   do ta mô phỏng, nên CNN sẽ học **mối quan hệ do chính script sinh dữ liệu bịa
   ra** giữa cân và giọt. Vẫn là tương quan ảo, chỉ đổi cặp.
2. **Dư thừa.** Cân và giọt đo cùng một hiện tượng vật lý — dịch rời khỏi bình:
   `dW/dt ≈ giọt/phút × thể tích mỗi giọt`. Gần như không thêm thông tin.
3. **Quan hệ này đơn giản, xác định, và ta biết trước.** Bắt mạng nơ-ron học lại
   một quy luật đã biết là lãng phí, và mất khả năng giải thích.

Thay vào đó, cân làm việc bằng luật số học thuần. Nhận xét vật lý nền tảng: bình
đầy → cột áp thủy tĩnh cao → chảy nhanh; gần hết → cột áp thấp → chảy chậm. **Chảy
chậm lúc gần hết là bình thường, không phải tắc.** Cân đứng yên hay không chính
là thứ phân biệt dứt khoát:

| Cân | Giọt | Kết luận |
|---|---|---|
| ↓ tương ứng | ↓ chậm lại | Dịch **vẫn đang ra** → gần hết / cột áp thấp → **bình thường**, chỉ nhắc "sắp hết" |
| **đứng yên** | ↓ chậm lại | Dịch **không ra khỏi bình** → **TẮC DÂY** → cảnh báo |
| ↓ rất nhanh | ↑ | **Chảy tự do** |
| đứng yên | vẫn đếm đều | Cảm biến giọt đếm nhầm (nắng, rung) → cảnh báo **kỹ thuật**, không phải lâm sàng |

Cộng thêm ước lượng **"còn ~X mL, còn ~Y phút"** → báo y tá sắp hết dịch. Chỉ là
phép chia từ cân và tốc độ, và thực tế đây là tính năng y tá cần nhất trong ngày.

**Hệ quả tốt:** sau thay đổi này, mọi kênh đầu vào của cả 3 model đều có gốc từ
dữ liệu **đo thật** — BIDMC cho sinh hiệu, cảm biến giọt ESP8266 cho drip. Không
còn kênh nào thuần bịa.

---

## 3. Ba model sau khi chốt

| | Model 1 — Drip | Model 2 — Vitals | Model 3 — Vitals AE |
|---|---|---|---|
| Mục tiêu | Dự báo dòng chảy 16s tới | Dự báo sinh hiệu 16s tới | Trạng thái sinh lý *hiện tại* có bình thường không |
| Input | `(1,1,64,1)` int8 — `drops_ratio` | `(1,1,64,2)` int8 — `hr`, `spo2` | `(1,2)` int8 — `hr_lệch`, `spo2` |
| Output | `(1,16)` int8 | `(1,32)` int8 | `(1,2)` int8 |
| Nguồn dữ liệu | Cảm biến giọt (`AI-nho-giot`) | PhysioNet BIDMC, 52 bệnh nhân | PhysioNet BIDMC (chỉ sinh hiệu) |
| Arena | 4 KB | 4 KB | 2 KB |

> **Model 3 KHÔNG nhận kênh giọt.** Bản đầu cho AE ăn 3 kênh gồm cả giọt. Sai hai
> lần: (a) không có gì để học — không bản ghi nào đo cùng lúc nhịp tim và tốc độ
> giọt của cùng một bệnh nhân, nên hai nguồn phải ghép ngẫu nhiên, mà ghép ngẫu
> nhiên thì **độc lập theo đúng cấu tạo**; AE tiêu nút thắt cổ chai để mô hình hoá
> ba phân phối biên rời rạc, thứ mà ngưỡng đơn lẻ đã làm được; (b) nghiêm trọng
> hơn, kênh giọt nằm chung mạng nghĩa là **tắc dây làm tăng sai số tái tạo, tức
> làm bẩn phán đoán về BỆNH NHÂN** — đúng lỗi coupling của v1, thu nhỏ lại.
>
> HR và SpO2 thì được **đo cùng lúc trên cùng một bệnh nhân**. Đó là quan hệ đa
> biến có thật, và là quan hệ duy nhất có thật trong cả hệ thống. Model 3 học
> đúng nó và không học gì khác. Việc kết hợp với nhánh giọt diễn ra **sau đó, ở
> logic tường minh** trong bộ hợp nhất — nên giải thích được cho y tá, thay vì
> chôn trong trọng số mạng.

**Model 3 có bắt được gì mà ngưỡng cứng bỏ sót không — đã đo.** Quét lưới toàn bộ
vùng **không luật cứng nào kích hoạt** (SpO2 ≥ 90, HR trong 45–150):

| HR | SpO2 | Luật cứng | AE (ngưỡng 3,46) |
|---|---|---|---|
| 45 bpm | 90,0 % | không báo | **8,91 → BÁO** |
| 45 bpm | 93,0 % | không báo | **3,83 → BÁO** |
| 105 bpm (lệch +25) | 93,0 % | không báo | 3,04 → sát ngưỡng |
| 80 bpm | 98,0 % | không báo | 0,07 → bình thường |

Sai số của tổ hợp đúng bằng **tổng** sai số hai thành phần riêng (0,841 + 2,266 −
0,069 = 3,038; đo được 3,037). Cộng tính theo *độ lệch* chính là điều cần: nó
diễn đạt được "hai sai lệch đều dưới ngưỡng, cộng lại thành đáng báo" — điều mà
hai luật cứng OR với nhau **không thể** biểu diễn. (Nếu AE hành xử như OR/max thì
mới là vô dụng, vì khi đó nó chỉ lặp lại ngưỡng cứng.)

Trên dữ liệu thật: gắn cờ **0,16%** số giây bình thường, bắt **99,75%** số giây
bất thường, AUC int8 **0,9996**. Lưu ý đọc AUC này cho đúng: nhãn "bất thường"
được định nghĩa *bằng chính* ngưỡng lâm sàng, nên AUC cao phần lớn nói rằng AE tái
hiện được ngưỡng — giá trị tăng thêm nằm ở bảng lưới bên trên, không ở AUC.

Chuẩn hoá tĩnh (phải khớp tuyệt đối giữa Python và firmware):

* `drops_ratio` = dpm thực tế / dpm y lệnh → `(ratio - 1.0) / 0.35`
* `heart_rate` → `(hr - 80.0) / 20.0`
* `spo2` → `(spo2 - 97.0) / 2.0`

**Kiến trúc Model 1 & 2** (tối ưu cho bộ tăng tốc MVP): `Conv2D` kernel `1×k` chứ
**không** dùng Keras `Conv1D` — `Conv1D` bị TFLite bung thành
`EXPAND_DIMS`/`CONV_2D`/`RESHAPE`, thổi model từ 6 lên 15 operator mà phần lớn
không được tăng tốc. Cũng **không** dùng LSTM/GRU (có trong TFLM nhưng MVP không
tăng tốc) và **không** dùng dilated conv (MVP không hỗ trợ dilation).

```
Conv2D(16, (1,5), strides=(1,2), same, relu)
Conv2D(32, (1,5), strides=(1,2), same, relu)
Conv2D(32, (1,3), strides=(1,2), same, relu)
Reshape((256,))            # kích thước tĩnh
Dense(32, relu)
Dense(out_len, linear)
```

**Model 3**: `Dense(3,relu) → Dense(2,relu) → Dense(3,relu) → Dense(3,linear)`.
Chỉ số bất thường là MSE tái tạo; ngưỡng chốt ở **phân vị 98%** của tập
validation bình thường.

Lượng tử hoá int8: `TFLiteConverter.from_keras_model`, `representative_dataset`
riêng cho từng input, `Optimize.DEFAULT`, `TFLITE_BUILTINS_INT8`,
`inference_input_type = inference_output_type = int8`, **batch size cố định = 1**
khi xuất để tránh sinh op `SHAPE`/`PACK` động.

> **Bẫy đã phát hiện lúc dựng dataset — `representative_dataset` PHẢI chứa cả
> window bất thường.** Đo trên dữ liệu thật: dải `drops_ratio` sau chuẩn hoá là
> **−2,75 … +6,93**, trong khi dải bình thường chỉ là **±0,29**. Model *train*
> trên window bình thường (đúng như thiết kế), nhưng nếu `representative_dataset`
> lúc lượng tử hoá cũng chỉ có window bình thường thì scale int8 sẽ bị chốt quanh
> ±0,3, và **mọi bất thường đều bão hoà về cùng một giá trị** — chip sẽ không
> phân biệt nổi "chậm nhẹ" với "tắc hoàn toàn". Không có lỗi build nào bắt được
> chuyện này; nó chỉ hiện ra dưới dạng model im lặng khi cần báo động nhất.
> Vì vậy: **train = chỉ bình thường, calibrate = toàn dải.**

### Dữ liệu và cách chia tập

**Drip** — repo `AI-nho-giot` **không trộn** raw với synthetic; synthetic v1 được
*hiệu chỉnh số học* từ 5 session thật (`003, 005, 006, 009, 010`), còn `data/raw/`
giữ nguyên vẹn. Theo đúng nguyên tắc đó:

* Train + validation: synthetic 8 kịch bản (240 session × 180 record), resample 1 Hz.
* Test: **chỉ session thật, và chỉ những session không tham gia hiệu chỉnh** →
  `001, 002, 004, 007, 008, 011`. Không rò rỉ, và câu "test trên dữ liệu cảm biến
  thật" là trung thực.

**Vitals** — 52 file BIDMC. Chia **theo mã bệnh nhân** `bidmc_src` (31 train / 10
val / 11 test), **tuyệt đối không chia ngẫu nhiên theo dòng** — cùng một bệnh nhân
nằm ở cả train lẫn test là rò rỉ dữ liệu, và điểm số sẽ đẹp một cách vô nghĩa.

**Snapshot AE** — HR/SpO2 lấy từ BIDMC, drip lấy từ drip dataset, **ghép ngẫu
nhiên độc lập**. Đây là điểm mấu chốt: AE học *biên độ hợp lệ của từng kênh và tổ
hợp của chúng*, chứ không học tương quan giữa hai nguồn.

Cả 3 model đều train **chỉ trên đoạn bình thường** (unsupervised), loss **Huber
(δ = 1.0)** cho 2 forecaster.

---

## 4. Bộ hợp nhất quyết định

Mỗi giây, sau khi 3 model chạy xong:

**Bộ lọc kéo dài K = 11.** Bất thường chỉ được nâng thành báo động khi giữ liên
tục ≥ 11 giây. Nháy 2–6 giây do ho/cử động bị triệt tiêu. Cơ sở: đo trên BIDMC,
quyết định tức thì cho tỉ lệ báo động giả 17,6% — tệ hơn cả ngưỡng thuần — thêm
persistence kéo xuống 2,3%.

**Luật lâm sàng cứng KHÔNG đi qua bộ lọc này.** SpO2 < 90%, HR < 45 hoặc > 150
phải kêu **ngay lập tức**. Bộ lọc chỉ áp cho tín hiệu do AI phát hiện.

```c
typedef enum {
    ALERT_LEVEL_NORMAL       = 0,  // XANH
    ALERT_LEVEL_LINE_WARNING = 1,  // VÀNG: sự cố đường truyền
    ALERT_LEVEL_VITALS_ALERT = 2,  // ĐỎ: dấu hiệu suy hô hấp / tim nhanh
    ALERT_LEVEL_CRITICAL     = 3   // ĐỎ KHẨN: cả hai cùng hỏng
} alert_level_t;
```

Ba model cho ba tín hiệu **độc lập, quy được về đúng nguồn**, rồi logic mới kết hợp:

```
nhánh DỊCH      = Model 1 bất thường  HOẶC  luật cân↔giọt kết luận tắc
nhánh BỆNH NHÂN = Model 2 bất thường  HOẶC  Model 3 (AE) bất thường
                                      HOẶC  vi phạm luật cứng
```

* **Cấp 1 (VÀNG)** — nhánh dịch báo > 11s, **nhánh bệnh nhân im** → sự cố nằm ở
  đường truyền, bệnh nhân vẫn ổn → *"KIỂM TRA DÂY TRUYỀN"*.
* **Cấp 2 (ĐỎ)** — nhánh bệnh nhân báo > 11s (hoặc luật cứng, không qua bộ lọc),
  **nhánh dịch im** → *"CẢNH BÁO SINH HIỆU"*.
* **Cấp 3 (ĐỎ KHẨN)** — **cả hai nhánh cùng báo** → *"NGUY HIỂM: NGHI NGỜ QUÁ TẢI
  DỊCH"*.

Đây chính là chỗ việc tách 3 model trả cổ tức: nếu dùng một model gộp thì cấp 1 và
cấp 2 **không phân biệt được**, vì chỉ có một điểm bất thường chung không nói được
hỏng ở đâu. Vai trò của Model 3 trong ma trận này là trả lời dứt khoát câu *"khi
đường truyền có vấn đề, bản thân bệnh nhân có đang ổn không?"* — câu quyết định
giữa VÀNG và ĐỎ KHẨN.

Lưu ý thi công: `alert_level_t` hiện có **3** giá trị (`ALERT_GREEN/YELLOW/RED`
trong `sensor_hub.h:62`), lên **4**. Mọi chỗ so sánh `ALERT_RED` phải rà lại tay.

---

## 5. Các bước thi công

Làm tuần tự, **xong bước nào kiểm thử bước đó** rồi mới sang bước sau.

### Giai đoạn 1 — `ml/`

| # | Việc | Kiểm thử |
|---|---|---|
| 1.1 | `ml/dataset/make_drip_timeseries.py` — chạy generator 8 kịch bản, resample sự kiện-giọt → 1 Hz, cắt cửa sổ 64→16, split theo session | Kiểm tra phân bố `drops_ratio`, số cửa sổ, không session nào nằm ở 2 tập |
| 1.2 | `ml/dataset/make_drip_realtest.py` — 6 session thật → tập test cùng schema | Đối chiếu dpm tính ra với `target_interval_ms` trong file gốc |
| 1.3 | `ml/dataset/make_vitals_timeseries.py` — BIDMC, split theo bệnh nhân 31/10/11 | Assert giao của 3 tập mã bệnh nhân là rỗng |
| 1.4 | `ml/dataset/make_snapshot_ae.py` — ghép độc lập 2 nguồn | Kiểm tra tương quan HR↔drops trong dataset ≈ 0 |
| 1.5 | `ml/train_drip_forecaster.py` | MAE trên tập test **thật**; so với baseline "dự báo = giá trị hiện tại" |
| 1.6 | `ml/train_vitals_forecaster.py` | MAE trên 11 bệnh nhân test |
| 1.7 | `ml/train_snapshot_ae.py` | Ngưỡng phân vị 98%; tỉ lệ báo giả trên val bình thường ≈ 2% |
| 1.8 | `ml/export_c_headers.py` — gọi tool MLTK 3 lần (shim `imp` + đổi tên symbol) | Đối chiếu byte array với file `.tflite` gốc; kiểm tra opcode resolver sinh ra khớp với op thật của model |
| 1.9 | `ml/evaluate.py` | Tỉ lệ báo động giả **trước và sau** bộ lọc K=11 — con số quan trọng nhất cho báo cáo |

#### Kết quả Giai đoạn 1 (đã chạy, đo trên model int8 sẽ nạp vào chip)

| Model | Kích thước | Operator | Chỉ số chính |
|---|---|---|---|
| Drip forecaster | 22.400 B | 6 (`CONV_2D`, `FULLY_CONNECTED`, `RESHAPE`) | **AUC 0,948** trên bản ghi thật leak-free (persistence baseline: 0,839) |
| Vitals forecaster | 23.520 B | 6 (như trên) | **AUC 0,885** trên 12 bệnh nhân chưa từng thấy (baseline: 0,680) |
| Vitals AE | 3.136 B | 4 (`FULLY_CONNECTED`) | Báo giả **0,16%** số giây bình thường, bắt **99,75%** số giây bất thường |

**Bộ lọc K=11 (đo bằng replay 1 Hz, chấm điểm nhân quả như firmware):**

| | Báo giả/giờ trước K=11 | Sau K=11 | Dữ liệu bình thường |
|---|---|---|---|
| Drip | 29,0 | **0,0** | 0,03 h (rất mỏng — xem hạn chế) |
| Vitals | 47,6 | **0,0** | 1,14 h |

Recall drip: **6/8** bản ghi bất thường có báo động, độ trễ thêm do bộ lọc trung
vị 17 giây.

#### Ba phát hiện làm thay đổi thiết kế

1. **Level augmentation (bắt buộc, không phải tuỳ chọn).** Lần train đầu: hơn
   baseline 11% trên dữ liệu mô phỏng nhưng **tệ hơn baseline 5 lần** trên dữ liệu
   thật. 93% sai số là độ lệch hằng số: mô phỏng chạy ở tỉ số 0,9984, bàn thử thật
   chạy ở 0,9490. Model đã học thuộc "bình thường = 1,0". Sửa bằng cách dịch ngẫu
   nhiên mức vận hành của mỗi cửa sổ lúc train.
2. **AE phải ăn ĐỘ LỆCH nhịp tim, không phải nhịp tim tuyệt đối.** Bản đầu gắn cờ
   **33% snapshot bình thường** của bệnh nhân mới — nó học nhịp nghỉ của nhóm bệnh
   nhân train và coi mọi nền khác là bất thường, tức là phát hiện "người lạ" chứ
   không phải "người bệnh". Chuyển sang độ lệch so với nền riêng (đúng cơ chế
   `ai_monitor_get_hr_baseline()` firmware đã có sẵn): còn **0,16%**.
3. **MAE là thước đo SAI cho model này.** Cả hai forecaster đều dự báo *kém hơn*
   persistence baseline trên dữ liệu bình thường — và đó là điều đúng đắn. Model
   chỉ học động lực bình thường nên **từ chối bám theo** động lực bất thường,
   khiến residual bùng lên. Persistence bám theo mọi thứ, kể cả tắc nghẽn, nên nó
   là forecaster ngoan nhưng là detector tồi. Thước đo chính là AUC phát hiện.

#### Hạn chế phải nêu trong báo cáo, không được giấu

* **Recall của nhánh sinh hiệu KHÔNG đo được trên BIDMC.** Toàn bộ tập test chỉ có
  2 lần vi phạm ngưỡng lâm sàng: một ca SpO2 đã 84% ngay *giây đầu tiên* của bản
  ghi (không thể cảnh báo sớm cho thứ có trước cả bản ghi), và một ca nhịp tim
  chạm 44 đúng *một giây* (bộ lọc K=11 cố tình không báo — đó là chức năng). BIDMC
  là 8 phút số liệu ICU phần lớn ổn định, không chứa diễn biến xấu dần. Đo được
  trên nó: **tỉ lệ báo giả**. Không đo được: **recall**. Vì vậy luật lâm sàng cứng
  vẫn là lưới an toàn *chính* cho sinh hiệu, không phải phương án dự phòng.
* **Dữ liệu drip bình thường chỉ có 0,03 giờ** (2 phút, từ 2 session ổn định thật).
  Con số "0 báo giả/giờ" của nhánh drip vì thế còn mỏng về mặt thống kê.
* **Lượng tử hoá int8 làm drip mất 0,033 AUC** (0,981 → 0,948). Vẫn bỏ xa baseline,
  nhưng là chi phí có thật. Vitals thì int8 lại nhỉnh hơn float (0,856 → 0,885).

### Giai đoạn 2 — Firmware

| # | Việc | Kiểm thử |
|---|---|---|
| 2.1 | `config/sl_tflite_micro_config.h`: `INTERPRETER_INIT_ENABLE = 0` | Build sạch; xác nhận `sl_tflite_micro_init()` không còn cấp phát gì |
| 2.2 | `.slcp`: gỡ `model_runner.cpp`, `ts_forecaster.cpp`, `model_data*.h`; thêm `ai_engine.cpp`, `ai_fusion.c`, `line_rules.c`; chạy lại `slc generate` | `slc generate` không lỗi; build ra `.hex` |
| 2.3 | `firmware/ai_engine.cpp/.h` — 3 interpreter, 3 arena, lỗi thì `return false` | Nạp chip, đọc log: 3 model init OK, in ra arena thực dùng |
| 2.4 | `firmware/line_rules.c/.h` — bảng luật cân↔giọt + ước lượng còn lại | Test bàn: bịt dây (tắc) vs để bình cạn tự nhiên — phải ra 2 kết luận khác nhau |
| 2.5 | `firmware/ai_fusion.c/.h` — 2 ring buffer độc lập, persistence K=11, ma trận 4 cấp | Bơm chuỗi giả qua CLI: nháy 5s không báo, kéo 11s thì báo |
| 2.6 | `sensor_hub.h`: `alert_level_t` 3 → 4 giá trị, rà mọi chỗ dùng `ALERT_RED` | `grep` toàn repo; build không cảnh báo |
| 2.7 | `oled_display.c` — 3 thông điệp mới | Nhìn màn hình thật ở cả 4 cấp |
| 2.8 | `app.c` — nối lại vòng 1 Hz, mở rộng attribute Zigbee | Bắt gói Zigbee, kiểm tra trường mới |
| 2.9 | Xoá `ai_monitor.*`, `ts_monitor.*`, `model_runner.cpp`, `ts_forecaster.*`, `model_data*.h` | Build lại từ đầu |

#### Kết quả Giai đoạn 2 (code xong, kiểm thử được phần kiểm thử được)

Đã viết mới: `ai_engine.{h,cpp}` (3 interpreter, 3 arena), `ai_fusion.{h,c}`
(ma trận 4 cấp + K=11), `line_rules.{h,c}` (luật cân↔giọt), `clinical_limits.h`
(luật cứng tách riêng để sống sót khi AI chết), `firmware/models/*` (sinh tự động).
Đã xoá: `ai_monitor.*`, `ts_monitor.*`, `model_runner.cpp`, `ts_forecaster.*`,
`model_data*.h`.

**Kiểm thử đã chạy — `tools/fusion_test.c`, 12/12 PASS.** Chạy `ai_fusion.c` và
`line_rules.c` **nguyên bản, không sửa** trên máy tính với cảm biến và model giả:

```
cc -I firmware -o /tmp/fusion_test tools/fusion_test.c \
   firmware/ai_fusion.c firmware/line_rules.c -lm && /tmp/fusion_test
```

Ca đáng giá nhất — cùng tốc độ giọt chậm (0,25×, 15 giọt/phút), hai nguyên nhân
vật lý khác nhau:

| Trọng lượng | Kết luận | Cấp |
|---|---|---|
| vẫn giảm | `Bag running low` | **NORMAL** (không kêu) |
| đứng yên | `LINE BLOCKED` | **LINE_WARNING** (kêu) |

Các ca khác: nháy 5 giây không báo; đúng giây thứ 11 mới báo; SpO2 < 90% báo
**ngay tick đầu, không qua bộ lọc**; hai nhánh cùng báo → CRITICAL; **cả ba model
không nạp được thì luật lâm sàng vẫn báo động**; mất tín hiệu cảm biến không bị
coi là bình thường; AE bắt tổ hợp dưới ngưỡng.

**Ba lỗi thật do test bắt được:**

1. **Ngưỡng "cân đứng yên" tuyệt đối là sai.** `LINE_STEADY_G = 3 g` trên cửa sổ
   60 giây đúng bằng lượng dịch của một ca truyền **hoàn toàn bình thường** (60
   giọt/phút, bộ dây 20 gtt/mL = 3 mL/phút). Hệ quả: ca truyền khoẻ mạnh bị báo
   "hỏng cảm biến giọt". Sửa thành ngưỡng **tương đối** so với tốc độ mà số giọt
   ngụ ý (`LINE_STEADY_FRACTION`), cộng sàn nhiễu — một hằng số cố định không thể
   đúng cho cả ca 15 giọt/phút lẫn ca 120 giọt/phút.
2. **`AI_AE_THRESHOLD` không nhìn thấy từ `app.c`** (nằm ở `ai_engine.h`, app chỉ
   include `ai_fusion.h`) — lỗi biên dịch thật, sửa bằng cách để `ai_fusion.h`
   include `ai_engine.h`.
3. **Residual không giữ được báo động qua sự cố kéo dài.** Forecaster được huấn
   luyện bất biến với mức vận hành, nên khi sự cố ổn định ở trạng thái mới thì
   model dự báo đúng nó và residual về 0. Residual bắt **chuyển biến**, không giữ
   báo động. Việc giữ báo động thuộc về luật cứng, luật cân↔giọt và AE — đã ghi rõ
   trong `ai_fusion.h` để sau này không ai "tối ưu" bỏ mất một trong ba.

Đồng thời bổ sung **cảnh báo sớm theo dự báo vượt ngưỡng** (`early_warning`):
residual chỉ bắt chuyển biến, nên diễn biến xấu **từ từ** — thứ mà model dự báo
rất chuẩn nên residual không nhúc nhích — cần một cơ chế riêng: kiểm tra xem
*bản thân dự báo* có vượt ngưỡng lâm sàng trong 16 giây tới không.

**Build firmware: THÀNH CÔNG.** `slc generate` + `ninja` chạy sạch, ra
`cmake_gcc/build/base/smart-iv-monitor.hex`. Không một cảnh báo nào trong
`ai_fusion.c`, `line_rules.c`, `ai_engine.cpp`, `app.c`.

Lệnh build (SDK và extension aiml phải truyền tường minh):

```bash
SDK=~/.silabs/slt/installs/conan/p/simpl35774a752829c/p
AIML=~/.silabs/slt/installs/conan/p/aiml220b56d6ae053/p
SLC=~/.silabs/slt/installs/archive/slc-cli-v6.0.20/slc_cli/slc
$SLC generate -p smart-iv-monitor.slcp -d . --with brd2709a --sdk-package-path $SDK,$AIML
cd cmake_gcc/build && ninja
```

**Ngân sách bộ nhớ:**

| | Byte | Ghi chú |
|---|---|---|
| RAM tĩnh (`.data`+`.bss`) | 32.504 | **6,2%** của 512 KB |
| Heap còn lại | 491.784 | 93,8% |
| Ba arena AI | 10.240 | 2,0% RAM; v1 dùng ~12 KB nên v2 **giảm ~2 KB** |
| Flash (`text`) | 381.364 | 3 model chiếm 49.056 B |

> Lưu ý đọc số: `arm-none-eabi-size` báo `bss` = 522.440 B, nhìn như sắp tràn
> 512 KB. Không phải — `.memory_manager_heap` là `NOLOAD` và **chiếm toàn bộ RAM
> còn lại** theo linker script, nên toàn bộ heap bị cộng vào `bss`. Phần tĩnh
> thật chỉ 32,5 KB.

#### Chạy thật trên chip (EK2709A, `EFR32MG26B510F3200IM48`)

Đã nạp và đọc log. **Cả ba model nạp thành công:**

```
[AI] drip   ready: arena 2836/4096 B, in scale 39214/1e6, zp -55
[AI] vitals ready: arena 2868/4096 B, in scale 76469/1e6, zp  49
[AI] ae     ready: arena 1108/2048 B, in scale 66618/1e6, zp  75
```

Arena thực dùng 6.812 B trên 10.240 B cấp phát (dư 33%) — kích thước 4/4/2 KB là
đúng, không cần chỉnh.

**Kiểm chứng đầu-cuối:** scale và zero-point mà chip đọc từ tensor **khớp tuyệt
đối** với cả ba file `.tflite` gốc. Nghĩa là chuỗi tool MLTK → mảng byte →
firmware → chip nguyên vẹn, không có model nào bị lệch phiên bản.

Ma trận 4 cấp hoạt động đúng ngay trên log thật: khi chưa cắm cảm biến PPG,
thiết bị ra `alert_level:1`, `line_branch:false`, `patient_branch:false`,
headline `SENSOR SIGNAL LOST` — mất tín hiệu không bị coi là bình thường, cũng
không bị thổi thành cảnh báo bệnh nhân.

**Một bug do log phát hiện:** `MicroPrintf` của TFLM **không hỗ trợ `%f`** — nó
tự cài một formatter tí hon riêng. Dòng log in ra `in scale .6f zp 1610612736`:
chuỗi `.6f` được in nguyên văn, đối số float không bị tiêu thụ, nên mọi specifier
sau đó đọc nhầm ô và zero-point in ra là **rác trông như số liệu thật**. Đã đổi
sang in scale dưới dạng số nguyên phần triệu. Không dùng `%f` với `MicroPrintf`.

#### CÁI BẪY `.zap` — đã tìm ra nguyên nhân thật và sửa dứt điểm

`slc generate` sinh `autogen/zap-id.h` thiếu 7 attribute ZCL tuỳ biến, làm
`app.c` không biên dịch được.

**Hai lần chẩn đoán sai trước khi ra nguyên nhân đúng**, ghi lại để không ai đi
lại đường cũ:

1. *"File `.zap` không được commit"* — **sai**, nó có đầy đủ
   (`config/zcl/zcl_config.zap`, `smart-iv-vitals.xml`, `slc_args.json`). Kết
   luận sai vì lúc kiểm tra shell đang ở thư mục con.
2. *"`slc_args.json` trỏ ZAP vào bộ cluster chuẩn của SDK nên XML tuỳ biến không
   được nạp"* — **cũng sai**. XML **có** được nạp: `ZCL_DROP_RATIO_ATTRIBUTE_ID`
   vẫn còn sau khi sinh, chỉ 7 attribute cuối là mất.

**Nguyên nhân thật: cache của ZAP.** ZAP lưu package ZCL vào
`~/.zap/generate.sqlite` đánh khoá theo **đường dẫn**, và dùng bản cũ dù CRC đã
đổi:

```
pkg 238   crc  168642456   attr 15   <đường dẫn tuyệt đối>   ← CŨ, được dùng
pkg 239   crc 1295804267   attr 22   <đường dẫn tương đối>   ← ĐÚNG, bị bỏ qua
```

**Sửa dứt điểm:** `tools/slc_generate.sh` — xoá bản cache cũ trước khi sinh, tự
tìm đường dẫn SDK/aiml, và **kiểm tra lại đủ 7/7 attribute** sau khi sinh, dừng
ngay nếu thiếu. Đã kiểm chứng: chạy script rồi `git diff autogen/` **trống**, tức
file sinh lại khớp y hệt bản đã commit. Không còn phải `git checkout` thủ công.

### Giai đoạn 3 — Gateway + Server

| # | Việc | Kiểm thử |
|---|---|---|
| 3.1 | `gateway/main.c` — giải mã trường mới, JSON thêm `alertLevel` + cờ từng model. Giữ nguyên tắc **gateway không tự suy luận** | Chạy trên Pi, `journalctl -u gateway -f`, đối chiếu JSON với log chip |
| 3.2 | `VitalsStatusEvaluator.cs` — ánh xạ 4 cấp; migration DB | Unit test cho từng cấp |
| 3.3 | Dashboard — cấp 3 hiển thị khác cấp 2; thêm "còn ~X mL / ~Y phút" | Thử đủ 4 cấp trên UI |

#### Kết quả Giai đoạn 3

`gateway/main.c` chuyển tiếp thêm 8 trường (`alertLevel`, `lineBranch`,
`patientBranch`, `dripAnomaly`, `vitalsAnomaly`, `lineState`, `remainingMl`,
`remainingMin`) — vẫn giữ nguyên tắc **gateway không tự suy luận**, chỉ chuyển
tiếp. `BedReading`/`BedDataParser`/`VitalsStatusEvaluator` nhận và dùng chúng.
Cả ba tầng build sạch, không cảnh báo.

**Một quyết định thiết kế phải nêu rõ: server NHƯỜNG phán quyết về đường truyền
cho thiết bị.** Đây là ngoại lệ duy nhất với nguyên tắc "thiết bị chỉ được nâng
cấp độ, không được hạ".

`LineBlocked` của v1 nghĩa là *"tỉ số giọt ra ngoài dải y lệnh"* — đúng cả khi
**tắc dây** lẫn khi **bình sắp hết** (cột dịch ngắn → áp thấp → ít giọt hơn).
Server chỉ có đúng một con số đó nên không phân biệt được. Thiết bị thì phân biệt
được, vì nó theo dõi **trọng lượng**: bình vẫn nhẹ dần nghĩa là dịch vẫn vào và
không có gì sai. Nên khi thiết bị có gửi `AlertLevel`, phán quyết về **nhánh
đường truyền** của nó thắng `LineBlocked`.

Nhường ở đây không phải server từ bỏ thẩm quyền, mà là server không ghi đè một
phán quyết được đưa ra với **nhiều thông tin hơn hẳn**. Gọi một bình sắp hết là
"Critical" mỗi lần là cách nhanh nhất để cả khoa học cách phớt lờ đúng cái báo
động quan trọng. Phía **bệnh nhân** thì không nhường: SpO2 84% vẫn ra Critical kể
cả khi thiết bị báo cấp 0.

**Kiểm thử: `server/tests/EvaluatorTests`, 20/20 PASS.**

```bash
dotnet run --project server/tests/EvaluatorTests
```

Cố ý viết bằng console app thuần thay vì xUnit — không cần restore NuGet, nên
chạy được cả trên máy không mạng, kể cả Pi.

Ca quan trọng nhất dùng **dòng JSON thật chụp từ board** (`chip_sample.json`,
không phải fixture viết tay): nó bắt được đúng loại lỗi mà không ai để ý — đổi
tên một trường ở một đầu dây thì mọi thứ vẫn biên dịch, vẫn chạy, và dashboard
lặng lẽ hiển thị giá trị mặc định mãi mãi.

**Chính test đầu-cuối này bắt được một lỗi thật:** chip báo *"cấp 1 — lỗi đường
truyền, bệnh nhân bình thường"* nhưng server vẫn ra `Critical`, vì luật
`LineBlocked` cũ ghi đè. Đó đúng là hành vi mà v2 sinh ra để sửa, và nếu không có
test dùng dữ liệu thật thì nó đã lọt.

Các ca khác: bình sắp hết **không** bị mô tả là tắc dây (kèm "còn ~18 phút");
cấp 2 → Critical kèm câu "đường truyền vẫn bình thường"; cấp 3 → nghi ngờ quá tải
dịch; thiết bị v1 (không gửi `AlertLevel`) vẫn chạy đúng như cũ; `lineState = -1`
thành `null` chứ không phải `0` — *"chưa biết"* khác *"bình thường"*.

#### Bố trí bit của `tsFlags` (attribute ZCL 0x000F)

Toàn bộ thông tin v2 đi qua Zigbee bằng **phần bit còn trống** của một attribute
gateway đã đọc sẵn — không phải đổi schema ZCL.

| Bit | Ý nghĩa |
|---|---|
| 0–2 | forecaster sẵn sàng / bất thường đã qua K=11 / cảnh báo sớm |
| 3–6 | xu hướng nhịp tim, xu hướng số giọt |
| 7–8 | dự báo HR / dự báo giọt có đáng tin không |
| **9–10** | **cấp cảnh báo 0–3** |
| **11–12** | **nhánh đường truyền / nhánh bệnh nhân** |
| **13–15** | **phán quyết loadcell, gửi dưới dạng `state+1`** |

`state+1` chứ không phải `state`: giá trị 0 mang nghĩa *"chưa kết luận được"* (cửa
sổ trọng lượng 60 giây chưa đầy). Gửi thẳng `state` thì "chưa biết" và "mọi thứ
ổn" trùng nhau, và bảng điều khiển sẽ hiện màu xanh mà nó chưa xứng đáng có.

#### Ba lỗi tìm ra khi chạy thật với đường Zigbee

1. **Đường Zigbee vẫn chạy code cũ.** `gateway/main.c` và converter đã sửa trong
   repo nhưng **chưa build lại và chưa triển khai xuống Pi**. Nên `alertLevel`
   không tới được server, server coi board là thiết bị đời cũ, và **mọi sự cố
   đường truyền bị đẩy lên mức đỏ**. Sửa `gateway/` trong repo là chưa xong việc
   — phải copy sang Pi, `gcc`, `systemctl restart`.
2. **Converter chia 100 thừa.** Firmware gửi **phần trăm** (`ratio × 100`),
   converter lại chia thêm 100 thành phân số, rồi `get_int_from_json` cắt `1.04`
   thành `0`. Hậu quả: ca truyền **đúng y lệnh** (52/50 dpm) hiện ra **0%** và bị
   tô đỏ. Đường serial không có lỗi này nên nó ẩn suốt.
3. **Biểu đồ coi số 0 là số đo thật.** Bỏ tay khỏi cảm biến → firmware gửi 0 →
   `0 < critBelow(45)` → thẻ đỏ nhấp nháy cho một giường chưa ai kẹp cảm biến.
   Nguyên tắc *"chưa có dữ liệu không phải là nguy kịch"* repo đã ghi từ trước,
   nhưng biểu đồ chưa áp dụng. Nay các kênh mà 0 là bất khả thi về sinh lý được
   đánh dấu `zeroMeansNoSignal`.

#### Thang màu sau khi chỉnh

**Đỏ dành riêng cho bệnh nhân đang nguy hiểm.** Sự cố đường truyền — tắc, chảy
tự do, lệch y lệnh — **chỉ tới mức vàng**, dù nguồn báo là thiết bị v1 hay v2.
Cho nó cùng màu cùng còi với tụt oxy là cách nhanh nhất khiến cả khoa thôi phản
ứng với màu đỏ. Ngoại lệ duy nhất: **cấp 3** — đường truyền *và* bệnh nhân cùng
bất thường; phần đỏ đó là do nửa bệnh nhân.

### Giai đoạn 4 — Tài liệu

Viết lại `AI_HOAT_DONG_THE_NAO.md`, `AI_TIME_SERIES_TAT_TAN_TAT.md`,
`Dataset_va_Phuong_phap_AI_SmartIV.md`, mục "AI chạy ở đâu" trong `README.md`;
thêm `docs/MLTK_AUTOGEN.md` mô tả luồng `config/tflite/` → tool MLTK → header.

---

## 6. Rủi ro đã biết

1. **`alert_level_t` 3 → 4 lan ra tận DB.** Firmware, attribute Zigbee, gateway,
   server, migration, UI — đúng một chuỗi. Phần tốn công nhất, và là chỗ dễ sót
   nhất. Rà bằng `grep` chứ không dựa vào trí nhớ.
2. **Model 1 train phần lớn trên dữ liệu mô phỏng**, Model 2 thì 100% ICU thật.
   Sự bất đối xứng này phải nêu rõ trong tài liệu, **không** gộp chung thành câu
   "AI train trên dữ liệu thật".
3. **Tool MLTK cần vá để chạy trên Python 3.12.** Vá nằm trong
   `ml/export_c_headers.py`, không sửa file trong SDK. Nếu Silabs cập nhật SDK,
   kiểm tra lại shim còn cần không.

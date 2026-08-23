# Dataset và phương pháp — AI v2 Smart IV

Tài liệu này trả lời câu mà người chấm sẽ hỏi đầu tiên: **dữ liệu ở đâu ra, và
con số các anh đưa ra có đáng tin không?**

Nhóm cố gắng viết phần **giới hạn** kỹ ngang phần kết quả. Chỗ nào đo được thì
nói rõ đo thế nào; chỗ nào **không** đo được thì nói thẳng là không đo được, thay
vì để người đọc tự suy ra một điều mạnh hơn sự thật.

Ba script sinh dataset nằm ở `ml/dataset/`, chạy được độc lập:

```bash
.venv-ai/bin/python ml/dataset/make_drip_timeseries.py    # train/val cho Model 1
.venv-ai/bin/python ml/dataset/make_drip_realtest.py      # test THẬT cho Model 1
.venv-ai/bin/python ml/dataset/make_vitals_timeseries.py  # Model 2
.venv-ai/bin/python ml/dataset/make_vitals_ae.py          # Model 3
```

---

## 1. Ba model, ba nguồn dữ liệu tách rời

| | Model 1 — Drip | Model 2 — Vitals | Model 3 — Vitals AE |
|---|---|---|---|
| Đầu vào | 64 giây `drops_ratio` | 64 giây (HR, SpO2) | (HR lệch nền, SpO2) tại 1 thời điểm |
| Nguồn | cảm biến giọt ESP8266 (`AI-nho-giot`) | PhysioNet BIDMC | PhysioNet BIDMC |
| Dữ liệu thật? | huấn luyện: mô phỏng hiệu chỉnh từ thật · **test: 100% thật** | **100% thật** | **100% thật** |
| Chia tập theo | **session** | **bệnh nhân** | **bệnh nhân** |

**Không model nào nhìn thấy dữ liệu của model khác.** Đây là điểm cốt lõi của
v2, và nó có lý do đo được — xem mục 6.

---

## 2. Model 1 — dữ liệu nhỏ giọt

### Nguồn

Từ dự án `AI-nho-giot` (github.com/dinhhieu1st-debug/AI-nho-giot), gồm:

* `data/raw/` — **11 session cảm biến thật** từ ESP8266 + photodiode, schema
  `timestamp_ms, drop_number, target_interval_ms, actual_interval_ms`.
* `data/synthetic/` — 240 session mô phỏng (3 preset × 8 kịch bản × 10 session ×
  180 bản ghi = 43.200 bản ghi).

Điểm đáng chú ý: theo `DATASET_CARD.md` của dự án đó, bộ sinh mô phỏng được
**hiệu chỉnh số học từ 5 session thật** (`003, 005, 006, 009, 010`). Nó không
phải bịa từ con số 0 — mức nhiễu và cách trôi dạt lấy từ cảm biến thật.

Tám kịch bản: `stable`, `gradually_slowing`, `gradually_speeding`, `rapid_change`,
`temporary_disturbance`, `missing_drop`, `recovery`, `irregular`.

### Từ sự kiện-giọt sang chuỗi 1 Hz

Cảm biến không cho một mẫu mỗi giây — nó cho **một dòng mỗi giọt**. Mọi thứ phía
sau (cửa sổ 64 giây, model, ring buffer trong firmware) đều định nghĩa trên lưới
1 Hz cố định, nên phép chuyển đổi này là chỗ **thời gian sự kiện** thành **thời
gian thực**. Sai ở đây thì mọi cửa sổ trong dataset lệch đi mà không có lỗi nào
báo. Nó nằm gọn trong một file, `ml/dataset/drip_common.py`.

Hai quyết định trong đó đáng nói:

**a) Dùng tỉ số TỐC ĐỘ, không phải tỉ số KHOẢNG CÁCH.** `AI-nho-giot` làm việc
với `actual_interval / target_interval` — chỉ số này **tăng** khi chảy chậm lại.
Firmware và đặc tả model làm việc bằng giọt/phút, nên phải nghịch đảo:

```
drops_ratio = dpm_thực_tế / dpm_y_lệnh = target_interval / actual_interval
```

chỉ số này **giảm** khi chảy chậm lại. Hai quy ước đều hợp lý; trộn lẫn thì
**lật ngược dấu của mọi ca tắc nghẽn** trong dataset. Nên phép nghịch đảo được
làm đúng một lần, ở một chỗ.

**b) Khoảng lặng là BẰNG CHỨNG, không phải thiếu dữ liệu.** Giữa hai giọt, cách
làm hiển nhiên là giữ nguyên tốc độ đo được lần cuối (zero-order hold). Cách đó
sai ở đúng trường hợp quan trọng nhất: nếu một giọt đáng lẽ phải rơi từ một giây
trước mà chưa rơi, dòng chảy **đã** chậm lại rồi — im lặng càng lâu thì càng
chậm. Đây là dấu hiệu **sớm nhất** của tắc nghẽn đang hình thành, và zero-order
hold **xoá sạch** nó. Vì vậy:

```
khoảng_hiệu_dụng(t) = max(khoảng_giọt_gần_nhất, t − thời_điểm_giọt_gần_nhất)
```

Đo trên session_001 thật, ở khoảng mất giọt 13,7 giây, tỉ số suy giảm mượt
`0,89 → 0,58 → 0,37 → 0,27 → … → 0,073` rồi hồi phục — đúng điều một y tá nhìn
buồng đếm giọt cũng cảm nhận được.

### Chia tập — và một vấn đề trung thực về nó

* **Train + validation:** 240 session mô phỏng, theo đúng cột `split` mà bộ sinh
  đã gán. Chia **theo session**, không bao giờ theo dòng: các cửa sổ chồng lấn
  nhau 79/80 mẫu, nên chia theo dòng sẽ đặt những cửa sổ gần như giống hệt nhau
  vào cả hai tập và cho ra điểm số vô nghĩa (mà lại đẹp).
* **Test:** **chỉ bản ghi thật**, và chỉ những session **không tham gia hiệu
  chỉnh** → `001, 002, 004, 007, 008`. (Session `011` bỏ vì cảm biến ngừng đếm
  sau giọt thứ hai — ghi rõ trong nhật ký của chính nó.)

**Vấn đề, nói thẳng:** cả 5 session giữ riêng đó **đều là session bất thường**:

| Session | Nhật ký của người vận hành |
|---|---|
| 001 | mất giọt rồi hồi phục |
| 002 | trôi dần lên biên trên, kèm một lần trễ |
| 004 | hồi phục thủ công về ổn định |
| 007 | tăng tốc thủ công, chuyển tiếp bất thường |
| 008 | hồi phục bất thường |

Hai session **ổn định thật** duy nhất (`005`, `006`) lại chính là hai session đã
dùng để hiệu chỉnh bộ sinh. Nghĩa là:

> Dữ liệu thật đo được **recall** một cách sạch sẽ, nhưng **không có dữ liệu thật
> sạch để đo tỉ lệ báo giả**.

Nhóm không giấu chuyện này. Script xuất ra **hai file riêng**, mỗi file mang cờ
`leakage`, và `evaluate.py` **từ chối gộp chúng**:

| File | Session | Rò rỉ |
|---|---|---|
| `drip_realtest_heldout.npz` | 001, 002, 004, 007, 008 | **không** — dùng làm số liệu chính |
| `drip_realtest_calibrated.npz` | 003, 005, 006, 009, 010 | **có ảnh hưởng qua hiệu chỉnh** — chỉ dùng kèm chú thích |

**Cách bịt lỗ hổng này:** ghi thêm 2–3 session ổn định từ phần cứng. Không gấp,
nhưng nên làm trước khi nộp báo cáo cuối.

### Một kiểm chứng tự phát sinh

Nhãn "bình thường" được gán tự động (mọi mẫu trong 80 giây nằm trong dải
0,90–1,10). Đối chiếu với nhật ký viết tay của người vận hành:

| Session | Người ghi | Máy gán |
|---|---|---|
| 005 | `stable` | **46/46 cửa sổ bình thường** |
| 006 | `stable` | **48/48 cửa sổ bình thường** |
| 001, 003, 007, 008, 009, 010 | bất thường | **0 cửa sổ bình thường** |

Máy và người đồng ý hoàn toàn, dù máy **không hề được cho biết nhãn**. Đây là
bằng chứng độc lập rằng khâu resample và định nghĩa dải bình thường đúng.

---

## 3. Model 2 — sinh hiệu ICU thật

**Nguồn:** PhysioNet BIDMC, 53 bản ghi bệnh nhân ICU, đã lấy mẫu sẵn ở 1 Hz, mỗi
bản ghi ~8 phút (`HR`, `SpO2`).

**Chia theo BỆNH NHÂN, không bao giờ theo dòng.** 31 train / 10 validation / 12
test, chia bằng hoán vị có seed cố định. Script **assert** ba tập mã bệnh nhân
rời nhau chứ không tin là phép trộn đã làm đúng. Cùng một bệnh nhân xuất hiện ở
cả train lẫn test là rò rỉ, và điểm số sẽ đẹp một cách vô nghĩa.

**Giá trị thiếu:** BIDMC có một ít ô NaN (6 bệnh nhân, tệ nhất là 102/962 ô ở
`bidmc_19`). Đó là **khoảng mất theo dõi**, không phải số 0: bệnh nhân có SpO2
đọc ra NaN **không phải** là bệnh nhân bão hoà 0%. Nội suy qua đó sẽ chế ra sinh
lý chưa từng được đo, nên **cửa sổ nào chứa NaN thì bỏ nguyên cửa sổ** (696 cửa
sổ bị bỏ).

**Kết quả chia tập:**

| Tập | Bệnh nhân | Cửa sổ | Bình thường | Bất thường |
|---|---:|---:|---:|---:|
| train | 30¹ | 11.532 | 11.532 | 0 |
| validation | 10 | 3.935 | 3.885 | 50 |
| test | 12 | 4.741 | 4.259 | 482 |

¹ Một trong 31 bệnh nhân được gán vào train **không có nổi 80 giây liên tục bình
thường**, nên bị lọc hết khi lấy dữ liệu huấn luyện.

Dải giá trị: train HR 66–139, SpO2 91–100 (đúng trong ngưỡng lâm sàng); test HR
44–129, SpO2 **83**–100 — tức là tập test **có** ca tụt oxy và nhịp chậm thật.

---

## 4. Model 3 — autoencoder sinh hiệu

**Chỉ hai kênh: `hr_lệch_nền` và `spo2`.** Không có kênh nhỏ giọt.

### Vì sao bỏ kênh nhỏ giọt ra

Bản đầu cho AE ăn 3 kênh gồm cả giọt. Sai hai lần:

1. **Không có gì để học.** Không tồn tại bản ghi nào đo *cùng lúc* nhịp tim và
   tốc độ giọt của **cùng một bệnh nhân** — BIDMC là bệnh nhân ICU Mỹ, dữ liệu
   giọt là bàn thử ở ICTU. Hai nguồn buộc phải ghép **ngẫu nhiên**, mà ghép ngẫu
   nhiên thì **độc lập theo đúng cấu tạo**. AE tiêu nút thắt cổ chai để mô hình
   hoá ba phân phối biên rời rạc — thứ mà ba cái ngưỡng đã làm được.
2. **Tái phạm lỗi của v1.** Có kênh giọt trong mạng nghĩa là **tắc dây làm tăng
   sai số tái tạo**, tức làm bẩn phán đoán về **bệnh nhân**.

HR và SpO2 thì **được đo cùng lúc trên cùng một bệnh nhân**. Đó là quan hệ đa
biến **có thật**, và là quan hệ có thật duy nhất trong cả hệ thống. Model 3 học
đúng nó.

### Vì sao nhịp tim vào dưới dạng ĐỘ LỆCH

Bản đầu dùng nhịp tim tuyệt đối. Nó hỏng theo kiểu chỉ lộ ra khi tập test có
**bệnh nhân chưa từng thấy**:

```
ngưỡng đặt ở phân vị 98 của snapshot BÌNH THƯỜNG trong validation
  → gắn cờ  2% snapshot bình thường của validation   (đúng theo cấu tạo)
  → gắn cờ 33% snapshot bình thường của TEST         (thảm hoạ)
```

Không có gì sai trong khâu huấn luyện. Nhịp nghỉ 55 và 95 **đều bình thường, chỉ
khác người**. AE trên nhịp tim tuyệt đối học nhịp nghỉ của nhóm bệnh nhân train,
nên mọi bệnh nhân test có nền khác đều tái tạo kém và bị gắn cờ — model đã học
cách phát hiện **"người lạ"**, không phải **"người bệnh"**.

Chuyển sang độ lệch so với nền riêng — đúng cơ chế firmware vốn đã có
(`ai_fusion_get_hr_baseline()`, chốt bằng trung vị 60 giây đầu sau khi kẹp cảm
biến) — thì còn **0,16%**.

SpO2 giữ tuyệt đối, vì 97% có nghĩa như nhau ở mọi người.

**Nhãn theo TỪNG GIÂY**, không theo cửa sổ: một cửa sổ bị coi là bất thường nếu
bất kỳ giây nào trong 80 giây của nó bất thường, và như thế sẽ gán nhãn sai cho
79 giây khoẻ mạnh còn lại.

| Tập | Snapshot | Bình thường | Bất thường |
|---|---:|---:|---:|
| train | 738.048 | 738.048 | 0 |
| validation | 251.840 | 251.741 | 99 |
| test | 303.424 | 277.632 | 25.792 |

---

## 5. Nguyên tắc chung: train trên BÌNH THƯỜNG, hiệu chuẩn trên TOÀN DẢI

Cả ba model chỉ huấn luyện trên dữ liệu bình thường (unsupervised). Đó là điều
làm cho sai số trở thành tín hiệu bất thường: model **từ chối bám theo** động lực
mà nó chưa từng thấy.

Nhưng khâu **lượng tử hoá int8** thì ngược lại, và đây là một cái bẫy tìm ra được
lúc dựng dataset:

> Dải `drops_ratio` sau chuẩn hoá trên dữ liệu thật là **−2,75 … +6,93**, trong
> khi dải bình thường chỉ **±0,29**.

Nếu `representative_dataset` lúc quantize cũng chỉ chứa cửa sổ bình thường,
TFLite sẽ đo dải đầu vào là ±0,29 và đặt scale int8 theo đó. **Mọi bất thường sau
đó bão hoà về cùng một giá trị**, và chip không phân biệt nổi "chậm nhẹ" với "tắc
hoàn toàn".

Không có gì bắt được lỗi này: model convert sạch, chạy nhanh, cho số liệu hợp lý
trên dữ liệu bình thường, và **mù đúng vào lúc cần nhất**.

Vì vậy khâu hiệu chuẩn dùng **bao thiết kế** (`ml/common.py`) — toàn bộ dải mà
cảm biến có thể báo, quét tường minh:

| Kênh | Bao thiết kế | Cơ sở |
|---|---|---|
| `drops_ratio` | 0 … 3,5 | 0 = tắc hoàn toàn, 3,5 = chảy tự do vượt xa ngưỡng 1,5× |
| `HR` | 30 … 200 bpm | dải báo cáo của MAX30102 |
| `HR` (độ lệch, Model 3) | −70 … +70 bpm | mọi biên độ từ mọi nền hợp lý |
| `SpO2` | 70 … 100 % | dải báo cáo của cảm biến |

Đây là **thuộc tính của phần cứng và ngưỡng lâm sàng**, không đo từ tập test —
nên không có dữ liệu test nào ảnh hưởng tới model xuất ra. Script **từ chối xuất
model** nếu dải int8 không phủ hết bao thiết kế.

---

## 6. Khuyết điểm của v1: đo được, và khác với điều nhóm tưởng ban đầu

Ban đầu nhóm lập luận rằng dataset v1 chứa "tương quan ảo" giữa sinh hiệu và
giọt. **Đo lại thì lập luận đó sai.** Tương quan tuyến tính chéo nguồn trong
`iv_hybrid_1hz.csv` (75.036 dòng) vốn đã xấp xỉ 0:

```
corr(heart_rate, drops_per_min) = −0,0117
corr(heart_rate, weight_g)      = −0,0461
corr(spo2,       drops_per_min) = −0,0180
```

Khuyết điểm thật nằm ở **kiến trúc**. Đo trực tiếp trên
`ml/models/forecaster_int8.tflite` — đúng file từng chạy trên chip — giữ nguyên
hai kênh sinh hiệu và **chỉ** đổi hai kênh đường truyền sang trạng thái tắc:

| Kênh đầu ra | Thay đổi dự báo | Quy ra lâm sàng |
|---|---:|---|
| HR | 0,0993 | **≈ 2,0 bpm** |
| SpO2 | 0,1642 | **≈ 0,33 %** |

Một dây truyền bị gập **không thể** làm đổi nhịp tim bệnh nhân. Mạng nói là có.

Đó là lý do tách ba model — và lý do đó là **một con số đo được**, không phải một
lập luận định tính.

---

## 7. Điều nhóm KHÔNG chứng minh được

Nói rõ, vì im lặng ở đây sẽ khiến người đọc suy ra một điều mạnh hơn sự thật.

**Recall của nhánh sinh hiệu không đo được trên BIDMC.** Toàn bộ tập test chỉ có
**2** lần vi phạm ngưỡng lâm sàng, và cả hai đều không dùng để chấm điểm được:

* **Bệnh nhân 32** — SpO2 đã 84% ngay **giây đầu tiên** của bản ghi. Không cơ chế
  nào cảnh báo sớm được cho một tình trạng có **trước cả bản ghi**.
* **Bệnh nhân 45** — nhịp tim chạm 44 đúng **một giây**, SpO2 100% suốt. Bộ lọc
  K=11 **cố ý** không báo cái đó; báo mới là lỗi.

BIDMC là các đoạn 8 phút số liệu ICU phần lớn ổn định, **không chứa ca diễn biến
xấu dần nào**. Trên nó:

* **đo được:** tỉ lệ báo giả, trên 1,14 giờ sinh lý bình thường của bệnh nhân
  chưa từng thấy;
* **không đo được:** tỉ lệ bắt đúng.

Hệ quả thiết kế: **luật lâm sàng cứng là lưới an toàn CHÍNH cho sinh hiệu**, còn
AI là lớp cảnh báo sớm bổ sung chưa chứng minh được recall bằng dữ liệu. Điều này
được ghi thẳng trong mã nguồn (`ml/evaluate.py`) chứ không chỉ trong tài liệu.

**Dữ liệu drip bình thường chỉ có 0,03 giờ** (~2 phút, từ 2 session ổn định
thật). Con số "0 báo giả/giờ" của nhánh drip vì thế còn mỏng về mặt thống kê.

**Kênh loadcell không có dữ liệu thật nào** — nhưng nó cũng **không tham gia model
nào**. Cân được dùng bằng luật số học tường minh (`firmware/line_rules.c`), nên
không cần dữ liệu huấn luyện, và không có mạng nào học được quan hệ bịa ra từ nó.
Xem [`AI_HOAT_DONG_THE_NAO.md`](AI_HOAT_DONG_THE_NAO.md) mục 4.

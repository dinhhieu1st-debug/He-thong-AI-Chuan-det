# AI trong Smart IV làm gì — khi nào báo động, khi nào không

Tài liệu này viết cho người **không đọc code**: bác sĩ, y tá, ban giám khảo,
hoặc thành viên mới trong nhóm. Mục tiêu là trả lời đúng bốn câu:

1. AI ở đây thực sự làm gì, và làm được gì mà một cái ngưỡng cố định không làm được?
2. **Hỏng ở dây truyền hay hỏng ở bệnh nhân?** — câu y tá hỏi đầu tiên.
3. **Khi nào** nó kêu?
4. **Khi nào nó cố tình KHÔNG kêu** — phần này quan trọng ngang phần trên.

Chi tiết kỹ thuật (kiến trúc model, dataset, số đo) nằm ở
[`AI_TIME_SERIES_TAT_TAN_TAT.md`](AI_TIME_SERIES_TAT_TAN_TAT.md) và
[`Dataset_va_Phuong_phap_AI_SmartIV.md`](Dataset_va_Phuong_phap_AI_SmartIV.md).
Lý do đằng sau từng quyết định thiết kế nằm ở
[`AI_V2_PLAN.md`](AI_V2_PLAN.md).

---

## 1. Toàn bộ AI chạy TRÊN CHIP ở đầu giường

Không có cloud, không có server nào tham gia vào việc **quyết định báo động**.
Chip EFR32MG26 gắn ở giường tự đo, tự chạy model, tự quyết định, rồi mới gửi
kết quả đi. Hệ quả thực tế: **rút mạng, sập Wi-Fi, tắt server thì thiết bị vẫn
báo động bình thường** — chỉ là không ai ở trạm điều dưỡng nhìn thấy.

Mỗi giây chip làm đúng một vòng: đọc cảm biến → chạy 3 model + luật lâm sàng +
đối chiếu cân↔giọt → quyết định → hiện màn hình đầu giường + đèn/còi + gửi Zigbee.

---

## 2. Câu hỏi mà bản cũ không trả lời được

Bản AI đời đầu (v1) dùng **một mạng nơ-ron duy nhất** đọc chung cả sinh hiệu lẫn
dữ liệu nhỏ giọt rồi phát ra **một điểm bất thường**. Nó nói được *"có gì đó
không ổn"*, nhưng không nói được **không ổn ở đâu**.

Đó không phải chuyện nhỏ. Tắc dây truyền và bệnh nhân đang xấu đi là hai sự cố
có cách xử trí **hoàn toàn khác nhau** — một cái là đi lấy bộ dây mới, một cái là
chạy tới giường ngay — mà v1 phát ra **cùng một tiếng còi**.

Tệ hơn, dùng chung một mạng khiến hai thứ không liên quan ảnh hưởng lẫn nhau.
Nhóm đo trực tiếp trên đúng file model đang chạy trên chip: giữ nguyên hai kênh
sinh hiệu và **chỉ** đổi hai kênh đường truyền sang trạng thái tắc, thì

> **dự báo nhịp tim của bệnh nhân dịch chuyển khoảng 2 nhịp/phút.**

Không có bệnh nhân nào đổi nhịp tim vì dây truyền bị gập. Con số đó là hệ quả
của việc một mạng dùng chung trọng số cho cả bốn kênh.

**AI v2 tách thành ba mạng độc lập**, mỗi mạng nhìn đúng một thứ. Nhờ vậy hệ
thống quy được trách nhiệm cho đúng nguồn, và trả lời được câu hỏi ở trên.

---

## 3. Thiết bị đo bốn thứ, ba model nhìn ba việc khác nhau

| Kênh | Cảm biến | Ý nghĩa |
|---|---|---|
| Nhịp tim | MAX30102 (kẹp ngón tay) | nhịp/phút |
| SpO2 | MAX30102 | % bão hoà oxy |
| Số giọt | cảm biến quang ở buồng đếm giọt | giọt/phút so với y lệnh |
| Trọng lượng bình | loadcell HX711 | gam, để biết dịch **có thật sự chảy ra không** |

Ba model:

| | Nhìn gì | Trả lời câu hỏi |
|---|---|---|
| **Model 1 — dòng chảy** | 64 giây số giọt vừa qua | *Dòng chảy sắp đi về đâu?* |
| **Model 2 — sinh hiệu** | 64 giây nhịp tim + SpO2 | *Sinh hiệu sắp đi về đâu?* |
| **Model 3 — trạng thái bệnh nhân** | nhịp tim + SpO2 **ngay lúc này** | *Bản thân bệnh nhân có đang ổn không?* |

**Model 3 cố ý KHÔNG nhìn dữ liệu nhỏ giọt.** Nếu cho kênh giọt vào chung mạng
thì tắc dây sẽ làm tăng điểm bất thường và **làm bẩn phán đoán về bệnh nhân** —
đúng cái lỗi của v1, thu nhỏ lại. Việc kết hợp hai bên diễn ra **sau đó, bằng
luật rõ ràng viết ra được**, chứ không chôn trong trọng số mạng. Nhờ vậy hệ
thống giải thích được cho y tá, và y tá kiểm chứng được lời giải thích đó.

---

## 4. Cái cân giải quyết một chuyện mà số giọt không giải quyết nổi

Đây là phần đáng chú ý nhất của v2, và nó **không dùng AI**.

Bình dịch đầy có cột dịch cao → áp suất lớn → chảy nhanh. Bình gần hết có cột
dịch thấp → áp suất nhỏ → **chảy chậm dần**. Chảy chậm lúc gần hết là **bình
thường**, không phải hỏng. Báo động vào lúc đó là cách nhanh nhất để cả khoa học
cách phớt lờ cái máy.

Nhưng nhìn từ số giọt thì **tắc dây và gần hết dịch giống hệt nhau** — đều là ít
giọt đi. Cái cân phân định dứt khoát:

| Cân | Giọt | Kết luận | Có kêu không |
|---|---|---|---|
| vẫn nhẹ dần | chậm lại | Dịch **vẫn đang vào**, bình sắp hết | **Không** — chỉ nhắc "còn ~X phút" |
| **đứng yên** | chậm lại | Dịch **không ra khỏi bình** → **TẮC DÂY** | **Có** — vàng |
| nhẹ đi rất nhanh | tăng vọt | **Chảy tự do** | **Có** — vàng |
| đứng yên | vẫn đếm đều | Cảm biến giọt đếm nhầm (nắng, rung) | **Có** — nhưng là lỗi **kỹ thuật**, không phải cảnh báo bệnh nhân |

Dòng cuối quan trọng ngang các dòng trên: báo một lỗi cảm biến thành cảnh báo
lâm sàng sẽ khiến y tá chạy tới nhầm chỗ.

### "Còn khoảng bao nhiêu mL, còn bao nhiêu phút"

Ngoài việc phân định tắc dây với hết dịch, cân còn cho con số mà **thực tế y tá
dùng nhiều nhất trong ca**: bao giờ phải đi lấy chai mới. Chỉ là phép chia từ
trọng lượng còn lại và tốc độ sụt cân.

Chi tiết nhỏ nhưng quan trọng: khi thiết bị **chưa tính được**, nó nói *"chưa
biết"* chứ không nói *"còn 0 phút"*. Cân cần đủ 60 giây mới có xu hướng, và dịch
chảy quá chậm thì không chia ra thời gian được. Báo *"còn khoảng 0 phút"* cho một
bình còn chưa treo là loại số liệu **sai một cách tự tin** — và đó là thứ khiến
người ta thôi tin cái máy.

---

## 5. Bốn cấp cảnh báo

Hệ thống gộp mọi thứ thành **hai nhánh**, rồi mới ra cấp:

```
nhánh ĐƯỜNG TRUYỀN = Model 1 báo bất thường  HOẶC  luật cân↔giọt kết luận tắc/chảy tự do
nhánh BỆNH NHÂN    = Model 2 báo bất thường  HOẶC  Model 3 báo bất thường
                                             HOẶC  vi phạm ngưỡng lâm sàng cứng
```

| Cấp | Điều kiện | Đèn / còi | Màn hình | Y tá làm gì |
|---|---|---|---|---|
| **0 — XANH** | cả hai nhánh im | xanh, không còi | *Monitoring* | không cần làm gì |
| **1 — VÀNG** | chỉ nhánh đường truyền | vàng, còi chậm 1 giây | *KIỂM TRA DÂY TRUYỀN* | xem dây, buồng giọt, kim |
| **2 — ĐỎ** | chỉ nhánh bệnh nhân | đỏ, còi nhanh 0,3 giây | *CẢNH BÁO SINH HIỆU* | tới giường ngay |
| **3 — ĐỎ KHẨN** | **cả hai cùng lúc** | đỏ **+ vàng**, còi rất nhanh 0,15 giây | *NGUY HIỂM: NGHI NGỜ QUÁ TẢI DỊCH* | **ngừng truyền**, gọi bác sĩ |

Cấp 3 không phải "cấp 2 nhưng to hơn". Dịch chảy ồ ạt *cùng lúc* sinh hiệu sụp
là hình ảnh của **quá tải dịch hoặc phản vệ** — và xử trí đầu tiên là **khoá dây
truyền**, khác hẳn cấp 2. Vì vậy nó có mã riêng, màu riêng, nhịp còi riêng.

### Màu đỏ dành riêng cho bệnh nhân

Đây là nguyên tắc, không phải chi tiết thẩm mỹ:

> **Sự cố đường truyền — tắc, chảy tự do, lệch y lệnh — chỉ tới mức VÀNG.**
> Dù thiết bị đời cũ hay đời mới báo, dù nặng tới đâu.

Tắc dây là vấn đề thật và phải có người đi xử lý. Nhưng nó **không cùng hạng**
với tụt oxy: một cái là đi lấy bộ dây mới, một cái là chạy tới giường. Cho hai
thứ đó cùng màu đỏ và cùng tiếng còi là cách nhanh nhất khiến cả khoa **thôi
phản ứng với màu đỏ** — và khi đó cái đỏ thật sự quan trọng cũng bị bỏ qua.

Ngoại lệ duy nhất là **cấp 3**. Ở đó phần đỏ đến từ **nửa bệnh nhân**, không phải
từ dây truyền.

Một hệ quả cụ thể: bảng điều khiển **không** tô đỏ biểu đồ *"Drip vs target"* nữa,
chỉ tô vàng. Sinh hiệu vượt ngưỡng lâm sàng mới đỏ.

Bảng điều khiển ở trạm điều dưỡng hiển thị thêm một **huy hiệu quy trách nhiệm**:
`IV LINE` / `PATIENT` / `LINE + PATIENT`.

---

## 6. Khi nào nó KÊU

**Kêu ngay lập tức, không chờ gì cả** — đây là các luật cứng, không đi qua bộ lọc:

* SpO2 < 90%
* Nhịp tim < 45 hoặc > 150
* Nhịp tim lệch quá 30% so với **nền riêng của chính bệnh nhân đó** — trung vị
  của các mẫu đo được trong 60 giây đầu sau khi kẹp cảm biến
* Cảm biến **đang hoạt động rồi mất tín hiệu**

**Kêu sau khi giữ liên tục 11 giây** — các tín hiệu do AI phát hiện:

* Model 1 thấy dòng chảy đang chuyển biến bất thường
* Model 2 thấy sinh hiệu đang chuyển biến bất thường
* Model 3 thấy tổ hợp nhịp tim + SpO2 nằm ngoài vùng bình thường

**Kêu vì dự báo** (cảnh báo sớm): bản thân **dự báo** của model vượt ngưỡng lâm
sàng trong 16 giây tới, dù số đo hiện tại vẫn còn trong ngưỡng.

---

## 7. Khi nào nó cố tình KHÔNG kêu

Phần này quan trọng ngang phần trên.

**Nháy dưới 11 giây thì không kêu.** Bệnh nhân ho, cử động tay, một giọt rơi
lệch — chỉ số nhảy vài giây rồi về. Đo bằng cách phát lại bản ghi thật ở nhịp 1
giây, bộ lọc này đưa tỉ lệ báo giả **từ 29 lần/giờ xuống 0 lần/giờ** cho nhánh
dòng chảy, và **từ 47,6 xuống 0** cho nhánh sinh hiệu.

**Nhưng luật cứng thì KHÔNG chờ 11 giây.** SpO2 tụt dưới 90% kêu ngay tick đầu
tiên. Lập luận chống báo động giả áp dụng cho một nháy thoáng qua, **không** áp
dụng cho một ca tụt oxy.

**Bình sắp hết không bị coi là tắc dây.** Xem mục 4.

**Kênh chưa cắm không bị coi là số đo.** Cảm biến chưa cắm đọc ra 0; nếu coi số 0
đó là thật thì giường trống cũng kêu "SpO2 0% — nguy kịch". Kênh chưa cắm hiện
`--`, còn kênh **đã cắm rồi mất tín hiệu** thì đẩy sang cảnh báo — vì cảm biến
chết **không phải** là bệnh nhân khoẻ.

Nguyên tắc này đúng cho cả **biểu đồ xu hướng**, và đó là chỗ trước đây bị sót:
bỏ tay khỏi cảm biến thì firmware gửi 0, mà 0 nằm dưới ngưỡng nhịp chậm, nên thẻ
nhịp tim **nhấp nháy đỏ cho một giường chưa ai kẹp cảm biến**. Nay các kênh mà số
0 là bất khả thi về sinh lý (nhịp tim, SpO2, tỉ số giọt) bỏ qua hẳn giá trị 0 khi
tô màu. Việc mất tín hiệu đã có dải cảnh báo và hàng trạng thái từng kênh lo —
nó không cần thêm một cái thẻ đỏ nói sai sự thật.

**Chưa kẹp cảm biến thì KHÔNG chốt nền nhịp tim.** Nền phải là số **đo được** từ
chính bệnh nhân đó. Nếu hết 60 giây mà chưa thu đủ 20 mẫu thật, thiết bị **khởi
động lại cửa sổ** thay vì chốt một con số không ai đo:

```
[HR] Only 0/20 samples in 60s - sensor not attached? Restarting the baseline window.
```

Chuyện này phát hiện được khi chạy thật: trước đó thiết bị chốt luôn giá trị mặc
định 80 bpm ngay cả khi chưa cắm cảm biến, rồi **không bao giờ thử lại**. Y tá bật
máy trước rồi mới kẹp cảm biến — tức là thứ tự bình thường — sẽ khiến mọi bệnh
nhân có nhịp nghỉ khác 80 bị so với một con số bịa. Một bệnh nhân 131 bpm trông
như lệch 64% khỏi "nền của họ" và làm nổ luật cứng.

**Nhịp tim nghỉ 55 và 95 đều là bình thường, chỉ khác người.** Cả luật cứng lẫn
Model 3 đều so với nền riêng của bệnh nhân đó. Bản đầu của Model 3 dùng nhịp tim
tuyệt đối và gắn cờ **33% số giây hoàn toàn bình thường** của bệnh nhân mới — nó
học nhịp nghỉ của nhóm bệnh nhân huấn luyện rồi coi mọi nền khác là bất thường,
tức là phát hiện *"người lạ"* chứ không phải *"người bệnh"*. Sau khi chuyển sang
độ lệch so với nền riêng: còn **0,16%**.

---

## 8. Sáu tình huống cụ thể

| # | Chuyện gì xảy ra | Thiết bị làm gì |
|---|---|---|
| 1 | Bệnh nhân ho, SpO2 tụt 4 giây rồi về 98% | **Không kêu.** Bộ lọc 11 giây nuốt. |
| 2 | SpO2 tụt xuống 88% và ở đó | **Kêu ngay tick đầu**, cấp 2 ĐỎ. Luật cứng không chờ. |
| 3 | Bình còn 120 mL, giọt chậm còn 40% y lệnh, cân vẫn nhẹ dần đều | **Không kêu.** Màn hình: *"Bag running low — còn khoảng 28 phút"*. |
| 4 | Y tá gập nhẹ dây; giọt chậm còn 25%, **cân đứng yên** | **Kêu**, cấp 1 VÀNG, *"KIỂM TRA DÂY TRUYỀN"*. Huy hiệu `IV LINE`. |
| 5 | Nhịp tim 105 (chưa quá 150), SpO2 91,5% (chưa dưới 90) | Không luật cứng nào nổ. **Model 3 vẫn bắt được tổ hợp này** và đẩy lên cấp 2 sau 11 giây. |
| 6 | Dây tuột, dịch chảy ồ ạt, đồng thời SpO2 tụt 86% | **Cấp 3 ĐỎ KHẨN.** Đỏ + vàng, còi 0,15 giây. *"NGUY HIỂM: NGHI NGỜ QUÁ TẢI DỊCH"*. |

Tình huống 5 là lý do Model 3 tồn tại. Nhịp tim 45 và SpO2 90% — **đúng biên của
cả hai ngưỡng, không luật nào nổ** — nhưng Model 3 cho điểm bất thường 8,9 trên
ngưỡng 3,46. Hai sai lệch đều dưới ngưỡng, cộng lại thành đáng báo. Hệ ngưỡng
`HOẶC` thông thường **không thể** diễn đạt được điều đó.

---

## 8b. Bốn kịch bản diễn biến theo từng giây

Phần trên là bảng tóm tắt. Phần này là **diễn biến thật**, để hình dung được máy
"nghĩ" gì ở từng thời điểm — và quan trọng hơn, **vì sao có lúc nó im lặng**.

### Kịch bản A — Bệnh nhân ho (máy phải IM LẶNG)

| Giây | SpO2 | Máy thấy gì | Bộ đếm | Phản ứng |
|---:|---:|---|---:|---|
| 0–40 | 98% | bình thường | 0 | xanh, im |
| 41 | 94% | Model 2 thấy lệch dự báo | 1/11 | **vẫn im** |
| 42 | 91% | vẫn lệch | 2/11 | vẫn im |
| 43 | 90% | vẫn lệch | 3/11 | vẫn im |
| 44 | 93% | vẫn lệch | 4/11 | vẫn im |
| 45 | 97% | **về bình thường** | **0** (reset) | vẫn im |
| 46+ | 98% | bình thường | 0 | xanh |

> **Không một tiếng còi nào.** Đây chính là 29–47 lần báo giả mỗi giờ mà bộ lọc
> K=11 loại bỏ. Nếu SpO2 có tụt xuống dưới 90% ở giây 43 thì luật cứng **đã kêu
> ngay lập tức** — bộ lọc không đứng chắn trước luật cứng.

### Kịch bản B — Bình dịch cạn dần tự nhiên (máy phải IM LẶNG)

Bình 500 mL, y lệnh 60 giọt/phút, dây 20 giọt/mL.

| Thời điểm | Cân | Giọt | Model 1 | Luật cân↔giọt | Cấp |
|---|---:|---:|---|---|---|
| 0 phút | 500 g | 60/ph (100%) | bình thường | cân giảm đúng 3 g/ph | 0 |
| 90 phút | 230 g | 52/ph (87%) | bình thường | cân vẫn giảm | 0 |
| 140 phút | 90 g | 34/ph (57%) | chậm dần, nhưng đều | cân **vẫn giảm** 1,7 g/ph | 0 |
| 150 phút | 55 g | 24/ph (40%) | — | cân vẫn giảm → **"còn ~28 phút"** | 0 |
| 165 phút | 20 g | 12/ph (20%) | — | cân vẫn giảm | 0 |
| 172 phút | 4 g | 0/ph | — | **cân đứng yên + gần hết** → *BAG EMPTY* | 1 |

> Từ phút 140 trở đi, tỉ số giọt **đã tụt dưới ngưỡng 30%** của y lệnh. Một hệ
> chỉ có cảm biến giọt sẽ kêu **suốt 30 phút cuối của MỌI chai dịch trong khoa**.
> Ở đây máy im, vì cân xác nhận dịch vẫn đang vào. Nó chỉ nói *"còn ~28 phút"* —
> đúng thứ y tá cần để chuẩn bị chai mới.

### Kịch bản C — Dây bị gập (máy PHẢI kêu, và phải nói đúng chỗ)

Cùng bình 500 mL, nhưng ở phút 40 y tá vô tình đè lên dây.

| Thời điểm | Cân | Giọt | Luật cân↔giọt | Model 1 | Cấp |
|---|---:|---:|---|---|---|
| 39 phút | 380 g | 60/ph | bình thường | bình thường | 0 |
| 40 phút | 380 g | 42/ph | chưa đủ 60 s để kết luận | **bắt đầu lệch dự báo** (1/11) | 0 |
| 40 ph 11 s | 379 g | 20/ph | — | **11/11 → xác nhận** | **1 VÀNG** |
| 41 phút | 379 g | 8/ph | **cân ĐỨNG YÊN + giọt chậm → TẮC DÂY** | — | **1 VÀNG** |
| 43 phút | 379 g | 0/ph | vẫn tắc | (residual về 0, xem ghi chú) | **1 VÀNG** |

Màn hình: *"KIỂM TRA DÂY TRUYỀN"*. Huy hiệu: `IV LINE`. Còi chậm 1 giây. Trạng
thái giường trên console: **Warning**, không phải Critical — vì bệnh nhân vẫn ổn.

> **Ghi chú quan trọng:** từ phút 43, Model 1 **thôi báo bất thường**. Không phải
> nó hỏng: model được huấn luyện bất biến với mức vận hành, nên khi dòng chảy đã
> ổn định ở trạng thái mới (0 giọt/phút), nó dự báo đúng trạng thái đó và sai số
> về 0. **Model dự báo bắt CHUYỂN BIẾN, không giữ báo động.** Giữ báo động là
> việc của luật cân↔giọt và luật cứng — chúng nhìn *trạng thái hiện tại*. Đó là
> lý do hệ thống cần cả ba, và bỏ bớt một cái thì sự cố kéo dài sẽ **âm thầm tắt
> chuông trong khi bệnh nhân vẫn đang gặp sự cố**.

So sánh B và C: **cùng là "giọt chậm còn 20–40%"**, hai kết luận trái ngược. Thứ
phân định duy nhất là **cân có nhẹ đi hay không**.

### Kịch bản D — Quá tải dịch (cấp 3, hiếm nhưng nguy hiểm nhất)

Dây tuột khỏi khoá, dịch chảy tự do vào bệnh nhân.

| Giây | Cân | Giọt | SpO2 | HR | Nhánh dịch | Nhánh bệnh nhân | Cấp |
|---:|---:|---:|---:|---:|---|---|---|
| 0 | 300 g | 60/ph | 98% | 78 | — | — | 0 |
| 5 | 297 g | 180/ph | 98% | 80 | Model 1 lệch (5/11) | — | 0 |
| 11 | 292 g | 195/ph | 97% | 86 | **11/11 xác nhận** | — | **1 VÀNG** |
| 20 | 280 g | 200/ph | 96% | 95 | cân giảm rất nhanh → **CHẢY TỰ DO** | Model 3 bắt đầu lệch | 1 |
| 35 | 258 g | 200/ph | 93% | 112 | chảy tự do | **Model 3 xác nhận** (11/11) | **3 ĐỎ KHẨN** |
| 50 | 235 g | 200/ph | 88% | 128 | chảy tự do | **luật cứng SpO2 < 90 nổ ngay** | **3 ĐỎ KHẨN** |

Ở giây 35, **chưa có ngưỡng cứng nào bị vi phạm**: SpO2 93% (> 90), HR 112
(< 150). Nhưng Model 3 thấy **tổ hợp** này bất thường, và nhánh dịch đang báo —
nên hệ thống đã lên **cấp 3** trước khi SpO2 chạm 90% tới **15 giây**.

Màn hình: *"NGUY HIỂM: NGHI NGỜ QUÁ TẢI DỊCH"*. Đèn đỏ **+** vàng, còi 0,15 giây.
Console: `LINE + PATIENT`, và thông điệp đầu tiên y tá đọc là *"Infusion line AND
patient vitals both abnormal — suspected fluid overload"* — tức là **khoá dây
truyền trước**, chứ không phải chỉ xử trí tụt oxy.

> Đây là lý do cấp 3 tồn tại như một mục riêng chứ không phải "cấp 2 to hơn":
> xử trí đầu tiên khác hẳn.

---

## 9. AI hỏng thì sao

**Thiết bị vẫn là máy theo dõi.** Đây là ràng buộc thiết kế, không phải may mắn:

* Ba model chạy trên **ba bộ nhớ riêng biệt**. Hỏng một model thì hai model kia
  vẫn chạy. (Gộp chung vào một file thì hỏng một là mất cả ba.)
* Không model nào nạp được → **luật lâm sàng cứng vẫn báo động đầy đủ**. Có bài
  kiểm thử riêng cho đúng tình huống này.
* Nhóm đã **tắt** cơ chế tự khởi tạo model của SDK Silicon Labs, vì nó xử lý lỗi
  bằng vòng lặp vô hạn ngay trong lúc khởi động hệ thống — model lỗi sẽ làm chip
  treo, mất luôn cảm biến, màn hình, Zigbee và cả luật lâm sàng. Với thiết bị y
  tế, **treo im lặng là kiểu hỏng tệ nhất có thể có**.
* Mất mạng, mất Zigbee, tắt server: đèn và còi ở đầu giường vẫn hoạt động.

---

## 10. Những gì AI này KHÔNG làm

Nói rõ để không ai kỳ vọng nhầm:

* **Không chẩn đoán bệnh.** Nó phát hiện *lệch khỏi bình thường*, không kết luận
  nguyên nhân.
* **Không thay bác sĩ đặt y lệnh.** Tốc độ truyền do bác sĩ đặt; AI chỉ so số đo
  với y lệnh đó.
* **Không tự điều chỉnh dịch truyền.** Thiết bị chỉ theo dõi và báo động.
* **Recall của nhánh sinh hiệu chưa chứng minh được bằng dữ liệu.** Bộ dữ liệu
  ICU dùng để đánh giá (PhysioNet BIDMC) gồm các đoạn 8 phút phần lớn ổn định và
  **không chứa ca diễn biến xấu dần nào** để đo. Đo được trên nó là **tỉ lệ báo
  giả**; **không** đo được là tỉ lệ bắt đúng. Vì vậy luật lâm sàng cứng là lưới
  an toàn **chính** cho sinh hiệu, còn AI là lớp cảnh báo sớm bổ sung. Nhóm nêu
  rõ điều này thay vì để nó trôi qua.

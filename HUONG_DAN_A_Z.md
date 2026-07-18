# Smart IV Monitoring System — Tài liệu A-Z (chip → app)

Tài liệu này giải thích **toàn bộ hệ thống**, từ con chip cảm biến gắn trên
giường bệnh nhân cho tới màn hình web mà y tá nhìn thấy. Viết cho người **chưa
biết gì về hệ thống này** — đọc từ đầu tới cuối là hiểu được toàn bộ luồng dữ
liệu, vì sao lại thiết kế như vậy, và sờ vào đâu nếu cần sửa/mở rộng.

> Đây là bản rewrite hoàn toàn của tài liệu cũ. Bản cũ mô tả app WinForms
> (`Server_FPT_upload` / `designWebForQuanKa-main/Server`) — app đó **không còn
> được dùng nữa**, đã được viết lại thành app ASP.NET Core (`HisServer`) mô tả
> trong tài liệu này.

---

## 0. Bức tranh toàn cảnh

```
┌──────────────┐  Zigbee   ┌──────────────┐  USB/UART   ┌─────────────────────────────┐
│  Board cảm   │ ────────► │  Board NCP   │ ──────────► │  Raspberry Pi               │
│  biến (chip  │           │ (Coordinator)│             │  - mosquitto (MQTT broker)  │
│  empty_2)    │           │              │             │  - zigbee2mqtt              │
│  BRD2709A    │           │  BRD2709A    │             │  - gateway_test (C program) │
│  xG26        │           │  xG26        │             │                             │
└──────────────┘           └──────────────┘             └──────────┬──────────────────┘
                                                                     │ TCP JSON (cổng 5000)
                                                                     ▼
                                                     ┌───────────────────────────────┐
                                                     │  HIS Server (ASP.NET Core)    │
                                                     │  - TCP ingestion              │
                                                     │  - MySQL (Docker)             │
                                                     │  - REST API + SignalR         │
                                                     │  - Web UI (HTML/JS thuần)     │
                                                     └───────────────────────────────┘
                                                                     │
                                                                     ▼
                                                          Trình duyệt (y tá xem)
```

Có **4 "trạm" xử lý** dữ liệu, mỗi trạm là một chương trình/thiết bị độc lập:

| # | Trạm | Chạy ở đâu | Vai trò |
|---|---|---|---|
| 1 | Firmware `empty_2` | Board cảm biến (gắn giường) | Đọc cảm biến giọt, chạy AI, tính ra HR/SpO2/Flow/Drop/cảnh báo, gửi qua Zigbee |
| 2 | Firmware NCP | Board thứ 2 (coordinator) | "Phiên dịch" Zigbee ↔ USB, không có logic riêng, dùng nguyên bản Silicon Labs |
| 3 | zigbee2mqtt + `gateway_test` | Raspberry Pi | Nhận gói Zigbee qua NCP, giải mã thành JSON dễ đọc (MQTT), rồi bắn tiếp qua TCP tới HIS Server |
| 4 | HIS Server | Máy chủ (hiện đang chạy trên máy dev) | Nhận dữ liệu, lưu MySQL, tính cảnh báo, phục vụ web UI cho y tá |

Vì sao chia làm nhiều trạm thay vì 1 chương trình duy nhất? Vì **mỗi trạm dùng
công nghệ khác nhau** — firmware là C nhúng (embedded C), zigbee2mqtt là
Node.js (thư viện Zigbee mạnh nhất hiện có), gateway là C thuần (nhẹ, chạy tốt
trên Pi), HIS Server là ASP.NET Core (web/API/DB mạnh). Ghép qua giao thức
chuẩn (Zigbee, MQTT, TCP/JSON) ở giữa để mỗi phần độc lập, dễ thay/sửa riêng.

---

## 0.5 Kiến thức nền — các khái niệm cần hiểu trước khi đọc tiếp

Phần này giải thích **lý thuyết đứng sau mỗi từ khoá**, không phải cách chạy.
Nếu đã quen Zigbee/MQTT có thể bỏ qua, nhảy thẳng xuống mục 1.

### 0.5.1 Zigbee là gì, và vì sao dùng nó thay vì WiFi/Bluetooth?

Zigbee là 1 giao thức mạng không dây, thiết kế riêng cho thiết bị IoT công
suất cực thấp (chạy pin được nhiều tháng/năm), tốc độ dữ liệu thấp (không cần
truyền video/audio, chỉ vài số liệu nhỏ mỗi giây), và đặc biệt hỗ trợ
**mesh network** (mạng lưới): mỗi thiết bị có thể "chuyển tiếp" gói tin hộ
thiết bị khác, nên vùng phủ sóng thực tế lớn hơn nhiều so với WiFi 1-1, và
mạng không sập nếu 1 thiết bị nào đó bị mất kết nối (gói tin tự tìm đường
khác). Đây là lý do các thiết bị y tế/nhà thông minh (cảm biến, công tắc,
khoá cửa...) hay chọn Zigbee thay vì WiFi: pin bé, chi phí thấp, không cần
router riêng.

Trong 1 mạng Zigbee luôn có **đúng 1 Coordinator** (thiết bị khởi tạo mạng,
giữ "sổ" ai đã join), và nhiều **Router**/**End Device** (thiết bị con join
vào). Trong hệ thống này: board NCP = Coordinator, board cảm biến `empty_2` =
End Device (join vào mạng do NCP tạo ra).

### 0.5.2 ZCL, cluster, endpoint, attribute — đơn vị dữ liệu nhỏ nhất của Zigbee

Zigbee tự nó chỉ lo việc "gửi gói tin từ A đến B qua mesh" — còn **định dạng
bên trong gói tin** (con số này nghĩa là gì, kiểu dữ liệu ra sao) do một tầng
phía trên quy định, gọi là **ZCL** (Zigbee Cluster Library). Ba khái niệm cần
nhớ, xếp từ to đến nhỏ:

- **Endpoint**: một thiết bị Zigbee có thể đóng nhiều "vai" cùng lúc — ví dụ 1
  ổ cắm thông minh 4 lỗ có thể có 4 endpoint, mỗi endpoint điều khiển 1 lỗ độc
  lập. Đánh số 1-255. Trong project này, board cảm biến có 6 endpoint, **mỗi
  endpoint không phải "4 lỗ cắm" mà là "1 kênh dữ liệu"** (HR/SpO2/Flow/
  Drop/Alarm/Basic) — dùng endpoint như một cách chia dữ liệu ra nhiều luồng
  độc lập trên cùng 1 thiết bị vật lý.
- **Cluster**: trong 1 endpoint, dữ liệu được nhóm theo **cluster** — 1 cluster
  là 1 "bộ chức năng chuẩn hoá" được đặt tên và đánh mã số sẵn trong chuẩn ZCL,
  ví dụ cluster "Temperature Measurement" (mã `0x0402`) định nghĩa sẵn: có 1
  attribute tên `MeasuredValue` kiểu số nguyên 16-bit có dấu, đơn vị 1/100 °C.
  Bất kỳ thiết bị Zigbee nào trên thế giới dùng đúng cluster này đều hiểu được
  nhau — đó là lý do Zigbee có tính "chuẩn hoá, cắm là chạy" giữa các hãng.
- **Attribute**: từng "ô dữ liệu" cụ thể bên trong cluster, ví dụ
  `MeasuredValue`. Đọc/ghi attribute là đơn vị thao tác nhỏ nhất khi giao tiếp
  Zigbee (đọc 1 attribute, ghi 1 attribute, hoặc "report" khi nó đổi giá trị).

### 0.5.3 Vì sao không viết "custom cluster" mà lại đi "mượn" cluster có sẵn?

Về lý thuyết, cách làm đúng chuẩn nhất là định nghĩa **cluster tuỳ chỉnh**
(custom cluster) — tự đặt mã cluster riêng (dải mã `0xFC00-0xFFFF` dành cho
mục đích này), tự định nghĩa attribute tên `HeartRate`, `Spo2`... với đúng ý
nghĩa ngữ nghĩa. Đây là cách "sạch" về mặt thiết kế: bên đọc dữ liệu (z2m)
không cần biết quy ước ngầm gì cả, cứ đọc đúng tên attribute là ra đúng nghĩa.

Nhưng làm custom cluster đòi hỏi sửa sâu vào file cấu hình ZAP (công cụ định
nghĩa cluster của Silicon Labs) — phải tự khai attribute mới, tự sinh lại mã
nguồn tương ứng, và phải sửa **cả 2 phía** (firmware VÀ converter zigbee2mqtt
đều phải biết cluster mới này, converter phải tự khai báo cluster đó vì
zigbee-herdsman — thư viện lõi mà z2m dùng — không có sẵn cluster này trong
danh sách chuẩn). Việc này cần thời gian tìm hiểu ZAP khá kỹ và dễ làm hỏng
cấu hình hiện có nếu chưa quen (rủi ro cao trong thời gian ngắn của dự án).

Giải pháp thực dụng đã chọn: dùng lại 5 cluster đo lường **có sẵn, đã được
test kỹ trong SDK** (Temperature/Humidity/Flow/Pressure/Illuminance
Measurement), chỉ khác là **gán ý nghĩa khác** cho con số nằm trong
`MeasuredValue` của chúng (ví dụ cluster "Temperature Measurement" đáng lẽ
chứa nhiệt độ, ở đây lại chứa nhịp tim). Đánh đổi: mất đi tính "tự giải
thích" của dữ liệu (ai đọc thẳng gói Zigbee bằng công cụ chuẩn sẽ tưởng đó là
nhiệt độ), bù lại: không phải sửa ZAP, không phải tự viết code đọc cluster lạ
ở phía z2m (zigbee-herdsman đã biết sẵn 5 cluster chuẩn này), giảm rủi ro kỹ
thuật đáng kể. Miễn là **converter (`zigbee2mqtt_smart_iv_converter.js`)** ghi
rõ quy ước "endpoint nào = ý nghĩa gì" thì hệ thống vẫn hoạt động đúng — đây
chính là lý do file converter có comment giải thích rất kỹ ở đầu file.

Nếu sau này có thời gian làm chuẩn hơn, hướng nâng cấp là viết custom cluster
thật (ví dụ cluster `0xFC01` "Smart IV Vitals" với attribute tên đúng nghĩa)
— khi đó bỏ được toàn bộ quy ước "mượn" này, nhưng không bắt buộc vì hệ thống
hiện tại hoạt động đúng và ổn định.

### 0.5.4 MQTT là gì?

MQTT là 1 giao thức nhắn tin theo mô hình **publish/subscribe** (xuất bản/
đăng ký), khác hẳn kiểu "gọi thẳng" (như HTTP request/response). Có 1
**broker** (ở đây là `mosquitto`) đứng giữa; bên gửi (`zigbee2mqtt`) **publish**
dữ liệu lên 1 **topic** (ví dụ `zigbee2mqtt/SmartIV-Sensor` — có thể hiểu như
1 "kênh" đặt tên bằng chuỗi, giống tên nhóm chat); bên nhận
(`gateway_test`, hoặc bất kỳ chương trình nào) **subscribe** topic đó, tự động
nhận được mọi tin nhắn publish lên topic mà không cần biết bên gửi là ai/ở
đâu. Ưu điểm so với gọi thẳng: bên gửi và bên nhận **không cần biết địa chỉ
của nhau** (chỉ cần cùng biết địa chỉ broker + tên topic), và có thể có
**nhiều bên nhận cùng lúc** (ví dụ vừa `gateway_test` vừa 1 công cụ debug
`mosquitto_sub` đều subscribe cùng topic mà không xung đột).

### 0.5.5 zigbee2mqtt là gì, và vì sao chọn nó thay vì tự viết?

Zigbee là 1 giao thức khá phức tạp ở tầng thấp (mã hoá mạng, quản lý mesh,
xử lý ZCL, hàng trăm loại thiết bị/cluster khác nhau của các hãng khác nhau).
Tự viết 1 chương trình đọc hiểu toàn bộ giao thức này từ đầu (gọi là viết 1
"Zigbee host stack") là công việc rất lớn, mất hàng tháng, dễ có bug ở tầng
thấp gây mất gói tin/crash. **zigbee2mqtt** là 1 dự án mã nguồn mở (Node.js)
đã làm sẵn việc này — nó nói chuyện với 1 con chip coordinator qua USB
(nhiều hãng chip khác nhau, trong đó có dòng EmberZNet của Silicon Labs mà dự
án này dùng), tự lo toàn bộ phần khó (mesh, bảo mật, ZCL), và **xuất dữ liệu
ra MQTT** — 1 giao thức đơn giản, dễ tích hợp với bất kỳ ngôn ngữ/hệ thống
nào (ở đây là chương trình C `gateway_test`). Chọn zigbee2mqtt vì: mã nguồn
mở, cộng đồng lớn (hỗ trợ hàng nghìn loại thiết bị Zigbee thương mại), và có
cơ chế **external converter** (mục 3.2) cho phép "dạy" nó hiểu 1 thiết bị lạ
(tự chế, như board cảm biến này) mà không cần sửa code lõi của z2m.

### 0.5.6 "Gateway" nghĩa là gì trong hệ thống IoT, và tại sao cần?

"Gateway" (cổng nối) là thuật ngữ chung chỉ 1 thành phần đứng giữa 2 mạng/
giao thức khác nhau, dịch dữ liệu từ bên này sang bên kia. Board cảm biến nói
Zigbee — nhưng HIS Server (chạy trên máy chủ, có thể ở xa, kết nối qua mạng
LAN/Internet) không thể "nghe" Zigbee trực tiếp (Zigbee cần phần cứng radio
chuyên dụng, tầm hoạt động ngắn ~10-100m). Cần một điểm trung chuyển: máy vật
lý (Raspberry Pi) cắm USB vào board NCP (nghe được Zigbee), chạy phần mềm
dịch dữ liệu sang giao thức mà mạng thường (Ethernet/WiFi/Internet) mang đi
được. Trong tài liệu này, "gateway" được dùng cho **cả cụm** Pi + NCP +
zigbee2mqtt + `gateway_test` nói chung, đôi khi dùng hẹp hơn để chỉ riêng
chương trình `gateway_test` (chương trình C do dự án tự viết, phần "gateway"
cuối cùng dịch từ MQTT sang TCP/JSON cho HIS Server) — ngữ cảnh trong mỗi mục
sẽ nói rõ.

### 0.5.7 NCP vs SoC, EZSP là gì?

Chip Silicon Labs EFR32 chạy Zigbee có 2 kiểu firmware mẫu:
- **SoC** (System-on-Chip): firmware Zigbee VÀ logic ứng dụng (đọc cảm biến,
  điều khiển đèn...) chạy chung trên 1 chip, độc lập, không cần máy tính
  đứng cạnh. Đây là kiểu board cảm biến `empty_2` dùng.
- **NCP** (Network Co-Processor): firmware chỉ lo phần Zigbee (join mạng, mã
  hoá, định tuyến mesh...), **không có logic ứng dụng riêng** — nó chỉ là
  "cái tai nghe Zigbee" cắm vào máy tính qua USB/UART, và máy tính (ở đây là
  zigbee2mqtt) mới là nơi có logic thật. Giao tiếp giữa firmware NCP và máy
  tính dùng giao thức **EZSP** (EmberZNet Serial Protocol — giao thức riêng
  của Silicon Labs để "ra lệnh" cho chip qua UART, ví dụ "join mạng đi",
  "gửi gói này", "tao nhận được gói kia").

Vì cần zigbee2mqtt (chạy trên Pi, không phải trên chip) làm Coordinator thật
sự (giữ danh sách thiết bị, xử lý ZCL ở tầng phần mềm dễ sửa hơn), board thứ
2 phải flash **firmware NCP** (không phải SoC) để "nhường" vai trò xử lý
logic cho zigbee2mqtt qua EZSP.

---

## 1. Board cảm biến — firmware `empty_2`

Thư mục: `~/SimplicityStudio/v6_workspace/empty_2/`. Đây là project Simplicity
Studio cho chip **EFR32xG26** (board `BRD2709A`).

### 1.1 Kiến trúc phần mềm firmware

Ba lớp, mỗi lớp một file:

```
sensor_hub.{c,h}   ← đọc cảm biến vật lý (giọt/loadcell/MAX30102), theo dõi "còn tín hiệu hay không"
ai_monitor.{c,h}   ← gom dữ liệu, chuẩn hoá, chạy model AI + luật lâm sàng, ra kết luận cảnh báo
app.c              ← vòng lặp chính, gọi 2 lớp trên, rồi ghi kết quả vào Zigbee
```

**`sensor_hub.h`** — mỗi kênh cảm biến có 3 trạng thái, đây là ý tưởng cốt lõi
của toàn hệ thống:

```c
typedef enum { CH_DISABLED = 0, CH_OK = 1, CH_LOST = 2 } ch_state_t;
```

- `CH_DISABLED`: cảm biến **chưa được lắp vào mạch** (chuyển mạch phần mềm tắt
  sẵn) → bỏ qua hoàn toàn, không báo động nhầm.
- `CH_OK`: có dữ liệu tươi (mới) → đọc và dùng giá trị thật.
- `CH_LOST`: **đã bật kênh** (tức phần cứng lẽ ra phải có) nhưng quá
  `VITAL_TIMEOUT_MS` (3 giây) không có mẫu mới → coi là mất tín hiệu, phải báo.

Bật/tắt từng kênh bằng macro ở đầu file:

```c
#define HR_ENABLED     0   // MAX30102 - nhịp tim        → CHƯA LẮP trên board demo hiện tại
#define SPO2_ENABLED   0   // MAX30102 - SpO2             → CHƯA LẮP
#define FLOW_ENABLED   0   // loadcell / lưu lượng dịch   → CHƯA LẮP
#define DROPS_ENABLED  1   // cảm biến giọt               → ĐÃ LẮP, đã test
```

Đây chính là lý do vì sao ở app hiện tại, giường demo `BED-101` luôn hiện "mất
tín hiệu" cho HR/SpO2/Flow — không phải lỗi phần mềm, mà là phần cứng demo chỉ
lắp đúng 1 cảm biến giọt. Khi lắp thêm MAX30102/loadcell thật, chỉ cần bật các
macro này lên `1`, không cần sửa gì khác — kiến trúc đã tính sẵn cho việc mở
rộng dần.

### 1.2 `ai_monitor` — 6 đặc trưng + AI + luật lâm sàng

Mỗi giây, `ai_monitor_step()` gom 6 con số ("đặc trưng", feature) theo đúng
thứ tự (khớp với model đã train):

```
[0] heart_rate    nhịp tim (bpm)
[1] spo2          % bão hoà oxy
[2] flow_ratio    lưu lượng thực / mức bác sĩ đặt   (1.0 = đúng mức)
[3] drops_ratio   giọt/phút thực / mức bác sĩ đặt
[4] vital_missing cờ: mất tín hiệu HR hoặc SpO2 (sau khi đã gắn máy)
[5] line_missing  cờ: mất tín hiệu đường truyền (Flow hoặc Drops)
```

Hai cơ chế phát hiện bất thường **chạy song song, OR với nhau** (bất kỳ cái nào
báo thì coi là có cảnh báo — ưu tiên không bỏ sót hơn là ít báo nhầm):

**A. Luật lâm sàng cứng** (ngưỡng cố định, dễ hiểu, dễ audit):

| Điều kiện | Ngưỡng | Ý nghĩa |
|---|---|---|
| `reason_hr` | lệch > 30% baseline cá nhân, HOẶC < 45 bpm, HOẶC > 150 bpm | Nhịp tim bất thường |
| `reason_spo2` | SpO2 < 90% | Tụt oxy máu |
| `reason_flow` | flow_ratio > 1.5× hoặc < 0.3× mức đặt | Free-flow (chảy tự do, nguy hiểm) hoặc tắc đường truyền |
| `reason_missing` | vital_missing hoặc line_missing = 1 | Mất tín hiệu cảm biến |

**B. Autoencoder (mô hình AI, TensorFlow Lite Micro)**: model học "dáng vẻ
bình thường" của 6 đặc trưng từ dữ liệu huấn luyện; khi đầu vào hiện tại khác
lạ so với những gì model từng thấy, sai số tái tạo (`recon_error`) tăng cao.
Nếu `recon_error > 1.4336` (ngưỡng `AI_AE_THRESHOLD`, chọn từ tập validation
lúc train) → `reason_ae = 1`. Đây là lớp phát hiện các bất thường **mà luật
cứng không định nghĩa trước được** (ví dụ dao động bất thường nhưng không vượt
ngưỡng tuyệt đối nào).

Baseline nhịp tim cá nhân hoá: 60 giây đầu sau khi gắn máy, hệ thống lấy nhịp
tim làm "mức nền" của riêng bệnh nhân đó (`ai_monitor_set_hr_baseline`), rồi
so lệch % với mức nền này thay vì một ngưỡng chung cho mọi người.

### 1.3 Đưa kết quả lên Zigbee — cluster & endpoint (phần "khó" nhất)

Đây là phần hay bị hỏi nhất nên giải thích kỹ. Zigbee không có sẵn khái niệm
"gửi 1 cục JSON tuỳ ý" như MQTT/HTTP — nó bắt buộc dữ liệu phải thuộc về một
**cluster** (nhóm chức năng chuẩn hoá, ví dụ "đo nhiệt độ", "đo độ ẩm"...),
và mỗi cluster có các **attribute** (thuộc tính) với kiểu dữ liệu cố định.

Cách chuẩn để làm đúng là định nghĩa **cluster tuỳ chỉnh** (custom cluster) —
nhưng việc đó đòi hỏi sửa file ZAP (ZCL Advanced Platform, công cụ cấu hình
Zigbee của Silicon Labs) ở mức khá sâu và dễ vỡ nếu chưa quen. Vì thời gian có
hạn, project này chọn giải pháp thực dụng: **"mượn"** 5 cluster đo lường có
sẵn trong SDK (được test kỹ, ổn định), rồi gán ý nghĩa khác cho attribute
`MeasuredValue` của chúng. Đây là điểm quan trọng nhất cần nhớ: **con số nằm
trong attribute không mang đúng ý nghĩa gốc của cluster** — bên nhận
(zigbee2mqtt) phải biết trước quy ước này để đọc lại đúng.

Có 6 endpoint (một board Zigbee có thể có nhiều "cổng" logic, mỗi cổng là 1
endpoint, đánh số 1-255):

| Endpoint | Cluster mượn (mã số) | Attribute | Kiểu | Ý nghĩa THẬT trong hệ thống này |
|---|---|---|---|---|
| 1 | Basic (`0x0000`) | `manufacturerName`="ICTU", `modelIdentifier`="SmartIV-Sensor" | string | Định danh thiết bị, để zigbee2mqtt nhận diện |
| 2 | Temperature Measurement (`0x0402`) | `MeasuredValue` | int16s | **HR** — nhịp tim (bpm), KHÔNG phải nhiệt độ |
| 3 | Relative Humidity Measurement (`0x0405`) | `MeasuredValue` | uint16 | **SpO2** — % bão hoà oxy, KHÔNG phải độ ẩm |
| 4 | Flow Measurement (`0x0404`) | `MeasuredValue` | uint16 | **Flow ratio** — × 100 để giữ số nguyên (100 = đúng 1.00×) |
| 5 | Pressure Measurement (`0x0403`) | `MeasuredValue` | int16s | **Drop ratio** — × 100, ý nghĩa gốc là "áp suất" nhưng dùng cho tốc độ giọt |
| 6 | Illuminance Measurement (`0x0400`) | `MeasuredValue` | uint16 | **Bitmap cảnh báo + trạng thái tín hiệu** (chi tiết bên dưới) |

Vì sao chọn đúng 5 cluster đo lường này (Temperature/Humidity/Flow/Pressure/
Illuminance)? Vì chúng đều dùng chung 1 kiểu attribute đơn giản
(`MeasuredValue`, số nguyên 16-bit) — không cluster nào đòi hỏi cấu trúc phức
tạp (mảng, struct) — nên code ghi/đọc giống hệt nhau cho cả 5, chỉ khác
clusterId/endpoint. Chọn 5 cluster **khác nhau** (không dùng lại 1 cluster ở
nhiều endpoint) để zigbee2mqtt phân biệt được "đây là gói HR" hay "đây là gói
SpO2" chỉ bằng cặp (endpoint, clusterId) — không cần đọc thêm dữ liệu gì khác.

**Endpoint 6 — bitmap cảnh báo (uint16, 9 bit dùng, đọc kỹ phần này để hiểu
toàn bộ hệ thống cảnh báo)**:

```
bit 0 (0x001) = 1  →  reason_missing  : mất tín hiệu (vital hoặc line)
bit 1 (0x002) = 1  →  reason_spo2     : SpO2 thấp (< 90%)
bit 2 (0x004) = 1  →  reason_hr       : nhịp tim bất thường
bit 3 (0x008) = 1  →  reason_flow     : đường truyền tắc / chảy tự do (free-flow)
bit 4 (0x010) = 1  →  reason_ae       : autoencoder (AI) phát hiện bất thường
bit 5 (0x020) = 1  →  kênh HR    đang CÓ tín hiệu (CH_OK)
bit 6 (0x040) = 1  →  kênh SpO2  đang CÓ tín hiệu (CH_OK)
bit 7 (0x080) = 1  →  kênh Flow  đang CÓ tín hiệu (CH_OK)
bit 8 (0x100) = 1  →  kênh Drops đang CÓ tín hiệu (CH_OK)
```

Bit 0-4 (5 bit đầu) = **lý do cảnh báo** (điều kiện xấu). Bit 5-8 (4 bit sau)
= **kênh nào có tín hiệu** — độc lập với bit cảnh báo, vì một kênh có thể vừa
"có tín hiệu" vừa "đang báo động" (ví dụ nhịp tim đo được nhưng bất thường),
hoặc "chưa từng nối" (bit tương ứng luôn = 0, không phải "mất" mà là "chưa
lắp"). Việc tách 2 nhóm bit này (thay vì chỉ có 1 "báo động chung") chính là
điều app phía sau cần để phân biệt "chưa lắp cảm biến" với "đã lắp nhưng đang
báo động" — nếu gộp chung, y tá sẽ không biết cần đi lắp thêm cảm biến hay
cảm biến đang thật sự bị lỗi.

Code build bitmap này nằm ở `app.c`, hàm `app_process_action()`:

```c
uint16_t alarm_bitmap = (uint16_t)((r.reason_missing ? 0x01 : 0)
                                    | (r.reason_spo2    ? 0x02 : 0)
                                    | (r.reason_hr      ? 0x04 : 0)
                                    | (r.reason_flow    ? 0x08 : 0)
                                    | (r.reason_ae      ? 0x10 : 0)
                                    | (sh_hr_state()    == CH_OK ? 0x20  : 0)
                                    | (sh_spo2_state()  == CH_OK ? 0x40  : 0)
                                    | (sh_flow_state()  == CH_OK ? 0x80  : 0)
                                    | (sh_drops_state() == CH_OK ? 0x100 : 0));
```

### 1.4 Tự động báo cáo (reporting) — firmware chủ động, không cần Pi cấu hình

Zigbee có cơ chế "reporting": thiết bị tự gửi giá trị mới khi thay đổi, không
cần bên coordinator liên tục hỏi (poll). `zb_configure_reporting()` (gọi 1 lần
khi vào mạng, `sl_zigbee_af_stack_status_cb` nhận `SL_STATUS_NETWORK_UP`) đăng
ký cho cả 5 attribute mang dữ liệu: gửi report tối thiểu mỗi 1 giây, tối đa
mỗi 60 giây (nếu 60s không đổi vẫn phải gửi 1 lần để bên nhận biết thiết bị
còn sống). Vì cấu hình này nằm **trong chính firmware**, hệ thống không phụ
thuộc việc zigbee2mqtt phải gọi đúng lệnh ZCL `ConfigureReporting` (lệnh này
hay bị timeout — xem mục 4.2).

Mỗi giây, `app_process_action()` gọi `zb_report_ai_result()` ghi cả 5 giá trị
vào 5 attribute — reporting plugin tự phát hiện thay đổi và gửi gói Zigbee đi.

### 1.5 Yêu cầu cấu hình quan trọng

- **Component cần cài** (Simplicity Studio → Software Components, hoặc sửa
  `.slcp`): `zigbee_basic`, `zigbee_core_cli`, `zigbee_network_steering`,
  `zigbee_pro_stack`, `zigbee_reporting`, `zigbee_zcl_cli`.
- **`SL_ZIGBEE_BINDING_TABLE_SIZE`** trong
  `config/sl_zigbee_pro_stack_config.h` phải **≥ 5** (mặc định chỉ 3 → không
  đủ chỗ cho 5 binding cần thiết, gateway sẽ báo lỗi `TABLE_FULL` khi bind).
  Đã tăng lên 10.
- Khi mất mạng (`NETWORK_DOWN`), firmware tự gọi
  `sl_zigbee_af_network_steering_start()` để tự tìm và join lại mạng — không
  cần can thiệp thủ công mỗi lần mất điện/reset, miễn là coordinator (NCP) vẫn
  còn cùng mạng đó.

### 1.6 Build & flash (không cần mở GUI Simplicity Studio)

```bash
CMAKE=~/.silabs/slt/installs/conan/p/cmake*/p/bin/cmake
NINJA=~/.silabs/slt/installs/conan/p/ninja*/p/ninja
SLC=~/.silabs/slt/installs/archive/slc-cli-*/slc_cli/slc
COMMANDER=~/.silabs/slt/installs/archive/commander/commander

# Sinh lại code khi sửa file .zap hoặc .slcp:
cd ~/SimplicityStudio/v6_workspace/empty_2
$SLC generate empty_2.slcp -d . -np \
  --sdk-package-path ~/.silabs/slt/installs/conan/p/<simplicity_sdk_id>/p,~/.silabs/slt/installs/conan/p/<aiml_ext_id>/p \
  --with cli:inst0

# Build:
cd cmake_gcc/build && $NINJA

# Flash (thay serial number đúng board cảm biến — xem bằng lsusb/udevadm):
$COMMANDER flash base/empty_2.hex --serialno <SERIAL_BOARD_CAM_BIEN>
```

Xem serial number + cổng `ttyACM` tương ứng (hữu ích khi có nhiều board cắm
cùng lúc, USB có thể đổi số cổng khi cắm lại):
```bash
udevadm info -q property -n /dev/ttyACM0 | grep ID_SERIAL_SHORT
```

Lệnh CLI hữu ích qua cổng serial (VCOM) của board cảm biến (gửi qua
`/dev/ttyACM0`, baud 115200, dùng `screen`/`minicom`/pyserial):
```
network leave                        # rời mạng hiện tại
plugin network-steering start 0      # bắt đầu join mạng mới (cần permit-join đang bật)
```

---

## 2. Board NCP (Coordinator)

- Trong Simplicity Studio: **File → New Project → chọn ví dụ "Zigbee - NCP"**
  (không phải SoC/Light), build & flash **nguyên bản, không sửa code**.
- Vai trò: đây là **coordinator** của mạng Zigbee — thiết bị trung tâm mà
  board cảm biến join vào. Nó nói giao thức **EZSP** (EmberZNet Serial
  Protocol) qua UART/USB cho phần mềm ở máy tính (zigbee2mqtt, cấu hình
  `adapter: ember`) — bản thân nó không xử lý logic gì, chỉ chuyển tiếp
  khung Zigbee ↔ USB.
- **Không dùng** project `zigbee_ble_dynamic_multiprotocol_light_V2` cho việc
  này — đó là app SoC độc lập (đóng vai "đèn" trong ví dụ mẫu), không nói
  được EZSP nên zigbee2mqtt không điều khiển được nó.

---

## 3. Raspberry Pi — zigbee2mqtt

Toàn bộ "gateway" (NCP cắm USB + zigbee2mqtt + mosquitto + gateway_test) hiện
đặt trên **Raspberry Pi** (thiết kế ban đầu là Pi làm gateway; trước đó có
giai đoạn tạm chạy trên máy dev, đã chuyển hẳn sang Pi).

### 3.1 Cài đặt

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
export NVM_DIR="$HOME/.nvm"; [ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
nvm install 22 && nvm alias default 22

git clone --depth 1 https://github.com/Koenkk/zigbee2mqtt.git ~/zigbee2mqtt
cd ~/zigbee2mqtt
git fetch --tags --depth 1
git checkout 2.12.1          # bản đã test thành công, hỗ trợ EZSP v18

corepack enable
corepack prepare pnpm@10.18.3 --activate
pnpm install && pnpm run build

sudo apt update && sudo apt install -y mosquitto mosquitto-clients libmosquitto-dev
```

### 3.2 `data/configuration.yaml` (trên Pi hiện tại)

```yaml
homeassistant:
  enabled: false
mqtt:
  base_topic: zigbee2mqtt
  server: mqtt://localhost:1883
serial:
  port: /dev/ttyACM0        # cổng của board NCP — kiểm tra đúng port trước khi chạy
  adapter: ember
advanced:
  log_level: info
frontend:
  enabled: true
  port: 8080
  host: 0.0.0.0
version: 5
devices:
  '0x64028ffffe641802':      # địa chỉ IEEE thiết bị cảm biến
    friendly_name: SmartIV-Sensor
```

Lưu ý bản zigbee2mqtt 2.x: `permit_join` không còn là key trong config — phải
bật qua MQTT lúc cần pair thiết bị mới (xem mục 3.4). External converter phải
đặt trong `data/external_converters/`, không khai trong `configuration.yaml`
như bản cũ.

### 3.2 External converter — "từ điển" giải mã 5 cluster mượn

File: `zigbee2mqtt_smart_iv_converter.js` (repo `empty_2`), copy vào
`~/zigbee2mqtt/data/external_converters/` trên Pi.

**Nhận diện thiết bị bằng fingerprint (không dùng chuỗi `zigbeeModel`)**:
đọc `modelID` qua cluster Basic lúc "interview" (bước đầu khi thiết bị mới
join, z2m hỏi thăm dò thông tin) không ổn định — đôi khi trả về `undefined`.
Giải pháp: nhận diện bằng đúng "vân tay" cấu trúc endpoint/cluster mà 6
endpoint của firmware khai báo:

```js
fingerprint: [{
    endpoints: [
        {ID: 1, inputClusters: [0]},      // Basic
        {ID: 2, inputClusters: [1026]},   // Temperature Measurement
        {ID: 3, inputClusters: [1029]},   // Relative Humidity
        {ID: 4, inputClusters: [1028]},   // Flow Measurement
        {ID: 5, inputClusters: [1027]},   // Pressure Measurement
        {ID: 6, inputClusters: [1024]},   // Illuminance Measurement
    ],
}],
```

**Giải mã (`fromZigbee`)** — mỗi cluster có 1 hàm convert riêng, đọc đúng
`msg.endpoint.ID` để tránh nhầm lẫn (vì nhiều cluster khác nhau vẫn có thể
trùng tên attribute `measuredValue`):

```js
flow:      msg.data.measuredValue / 100,   // đảo ngược phép ×100 bên firmware
drop_rate: msg.data.measuredValue / 100,
```

Bitmap ở endpoint 6 được tách thành **10 field JSON** riêng biệt (dễ đọc hơn
nhiều so với gửi 1 số nguyên thô), dùng đúng các hằng số bit đã định nghĩa ở
mục 1.3:

```js
alarm:               (bitmap & 0x1F) !== 0,   // CHỈ mask 5 bit lý do (0-4), KHÔNG lẫn bit tín hiệu (5-8)
signal_lost:         (bitmap & 0x01) !== 0,
spo2_low:            (bitmap & 0x02) !== 0,
heart_rate_abnormal: (bitmap & 0x04) !== 0,
line_blocked:        (bitmap & 0x08) !== 0,
ae_alarm:            (bitmap & 0x10) !== 0,
hr_signal:           (bitmap & 0x20) !== 0,
spo2_signal:         (bitmap & 0x40) !== 0,
flow_signal:         (bitmap & 0x80) !== 0,
drops_signal:        (bitmap & 0x100) !== 0,
```

> Lỗi từng gặp: nếu tính `alarm` bằng `bitmap !== 0` (không mask), thì ngay
> khi có bất kỳ kênh nào "có tín hiệu" (bit 5-8 = 1) là `alarm` sẽ luôn `true`
> mãi mãi — sai hoàn toàn ý nghĩa "đang báo động". Phải mask đúng `& 0x1F`.

**`configure()` — CHỈ bind, không gọi `configureReporting()`**:

```js
configure: async (device, coordinatorEndpoint, logger) => {
    const bindings = [
        {epId: 2, cluster: 'msTemperatureMeasurement'},
        {epId: 3, cluster: 'msRelativeHumidity'},
        {epId: 4, cluster: 'msFlowMeasurement'},
        {epId: 5, cluster: 'msPressureMeasurement'},
        {epId: 6, cluster: 'msIlluminanceMeasurement'},
    ];
    for (const b of bindings) {
        try {
            const ep = device.getEndpoint(b.epId);
            await reporting.bind(ep, coordinatorEndpoint, [b.cluster]);
        } catch (error) {
            logger.warning(`bind endpoint ${b.epId} failed: ${error.message}`);
        }
    }
},
```

"Bind" nghĩa là báo cho thiết bị biết "gửi report của cluster này tới địa chỉ
coordinator" — cần thiết để report thật sự đến được z2m (không có bind thì
thiết bị vẫn tự tính, nhưng report "gửi đi đâu" không xác định). **Không** gọi
lệnh ZCL `configureReporting()` ở đây (đã bị bỏ có chủ đích): lệnh này hay bị
timeout trên thực tế, và nếu 1 endpoint bị lỗi giữa chừng, vòng lặp `for` sẽ
văng exception và các endpoint sau nó **không được bind luôn** — vì firmware
đã tự cấu hình reporting cục bộ rồi (mục 1.4), z2m chỉ cần bind là đủ.

### 3.4 Chạy & ghép nối (pairing) thiết bị mới

```bash
cd ~/zigbee2mqtt
node index.js                          # log ra terminal + file data/log.log
```

Cho phép thiết bị mới join (ở terminal/session khác):
```bash
mosquitto_pub -h localhost -p 1883 -t 'zigbee2mqtt/bridge/request/permit_join' \
  -m '{"value": true, "time": 254}'
```

Cho board cảm biến rời mạng cũ và join lại (gửi qua serial VCOM, mục 1.6):
```
network leave
plugin network-steering start 0
```

Theo dõi log để xác nhận (`tail -f ~/zigbee2mqtt/data/log.log`) — cần thấy
`"Successfully interviewed"` và `"is supported, identified as: ... SmartIV-Sensor"`.

Nếu thiết bị từng pair với định nghĩa cũ/sai (model hiện "undefined"), xoá và
pair lại:
```bash
mosquitto_pub -h localhost -p 1883 -t 'zigbee2mqtt/bridge/request/device/remove' \
  -m '{"id": "<IEEE_ADDR>", "force": true}'
```

Xem dữ liệu nhanh không cần trình duyệt:
```bash
mosquitto_sub -h localhost -p 1883 -t 'zigbee2mqtt/SmartIV-Sensor' -v
```
Hoặc web UI riêng của zigbee2mqtt: `http://<ip_pi>:8080`.

---

## 4. Gateway — `gateway/main.c`

Chương trình C thuần (không phải firmware, không liên quan Simplicity
Studio), chạy trên Raspberry Pi, đóng vai trò **cầu nối MQTT → TCP**.

### 4.1 Vì sao cần chương trình riêng này (không để HIS Server đọc MQTT thẳng)?

HIS Server không nói MQTT — nó chỉ mở 1 cổng TCP thô nhận dòng JSON kết thúc
`\n` (thiết kế kế thừa từ app cũ, xem mục 5.2). `gateway_test` là lớp trung
gian: subscribe MQTT (nói ngôn ngữ zigbee2mqtt), rồi dịch sang định dạng TCP/
JSON mà HIS Server hiểu.

### 4.2 Luồng xử lý trong `on_message()`

1. Nhận 1 message MQTT trên topic `zigbee2mqtt/SmartIV-Sensor`, payload là 1
   cục JSON phẳng (được z2m tổng hợp từ mọi thuộc tính converter expose).
2. Parse các field bằng 2 hàm tự viết
   (`lay_gia_tri_so_tu_json`/`lay_gia_tri_bool_tu_json` — parser JSON tối
   giản, không dùng thư viện ngoài, đủ dùng vì định dạng payload cố định):
   `heart_rate`, `spo2`, `flow`, `drop_rate`, và toàn bộ 6 cờ bool
   (`alarm`, `signal_lost`, `spo2_low`, `heart_rate_abnormal`, `line_blocked`,
   `ae_alarm`) cùng 4 cờ tín hiệu (`hr_signal`, `spo2_signal`, `flow_signal`,
   `drops_signal`).
3. In toàn bộ ra console (log tiếng Việt, phục vụ debug tại chỗ trên Pi).
4. Gọi `his_send_bed_data(...)` — đóng gói lại thành JSON **tiếng Anh** rồi
   gửi qua socket TCP tới HIS Server.

### 4.3 Định dạng gửi đi (wire format hiện tại)

```json
{"bedId":"BED-101","room":"ICU-1","spo2":98,"heartRate":82,
 "dripRate":0,"flowRate":1,"heartRateSignal":false,"spo2Signal":false,
 "flowSignal":false,"dripRateSignal":true,"lineBlocked":true,"aeAlarm":true}
```

`bedId`/`room` là 2 tham số cấu hình cố định lúc khởi động chương trình (mỗi
gateway vật lý phục vụ đúng 1 giường/phòng — xem mục 8 về mô hình nhiều
phòng). Toàn bộ phần "suy luận trạng thái" (Stable/Warning/Critical) đã bị bỏ
khỏi gateway — gateway chỉ gửi **dữ liệu thô + cờ tín hiệu/cảnh báo**, để HIS
Server tự quyết định trạng thái theo 1 nơi duy nhất
(`VitalsStatusEvaluator.cs`, mục 6.3) — tránh tình trạng 2 nơi tính trạng thái
khác công thức nhau (đây từng là bug thật ở bản app cũ).

### 4.4 Tự kết nối lại TCP

`his_socket` giữ trạng thái kết nối; nếu gửi thất bại (HIS Server restart,
mất mạng...), gateway đóng socket cũ và tự thử `his_connect()` lại ở lần gửi
tiếp theo — không cần khởi động lại `gateway_test`. Có `sleep(1)` trước khi
tự reconnect MQTT nếu vòng lặp `mosquitto_loop()` lỗi, để tránh vòng lặp
connect/disconnect liên tục làm broker liên tục kick client (từng gặp lỗi
"Client ... already connected, closing old connection" khi có 2 tiến trình
cùng dùng 1 client ID chạy song song — luôn đảm bảo chỉ có đúng 1 tiến trình
`gateway_test` chạy tại một thời điểm cho mỗi giường).

### 4.5 Build

```bash
cd gateway
sudo apt install -y libmosquitto-dev     # nếu chưa có
gcc -Wall -Wextra -o gateway_test main.c -lmosquitto
```

(`main.exe` có sẵn trong thư mục là bản build Windows, không chạy được trên
Linux/Pi — luôn build lại bằng `gcc` như trên sau khi sửa `main.c`.)

### 4.6 Chạy thủ công (debug) / tham số dòng lệnh

```bash
./gateway_test <mqtt_host> <mqtt_port> <topic> <his_host> <his_port> <bed_id> <room>

# Ví dụ đúng với cấu hình hiện tại trên Pi:
./gateway_test localhost 1883 zigbee2mqtt/SmartIV-Sensor 192.168.2.10 5000 "BED-101" "ICU-1"
```

Nếu không truyền `<his_host>`, chương trình chỉ in màn hình, không forward đi
đâu — hữu ích để debug độc lập z2m mà không cần HIS Server chạy.

---

## 5. Vận hành trên Raspberry Pi — systemd

Cả 3 tiến trình chạy nền vĩnh viễn qua **systemd**, tự khởi động lại nếu
crash và tự chạy lại sau khi Pi reboot.

```
mosquitto        (cài qua apt, đã có unit sẵn, cổng 1883)
        │
        ▼ (After=)
zigbee2mqtt.service
        │
        ▼ (After=, Requires=)
gateway.service
```

`/etc/systemd/system/zigbee2mqtt.service`:
```ini
[Unit]
Description=Zigbee2MQTT
After=network-online.target mosquitto.service
Wants=network-online.target

[Service]
Type=simple
User=iotchallenge
WorkingDirectory=/home/iotchallenge/zigbee2mqtt
ExecStart=/usr/bin/node index.js
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

`/etc/systemd/system/gateway.service`:
```ini
[Unit]
Description=Smart IV Gateway (MQTT to HIS Server bridge)
After=network-online.target zigbee2mqtt.service
Wants=network-online.target
Requires=zigbee2mqtt.service

[Service]
Type=simple
User=iotchallenge
WorkingDirectory=/home/iotchallenge
ExecStart=/home/iotchallenge/gateway_test localhost 1883 zigbee2mqtt/SmartIV-Sensor 192.168.2.10 5000 "BED-101" "ICU-1"
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Lệnh quản lý thường dùng:
```bash
sudo systemctl daemon-reload            # sau khi sửa file .service
sudo systemctl restart gateway
sudo systemctl status gateway
journalctl -u gateway -f                # xem log realtime
journalctl -u zigbee2mqtt -f
```

`192.168.2.10` là IP LAN của máy đang chạy HIS Server — đổi giá trị này
trong `ExecStart` (rồi `daemon-reload` + `restart`) nếu HIS Server chuyển máy
khác hoặc đổi IP.

---

## 6. HIS Server — app cuối cùng (ASP.NET Core)

Thư mục: `designWebForQuanKa-main/src/HisServer`. Đây là app **được viết lại
hoàn toàn** (thay thế bản WinForms cũ) — ASP.NET Core Minimal API, một tiến
trình duy nhất vừa nhận dữ liệu, vừa lưu DB, vừa phục vụ web UI, vừa đẩy cập
nhật realtime.

### 6.1 Công nghệ dùng

| Thành phần | Công nghệ | Vai trò |
|---|---|---|
| Web server | Kestrel (ASP.NET Core) | Phục vụ REST API + file tĩnh (HTML/CSS/JS) |
| Nhận dữ liệu thiết bị | `TcpListener` thô (background service) | Nghe cổng 5000, đọc từng dòng JSON |
| Database | MySQL (chạy trong Docker, container `his-mysql`) | Lưu trạng thái hiện tại + lịch sử |
| Truy vấn DB | Dapper + MySqlConnector | Micro-ORM, viết SQL tay, nhẹ hơn Entity Framework |
| Đẩy realtime cho trình duyệt | SignalR (`/hubs/monitoring`) | Khi có dữ liệu mới, đẩy thẳng xuống UI, không cần trình duyệt tự polling |
| Web UI | HTML/CSS/JS thuần, không framework/build step | `wwwroot/`, load trực tiếp, không cần `npm run build` |
| Push mobile (tuỳ chọn) | Firebase Cloud Messaging (`FcmPushService`) | Báo động qua app di động nếu cấu hình `Firebase:ProjectId` |

### 6.2 Nhận dữ liệu — TCP ingestion (`Ingestion/BedTcpIngestionService.cs`)

Background service mở `TcpListener` ở cổng cấu hình trong `appsettings.json`
(`Tcp:Port`, mặc định 5000). Mỗi kết nối gateway được xử lý độc lập
(`HandleClientAsync`), đọc từng dòng (`ReadLineAsync`, JSON kết thúc `\n`),
parse bằng `BedDataParser.Parse()`.

`BedDataParser` cố ý **alias-tolerant** (chấp nhận nhiều tên field khác nhau
cho cùng 1 ý nghĩa, ví dụ `bedId`/`bed_id`/`maGiuong` đều ra `BedId`) — để
tương thích ngược với các bản gateway/firmware cũ hơn mà không cần đồng bộ
release 2 bên cùng lúc. Các trường tín hiệu/cảnh báo mới
(`lineBlocked`/`aeAlarm`) mặc định `false` nếu gateway cũ chưa gửi field này.

Với mỗi dòng nhận được (`ProcessReadingAsync`):
1. Tính `status` mới qua `VitalsStatusEvaluator.Evaluate()` (mục 6.3).
2. Cập nhật `BedStateStore` (dictionary trong bộ nhớ, nguồn "sự thật" cho mọi
   API đọc — nhanh, không phải hỏi DB mỗi request).
3. Đẩy `BedUpdated` qua SignalR cho mọi trình duyệt đang mở.
4. Ghi đè bảng `beds` (luôn cập nhật — đây là bảng "trạng thái hiện tại").
5. Ghi thêm 1 dòng vào `vital_samples` (lịch sử) — nhưng **có giới hạn tần
   suất** (`VitalsPersistenceCoordinator`, mặc định 10 giây/lần) để tránh
   bảng lịch sử phình to vô ích khi thiết bị gửi report mỗi giây.
6. Nếu `AlertTransitionTracker.ShouldRaiseAlert()` nói "có" (xem mục 6.3) →
   ghi 1 dòng vào bảng `alerts`, đẩy `AlertCreated` qua SignalR, gửi FCM push
   nếu có cấu hình.

### 6.3 Trung tâm quyết định trạng thái — `Domain/VitalsStatusEvaluator.cs`

Đây là **nơi DUY NHẤT** trong toàn hệ thống quyết định 1 giường đang
Stable/Warning/Critical — không nơi nào khác (kể cả gateway) được tự suy luận
lại, để tránh 2 nơi tính ra kết quả khác nhau.

```
Critical  nếu  SpO2 < 90%   HOẶC   lineBlocked = true (dây tắc/free-flow)
Warning   nếu  SpO2 < 95%   HOẶC   HR < 60 hoặc > 110   HOẶC
               aeAlarm = true (AI phát hiện bất thường)   HOẶC
               bất kỳ 1 trong 4 kênh (HR/SpO2/Flow/Drip) đang mất tín hiệu
Stable    còn lại
```

Nguyên tắc quan trọng: **"chưa có dữ liệu" không phải là "ổn định"**. Một
giường có cảm biến bị rút/hỏng phải hiện Warning để y tá biết cần đi kiểm
tra/lắp lại — không được lặng lẽ hiện xanh như thể mọi thứ bình thường. Đây
chính là điểm sửa gần đây nhất (trước đó bug: mất tín hiệu HR/SpO2/Flow không
hề ảnh hưởng tới status, nên giường vẫn hiện Stable dù 3/4 cảm biến chưa lắp).

`DescribeAlert()` sinh nội dung + mã loại cảnh báo theo đúng thứ tự ưu tiên ở
trên (nghiêm trọng nhất trước), ví dụ:
- `SPO2_LOW_CRITICAL` — "Critically low SpO2: 84%"
- `LINE_BLOCKED` — "IV line blocked or free-flowing — check tubing immediately"
- `SENSOR_DISCONNECTED` — "Sensor(s) disconnected: HR, SpO2, Flow"
- `AE_ALARM` — "AI model flagged an abnormal drip pattern"

`Domain/AlertTransitionTracker.cs` chỉ tạo bản ghi `alerts` mới khi trạng thái
**chuyển** vào Warning/Critical (so với lần đọc trước) — không tạo 1 dòng mới
mỗi giây trong khi vẫn đang ở trạng thái xấu đó (tránh spam bảng alerts + push
FCM liên tục).

### 6.4 Database (MySQL, schema tại `database/schema.sql`)

| Bảng | Vai trò |
|---|---|
| `beds` | Trạng thái **hiện tại** của mỗi giường (1 dòng/giường) — nguồn phục hồi khi HIS Server restart |
| `devices` | Danh sách thiết bị vật lý (gateway, chip XG26, BLE) và giường được gán |
| `vital_samples` | Lịch sử theo thời gian (time-series) các chỉ số, ghi có giới hạn tần suất |
| `alerts` | Lịch sử cảnh báo (chỉ ghi lúc chuyển trạng thái), có thể "acknowledge" (xác nhận đã xem) |
| `fcm_tokens` | Token thiết bị di động để đẩy push notification |

Cột đáng chú ý trong `beds` (đã mở rộng dần theo tính năng): `spo2`,
`heart_rate`, `drip_rate`, `flow_rate`, `heart_rate_signal`, `spo2_signal`,
`flow_signal`, `drip_rate_signal`, `line_blocked`, `ae_alarm`. Cột
`temperature` vẫn còn trong schema (lịch sử) nhưng **không có cảm biến thật**
nên không dùng để tính trạng thái hay hiển thị nữa (luôn = 0).

MySQL chạy trong Docker: `docker exec -i his-mysql mysql -uroot -pdevpass
his_server`. MySQL 8.0 không hỗ trợ `ADD COLUMN IF NOT EXISTS` (chỉ MariaDB
có) — khi cần thêm cột mới, dùng `ALTER TABLE ... ADD COLUMN` trần và tự kiểm
tra cột chưa tồn tại trước khi chạy.

### 6.5 REST API + SignalR

| Method | Path | Việc gì |
|---|---|---|
| GET | `/api/beds` | Danh sách toàn bộ giường + trạng thái hiện tại |
| POST/PUT | `/api/beds`, `/api/beds/{id}` | Thêm/sửa giường (thủ công, cho demo/quản trị) |
| GET | `/api/alerts` | Lịch sử cảnh báo, lọc theo mức độ/đã ack |
| POST | `/api/alerts/{id}/ack` | Xác nhận đã xem cảnh báo |
| GET/POST/PUT/DELETE | `/api/devices` | Quản lý thiết bị |
| GET | `/api/logs`, `/api/logs/export` | System Log (hợp nhất Alert + Vital), export CSV |
| — | `/hubs/monitoring` | SignalR hub — trình duyệt subscribe để nhận `BedUpdated`/`AlertCreated` realtime |

Khi HIS Server khởi động lại, nó nạp lại toàn bộ bảng `beds` vào bộ nhớ
(`Program.cs`) để tránh dashboard hiện trống trong lúc gateway đang gửi lại
dữ liệu mới — nạp đầy đủ mọi cột (kể cả các cờ tín hiệu/cảnh báo), không chỉ
riêng vitals cơ bản.

### 6.6 Web UI (`wwwroot/`) — 5 tab

Không dùng React/Vue hay bundler — HTML/CSS/JS thuần, mỗi tab 1 file JS riêng
(`dashboard.js`, `beds.js`, `alerts.js`, `devices.js`, `system-log.js`),
`main.js` chỉ lo chuyển tab. `state.js` giữ dữ liệu dùng chung (danh sách
giường hiện tại), `signalr-client.js` lắng nghe sự kiện realtime và cập nhật
`state.js`, các tab tự vẽ lại khi `state.js` báo thay đổi.

| Tab | Nội dung |
|---|---|
| **Dashboard** | Tổng quan: số giường theo trạng thái, lưới thẻ giường, cảnh báo mới nhất |
| **Beds & Rooms** | Danh sách đầy đủ, lọc theo phòng/trạng thái, panel chi tiết 1 giường, thêm giường mới |
| **Alerts** | Lịch sử cảnh báo, lọc theo mức độ/đã-xác-nhận, xác nhận (acknowledge) |
| **Devices** | Danh sách thiết bị vật lý (gateway/XG26/BLE), trạng thái pin/tín hiệu |
| **System Log** | Hợp nhất log Alert + Vital theo thời gian, lọc theo ngày/loại, export CSV |

Mỗi thẻ giường hiện 1 hàng "chấm tín hiệu" (`signalRowHtml` trong
`ui-utils.js`): HR / SpO2 / Flow / Drip / Line — xanh = ổn, xám = mất tín
hiệu hoặc (với Line) đang tắc/free-flow. Đây là cách y tá nhận biết vấn đề
thiết bị **ngay trên lưới giường**, không cần mở tab Alerts.

---

## 7. Đường đi 1 gói dữ liệu — ví dụ cụ thể từ đầu tới cuối

1. Board cảm biến đo được: HR chưa lắp, SpO2 chưa lắp, Flow chưa lắp, Drops
   đang chảy đúng mức nhưng dây bị tắc (drop_ratio < 0.3× mức đặt).
2. `ai_monitor_step()` → `reason_flow = 1` (flow_ratio ngoài khoảng), các
   kênh chưa lắp → trạng thái `CH_DISABLED`, không set `reason_missing`.
3. `app.c` ghi `alarm_bitmap = 0x08` (bit3, line_blocked) vào endpoint 6, ghi
   3 giá trị còn lại vào endpoint 2/3/4/5, reporting tự gửi Zigbee.
4. NCP nhận khung Zigbee, chuyển qua USB dạng EZSP.
5. zigbee2mqtt (`fromZigbee`) đọc `msg.endpoint.ID === 6`, tách bitmap thành
   `line_blocked: true`, publish MQTT topic `zigbee2mqtt/SmartIV-Sensor` với
   toàn bộ state hiện tại (mọi field expose gộp lại).
6. `gateway_test` (`on_message`) parse `line_blocked` = 1, gọi
   `his_send_bed_data(..., line_blocked=1, ...)`.
7. Gói TCP gửi tới HIS Server:
   `{"bedId":"BED-101","room":"ICU-1",...,"lineBlocked":true,...}`.
8. `BedDataParser` → `BedReading { LineBlocked = true, ... }`.
9. `VitalsStatusEvaluator.Evaluate()` → `LineBlocked == true` → **Critical**.
10. `DescribeAlert()` → `("LINE_BLOCKED", "IV line blocked or free-flowing — check tubing immediately")`.
11. Vì trạng thái trước đó không phải Critical → `AlertTransitionTracker` nói
    có → ghi bảng `alerts`, đẩy SignalR `AlertCreated`.
12. Trình duyệt đang mở nhận sự kiện, `alerts.js` gọi lại API, danh sách cập
    nhật ngay không cần F5; thẻ giường trên Dashboard/Beds & Rooms chuyển đỏ,
    chấm "Line" chuyển xám.

---

## 8. Mô hình nhiều phòng / nhiều giường

Thiết kế đích: **mỗi phòng có 1 gateway riêng** (1 NCP + 1 tiến trình
`gateway_test` cấu hình `bed_id`/`room` tương ứng), **mỗi phòng có nhiều
giường**, mỗi giường có 1 chip XG26 làm end-device gửi Zigbee tới gateway của
đúng phòng đó. HIS Server không quan tâm dữ liệu đến từ gateway nào — chỉ
đọc `bedId`/`room` trong JSON để biết gán vào đâu.

Dữ liệu demo hiện tại (`database/seed_demo.sql`) tạo sẵn 3 phòng
(**ICU-1**, **ICU-2**, **Ward-A**), mỗi phòng có 1 gateway registry
(`GW-ICU1`/`GW-ICU2`/`GW-WARDA`) và vài giường (`BED-101..103`,
`BED-201..203`, `BED-301..302`), tất cả để `OFFLINE` — trừ `BED-101` đang có
dữ liệu thật từ board cảm biến vật lý duy nhất hiện có.

---

## 9. Bảng lỗi thường gặp (tham khảo khi tái lập trên máy khác)

| Triệu chứng | Nguyên nhân | Cách sửa |
|---|---|---|
| `network-steering start` → `SL_STATUS_NO_BEACONS` | Coordinator chưa mở permit-join, hoặc cửa sổ permit-join (254s) đã hết hạn | Bật permit-join qua MQTT trước, thử lại ngay (đừng để trễ quá ~4 phút) |
| Join xong nhưng z2m luôn "0 devices joined" | Thiết bị rejoin từ NVM3 cũ (mạng cũ đã bị z2m reform, không còn khớp) | `network leave` rồi `plugin network-steering start 0` để join lại từ đầu |
| `Bind ... failed (Status 'TABLE_FULL')` | `SL_ZIGBEE_BINDING_TABLE_SIZE` trên board cảm biến quá nhỏ (mặc định 3, cần ≥5) | Tăng lên 10 trong `config/sl_zigbee_pro_stack_config.h`, build & flash lại |
| Model/manufacturer "undefined" khi interview | Basic cluster đọc modelID không ổn định | Dùng converter theo `fingerprint` endpoint/cluster thay vì `zigbeeModel` |
| `configureReporting(...)` timed out, các endpoint sau không được bind | Vòng lặp `configure()` bị throw giữa chừng khi 1 lệnh ZCL timeout | Bỏ `configureReporting()` trong converter, chỉ giữ `reporting.bind()`, bọc try/catch từng endpoint |
| `alarm` trong MQTT payload luôn `true` dù không có cảnh báo gì | Tính `alarm` bằng `bitmap !== 0` — dính luôn bit tín hiệu (5-8) | Mask đúng `bitmap & 0x1F` (chỉ 5 bit lý do cảnh báo) |
| mosquitto "Not authorized" trên Pi | `allow_anonymous false` nằm SAU `include_dir` trong `mosquitto.conf` (directive sau đè directive trước) | Sửa trực tiếp `allow_anonymous true` trong file conf chính |
| Gateway log "Client ... already connected, closing old connection" liên tục | 2 tiến trình `gateway_test` (1 chạy tay + 1 chạy qua systemd) cùng dùng 1 MQTT client ID | `pkill` tiến trình chạy tay thừa, chỉ để lại đúng 1 instance (do systemd quản lý) |
| Giường hiện Stable rồi nhảy Offline liên tục | `Offline.ThresholdSeconds` quá thấp (3s) trong khi thiết bị chỉ report tối đa 60s/lần khi rảnh | Tăng `Offline.ThresholdSeconds` lên 90 trong `appsettings.json` |
| Giường luôn hiện Stable dù cảm biến chưa lắp | `VitalsStatusEvaluator` (bản cũ) không xét cờ tín hiệu, chỉ xét SpO2/HR | Đưa việc mất tín hiệu bất kỳ kênh nào vào `Evaluate()` → Warning |
| Sau khi restart HIS Server, cờ tín hiệu/flow_rate của giường về mặc định dù DB có dữ liệu đúng | `Program.cs` (đoạn nạp lại state lúc khởi động) quên copy các trường mở rộng (`FlowRate`, 4 cờ tín hiệu, `LineBlocked`, `AeAlarm`) | Bổ sung đủ field khi copy từ `BedRepository.LoadAllAsync()` vào `BedStateStore` |
| MySQL `ADD COLUMN IF NOT EXISTS` báo lỗi cú pháp | MySQL 8.0 không hỗ trợ cú pháp này (chỉ MariaDB) | Dùng `ALTER TABLE ... ADD COLUMN` trần, tự kiểm tra cột đã tồn tại chưa trước khi chạy |
| zigbee2mqtt: "permit_join setting removed" khi migrate config | Từ bản 2.x, permit_join chỉ điều khiển qua MQTT/frontend, không còn là key YAML | Dùng lệnh `mosquitto_pub` ở mục 3.4 |
| Converter không load ("Loaded external converter" không xuất hiện trong log) | Để file converter sai chỗ (`data/` thay vì `data/external_converters/`) | Di chuyển đúng thư mục |

---

## 10. Tổng kết đường dẫn file quan trọng

```
empty_2/
├── sensor_hub.{c,h}          firmware: đọc cảm biến, trạng thái từng kênh
├── ai_monitor.{c,h}          firmware: AI + luật lâm sàng
├── app.c                     firmware: vòng lặp chính, ghi Zigbee
├── config/zcl/zcl_config.zap cấu hình ZAP: 6 endpoint, cluster mượn
├── zigbee2mqtt_smart_iv_converter.js   converter giải mã cho zigbee2mqtt
├── gateway/main.c            chương trình C: MQTT → TCP JSON
└── designWebForQuanKa-main/
    ├── database/schema.sql, seed_demo.sql
    └── src/HisServer/
        ├── Ingestion/BedDataParser.cs, BedTcpIngestionService.cs
        ├── Domain/VitalsStatusEvaluator.cs, AlertTransitionTracker.cs
        ├── Data/*.Repository.cs
        ├── Api/*Endpoints.cs
        ├── Program.cs
        └── wwwroot/          web UI (index.html, css/, js/)
```

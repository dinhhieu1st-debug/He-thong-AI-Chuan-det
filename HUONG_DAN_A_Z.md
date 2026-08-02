# Smart IV Monitoring System — Tài liệu A-Z (chip → app)

Tài liệu này giải thích **toàn bộ hệ thống**, từ con chip cảm biến gắn trên
giường bệnh nhân cho tới màn hình web mà y tá nhìn thấy. Viết cho người **chưa
biết gì về hệ thống này** — đọc từ đầu tới cuối là hiểu được toàn bộ luồng dữ
liệu, vì sao lại thiết kế như vậy, và sờ vào đâu nếu cần sửa/mở rộng.

> Đây là bản rewrite hoàn toàn của tài liệu cũ. Bản cũ mô tả app WinForms
> (`Server_FPT_upload` / `his-server/Server`) — app đó **không còn
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
  lập. Đánh số 1-255. Trong project này, board cảm biến có **2 endpoint**:
  endpoint 1 (Basic, định danh thiết bị) và endpoint 2 (cluster tuỳ chỉnh
  "Smart IV Vitals", mang cả 5 giá trị AI — HR/SpO2/Flow/Drop/Alarm — dưới
  dạng 5 attribute riêng biệt trong CÙNG 1 cluster, xem mục 1.3).
- **Cluster**: trong 1 endpoint, dữ liệu được nhóm theo **cluster** — 1 cluster
  là 1 "bộ chức năng chuẩn hoá" được đặt tên và đánh mã số sẵn trong chuẩn ZCL,
  ví dụ cluster "Temperature Measurement" (mã `0x0402`) định nghĩa sẵn: có 1
  attribute tên `MeasuredValue` kiểu số nguyên 16-bit có dấu, đơn vị 1/100 °C.
  Bất kỳ thiết bị Zigbee nào trên thế giới dùng đúng cluster này đều hiểu được
  nhau — đó là lý do Zigbee có tính "chuẩn hoá, cắm là chạy" giữa các hãng.
- **Attribute**: từng "ô dữ liệu" cụ thể bên trong cluster, ví dụ
  `MeasuredValue`. Đọc/ghi attribute là đơn vị thao tác nhỏ nhất khi giao tiếp
  Zigbee (đọc 1 attribute, ghi 1 attribute, hoặc "report" khi nó đổi giá trị).

### 0.5.3 Custom cluster — vì sao dự án từng "mượn" cluster có sẵn, và vì sao giờ đã bỏ

> **Cập nhật:** dự án **ĐÃ chuyển sang dùng custom cluster thật** (ngày
> 2026-07-18). Phần dưới đây giữ lại để giải thích LỊCH SỬ quyết định ban đầu
> — thiết kế hiện tại đã khác, xem mục 1.3 và 3.2 cho kiến trúc THẬT đang chạy.

Về lý thuyết, cách làm đúng chuẩn nhất là định nghĩa **cluster tuỳ chỉnh**
(custom cluster) — tự đặt mã cluster riêng (dải mã `0xFC00-0xFFFF` dành cho
mục đích này), tự định nghĩa attribute tên `HeartRate`, `Spo2`... với đúng ý
nghĩa ngữ nghĩa. Đây là cách "sạch" về mặt thiết kế: bên đọc dữ liệu (z2m)
không cần biết quy ước ngầm gì cả, cứ đọc đúng tên attribute là ra đúng nghĩa.

Ban đầu, làm custom cluster bị coi là rủi ro cao trong thời gian ngắn của dự
án (đòi hỏi sửa sâu file ZAP, sửa cả 2 phía firmware + converter), nên giải
pháp thực dụng lúc đó là **"mượn"** 5 cluster đo lường có sẵn trong SDK
(Temperature/Humidity/Flow/Pressure/Illuminance Measurement), gán ý nghĩa
khác cho `MeasuredValue` của chúng. Đánh đổi: mất tính "tự giải thích" của dữ
liệu, bù lại giảm rủi ro kỹ thuật.

Sau khi hệ thống đã chạy ổn định (HR/SpO2/Flow đọc được dữ liệu thật, pipeline
đầu-cuối đã kiểm chứng), dự án đã nâng cấp lên **custom cluster thật**: 1
cluster `Smart IV Vitals` (mã `0xFC01`, dải mfg-specific `0xFC00-0xFFFF`,
manufacturer code `0x1049`) với 5 attribute đặt tên đúng nghĩa (`HeartRate`,
`Spo2`, `FlowRatio`, `DropRatio`, `AlarmBitmap`). Cách làm cụ thể (xác nhận đã
chạy được, không phải lý thuyết):

1. Viết 1 file XML riêng định nghĩa cluster mới
   (`config/zcl/smart-iv-vitals.xml`), theo đúng khuôn mẫu Silicon Labs cung
   cấp sẵn trong SDK (`zigbee/app/zcl/sample-extensions.xml`,
   `zigbee/app/zcl/silabs.xml` — 2 file này CHÍNH LÀ tài liệu/ví dụ chính thức
   cho việc này, không cần đoán mò cấu trúc).
2. Khai báo file XML đó như 1 package kiểu `"zcl-xml-standalone"` trong
   `config/zcl/zcl_config.zap` (mảng `"package"`) — ZAP sẽ tự đọc và hiểu
   cluster mới mà KHÔNG cần sửa file `zcl-zap.json` gốc của SDK (dùng chung,
   không nên đụng vào).
3. Chạy lại `slc generate` (mục 1.6) — sinh ra đúng
   `ZCL_SMART_IV_VITALS_CLUSTER_ID`, `ZCL_HEART_RATE_ATTRIBUTE_ID`, v.v. trong
   `autogen/zap-id.h`.
4. Bên firmware (`app.c`), vì cluster có `mfgCode` nên phải dùng hàm ghi
   attribute riêng cho mfg-specific:
   `sl_zigbee_af_write_manufacturer_specific_server_attribute(...)` (KHÔNG
   dùng `sl_zigbee_af_write_attribute()` thường như với cluster chuẩn) —
   tương tự, `zb_configure_reporting()` phải set
   `reportingEntry.manufacturerCode` đúng mã `0x1049`.
5. Bên zigbee2mqtt (`zigbee2mqtt_smart_iv_converter.js`), dùng helper
   `deviceAddCustomCluster(name, clusterDefinition)` từ
   `zigbee-herdsman-converters/lib/modernExtend` — đây chính là cơ chế "tự
   khai báo cluster lạ" mà đoạn văn cũ (bản trước) coi là rủi ro; thực tế đã
   có sẵn helper chính thức, không cần tự viết code parse ZCL thô. **Lưu ý
   struct bắt buộc** (xác nhận qua các converter thật đã có sẵn trong
   `zigbee-herdsman-converters/dist/devices/*.js`, ví dụ `sber.js`,
   `lytko.js`): mỗi attribute cần đủ `{name, ID, type}` (thiếu `name` sẽ lỗi
   dù có `ID`/`type` đúng), và cluster-level `manufacturerCode` là field hợp
   lệ theo type `Cluster` của `zigbee-herdsman`
   (`zspec/zcl/definition/tstype.d.ts`).

Kết quả: bên nhận (z2m, hoặc bất kỳ công cụ Zigbee chuẩn nào đọc gói tin) giờ
thấy đúng tên attribute `HeartRate`/`Spo2`/... — không còn hiểu nhầm thành
nhiệt độ/độ ẩm như trước. Toàn bộ field JSON expose ra ngoài
(`heart_rate`, `spo2`, `flow`, ...) **giữ nguyên tên** như bản cũ, nên
`gateway/main.c` và HIS Server không cần sửa gì.

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
#define HR_ENABLED     1   // MAX30102 - nhịp tim        → ĐÃ LẮP, đã test đọc được số thật
#define SPO2_ENABLED   1   // MAX30102 - SpO2             → ĐÃ LẮP (chung chip với HR)
#define FLOW_ENABLED   1   // HX711 + loadcell            → ĐÃ LẮP, đã hiệu chuẩn (mục 1.3.1)
#define DROPS_ENABLED  0   // cảm biến giọt               → RÚT RA tạm thời để test HR/SpO2/Flow riêng
```

Trạng thái hiện tại (2026-07-18): HR/SpO2 (module DFRobot MAX30102, bit-bang
I2C) và Flow (loadcell + HX711, bit-bang GPIO) đều đã lắp và đọc được dữ liệu
thật ổn định. `DROPS_ENABLED` đang tắt tạm (rút cảm biến giọt ra để test 2 kênh
kia riêng, xem `sensor_hub.h` dòng comment) — bật lại `1` khi cắm lại cảm biến
giọt, không cần sửa gì khác trong code, kiến trúc đã tính sẵn cho việc bật/tắt
từng kênh độc lập.

**Bài học từ quá trình debug 2 kênh HR và Flow** (rất đáng đọc nếu định thêm
cảm biến mới bit-bang GPIO tương tự — xem mục 9 để biết chi tiết từng lỗi):
timing của vòng lặp bit-bang (I2C cho MAX30102, giao thức 2 dây cho HX711)
**PHẢI dùng `sl_udelay_wait()`** (delay canh theo đồng hồ CPU thật) thay vì
vòng lặp rỗng đếm số lần lặp — dưới build tối ưu `-Os`, vòng lặp rỗng chạy
nhanh hơn dự định hàng chục đến hàng trăm lần, khiến giao thức bit-bang đọc
sai hoặc đọc "đứng hình" 1 giá trị cố định. Ngoài ra, đọc HX711 phải bọc trong
`CORE_ENTER_ATOMIC()`/`CORE_EXIT_ATOMIC()` (tắt ngắt tạm thời) để tránh
Zigbee radio stack chen ngang giữa chừng làm hỏng dữ liệu.

**Chân HX711 thực tế trên board này** (`sensor_hub.c`): `DOUT` nối **PC01**,
`SCK` nối **PC03**. Lưu ý: tài liệu tham khảo chính thức của SDK cho mikroBUS
SPI trên BRD2709A (`sl_spidrv_usart_mikroe_config.h`) ghi MISO=PC02 — nhưng
qua debug thực tế (đọc trực tiếp giá trị GPIO khi có/không tải), **PC01 mới
là chân cho dữ liệu ổn định thật**, không phải PC02. Nếu đổi board/module
khác, đừng tin ngay tài liệu SDK, hãy in giá trị đọc thô (raw) ra serial và
so với việc treo/gỡ vật nặng thật để xác nhận trước.

**Hiệu chuẩn** (`HX711_CALIBRATION_FACTOR` trong `sensor_hub.h`): giá trị hiện
tại `202534.0f`, đo thực tế bằng cách treo túi ~500ml nước (~500g) và so raw
lúc cân trống với lúc có tải, TRONG CÙNG 1 PHIÊN chạy (đo tare và tải ở 2 lần
nạp firmware khác nhau sẽ bị lệch baseline do trôi nhiễu, gây hệ số sai — đã
gặp thực tế). Giá trị `14000` cũ chỉ là số mượn tạm từ `hx711_test.ino`
(loadcell/hiệu chuẩn khác), không đúng cho loadcell hiện tại.

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

### 1.3 Đưa kết quả lên Zigbee — cluster tuỳ chỉnh "Smart IV Vitals"

Đây là phần hay bị hỏi nhất nên giải thích kỹ. Zigbee không có sẵn khái niệm
"gửi 1 cục JSON tuỳ ý" như MQTT/HTTP — nó bắt buộc dữ liệu phải thuộc về một
**cluster** (nhóm chức năng chuẩn hoá, ví dụ "đo nhiệt độ", "đo độ ẩm"...),
và mỗi cluster có các **attribute** (thuộc tính) với kiểu dữ liệu cố định.

Dự án dùng **1 cluster tuỳ chỉnh thật** (custom cluster, xem lịch sử quyết
định + cách làm cụ thể ở mục 0.5.3) tên `Smart IV Vitals`, mã `0xFC01` (nằm
trong dải mfg-specific `0xFC00-0xFFFF`), `manufacturerCode` = `0x1049`, định
nghĩa tại `config/zcl/smart-iv-vitals.xml` — KHÔNG còn "mượn" ý nghĩa cluster
chuẩn nào nữa. Chỉ có **2 endpoint**:

| Endpoint | Cluster | Attribute | Kiểu ZCL | Ý nghĩa |
|---|---|---|---|---|
| 1 | Basic (`0x0000`) | `manufacturerName`="ICTU", `modelIdentifier`="SmartIV-Sensor" | string | Định danh thiết bị, để zigbee2mqtt nhận diện |
| 2 | Smart IV Vitals (`0xFC01`, mfg `0x1049`) | `HeartRate` | int16s | Nhịp tim (bpm). `0x8000` = chưa có dữ liệu thật (xem `ZCL_HR_INVALID` trong `app.c`) |
| 2 | (cùng cluster) | `Spo2` | int16u | % bão hoà oxy. `0xFFFF` = chưa có dữ liệu thật |
| 2 | (cùng cluster) | `FlowRatio` | int16u | Flow ratio × 100 (100 = đúng 1.00× mức đặt) |
| 2 | (cùng cluster) | `DropRatio` | int16s | Drop ratio × 100 |
| 2 | (cùng cluster) | `AlarmBitmap` | bitmap16 | Bitmap cảnh báo + trạng thái tín hiệu (chi tiết bên dưới) |

Vì cluster có `mfgCode`, ghi attribute phải dùng hàm riêng
`sl_zigbee_af_write_manufacturer_specific_server_attribute()` (không phải
`sl_zigbee_af_write_attribute()` thường) — xem `zb_report_ai_result()` trong
`app.c`.

**Attribute `AlarmBitmap` — bitmap cảnh báo (uint16, 9 bit dùng, đọc kỹ phần
này để hiểu toàn bộ hệ thống cảnh báo)**:

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
ký cho toàn bộ 13 attribute của cluster `Smart IV Vitals` (cùng 1 endpoint +
cluster, khác attributeId). Vì cluster có `mfgCode`, mỗi entry reporting phải
set đúng `reportingEntry.manufacturerCode = 0x1049` (khác
`SL_ZIGBEE_AF_NULL_MANUFACTURER_CODE` dùng cho cluster chuẩn). Vì cấu hình này
nằm **trong chính firmware**, hệ thống không phụ thuộc việc zigbee2mqtt phải
gọi đúng lệnh ZCL `ConfigureReporting`.

Mỗi giây, `app_process_action()` gọi `zb_report_ai_result()` ghi các giá trị
vào attribute — reporting plugin tự phát hiện thay đổi và gửi gói Zigbee đi.

**Nhịp báo cáo được phân loại theo vai trò, KHÔNG dùng chung một mức.** Bản
đầu đặt phẳng `minInterval=1s` + `reportableChange=1` cho cả 13 attribute, hậu
quả là thiết bị phát gần như liên tục: load cell rung ~1g ngay cả khi tải đứng
yên nên riêng `WeightG` đã báo lại mỗi giây, và coordinator còn trả một Default
Response cho *từng* khung. Đo thực tế: **~120 bản tin/phút**. Đó là airtime
2.4GHz dùng chung với WiFi, làm tăng va chạm khung và sẽ tệ hơn nhiều khi
nhiều giường dùng chung một coordinator.

Cách sửa **không phải** là giãn đều tất cả — làm vậy sẽ trễ cảnh báo, không
chấp nhận được với thiết bị y tế. Thay vào đó (xem bảng `reportCfgs[]` trong
`zb_configure_reporting()`):

| Nhóm | `minInterval` | `reportableChange` | Lý do |
|---|---|---|---|
| `AlarmBitmap` | 1s | 1 | Cảnh báo phải đi ngay tick đầu tiên, tuyệt đối không bóp |
| `HeartRate`, `Spo2` | 2s | 1 | Đúng bằng nhịp cửa sổ MAX30102 tự publish |
| `FlowRatio`, `DropRatio` | 5s | 5 (đơn vị 1%) | Vùng chết 5 điểm % — ngưỡng báo động là 30%/150% nên thừa mịn |
| `WeightG` | 10s | 5 (gram) | 5g nằm trên mức nhiễu ~1g của cân → tải đứng yên thì ngừng báo |
| `DropsPerMin` | 5s | 1 | 1 giọt/phút vẫn có ý nghĩa lâm sàng |
| Ngưỡng đặt + bộ đếm sự kiện | 1s | 1 | Chỉ đổi khi bác sĩ thao tác → phản hồi tức thì, lúc rảnh không tốn gì |

Kết quả sau khi chỉnh: **12 bản tin/phút** (giảm 10 lần) mà độ trễ cảnh báo
không đổi.

`maxInterval` giữ **60s** cho mọi attribute và **không được vượt 90s**, vì HIS
Server đánh dấu giường `Offline` sau `Offline.ThresholdSeconds = 90`
(`appsettings.json`) nếu không nhận được cập nhật nào — để dài hơn thì giường
khoẻ mạnh vẫn bị hiện Offline.

### 1.4.1 Bác sĩ chỉnh ngưỡng từ xa — KHÔNG cần sửa code, không cần nạp lại chip

Đây là hiểu lầm rất dễ mắc: `SET_FLOW_ML_H` / `SET_DROPS_DPM` trong
`sensor_hub.h` **chỉ là giá trị mặc định lúc khởi động**, không phải hằng số
quyết định hành vi. `sh_flow_ratio()` / `sh_drops_ratio()` luôn chia cho biến
`target_flow_ml_h` / `target_drops_per_min` hiện hành, nên ghi giá trị mới
xuống là AI đổi cách đánh giá ngay lập tức.

Chuỗi đầy đủ khi bác sĩ nhập số trên tab "Beds and Rooms" rồi bấm nút:

```
UI (wwwroot/js/beds.js)
  → PUT /api/beds/{bedId}/target-drops        (Api/BedEndpoints.cs)
  → TCP {"cmd":"set_target_drops_per_min","value":50}   (cùng socket nhận vitals)
  → gateway_test check_his_commands()          (gateway/main.c)
  → MQTT zigbee2mqtt/SmartIV-Sensor/set  {"target_drops_per_min":50}
  → converter toZigbee                         (zigbee2mqtt_smart_iv_converter.js)
  → ZCL Write Attributes  TargetDropsPerMin=50  (mfgCode 0x1049, endpoint 2)
  → sl_zigbee_af_post_attribute_change_cb()    (app.c)
  → sh_set_target_drops_per_min(50)            (sensor_hub.c)
```

Cùng cơ chế cho `target_flow_ml_h`, tare cân từ xa (`TareCommand`) và hiệu
chuẩn lại baseline nhịp tim (`HrRecalibrateCommand`).

**Tuyệt đối không** làm kiểu "app tự sửa code rồi build + nạp chip": mỗi lần
nạp là chip reset, mất sạch trạng thái đang theo dõi (baseline HR, tare cân,
kết nối Zigbee) trong hàng chục giây — không thể chấp nhận khi máy đang truyền
dịch cho bệnh nhân.

**Ngưỡng được lưu vào NVM3 nên sống sót qua mất điện.** Nếu không lưu, một cú
chớp điện sẽ âm thầm đưa cả hai ngưỡng về mặc định trong khi ca truyền vẫn
tiếp diễn — AI sẽ đánh giá theo một y lệnh không ai kê, còn dashboard hiển thị
ngưỡng bác sĩ chưa từng đặt. `targets_save()` / `targets_load()` trong
`sensor_hub.c` dùng khoá NVM3 `0x0A001` (vùng "user" `0x00000-0x0FFFF` theo
`sl_token_manager_defines.h`; stack Zigbee nằm từ `0x10000` trở lên nên không
đụng nhau). Hai ngưỡng nằm chung **một** record để không bao giờ bị cập nhật
dở dang khi mất điện giữa chừng, có `magic` chống đọc nhầm record lạ và có
kiểm tra miền giá trị chống record hỏng đẩy số vô lý vào phép chia của AI.
Hao mòn flash không đáng lo: chỉ ghi khi giá trị **thực sự đổi** (setter thoát
sớm nếu trùng), không ghi trên đường báo cáo định kỳ.

### 1.5 Yêu cầu cấu hình quan trọng

- **Component cần cài** (Simplicity Studio → Software Components, hoặc sửa
  `.slcp`): `zigbee_basic`, `zigbee_core_cli`, `zigbee_network_steering`,
  `zigbee_pro_stack`, `zigbee_reporting`, `zigbee_zcl_cli`.
- **`SL_ZIGBEE_BINDING_TABLE_SIZE`** trong
  `config/sl_zigbee_pro_stack_config.h`: đã tăng lên 10 từ thời còn 5 endpoint
  cluster mượn (mỗi endpoint 1 binding). Từ khi gộp về 1 cluster tuỳ chỉnh
  (mục 1.3), chỉ cần **1 binding** cho cluster `Smart IV Vitals` — giá trị 10
  vẫn giữ nguyên vì không hại gì (dư chỗ), không bắt buộc phải giảm lại.
- **`profileId` của endpoint PHẢI là `260`** (0x0104 Home Automation) trong
  `config/zcl/zcl_config.zap`. ZAP mặc định đặt `65535` (0xFFFF) khi tạo
  endpoint kiểu "Custom ZCL Device Type", và giá trị đó làm **mọi lệnh ZCL gửi
  xuống thiết bị bị drop im lặng** (SDK lọc profile ở
  `app/framework/util/util.c:471`) trong khi chiều báo cáo lên vẫn chạy bình
  thường — rất dễ chẩn đoán nhầm. Chỉ sửa ở cấp **endpoint**, giữ nguyên device
  type là Custom; xem dòng tương ứng trong bảng lỗi mục 9 để biết vì sao không
  được đổi `deviceTypes.profileId`.
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

### 3.2 External converter — "từ điển" giải mã cluster tuỳ chỉnh Smart IV Vitals

File: `zigbee2mqtt_smart_iv_converter.js` (repo `empty_2`), copy vào
`~/zigbee2mqtt/data/external_converters/` trên Pi.

**Nhận diện thiết bị bằng fingerprint (không dùng chuỗi `zigbeeModel`)**:
đọc `modelID` qua cluster Basic lúc "interview" (bước đầu khi thiết bị mới
join, z2m hỏi thăm dò thông tin) không ổn định — đôi khi trả về `undefined`.
Giải pháp: nhận diện bằng đúng "vân tay" cấu trúc endpoint/cluster mà 2
endpoint của firmware khai báo (`0xFC01` = `64513` decimal):

```js
fingerprint: [{
    endpoints: [
        {ID: 1, inputClusters: [0]},        // Basic
        {ID: 2, inputClusters: [0xFC01]},   // Smart IV Vitals (custom cluster)
    ],
}],
```

**Đăng ký cluster tuỳ chỉnh** — vì `0xFC01` không có trong danh sách cluster
chuẩn của `zigbee-herdsman`, phải tự khai báo bằng helper
`deviceAddCustomCluster` (từ `zigbee-herdsman-converters/lib/modernExtend`):

```js
extend: [
    deviceAddCustomCluster('smartIvVitals', {
        name: 'smartIvVitals',
        ID: 0xFC01,
        manufacturerCode: 0x1049,
        attributes: {
            heartRate:   {name: 'heartRate',   ID: 0x0000, type: 0x29}, // int16s
            spo2:        {name: 'spo2',        ID: 0x0001, type: 0x21}, // int16u
            flowRatio:   {name: 'flowRatio',   ID: 0x0002, type: 0x21}, // int16u
            dropRatio:   {name: 'dropRatio',   ID: 0x0003, type: 0x29}, // int16s
            alarmBitmap: {name: 'alarmBitmap', ID: 0x0004, type: 0x19}, // bitmap16
        },
        commands: {}, commandsResponse: {},
    }),
],
```

> Mỗi attribute **bắt buộc phải có đủ `{name, ID, type}`** — chỉ có `ID`/`type`
> mà thiếu `name` sẽ không hoạt động đúng (xác nhận qua các converter thật có
> sẵn trong `zigbee-herdsman-converters/dist/devices/*.js`, ví dụ `sber.js`).
> `type` là số ZCL DataType thô (`Zcl.DataType.INT16S` = `0x29`,
> `UINT16` = `0x21`, `BITMAP16` = `0x19`).

**Giải mã (`fromZigbee`)** — 1 hàm convert duy nhất cho cả cluster (khác bản
cũ cần 5 hàm riêng vì trước đó mỗi giá trị nằm ở 1 cluster/endpoint khác
nhau), đọc thẳng theo tên attribute (không cần phân biệt `msg.endpoint.ID` vì
giờ chỉ còn 1 endpoint mang dữ liệu):

```js
if (msg.data.heartRate !== undefined) {
    result.heart_rate = msg.data.heartRate === HR_INVALID ? null : msg.data.heartRate;
}
if (msg.data.flowRatio !== undefined) {
    result.flow = msg.data.flowRatio / 100;   // đảo ngược phép ×100 bên firmware
}
```

`HeartRate`/`Spo2` có sentinel "chưa có dữ liệu thật" (khớp `ZCL_HR_INVALID`/
`ZCL_SPO2_INVALID` trong `app.c`): `HR_INVALID = -32768` (0x8000 đọc theo
int16s), `SPO2_INVALID = 0xFFFF` — converter đổi 2 giá trị này thành `null`
trong JSON thay vì hiện số giả.

`AlarmBitmap` được tách thành **10 field JSON** riêng biệt (dễ đọc hơn nhiều
so với gửi 1 số nguyên thô), dùng đúng các hằng số bit đã định nghĩa ở
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
    try {
        const ep = device.getEndpoint(2);
        await reporting.bind(ep, coordinatorEndpoint, ['smartIvVitals']);
    } catch (error) {
        logger.warning(`bind endpoint 2 failed: ${error.message}`);
    }
},
```

"Bind" nghĩa là báo cho thiết bị biết "gửi report của cluster này tới địa chỉ
coordinator" — cần thiết để report thật sự đến được z2m (không có bind thì
thiết bị vẫn tự tính, nhưng report "gửi đi đâu" không xác định). Giờ chỉ còn
**1 bind duy nhất** (trước đây 5 bind cho 5 endpoint). **Không** gọi lệnh ZCL
`configureReporting()` ở đây (đã bị bỏ có chủ đích): lệnh này hay bị timeout
trên thực tế — vì firmware đã tự cấu hình reporting cục bộ rồi (mục 1.4), z2m
chỉ cần bind là đủ.

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

## 5.5 Gateway dự phòng khi KHÔNG có Raspberry Pi

Khi Pi hỏng (đã xảy ra: hỏng thẻ SD), toàn bộ đoạn giữa có thể thay bằng một
script chạy ngay trên máy tính:

```
board → USB-serial (VCOM) → tools/serial_gateway.py → TCP → HIS Server
```

Firmware in một dòng `[JSON]{...}` mỗi chu kỳ AI trên VCOM, tên trường **đặt
trùng payload zigbee2mqtt** nên **không phải sửa gì bên server**.

```bash
# /dev/ttyACM1 la board CAM BIEN (khong phai NCP) - kiem tra bang:
#   udevadm info -q property -n /dev/ttyACM1 | grep ID_SERIAL_SHORT
python3 tools/serial_gateway.py --port /dev/ttyACM1 \
    --server 127.0.0.1 --tcp-port 5000 --bed-id BED-101 --room ICU-1
```

**Hạn chế phải biết:** đường này **bỏ qua Zigbee hoàn toàn** — không kiểm chứng
được NCP, zigbee2mqtt hay việc ghép đôi. Dùng để xem giao diện và kiểm chứng AI +
server, **không thay thế** test thật với Pi. Ngoài ra nó **một chiều**: các lệnh
từ bác sĩ (đặt ngưỡng, tare cân, hiệu chuẩn baseline) sẽ **không tới được chip**.

**Hai cái bẫy đã gặp:**

1. **Không mở `/dev/ttyACM1` bằng công cụ khác khi gateway đang chạy** — cổng
   serial không chia sẻ được, gateway sẽ chết với lỗi *"multiple access on port"*.
2. Script **cố ý tắt DTR/RTS** trước khi mở cổng. Trên board Silabs, DTR nối vào
   chân reset, nên mở cổng theo mặc định sẽ **reset chip** — hậu quả rất dễ chẩn
   đoán nhầm: cửa sổ 64 giây của model bị xóa và gom lại từ đầu, dashboard báo
   "đang gom cửa sổ" mãi không xong trong khi log serial cho thấy model chạy bình
   thường.

---

## 6. HIS Server — app cuối cùng (ASP.NET Core)

Thư mục: `his-server/src/HisServer`. Đây là app **được viết lại
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

1. Board cảm biến đo được: HR/SpO2 bình thường, Drops chưa lắp (`CH_DISABLED`),
   Flow (loadcell) đang chảy nhưng dây bị tắc (drop_ratio ngoài khoảng đặt).
2. `ai_monitor_step()` → `reason_flow = 1` (flow_ratio ngoài khoảng), kênh
   Drops chưa lắp → trạng thái `CH_DISABLED`, không set `reason_missing`.
3. `app.c` ghi cả 5 giá trị (bao gồm `alarm_bitmap` có bit3 `line_blocked`)
   vào 5 attribute của **cùng 1 cluster `Smart IV Vitals`, endpoint 2**
   (dùng `sl_zigbee_af_write_manufacturer_specific_server_attribute()`),
   reporting tự gửi Zigbee.
4. NCP nhận khung Zigbee, chuyển qua USB dạng EZSP.
5. zigbee2mqtt (`fromZigbee`) đọc `msg.data.alarmBitmap` từ cluster
   `smartIvVitals`, tách bitmap thành `line_blocked: true`, publish MQTT
   topic `zigbee2mqtt/SmartIV-Sensor` với toàn bộ state hiện tại (mọi field
   expose gộp lại).
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
| **MỌI lệnh ZCL gửi XUỐNG thiết bị đều timeout** (`configureReporting(...)`, `smartIvVitals.read(...)`, `smartIvVitals.write(...)` đều `timed out after 10000ms`), trong khi báo cáo thiết bị gửi LÊN vẫn chạy tốt | **Endpoint khai `profileId = 65535`** (0xFFFF, mặc định của "Custom ZCL Device Type" trong ZAP) nhưng zigbee2mqtt gửi lệnh với `profileId = 260` (0x0104 Home Automation). SDK lọc profile ở `app/framework/util/util.c:471` và **drop im lặng, không trả lời gì** → z2m chờ 10s rồi timeout. Chiều gửi lên không bị lọc nên vẫn chạy, tạo cảm giác "một chiều" rất dễ chẩn đoán nhầm là lỗi mạng/binding | Sửa `profileId` của **endpoint** sang `260` trong `config/zcl/zcl_config.zap`, rồi `slc generate` + build + flash. **Chỉ đổi ở cấp endpoint** — nếu đổi luôn `deviceTypes.profileId`/`deviceTypeRef.profileId` sang 260 thì ZAP không resolve được "Custom ZCL Device Type" ở profile HA và **âm thầm xoá sạch mọi endpoint** khỏi bảng sinh ra (`ZCL_FIXED_ENDPOINT_ARRAY { }` rỗng) mà không báo lỗi |
| ~~`configureReporting(...)` timed out, các endpoint sau không được bind~~ (cách né cũ, ĐÃ LỖI THỜI) | Trước đây tưởng do vòng lặp `configure()` bị throw giữa chừng | Đây thực ra chỉ là **triệu chứng** của lỗi `profileId` ở dòng trên. Sau khi sửa profile thì lệnh ZCL xuống thiết bị chạy bình thường, **không cần** bỏ `configureReporting()` nữa |
| `alarm` trong MQTT payload luôn `true` dù không có cảnh báo gì | Tính `alarm` bằng `bitmap !== 0` — dính luôn bit tín hiệu (5-8) | Mask đúng `bitmap & 0x1F` (chỉ 5 bit lý do cảnh báo) |
| mosquitto "Not authorized" trên Pi | `allow_anonymous false` nằm SAU `include_dir` trong `mosquitto.conf` (directive sau đè directive trước) | Sửa trực tiếp `allow_anonymous true` trong file conf chính |
| Gateway log "Client ... already connected, closing old connection" liên tục | 2 tiến trình `gateway_test` (1 chạy tay + 1 chạy qua systemd) cùng dùng 1 MQTT client ID | `pkill` tiến trình chạy tay thừa, chỉ để lại đúng 1 instance (do systemd quản lý) |
| Giường hiện Stable rồi nhảy Offline liên tục | `Offline.ThresholdSeconds` quá thấp (3s) trong khi thiết bị chỉ report tối đa 60s/lần khi rảnh | Tăng `Offline.ThresholdSeconds` lên 90 trong `appsettings.json` |
| Giường luôn hiện Stable dù cảm biến chưa lắp | `VitalsStatusEvaluator` (bản cũ) không xét cờ tín hiệu, chỉ xét SpO2/HR | Đưa việc mất tín hiệu bất kỳ kênh nào vào `Evaluate()` → Warning |
| Sau khi restart HIS Server, cờ tín hiệu/flow_rate của giường về mặc định dù DB có dữ liệu đúng | `Program.cs` (đoạn nạp lại state lúc khởi động) quên copy các trường mở rộng (`FlowRate`, 4 cờ tín hiệu, `LineBlocked`, `AeAlarm`) | Bổ sung đủ field khi copy từ `BedRepository.LoadAllAsync()` vào `BedStateStore` |
| MySQL `ADD COLUMN IF NOT EXISTS` báo lỗi cú pháp | MySQL 8.0 không hỗ trợ cú pháp này (chỉ MariaDB) | Dùng `ALTER TABLE ... ADD COLUMN` trần, tự kiểm tra cột đã tồn tại chưa trước khi chạy |
| zigbee2mqtt: "permit_join setting removed" khi migrate config | Từ bản 2.x, permit_join chỉ điều khiển qua MQTT/frontend, không còn là key YAML | Dùng lệnh `mosquitto_pub` ở mục 3.4 |
| Converter không load ("Loaded external converter" không xuất hiện trong log) | Để file converter sai chỗ (`data/` thay vì `data/external_converters/`) | Di chuyển đúng thư mục |
| HR/SpO2/loadcell đọc ra số cố định (đứng hình) hoặc nhảy loạn xạ theo lũy thừa 2 (vd 379g → 758g → 47935g) | Bit-bang I2C/HX711 dùng vòng lặp rỗng đếm số lần làm delay (không canh theo thời gian thực) — build `-Os` chạy nhanh hơn dự định hàng chục-hàng trăm lần, xung SCK/clock quá nhanh so với chip đọc kịp | Dùng `sl_udelay_wait(us)` (component `udelay`, calibrate theo đồng hồ CPU thật) thay cho `for (volatile int i=0;...)` |
| HX711 đọc ra giá trị nhảy vọt không đều dù cân không đổi tải | Zigbee radio stack (interrupt) chen ngang giữa lúc đang dịch 24-bit, làm mẫu bị "gãy" (trộn lẫn 2 lần đọc) | Bọc toàn bộ vòng lặp đọc 24-bit trong `CORE_ENTER_ATOMIC()`/`CORE_EXIT_ATOMIC()` (`em_core.h`), giống `noInterrupts()`/`interrupts()` của thư viện Arduino HX711 gốc |
| `gateway.service` hiện `active (running)` nhưng không log gì mới dù z2m đang publish dữ liệu | Kết nối MQTT bên trong `gateway_test` bị "chết" ngầm (mất kết nối) mà tiến trình vẫn sống, không tự nhận ra | `sudo systemctl restart gateway` |
| z2m interview treo mãi không xong (không thấy "Successfully interviewed" dù đợi vài phút) | Trạng thái nội bộ zigbee2mqtt bị kẹt (nguyên nhân chưa rõ hẳn, có thể do nhiều lần join/leave liên tục trong thời gian ngắn lúc test) | `sudo systemctl restart zigbee2mqtt`, mở lại permit-join, gửi lại `network leave` + `plugin network-steering start 0` |
| Đăng ký `deviceAddCustomCluster` xong nhưng z2m báo device "Not supported" dù `inputClusters` khớp fingerprint | Thiếu field `name` trong định nghĩa attribute (chỉ có `ID`/`type` không đủ) — xác nhận qua struct `Attribute` thật của `zigbee-herdsman` (`zspec/zcl/definition/tstype.d.ts`) | Mỗi attribute phải có đủ `{name, ID, type}`, đặt tên trùng với key trong object `attributes` |
| Cluster tuỳ chỉnh (mfg-specific) ghi attribute bằng `sl_zigbee_af_write_attribute()` không có tác dụng | Cluster có `mfgCode` (manufacturer-specific) cần hàm ghi riêng | Dùng `sl_zigbee_af_write_manufacturer_specific_server_attribute(endpoint, cluster, attrId, mfgCode, dataPtr, type)` |
| Board "mất tích" hoàn toàn: không log serial, không join Zigbee, mọi lệnh debug/reset đều như không có tác dụng | Tiến trình `silink` (J-Link) cũ bị treo từ phiên debug trước, giữ session hỏng khiến CPU ở trạng thái debug-halt | `ps aux \| grep silink`, `kill -9 <pid>` tiến trình cũ, rồi mass-erase + flash lại để chắc chắn resume chạy |

---

## 10. Tổng kết đường dẫn file quan trọng

```
empty_2/
├── sensor_hub.{c,h}          firmware: đọc cảm biến, trạng thái từng kênh
├── ai_monitor.{c,h}          firmware: AI + luật lâm sàng
├── app.c                     firmware: vòng lặp chính, ghi Zigbee
├── config/zcl/zcl_config.zap cấu hình ZAP: 2 endpoint, cluster tuỳ chỉnh
├── config/zcl/smart-iv-vitals.xml     dinh nghia cluster "Smart IV Vitals" (0xFC01)
├── zigbee2mqtt_smart_iv_converter.js   converter giải mã cho zigbee2mqtt
├── gateway/main.c            chương trình C: MQTT → TCP JSON
└── his-server/
    ├── database/schema.sql, seed_demo.sql
    └── src/HisServer/
        ├── Ingestion/BedDataParser.cs, BedTcpIngestionService.cs
        ├── Domain/VitalsStatusEvaluator.cs, AlertTransitionTracker.cs
        ├── Data/*.Repository.cs
        ├── Api/*Endpoints.cs
        ├── Program.cs
        └── wwwroot/          web UI (index.html, css/, js/)
```

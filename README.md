# Smart IV Monitor — hệ thống giám sát dịch truyền có AI

Hệ thống theo dõi **dịch truyền tĩnh mạch** cho bệnh viện: một thiết bị gắn ở
mỗi giường tự đo nhịp tim, SpO2, tốc độ truyền và số giọt/phút, chạy AI ngay
trên chip để phát hiện bất thường, rồi gửi về màn hình trung tâm cho y tá qua
Zigbee. Khi có sự cố (tụt oxy, tắc dây, chảy tự do, tuột cảm biến) thì báo
động **ở cả hai nơi**: màn hình đầu giường và console ở trạm điều dưỡng.

Dự án làm cho IoT Challenge 2026 (nhóm ICTU).

---

## Hệ thống gồm những gì

```
┌──────────────┐  Zigbee   ┌──────────────┐  USB/UART  ┌──────────────────────┐
│ Board cảm    │ ────────► │  Board NCP   │ ─────────► │  Raspberry Pi        │
│ biến đầu     │           │ (Coordinator)│            │  mosquitto (MQTT)    │
│ giường       │           │  BRD2709A    │            │  zigbee2mqtt         │
│ EFR32xG26    │           │              │            │  gateway_test (C)    │
│ + OLED       │           └──────────────┘            └──────────┬───────────┘
└──────────────┘                                                  │ TCP JSON :5000
                                                                  ▼
                                                    ┌───────────────────────────┐
                                                    │  HIS Server (ASP.NET Core)│
                                                    │  MySQL + REST + SignalR   │
                                                    │  Web UI cho y tá          │
                                                    └───────────────────────────┘
```

Bốn "trạm" độc lập, nối với nhau bằng giao thức chuẩn (Zigbee → MQTT → TCP/
JSON), mỗi trạm viết bằng công nghệ hợp với việc của nó và **sửa được riêng**
mà không đụng ba trạm kia.

| # | Trạm | Chạy ở đâu | Việc |
|---|---|---|---|
| 1 | Firmware `smart-iv-monitor` | Chip EFR32xG26 gắn ở giường | Đọc cảm biến, chạy AI, quyết định báo động, hiện OLED, gửi Zigbee |
| 2 | Firmware NCP | Board thứ hai cắm vào Pi | "Phiên dịch" Zigbee ↔ USB, dùng nguyên bản Silicon Labs, không sửa gì |
| 3 | zigbee2mqtt + `gateway_test` | Raspberry Pi | Giải mã gói Zigbee thành JSON, bắn tiếp qua TCP tới server |
| 4 | HIS Server | Máy chủ / máy dev | Lưu MySQL, tính trạng thái giường, phục vụ web UI realtime |

## AI chạy ở đâu, làm gì

Cả **ba** model đều chạy **trên chip**, không cần mạng, không gửi dữ liệu bệnh
nhân đi đâu để suy luận. Chúng chạy **độc lập với nhau** — đó là điểm cốt lõi:

| Model | Nhìn gì | Trả lời câu hỏi | Kích thước |
|---|---|---|---|
| **Drip forecaster** | 64 giây số giọt | *Dòng chảy sắp đi về đâu?* | 22,4 KB |
| **Vitals forecaster** | 64 giây HR + SpO2 | *Sinh hiệu sắp đi về đâu?* | 23,5 KB |
| **Vitals autoencoder** | HR + SpO2 **ngay lúc này** | *Bản thân bệnh nhân có ổn không?* | 3,1 KB |

**Vì sao tách ba.** Bản cũ dùng **một** mạng đọc chung sinh hiệu lẫn dữ liệu
giọt, nên nó nói được *"có gì đó không ổn"* nhưng không nói được **không ổn ở
đâu** — trong khi tắc dây và bệnh nhân xấu đi có cách xử trí hoàn toàn khác nhau.
Tệ hơn, đo trực tiếp trên chính file model đang chạy: chỉ đổi hai kênh đường
truyền sang trạng thái tắc thì **dự báo nhịp tim dịch chuyển ≈ 2 bpm**. Không dây
truyền nào làm đổi nhịp tim bệnh nhân.

Tách rời cho ba tín hiệu **quy được về đúng nguồn**, nên hệ thống ra được **4 cấp
cảnh báo** phân biệt *lỗi đường truyền* với *lỗi bệnh nhân*, và cấp cao nhất là
khi **cả hai cùng xảy ra** — nghi ngờ quá tải dịch, xử trí đầu tiên là khoá dây.

**Màu đỏ dành riêng cho bệnh nhân đang nguy hiểm.** Sự cố đường truyền — tắc,
chảy tự do, lệch y lệnh — chỉ tới mức **vàng**. Tắc dây là vấn đề thật và phải có
người đi xử lý, nhưng cho nó cùng màu cùng tiếng còi với tụt oxy là cách nhanh
nhất khiến cả khoa thôi phản ứng với màu đỏ. Ngoại lệ duy nhất là cấp 3, và ở đó
phần đỏ đến từ nửa bệnh nhân.

**Cái cân không dùng AI.** Bình đầy chảy nhanh, bình gần hết chảy chậm — chảy
chậm lúc gần hết là **bình thường**. Nhìn từ số giọt thì tắc dây và hết dịch
giống hệt nhau; **trọng lượng bình phân định dứt khoát**: cân vẫn nhẹ dần nghĩa
là dịch vẫn vào (im lặng, chỉ nhắc "còn ~28 phút"), cân đứng yên nghĩa là tắc
(báo động). Đây là luật số học tường minh, giải thích được cho y tá, không phải
trọng số mạng.

Cân cũng cho con số y tá dùng nhiều nhất trong ca: **còn bao nhiêu mL, còn bao
nhiêu phút**. Khi chưa tính được, thiết bị nói *"chưa biết"* chứ không nói *"còn
0 phút"* — báo một con số sai một cách tự tin là thứ khiến người ta thôi tin cái
máy.

Ba file `.tflite` đang chạy nằm trong [`ml/models/`](ml/models/) — mở bằng
[Netron](https://netron.app) là xem được từng lớp, không phải hộp đen. Tham số
lượng tử hoá chip in ra lúc khởi động đã được **đối chiếu khớp tuyệt đối** với
ba file này.

Song song với AI luôn có **luật lâm sàng cứng** (SpO2 < 90%, nhịp tim lệch >30%
so với baseline riêng của bệnh nhân, flow ngoài 0.3–1.5× mức bác sĩ đặt...). Luật
cứng **không đi qua bộ lọc chống báo giả** — SpO2 tụt kêu ngay tick đầu tiên. Và
nếu **không model nào nạp được**, luật cứng vẫn gánh toàn bộ việc báo động; có
bài kiểm thử riêng cho đúng tình huống đó.

Số đo (trên bản int8 đang chạy trên chip):

| | Kết quả |
|---|---|
| Phát hiện bất thường dòng chảy, **bản ghi thật** | AUC **0,948** (baseline 0,839) |
| Phát hiện bất thường sinh hiệu, **bệnh nhân chưa từng thấy** | AUC **0,885** (baseline 0,680) |
| Báo động giả sau bộ lọc 11 giây | **0,0 lần/giờ** (trước đó 29–47) |
| RAM tĩnh / Flash | 32,5 KB (6,2%) / 381 KB |

**Một giới hạn nhóm nêu rõ:** bộ dữ liệu ICU dùng để đánh giá không chứa ca diễn
biến xấu dần nào, nên **tỉ lệ bắt đúng của nhánh sinh hiệu chưa đo được** — chỉ
đo được tỉ lệ báo giả. Chi tiết ở
[`docs/Dataset_va_Phuong_phap_AI_SmartIV.md`](docs/Dataset_va_Phuong_phap_AI_SmartIV.md).

---

## Ba vai trò sử dụng

Mỗi vai trò chỉ thấy dữ liệu của việc mình làm — không ai có màn hình "biết
tuốt". Lập luận đầy đủ kèm 7 tình huống thực tế ở
[`docs/PHAN_QUYEN_VA_VAI_TRO.md`](docs/PHAN_QUYEN_VA_VAI_TRO.md).

| | Điều dưỡng | Kỹ thuật viên | Quản trị viên |
|---|---|---|---|
| Dashboard, chỉ số, cảnh báo lâm sàng | ✅ | ❌ | ❌ |
| Đặt ngưỡng, tare cân, nhận/xuất bệnh nhân | ✅ | ❌ | ❌ |
| Báo hỏng thiết bị | ✅ tạo | ✅ xử lý | ❌ |
| Tình trạng thiết bị + gán giường | ❌ | ✅ | ❌ |
| Cập nhật firmware từ xa (OTA) | ❌ | ✅ | ✅ |
| Danh mục giường, System Log, tài khoản | ❌ | ❌ | ✅ |

Chặn ở **cả hai lớp**: giao diện xoá hẳn phần tử không thuộc quyền, và API trả
403 — kể cả kênh realtime SignalR cũng chia nhóm theo năng lực, nên máy của kỹ
thuật viên không nhận được gói dữ liệu bệnh nhân nào.

## Cập nhật firmware từ xa

Kỹ thuật viên tải file `.ota` lên ngay trên web, bấm kiểm tra, bấm cập nhật, và
xem thanh tiến độ — **không cần ssh, không cần cầm dây tới giường**. Chip tự
khởi động lại vào bản mới.

Đã chạy thật đầu-cuối: **v2 → v4**, 436 KB, tải 862 giây, xác minh 54 giây, tự
khởi động lại và chạy đủ sau 5 giây.

Ba thứ đáng chú ý trong thiết kế:

- **Ảnh được chào theo số phiên bản, không theo kích thước file.** File nhỏ hơn
  vẫn cập nhật đè được, miễn phiên bản cao hơn.
- **Kiểm theo header của chính file, không theo tên.** Chọn nhầm `.gbl` hay
  `.s37` cùng thư mục build là bị từ chối ngay lúc tải lên — và index do server
  **tự sinh từ header**, nên không thể mâu thuẫn với file nó trỏ tới.
- **Ảnh sai chip không bao giờ được chào.** Mã nhà sản xuất phải trùng. Đây
  không phải lo xa: đã có lần thiết bị khởi động vào firmware của người khác vì
  một ảnh cũ nằm lại trong bộ nhớ — ghi lại đầy đủ ở
  [`gateway/ota/README.md`](gateway/ota/README.md).

Toàn bộ quy trình, phân vai và bảng sự cố: [`docs/OTA.md`](docs/OTA.md).

---

## Cấu trúc thư mục

Repo xếp theo **trạm xử lý**: mã của trạm nào nằm trong thư mục của trạm đó.

| Thư mục | Nội dung |
|---|---|
| `firmware/` | **Trạm 1** — mã nguồn chạy trên chip: đọc cảm biến, AI, báo động, OLED, Zigbee |
| `gateway/` | **Trạm 3** — `main.c` (cầu MQTT → TCP) và converter cho zigbee2mqtt, đều chạy trên Pi |
| `server/` | **Trạm 4** — HIS Server: ASP.NET Core + MySQL + SignalR + web UI |
| `ml/models/` | Ba file `.tflite` **đang chạy thật trên chip** — mở bằng Netron là xem được |
| `ml/dataset/` | Script sinh dataset cho từng model, tách rời nhau |
| `ml/` | `train_*.py`, `evaluate.py`, `export_c_headers.py` (gọi tool MLTK của Silicon Labs) |
| `ml/data_src/` | Dữ liệu nguồn: bản ghi cảm biến giọt thật + ICU BIDMC |
| `firmware/models/` | Header C **sinh tự động** từ `.tflite` — đừng sửa tay |
| `tools/` | `serial_gateway.py`; các bài kiểm thử chạy trên máy: `fusion_test.c`, `drop_filter_test.c`, `oled_test.c`, `dashboard_render_check.js`, `ota_render_check.js` |
| `gateway/ota/` | Index OTA và luật an toàn khi phát hành firmware (ảnh `.ota` **không** đưa vào git) |
| `server/tests/` | Kiểm thử bộ đánh giá trạng thái giường (console app, không cần NuGet) |
| `docs/` | Toàn bộ tài liệu (xem bên dưới) |

Bốn thứ này **bắt buộc nằm ở gốc repo**, không gom vào `firmware/` được:
`smart-iv-monitor.slcp`, `config/`, `autogen/`, `cmake_gcc/` và `main.c`.
Simplicity Studio và `slc` coi gốc repo là gốc project và tự sinh ba thư mục
đó ở đúng vị trí; đẩy chúng xuống thư mục con sẽ làm hỏng cả việc mở project
trong Studio lẫn lệnh `slc generate`. Đường dẫn của mã trong `firmware/` được
khai trong `smart-iv-monitor.slcp` — thêm file `.c` mới thì phải khai vào đó
rồi chạy lại `slc generate`.

`simplicity_sdk_*/` và `aiml_*/` là bản copy SDK, **không** được commit (xem
`.gitignore`): build thật compile với SDK cài trong `~/.silabs/`.

---

## Tài liệu

| File | Nội dung |
|---|---|
| [`docs/HUONG_DAN_A_Z.md`](docs/HUONG_DAN_A_Z.md) | **Đọc file này trước.** Toàn bộ hệ thống từ chip tới web, giải thích cả *vì sao* thiết kế như vậy, kèm bảng lỗi thường gặp |
| [`docs/AI_HOAT_DONG_THE_NAO.md`](docs/AI_HOAT_DONG_THE_NAO.md) | AI làm gì, **khi nào báo động và khi nào cố tình không báo**, kèm 4 kịch bản diễn biến **theo từng giây** — viết cho người không đọc code |
| [`ml/models/`](ml/models/) | Ba file `.tflite` đang chạy thật trên chip, mở bằng Netron được |
| [`docs/AI_TIME_SERIES_TAT_TAN_TAT.md`](docs/AI_TIME_SERIES_TAT_TAN_TAT.md) | Tất tần tật phần AI: kiến trúc, huấn luyện, đánh giá, bộ hợp nhất, nhúng vào firmware |
| [`docs/Dataset_va_Phuong_phap_AI_SmartIV.md`](docs/Dataset_va_Phuong_phap_AI_SmartIV.md) | Dataset, cách chia tập, và **những gì nhóm không chứng minh được** |
| [`docs/MLTK_AUTOGEN.md`](docs/MLTK_AUTOGEN.md) | Luồng `.tflite` → tool MLTK của Silicon Labs → chip, kèm hai cái bẫy |
| [`docs/TRIEN_KHAI_PI.md`](docs/TRIEN_KHAI_PI.md) | **Đọc trước khi sửa `gateway/`.** Hai file đó chạy trên Pi chứ không phải máy bạn — kèm ba kiểu hỏng đã gặp thật và cách cứu hộ |
| [`docs/BAO_CAO_KIEM_THU_WEB.md`](docs/BAO_CAO_KIEM_THU_WEB.md) | **Báo cáo kiểm thử web:** 70 ca chạy thật trên ba vai trò, kèm mã HTTP thực tế và hai chức năng còn thiếu |
| [`docs/OTA.md`](docs/OTA.md) | **Cập nhật firmware từ xa.** Ai được thao tác, bấm gì trên giao diện, và các bước phát hành một bản firmware mới từ đầu tới cuối |
| [`docs/AI_V2_PLAN.md`](docs/AI_V2_PLAN.md) | Lý do đằng sau từng quyết định của bản AI v2, **kèm cả những lần đi sai** |
| [`docs/Nghien_cuu_Nang_cap_AI_Time_Series.md`](docs/Nghien_cuu_Nang_cap_AI_Time_Series.md) | Nghiên cứu nâng cấp phần AI (bản cũ, giữ để tham chiếu) |
| [`docs/AI_SYSTEM_SPECIFICATION.md`](docs/AI_SYSTEM_SPECIFICATION.md) | **Bản đặc tả đề xuất ban đầu — KHÔNG phải hệ thống đang chạy.** Giữ lại để đối chiếu; bốn điểm khác biệt ghi ngay đầu file |
| [`server/README.md`](server/README.md) | Riêng HIS Server: API, DB, cách chạy |

---

## Chạy nhanh

Chi tiết đầy đủ (kể cả cách pair Zigbee và xử lý sự cố) nằm trong
`docs/HUONG_DAN_A_Z.md`; đây chỉ là bản rút gọn.

**Firmware** (không cần mở GUI Simplicity Studio):

```bash
NINJA=~/.silabs/slt/installs/conan/p/ninja*/p/ninja
COMMANDER=~/.silabs/slt/installs/archive/commander/commander

cd cmake_gcc/build && $NINJA                      # build
$COMMANDER flash base/smart-iv-monitor.hex --serialno <SERIAL_BOARD>
```

Chỉ khi sửa `.slcp` hoặc file `.zap` mới cần sinh lại code (`slc generate`,
xem mục 1.6 của tài liệu A-Z).

**HIS Server** (cần .NET 8 + Docker):

```bash
docker start his-mysql                            # MySQL 8, lần đầu xem server/README.md
cd server/src/HisServer && dotnet run --urls http://0.0.0.0:5100
```

Web UI ở `http://localhost:5100`. Cổng **5000 để riêng cho TCP** nhận dữ liệu
từ gateway (`Tcp:Port` trong `appsettings.json`) — đừng cho web nghe trùng
cổng đó. `0.0.0.0` là để gateway trên Pi gọi vào được từ máy khác, không chỉ
localhost.

**Kiểm thử chạy trên máy** (không cần board, không cần Pi):

```bash
cc -I firmware -o /tmp/t tools/fusion_test.c firmware/ai_fusion.c firmware/line_rules.c -lm && /tmp/t
cc -I firmware -o /tmp/t tools/drop_filter_test.c firmware/drop_filter.c -lm && /tmp/t
cc -I firmware -o /tmp/t tools/oled_test.c firmware/oled_display.c && /tmp/t
node tools/dashboard_render_check.js
node tools/ota_render_check.js
dotnet run --project server/tests/EvaluatorTests
```

**Gateway trên Pi**: cả `mosquitto`, `zigbee2mqtt` và `gateway` đều chạy nền
bằng systemd, tự bật lại sau khi Pi khởi động:

```bash
sudo systemctl restart gateway
journalctl -u gateway -f
```

`ExecStart` trong `/etc/systemd/system/gateway.service` trỏ vào **tên mDNS** của
máy chạy HIS Server (`<tên-máy>.local`), **không phải IP**. Đây là chủ ý: máy dev
dùng wifi DHCP và đã đổi IP ba lần trong hai ngày, mà mỗi lần đổi IP cứng là
dashboard lặng lẽ ngừng cập nhật. Đừng thay bằng IP.

---

## Những quyết định thiết kế đáng chú ý

Phần này để người đọc code khỏi "sửa lại cho gọn" đúng những chỗ đã cân nhắc kỹ:

- **"Chưa có dữ liệu" không phải là "ổn định", cũng không phải là "nguy kịch".**
  Cảm biến chưa cắm đọc ra 0; nếu coi số 0 đó là chỉ số thật thì giường trống
  cũng kêu "SpO2 0% — nguy kịch". Kênh mất tín hiệu hiện `--` và đẩy giường
  sang **Warning** kèm lý do "No signal from: ...".
- **Chỉ một nơi được quyết định trạng thái giường**: `VitalsStatusEvaluator.cs`
  trên server. Gateway chỉ chuyển tiếp dữ liệu thô + cờ, không tự suy luận —
  trước đây hai nơi cùng tính và ra kết quả khác nhau.
- **Bác sĩ đổi ngưỡng từ xa, không bao giờ phải nạp lại chip.** Ngưỡng là biến
  runtime, ghi qua Zigbee và lưu vào NVM3 nên sống sót qua mất điện. Nạp lại
  chip giữa ca truyền là mất baseline nhịp tim, mất tare cân và mất kết nối
  hàng chục giây.
- **Nhịp báo cáo Zigbee chia theo vai trò**, không dùng chung một mức: cảnh báo
  đi ngay tick đầu tiên, còn cân nặng thì 10 giây/lần — giảm từ ~120 xuống 12
  bản tin/phút mà độ trễ cảnh báo không đổi.
- **Bất kỳ thứ gì hỏng ở tầng ngoài cũng không được làm hỏng việc theo dõi**:
  không có màn hình OLED, không load được model AI, không có mạng — thiết bị
  vẫn đo và vẫn báo động bằng luật lâm sàng.

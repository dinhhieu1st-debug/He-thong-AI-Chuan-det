# Phân hệ âm thanh Smart IV: xG26 <-> ESP32-S3 <-> DFPlayer <-> Loa

Tài liệu này mô tả **toàn bộ** phân hệ phát âm thanh của Smart IV, nhìn
từ phía firmware xG26 (EFR32MG26). Phần firmware ESP32-S3 (audio
coprocessor) nằm ở project riêng
[`uno/`](../../uno/README.md) (`uno/AmThanh/AmThanh.ino`) — đọc kèm file
đó để có bức tranh đầy đủ cả hai phía.

Mục tiêu của tài liệu: một người/chatbot khác chỉ cần đọc file này +
`uno/README.md` là hiểu được hệ thống mà không cần dò lại lịch sử chat.

## 1. Vai trò từng khối

```
EFR32MG26 / xG26          ESP32-S3                 DFPlayer Mini        Loa
------------------        ---------------------    ----------------     ---
- đọc cảm biến        --> - audio coprocessor  --> - đọc WAV từ    -->  phát
  (MAX30102, HX711,        - nhận lệnh UART từ       microSD              âm
  drop sensor)              xG26                    - xuất tín hiệu       thanh
- Zigbee                  - điều khiển DFPlayer      loa
- AI / cảnh báo           - gửi READY/ACK/DONE/
- OLED                      ERROR ngược lại xG26
- CLI (voice <digits>)
- quyết định KHI NÀO
  cần phát âm thanh
```

xG26 là bộ xử lý trung tâm (cảm biến, Zigbee, AI, cảnh báo, OLED...) và
**không** trực tiếp lái DFPlayer. Nó chỉ quyết định "cần đọc chuỗi số
này" và gửi qua UART; toàn bộ việc điều khiển DFPlayer, chọn file WAV,
theo dõi trạng thái phát... do ESP32-S3 đảm nhận.

## 2. Kết nối phần cứng xG26 <-> ESP32-S3

### 2.1 Vì sao PA04/PA05 được giải phóng — LED đã chuyển chân

Ban đầu `PA04`/`PA05` dùng làm output cho LED vàng/đỏ. Để có một cặp
UART TX/RX rảnh nối sang ESP32, phần cứng + firmware đã đổi:

| Tín hiệu | Trước | Sau | Ghi chú |
|---|---|---|---|
| LED vàng | PA04 | **PC02** | mikroBUS MOSI |
| LED đỏ | PA05 | **PC00** | mikroBUS INT |
| LED xanh | PA07 | PA07 | không đổi |

Bảng pin map đầy đủ và cập nhật nằm ở
[`../firmware/PIN_MAP.md`](../firmware/PIN_MAP.md) — đó là nguồn chân lý
(source of truth) cho toàn bộ pin mapping của board, tài liệu này chỉ
trích phần liên quan tới audio.

**CẢNH BÁO CHO NGƯỜI SỬA CODE SAU NÀY:** `PA04` và `PA05` bây giờ **chỉ**
dùng làm UART (EUSART1) nối ESP32-S3. **Không được đổi lại thành chân
LED** — nếu cần thêm LED, dùng chân GPIO trống khác, không đụng vào
PA04/PA05.

### 2.2 Sơ đồ đấu dây

Đấu **chéo** (TX của bên này nối RX của bên kia):

```
xG26 PA04 (UART TX)  ------------------->  ESP32-S3 GPIO44 (UART0 RX)
xG26 PA05 (UART RX)  <-------------------  ESP32-S3 GPIO43 (UART0 TX)
xG26 GND             ------------------->  ESP32-S3 GND   (bắt buộc chung)
```

> Lưu ý: phía ESP32, cặp chân UART0 vật lý (GPIO43/44) là 2 chân
> TX/RX gốc cạnh đầu nối USB-C của board — **không phải** GPIO17/18.
> Xem mục 2.4 để biết vì sao.

Cấu hình UART cả hai phía:

```
Baud rate : 115200
Data bits : 8
Parity    : None
Stop bits : 1
Flow ctrl : None
```

Tức **115200 8N1**.

Phía xG26 đây là **EUSART1**, tách biệt hoàn toàn với **EUSART0**
(PB02/PB03) — đường VCOM dùng cho CLI debug qua USB. Hai UART này độc
lập, không chia sẻ buffer hay logic.

### 2.3 Phía firmware xG26 (`firmware/app.c`)

Các hằng số liên quan (tìm bằng `ESP32_UART` trong `app.c`):

```c
#define ESP32_UART              EUSART1
#define ESP32_UART_PORT         gpioPortA
#define ESP32_UART_TX_PIN       4U      // PA04
#define ESP32_UART_RX_PIN       5U      // PA05
#define ESP32_UART_BAUDRATE     115200U
#define ESP32_UART_LINE_MAX     63U
```

- `esp32_uart_init()` — bật clock EUSART1, cấu hình GPIO (TX push-pull,
  RX input có pull-up để đường truyền ở mức "idle-high" khi ESP32 chưa
  bắt đầu drive TX), init EUSART UART HF, route TX/RX qua crossbar.
  Được gọi 1 lần trong `app_init()`.
- `esp32_uart_send_line(const char *digits)` — gửi `digits` + `\n` ra
  UART (blocking chờ FIFO, chỉ gọi từ ngữ cảnh lệnh CLI nên chấp nhận
  được).
- `esp32_uart_poll()` — được gọi mỗi tick trong `app_process_action()`
  (không blocking): đọc byte có sẵn trong RX FIFO, gom thành dòng theo
  `\n`/`\r`, khi đủ 1 dòng thì gọi `esp32_uart_handle_line()`.
  `esp32_uart_handle_line()` **hiện tại (giai đoạn debug) chỉ in mọi
  dòng nhận được ra CLI** dạng `[ESP32 RX] <line>` — chưa phân biệt
  READY/ACK/DONE/ERROR để làm logic riêng (vd. retry, timeout...). Nếu
  nhận byte nhưng không kết thúc bằng dòng hợp lệ trong 500 ms (lỗi
  baud/framing/wiring), nó dump ra dạng hex `[ESP32 RX HEX-PARTIAL]` để
  dễ debug thay vì im lặng mất dữ liệu.

### 2.4 Ghi chú lệch so với thiết kế ban đầu (đọc kỹ trước khi dùng số liệu)

Tài liệu thiết kế ban đầu của hệ thống này từng ghi ESP32 dùng
`GPIO18`/`GPIO17` (`HardwareSerial xg26Serial(2)` với
`XG26_RX=18, XG26_TX=17`) cho UART nối xG26. **Code thực tế hiện tại
trong `uno/AmThanh/AmThanh.ino` dùng `GPIO44`/`GPIO43`** (UART0 vật lý,
cặp chân TX/RX gốc cạnh USB-C), không phải GPIO18/17.

Lý do (theo comment trong code): `Serial` (console debug qua USB) từng
dùng chung UART0 vật lý này khi build với `CDCOnBoot=Disabled`, gây
lẫn log vào đường dây xG26. Giải pháp đã áp dụng: chuyển `Serial` sang
**USB CDC native** (`CDCOnBoot=cdc`), giải phóng UART0 vật lý hoàn toàn
cho `xg26Serial`, thay vì chuyển `xg26Serial` sang GPIO17/18.

**Tài liệu này ưu tiên code đang chạy thật**: coi `GPIO44`
(RX)/`GPIO43` (TX) là đúng, `GPIO18`/`GPIO17` là thông tin cũ/lỗi thời.
Phía xG26 (`PA04`/`PA05`, EUSART1) không đổi — chỉ phía ESP32 khác so
với thiết kế ban đầu. Nếu về sau đổi lại sang GPIO17/18, phải cập nhật
cả file này và `uno/README.md`.

## 3. Kết nối ESP32-S3 <-> DFPlayer Mini

```c
#define DFPLAYER_RX 10   // ESP32 RX  <- DFPlayer TX
#define DFPLAYER_TX 9    // ESP32 TX  -> DFPlayer RX
```

```
ESP32 GPIO10 (RX)  <-------------------  DFPlayer TX
ESP32 GPIO9  (TX)  ------------------->  DFPlayer RX
ESP32 GND          ------------------->  DFPlayer GND
```

Cấu hình: `9600 8N1`, `HardwareSerial dfSerial(1)`.

**QUAN TRỌNG:** mapping `GPIO10=RX / GPIO9=TX` đã test chạy thành công
— **không đảo lại**. Loa nối vào ngõ ra loa của DFPlayer; nguồn DFPlayer
theo mạch hiện tại (ngoài phạm vi tài liệu này).

## 4. Giao thức UART hiện tại (ASCII line protocol)

Mỗi message kết thúc bằng `\n`.

### 4.1 xG26 -> ESP32

Trên CLI xG26, lệnh người dùng gõ:

```
voice <digits>
```

nhưng xG26 **không** gửi chữ `voice` qua UART — chỉ gửi phần số:

```
<digits>\n
```

Ví dụ:

| Gõ trên CLI | Gửi qua UART (EUSART1) |
|---|---|
| `voice 1` | `1\n` |
| `voice 123` | `123\n` |
| `voice 405` | `405\n` |

Ràng buộc hiện tại (validate trong `process_command()` của `app.c`,
lệnh `voice`):

- Chỉ chấp nhận ký tự `0`-`9`.
- Độ dài 1-10 ký tự.
- Sai định dạng: xG26 in lỗi ra CLI cục bộ (`[VOICE] ERROR: ...`),
  **không** gửi gì qua UART sang ESP32 (khác với validate lỗi phía
  ESP32 — xem 4.2).

### 4.2 ESP32 -> xG26

| Message | Ý nghĩa |
|---|---|
| `READY\n` | ESP32 + DFPlayer sẵn sàng. Gửi 1 lần lúc boot xong, và lặp lại mỗi 2 giây (tín hiệu debug/keep-alive, không phụ thuộc thứ tự boot 2 board) |
| `ACK\n` | Đã nhận lệnh hợp lệ từ xG26, chuẩn bị phát |
| `DONE\n` | Đã phát xong toàn bộ chuỗi |
| `ERROR\n` | Chuỗi rỗng / quá 10 ký tự / có ký tự không phải số / DFPlayer chưa sẵn sàng / buffer UART tràn / lỗi phát từ DFPlayer |

Phía xG26 hiện tại (`esp32_uart_handle_line()`) chỉ **in ra** mọi dòng
nhận được (`[ESP32 RX] <line>`), chưa có logic riêng theo từng loại
message (vd. chờ `ACK` mới coi là gửi thành công, timeout nếu không
thấy `DONE`...). Đây là điểm có thể mở rộng sau.

### 4.3 Lọc nhiễu EMI (phía ESP32)

Dây PA04/PA05 đi gần Zigbee/HX711 trên board xG26 nên có thể dính vài
byte rác ở đầu dòng UART. ESP32 có hàm lọc chạy **sau khi đã nhận đủ 1
dòng** (`\n`), chỉ giữ ký tự `0`-`9`, không đụng vào logic UART/parity/
framing. Xem chi tiết ở `uno/README.md` §3.3.

### 4.4 Ví dụ một phiên giao tiếp

```
ESP32 boot xong              -> xG26    READY
Người dùng gõ trên CLI xG26:    voice 123
xG26 gửi qua UART (PA04)     -> ESP32   123\n
ESP32 nhận, phản hồi         -> xG26    ACK
ESP32 điều khiển DFPlayer đọc lần lượt: một, hai, ba
Khi phát xong toàn bộ chuỗi  -> xG26    DONE
```

## 5. Cấu trúc thẻ microSD của DFPlayer

```
/
├── 01/
│   ├── 001.wav   = "một"
│   ├── 002.wav   = "hai"
│   ├── 003.wav   = "ba"
│   ├── 004.wav   = "bốn"
│   ├── 005.wav   = "năm"
│   ├── 006.wav   = "sáu"
│   ├── 007.wav   = "bảy"
│   ├── 008.wav   = "tám"
│   ├── 009.wav   = "chín"
│   └── 010.wav   = "không"
└── 02/
    ├── 001.wav   = "cảnh báo"
    ├── 002.wav   = "chảy"
    ├── 003.wav   = etpeohai.wav (DỰ KIẾN dùng cho SpO2 — xem cảnh báo bên dưới)
    ├── 004.wav   = "nhịp tim"
    └── 005.wav   = "nhỏ giọt"
```

Mapping số -> file (folder `01/`), khớp `digitToFileNumber()` trong
`uno/AmThanh/AmThanh.ino`:

```
1 -> /01/001.wav      6 -> /01/006.wav
2 -> /01/002.wav      7 -> /01/007.wav
3 -> /01/003.wav      8 -> /01/008.wav
4 -> /01/004.wav      9 -> /01/009.wav
5 -> /01/005.wav      0 -> /01/010.wav
```

Ví dụ nhận `123` -> ESP32 phát tuần tự `/01/001.wav`, `/01/002.wav`,
`/01/003.wav`.

**CẢNH BÁO — chưa xác minh được:** `/02/003.wav` hiện được đặt tên
`etpeohai.wav` trên tài liệu gốc cung cấp cho phiên làm việc này. Trong
lần chuẩn bị tài liệu này, **không thể mở thẻ SD vật lý hoặc tìm thấy
metadata xác nhận nội dung chính xác của file** này trong code hoặc
project. Vì vậy tài liệu **giữ nguyên tên `etpeohai.wav`** và ghi chú
đây là **file dự kiến dùng cho SpO2**, chưa xác nhận 100%. Người tiếp
theo cần đối chiếu trực tiếp với thẻ SD thật trước khi dùng file này
trong logic ghép câu.

**Lưu ý quan trọng:** thư mục `02/` (từ/cụm từ) **chưa được dùng trong
code hiện tại** (`uno/AmThanh/AmThanh.ino` chỉ gọi
`dfPlayer.playFolder(1, ...)`, không có chỗ nào gọi folder `2`). Đây là
tài nguyên đã có trên thẻ SD nhưng logic ghép câu chưa implement — xem
mục 6.

## 6. Ý tưởng ghép âm thanh (hiện tại vs. tương lai)

**HIỆN TẠI (đã chạy):** chuỗi số thuần, đọc từng chữ số một.

```
123  ->  "một" "hai" "ba"
```

**TƯƠNG LAI (ý tưởng, CHƯA implement):** ghép cụm từ (folder `02/`) với
số (folder `01/`) để đọc câu có ngữ nghĩa, ví dụ:

```
HR = 123        -> /02/004.wav ("nhịp tim") + /01/001 + /01/002 + /01/003
SpO2 = 98        -> file SpO2 (/02/003.wav?) + /01/009 ("chín") + /01/008 ("tám")
Cảnh báo nhỏ giọt -> /02/001 ("cảnh báo") + /02/005 ("nhỏ giọt") + ...
```

Đây **chỉ là kế hoạch**, chưa có trong `process_command()` (xG26) hay
`AmThanh.ino` (ESP32). Không tự ý implement khi chưa được yêu cầu rõ.

## 7. Protocol dự kiến mở rộng (kế hoạch, KHÔNG phải code hiện tại)

Protocol hiện tại chỉ là `<digits>\n`. Có thể mở rộng bằng prefix, ví
dụ:

```
VOICE:123\n
HR:85\n
SPO2:98\n
DROP:20\n
ALERT:2\n
```

ESP32 parse prefix để biết cần đọc câu gì, ví dụ `HR:85` ->
"nhịp tim" + "tám" + "năm".

**Nhắc lại: đây là kế hoạch tương lai.** Code hiện tại (cả `app.c` lẫn
`AmThanh.ino`) chỉ hỗ trợ chuỗi số thô. Nếu triển khai, phải sửa đồng bộ
cả 2 phía (`process_command()` trong `app.c` và parser trong
`AmThanh.ino`) và cập nhật tài liệu này + `uno/README.md`.

## 8. Test nhanh

1. Cấp nguồn xG26 và ESP32-S3 (đã đấu dây theo mục 2.2 và mục 3).
2. Mở CLI xG26 (VCOM/EUSART0, không phải UART sang ESP32).
3. Gõ:
   ```
   voice 123
   ```
4. Kỳ vọng: nghe loa đọc "một hai ba", và trên CLI xG26 lần lượt thấy
   `[ESP32 RX] ACK` rồi `[ESP32 RX] DONE` (log hiện tại của
   `esp32_uart_handle_line()`, in mọi dòng ESP32 gửi lên).
5. Có thể test riêng ESP32 + DFPlayer (không cần xG26) bằng Serial
   Monitor USB của ESP32 (115200 baud), gõ thẳng chuỗi số.

## 9. Những gì KHÔNG được đụng tới khi sửa phần audio này

- Zigbee, OTA, AI, MAX30102, HX711, drop sensor, OLED, buzzer, CLI (các
  lệnh khác ngoài `voice`), LED, sensor processing trong `app.c`.
- `xg26v1/simplicity_sdk_2025.12.3` — chỉ đọc header để hiểu API, không
  sửa.
- Mapping DFPlayer `GPIO10=RX / GPIO9=TX` phía ESP32.
- `PA04`/`PA05` phía xG26 — chỉ dùng cho UART sang ESP32 (EUSART1), không
  đổi lại thành LED.

## 10. Tài liệu liên quan

- [`../firmware/PIN_MAP.md`](../firmware/PIN_MAP.md) — pin map đầy đủ
  của board (nguồn chân lý cho mọi mapping GPIO).
- [`../../uno/README.md`](../../uno/README.md) — chi tiết phía ESP32-S3
  (setup, thư viện, build bằng Arduino IDE).

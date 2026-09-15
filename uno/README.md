# uno/ — ESP32-S3 Audio Coprocessor (Smart IV)

Đây là firmware Arduino chạy trên **ESP32-S3**, đóng vai trò **audio
coprocessor** của hệ thống Smart IV. Nó nhận lệnh từ board xG26
(EFR32MG26) qua UART, điều khiển **DFPlayer Mini** để phát file WAV từ
thẻ nhớ microSD ra loa, và báo trạng thái ngược lại cho xG26.

File chính: [`AmThanh/AmThanh.ino`](AmThanh/AmThanh.ino)

> Tài liệu chi tiết hơn về giao thức UART (từ góc nhìn cả hai phía) nằm ở
> [`xg26v1/docs/AUDIO_UART_PROTOCOL.md`](../xg26v1/docs/AUDIO_UART_PROTOCOL.md).
> Tài liệu này chỉ tập trung vào phần ESP32.

## 1. Kiến trúc tổng thể

```
EFR32MG26 / xG26  --UART 115200 8N1-->  ESP32-S3  --UART 9600 8N1-->  DFPlayer Mini --> Loa
   (đo cảm biến,        (audio coprocessor:           (đọc WAV từ
    Zigbee, AI,          nhận lệnh, điều khiển          microSD, xuất
    quyết định khi        DFPlayer, báo trạng            loa)
    nào cần phát âm         thái ngược lại)
    thanh)
```

xG26 là "bộ não" (cảm biến, Zigbee, AI, cảnh báo, OLED...) và chỉ cần gửi
một chuỗi số qua UART khi muốn đọc to; ESP32 lo toàn bộ việc còn lại:
parse lệnh, điều khiển DFPlayer, phát đúng file WAV, và báo lại
READY/ACK/DONE/ERROR.

## 2. Đấu nối phần cứng

### 2.1 ESP32-S3 <-> xG26 (UART)

**QUAN TRỌNG — khác với tài liệu thiết kế ban đầu:** code thực tế hiện
tại dùng UART0 vật lý của ESP32-S3 (cặp chân TX/RX gốc cạnh đầu nối
USB-C của board), **không phải GPIO17/GPIO18**. Đây là trạng thái đang
chạy thật, README phản ánh đúng code — không phải đề xuất ban đầu.

```cpp
#define XG26_RX 44   // ESP32 RX  <- xG26 TX / PA04
#define XG26_TX 43   // ESP32 TX  -> xG26 RX / PA05
```

Đấu nối chéo (TX nối RX):

| xG26 (EFR32MG26) | ESP32-S3 |
|---|---|
| PA04 (UART TX) | GPIO44 (UART0 RX) |
| PA05 (UART RX) | GPIO43 (UART0 TX) |
| GND | GND (bắt buộc chung) |

Cấu hình: `115200 8N1` (8 data bit, không parity, 1 stop bit, không flow
control), dùng `HardwareSerial xg26Serial(2)` (UART logic thứ 2 trong
firmware, nhưng map vào UART0 vật lý qua GPIO43/44 ở trên).

**Ghi chú lịch sử (xem comment trong `AmThanh.ino`):** trước đây
`Serial` (console debug qua USB) cũng dùng chung UART0 vật lý này khi
build với `CDCOnBoot=Disabled`, gây nhiễu/lẫn log vào đường dây xG26.
Đã chuyển `Serial` sang **USB CDC native** (`CDCOnBoot=cdc` trong cấu
hình board Arduino) để giải phóng UART0 hoàn toàn cho `xg26Serial`. Khi
build lại project này trong Arduino IDE, **phải chọn "USB CDC On Boot:
Enabled"**, nếu không UART xG26 sẽ bị lẫn log console.

### 2.2 ESP32-S3 <-> DFPlayer Mini (UART)

```cpp
#define DFPLAYER_RX 10   // ESP32 RX  <- DFPlayer TX
#define DFPLAYER_TX 9    // ESP32 TX  -> DFPlayer RX
```

| ESP32-S3 | DFPlayer Mini |
|---|---|
| GPIO10 (RX) | TX |
| GPIO9 (TX) | RX |
| GND | GND |

Cấu hình: `9600 8N1`, dùng `HardwareSerial dfSerial(1)`.

**KHÔNG đảo GPIO9/GPIO10** — mapping này đã test chạy thành công, đảo
lại sẽ làm mất kết nối DFPlayer.

Loa nối vào ngõ ra loa của DFPlayer Mini. Nguồn DFPlayer theo mạch hiện
tại (không thuộc phạm vi file này).

### 2.3 Phía xG26 — LED đã đổi chân để nhường TX/RX cho ESP32

Ban đầu PA04/PA05 dùng cho LED vàng/đỏ. Để giải phóng UART cho ESP32,
phần cứng + firmware xG26 đã đổi:

| LED | Chân cũ | Chân mới |
|---|---|---|
| LED vàng | PA04 | **PC02** (mikroBUS MOSI) |
| LED đỏ | PA05 | **PC00** (mikroBUS INT) |
| LED xanh | PA07 | PA07 (không đổi) |

`PA04`/`PA05` bây giờ **chỉ** dùng cho UART sang ESP32 (EUSART1 phía
xG26). Xem chi tiết phía firmware xG26 tại
[`xg26v1/firmware/PIN_MAP.md`](../xg26v1/firmware/PIN_MAP.md) và
[`xg26v1/docs/AUDIO_UART_PROTOCOL.md`](../xg26v1/docs/AUDIO_UART_PROTOCOL.md).
**Không tự ý đổi PA04/PA05 về LED nữa.**

## 3. Giao thức UART xG26 <-> ESP32 (hiện tại)

Giao thức test hiện tại là **ASCII line protocol**: mỗi message kết
thúc bằng `\n`.

### 3.1 xG26 -> ESP32

Trên CLI của xG26, người dùng gõ:

```
voice 123
```

xG26 **không** gửi nguyên chuỗi `voice 123` qua UART — nó chỉ gửi phần
số:

```
123\n
```

Ràng buộc hiện tại: chỉ chấp nhận ký tự `0`-`9`, tối đa 10 chữ số.

### 3.2 ESP32 -> xG26

| Message | Ý nghĩa |
|---|---|
| `READY\n` | ESP32 + DFPlayer đã sẵn sàng (gửi khi boot xong, **và lặp lại mỗi 2 giây** như một tín hiệu debug/keep-alive — xem `READY_INTERVAL_MS` trong code, không phụ thuộc thứ tự boot của hai board) |
| `ACK\n` | Đã nhận được lệnh hợp lệ, chuẩn bị phát |
| `DONE\n` | Đã phát xong toàn bộ chuỗi |
| `ERROR\n` | Lỗi: chuỗi rỗng, quá 10 ký tự, có ký tự không phải số, DFPlayer chưa sẵn sàng, buffer UART tràn, hoặc lỗi phát từ DFPlayer |

### 3.3 Lọc nhiễu (EMI filter)

Do dây PA04/PA05 chạy gần Zigbee/HX711 trên board xG26 nên đôi khi có
vài byte rác dính vào đầu dòng UART. `AmThanh.ino` có hàm
`filterDigitsOnly()` chạy **sau khi đã nhận đủ một dòng** (không đụng
tới logic UART/parity/framing): chỉ giữ lại ký tự `0`-`9`, bỏ mọi byte
khác, rồi mới validate/xử lý. Nếu có lọc, ESP32 in log
`[xG26] Da loc rac, con lai: ...` ra Serial USB để debug.

### 3.4 Ví dụ một phiên giao tiếp

```
ESP32 boot xong        -> xG26   READY
Người dùng CLI xG26:      voice 123
xG26 gửi qua UART      -> ESP32  123\n
ESP32 phản hồi         -> xG26   ACK
ESP32 điều khiển DFPlayer đọc lần lượt: 1, 2, 3 (mỗi số 1 file WAV)
Khi phát xong toàn bộ  -> xG26   DONE
```

Có thể test riêng ESP32 + DFPlayer (không cần xG26) bằng cách gõ thẳng
chuỗi số (vd. `123`) vào Serial Monitor (USB, 115200 baud).

## 4. Mapping số -> file WAV (thư mục `01/` trên thẻ SD)

```cpp
int digitToFileNumber(char digit) {
  if (digit >= '1' && digit <= '9') return digit - '0';
  if (digit == '0') return 10;
  return -1;
}
```

| Ký tự | File | Nội dung |
|---|---|---|
| `1` | `/01/001.wav` | "một" |
| `2` | `/01/002.wav` | "hai" |
| `3` | `/01/003.wav` | "ba" |
| `4` | `/01/004.wav` | "bốn" |
| `5` | `/01/005.wav` | "năm" |
| `6` | `/01/006.wav` | "sáu" |
| `7` | `/01/007.wav` | "bảy" |
| `8` | `/01/008.wav` | "tám" |
| `9` | `/01/009.wav` | "chín" |
| `0` | `/01/010.wav` | "không" |

Gọi qua `dfPlayer.playFolder(1, fileNumber)` (thư viện
`DFRobotDFPlayerMini`).

Thư mục `02/` (các từ/cụm từ như "cảnh báo", "nhịp tim", "nhỏ giọt"...)
**chưa được dùng trong code hiện tại** — đây là phần dự kiến mở rộng, xem
mục 6 và tài liệu
[`xg26v1/docs/AUDIO_UART_PROTOCOL.md`](../xg26v1/docs/AUDIO_UART_PROTOCOL.md#cấu-trúc-thẻ-microsd)
để biết cấu trúc đầy đủ của thẻ SD (cả 2 thư mục).

## 5. Luồng xử lý chính trong code

- `setup()`: khởi động `Serial` (USB, 115200), `dfSerial` (UART1, GPIO9/10,
  9600 8N1) rồi `dfPlayer.begin()`, cuối cùng `xg26Serial` (UART2 nhưng
  vật lý là UART0, GPIO43/44, 115200 8N1). Nếu DFPlayer không kết nối
  được, firmware **vẫn tiếp tục chạy** ở chế độ debug (giữ UART xG26
  sống) thay vì treo — gửi `ERROR` cho xG26 và set `dfPlayerReady =
  false`.
- `loop()`: gọi tuần tự, không blocking:
  - `handleUsbSerial()` — cho phép test bằng cách gõ số thẳng vào Serial
    Monitor.
  - `handleXG26Serial()` — đọc từng byte từ `xg26Serial`, tách dòng theo
    `\n`, lọc rác, gọi `startNumberSequence()`.
  - `handleDFPlayer()` — đọc sự kiện từ DFPlayer (phát xong 1 file thì
    tự động gọi `playNextDigit()` để phát số tiếp theo; nếu lỗi thì huỷ
    hàng đợi và báo `ERROR`).
  - Gửi `READY` định kỳ mỗi `READY_INTERVAL_MS` (2000 ms) — mục đích
    debug, không phụ thuộc thứ tự boot hai board.
- `startNumberSequence()` validate chuỗi (chỉ số, 1-10 ký tự), dừng
  chuỗi cũ nếu đang phát dở, rồi bắt đầu phát tuần tự từng chữ số qua
  `playNextDigit()` / `playDigit()`.

## 6. Hướng mở rộng (CHƯA implement, chỉ là kế hoạch)

Protocol hiện tại **chỉ** là chuỗi số thô `<digits>\n`. Có thể mở rộng
sau này bằng prefix, ví dụ:

```
VOICE:123\n
HR:85\n
SPO2:98\n
DROP:20\n
ALERT:2\n
```

ESP32 parse prefix để biết cần ghép câu nào, ví dụ `HR:85` ->
`/02/004.wav` ("nhịp tim") + `/01/008.wav` ("tám") + `/01/005.wav`
("năm"). Đây **không phải** code hiện có — nếu triển khai, cần cập nhật
cả `AmThanh.ino` (parser) lẫn `xg26v1/firmware/app.c` (nơi phát lệnh
`voice`) và tài liệu này.

## 7. Build / nạp firmware

Đây là sketch Arduino chuẩn, không có `platformio.ini` hay file cấu
hình build khác trong thư mục này — build bằng **Arduino IDE**:

1. Board: ESP32-S3 (chọn đúng variant board bạn dùng).
2. **USB CDC On Boot: Enabled** — bắt buộc, xem mục 2.1 (nếu để
   `Disabled`, log debug sẽ lẫn vào UART0 dùng cho xG26).
3. Thư viện cần cài qua Library Manager: `DFRobotDFPlayerMini` (tác giả
   DFRobot).
4. Mở `AmThanh/AmThanh.ino`, chọn đúng cổng COM, Upload.

Không có toolchain dòng lệnh (arduino-cli/platformio) sẵn có trong môi
trường build ở thời điểm viết tài liệu này, nên phần ESP32 **chưa được
build tự động khi chuẩn bị commit này** — cần build/nạp thủ công bằng
Arduino IDE trước khi dùng trên phần cứng thật.

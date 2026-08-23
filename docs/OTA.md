# Cập nhật firmware từ xa (OTA)

Kỹ thuật viên có thể nạp firmware mới cho thiết bị Zigbee **mà không cần tới tận
giường**. Tài liệu này gồm ba phần: hệ thống chạy thế nào, **ai làm gì**, và
**các bước cụ thể để phát hành một bản cập nhật**.

---

## 1. Đường đi của một lệnh

```
Trình duyệt (kythuat)
   │  POST /api/devices/{id}/ota/check   (quyền ManageDevices)
   ▼
HIS Server ──► gateway trên Pi  {"cmd":"ota_check","deviceId":…}
                   │
                   ▼  MQTT zigbee2mqtt/bridge/request/device/ota_update/check
              zigbee2mqtt ──► thiết bị (cluster OTA 0x0019)
                   │
                   ▼  bridge/response/device/ota_update/*  +  bản tin thiết bị
              gateway ──► server  {"type":"ota_status", …}
                   │
                   ▼  SignalR OtaStatusChanged
              thanh tiến độ trên trang kỹ thuật
```

Trạng thái OTA giữ **trong bộ nhớ server**, cố ý. Một lần nạp kéo dài vài phút và
không sống sót qua lần khởi động lại server; hiện lại “đang nạp 40%” lấy từ CSDL
cho một tiến trình đã chết còn tệ hơn là không hiện gì.

Trang vừa mở thì gọi `GET /api/devices/ota` lấy toàn bộ trạng thái đang có, nên
tải lại trang giữa lúc đang nạp vẫn thấy đúng — và **không** mọc lại nút Cập nhật.

---

## 2. Ai làm gì

| Vai | Làm được gì với OTA | Vì sao |
|---|---|---|
| **Kỹ thuật viên** (`kythuat`) | Bấm *Check for update*, bấm *Update*, theo dõi tiến độ | quyền `ManageDevices`; đây là người chịu trách nhiệm về thiết bị |
| **Y tá** (`yta`) | **Không thấy và không gọi được** các API OTA | y tá lo bệnh nhân; một cú bấm nhầm làm máy đầu giường mất theo dõi vài phút |
| **Quản trị** (`admin`) | Như kỹ thuật viên | |
| **Người dựng bản phát hành** (mục 4) | Build firmware, tạo file `.ota` | cần máy có SDK; **không** cần ssh vào Pi nữa — file tải lên qua web |

> Giao diện trang kỹ thuật viết bằng **tiếng Anh** (đồng bộ với phần còn lại
> của trang). Tài liệu này giữ tiếng Việt và có ghi kèm nhãn tiếng Anh ở những
> chỗ mô tả nút bấm.

Kỹ thuật viên **tải file `.ota` lên** qua trang web (mục 3), nhưng không chọn
được file tuỳ ý cho một thiết bị: server đọc **header của chính file** để biết nó
dành cho loại thiết bị nào, và z2m chỉ chào ảnh có mã nhà sản xuất trùng. Chọn
nhầm file `.gbl` hay `.s37` thì bị từ chối ngay lúc tải lên.

---

## 3. Thao tác trên giao diện (kỹ thuật viên)

1. Đăng nhập `kythuat / kythuat` → trang **Devices**.
2. Mỗi thẻ thiết bị có khối **FIRMWARE**. Trạng thái có thể là:

| Hiện trên thẻ | Nghĩa là | Có nút gì |
|---|---|---|
| **Not checked yet** | chưa hỏi lần nào từ khi server chạy | *Check for update* |
| **Up to date** | đã hỏi, không có bản nào mới hơn | *Check for update* |
| **Update available** | có ảnh mới hơn trong kho | *Check for update* + **Update** |
| **Starting… / Installing** | đang truyền | **không nút nào** |
| **Updated** | thiết bị đang khởi động lại vào bản mới | *Check for update* |
| **Update failed** | có kèm nguyên văn lý do | *Check for update* để thử lại |

3. Bấm **Check for update** → đợi vài giây, trạng thái tự đổi (không cần tải lại
   trang).
4. Nếu hiện **Update available** → bấm **Update** → xác nhận trong hộp thoại.
5. Trong lúc nạp: thanh tiến độ + phần trăm + ước lượng thời gian còn lại, kèm
   dòng **“Installing firmware — do not disconnect power.”**
6. Nạp xong thiết bị **tự khởi động lại** vào bản mới. Mất theo dõi khoảng vài
   chục giây — trên trang y tá giường đó sẽ báo mất tín hiệu trong lúc này.

> **Chọn giờ mà làm.** Nạp firmware là cắt theo dõi của một giường đang truyền
> dịch trong vài phút. Đừng làm khi giường đó đang có cảnh báo.

### Firmware library

Ở đầu trang Devices có khối **Firmware library (OTA)**: chọn file `.ota` →
**Upload**. Bảng bên dưới hiện mọi ảnh đang được chào, kèm **mã nhà sản xuất đọc từ
chính file** (mã lạ thì tô đỏ) và số phiên bản.

Server đọc header trước khi nhận: chọn nhầm file `.gbl`/`.s37` nằm cùng thư mục
build thì bị từ chối kèm đúng lý do, chứ không nhận rồi hỏng lúc nạp.

zigbee2mqtt lấy index **trực tiếp từ server** qua HTTP, nên tải lên xong là dùng
được ngay — không phải ssh, không phải sửa JSON bằng tay.

### Lịch sử cập nhật

Bấm vào một thiết bị → khối **Device log** ghi lại, có tô màu riêng:

| Dòng | Khi nào |
|---|---|
| `Firmware update started` — *Update started: v2 to v4* | vừa bấm Update |
| `Firmware updated` — *Updated: v2 to v4* | nạp xong |
| `Firmware update failed` — *Update failed: lý do* | hỏng giữa chừng |

Số phiên bản "đi từ đâu" được **đóng băng suốt lúc cập nhật**. Nếu không, đến
lúc báo xong thì chip đã khởi động lại và khai bản mới, và dòng lịch sử sẽ đọc
thành *"v4 → v4"* — mất đúng cái dữ kiện nó sinh ra để ghi.

**Ghi cả lần hỏng là chủ ý.** Một thiết bị phải thử ba lần mới lên được là một
thiết bị đáng ngờ; chỉ ghi lần thành công thì ba lần kia biến mất khỏi hồ sơ.

Chỉ ghi khi trạng thái **thật sự đổi** — thiết bị phát lại trạng thái OTA mỗi
giây, ghi hết thì ba dòng đáng đọc chìm dưới hàng nghìn dòng "vẫn đang rảnh".

### Những gì giao diện cố tình không cho làm

| Tình huống | Giao diện | Lý do |
|---|---|---|
| Chưa biết có bản mới | không có nút Cập nhật | mời cập nhật khi chưa biết có gì là mời mù |
| **Đang nạp** | **ẩn cả hai nút** | bấm Cập nhật lần hai = hai luồng ảnh chồng nhau = **nửa firmware trong máy đầu giường** |
| Đang nạp, chưa có % | ghi “đang chuẩn bị…” | thanh 0% không phân biệt được với thanh treo |

Server chặn độc lập, không chỉ dựa vào giao diện: `POST …/ota/update` trả **409**
nếu thiết bị đang nạp dở, **503** nếu không gateway nào đang kết nối.

---

## 4. Phát hành một bản cập nhật

### 4.1. Điều kiện thiết bị — đã xong, ghi lại để biết vì sao

Thiết bị chỉ nhận OTA khi có **cả ba**:

1. **Gecko Bootloader** đã nạp (`bootloader-storage-internal-single-3200k`),
2. các component OTA trong `smart-iv-monitor.slcp`:
   `zigbee_ota_client`, `zigbee_ota_client_policy`, `zigbee_ota_storage_simple`,
   `zigbee_ota_storage_simple_eeprom`, `bootloader_app_properties`,
   **`slot_manager`**,
3. **cluster OTA khai trong ZAP** — endpoint 1, cluster `25 (0x0019)`, phía
   *client*.

Điểm số 3 là chỗ dễ sót nhất. Thêm component thôi thì code có link vào, nhưng
zigbee2mqtt vẫn báo *“No endpoint found with OTA cluster support”*, vì cluster
không được công bố ra ngoài. Đã mất một vòng để tìm ra.

Bố cục bộ nhớ sau khi thêm bootloader (đã kiểm chứng trên board thật):

| Vùng | Địa chỉ |
|---|---|
| Bootloader | `0x8000000` – `0x8006000` |
| **Ứng dụng** | **`0x8006000`** – … (trước đây ở `0x8000000`) |
| Slot tải ảnh OTA | `0x8190000` – `0x8314000` |
| NVM3 (token, khoá mạng) | `0x8314000` – `0x831E000` |

Slot tải ảnh kết thúc **đúng** chỗ NVM3 bắt đầu. Nếu hai vùng đè nhau thì một
lần tải firmware sẽ ăn mất khoá mạng và thiết bị rơi khỏi mạng Zigbee — kiểm lại
con số này mỗi khi đổi `NVM3_DEFAULT_NVM_SIZE` hoặc `SLOT0_SIZE`.

Vì ứng dụng phải dời lên trên bootloader nên **lần đầu bắt buộc nạp qua dây**.
Từ lần sau mới OTA được.

#### Chỗ chứa ảnh tải về — hai thứ phải đi cùng nhau

`config/ota-storage-simple-eeprom-config.h` phải đặt:

```c
#define SL_ZIGBEE_AF_PLUGIN_OTA_STORAGE_SIMPLE_EEPROM_GECKO_BOOTLOADER_STORAGE_SUPPORT   USE_FIRST_SLOT
```

**và** `.slcp` phải có component `slot_manager`. Thiếu một trong hai là hỏng, và
hỏng theo kiểu khó tìm:

- Để mặc định `DO_NOT_USE_SLOTS` → client bỏ qua slot 1,58 MB, dùng cửa sổ
  `STORAGE_START..STORAGE_END` mặc định **256 KB**.
- Thiếu `slot_manager` → đoạn code tính kích thước từ slot **bị loại khỏi bản
  build**, nên đặt đúng cấu hình thôi vẫn chưa đủ.

Ảnh của dự án này 430 KB, nên hậu quả là thiết bị từ chối mọi lời chào:

```
ERROR: Next Image is too big to store (0x0006927E > 0x0003F800)
```

Còn z2m chỉ báo *"did not start/finish firmware download after being notified"* —
mô tả triệu chứng và giấu nguyên nhân. **Phải đọc log serial của chip mới ra.**

#### Slot tự dọn sau mỗi lần cập nhật

Cài xong, ảnh vừa nạp **vẫn nằm nguyên trong slot**. Firmware tự xoá nó ở lần
khởi động ngay sau đó (`ota_slot_housekeeping()` trong `firmware/app.c`), mất
khoảng **2,4 giây đo trên board thật**:

```
[OTA] Slot still holds an image, erasing (a few seconds)...
[OTA] Slot erase done
```

Chỉ chạy đúng một lần sau mỗi lần cập nhật — những lần khởi động sau thấy slot
đã trống thì bỏ qua. Làm lúc khởi động chứ không phải trước khi tải, vì trước
khi tải thì mấy giây đó cộng thẳng vào thời gian kỹ thuật viên đang ngồi nhìn.

Việc này giữ cho slot **không bao giờ còn ảnh cũ nằm lại** — chính thứ đã gây ra
sự cố ở khung dưới đây.

> ### ⚠ XOÁ SLOT TRƯỚC KHI BẬT, trên board đã từng thử OTA
>
> Chuyện đã xảy ra và mất firmware AI một lần: slot 0 **đã có sẵn một ảnh GBL
> hoàn chỉnh** từ lần thử OTA trước đó. Chừng nào còn `DO_NOT_USE_SLOTS` thì
> không ai đụng tới nó; **đúng lúc** chuyển sang dùng slot, OTA client thấy một
> ảnh đầy đủ nằm sẵn, coi như vừa tải xong, và bảo bootloader **cài đặt nó** —
> thiết bị khởi động lên chạy một firmware hoàn toàn khác.
>
> ```bash
> C=~/.silabs/slt/installs/archive/commander/commander
> $C readmem --range 0x8190000:+16          # EB 17 A6 03 = có ảnh nằm sẵn
> $C device pageerase --range 0x8190000:0x8314000
> $C flash <firmware đúng>.s37              # xoá TRƯỚC, nạp SAU
> ```

### 4.2. Tăng số phiên bản — **bắt buộc**

`config/ota-client-policy-config.h`:

```c
#define SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION   1   // tăng lên 2, 3, …
```

**Là SỐ PHIÊN BẢN, không phải kích thước file.** Câu hỏi hay gặp: *"file OTA bản
mới 90 KB mà bản đang chạy 100 KB thì có cập nhật được không?"* — **được**. z2m
so đúng một con số duy nhất:

```
fileVersion trong header của file   >   phiên bản chip đang chạy   →  chào bản mới
```

Kích thước, ngày tháng, tên file: **không cái nào được xét**. Một bản vá làm
firmware nhỏ đi vẫn cập nhật bình thường, miễn số phiên bản lớn hơn. Ngược lại,
một file to gấp đôi nhưng khai `fileVersion` bằng hoặc nhỏ hơn thì thiết bị lịch
sự từ chối và z2m báo *"đang ở bản mới nhất"*.

Quên tăng thì mọi thứ chạy đúng và kết quả là *"Đang ở bản mới nhất"* — không có
lỗi nào để mà tìm.

### 4.3. Build và tạo file `.ota`

```bash
tools/slc_generate.sh
cd cmake_gcc/build && ninja && cd ../..

C=~/.silabs/slt/installs/archive/commander/commander

# .s37 -> .gbl (định dạng bootloader hiểu)
$C gbl3 create smart_iv_v2.gbl \
     --app cmake_gcc/build/base/smart-iv-monitor.s37 \
     --device EFR32MG26B510F3200IM48

# .gbl -> .ota (định dạng Zigbee OTA)
$C ota create --upgrade-image smart_iv_v2.gbl \
     --firmware-version 2 \
     --manufacturer-id 0x1049 \
     --image-type 0 \
     --string "ICTU SmartIV-Sensor v2" \
     -o smart_iv_v2.ota
```

`--firmware-version` phải **khớp** giá trị vừa đặt ở mục 4.2, và
`--manufacturer-id 0x1049` phải khớp mã của thiết bị.

### 4.4. Kiểm tra file trước khi công bố

```bash
python3 - smart_iv_v2.ota <<'PY'
import struct, sys
d = open(sys.argv[1],'rb').read()
magic,hver,hlen,fc,mfg,itype,fver,zver = struct.unpack('<IHHHHHIH', d[:20])
print(f"magic={magic:#x} mfg={mfg} imageType={itype} fileVersion={fver} size={len(d)}")
print("header:", d[20:52].split(b'\x00')[0].decode('ascii','replace'))
PY
```

Phải ra `magic=0xbeef11e`, `mfg=4169`, và `fileVersion` đúng số mới.

> **LUẬT AN TOÀN:** ảnh được chào cho thiết bị theo **mã nhà sản xuất +
> imageType**, **không** theo tên file. Đừng bao giờ tin tên file — xem
> [`../gateway/ota/README.md`](../gateway/ota/README.md), ở đó ghi lại ba file
> tên `xg24_*` nhưng ruột là ảnh **xG26** mã 4169, tức trùng mã máy đầu giường.
> Đưa nhầm chúng vào index là **ghi đè firmware AI bằng app thử nghiệm**.

### 4.5. Đưa lên hệ thống

Vào trang kỹ thuật → **Firmware library (OTA)** → chọn file → **Upload**. Xong.

Không còn bước `scp` lên Pi và sửa `smart_iv_ota_index.json` bằng tay: server
lưu ảnh, **tự sinh index từ header của từng file**, và z2m đọc index đó qua HTTP.
Index sinh ra thì không thể mâu thuẫn với file nó trỏ tới — đúng lỗi của
`xg24_ota_index.json` bên nhánh kia (khai mã 4098 mà trỏ vào file mã 4169).

Cấu hình một lần trên Pi, trong `~/zigbee2mqtt/data/configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: http://<tên-máy-chủ>.local:5100/api/ota/index.json
  image_block_response_delay: 50
  default_maximum_data_size: 63
```

Hai endpoint `index.json` và tải ảnh để **anonymous**, vì z2m không có tài khoản
đăng nhập. Tải lên và xoá thì **không** — bỏ được file vào kho là chọn được thứ
chạy trên máy đầu giường.

### 4.6. Rồi mới tới lượt kỹ thuật viên

Vào trang Devices, bấm **Check for update** → phải thấy **Update available** →
bấm **Update** (mục 3).

### 4.7. Sau khi nạp lại chip qua dây

Hai chuyện phải làm/biết:

**a) Bảo z2m phỏng vấn lại.** z2m **nhớ đệm** danh sách cluster. Firmware mới có
thêm cluster OTA nhưng z2m vẫn tưởng không có, cho tới khi:

```bash
mosquitto_pub -h localhost -t 'zigbee2mqtt/bridge/request/device/interview' \
  -m '{"id":"0x64028ffffe641802"}'
```

Kiểm tra đã thấy chưa — phải ra `ep 1 ... out [25]`:

```bash
grep '0x64028ffffe641802' ~/zigbee2mqtt/data/database.db | python3 -c "
import sys,json
d=json.loads(sys.stdin.readline())
for ep,v in d['endpoints'].items():
    print('ep',ep,'in',v.get('inClusterList'),'out',v.get('outClusterList'))"
```

**b) Đợi qua khoảng im lặng đầu tiên.** Sau **mỗi lần khởi động**, OTA client
chờ một khoảng **ngẫu nhiên 0–255 giây** rồi mới bắt đầu chạy. Trong lúc đó thiết
bị **không đáp bất kỳ lệnh OTA nào**, và z2m chỉ báo:

```
Failed to check if OTA update available (Device didn't respond to OTA request)
```

— đọc y như thiết bị hỏng. Chip có in ra `Delaying 216 seconds before starting
OTA client`, nhưng chỉ trên serial.

Con số này **cố định trong SDK** (`MAXIMUM_RANDOM_DELAY_SECONDS_MASK = 0x00FF`
trong `ota-client.c`), **không chỉnh được** qua config. Đừng nhầm nó với
`QUERY_DELAY_MINUTES` — tham số đó chỉ quy định *khi đã chạy rồi thì bao lâu hỏi
server một lần*.

**Quy tắc thực dụng: nạp dây xong thì đợi ~5 phút rồi mới bấm Kiểm tra.**

Khoảng chờ này bản thân nó hợp lý — nó tồn tại để cả một khoa thiết bị bật lại
sau khi mất điện không cùng lúc đập vào server.

## 5. Cấu hình z2m trên Pi

Cấu hình **một lần**, trong `~/zigbee2mqtt/data/configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: http://<tên-máy-chủ>.local:5100/api/ota/index.json
  image_block_response_delay: 50
  default_maximum_data_size: 63
```

| Tham số | Vì sao |
|---|---|
| `zigbee_ota_override_index_location` | trỏ về **HIS Server**, không phải file trên Pi. Server sinh index từ header của từng ảnh đã tải lên, nên tải file qua web là z2m thấy ngay — không ssh, không sửa JSON tay |
| `default_maximum_data_size: 63` | **trần cứng**. Lớn hơn thì firmware xG26 bỏ qua khối và OTA **đứng im không báo lỗi** |
| `image_block_response_delay: 50` | giãn nhịp gửi khối, tránh làm nghẽn mạng Zigbee trong lúc nạp |

Dùng **tên mDNS** giống `gateway.service`, đừng thay bằng IP cứng — máy chủ chạy
wifi DHCP và đổi IP thường xuyên.

Kiểm tra Pi đọc được index:

```bash
curl -s http://<tên-máy-chủ>.local:5100/api/ota/index.json
```

Repo vẫn giữ `gateway/ota/smart_iv_ota_index.json` (rỗng) cho trường hợp muốn
quay về cách cũ đặt file trực tiếp trên Pi.

---

## 6. Bảng sự cố

| Hiện tượng | Nguyên nhân thường gặp |
|---|---|
| *“No endpoint found with OTA cluster support”* | firmware thiếu cluster OTA trong ZAP, **hoặc** z2m còn đệm bản phỏng vấn cũ → mục 4.7a |
| *“Device didn't respond to OTA request”* | đang trong khoảng im lặng 0–255 giây sau khởi động → mục 4.7b. Đợi ~5 phút |
| *“did not start/finish firmware download”* | chỗ chứa ảnh quá nhỏ. Đọc serial của chip, tìm dòng `Next Image is too big to store` → mục 4.1 |
| Luôn báo *“Đang ở bản mới nhất”* dù đã có file | quên tăng `FIRMWARE_VERSION` (4.2). **Không liên quan kích thước file** |
| *“OTA update or check for update already in progress”* | bấm Cập nhật khi lệnh Kiểm tra chưa xong. Kiểm tra mất tới 60 giây — đợi trạng thái đổi rồi hãy bấm |
| Kiểm tra xong nhưng luôn báo *“Đang ở bản mới nhất”* trên Zigbee2MQTT 2.x | gateway cũ chỉ đọc `updateAvailable`; Zigbee2MQTT 2.x trả `data.update_available`. Triển khai lại `gateway/main.c` mới |
| Bấm Kiểm tra trả **503** | không gateway nào đang kết nối; kiểm tra `gateway.service` và đường TCP tới cổng 5000 |
| Bấm Cập nhật trả **503** | không gateway nào đang kết nối → `docs/TRIEN_KHAI_PI.md` mục 5.1 |
| Bấm Cập nhật trả **409** | thiết bị đang nạp dở — đúng thiết kế, đợi cho xong |
| Tiến độ chạy rồi đứng im | `default_maximum_data_size` lớn hơn 63 |
| Nạp xong khởi động vào **firmware lạ** | slot còn ảnh cũ của người khác → khung cảnh báo ở mục 4.1 |
| Nạp xong thiết bị không vào lại mạng | slot tải ảnh đè lên NVM3 → kiểm bảng địa chỉ ở 4.1 |
| Boot log có `sl_zigbee_af_ota_storage_driver_read_cb() failed!` | bình thường khi slot còn trống, chưa từng tải ảnh nào |

## 7. Kiểm thử

```bash
node tools/ota_render_check.js                      # 12 phép thử giao diện
dotnet run --project server/tests/EvaluatorTests    # gồm phần OTA + lịch sử
cc -I firmware -o /tmp/oled_test tools/oled_test.c firmware/oled_display.c \
   && /tmp/oled_test                                # màn hình đầu giường
```

Các phép thử giao diện dựng thẳng HTML của thẻ thiết bị từ `devices.js`, nên
chúng bắt được đúng thứ nguy hiểm nhất: nút Cập nhật xuất hiện trong lúc đang nạp.

---

## 8. Đã chạy thật đến đâu

Một lần cập nhật từ xa trọn vẹn, đo trên phần cứng thật (v2 → v4):

| Mốc | Thời điểm |
|---|---|
| Bắt đầu tải | 0 s |
| Tải xong 100% | **862 s** (~14 phút, ảnh 436 KB) |
| Xác minh GBL xong | +54 s |
| Đếm ngược rồi tự khởi động lại | +15 s |
| Chạy lại đầy đủ ở bản mới | **+5 s** |

```
GBL passed verification.
Custom verification passed: 0x00
Countdown to upgrade: 3000 ms
Reset info: 0x06 ( SW)
=== Smart IV - AI module ready (firmware v4) ===
[OTA] Slot still holds an image, erasing (a few seconds)...
[OTA] Slot erase done
```

**Thiết bị tự khởi động lại và tự chạy lại — không phải rút nguồn.** Nếu phải rút
nguồn thì đó là sự cố, không phải hành vi bình thường.

Một lần thử trước đó (v3) thất bại với `INVALID_IMAGE` sau khi tải đủ 100%: chip
tự kiểm ảnh, thấy hỏng, **từ chối cài và giữ nguyên firmware đang chạy** — đúng
hành vi an toàn cần có. Nguyên nhân lần đó **chưa xác định được**; lần chạy ngay
sau với gần như cùng mã nguồn thì qua sạch, nên chưa tái hiện được. Nếu gặp lại:
**đọc slot trước khi xoá** (`readmem --range 0x8190000:+64`) để giữ bằng chứng.

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
| **Kỹ thuật viên** (`kythuat`) | Bấm *Kiểm tra bản mới*, bấm *Cập nhật*, theo dõi tiến độ | quyền `ManageDevices`; đây là người chịu trách nhiệm về thiết bị |
| **Y tá** (`yta`) | **Không thấy và không gọi được** các API OTA | y tá lo bệnh nhân; một cú bấm nhầm làm máy đầu giường mất theo dõi vài phút |
| **Quản trị** (`admin`) | Như kỹ thuật viên | |
| **Người dựng bản phát hành** (mục 4) | Build firmware, tạo file `.ota`, đưa vào index trên Pi | việc này **không** làm qua giao diện web — cần cầm máy build và quyền ssh vào Pi |

Ranh giới quan trọng: **giao diện chỉ chọn “nạp bản đã được duyệt”, không chọn
“nạp file nào”.** Người dùng web không thể trỏ thiết bị vào một file firmware
tuỳ ý. Muốn có bản mới thì phải qua mục 4, có kiểm tra mã nhà sản xuất.

---

## 3. Thao tác trên giao diện (kỹ thuật viên)

1. Đăng nhập `kythuat / kythuat` → trang **Devices**.
2. Mỗi thẻ thiết bị có khối **FIRMWARE**. Trạng thái có thể là:

| Hiện trên thẻ | Nghĩa là | Có nút gì |
|---|---|---|
| **Chưa kiểm tra** | chưa hỏi lần nào từ khi server chạy | *Kiểm tra bản mới* |
| **Đang ở bản mới nhất** | đã hỏi, không có bản nào mới hơn | *Kiểm tra bản mới* |
| **Có bản cập nhật** | có ảnh mới hơn trong index | *Kiểm tra bản mới* + **Cập nhật** |
| **Đang bắt đầu… / Đang nạp firmware** | đang truyền | **không nút nào** |
| **Đã cập nhật xong** | thiết bị đang khởi động lại vào bản mới | *Kiểm tra bản mới* |
| **Cập nhật thất bại** | có kèm nguyên văn lý do | *Kiểm tra bản mới* để thử lại |

3. Bấm **Kiểm tra bản mới** → đợi vài giây, trạng thái tự đổi (không cần tải lại
   trang).
4. Nếu hiện **Có bản cập nhật** → bấm **Cập nhật** → xác nhận trong hộp thoại.
5. Trong lúc nạp: thanh tiến độ + phần trăm + ước lượng thời gian còn lại, kèm
   dòng **“Đang nạp firmware — đừng rút nguồn thiết bị.”**
6. Nạp xong thiết bị **tự khởi động lại** vào bản mới. Mất theo dõi khoảng vài
   chục giây — trên trang y tá giường đó sẽ báo mất tín hiệu trong lúc này.

> **Chọn giờ mà làm.** Nạp firmware là cắt theo dõi của một giường đang truyền
> dịch trong vài phút. Đừng làm khi giường đó đang có cảnh báo.

### Những gì giao diện cố tình không cho làm

| Tình huống | Giao diện | Lý do |
|---|---|---|
| Chưa biết có bản mới | không có nút Cập nhật | mời cập nhật khi chưa biết có gì là mời mù |
| **Đang nạp** | **ẩn cả hai nút** | bấm Cập nhật lần hai = hai luồng ảnh chồng nhau = **nửa firmware trong máy đầu giường** |
| Đang nạp, chưa có % | ghi “đang chuẩn bị…” | thanh 0% không phân biệt được với thanh treo |

Server chặn độc lập, không chỉ dựa vào giao diện: `POST …/ota/update` trả **409**
nếu thiết bị đang nạp dở, **503** nếu không gateway nào đang kết nối.

---

## 4. Phát hành một bản cập nhật (người build làm, không qua web)

### 4.1. Điều kiện thiết bị — đã xong, ghi lại để biết vì sao

Thiết bị chỉ nhận OTA khi có **cả ba**:

1. **Gecko Bootloader** đã nạp (`bootloader-storage-internal-single-3200k`),
2. các component OTA trong `smart-iv-monitor.slcp`:
   `zigbee_ota_client`, `zigbee_ota_client_policy`, `zigbee_ota_storage_simple`,
   `zigbee_ota_storage_simple_eeprom`, `bootloader_app_properties`,
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

### 4.2. Tăng số phiên bản — **bắt buộc**

`config/ota-client-policy-config.h`:

```c
#define SL_ZIGBEE_AF_PLUGIN_OTA_CLIENT_POLICY_FIRMWARE_VERSION   1   // tăng lên 2, 3, …
```

z2m chỉ chào ảnh có `fileVersion` **lớn hơn** phiên bản thiết bị đang chạy. Quên
tăng thì mọi thứ chạy đúng và kết quả là *“Đang ở bản mới nhất”* — không có lỗi
nào để mà tìm.

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

### 4.5. Đưa lên Pi

```bash
scp smart_iv_v2.ota iotchallenge@raspberrypi.local:~/zigbee2mqtt/data/ota/

ssh iotchallenge@raspberrypi.local
cat > ~/zigbee2mqtt/data/smart_iv_ota_index.json <<'JSON'
[
  {
    "url": "ota/smart_iv_v2.ota",
    "manufacturerCode": 4169,
    "imageType": 0,
    "fileVersion": 2
  }
]
JSON
sudo systemctl restart zigbee2mqtt
```

Nhớ cập nhật cả `gateway/ota/smart_iv_ota_index.json` trong repo cho khớp — repo
là nguồn sự thật, Pi chỉ là bản triển khai.

### 4.6. Rồi mới tới lượt kỹ thuật viên

Vào trang Devices, bấm **Kiểm tra bản mới** → phải thấy **Có bản cập nhật** →
bấm **Cập nhật** (mục 3).

### 4.7. Nếu thiết bị vừa được nạp lại qua dây

z2m **nhớ đệm** danh sách cluster từ lần phỏng vấn trước. Firmware mới có thêm
cluster OTA nhưng z2m vẫn tưởng không có, cho tới khi phỏng vấn lại:

```bash
mosquitto_pub -h localhost -t 'zigbee2mqtt/bridge/request/device/interview' \
  -m '{"id":"0x64028ffffe641802"}'
```

Kiểm tra đã thấy chưa:

```bash
grep '0x64028ffffe641802' ~/zigbee2mqtt/data/database.db | python3 -c "
import sys,json
d=json.loads(sys.stdin.readline())
for ep,v in d['endpoints'].items():
    print('ep',ep,'in',v.get('inClusterList'),'out',v.get('outClusterList'))"
```

Phải thấy `ep 1 ... out [25]`.

---

## 5. Cấu hình z2m trên Pi

`~/zigbee2mqtt/data/configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: smart_iv_ota_index.json
  image_block_response_delay: 50
  default_maximum_data_size: 63
```

`63` là **trần cứng**, không phải con số chỉnh cho vui: firmware xG26 bỏ qua khối
lớn hơn, và OTA sẽ **đứng im không báo lỗi**.

---

## 6. Bảng sự cố

| Hiện tượng | Nguyên nhân thường gặp |
|---|---|
| *“No endpoint found with OTA cluster support”* | firmware thiếu cluster OTA trong ZAP, **hoặc** z2m còn đệm bản phỏng vấn cũ → mục 4.7 |
| Luôn báo *“Đang ở bản mới nhất”* dù đã có file | quên tăng `FIRMWARE_VERSION` (4.2), hoặc `fileVersion` trong index không lớn hơn bản đang chạy |
| Bấm Cập nhật trả **503** | không gateway nào đang kết nối → xem `docs/TRIEN_KHAI_PI.md` mục 5.1 |
| Bấm Cập nhật trả **409** | thiết bị đang nạp dở — đúng như thiết kế, đợi cho xong |
| Tiến độ chạy rồi đứng im | `default_maximum_data_size` lớn hơn 63 |
| Nạp xong thiết bị không vào lại mạng | slot tải ảnh đè lên NVM3 → kiểm bảng địa chỉ ở 4.1 |
| Boot log có `sl_zigbee_af_ota_storage_driver_read_cb() failed!` | bình thường khi slot còn trống, chưa từng tải ảnh nào |

---

## 7. Kiểm thử

```bash
node tools/ota_render_check.js                      # 12 phép thử giao diện
dotnet run --project server/tests/EvaluatorTests    # gồm 11 phép thử OTA
```

Các phép thử giao diện dựng thẳng HTML của thẻ thiết bị từ `devices.js`, nên
chúng bắt được đúng thứ nguy hiểm nhất: nút Cập nhật xuất hiện trong lúc đang nạp.

Đã kiểm chứng trên phần cứng thật: sau khi nạp bootloader + firmware có OTA
client, thiết bị **giữ nguyên token và tự vào lại mạng**, ba model AI và màn hình
OLED vẫn chạy, và lệnh Kiểm tra từ trang kỹ thuật trả về **`UpToDate`** thay vì
báo lỗi. Chưa thực hiện một lần truyền ảnh thật, vì index đang cố ý để rỗng.

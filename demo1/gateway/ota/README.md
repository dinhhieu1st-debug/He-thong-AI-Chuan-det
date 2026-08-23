# Ảnh firmware OTA và file index

```
images/*.ota                ảnh firmware (KHÔNG đưa vào git — xem dưới)
smart_iv_ota_index.json     index dạng file, để trống, chỉ dùng khi cần lùi
```

## Bây giờ index nằm ở đâu

**HIS Server sinh index, không phải file này.** Kỹ thuật viên tải `.ota` lên
qua trang web; server đọc header của từng file và tự dựng
`/api/ota/index.json`, còn zigbee2mqtt trỏ thẳng vào đó qua HTTP.

Đổi như vậy vì cách cũ — `scp` file lên Pi rồi sửa JSON bằng tay — là việc không
ai làm được từ ngoài phòng bệnh, và sửa index bằng tay chính là chỗ một mã nhà
sản xuất sai lọt vào. Index sinh ra từ header thì **không thể mâu thuẫn với file
nó trỏ tới**.

`smart_iv_ota_index.json` giữ lại và để rỗng `[]`, phòng khi muốn quay về cách
đặt file trực tiếp trên Pi.

Ảnh `.ota` **không đưa vào git** (`.gitignore`): mỗi bản phát hành ~430 KB, dựng
lại được từ đúng commit của nguồn (xem `docs/OTA.md` mục 4.3), và không ai đọc
diff của một file nhị phân bao giờ.

---

## LUẬT AN TOÀN SỐ 1: khớp mã nhà sản xuất, đừng tin tên file

Một ảnh OTA được chào cho thiết bị dựa trên **mã nhà sản xuất** và **imageType**,
không dựa vào tên file. Ảnh sai chip mà lọt vào index thì z2m sẽ vui vẻ chào nó.

Thiết bị của dự án này:

| Thiết bị | Mã nhà sản xuất | Ghi chú |
|---|---|---|
| **SmartIV-Sensor (xG26)** | **4169 = `0x1049`** | thiết bị **đang chạy thật ở giường** |
| xG24 (board thử) | 4098 = `0x1002` | dùng để thử OTA |

### Đã suýt sai một lần — đọc kỹ đoạn này

Trên nhánh `Xg26-SmartIV-Full` có sẵn vài ảnh OTA, và **tên file của chúng nói
dối**. Đọc header thật:

| File | Mã trong header | Chuỗi trong header | Thật sự dành cho |
|---|---|---|---|
| `xg24_ota_client_v2.ota` | 4098 | "FPT xG24 OTA client v2" | xG24 |
| `xg24_uploaded_v2.ota` | **4169** | "FPT **xG26** OTA client v2" | **xG26** |
| `xg24_uploaded_v3.ota` | **4169** | "FPT xG26 OTA client v3" | **xG26** |
| `xg24_uploaded_v4.ota` | **4169** | "FPT xG26 OTA client v4" | **xG26** |

Ba file mang tên `xg24_*` thật ra là ảnh **xG26**. Và `xg24_ota_index.json` khai
`manufacturerCode: 4098` trong khi trỏ vào một file có header ghi `4169` — index
và file mâu thuẫn nhau.

**Hệ quả nếu đưa chúng vào index của ta:** mã `4169` trùng đúng mã của
`SmartIV-Sensor`, nên z2m sẽ chào ảnh đó cho thiết bị đầu giường và **ghi đè
firmware AI bằng ứng dụng thử nghiệm**. Mất toàn bộ ba model, luật lâm sàng và
màn hình OLED, ngay giữa buổi demo.

Vì vậy index ở đây **chỉ liệt kê ảnh dựng từ chính firmware của dự án này**.

### Kiểm tra một file OTA trước khi thêm vào index

```bash
python3 - <<'PY'
import struct, sys
d = open(sys.argv[1] if len(sys.argv)>1 else 'images/x.ota','rb').read()
magic, hver, hlen, fc, mfg, itype, fver, zver = struct.unpack('<IHHHHHIH', d[:20])
print(f"magic={magic:#x} mfg={mfg} ({mfg:#x}) imageType={itype} fileVersion={fver}")
print("header:", d[20:52].split(b'\x00')[0].decode('ascii','replace'))
PY
```

`magic` phải là `0x0beef11e`. `mfg` phải khớp thiết bị bạn định cập nhật.

---

## Điều kiện tiên quyết trên thiết bị

OTA không tự có. Thiết bị phải có **cả hai**:

1. **Gecko Bootloader** đã nạp (có sẵn ở
   `../bootloader-storage-internal-single-3200k/artifact/`).
2. **Thành phần OTA client** trong ứng dụng Zigbee.

Firmware `smart-iv-monitor` hiện **chưa có** thành phần OTA client — kiểm tra
bằng `grep -i ota smart-iv-monitor.slcp`. Thêm nó vào là đổi bố cục bộ nhớ (ứng
dụng phải dời lên trên bootloader), nên **phải nạp qua dây một lần** trước khi
OTA dùng được lần đầu.

---

## Slot tải ảnh có thể chứa sẵn firmware của người khác — XOÁ TRƯỚC KHI BẬT

Đã xảy ra thật, và mất firmware AI một lần.

Bật `USE_FIRST_SLOT` cho OTA storage xong nạp lại chip, thiết bị khởi động lên
chạy **một firmware hoàn toàn khác**: `[BOOT] Smart IV firmware v4 - nhay 4 lan`,
một autoencoder 6 đặc trưng — tức bản AI **đời cũ**, không phải ba model hiện tại.

Nguyên nhân: slot 0 **đã có sẵn một ảnh GBL hoàn chỉnh** từ những lần thử OTA
trước đó (đọc `0x8190000` thấy `EB 17 A6 03` = magic GBL). Chừng nào OTA storage
còn đặt `DO_NOT_USE_SLOTS` thì không ai đụng tới nó. Ngay khi chuyển sang dùng
slot, OTA client thấy một ảnh đầy đủ và hợp lệ nằm đó, coi như **vừa tải xong**,
và bảo bootloader cài đặt.

Đúng cái bẫy mã nhà sản xuất ở mục trên, nhưng đến từ **bộ nhớ của chính thiết
bị** chứ không phải từ index — nên không có luật nào ở phía server chặn được.

**Trước khi bật OTA storage theo slot trên một board đã từng dùng để thử OTA,
xoá slot:**

```bash
C=~/.silabs/slt/installs/archive/commander/commander

$C readmem --range 0x8190000:+16      # EB 17 A6 03 = có ảnh nằm sẵn
$C device pageerase --range 0x8190000:0x8314000
$C readmem --range 0x8190000:+16      # phải toàn FF
$C flash <firmware đúng>.s37
```

Xoá slot **trước** khi nạp firmware, không phải sau: nạp trước thì thiết bị khởi
động, OTA client lại thấy ảnh cũ và cài đè lần nữa.

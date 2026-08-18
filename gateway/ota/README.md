# Ảnh firmware OTA và file index

Thư mục này là **nguồn sự thật** cho những gì zigbee2mqtt được phép chào cho
thiết bị. Nội dung được triển khai sang `~/zigbee2mqtt/data/` trên Pi.

```
smart_iv_ota_index.json     danh sách ảnh z2m được phép chào
images/*.ota                ảnh firmware
```

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

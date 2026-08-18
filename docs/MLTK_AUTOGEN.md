# Từ file `.tflite` tới chip: luồng sinh code bằng tool MLTK của Silicon Labs

Tài liệu này giải thích **model đi từ Python lên chip bằng đường nào**, vì sao
nhóm chọn đường đó, và **hai cái bẫy** đã mất thời gian để tìm ra.

Người đọc mục tiêu: thành viên nhóm cần sửa model, hoặc người chấm muốn kiểm tra
rằng số liệu trong báo cáo đúng là số liệu của model đang chạy trên chip.

---

## 1. Luồng tổng thể

```
ml/train_*.py                 huấn luyện, xuất int8
      │
      ▼
ml/models/*.tflite            3 file, đây là "nguồn sự thật"
      │
      ▼
ml/export_c_headers.py        gọi TOOL CỦA SILICON LABS, 3 lần
      │
      ▼
firmware/models/              model_drip.{c,h}  model_vitals.{c,h}  model_ae.{c,h}
                              + model_*_opcodes.h   ← phần đáng giá nhất
      │
      ▼
firmware/ai_engine.cpp        3 interpreter, 3 arena
```

Sinh lại toàn bộ:

```bash
.venv-ai/bin/python ml/export_c_headers.py
```

---

## 2. Vì sao không tự viết mảng byte cho nhanh

Xuất `const uint8_t model[] = {0x1c, 0x00, ...}` là mười dòng Python. Lý do gọi
tool của hãng thay vì tự viết nằm ở **file thứ hai** mà nó sinh ra:
`sl_tflite_micro_opcode_resolver.h`. Nội dung file này được suy ra bằng cách
**phân tích chính flatbuffer**, nên nó liệt kê đúng những operator model thật sự
dùng, và khai báo đúng kích thước:

```c
// sinh cho model dự báo (drip / vitals)
#define DRIP_OPCODE_RESOLVER(r) \
static tflite::MicroMutableOpResolver<3> r; \
r.AddConv2D(); \
r.AddReshape(); \
r.AddFullyConnected();

// sinh cho autoencoder — tool tự biết nó chỉ cần 1 operator
#define AE_OPCODE_RESOLVER(r) \
static tflite::MicroMutableOpResolver<1> r; \
r.AddFullyConnected();
```

Resolver viết tay chính là chỗ loại dự án này mục ruỗng. Đổi kiến trúc model,
quên thêm operator mới, thì firmware **hỏng ở `AllocateTensors`** — tức là hỏng
**ở đầu giường bệnh nhân, không phải lúc build**. Sinh resolver từ chính model
làm cho loại lỗi đó **không thể xảy ra**.

Ngoài ra: không phải commit mảng byte viết tay, và không bao giờ có chuyện file
header lệch phiên bản với file `.tflite`.

---

## 3. Vì sao gọi tool trực tiếp thay vì qua `config/tflite/`

Simplicity Studio tự chạy converter này cho **bất kỳ** file `.tflite` nào đặt
trong `config/tflite/`. Đó là đường đi chuẩn, và dự án sẽ dùng nó **nếu chỉ có
một model**.

Dự án có **ba**. Đọc mã nguồn tool
(`aiml_2.2.2/tool/flatbuffer-converter/flatbuffer_converter.py`) thì thấy hàm
`find_first_tflite_file()` — nó lấy đúng **file đầu tiên** nó tìm thấy, hai file
kia bị bỏ qua trong im lặng.

Vì vậy `ml/export_c_headers.py` gọi thẳng tool đó, **ba lần**, mỗi lần một model,
rồi đổi tên symbol cho khỏi đụng nhau:

| Tool sinh ra | Đổi thành |
|---|---|
| `sl_tflite_model_array` | `g_drip_model_array` / `g_vitals_model_array` / `g_ae_model_array` |
| `SL_TFLITE_MICRO_OPCODE_RESOLVER` | `DRIP_OPCODE_RESOLVER` / `VITALS_OPCODE_RESOLVER` / `AE_OPCODE_RESOLVER` |

Code sinh ra **giống hệt** thứ Studio sẽ sinh, chỉ là ba lần với ba bộ tên khác
nhau.

---

## 4. Hai bản vá cho tool của hãng

Cả hai đều áp ở phía mình (`ml/export_c_headers.py`), **không sửa gì trong SDK**.

**1. Shim `imp`.** Thư viện `flatbuffers` mà Silicon Labs đóng gói kèm tool vẫn
còn `import imp` — module đã bị gỡ khỏi Python 3.12. Nó chỉ gọi đúng một hàm,
`imp.find_module('numpy')`, nên chèn một module giả có mỗi hàm đó là đủ.

**2. Thêm `sys.path`**, vì tool không phải package đã cài.

Phụ thuộc Python của chính tool: `pyyaml`, `jinja2`, `numpy`.

---

## 5. CÁI BẪY THỨ NHẤT: ZAP dùng bản cache CŨ của cluster tuỳ biến

**Triệu chứng:** chạy `slc generate` xong thì build gãy với

```
error: 'ZCL_TS_FLAGS_ATTRIBUTE_ID' undeclared
```

`autogen/zap-id.h` sinh ra thiếu đúng **7 dòng** — 7 attribute ZCL mà phần AI v2
thêm vào.

**Nguyên nhân — không phải chỗ ai cũng đoán.** File `.zap` **có** trong repo, có
khai báo `smart-iv-vitals.xml` dạng `zcl-xml-standalone`, và bản thân XML **có
đủ 22 attribute**. Cả hai đều đúng.

Vấn đề nằm ở **cache của ZAP**. ZAP lưu các package ZCL đã nạp vào
`~/.zap/generate.sqlite`, đánh khoá theo **đường dẫn**. Khi
`config/zcl/smart-iv-vitals.xml` được sửa để thêm 7 attribute, ZAP vẫn dùng bản
đã cache — **dù CRC đã khác**:

```
pkg 238   crc  168642456   attr 15   /…/smart-iv-monitor/config/zcl/smart-iv-vitals.xml   ← CŨ, được dùng
pkg 239   crc 1295804267   attr 22   config/zcl/smart-iv-vitals.xml                        ← ĐÚNG, bị bỏ qua
```

(CRC thật của file hiện tại là `1295804267` — tức bản 22 attribute.)

Không có cảnh báo nào. ZAP chạy sạch, báo "Generation time: 8s", chỉ là nó dùng
**định nghĩa cluster của tháng trước**.

**Cách sửa — dùng script, đừng gọi `slc generate` trần:**

```bash
tools/slc_generate.sh
```

Script tự tìm đường dẫn SDK và extension aiml (mã băm khác nhau trên từng máy),
**xoá bản cache cũ của XML**, sinh code, rồi **kiểm tra lại đủ 9/9 attribute** và
dừng ngay với thông báo rõ ràng nếu thiếu — thay vì để lỗi lộ ra ở bước biên
dịch.

Script đã được thử với một attribute **thật sự mới** (`RemainingMl` 0x16 và
`RemainingMin` 0x17, thêm vào ngày 18/08): sinh code chạy thẳng, không phải
`git checkout` lần nào. Thêm attribute mới thì nhớ bổ sung tên nó vào danh sách
kiểm tra trong script, nếu không lần sinh sau mất nó mà không ai biết.

Nếu vẫn thiếu, xoá thẳng cache (ZAP tự dựng lại):

```bash
rm ~/.zap/generate.sqlite
```

> Cách chữa cũ là `git checkout autogen/zap-id.h autogen/zap-config.h` sau mỗi
> lần sinh. Cách đó chỉ giấu triệu chứng, và sẽ hỏng ngay khi có người thật sự
> cần thêm một attribute mới. Script trên chữa đúng nguyên nhân.

---

## 6. CÁI BẪY THỨ HAI: `MicroPrintf` không hỗ trợ `%f`

TFLM tự cài một formatter tí hon riêng, **không có dấu phẩy động**. Dòng log

```c
MicroPrintf("... in scale %.6f zp %d", scale, zero_point);   // SAI
```

in ra trên chip:

```
[AI] drip ready: arena 2836/4096 B, in scale .6f zp 1610612736
```

Chuỗi `.6f` được in **nguyên văn**, đối số float **không bị tiêu thụ**, nên mọi
specifier sau đó đọc nhầm ô — zero-point in ra là **rác trông y hệt số liệu
thật**. Loại lỗi này nguy hiểm vì nó không crash, không cảnh báo, chỉ lặng lẽ cho
ra số sai trong đúng cái log mà ta dùng để kiểm tra.

Cách làm đúng: quy về số nguyên.

```c
MicroPrintf("... in scale %u/1e6, zp %d",
            (unsigned)(scale * 1000000.0f + 0.5f), zero_point);
```

---

## 7. Vì sao TẮT `SL_TFLITE_MICRO_INTERPRETER_INIT_ENABLE`

Đặt `0` trong `config/sl_tflite_micro_config.h`. **Đừng bật lại.**

Khi bật, `sl_tflite_micro_init()` dựng interpreter lúc khởi động hệ thống và xử
lý **mọi lỗi** bằng vòng lặp vô hạn:

```c
if (model->version() != TFLITE_SCHEMA_VERSION) { ...; while (1); }
if (interpreter->AllocateTensors() != kTfLiteOk) { ...; while (1); }
```

Hàm này được gọi từ `autogen/sl_event_handler.c` **trước cả app**. Nên một model
lỗi không làm hệ thống suy giảm chức năng — nó làm **treo máy**: không cảm biến,
không OLED, không Zigbee, và **không cả luật lâm sàng**. Với thiết bị theo dõi
bệnh nhân, treo im lặng là kiểu hỏng tệ nhất có thể có.

Nó cũng không phục vụ được dự án này: nó tạo **một** interpreter, mà ở đây có
**ba**. `firmware/ai_engine.cpp` tự dựng cả ba và báo lỗi bằng `return false`,
để luật lâm sàng gánh thiết bị.

---

## 8. Kiểm chứng chuỗi công cụ còn nguyên vẹn

Hai phép kiểm tra, cả hai đều nên chạy sau khi đổi model.

**a) Mảng byte khớp từng byte với file `.tflite`:**

```bash
.venv-ai/bin/python - <<'EOF'
import re, pathlib
for stem, tfl in (('drip','drip_forecaster_int8'),
                  ('vitals','vitals_forecaster_int8'),
                  ('ae','vitals_ae_int8')):
    c = pathlib.Path(f'firmware/models/model_{stem}.c').read_text()
    body = c[c.index('{')+1:c.rindex('}')]
    arr = bytes(int(t,16) for t in re.findall(r'0x[0-9a-fA-F]{2}', body))
    ref = pathlib.Path(f'ml/models/{tfl}.tflite').read_bytes()
    print(f'{stem:7s} {len(arr):6d} B  khớp: {arr == ref}')
EOF
```

**b) Chip đang chạy đúng model đã huấn luyện.** Sau khi nạp, đọc log khởi động:

```
[AI] drip   ready: arena 2836/4096 B, in scale 39214/1e6, zp -55
[AI] vitals ready: arena 2868/4096 B, in scale 76469/1e6, zp  49
[AI] ae     ready: arena 1108/2048 B, in scale 66618/1e6, zp  75
```

`scale` và `zero-point` chip in ra phải **khớp tuyệt đối** với tham số lượng tử
hoá trong file `.tflite`. Đây là phép kiểm tra đầu-cuối duy nhất chứng minh
không có model nào bị lệch phiên bản giữa Python và chip — và nó đã được chạy:
cả ba khớp.

Con số arena cũng cho biết kích thước đặt trong `ai_engine.cpp` có đủ không:
hiện dùng 6.812 B trên 10.240 B cấp phát, dư 33%.

---

## 9. Ngân sách bộ nhớ hiện tại

| | Byte | |
|---|---:|---|
| RAM tĩnh (`.data` + `.bss`) | 32.504 | **6,2%** của 512 KB |
| Ba arena AI | 10.240 | 2,0% RAM |
| Flash (`text`) | 381.364 | trong đó 3 model chiếm 49.056 B |

> **Đọc số `size` cho đúng.** `arm-none-eabi-size` báo `bss` = 522.440 B, trông
> như sắp tràn 512 KB. Không phải: `.memory_manager_heap` là `NOLOAD` và **chiếm
> toàn bộ RAM còn lại** theo linker script, nên cả heap bị cộng vào `bss`. Phần
> tĩnh thật chỉ 32,5 KB.

---

## 10. Lệnh build đầy đủ

SDK và extension `aiml` phải truyền tường minh — `slc` không tự tìm ra:

```bash
SDK=~/.silabs/slt/installs/conan/p/simpl35774a752829c/p
AIML=~/.silabs/slt/installs/conan/p/aiml220b56d6ae053/p
SLC=~/.silabs/slt/installs/archive/slc-cli-v6.0.20/slc_cli/slc
NINJA=$(ls ~/.silabs/slt/installs/conan/p/ninja*/p/ninja | head -1)
COMMANDER=~/.silabs/slt/installs/archive/commander/commander

tools/slc_generate.sh          # tự xử lý cache ZAP + kiểm tra, xem mục 5
cd cmake_gcc/build && $NINJA
$COMMANDER flash base/smart-iv-monitor.hex --serialno <SERIAL_BOARD>
```

Đường dẫn SDK có mã băm (`simpl35774a752829c`) khác nhau trên từng máy; tra bằng:

```bash
python3 -c "import json;print([e['path'] for s in json.load(open('$HOME/.silabs/sdks.json')) for e in s['extensions'] if e['id']=='simplicity-sdk'])"
```

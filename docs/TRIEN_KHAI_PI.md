# Triển khai xuống Raspberry Pi — và ba kiểu hỏng đã gặp thật

Hai file trong repo này **không chạy trên máy bạn**. Chúng chạy trên Pi:

| File trong repo | Chạy ở đâu trên Pi |
|---|---|
| `gateway/main.c` | biên dịch thành `/home/iotchallenge/gateway_test` |
| `gateway/zigbee2mqtt_smart_iv_converter.js` | `~/zigbee2mqtt/data/external_converters/` |

> **Sửa chúng trong repo là CHƯA XONG VIỆC.** Hệ thống vẫn chạy bản cũ cho tới
> khi bạn copy sang, biên dịch lại và khởi động lại dịch vụ.

Đây không phải cảnh báo lý thuyết. Nhóm đã mắc đúng lỗi này: thêm `alert_level`
vào firmware và gateway, test qua đường serial thấy chạy tốt, rồi đinh ninh là
xong. Thực tế đường Zigbee vẫn chạy binary cũ suốt nhiều ngày, server luôn coi
board là **thiết bị đời cũ**, và hệ quả là **mọi sự cố đường truyền bị đẩy lên
mức đỏ** — đúng thứ bản v2 sinh ra để sửa.

---

## 1. Thông tin kết nối

```bash
ssh iotchallenge@raspberrypi.local        # mật khẩu: iotchallenge
```

`raspberrypi.local` phân giải qua mDNS, nên **không phụ thuộc IP**. Điều đó quan
trọng: máy chạy HIS Server dùng wifi DHCP và đã đổi IP ba lần trong hai ngày.
`gateway.service` trên Pi cũng trỏ vào **tên mDNS của máy chủ**, không phải IP —
đừng đổi nó thành IP cứng.

---

## 2. Triển khai gateway (`main.c`)

```bash
scp gateway/main.c iotchallenge@raspberrypi.local:/home/iotchallenge/gateway_main.c

ssh iotchallenge@raspberrypi.local '
  cd /home/iotchallenge
  cp gateway_test gateway_test.bak.$(date +%H%M)      # luôn giữ bản lùi
  gcc -Wall -Wextra -O2 -o gateway_test.new gateway_main.c -lmosquitto
  sudo systemctl stop gateway
  mv gateway_test.new gateway_test
  sudo systemctl start gateway
  systemctl is-active gateway
'
```

Kiểm tra nó thật sự nhận được dữ liệu:

```bash
ssh iotchallenge@raspberrypi.local 'journalctl -u gateway -f'
```

Phải thấy `MQTT message received` đều đặn. **Im lặng nghĩa là hỏng** — xem mục 5.

---

## 3. Triển khai converter zigbee2mqtt

```bash
scp gateway/zigbee2mqtt_smart_iv_converter.js \
    iotchallenge@raspberrypi.local:/home/iotchallenge/zigbee2mqtt/data/external_converters/

ssh iotchallenge@raspberrypi.local 'sudo systemctl restart zigbee2mqtt'
```

Xác nhận z2m nạp đúng file:

```bash
ssh iotchallenge@raspberrypi.local \
  "journalctl -u zigbee2mqtt --since '2 min ago' | grep -i 'external converter'"
```

### Coi chừng giá trị cache

z2m bật `cache_state: true` và **gộp trạng thái đã lưu** vào mỗi bản tin. Sau khi
đổi converter, các trường cũ vẫn hiện giá trị do bản converter **cũ** tính ra,
cho tới khi thiết bị gửi báo cáo mới cho attribute đó.

Nhóm đã mất thời gian vì chuyện này: sửa xong vẫn thấy giá trị sai, tưởng bản vá
không ăn. Ép đọc lại từ thiết bị:

```bash
mosquitto_pub -h localhost -t 'zigbee2mqtt/SmartIV-Sensor/get' -m '{"drop_rate":""}'
```

---

## 4. Kiểm chứng cả chuỗi sau khi triển khai

Chạy từng chặng, dừng ở chặng đầu tiên sai:

```bash
# 1. Chip có báo cáo không?
#    (trên máy có board) đọc /dev/ttyACM0, tìm dòng [JSON]

# 2. z2m có nhận và giải mã không?
ssh iotchallenge@raspberrypi.local \
  "timeout 30 mosquitto_sub -h localhost -t 'zigbee2mqtt/SmartIV-Sensor' -C 1"
#    phải thấy: alert_level, line_state, remaining_ml, remaining_min

# 3. gateway có chuyển tiếp không?
ssh iotchallenge@raspberrypi.local "journalctl -u gateway -n 20"

# 4. gateway có kết nối lên server không? (chạy trên máy chủ)
ss -tn | grep :5000

# 5. server có hiểu không?
curl -s -b cookie.txt http://localhost:5100/api/beds | python3 -m json.tool | grep -i alertlevel
```

---

## 5. Ba kiểu hỏng đã gặp thật

### 5.1. zigbee2mqtt "active" nhưng đã chết

**Triệu chứng:** không gói MQTT nào, gateway không kết nối lên server, bên kỹ
thuật **rescan không thấy thiết bị nào**. Nhưng `systemctl status zigbee2mqtt`
báo `active (running)` — suốt 16 tiếng.

**Nguyên nhân:** board NCP bị rút, zigbee-herdsman lỗi
(`Failed to init port: No such file or directory`), và tiến trình node **treo
thay vì thoát**. `Restart=on-failure` không cứu được vì nó không thoát.

**Vì sao rescan im lặng:** rescan phát lệnh xuống **các gateway đang kết nối**.
Không gateway nào kết nối thì không ai nhận lệnh, và thiết bị đã xoá sẽ không bao
giờ quay lại.

**Đã sửa:** `Restart=always` + một health-check chạy mỗi 2 phút.

```
/etc/systemd/system/zigbee2mqtt.service.d/override.conf   Restart=always
/home/iotchallenge/z2m_healthcheck.sh                     phép thử sống
/etc/systemd/system/z2m-healthcheck.{service,timer}        hẹn giờ 2 phút
```

Health-check **không** đọc topic `bridge/state`. Đã thử và nó **sai**:
`bridge/state` là **retained message**, nên khi treo tiến trình bằng `SIGSTOP`,
mosquitto vẫn trả `{"state":"online"}` và phép kiểm tra đi qua như không có gì.

Cách đúng là gọi API và **đợi phản hồi** — phản hồi chỉ đến nếu z2m thật sự xử lý
được request:

```bash
mosquitto_sub -t 'zigbee2mqtt/bridge/response/health_check' -C 1 &
mosquitto_pub -t 'zigbee2mqtt/bridge/request/health_check' -m '{}'
```

Đã kiểm chứng bằng cách `SIGSTOP` tiến trình: `systemctl` báo active, retained
state báo online, và health-check **vẫn bắt được** rồi khởi động lại.

> Đừng dùng `WatchdogSec`. Nó đòi tiến trình tự gửi `sd_notify`, mà z2m không
> gửi, nên systemd sẽ giết nhầm nó liên tục.

### 5.2. Converter chia 100 thừa

**Triệu chứng:** một ca truyền **đúng y lệnh** (52 giọt/phút so với mức đặt 50)
hiện ra **0%** trên bảng điều khiển và bị tô đỏ như dòng chảy đã dừng.

**Nguyên nhân:** firmware gửi **phần trăm** (`tỉ số × 100` = 104), converter chia
thêm 100 thành `1.04`, rồi `get_int_from_json` trong gateway **cắt thành 0**.

Đường serial không chia nên không dính lỗi — và vì nhóm test bằng đường serial,
nó ẩn rất lâu. **Bài học: test cả hai đường, chúng không tương đương.**

### 5.3. `slc generate` xoá cluster ZCL tuỳ biến

Xem [`MLTK_AUTOGEN.md`](MLTK_AUTOGEN.md) mục 5. Tóm tắt: **dùng
`tools/slc_generate.sh`**, đừng gọi `slc generate` trần.

---

## 6. Bố trí attribute ZCL tuỳ biến

Cluster `Smart IV Vitals`, mã nhà sản xuất `0x1049`. Nguồn:
`config/zcl/smart-iv-vitals.xml`.

| Mã | Tên | Kiểu | Ghi chú |
|---|---|---|---|
| 0x00–0x0E | sinh hiệu, giọt, cân, y lệnh, nền HR | | |
| **0x0F** | `TsFlags` | bitmap16 | xem bảng bit dưới |
| 0x10–0x15 | dự báo và xu hướng | | |
| **0x16** | `RemainingMl` | int16u | `0xFFFF` = chưa ước lượng được |
| **0x17** | `RemainingMin` | int16u | `0xFFFF` = chưa ước lượng được |

### Bit của `TsFlags`

| Bit | Ý nghĩa |
|---|---|
| 0–2 | forecaster sẵn sàng · bất thường qua K=11 · cảnh báo sớm |
| 3–6 | xu hướng nhịp tim · xu hướng số giọt |
| 7–8 | dự báo HR / giọt có đáng tin không |
| **9–10** | **cấp cảnh báo 0–3** |
| **11–12** | **nhánh đường truyền · nhánh bệnh nhân** |
| **13–15** | **phán quyết loadcell, gửi dưới dạng `state+1`** |

Dùng bit trống của một attribute gateway đã đọc sẵn nên **không phải đổi schema
ZCL** — chỉ thêm vài dòng giải mã trong converter.

`state+1` chứ không phải `state`: giá trị 0 mang nghĩa *"chưa kết luận được"*
(cửa sổ trọng lượng 60 giây chưa đầy). Gửi thẳng `state` thì "chưa biết" trùng
với "mọi thứ ổn", và bảng điều khiển sẽ hiện màu xanh mà nó chưa xứng đáng có.

Cùng lý do, `RemainingMl`/`RemainingMin` gửi `0xFFFF` thay vì `0` khi chưa tính
được, và converter đổi thành `null` chứ không phải `0`. Báo *"còn khoảng 0 phút"*
cho một bình chưa treo là loại số liệu sai một cách tự tin — thứ khiến người ta
thôi tin cái máy.

---

## 6.5 Cấu hình OTA trên Pi

Chỉ cấu hình **một lần**, trong `~/zigbee2mqtt/data/configuration.yaml`:

```yaml
ota:
  zigbee_ota_override_index_location: http://<tên-máy-chủ>.local:5100/api/ota/index.json
  image_block_response_delay: 50
  default_maximum_data_size: 63
```

Ba điểm đáng ghi:

- **Index trỏ về HIS Server, không phải file trên Pi.** Server tự sinh index từ
  header của từng ảnh đã tải lên, nên **không còn phải scp file và sửa JSON bằng
  tay**. Kỹ thuật viên tải file qua web là z2m thấy ngay.
- `default_maximum_data_size: 63` là **trần cứng**. Lớn hơn thì firmware xG26 bỏ
  qua khối và OTA **đứng im không báo lỗi**.
- Tên máy chủ dùng **mDNS** giống `gateway.service`, đừng thay bằng IP cứng.

Kiểm tra Pi đọc được index:

```bash
curl -s http://<tên-máy-chủ>.local:5100/api/ota/index.json
```

Toàn bộ quy trình phát hành, các bẫy và bảng sự cố nằm ở [`OTA.md`](OTA.md).

---

## 7. Thêm thiết bị Zigbee mới

Đã thử với một board xG24 (BRD2703A) nạp mẫu `zigbee_z3_light` — không AI, không
cảm biến, chỉ để kiểm tra phát hiện và gán giường.

```bash
# 1. mở cho phép join
mosquitto_pub -h localhost -t 'zigbee2mqtt/bridge/request/permit_join' -m '{"time":300}'

# 2. trên CLI của board, ÉP CHỈ QUÉT KÊNH CỦA MẠNG MÌNH
network leave
plugin network-steering mask set 1 0x800     # 0x800 = kênh 11
plugin network-steering mask set 2 0x800
plugin network-steering start 0
```

**Vì sao phải ép mask:** lần đầu không ép, network steering quét mọi kênh và
board **join nhầm mạng Zigbee của người khác** đang mở gần đó (kênh 15, PAN
`0x7082`, trong khi mạng của ta ở kênh 11, PAN `0x1A62`). Board báo "join thành
công" và mọi thứ trông ổn, chỉ là nó ở nhầm mạng.

Kiểm tra board đang ở mạng nào bằng lệnh `info` trên CLI — so `panID` và `chan`
với `zigbee2mqtt/bridge/info`.

Sau khi join, thiết bị **tự lên** danh sách bên kỹ thuật viên; gateway đọc
`zigbee2mqtt/bridge/devices` và công bố mọi thiết bị, không cần bấm Rescan.

---

## 8. Lệnh cứu hộ nhanh

```bash
# chuỗi đang tắc ở đâu?
ssh iotchallenge@raspberrypi.local '
  systemctl is-active zigbee2mqtt gateway mosquitto
  timeout 10 mosquitto_sub -h localhost -t "zigbee2mqtt/bridge/state" -C 1
  journalctl -u gateway -n 5 --no-pager
'

# ép z2m sống lại (health-check cũng làm việc này mỗi 2 phút)
ssh iotchallenge@raspberrypi.local 'sudo /home/iotchallenge/z2m_healthcheck.sh'

# lùi về bản gateway trước
ssh iotchallenge@raspberrypi.local '
  cd /home/iotchallenge && ls gateway_test.bak.*
  sudo systemctl stop gateway && cp gateway_test.bak.XXXX gateway_test && sudo systemctl start gateway
'
```

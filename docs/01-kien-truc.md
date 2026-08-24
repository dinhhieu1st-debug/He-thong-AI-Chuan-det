# 1. Kiến trúc hệ thống

## Bốn trạm

Hệ thống chia thành bốn trạm chạy độc lập, nối với nhau bằng giao thức chuẩn.
Sửa một trạm không phải đụng ba trạm còn lại.

| # | Trạm | Chạy ở đâu | Việc của nó | Mã nguồn |
|---|---|---|---|---|
| 1 | Firmware Smart IV | Chip EFR32xG26 gắn đầu giường | Đọc cảm biến, chạy AI, quyết định mức cảnh báo, hiện OLED, phát LED/buzzer, report Zigbee | `firmware/` |
| 2 | Zigbee coordinator | Board thứ hai cắm USB vào Pi | Phiên dịch Zigbee ↔ USB. Dùng firmware NCP nguyên bản Silicon Labs, không sửa | — |
| 3 | Gateway Pi | Raspberry Pi ARM64 | Zigbee2MQTT giải mã gói thành JSON; gateway C bắc cầu MQTT ↔ TCP tới server | `gateway-pi/` |
| 4 | HIS Server | Máy Windows/Linux chạy .NET 8 | Lưu MySQL, tính trạng thái giường, đẩy realtime qua SignalR, phục vụ web UI | `He-thong-AI-Chuan-det-server/` |

## Luồng dữ liệu

```text
MAX30102 (HR/SpO2) + photodiode (giọt) + HX711 (cân)
                     |
                     v
        +----------------------------+
        |  EFR32xG26  --  Tram 1     |
        |  loc tin hieu -> AI ->     |
        |  muc canh bao -> OLED/LED  |
        +-------------+--------------+
                      | ZCL Attribute, EP 2, cluster 0xFC01
                      v
        +----------------------------+
        |  Coordinator  --  Tram 2   |
        +-------------+--------------+
                      | USB / UART
                      v
        +----------------------------+
        |  Raspberry Pi  --  Tram 3  |
        |  zigbee2mqtt -> MQTT :1885 |
        |  gateway (C) -> TCP        |
        +-------------+--------------+
                      | TCP :5000, moi dong mot ban ghi JSON
                      v
        +----------------------------+
        |  HIS Server  --  Tram 4    |
        |  MySQL + SignalR + web UI  |
        +-------------+--------------+
                      | HTTP :5194
                      v
                Trinh duyet y ta
```

Đường lệnh đi **ngược lại đúng chuỗi đó**: y tá đặt tốc độ giọt trên web →
server → TCP → gateway → MQTT → zigbee2mqtt → ZCL write → G26. Không có
đường tắt nào.

## Nguyên tắc thiết kế

Ghi lại để người đọc code sau này không "sửa lại cho gọn" đúng những chỗ đã
cân nhắc kỹ.

**Chip là nơi duy nhất quyết định mức cảnh báo.** G26 tính mức 1/2/3 rồi gửi
lên. Server hiển thị và lưu, **không** tính lại. Trước đây hai nơi cùng tính
và ra hai kết quả khác nhau trên cùng một bản ghi.

**G26 không tự sinh JSON.** Firmware chỉ ghi giá trị vào ZCL attribute. Việc
đóng gói thành JSON là của converter zigbee2mqtt. Nhờ vậy đổi định dạng bản
tin không phải nạp lại chip.

**"Chưa có dữ liệu" khác "bình thường" và cũng khác "nguy kịch".** Cảm biến
chưa cắm đọc ra 0. Nếu coi số 0 đó là chỉ số thật thì giường trống cũng kêu
"SpO2 0% — nguy kịch". Kênh mất tín hiệu hiện `--`, không hiện số.

**Cảnh báo chỉ bật sau khi học xong.** Chip cần 20 mẫu giọt và 64 mẫu sinh
hiệu để có mốc so sánh riêng cho từng bệnh nhân. Trước lúc đó vẫn đo và vẫn
hiển thị, nhưng `alerts_armed` bằng `false` và không có báo động nào.

**Leo thang tức thì, hạ mức thì phải chờ.** Một khoảng giọt bất thường là tín
hiệu thật, đẩy mức lên ngay. Nhưng phải có **3 khoảng giọt bình thường liên
tiếp** mới hạ về mức 1 — một giọt sạch ngay sau khi hết tắc không được phép
xoá ngay cảnh báo mà y tá còn cần nhìn thấy.

**Số để đọc và số để cảnh báo là hai thứ khác nhau.** Tốc độ giọt hiện trên
OLED/web lấy median 7 khoảng gần nhất cho đỡ nhảy. Nhưng phán quyết cảnh báo
dùng **từng khoảng giọt thô**, không qua median — làm mượt là để dễ đọc,
không được phép che một giọt đến muộn.

## Đọc tiếp

- Sơ đồ chân và đấu nối: [`02-phan-cung.md`](02-phan-cung.md)
- Bảng attribute Zigbee: [`03-zigbee.md`](03-zigbee.md)
- Ngưỡng và công thức cảnh báo: [`04-canh-bao.md`](04-canh-bao.md)

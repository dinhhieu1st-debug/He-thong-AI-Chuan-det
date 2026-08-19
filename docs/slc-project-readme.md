# Smart IV Monitor — firmware đầu giường (EFR32xG26 / BRD2709A)

Đây là mô tả project hiện trong Simplicity Studio. Nó **không phải** tài liệu
chính — bắt đầu từ [`../README.md`](../README.md).

> File này từng là bản mẫu "Empty C Example" của SDK. Nó được `.slcp` trỏ tới
> nên không xoá được, và để nguyên thì người mở project sẽ đọc thấy "một project
> rỗng để bắt đầu thêm chức năng", mô tả sai hoàn toàn thứ đang nằm trong đây.

Firmware này đọc bốn kênh cảm biến ở đầu giường (nhịp tim, SpO2, số giọt, trọng
lượng bình dịch), chạy **ba model TensorFlow Lite Micro ngay trên chip**, hợp
nhất với luật lâm sàng cứng và phép đối chiếu cân ↔ số giọt để ra **một mức cảnh
báo bốn cấp**, rồi hiện lên màn OLED đầu giường và gửi qua Zigbee.

Không có cloud và không có server nào tham gia vào việc **quyết định báo động**:
rút mạng thì thiết bị vẫn báo động bình thường, chỉ là trạm điều dưỡng không
nhìn thấy.

| Tài liệu | Nội dung |
|---|---|
| [`HUONG_DAN_A_Z.md`](HUONG_DAN_A_Z.md) | Toàn bộ hệ thống từ chip tới web, kèm build/flash không cần GUI |
| [`AI_HOAT_DONG_THE_NAO.md`](AI_HOAT_DONG_THE_NAO.md) | AI làm gì, khi nào báo động và khi nào cố tình không báo |
| [`MLTK_AUTOGEN.md`](MLTK_AUTOGEN.md) | Luồng `.tflite` → tool MLTK → chip |
| [`OTA.md`](OTA.md) | Cập nhật firmware từ xa |

**Đừng chạy `slc generate` trần** — dùng [`../tools/slc_generate.sh`](../tools/slc_generate.sh),
nếu không cache của ZAP sẽ âm thầm xoá các attribute ZCL tuỳ biến. Lý do đầy đủ
ghi trong chính script đó.

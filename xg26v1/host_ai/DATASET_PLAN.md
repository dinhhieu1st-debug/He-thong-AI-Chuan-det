# Kế hoạch dataset cảnh báo sớm giọt dịch

## Phạm vi

Đây là protocol cho prototype nghiên cứu, không phải chỉ định tốc độ truyền cho
bệnh nhân. Tốc độ lâm sàng thực tế phụ thuộc y lệnh, thể tích, thời gian và hệ
số giọt in trên bộ dây.

## Target đầu vào

| Preset | Target interval | Target rate |
|---|---:|---:|
| `slow` | 1500 ms/giọt | 40 giọt/phút |
| `normal` | 1000 ms/giọt | 60 giọt/phút |
| `fast` | 750 ms/giọt | 80 giọt/phút |

Mỗi session chỉ dùng một preset. Dải ổn định đã được xác nhận là 90–110% của
target. Ví dụ `normal` có dải ổn định 900–1100 ms.

## RAW input

Mỗi record giữ nguyên bốn trường từ firmware:

```text
timestamp_ms,drop_number,target_interval_ms,actual_interval_ms
```

Metadata session bổ sung preset, target, điều kiện dự định, điều kiện quan sát,
ghi chú, thời điểm bắt đầu/kết thúc và drop factor nếu biết.

## Các tình huống cần thu cho mỗi preset

1. `stable`: giữ trong dải 90–110% target.
2. `gradually_slowing`: interval tăng dần.
3. `gradually_speeding`: interval giảm dần.
4. `rapid_change`: thay đổi nhanh khỏi target.
5. `temporary_disturbance`: lệch ngắn rồi trở lại.
6. `missing_drop`: dừng hoặc tạo khoảng trễ dài.
7. `recovery`: từ lệch lớn quay về target.
8. `irregular`: dao động không đều quanh target.

Pilot đầu tiên thu ít nhất 2 session cho mỗi tình huống và mỗi preset. Mỗi
session nên có ít nhất 120 record hợp lệ. Sau khi kiểm tra pilot mới quyết định
số lần lặp cho dataset chính.

## Mẫu học chuỗi

```text
X = 20 giọt quá khứ
y = trạng thái của 5 giọt tương lai
```

Feature sẽ được tạo trong Python theo target động, gồm `actual / target`, sai
lệch phần trăm, rolling mean/std, slope và biến thiên. RAW không bị sửa.

## Output nghiên cứu

```text
1 = NORMAL
2 = ATTENTION
3 = WARNING
```

Preset `slow/normal/fast` là target đầu vào. Class `1/2/3` là trạng thái tương
lai so với target đã chọn; hai khái niệm này không được trộn với nhau.

## Chống leakage

Toàn bộ window từ một session phải nằm trong cùng train, validation hoặc test.
Không chia ngẫu nhiên từng dòng của cùng session.

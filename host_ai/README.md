# AI nhỏ giọt — nhánh nghiên cứu trên ESP8266

> **Thư mục này KHÔNG thuộc hệ thống Smart IV đang chạy.**
>
> Đây là nhánh nghiên cứu trước đó, dùng **ESP8266** đọc giọt qua Serial và
> huấn luyện model trên máy tính. Hệ thống hiện tại chạy trên **EFR32xG26** với
> AI nhúng thẳng vào chip (`firmware/`, `firmware/models/`) và **không** dùng
> mã trong thư mục này khi vận hành.
>
> Giữ lại vì đây là nơi sinh ra dataset và mô hình MLP/LSTM cho nhánh nhỏ giọt.
> Xem [`../docs/04-canh-bao.md`](../docs/04-canh-bao.md) để biết phần nào của
> nghiên cứu này thực sự đang chạy trên chip.

Nội dung: firmware ESP8266, script thu thập dữ liệu, dataset thật và mô phỏng,
mô hình MLP/LSTM, và chương trình inference liên tục trên Raspberry Pi 4.

## Giai đoạn hiện tại
Chỉ làm phần nhỏ giọt dịch:

Photodiode + Laser -> ESP8266 -> RAW time-series -> Dataset -> LSTM -> Normal / Attention / Warning

Nhịp tim sẽ được xây dựng thành pipeline riêng và tích hợp sau.

## Firmware hiện tại
File: `firmware/nhogiot.ino`

- Board: NodeMCU ESP8266
- Sensor DO: D0 / GPIO16
- Baud: 115200
- Target interval mặc định: 1000 ms
- RAW Serial format:

```text
timestamp_ms,drop_number,target_interval_ms,actual_interval_ms
```

Ví dụ:

```text
5231,5,1000,1018
```

## Cấu trúc thư mục

```text
firmware/      Code ESP8266
data/raw/      Dữ liệu gốc từ cảm biến
data/processed/Dữ liệu sau xử lý
data/sessions/ Dữ liệu tách theo phiên thử nghiệm
scripts/       Logger + preprocessing (làm ở bước sau)
models/        Model đã huấn luyện
training/      Code train MLP/LSTM
inference/     Code dự đoán realtime
config/        Cấu hình project
```

## Ghi RAW dataset theo session

Trong VS Code, chạy một trong các task `Dataset: Record SLOW session`,
`Dataset: Record NORMAL session` hoặc `Dataset: Record FAST session`, sau đó
nhập điều kiện thử và ghi chú. Logger tự động tạo `session_001`,
`session_002`, ... và lưu:

```text
data/raw/session_001.csv
data/sessions/session_001.json
```

Nhấn `Ctrl+C` để kết thúc session an toàn. Logger chỉ ghi các dòng Serial đúng
schema 4 cột, flush từng record và bỏ qua toàn bộ dòng debug.

## Synthetic dataset v1

Dataset mô phỏng phục vụ phát triển pipeline được lưu riêng trong
`data/synthetic/` và `data/processed/`, không trộn với dữ liệu cảm biến thật.
Xem `DATASET_CARD.md` để biết schema, preset, tình huống, label policy, split và
giới hạn sử dụng.

## Quy tắc làm việc
Không chuyển sang bước tiếp theo cho tới khi firmware đọc giọt ổn định và dữ liệu RAW được xác nhận sạch.

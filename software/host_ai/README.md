# AI nhỏ giọt

Hệ thống AI cảnh báo sớm cho prototype IoT giám sát nhỏ giọt dịch truyền,
gồm firmware ESP8266, thu thập dữ liệu, dataset thật/mô phỏng, mô hình MLP/LSTM
và chương trình inference liên tục trên Raspberry Pi 4.

Project VS Code cho hệ thống giám sát nhỏ giọt dịch truyền và hướng tới cảnh báo sớm bằng AI/LSTM.

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

# Dataset Card — IV Drip Early Warning Synthetic v1

## Mục đích

Dataset dùng để phát triển và kiểm thử pipeline AI cảnh báo sớm cho prototype
theo dõi giọt dịch. Dataset không phải dữ liệu bệnh nhân và không dùng để đưa ra
quyết định y khoa.

## Nguồn dữ liệu

- `data/raw/`: dữ liệu cảm biến thật, không bị thay thế hoặc trộn với dữ liệu mô
  phỏng.
- `data/synthetic/`: dữ liệu mô phỏng, luôn có `source_type=synthetic` và seed
  tái tạo.
- Synthetic v1 được hiệu chỉnh số học từ các session thật đại diện:
  `session_003`, `session_005`, `session_006`, `session_009`, `session_010`.
- Các tình huống mô phỏng cũng phản ánh những dạng diễn biến đã quan sát trong
  toàn bộ pilot: stable, drift, speeding, disturbance, missing drop, recovery và
  irregular transition.

## Preset đầu vào

| Preset | Target interval | Target rate |
|---|---:|---:|
| `slow` | 1500 ms/giọt | 40 giọt/phút |
| `normal` | 1000 ms/giọt | 60 giọt/phút |
| `fast` | 750 ms/giọt | 80 giọt/phút |

Preset là mục tiêu do người vận hành chọn, không phải nhãn cảnh báo.

## Quy mô synthetic v1

```text
3 preset × 8 tình huống × 10 session = 240 session
240 session × 180 record             = 43.200 RAW record
                                      = 37.440 mẫu cửa sổ LSTM
```

Mỗi preset có 80 session. Mỗi tình huống có 30 session, phân bổ đều qua ba
preset.

Tám tình huống:

1. `stable`
2. `gradually_slowing`
3. `gradually_speeding`
4. `rapid_change`
5. `temporary_disturbance`
6. `missing_drop`
7. `recovery`
8. `irregular`

## RAW input

Mỗi session giữ schema gốc:

```text
timestamp_ms,drop_number,target_interval_ms,actual_interval_ms
```

Feature cho AI được tính từ RAW, không ghi ngược vào RAW:

```text
ratio = actual_interval_ms / target_interval_ms
error_percent = (ratio - 1) × 100
delta_ratio = ratio hiện tại - ratio trước
```

## Mẫu LSTM

```text
X = 20 giọt quá khứ × 3 feature
y = trạng thái của 5 giọt tương lai
```

Khi load vào mô hình, 60 cột feature phẳng trong CSV được reshape thành:

```python
(number_of_samples, 20, 3)
```

## Output

| Class | Tên | Policy prototype trên 5 giọt tương lai |
|---:|---|---|
| 1 | NORMAL | Cả 5 ratio nằm trong 0.90–1.10 |
| 2 | ATTENTION | Chuyển tiếp hoặc lệch vừa, không thuộc 1/3 |
| 3 | WARNING | Mean ratio ≤0.75 hoặc ≥1.30, hoặc có ratio ≥2.0 |

Đây là ground-truth policy cho mô phỏng và phát triển pipeline, không phải ngưỡng
y khoa cuối cùng. Policy nằm trong `data/synthetic/label_policy.json` và có
version để thay đổi có kiểm soát.

Phân bố 37.440 mẫu:

```text
1 NORMAL     24.445 (65,29%)
2 ATTENTION   6.263 (16,73%)
3 WARNING     6.732 (17,98%)
```

## Chia tập không leakage

| Split | Session | Window sample |
|---|---:|---:|
| Train | 168 | 26.208 |
| Validation | 24 | 3.744 |
| Test | 48 | 7.488 |

Một session chỉ xuất hiện trong đúng một split.

## File chính

```text
data/synthetic/manifest.csv
data/synthetic/all_sessions_raw.csv
data/synthetic/raw/syn_0001.csv ... syn_0240.csv
data/synthetic/sessions/syn_0001.json ... syn_0240.json
data/synthetic/label_policy.json
data/synthetic/dataset_summary.json
data/synthetic/validation_report.json
data/synthetic/validation_summary.csv
data/synthetic/checksums.sha256

data/processed/synthetic_windows.csv
data/processed/synthetic_train.csv
data/processed/synthetic_validation.csv
data/processed/synthetic_test.csv
```

## Tái tạo và kiểm định

```powershell
.\.venv\Scripts\python.exe scripts\generate_synthetic_dataset.py --force
.\.venv\Scripts\python.exe scripts\inspect_dataset.py
```

Seed mặc định: `20260813`. Validation hiện tại: `PASS`, không có error hoặc
warning.

## Giới hạn

- Synthetic data không thay thế validation bằng cảm biến thật độc lập.
- Ba preset là preset nghiên cứu; tốc độ lâm sàng phải dựa trên y lệnh, thể
  tích, thời gian và drop factor của bộ dây.
- Không dùng test set để chỉnh generator, feature, label policy hoặc model.
- Trước triển khai thực tế phải thu thêm dữ liệu thật ở cả ba preset và nhiều
  cấu hình phần cứng/điều kiện môi trường.

> **BẢN ĐẶC TẢ ĐỀ XUẤT BAN ĐẦU — không phải mô tả hệ thống đang chạy.**
>
> Bản triển khai thật khác tài liệu này ở bốn điểm, mỗi điểm đều có lý do đo
> được (xem [`AI_V2_PLAN.md`](AI_V2_PLAN.md) mục 2):
>
> 1. **Ba model chạy riêng**, không gộp vào một flatbuffer.
> 2. **Loadcell ra khỏi Model 1**, chuyển thành luật số học tường minh.
> 3. **Model 3 chỉ nhận 2 kênh sinh hiệu** (không có kênh giọt), và nhịp tim
>    vào dưới dạng **độ lệch so với nền riêng bệnh nhân**.
> 4. **Tắt** cơ chế tự khởi tạo model của SDK, vì nó xử lý lỗi bằng `while(1)`.
>
> Mô tả hệ thống đang chạy: [`AI_TIME_SERIES_TAT_TAN_TAT.md`](AI_TIME_SERIES_TAT_TAN_TAT.md).

---

# TÀI LIỆU ĐẶC TẢ KỸ THUẬT & HƯỚNG DẪN HUẤN LUYỆN HỆ THỐNG AI SMART IV
## (Dành cho Claude Code / AI Engineer triển khai huấn luyện và tích hợp nhúng)

---

## 1. TỔNG QUAN HỆ THỐNG & TRIẾT LÝ THIẾT KẾ

Hệ thống AI giám sát an toàn truyền dịch thông minh (Smart IV Monitor) hoạt động theo triết lý **On-Chip Edge AI (TinyML)** chạy trực tiếp trên vi điều khiển **Silicon Labs EFR32MG26 (Cortex-M33)** có bộ tăng tốc phần cứng **MVP (Matrix Vector Processor)**.

Để giải quyết triệt để bài toán **"không gán ghép tương quan ảo giữa sinh hiệu thật và dữ liệu giọt mô phỏng"** và **"triệt tiêu báo động giả"**, hệ thống được thiết kế theo kiến trúc **AI Đa Tầng (Multi-Tier Decoupled AI)** gồm 3 mô hình độc lập + 1 bộ hợp nhất quyết định:

```
[ CẢM BIẾN 1 Hz ]
  ├── Cảm biến Giọt + Loadcell  ──> (Cửa sổ 64s x 2) ──> [ MODEL 1: Drip 1D-CNN Forecaster ]
  │                                                                 │ (Dự báo 16s tới)
  ├── Cảm biến MAX30102 (HR/SpO2) ─> (Cửa sổ 64s x 2) ──> [ MODEL 2: Vitals 1D-CNN Forecaster ]
  │                                                                 │ (Dự báo 16s tới)
  └── Snapshot tức thời (4 kênh) ───> [ Vector 4x1 ]  ──> [ MODEL 3: Snapshot Autoencoder ]
                                                                    │ (Tổ hợp đa biến lạ)
                                                                    ▼
                                                    [ BỘ HỢP NHẤT & LỌC NHIỄU K=11s ]
                                                                    │
                                                    [ CẢNH BÁO: XANH / VÀNG / ĐỎ ]
```

---

## 2. ĐẶC TẢ CHI TIẾT 3 MÔ HÌNH AI

### 2.1. MODEL 1: Drip Time-Series Forecaster (Dự báo Dòng chảy Truyền dịch)

* **Mục tiêu:** Nhìn quán tính cơ học $64\text{ giây}$ quá khứ của bình dịch để dự báo trước $16\text{ giây}$ tương lai, phát hiện sớm nguy cơ tắc nghẽn hoặc chảy tự do.
* **Tần số lấy mẫu:** $1\text{ Hz}$ (1 mẫu/giây).
* **Đầu vào (Input Tensor):** Shape `(1, 1, 64, 2)` kiểu `int8` (chuẩn hóa tĩnh):
  1. `drops_ratio`: $\frac{\text{dpm thực tế}}{\text{dpm y lệnh}}$, chuẩn hóa: `(ratio - 1.0) / 0.35`
  2. `relative_weight`: Sụt giảm khối lượng loadcell so với đầu cửa sổ, chuẩn hóa: `(weight[t] - weight[t-63]) / 5.0`
* **Đầu ra (Output Tensor):** Shape `(1, 32)` kiểu `int8` (phẳng hóa $16\text{ giây} \times 2\text{ kênh}$).
* **Kiến trúc mạng (Tối ưu cho Silicon Labs MVP):**
  * `Input(1, 64, 2)` (dùng `Conv2D` với kernel $1 \times k$ thay vì Keras `Conv1D` để tránh sinh op phụ).
  * `Conv2D(filters=16, kernel_size=(1, 5), strides=(1, 2), padding="same", activation="relu")` $\rightarrow (1, 32, 16)$
  * `Conv2D(filters=32, kernel_size=(1, 5), strides=(1, 2), padding="same", activation="relu")` $\rightarrow (1, 16, 32)$
  * `Conv2D(filters=32, kernel_size=(1, 3), strides=(1, 2), padding="same", activation="relu")` $\rightarrow (1, 8, 32)$
  * `Reshape(target_shape=(256,))` (Kích thước tĩnh).
  * `Dense(units=32, activation="relu")`
  * `Dense(units=32, activation="linear")` (Output $16 \times 2 = 32$).
* **Dữ liệu huấn luyện:**
  * Sinh dữ liệu mô phỏng dựa trên **8 kịch bản lâm sàng của `AI-nho-giot`**: `stable`, `gradually_slowing`, `gradually_speeding`, `rapid_change`, `temporary_disturbance`, `missing_drop`, `recovery`, `irregular`.
  * Chuẩn hóa tốc độ $1\text{ Hz}$ có trôi dạt $\text{AR}(1)$ ($\rho = 0.97, \sigma = 0.35$).
  * Train **chỉ trên các đoạn bình thường** (Unsupervised Dynamics), dùng Loss **Huber ($\delta = 1.0$)**.

---

### 2.2. MODEL 2: Vitals Time-Series Forecaster (Dự báo Sinh hiệu Bệnh nhân)

* **Mục tiêu:** Nhìn động lực học sinh lý $64\text{ giây}$ quá khứ để dự báo $16\text{ giây}$ tới của Nhịp tim và $\text{SpO}_2$, phát hiện xu hướng tụt oxy hoặc loạn nhịp và tính toán độ dốc xu hướng ($\pm 10\text{ bpm/phút}$).
* **Tần số lấy mẫu:** $1\text{ Hz}$.
* **Đầu vào (Input Tensor):** Shape `(1, 1, 64, 2)` kiểu `int8`:
  1. `heart_rate`: Chuẩn hóa cố định: `(hr - 80.0) / 20.0`
  2. `spo2`: Chuẩn hóa cố định: `(spo2 - 97.0) / 2.0`
* **Đầu ra (Output Tensor):** Shape `(1, 32)` kiểu `int8` ($16\text{ giây} \times 2\text{ kênh}$).
* **Kiến trúc mạng:** Tương tự Model 1 (dùng chung cấu trúc 1D-CNN Conv2D kernel $1 \times k$ để tận dụng bộ nhớ và MVP).
* **Dữ liệu huấn luyện:**
  * **100% dữ liệu ICU thật từ PhysioNet BIDMC (52 bệnh nhân)** (`bidmc_##_Numerics.csv`).
  * **Phân chia tập (Split):** Tách theo mã bệnh nhân gốc `bidmc_src` (Train: 31 bệnh nhân, Validation: 10 bệnh nhân, Test: 11 bệnh nhân), **tuyệt đối không tách ngẫu nhiên theo dòng để tránh rò rỉ dữ liệu**.
  * Train trên các cửa sổ sinh hiệu bình thường với Loss **Huber ($\delta = 1.0$)**.

---

### 2.3. MODEL 3: Multi-Sensor Snapshot Autoencoder (Bắt Tổ hợp Đa biến Bất thường)

* **Mục tiêu:** Quét tức thời vector 4 kênh tại thời điểm $t$ để phát hiện các trạng thái "tổ hợp bất thường" mà từng ngưỡng đơn lẻ chưa vi phạm (ví dụ: HR 130 + SpO2 92% + Drip 1.4x).
* **Đầu vào (Input Tensor):** Shape `(1, 4)` kiểu `int8`:
  * `[hr_norm, spo2_norm, drops_ratio_norm, flow_ratio_norm]`
* **Kiến trúc mạng (Bottleneck Autoencoder):**
  * `Dense(units=3, activation="relu")`
  * `Dense(units=2, activation="relu")` (Nút thắt cổ chai 2 chiều)
  * `Dense(units=3, activation="relu")`
  * `Dense(units=4, activation="linear")`
* **Đầu ra & Đánh giá:** Tái tạo lại vector 4 kênh.
* **Chỉ số bất thường:** Sai số tái tạo $\text{MSE} = \frac{1}{4} \sum_{i=0}^{3} (x_i - \hat{x}_i)^2$. Nếu $\text{MSE} > \text{Threshold}_{\text{AE}}$ (chốt ở phân vị 98% của tập validation bình thường) $\rightarrow$ Cờ `AE_ANOMALY = TRUE`.

---

## 3. BỘ HỢP NHẤT QUYẾT ĐỊNH & LỌC NHIỄU K=11 (FIRMWARE LOGIC)

Cứ mỗi giây ($1\text{ Hz}$), sau khi cả 3 model thực hiện suy luận trên EFR32MG26:

### 3.1. Bộ lọc kéo dài (Persistence Filter)
* Nếu Model 1 hoặc Model 2 hoặc Model 3 báo bất thường: Tăng bộ đếm `persistence_counter++`.
* Nếu tín hiệu trở về bình thường: `persistence_counter = 0`.
* **Quy tắc:** Bất thường chỉ được chuyển sang báo động ĐỎ nếu `persistence_counter >= 11` (11 giây liên tiếp). Các cú nháy $2 - 6\text{s}$ do ho/cử động sẽ bị triệt tiêu hoàn toàn.

### 3.2. Ma trận ra quyết định lâm sàng 4 cấp độ

```c
typedef enum {
    ALERT_LEVEL_NORMAL = 0,      // XANH: Tất cả bình thường
    ALERT_LEVEL_LINE_WARNING = 1, // VÀNG: Sự cố đường truyền dịch (tắc/chảy lệch)
    ALERT_LEVEL_VITALS_ALERT = 2, // ĐỎ: Bệnh nhân có dấu hiệu suy hô hấp / tim nhanh
    ALERT_LEVEL_CRITICAL = 3      // ĐỎ KHẨN CẤP: Quá tải dịch / Phản vệ (cả 2 cùng hỏng)
} alert_level_t;
```

* **Cấp 0 (XANH):** Model 1 OK, Model 2 OK, Autoencoder OK.
* **Cấp 1 (VÀNG - Sự cố đường truyền):** Model 1 dự báo tắc/chảy nhanh kéo dài $> 11\text{s}$ HOẶC Autoencoder lệch kênh giọt, nhưng sinh hiệu bệnh nhân vẫn bình thường $\rightarrow$ Màn hình: *"KIỂM TRA DÂY TRUYỀN"*.
* **Cấp 2 (ĐỎ - Cảnh báo sinh hiệu):** Model 2 dự báo $\text{SpO}_2$ tụt / $\text{HR}$ bất thường kéo dài $> 11\text{s}$ HOẶC vi phạm luật cứng ($\text{SpO}_2 < 90\%$, $\text{HR} < 45$ hoặc $> 150$) $\rightarrow$ Màn hình: *"CẢNH BÁO SINH HIỆU"*.
* **Cấp 3 (ĐỎ KHẨN CẤP):** Cả Model 1 (Dịch chảy xối xả) VÀ Model 2 (Sinh hiệu suy sụp) cùng đồng thời xảy ra $\rightarrow$ Hú còi khẩn cấp: *"NGUY HIỂM: NGHI NGỜ QUÁ TẢI DỊCH"*.

---

## 4. HƯỚNG DẪN DÀNH CHO CLAUDE CODE TRIỂN KHAI MÃ NGUỒN

Khi đưa file này cho Claude Code, hãy yêu cầu viết các file theo cấu trúc chuẩn sau:

### Danh mục file cần tạo trong `ml/`:
1. `ml/dataset/make_drip_timeseries.py`: Sinh dataset 8 kịch bản giọt $1\text{ Hz}$ theo logic của `AI-nho-giot`.
2. `ml/dataset/make_vitals_timeseries.py`: Đọc 52 file BIDMC từ PhysioNet, làm sạch và chia split theo bệnh nhân.
3. `ml/train_drip_forecaster.py`: Huấn luyện Model 1 1D-CNN $\rightarrow$ Xuất `drip_forecaster_int8.tflite`.
4. `ml/train_vitals_forecaster.py`: Huấn luyện Model 2 1D-CNN $\rightarrow$ Xuất `vitals_forecaster_int8.tflite`.
5. `ml/train_snapshot_ae.py`: Huấn luyện Model 3 Autoencoder $\rightarrow$ Xuất `snapshot_ae_int8.tflite`.
6. `ml/export_c_headers.py`: Tự động convert 3 file `.tflite` thành 3 file C header chứa mảng byte:
   * `firmware/model_drip_data.h`
   * `firmware/model_vitals_data.h`
   * `firmware/model_ae_data.h`

### Yêu cầu lượng tử hóa TFLite Int8:
* Dùng `tf.lite.TFLiteConverter.from_keras_model`.
* Cung cấp `representative_dataset` đại diện cho từng kênh.
* Ép kiểu: `converter.optimizations = [tf.lite.Optimize.DEFAULT]`, `converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]`, `converter.inference_input_type = tf.int8`, `converter.inference_output_type = tf.int8`.
* Batch size cố định $= 1$ khi xuất model để tránh sinh op `SHAPE`/`PACK` động.

### Tích hợp Firmware trên EFR32MG26:
* Trong `firmware/ts_forecaster.cpp`: Khởi tạo 3 `tflite::MicroInterpreter` với bộ nhớ arena riêng biệt:
  * Arena Drip: $4\text{ KB}$
  * Arena Vitals: $4\text{ KB}$
  * Arena AE: $2\text{ KB}$
  * Tổng RAM chỉ tốn $\approx 10\text{ KB}$, hoàn toàn nằm trong ngân sách RAM $256\text{ KB}$ của chip.

# Mục lục tài liệu

Toàn bộ tài liệu của Smart IV Monitor nằm phẳng trong thư mục này. Trang này là
**bản đồ** — tìm theo *việc bạn đang định làm*, không phải theo tên file.

> **Vì sao không chia thư mục con?** Ba lý do, đều đã kiểm chứng:
> 1. Nhiều file tham chiếu nhau bằng **văn bản thuần**, không phải link —
>    `Script.md` nhắc `docs/OTA.md`, `Quay_Ky_Thuat.md` nhắc `docs/images/*.svg`.
>    Di chuyển sẽ âm thầm làm sai mà **không có gì báo lỗi**.
> 2. `smart-iv-monitor.slcp` (dòng 20) trỏ **đích danh** `docs/slc-project-readme.md`.
>    Di chuyển là hỏng việc mở project trong Simplicity Studio.
> 3. Gom bằng mục lục đạt đúng mục đích "dễ tìm" mà không mang theo rủi ro nào.

---

## Bắt đầu từ đâu

**Chưa biết gì về dự án** → [`HUONG_DAN_A_Z.md`](HUONG_DAN_A_Z.md)
Toàn bộ hệ thống từ chip tới web, giải thích cả *vì sao* thiết kế như vậy, kèm
bảng lỗi thường gặp. Đọc file này trước mọi file khác.

**Vừa nhận bàn giao, cần dựng lại hệ thống** → [`BAN_GIAO_UI_UX.md`](BAN_GIAO_UI_UX.md)
Trạng thái hiện tại, thứ tự dựng từng phần, và **những gì chưa xác nhận được nếu
không có phần cứng**.

**Chỉ cần chạy HIS Server** → [`../server/README.md`](../server/README.md)
API, schema, biến cấu hình, cách chạy.

---

## Theo việc bạn định làm

### Sửa web (UI/UX, API, trạng thái giường)

| File | Nội dung |
|---|---|
| [`BAN_GIAO_UI_UX.md`](BAN_GIAO_UI_UX.md) | Đợt gần nhất đã sửa gì, test bằng gì, còn khoảng trống nào |
| [`PHAN_QUYEN_VA_VAI_TRO.md`](PHAN_QUYEN_VA_VAI_TRO.md) | Vì sao ba vai trò chia quyền như hiện tại — **đọc trước khi đổi phân quyền** |
| [`BAO_CAO_KIEM_THU_2026-08-22.md`](BAO_CAO_KIEM_THU_2026-08-22.md) | **Báo cáo kiểm thử mới nhất** — 97 ca, sau đợt UI/UX, kèm 2 mục cần phần cứng mới kiểm được |
| [`BAO_CAO_KIEM_THU_WEB.md`](BAO_CAO_KIEM_THU_WEB.md) | Báo cáo cũ ngày 19/08/2026 — giữ làm hồ sơ lịch sử, giao diện đã đổi vài chỗ kể từ đó |
| [`LUONG_SETUP_VA_TAM_NGUNG.md`](LUONG_SETUP_VA_TAM_NGUNG.md) | **Thiết kế, CHƯA triển khai** — luồng thiết lập bệnh nhân mới và tạm ngưng theo dõi |

### Sửa firmware hoặc gateway

| File | Nội dung |
|---|---|
| [`TRIEN_KHAI_PI.md`](TRIEN_KHAI_PI.md) | **Đọc trước khi sửa `gateway/`.** Hai file đó chạy trên Pi chứ không phải máy bạn — kèm ba kiểu hỏng đã gặp thật |
| [`MLTK_AUTOGEN.md`](MLTK_AUTOGEN.md) | Luồng `.tflite` → tool MLTK → chip, kèm hai cái bẫy |
| [`OTA.md`](OTA.md) | Cập nhật firmware từ xa: ai thao tác, bấm gì, phát hành bản mới ra sao |
| [`slc-project-readme.md`](slc-project-readme.md) | File Simplicity Studio đọc. **Đừng di chuyển** — `.slcp` trỏ đích danh |

### Làm việc với phần AI

| File | Nội dung |
|---|---|
| [`AI_HOAT_DONG_THE_NAO.md`](AI_HOAT_DONG_THE_NAO.md) | AI làm gì, **khi nào báo động và khi nào cố tình không báo**, 4 kịch bản theo từng giây — viết cho người không đọc code |
| [`AI_TIME_SERIES_TAT_TAN_TAT.md`](AI_TIME_SERIES_TAT_TAN_TAT.md) | Tất tần tật: kiến trúc, huấn luyện, đánh giá, bộ hợp nhất, nhúng vào firmware |
| [`Dataset_va_Phuong_phap_AI_SmartIV.md`](Dataset_va_Phuong_phap_AI_SmartIV.md) | Dataset, cách chia tập, và **những gì nhóm không chứng minh được** |
| [`AI_V2_PLAN.md`](AI_V2_PLAN.md) | Lý do đằng sau từng quyết định của AI v2, **kèm cả những lần đi sai** |

<details>
<summary>Tài liệu AI lịch sử — không mô tả bản đang chạy</summary>

| File | Nội dung |
|---|---|
| [`Nghien_cuu_Nang_cap_AI_Time_Series.md`](Nghien_cuu_Nang_cap_AI_Time_Series.md) | Nghiên cứu dẫn tới AI v1 — v1 đã bị thay thế, file mã nó nhắc tới không còn tồn tại |
| [`AI_SYSTEM_SPECIFICATION.md`](AI_SYSTEM_SPECIFICATION.md) | **Bản đặc tả đề xuất ban đầu — KHÔNG phải hệ thống đang chạy.** Bốn điểm khác biệt ghi ngay đầu file |

</details>

### Quay video demo

| File | Nội dung |
|---|---|
| [`Script.md`](Script.md) | Kịch bản quay đầy đủ, theo cảnh |
| [`Quay_Ky_Thuat.md`](Quay_Ky_Thuat.md) | Shot list kỹ thuật — chỉ phần quay thật + sơ đồ, bỏ hoạt hình |
| [`images/`](images/) | Sơ đồ `.svg` dựng sẵn dùng trong video |

---

## Quy ước khi viết thêm tài liệu

Các file ở đây đều theo cùng một lối, giữ cho nhất quán:

- **Ghi cả những lần đi sai**, không chỉ kết quả cuối. Phần lớn giá trị của
  `TRIEN_KHAI_PI.md` và `AI_V2_PLAN.md` nằm ở chỗ đó.
- **Nói rõ cái gì chưa chứng minh được.** `Dataset_va_Phuong_phap_AI_SmartIV.md`
  ghi thẳng giới hạn của bộ dữ liệu — đó là điểm mạnh, không phải điểm yếu.
- **Đánh dấu rõ file mô tả thiết kế chưa triển khai**, ngay ở tiêu đề, để không
  ai đọc nhầm thành hệ thống đang chạy.
- **Đặt tên file bằng chữ HOA_CÓ_GẠCH_DƯỚI** như các file sẵn có.
- **Thêm file mới thì thêm một dòng vào trang này**, đúng nhóm việc.

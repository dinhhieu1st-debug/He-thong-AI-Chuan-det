# Smart IV - three-project workspace

Nhánh này gom ba dự án đang dùng của hệ thống Smart IV thành ba thư mục độc lập:

- `xg26v1/`: firmware Silicon Labs EFR32MG26 và mã AI/OLED trên thiết bị.
- `demo1/`: HIS server, web dashboard, firmware/gateway tham chiếu và tài liệu.
- `gateway-pi/`: mã nguồn gateway Zigbee/MQTT/HIS dành cho Windows và Raspberry Pi.

Đây là snapshot mã nguồn từ ba working tree tại thời điểm tích hợp. SDK vendor,
thư mục build, binary, DLL, log và gói deployment sinh lại được không nằm trong
nhánh này. Xem `README.md` trong từng thư mục để build và chạy từng thành phần.

> Lưu ý: các migration tài khoản demo chỉ dành cho môi trường trình diễn. Hãy
> đổi hoặc bỏ tài khoản/mật khẩu demo trước khi triển khai thật.

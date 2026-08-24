-- Lịch sử cập nhật firmware.
--
-- Dùng luôn bảng device_events sẵn có chứ không dựng bảng mới: đây vẫn là
-- "chuyện đã xảy ra với một thiết bị", cùng loại với JOINED hay SENSOR_FAULT,
-- và kỹ thuật viên đọc nó ở đúng một chỗ. Bảng riêng thì lịch sử thiết bị bị
-- chẻ làm hai và không ai ghép lại khi cần dựng lại diễn biến một sự cố.
--
-- Ba trạng thái, không phải một:
--   FIRMWARE_UPDATE_STARTED  - đã bấm, chưa biết kết quả
--   FIRMWARE_UPDATED         - đã xong, kèm đi từ bản nào sang bản nào
--   FIRMWARE_UPDATE_FAILED   - hỏng, kèm nguyên văn lý do
--
-- Ghi cả lần hỏng là chủ ý. Một thiết bị hỏng ba lần rồi mới lên được là một
-- thiết bị đáng ngờ, và nếu chỉ ghi lần thành công thì ba lần kia biến mất.

ALTER TABLE device_events
  MODIFY COLUMN event_type ENUM(
    'JOINED', 'ASSIGNED', 'ONLINE', 'SENSOR_FAULT', 'OFFLINE',
    'FAULT_REPORTED', 'FAULT_RESOLVED',
    'FIRMWARE_UPDATE_STARTED', 'FIRMWARE_UPDATED', 'FIRMWARE_UPDATE_FAILED'
  ) NOT NULL;

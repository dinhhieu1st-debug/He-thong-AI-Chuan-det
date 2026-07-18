#include "HX711.h"

const int LOADCELL_DOUT_PIN = D2; 
const int LOADCELL_SCK_PIN = D1;  

// Thay hệ số chuẩn của bạn vào đây (Nếu treo vật lên bị âm, hãy đổi thành -14000.0)
const float CALIBRATION_FACTOR = 14000.0; 

HX711 scale;

void setup() {
  Serial.begin(115200);
  
  // Chờ hẳn 5 giây để ESP8266 ổn định hoàn toàn điện áp và dòng nạp
  delay(5000); 
  Serial.println("\n--- HỆ THỐNG CÂN CHỦ ĐỘNG KHỬ ÂM ---");

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR); 
  
  // Đợi tín hiệu từ người dùng để đảm bảo cân trống không và điện áp chuẩn
  Serial.println("==================================================");
  Serial.println("HÃY ĐỂ TRỐNG CÂN VÀ GỬI CHỮ 't' QUA SERIAL ĐỂ TRỪ BÌ!");
  Serial.println("==================================================");
  
  while (true) {
    if (Serial.available() > 0) {
      char ch = Serial.read();
      if (ch == 't' || ch == 'T') {
        break; // Thoát vòng lặp khi nhận được lệnh trừ bì
      }
    }
    delay(100);
  }

  Serial.println("Đang trừ bì tĩnh (Tare 30 lần)...");
  scale.tare(30); // Lấy mẫu hẳn 30 lần cho cực kỳ chính xác
  Serial.println("Trừ bì xong! Cân đã về 0.000 kg. Hãy treo đồ lên đo.");
}

void loop() {
  if (scale.wait_ready_timeout(200)) {
    float weight_kg = scale.get_units(10); 

    // Bộ lọc triệt tiêu sai số tĩnh quanh mức 0
    if (weight_kg < 0.015 && weight_kg > -0.015) {
      weight_kg = 0.0;
    }

    Serial.print("Khối lượng bịch dịch: ");
    Serial.print(weight_kg, 3); 
    Serial.println(" kg");
  } else {
    Serial.println("Lỗi phần cứng: HX711 không phản hồi!");
  }
  delay(500);
}
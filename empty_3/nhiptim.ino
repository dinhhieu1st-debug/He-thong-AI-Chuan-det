#include <Wire.h>
#include "DFRobot_BloodOxygen_S.h"

// Chân I2C của ESP8266 NodeMCU
#define SDA_PIN D2       // GPIO4
#define SCL_PIN D1       // GPIO5

// Địa chỉ I2C của MAX30102 V2.0
#define I2C_ADDRESS 0x57

DFRobot_BloodOxygen_S_I2C MAX30102(&Wire, I2C_ADDRESS);

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Khoi dong ESP8266 + MAX30102 V2.0...");

  // Khởi tạo I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  // Kiểm tra kết nối cảm biến
  while (MAX30102.begin() == false)
  {
    Serial.println("Khong tim thay MAX30102!");
    Serial.println("Kiem tra: 3V3, GND, SDA=D2, SCL=D1");
    delay(1000);
  }

  Serial.println("Ket noi MAX30102 thanh cong!");
  Serial.println("Bat dau do...");
  Serial.println("Dat dau ngon tay len mat cam bien.");
  Serial.println("--------------------------------");

  // Bắt đầu thu thập dữ liệu
  MAX30102.sensorStartCollect();

  // Chờ cảm biến xử lý dữ liệu lần đầu
  delay(4000);
}

void loop()
{
  // Đọc nhịp tim và SpO2
  MAX30102.getHeartbeatSPO2();

  int spo2 = MAX30102._sHeartbeatSPO2.SPO2;
  int heartRate = MAX30102._sHeartbeatSPO2.Heartbeat;
  float boardTemperature = MAX30102.getTemperature_C();

  Serial.println("===== KET QUA DO =====");

  if (heartRate > 0)
  {
    Serial.print("Nhip tim: ");
    Serial.print(heartRate);
    Serial.println(" BPM");
  }
  else
  {
    Serial.println("Nhip tim: Chua co du lieu");
  }

  if (spo2 > 0)
  {
    Serial.print("SpO2: ");
    Serial.print(spo2);
    Serial.println(" %");
  }
  else
  {
    Serial.println("SpO2: Chua co du lieu");
  }

  Serial.print("Nhiet do board: ");
  Serial.print(boardTemperature, 2);
  Serial.println(" do C");

  Serial.println("======================");
  Serial.println();

  // Module cập nhật dữ liệu khoảng 4 giây một lần
  delay(4000);
}
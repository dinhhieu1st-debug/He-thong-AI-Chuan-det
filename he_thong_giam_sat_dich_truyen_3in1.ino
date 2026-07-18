#include <Wire.h>
#include "DFRobot_BloodOxygen_S.h"
#include "HX711.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================================
// OLED 0.96 INCH I2C - SSD1306 128x64
// DUNG CHUNG I2C VOI MAX30102
// SDA = D2 / GPIO4, SCL = D1 / GPIO5
// =====================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledConnected = false;

// =====================================================
// 1. MAX30102 - NHIP TIM VA SpO2
// GIU NGUYEN CHAN I2C DA CAU HINH
// =====================================================
#define SDA_PIN D2
#define SCL_PIN D1
#define I2C_ADDRESS 0x57

DFRobot_BloodOxygen_S_I2C MAX30102(&Wire, I2C_ADDRESS);

int spo2 = 0;
int heartRate = 0;
bool max30102Connected = false;
bool max30102HasData = false;

// Lay mau moi 0.5 giay, chot ket qua sau 4 mau = 2 giay
const unsigned long SAMPLE_INTERVAL = 500;
const uint8_t SAMPLE_COUNT = 4;

const unsigned long MAX30102_RECONNECT_INTERVAL = 10000;
unsigned long lastMAX30102ReconnectTime = 0;
unsigned long lastSampleTime = 0;

int heartRateSamples[SAMPLE_COUNT];
int spo2Samples[SAMPLE_COUNT];
float weightSamples[SAMPLE_COUNT];

uint8_t currentSampleIndex = 0;
uint8_t heartRateValidSamples = 0;
uint8_t spo2ValidSamples = 0;
uint8_t weightValidSamples = 0;

// =====================================================
// 2. CAM BIEN QUANG DEM GIOT
// GIU NGUYEN CHAN D0 / GPIO16
// =====================================================
#define SENSOR_PIN 16
const unsigned long DEBOUNCE_TIME = 200;

unsigned long totalDrops = 0;
unsigned long lastDropTime = 0;
float lastDropIntervalSec = 0.0;
int lastState = HIGH;

// Cam bien thuong keo LOW khi giot di qua.
// Neu cam bien cua ban hoat dong nguoc, doi LOW thanh HIGH.
#define DROP_ACTIVE_STATE LOW

// Tinh toc do dua tren khoang cach trung binh giua cac giot.
// Luu toi da 20 khoang cach gan nhat de ket qua on dinh.
#define MAX_INTERVAL_SAMPLES 20
float dropIntervals[MAX_INTERVAL_SAMPLES];
uint8_t intervalSampleCount = 0;
uint8_t intervalWriteIndex = 0;

float averageDropIntervalSec = 0.0;
float dropsPerMinute = 0.0;

// =====================================================
// 3. LOADCELL HX711
// GIU NGUYEN CHAN DT=D6/GPIO12, SCK=D5/GPIO14
// =====================================================
#define LOADCELL_DOUT_PIN 12.
#define LOADCELL_SCK_PIN 14

const float CALIBRATION_FACTOR = 14000.0;
HX711 scale;

float currentWeightKg = 0.0;
bool hx711Connected = false;
bool scaleTared = false;

// Khong coi HX711 mat ket noi chi vi tre mot lan.
// Chi bao loi sau 6 lan doc that bai lien tiep.
uint8_t hx711FailCount = 0;
const uint8_t HX711_MAX_FAILS = 6;

// Ket qua hien thi duoc cap nhat moi 2 giay
unsigned long lastResultTime = 0;

// =====================================================
// LOC DU LIEU 4 MAU / 2 GIAY
// =====================================================
int averageIntSamples(const int *values, uint8_t count)
{
  if (count == 0) {
    return 0;
  }

  long sum = 0;
  for (uint8_t i = 0; i < count; i++) {
    sum += values[i];
  }

  return (int)((sum + count / 2) / count);
}

float averageFloatSamples(const float *values, uint8_t count)
{
  if (count == 0) {
    return 0.0;
  }

  float sum = 0.0;
  for (uint8_t i = 0; i < count; i++) {
    sum += values[i];
  }

  return sum / count;
}

void resetSampleBuffers()
{
  currentSampleIndex = 0;
  heartRateValidSamples = 0;
  spo2ValidSamples = 0;
  weightValidSamples = 0;
}

// =====================================================
// HAM HIEN THI OLED
// =====================================================
void printNoData()
{
  display.print("Khong du lieu");
}

void updateDropRateFromIntervals()
{
  if (intervalSampleCount == 0) {
    averageDropIntervalSec = 0.0;
    dropsPerMinute = 0.0;
    return;
  }

  float sum = 0.0;

  for (uint8_t i = 0; i < intervalSampleCount; i++) {
    sum += dropIntervals[i];
  }

  averageDropIntervalSec = sum / intervalSampleCount;

  if (averageDropIntervalSec > 0.0) {
    dropsPerMinute = 60.0 / averageDropIntervalSec;
  } else {
    dropsPerMinute = 0.0;
  }
}

void addDropInterval(float intervalSec)
{
  if (intervalSec <= 0.0) {
    return;
  }

  dropIntervals[intervalWriteIndex] = intervalSec;
  intervalWriteIndex =
      (intervalWriteIndex + 1) % MAX_INTERVAL_SAMPLES;

  if (intervalSampleCount < MAX_INTERVAL_SAMPLES) {
    intervalSampleCount++;
  }

  updateDropRateFromIntervals();
}

void updateOLED()
{
  if (!oledConnected) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Dong 1: Nhip tim
  display.setCursor(0, 0);
  display.print("Nhip tim: ");
  if (max30102Connected && max30102HasData && heartRate > 0) {
    display.print(heartRate);
    display.print(" BPM");
  } else {
    printNoData();
  }

  // Dong 2: SpO2
  display.setCursor(0, 11);
  display.print("SpO2: ");
  if (max30102Connected && max30102HasData && spo2 > 0) {
    display.print(spo2);
    display.print("%");
  } else {
    printNoData();
  }

  // Dong 3: Khoi luong
  display.setCursor(0, 22);
  display.print("Can: ");
  if (hx711Connected && scaleTared) {
    display.print(currentWeightKg, 3);
    display.print(" kg");
  } else {
    printNoData();
  }

  // Dong 4: Tong so giot
  display.setCursor(0, 33);
  display.print("Tong giot: ");
  display.print(totalDrops);

  // Dong 5: Khoang cach giua 2 giot gan nhat
  display.setCursor(0, 44);
  display.print("Khoang: ");
  if (totalDrops > 1 && lastDropIntervalSec > 0.0) {
    display.print(lastDropIntervalSec, 1);
    display.print(" giay");
  } else {
    printNoData();
  }

  // Dong 6: Toc do tinh tu khoang cach trung binh
  display.setCursor(0, 55);
  display.print("TB: ");
  if (intervalSampleCount > 0 && dropsPerMinute > 0.0) {
    display.print(dropsPerMinute, 1);
    display.print(" giot/ph");
  } else {
    printNoData();
  }

  display.display();
}

void showStartupTareScreen()
{
  if (!oledConnected) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 5);
  display.println("VUI LONG CHUA TREO");
  display.setCursor(0, 18);
  display.println("BICH DICH");

  display.setCursor(0, 38);
  display.println("DANG TRU BI...");
  display.display();
}

void showReadyToMonitorScreen()
{
  if (!oledConnected) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("HAY TREO BICH DICH");
  display.setCursor(0, 13);
  display.println("VA LAP HE THONG");
  display.setCursor(0, 26);
  display.println("DE BAT DAU");
  display.setCursor(0, 39);
  display.println("THEO DOI");

  display.setCursor(0, 55);
  display.println("Cho 3 giay...");
  display.display();
}

void showOLEDMessage(const char *line1, const char *line2 = nullptr)
{
  if (!oledConnected) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println(line1);

  if (line2 != nullptr) {
    display.setCursor(0, 30);
    display.println(line2);
  }

  display.display();
}

// =====================================================
// HAM DOC CAN
// =====================================================
float readWeightKg(uint8_t samples)
{
  if (!scale.wait_ready_timeout(250)) {
    if (hx711FailCount < 255) {
      hx711FailCount++;
    }

    if (hx711FailCount >= HX711_MAX_FAILS) {
      hx711Connected = false;
    }

    return currentWeightKg;
  }

  hx711FailCount = 0;
  hx711Connected = true;

  if (!scaleTared) {
    return currentWeightKg;
  }

  float weightKg = scale.get_units(samples);

  if (weightKg < 0.015 && weightKg > -0.015) {
    weightKg = 0.0;
  }

  currentWeightKg = weightKg;
  return currentWeightKg;
}

// =====================================================
// HAM DOC MAX30102
// =====================================================
bool isValidHeartRate(int value)
{
  return value >= 30 && value <= 240;
}

bool isValidSpO2(int value)
{
  return value >= 70 && value <= 100;
}

bool checkI2CDevice(uint8_t address)
{
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void sampleMAX30102()
{
  if (!max30102Connected) {
    return;
  }

  if (!checkI2CDevice(I2C_ADDRESS)) {
    Serial.println("[MAX30102] Mat ket noi I2C.");
    max30102Connected = false;
    max30102HasData = false;
    heartRate = 0;
    spo2 = 0;
    return;
  }

  MAX30102.getHeartbeatSPO2();

  int rawSpO2 = MAX30102._sHeartbeatSPO2.SPO2;
  int rawHeartRate = MAX30102._sHeartbeatSPO2.Heartbeat;

  if (isValidHeartRate(rawHeartRate) &&
      heartRateValidSamples < SAMPLE_COUNT) {
    heartRateSamples[heartRateValidSamples++] = rawHeartRate;
  }

  if (isValidSpO2(rawSpO2) &&
      spo2ValidSamples < SAMPLE_COUNT) {
    spo2Samples[spo2ValidSamples++] = rawSpO2;
  }
}

void publishFilteredResults()
{
  if (heartRateValidSamples > 0) {
    heartRate =
        averageIntSamples(heartRateSamples, heartRateValidSamples);
  } else {
    heartRate = 0;
  }

  if (spo2ValidSamples > 0) {
    spo2 = averageIntSamples(spo2Samples, spo2ValidSamples);
  } else {
    spo2 = 0;
  }

  max30102HasData = (heartRate > 0 || spo2 > 0);

  if (weightValidSamples > 0) {
    currentWeightKg =
        averageFloatSamples(weightSamples, weightValidSamples);

    if (currentWeightKg < 0.015 && currentWeightKg > -0.015) {
      currentWeightKg = 0.0;
    }
  }

  updateDropRateFromIntervals();

  printAllData();
  updateOLED();

  resetSampleBuffers();
}

void tryReconnectMAX30102()
{
  // Khong tu khoi tao lai MAX30102 khi dang chay.
  // MAX30102 va OLED dung chung I2C, viec begin lai lien tuc
  // khi cam bien khong phan hoi co the lam OLED bi treo.
  //
  // Muon thu lai MAX30102: kiem tra day roi nhan RESET ESP8266.
}

// =====================================================
// HAM XU LY DEM GIOT
// =====================================================
void handleDropSensor()
{
  int currentState = digitalRead(SENSOR_PIN);
  unsigned long currentTime = millis();

  // Chi dem khi tin hieu chuyen vao muc ACTIVE,
  // moi giot chi duoc dem mot lan.
  if (currentState == DROP_ACTIVE_STATE &&
      lastState != DROP_ACTIVE_STATE) {

    if (currentTime - lastDropTime > DEBOUNCE_TIME) {
      totalDrops++;

      if (totalDrops > 1) {
        lastDropIntervalSec =
            (currentTime - lastDropTime) / 1000.0;

        // Them khoang cach vua do vao bo tinh trung binh
        addDropInterval(lastDropIntervalSec);
      }

      lastDropTime = currentTime;

      if (hx711Connected && scaleTared) {
        readWeightKg(3);
      }

      Serial.print("-> CO GIOT! Tong: ");
      Serial.print(totalDrops);

      Serial.print(" | Khoang cach: ");
      if (totalDrops > 1) {
        Serial.print(lastDropIntervalSec, 2);
        Serial.print(" giay");
      } else {
        Serial.print("Khong du lieu");
      }

      Serial.print(" | Khoang TB: ");
      if (intervalSampleCount > 0) {
        Serial.print(averageDropIntervalSec, 2);
        Serial.print(" giay");
      } else {
        Serial.print("Khong du lieu");
      }

      Serial.print(" | Toc do TB: ");
      if (intervalSampleCount > 0) {
        Serial.print(dropsPerMinute, 1);
        Serial.print(" giot/phut");
      } else {
        Serial.print("Khong du lieu");
      }

      Serial.print(" | Can: ");
      if (hx711Connected && scaleTared) {
        Serial.print(currentWeightKg, 3);
        Serial.println(" kg");
      } else {
        Serial.println("Khong du lieu");
      }
    }
  }

  lastState = currentState;
}

// =====================================================
// LENH SERIAL
// Gui t hoac T de tru bi lai can
// =====================================================
void handleSerialCommand()
{
  if (Serial.available() <= 0) {
    return;
  }

  char ch = Serial.read();

  if (ch == 't' || ch == 'T') {
    if (scale.wait_ready_timeout(1000)) {
      Serial.println("[HX711] Dang tru bi...");
      showOLEDMessage("HX711 dang tru bi", "Vui long cho...");
      scale.tare(30);
      scaleTared = true;
      hx711Connected = true;
      hx711FailCount = 0;
      currentWeightKg = 0.0;
      Serial.println("[HX711] Tru bi xong.");
    } else {
      hx711Connected = false;
      scaleTared = false;
      Serial.println("[HX711] Khong co du lieu de tru bi.");
    }
  }

  while (Serial.available() > 0) {
    Serial.read();
  }
}

// =====================================================
// IN DU LIEU SERIAL
// =====================================================
void printAllData()
{
  Serial.print("[DU LIEU] ");

  Serial.print("Nhip tim: ");
  if (max30102Connected && max30102HasData && heartRate > 0) {
    Serial.print(heartRate);
    Serial.print(" BPM");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.print(" | SpO2: ");
  if (max30102Connected && max30102HasData && spo2 > 0) {
    Serial.print(spo2);
    Serial.print("%");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.print(" | Can: ");
  if (hx711Connected && scaleTared) {
    Serial.print(currentWeightKg, 3);
    Serial.print(" kg");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.print(" | Tong giot: ");
  Serial.print(totalDrops);

  Serial.print(" | Khoang cach giot: ");
  if (totalDrops > 1 && lastDropIntervalSec > 0.0) {
    Serial.print(lastDropIntervalSec, 2);
    Serial.print(" giay");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.print(" | Khoang cach TB: ");
  if (intervalSampleCount > 0) {
    Serial.print(averageDropIntervalSec, 2);
    Serial.print(" giay");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.print(" | Toc do TB: ");
  if (intervalSampleCount > 0 && dropsPerMinute > 0.0) {
    Serial.print(dropsPerMinute, 1);
    Serial.print(" giot/phut");
  } else {
    Serial.print("Khong du lieu");
  }

  Serial.println();
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(115200);
  delay(500);

  pinMode(SENSOR_PIN, INPUT);
  lastState = digitalRead(SENSOR_PIN);

  // Khoi tao bus I2C dung chung cho OLED va MAX30102
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(50000);
  Wire.setClockStretchLimit(150000);

  // Khoi tao OLED
  oledConnected = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);

  if (oledConnected) {
    showStartupTareScreen();
    Serial.println("[OLED] Ket noi thanh cong tai dia chi 0x3C.");
  } else {
    Serial.println("[OLED] Khong tim thay. He thong van chay Serial.");
  }

  // Khoi tao MAX30102, neu khong co thi bo qua
  Serial.println("[MAX30102] Dang khoi tao...");
  if (checkI2CDevice(I2C_ADDRESS) && MAX30102.begin()) {
    max30102Connected = true;
    MAX30102.sensorStartCollect();
    Serial.println("[MAX30102] Ket noi thanh cong.");
    Serial.println("[MAX30102] Dat ngon tay va giu yen 10-15 giay.");
  } else {
    max30102Connected = false;
    max30102HasData = false;
    Serial.println("[MAX30102] Khong tim thay - bo qua.");
  }

  // Khoi tao HX711, neu khong co thi bo qua
  Serial.println("[HX711] Dang khoi tao...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);

  if (scale.wait_ready_timeout(1500)) {
    hx711Connected = true;

    // Tu dong tru bi luc khoi dong
    showStartupTareScreen();
    Serial.println("[HX711] Tu dong tru bi 30 mau...");
    scale.tare(30);
    scaleTared = true;
    hx711FailCount = 0;
    currentWeightKg = 0.0;
    Serial.println("[HX711] Tru bi xong.");

    showReadyToMonitorScreen();
    delay(3000);
  } else {
    hx711Connected = false;
    scaleTared = false;
    Serial.println("[HX711] Khong tim thay - bo qua.");

    if (oledConnected) {
      showOLEDMessage("KHONG TIM THAY HX711", "Kiem tra day noi");
      delay(3000);
    }
  }

  if (max30102Connected) {
    delay(4000);
  }

  resetSampleBuffers();
  lastSampleTime = millis();
  lastResultTime = millis();

  Serial.println("HE THONG DA SAN SANG.");
  Serial.println("Gui 't' tren Serial neu muon tru bi lai can.");

  // Chuyen sang man hinh theo doi cac chi so
  updateOLED();
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  unsigned long currentTime = millis();

  // Quet cam bien giot lien tuc
  handleDropSensor();

  // Nhan lenh tru bi
  handleSerialCommand();

  // Tu thu ket noi lai neu MAX30102 bi mat
  tryReconnectMAX30102();

  // Lay mot bo mau moi 0.5 giay
  if (currentTime - lastSampleTime >= SAMPLE_INTERVAL) {
    lastSampleTime = currentTime;

    // 1. Lay mau MAX30102
    sampleMAX30102();

    // 2. Lay mau HX711
    // Moi lan lay 1 gia tri da trung binh noi bo tu 2 mau.
    if (scaleTared) {
      if (scale.wait_ready_timeout(250)) {
        hx711FailCount = 0;

        bool wasDisconnected = !hx711Connected;
        hx711Connected = true;

        if (wasDisconnected) {
          Serial.println("[HX711] Ket noi da on dinh tro lai.");
        }

        float sampleWeight = scale.get_units(2);

        // Chi nhan gia tri trong mien hop ly.
        if (sampleWeight > -10.0 && sampleWeight < 10.0 &&
            weightValidSamples < SAMPLE_COUNT) {
          weightSamples[weightValidSamples++] = sampleWeight;
        }
      } else {
        if (hx711FailCount < 255) {
          hx711FailCount++;
        }

        // Chi danh dau mat ket noi sau 6 lan loi lien tiep
        // tuong duong khoang 3 giay.
        if (hx711FailCount == HX711_MAX_FAILS) {
          hx711Connected = false;
          Serial.println("[HX711] Mat du lieu sau nhieu lan thu.");
        }
      }
    }

    currentSampleIndex++;

    // Du 4 mau thi chot ket qua mot lan sau 2 giay
    if (currentSampleIndex >= SAMPLE_COUNT) {
      lastResultTime = currentTime;
      publishFilteredResults();
    }
  }

  yield();
}
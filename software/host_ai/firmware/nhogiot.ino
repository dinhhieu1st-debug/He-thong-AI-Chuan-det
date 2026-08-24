/*
 ============================================================
   HE THONG DO GIOT DICH TRUYEN
   NodeMCU ESP8266 + Photodiode + Laser

   Cam bien:
       VCC -> 3V3
       GND -> GND
       DO  -> D0 (GPIO16)

   MUC TIEU:
   - Phat hien tung giot
   - Chong nhieu
   - Chong dem trung
   - Do khoang cach giua 2 giot
   - Ghi RAW DATASET de huan luyen AI/LSTM

   RAW DATA:
   timestamp_ms,drop_number,target_interval_ms,actual_interval_ms
 ============================================================
*/

#define SENSOR_PIN D0

const unsigned long TARGET_INTERVAL_MS = 1000;
const unsigned long DROP_CONFIRM_TIME = 12;
const unsigned long RELEASE_TIME = 25;
const unsigned long MIN_DROP_INTERVAL = 250;
const unsigned long NO_DROP_TIMEOUT = 10000;

#define AVG_SIZE 5
unsigned long intervalBuffer[AVG_SIZE];
byte bufferIndex = 0;
byte bufferCount = 0;

unsigned long totalDrops = 0;
unsigned long lastDropTime = 0;
unsigned long candidateStartTime = 0;
unsigned long releaseStartTime = 0;
int idleState = HIGH;

enum DropState {
  WAITING_DROP,
  CONFIRMING_DROP,
  WAITING_RELEASE,
  CONFIRMING_RELEASE
};

DropState state = WAITING_DROP;

float getAverageInterval() {
  if (bufferCount == 0) return 0;
  unsigned long sum = 0;
  for (byte i = 0; i < bufferCount; i++) sum += intervalBuffer[i];
  return (float)sum / bufferCount;
}

void addInterval(unsigned long interval) {
  intervalBuffer[bufferIndex] = interval;
  bufferIndex++;
  if (bufferIndex >= AVG_SIZE) bufferIndex = 0;
  if (bufferCount < AVG_SIZE) bufferCount++;
}

void registerDrop(unsigned long now) {
  if (lastDropTime == 0) {
    totalDrops++;
    lastDropTime = now;
    Serial.println();
    Serial.println("================================");
    Serial.print("GIOT SO: ");
    Serial.println(totalDrops);
    Serial.println("Giot dau tien - chua ghi dataset vi chua co interval.");
    return;
  }

  unsigned long interval = now - lastDropTime;

  if (interval < MIN_DROP_INTERVAL) {
    Serial.print("[BO QUA NHIEU] Xung = ");
    Serial.print(interval);
    Serial.println(" ms");
    return;
  }

  totalDrops++;
  addInterval(interval);

  float intervalSeconds = interval / 1000.0;
  float dropsPerMinute = 60000.0 / interval;
  float avgInterval = getAverageInterval();
  float avgSeconds = avgInterval / 1000.0;
  float avgDropsPerMinute = (avgInterval > 0) ? 60000.0 / avgInterval : 0;

  long errorMs = (long)interval - (long)TARGET_INTERVAL_MS;
  float errorPercent = ((float)errorMs / (float)TARGET_INTERVAL_MS) * 100.0;

  Serial.println();
  Serial.println("================================");
  Serial.print("GIOT SO             : "); Serial.println(totalDrops);
  Serial.print("Target interval     : "); Serial.print(TARGET_INTERVAL_MS); Serial.println(" ms");
  Serial.print("Khoang cach thuc te : "); Serial.print(interval); Serial.println(" ms");
  Serial.print("Khoang cach         : "); Serial.print(intervalSeconds, 3); Serial.println(" giay");
  Serial.print("Sai lech            : "); Serial.print(errorMs); Serial.println(" ms");
  Serial.print("Sai lech            : "); Serial.print(errorPercent, 2); Serial.println(" %");
  Serial.print("Toc do tuc thoi     : "); Serial.print(dropsPerMinute, 2); Serial.println(" giot/phut");
  Serial.print("TB "); Serial.print(bufferCount); Serial.print(" giot          : "); Serial.print(avgSeconds, 3); Serial.println(" giay");
  Serial.print("Toc do trung binh   : "); Serial.print(avgDropsPerMinute, 2); Serial.println(" giot/phut");

  // Mot dong CSV duy nhat cho moi giot hop le (tru giot dau tien).
  Serial.print(now); Serial.print(",");
  Serial.print(totalDrops); Serial.print(",");
  Serial.print(TARGET_INTERVAL_MS); Serial.print(",");
  Serial.println(interval);

  lastDropTime = now;
}

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" HE THONG GIAM SAT GIOT DICH TRUYEN");
  Serial.println(" NodeMCU ESP8266 + Photodiode + Laser");
  Serial.println("========================================");
  Serial.print("TARGET INTERVAL = ");
  Serial.print(TARGET_INTERVAL_MS);
  Serial.println(" ms");
  Serial.println("timestamp_ms,drop_number,target_interval_ms,actual_interval_ms");

  Serial.println("Dang do trang thai nen...");
  int highCount = 0;
  int lowCount = 0;
  for (int i = 0; i < 200; i++) {
    if (digitalRead(SENSOR_PIN) == HIGH) highCount++;
    else lowCount++;
    delay(5);
  }

  idleState = (highCount >= lowCount) ? HIGH : LOW;
  Serial.print("Trang thai nen = ");
  Serial.println(idleState == HIGH ? "HIGH" : "LOW");
  Serial.println("San sang. Cho giot dich...");
}

void loop() {
  unsigned long now = millis();
  int sensorState = digitalRead(SENSOR_PIN);

  if (state == WAITING_DROP) {
    if (sensorState != idleState) {
      candidateStartTime = now;
      state = CONFIRMING_DROP;
    }
  }
  else if (state == CONFIRMING_DROP) {
    if (sensorState == idleState) {
      state = WAITING_DROP;
    } else if (now - candidateStartTime >= DROP_CONFIRM_TIME) {
      registerDrop(candidateStartTime);
      state = WAITING_RELEASE;
    }
  }
  else if (state == WAITING_RELEASE) {
    if (sensorState == idleState) {
      releaseStartTime = now;
      state = CONFIRMING_RELEASE;
    }
  }
  else if (state == CONFIRMING_RELEASE) {
    if (sensorState != idleState) {
      state = WAITING_RELEASE;
    } else if (now - releaseStartTime >= RELEASE_TIME) {
      state = WAITING_DROP;
    }
  }

  static unsigned long lastNoDropPrint = 0;
  if (lastDropTime != 0 && now - lastDropTime > NO_DROP_TIMEOUT) {
    if (now - lastNoDropPrint >= 5000) {
      Serial.println("!!! CANH BAO: KHONG PHAT HIEN GIOT !!!");
      lastNoDropPrint = now;
    }
  }

  delay(1);
}

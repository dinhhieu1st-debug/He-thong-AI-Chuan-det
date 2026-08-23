#include "blood_oxygen.h"

#include "software_i2c.h"

#define BLOOD_OXYGEN_ADDRESS 0x57U
#define BLOOD_OXYGEN_READ_ATTEMPTS 3U

static bool connected;

static bool configure_sensor(void)
{
  if (!software_i2c_probe(BLOOD_OXYGEN_ADDRESS)) { return false; }
  const uint8_t start[] = { 0x20U, 0x00U, 0x01U };
  return software_i2c_write(BLOOD_OXYGEN_ADDRESS, start, sizeof(start));
}

bool blood_oxygen_init(void)
{
  connected = configure_sensor();
  return connected;
}

bool blood_oxygen_sample(int16_t *heart_rate, int16_t *spo2)
{
  if (!connected) {
    software_i2c_init();
    connected = configure_sensor();
    if (!connected) { return false; }
  }

  for (uint8_t attempt = 0U; attempt < BLOOD_OXYGEN_READ_ATTEMPTS; attempt++) {
    uint8_t reg = 0x0CU;
    uint8_t data[8] = { 0 };
    if (!software_i2c_write_read(BLOOD_OXYGEN_ADDRESS, &reg, 1U,
                                 data, sizeof(data))) {
      /* Recover a bus disturbed by OLED traffic/noise before retrying. */
      software_i2c_init();
      connected = configure_sensor();
      if (!connected) { return false; }
      continue;
    }

    uint32_t raw_hr = ((uint32_t)data[2] << 24)
                      | ((uint32_t)data[3] << 16)
                      | ((uint32_t)data[4] << 8)
                      | data[5];
    int16_t raw_spo2 = data[0];
    if (raw_hr >= 30U && raw_hr <= 240U
        && raw_spo2 >= 70 && raw_spo2 <= 100) {
      *heart_rate = (int16_t)raw_hr;
      *spo2 = raw_spo2;
      return true;
    }
  }

  return false;
}

bool blood_oxygen_connected(void) { return connected; }

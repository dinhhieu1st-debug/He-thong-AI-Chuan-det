#include "sl_i2cspm_instances.h"
#include "sl_sleeptimer.h"
#include <stdio.h>
#include <stdbool.h>

#define MAX30102_ADDR       0x57      // 7-bit
#define REG_HR_SPO2         0x0C
#define REG_TEMP            0x14
#define REG_COLLECT_CTRL    0x20

/* ---------- L?p I2C co b?n ---------- */

static bool max30102_write(uint8_t reg, const uint8_t *data, uint8_t len)
{
  uint8_t buf[8];
  buf[0] = reg;
  for (uint8_t i = 0; i < len; i++) buf[i + 1] = data[i];

  I2C_TransferSeq_TypeDef seq = {
    .addr        = MAX30102_ADDR << 1,
    .flags       = I2C_FLAG_WRITE,
    .buf[0].data = buf,
    .buf[0].len  = len + 1,
  };
  return I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone;
}

static bool max30102_read(uint8_t reg, uint8_t *data, uint8_t len)
{
  I2C_TransferSeq_TypeDef seq = {
    .addr        = MAX30102_ADDR << 1,
    .flags       = I2C_FLAG_WRITE_READ,
    .buf[0].data = &reg,
    .buf[0].len  = 1,
    .buf[1].data = data,
    .buf[1].len  = len,
  };
  return I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone;
}

/* Tuong duong begin(): ch? ping d?a ch?, xem có ACK không */
static bool max30102_begin(void)
{
  uint8_t dummy = 0;
  I2C_TransferSeq_TypeDef seq = {
    .addr        = MAX30102_ADDR << 1,
    .flags       = I2C_FLAG_WRITE,
    .buf[0].data = &dummy,
    .buf[0].len  = 0,
  };
  return I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone;
}

/* ---------- L?p ?ng d?ng ---------- */

static void max30102_start_collect(void)
{
  uint8_t wbuf[2] = { 0x00, 0x01 };
  max30102_write(REG_COLLECT_CTRL, wbuf, 2);
}

static void max30102_stop_collect(void)
{
  uint8_t wbuf[2] = { 0x00, 0x02 };
  max30102_write(REG_COLLECT_CTRL, wbuf, 2);
}

static void max30102_get_hr_spo2(int *spo2, int *heartbeat)
{
  uint8_t rbuf[8] = { 0 };
  max30102_read(REG_HR_SPO2, rbuf, 8);

  *spo2 = rbuf[0];
  if (*spo2 == 0) *spo2 = -1;

  *heartbeat = ((uint32_t)rbuf[2] << 24) | ((uint32_t)rbuf[3] << 16)
             | ((uint32_t)rbuf[4] << 8)  | ((uint32_t)rbuf[5]);
  if (*heartbeat == 0) *heartbeat = -1;
}

static float max30102_get_temperature_c(void)
{
  uint8_t tbuf[2] = { 0 };
  max30102_read(REG_TEMP, tbuf, 2);
  return (float)tbuf[0] + (float)tbuf[1] / 100.0f;
}

/* ---------- app_init / app_process_action ---------- */

void app_init(void)
{
  printf("\r\nKhoi dong xG26 + MAX30102 V2.0...\r\n");

  while (!max30102_begin()) {
    printf("Khong tim thay MAX30102!\r\n");
    printf("Kiem tra: 3V3, GND, SDA, SCL\r\n");
    sl_sleeptimer_delay_millisecond(1000);
  }

  printf("Ket noi MAX30102 thanh cong!\r\n");
  printf("Bat dau do...\r\n");
  printf("Dat dau ngon tay len mat cam bien.\r\n");
  printf("--------------------------------\r\n");

  max30102_start_collect();
  sl_sleeptimer_delay_millisecond(4000);
}

void app_process_action(void)
{
  int spo2, heart_rate;
  max30102_get_hr_spo2(&spo2, &heart_rate);
  float board_temp = max30102_get_temperature_c();

  printf("===== KET QUA DO =====\r\n");

  if (heart_rate > 0) printf("Nhip tim: %d BPM\r\n", heart_rate);
  else                printf("Nhip tim: Chua co du lieu\r\n");

  if (spo2 > 0) printf("SpO2: %d %%\r\n", spo2);
  else          printf("SpO2: Chua co du lieu\r\n");

  printf("Nhiet do board: %.2f do C\r\n", board_temp);
  printf("======================\r\n\r\n");

  sl_sleeptimer_delay_millisecond(4000);
}

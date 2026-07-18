#include "sl_i2cspm_instances.h"
#include "sl_sleeptimer.h"
#include <stdio.h>
#include <stdbool.h>
#include "em_i2c.h"
#include "sl_i2cspm.h"

/*
 * Cam bien la module DFRobot Gravity MAX30102 V2.0 (SEN0344), co vi xu ly
 * rieng ben trong tu tinh san nhip tim/SpO2 va expose qua giao thuc lenh
 * rieng cua DFRobot (thu vien DFRobot_BloodOxygen_S), KHONG phai thanh ghi
 * tho theo datasheet chip MAX30102 goc. Cac dia chi thanh ghi ben duoi
 * duoc doi chieu truc tiep tu thu vien DFRobot_BloodOxygen_S (ban .cpp).
 */
#define MAX30102_ADDR       0x57      // 7-bit
#define REG_HR_SPO2         0x0C
#define REG_COLLECT_CTRL    0x20

/* ---------- Lop I2C co ban ---------- */

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
  return I2CSPM_Transfer(sl_i2cspm_mikroe, &seq) == i2cTransferDone;
}

/*
 * Thu vien goc DFRobot doc thanh ghi bang 2 giao dich I2C rieng biet:
 * Wire.beginTransmission()+write(reg)+endTransmission() (endTransmission()
 * khong tham so se tu gui STOP), roi Wire.requestFrom() mo mot START moi
 * de doc. Day KHONG phai la mot giao dich WRITE_READ voi repeated-start.
 * Vi xu ly phu ben trong module co the khong xu ly dung repeated-start
 * nen phai lam dung 2 buoc tach biet nhu ban goc, thay vi gop thanh
 * I2C_FLAG_WRITE_READ.
 */
static bool max30102_read(uint8_t reg, uint8_t *data, uint8_t len)
{
  I2C_TransferSeq_TypeDef wseq = {
    .addr        = MAX30102_ADDR << 1,
    .flags       = I2C_FLAG_WRITE,
    .buf[0].data = &reg,
    .buf[0].len  = 1,
  };
  if (I2CSPM_Transfer(sl_i2cspm_mikroe, &wseq) != i2cTransferDone) return false;

  I2C_TransferSeq_TypeDef rseq = {
    .addr        = MAX30102_ADDR << 1,
    .flags       = I2C_FLAG_READ,
    .buf[0].data = data,
    .buf[0].len  = len,
  };
  return I2CSPM_Transfer(sl_i2cspm_mikroe, &rseq) == i2cTransferDone;
}

/*
 * Thu vien goc cua DFRobot ping dia chi bang write 0-byte
 * (Wire.beginTransmission()+endTransmission(), khong write() gi).
 * Driver I2CSPM cua SDK xG26 (sl_i2cspm.c) tu choi thang write 0-byte
 * (tra ve i2cTransferUsageFault ma khong gui gi len bus), nen kieu ping
 * do se luon fail o day. Doc thu 1 byte bat ky tu REG_HR_SPO2 de kiem
 * tra ACK thay the - gia tri doc duoc khong quan trong, chi can co ACK.
 */
static bool max30102_begin(void)
{
  uint8_t dummy;
  return max30102_read(REG_HR_SPO2, &dummy, 1);
}

/* ---------- Lop ung dung ---------- */

static void max30102_start_collect(void)
{
  uint8_t wbuf[2] = { 0x00, 0x01 };
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

/* ---------- app_init / app_process_action ---------- */

void app_init(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);   // tat dem -> printf hien ngay (test lai code cu)
  printf("HELLO\r\n");
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

  printf("===== KET QUA DO =====\r\n");

  if (heart_rate > 0) printf("Nhip tim: %d BPM\r\n", heart_rate);
  else                printf("Nhip tim: Chua co du lieu\r\n");

  if (spo2 > 0) printf("SpO2: %d %%\r\n", spo2);
  else          printf("SpO2: Chua co du lieu\r\n");

  printf("======================\r\n\r\n");

  sl_sleeptimer_delay_millisecond(4000);
}

#include "hx711_sensor.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "em_core.h"
#include "em_gpio.h"
#include "sl_sleeptimer.h"
#include "sl_udelay.h"

#define HX711_DOUT_PORT          gpioPortC
#define HX711_DOUT_PIN           1U
#define HX711_SCK_PORT           gpioPortC
#define HX711_SCK_PIN            3U

/* He so hieu chuan cho bich truyen 1050ml: delta +364033 / 1050g = +346.7 */
static float calibration_factor = 346.7f;

static int32_t tare_offset = 0;
static bool is_tared = false;
static uint32_t last_loop_ms = 0;
static uint32_t step_timer_ms = 0;
static int64_t tare_accum = 0;
static uint32_t tare_samples = 0;
static float current_weight_g = 0.0f;

enum {
  STEP_CHECK_READY = 0,
  STEP_WAIT_EMPTY,
  STEP_TARING,
  STEP_RUNNING
};
static int test_step = STEP_CHECK_READY;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

static bool hx711_is_ready(void)
{
  return GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) == 0;
}

static int32_t hx711_read(void)
{
  uint32_t value = 0;
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_ATOMIC();

  for (uint32_t bit = 0; bit < 24U; bit++) {
    GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(2);
    value = (value << 1) |
            (GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) ? 1U : 0U);
    GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(2);
  }

  /* Pulse 25: Gain 128 tren Kenh A */
  GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
  sl_udelay_wait(2);
  GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
  sl_udelay_wait(2);
  CORE_EXIT_ATOMIC();

  if ((value & 0x00800000UL) != 0U) {
    value |= 0xFF000000UL;
  }
  return (int32_t)value;
}

void hx711_sensor_init(void)
{
  GPIO_PinModeSet(HX711_SCK_PORT, HX711_SCK_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(HX711_DOUT_PORT, HX711_DOUT_PIN, gpioModeInput, 0);

  test_step = STEP_CHECK_READY;
  step_timer_ms = now_ms();
  last_loop_ms = now_ms();
  is_tared = false;

  printf("\r\n\r\n==================================================\r\n");
  printf("             TEST CAN HX711 (EFR32xG26)\r\n");
  printf("  Chan dau noi: DOUT = PC01 | SCK = PC03\r\n");
  printf("                VCC  = 3.3V | GND = GND\r\n");
  printf("  Calibration factor: %.1f\r\n", calibration_factor);
  printf("==================================================\r\n");
  printf("Dang kiem tra ket noi HX711...\r\n");
}

void hx711_sensor_poll(void)
{
  uint32_t ms = now_ms();

  switch (test_step) {
    case STEP_CHECK_READY:
      if (hx711_is_ready()) {
        printf("-> Da tim thay HX711!\r\n");
        printf("Dang tru bi...\r\nKhong dat vat len can.\r\n");
        step_timer_ms = ms;
        test_step = STEP_WAIT_EMPTY;
      } else {
        if ((ms - step_timer_ms) >= 2000U) {
          step_timer_ms = ms;
          printf("Khong tim thay HX711! (Kiem tra lai day PC01/PC03/3V3/GND)\r\n");
        }
      }
      break;

    case STEP_WAIT_EMPTY:
      if ((ms - step_timer_ms) >= 1500U) {
        tare_accum = 0;
        tare_samples = 0;
        last_loop_ms = ms;
        test_step = STEP_TARING;
      }
      break;

    case STEP_TARING:
      if (hx711_is_ready() && (ms - last_loop_ms) >= 110U) {
        last_loop_ms = ms;
        int32_t sample = hx711_read();
        tare_accum += sample;
        tare_samples++;
        printf("  [Tru bi %lu/10] raw = %ld\r\n", (unsigned long)tare_samples, (long)sample);
        if (tare_samples >= 10U) {
          tare_offset = (int32_t)(tare_accum / 10);
          is_tared = true;
          printf("Da tru bi thanh cong! (Offset = %ld)\r\n", (long)tare_offset);
          printf("Bat dau can...\r\n");
          printf("--------------------------------------------------\r\n");
          last_loop_ms = ms;
          test_step = STEP_RUNNING;
        }
      }
      break;

    case STEP_RUNNING:
      if ((ms - last_loop_ms) >= 500U) {
        last_loop_ms = ms;
        if (hx711_is_ready()) {
          int32_t raw = hx711_read();
          float weight = (float)(raw - tare_offset) / calibration_factor;
          if (weight > -0.2f && weight < 0.2f) { weight = 0.0f; }
          current_weight_g = weight;
          printf("Khoi luong: %.2f g  (raw=%ld)\r\n", weight, (long)raw);
        } else {
          printf("Mat ket noi HX711!\r\n");
        }
      }
      break;

    default:
      test_step = STEP_CHECK_READY;
      break;
  }
}

void hx711_sensor_tare(void)
{
  printf("\r\n[TRU BI LAI] Khong dat vat len can...\r\n");
  tare_accum = 0;
  tare_samples = 0;
  is_tared = false;
  last_loop_ms = now_ms();
  test_step = STEP_TARING;
}

bool hx711_sensor_connected(void) { return (test_step == STEP_RUNNING); }
bool hx711_sensor_tared(void) { return is_tared; }
float hx711_sensor_weight_kg(void) { return (current_weight_g > 0.0f) ? (current_weight_g / 1000.0f) : 0.0f; }

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
#define HX711_TARE_SAMPLES       30U
#define HX711_REPORT_PERIOD_MS   500U
#define HX711_WAIT_REPORT_MS     2000U

static uint32_t tare_count;
static int64_t tare_sum;
static int32_t tare_offset;
static bool tare_done;
static uint32_t last_report_ms;
static uint32_t last_wait_report_ms;
static uint32_t sample_count;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

static bool hx711_ready(void)
{
  return GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) == 0;
}

static int32_t hx711_read_raw(void)
{
  uint32_t value = 0;
  CORE_DECLARE_IRQ_STATE;
  CORE_ENTER_ATOMIC();

  for (uint32_t bit = 0; bit < 24U; bit++) {
    GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(1);
    value = (value << 1) |
            (GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) ? 1U : 0U);
    GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(1);
  }

  /* Pulse 25 selects channel A, gain 128 for the next conversion. */
  GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
  sl_udelay_wait(1);
  GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
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
  printf("[HX711] DOUT=PC01, SCK=PC03. Keep loadcell EMPTY for automatic tare.\r\n");
}

void hx711_sensor_poll(void)
{
  uint32_t ms = now_ms();
  if (!hx711_ready()) {
    if ((ms - last_wait_report_ms) >= HX711_WAIT_REPORT_MS) {
      last_wait_report_ms = ms;
      printf("[HX711] Waiting for data: DOUT=HIGH. Check 3V3/GND/PC01/PC03.\r\n");
    }
    return;
  }

  int32_t raw = hx711_read_raw();
  sample_count++;

  if (!tare_done) {
    tare_sum += raw;
    tare_count++;
    if (tare_count >= HX711_TARE_SAMPLES) {
      tare_offset = (int32_t)(tare_sum / (int64_t)HX711_TARE_SAMPLES);
      tare_done = true;
      printf("[HX711] TARE OK: offset=%ld from %lu samples\r\n",
             (long)tare_offset, (unsigned long)tare_count);
      printf("[HX711] Put a known load on the loadcell now.\r\n");
    }
    return;
  }

  if ((ms - last_report_ms) >= HX711_REPORT_PERIOD_MS) {
    int32_t delta = raw - tare_offset;
    last_report_ms = ms;
    printf("[HX711] sample=%lu raw=%ld delta=%ld DOUT=LOW\r\n",
           (unsigned long)sample_count, (long)raw, (long)delta);
  }
}

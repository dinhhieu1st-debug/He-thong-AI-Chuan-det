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
#define HX711_REPORT_PERIOD_MS   2000U
#define HX711_WAIT_REPORT_MS     5000U
#define HX711_CALIBRATION_FACTOR 14000.0f
#define HX711_DISCONNECT_MS      3000U

static uint32_t tare_count;
static int64_t tare_sum;
static int32_t tare_offset;
static bool tare_done;
static uint32_t last_report_ms;
static uint32_t last_wait_report_ms;
static uint32_t sample_count;
static bool connected;
static float weight_kg;
static uint32_t last_data_ms;

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
    sl_udelay_wait(2);
    value = (value << 1) |
            (GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) ? 1U : 0U);
    GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(2);
  }

  /* Pulse 25 selects channel A, gain 128 for the next conversion.
   * This 25th pulse is required by HX711 to pull DOUT back to HIGH. */
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
  /* Diagnostic check of PC01 and PC03 */
  GPIO_PinModeSet(HX711_DOUT_PORT, HX711_DOUT_PIN, gpioModeInputPull, 0);
  sl_udelay_wait(50);
  int dout_pd = GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN);

  GPIO_PinModeSet(HX711_DOUT_PORT, HX711_DOUT_PIN, gpioModeInputPull, 1);
  sl_udelay_wait(50);
  int dout_pu = GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN);

  GPIO_PinModeSet(HX711_SCK_PORT, HX711_SCK_PIN, gpioModeInputPull, 0);
  sl_udelay_wait(50);
  int sck_pd = GPIO_PinInGet(HX711_SCK_PORT, HX711_SCK_PIN);

  GPIO_PinModeSet(HX711_SCK_PORT, HX711_SCK_PIN, gpioModeInputPull, 1);
  sl_udelay_wait(50);
  int sck_pu = GPIO_PinInGet(HX711_SCK_PORT, HX711_SCK_PIN);

  printf("\r\n--- [HX711 PIN DIAGNOSTIC] ---\r\n");
  printf("  PC01 (DOUT): PD=%d PU=%d -> %s\r\n",
         dout_pd, dout_pu,
         (dout_pd == 0 && dout_pu == 1) ? "FLOATING/OPEN" :
         (dout_pd == 0 && dout_pu == 0) ? "TIED TO GND (0V / Shorted)" :
         (dout_pd == 1 && dout_pu == 1) ? "ACTIVE HIGH (3.3V / VCC)" : "OTHER");
  printf("  PC03 (SCK) : PD=%d PU=%d -> %s\r\n",
         sck_pd, sck_pu,
         (sck_pd == 0 && sck_pu == 1) ? "FLOATING/OPEN" :
         (sck_pd == 0 && sck_pu == 0) ? "TIED TO GND (0V / Shorted)" :
         (sck_pd == 1 && sck_pu == 1) ? "ACTIVE HIGH (3.3V / VCC)" : "OTHER");
  printf("-------------------------------\r\n");

  /* Restore normal operating modes */
  GPIO_PinModeSet(HX711_SCK_PORT, HX711_SCK_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(HX711_DOUT_PORT, HX711_DOUT_PIN, gpioModeInputPull, 1);
  printf("[HX711] DOUT=PC01, SCK=PC03 configured. Keep loadcell EMPTY for automatic tare.\r\n");
}

static uint32_t last_sample_time_ms = 0U;

void hx711_sensor_poll(void)
{
  uint32_t ms = now_ms();
  if (!hx711_ready()) {
    if (connected && (ms - last_data_ms) >= HX711_DISCONNECT_MS) {
      connected = false;
    }
    if ((ms - last_wait_report_ms) >= HX711_WAIT_REPORT_MS) {
      last_wait_report_ms = ms;
      printf("[HX711] Waiting for data: DOUT=HIGH. Check 3V3/GND/PC01/PC03.\r\n");
    }
    return;
  }

  /* An HX711 chip at 10Hz/80Hz cannot produce a new sample faster than ~10ms.
   * If DOUT is permanently stuck LOW (short to GND or SCK broken), enforce a minimum gap
   * to avoid spinning at 100% CPU and fake-sampling tens of thousands of zeros. */
  if ((ms - last_sample_time_ms) < 10U) {
    return;
  }
  last_sample_time_ms = ms;

  int32_t raw = hx711_read_raw();
  connected = true;
  last_data_ms = ms;
  sample_count++;

  /* Check if DOUT stayed LOW after pulse 25. If so, HX711 did not acknowledge clock. */
  bool dout_stuck_low = (GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) == 0);

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
    weight_kg = (float)delta / HX711_CALIBRATION_FACTOR;
    if (weight_kg < 0.0f) { weight_kg = -weight_kg; }
    if (weight_kg > -0.015f && weight_kg < 0.015f) { weight_kg = 0.0f; }
    last_report_ms = ms;
    printf("[HX711] sample=%lu raw=%ld (0x%06lX) delta=%ld wt=%.3fkg DOUT=%s%s\r\n",
           (unsigned long)sample_count, (long)raw, (unsigned long)(raw & 0x00FFFFFFUL),
           (long)delta, weight_kg,
           dout_stuck_low ? "STUCK_LOW" : "HIGH",
           dout_stuck_low ? " (CHECK SCK PC03 / DOUT PC01!)" : "");
  }
}

void hx711_sensor_tare(void)
{
  tare_count = 0U;
  tare_sum = 0;
  tare_offset = 0;
  tare_done = false;
  weight_kg = 0.0f;
}

bool hx711_sensor_connected(void) { return connected; }
bool hx711_sensor_tared(void) { return tare_done; }
float hx711_sensor_weight_kg(void) { return weight_kg; }

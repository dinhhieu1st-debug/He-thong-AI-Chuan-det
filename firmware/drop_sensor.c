#include "drop_sensor.h"

#include <stdbool.h>
#include <stdio.h>

#include "em_gpio.h"
#include "sl_gpio.h"
#include "sl_sleeptimer.h"

#define DROP_PORT                 gpioPortD
#define DROP_PIN                  2U
#define CALIBRATION_TIME_MS       1000U
#define STATUS_PERIOD_MS          1000U
#define MIN_VALID_PULSE_US        300U
#define MAX_VALID_PULSE_US        100000U
#define INTERVAL_SAMPLES          20U
#define MIN_DROP_GAP_DEFAULT_MS   200U
#define MIN_DROP_GAP_MAX_MS       800U

static bool calibrated;
static volatile bool previous_level;
static bool idle_level;
static volatile bool pulse_active;
static uint32_t calibration_start_ms;
static uint32_t calibration_high_samples;
static uint32_t calibration_low_samples;
static volatile uint32_t pulse_start_us;
static uint32_t last_status_ms;
static volatile uint32_t valid_pulse_count;
static volatile uint32_t rejected_pulse_count;
static volatile uint32_t transition_count;
static volatile uint32_t last_drop_ms;
static volatile uint32_t last_interval_ms;
static volatile uint32_t interval_samples[INTERVAL_SAMPLES];
static volatile uint8_t interval_count;
static volatile uint8_t interval_index;
static volatile uint32_t blank_until_ms;
static volatile uint32_t min_drop_gap_ms = MIN_DROP_GAP_DEFAULT_MS;
static int32_t drop_interrupt_number = SL_GPIO_INTERRUPT_UNAVAILABLE;
static uint32_t last_reported_pulse_count;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

static uint32_t now_us(void)
{
  uint64_t ticks = (uint64_t)sl_sleeptimer_get_tick_count();
  uint64_t frequency = (uint64_t)sl_sleeptimer_get_timer_frequency();
  return (uint32_t)((ticks * 1000000ULL) / frequency);
}

static void drop_edge_callback(uint8_t interrupt_number, void *context)
{
  (void)interrupt_number;
  (void)context;
  uint32_t ms = now_ms();
  bool level = GPIO_PinInGet(DROP_PORT, DROP_PIN) != 0;

  if (!calibrated || (int32_t)(ms - blank_until_ms) < 0) {
    previous_level = level;
    pulse_active = false;
    return;
  }
  if (level == previous_level) { return; }

  uint32_t us = now_us();
  transition_count++;
  if (level != idle_level) {
    pulse_active = true;
    pulse_start_us = us;
  } else if (pulse_active) {
    uint32_t width_us = us - pulse_start_us;
    pulse_active = false;
    if (width_us >= MIN_VALID_PULSE_US && width_us <= MAX_VALID_PULSE_US) {
      uint32_t gap_ms = ms - last_drop_ms;
      if (last_drop_ms != 0U && gap_ms < min_drop_gap_ms) {
        rejected_pulse_count++;
      } else {
        valid_pulse_count++;
        if (last_drop_ms != 0U) {
          last_interval_ms = gap_ms;
          interval_samples[interval_index] = gap_ms;
          interval_index = (uint8_t)((interval_index + 1U) % INTERVAL_SAMPLES);
          if (interval_count < INTERVAL_SAMPLES) { interval_count++; }
        }
        last_drop_ms = ms;
      }
    } else {
      rejected_pulse_count++;
    }
  }
  previous_level = level;
}

void drop_sensor_init(void)
{
  GPIO_PinModeSet(DROP_PORT, DROP_PIN, gpioModeInput, 0);
  previous_level = GPIO_PinInGet(DROP_PORT, DROP_PIN) != 0;
  calibration_start_ms = now_ms();
  last_status_ms = calibration_start_ms;
  const sl_gpio_t drop_gpio = { (uint8_t)DROP_PORT, DROP_PIN };
  (void)sl_gpio_configure_external_interrupt(&drop_gpio,
                                             &drop_interrupt_number,
                                             SL_GPIO_INTERRUPT_RISING_FALLING_EDGE,
                                             drop_edge_callback,
                                             NULL);

  printf("[DROP] D0/OUT=PD02. Keep beam clear for 1 second...\r\n");
}

void drop_sensor_poll(void)
{
  uint32_t ms = now_ms();
  bool level = GPIO_PinInGet(DROP_PORT, DROP_PIN) != 0;

  if ((int32_t)(ms - blank_until_ms) < 0) {
    previous_level = level;
    pulse_active = false;
    return;
  }

  if (!calibrated) {
    if (level) { calibration_high_samples++; }
    else { calibration_low_samples++; }

    if ((ms - calibration_start_ms) >= CALIBRATION_TIME_MS) {
      idle_level = calibration_high_samples >= calibration_low_samples;
      previous_level = level;
      calibrated = true;
      last_status_ms = ms;
      printf("[DROP] Calibration OK: idle=%s, active=%s\r\n",
             idle_level ? "HIGH" : "LOW",
             idle_level ? "LOW" : "HIGH");
    }
    return;
  }

  if (valid_pulse_count != last_reported_pulse_count) {
    last_reported_pulse_count = valid_pulse_count;
    printf("[DROP] PULSE #%lu interval=%lu ms IRQ\r\n",
           (unsigned long)valid_pulse_count, (unsigned long)last_interval_ms);
  }

  if ((ms - last_status_ms) >= STATUS_PERIOD_MS) {
    last_status_ms = ms;
    printf("[DROP] level=%s pulses=%lu rejected=%lu transitions=%lu\r\n",
           level ? "HIGH" : "LOW",
           (unsigned long)valid_pulse_count,
           (unsigned long)rejected_pulse_count,
           (unsigned long)transition_count);
  }
}

uint32_t drop_sensor_count(void)
{
  return valid_pulse_count;
}

float drop_sensor_last_interval_seconds(void)
{
  return (float)last_interval_ms / 1000.0f;
}

float drop_sensor_average_interval_seconds(void)
{
  if (interval_count == 0U) { return 0.0f; }
  uint64_t sum = 0U;
  for (uint8_t i = 0U; i < interval_count; i++) { sum += interval_samples[i]; }
  return (float)sum / (float)interval_count / 1000.0f;
}

float drop_sensor_median_interval_seconds(void)
{
  uint32_t sorted[INTERVAL_SAMPLES];
  if (interval_count == 0U) { return 0.0f; }
  for (uint8_t i = 0U; i < interval_count; i++) { sorted[i] = interval_samples[i]; }
  for (uint8_t i = 1U; i < interval_count; i++) {
    uint32_t value = sorted[i];
    uint8_t j = i;
    while (j > 0U && sorted[j - 1U] > value) {
      sorted[j] = sorted[j - 1U];
      j--;
    }
    sorted[j] = value;
  }
  uint32_t median = sorted[interval_count / 2U];
  if ((interval_count & 1U) == 0U) {
    median = (sorted[interval_count / 2U - 1U] + median) / 2U;
  }
  return (float)median / 1000.0f;
}

float drop_sensor_drops_per_minute(void)
{
  float interval = drop_sensor_average_interval_seconds();
  return interval > 0.0f ? 60.0f / interval : 0.0f;
}

uint8_t drop_sensor_interval_count(void) { return interval_count; }

uint32_t drop_sensor_last_drop_ms(void) { return last_drop_ms; }
bool drop_sensor_calibrated(void) { return calibrated; }

void drop_sensor_reset_statistics(void)
{
  valid_pulse_count = 0U;
  rejected_pulse_count = 0U;
  transition_count = 0U;
  last_drop_ms = 0U;
  last_interval_ms = 0U;
  interval_count = 0U;
  interval_index = 0U;
  last_reported_pulse_count = 0U;
}

void drop_sensor_blank_until(uint32_t time_ms) { blank_until_ms = time_ms; }

void drop_sensor_set_min_gap_ms(uint32_t value_ms)
{
  if (value_ms < MIN_DROP_GAP_DEFAULT_MS) { value_ms = MIN_DROP_GAP_DEFAULT_MS; }
  if (value_ms > MIN_DROP_GAP_MAX_MS) { value_ms = MIN_DROP_GAP_MAX_MS; }
  min_drop_gap_ms = value_ms;
  printf("[DROP] Anti-double-pulse gap=%lu ms\r\n", (unsigned long)value_ms);
}

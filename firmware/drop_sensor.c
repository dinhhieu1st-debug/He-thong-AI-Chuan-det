#include "drop_sensor.h"

#include <stdbool.h>
#include <stdio.h>

#include "em_gpio.h"
#include "sl_sleeptimer.h"

#define DROP_PORT                 gpioPortD
#define DROP_PIN                  2U
#define CALIBRATION_TIME_MS       1000U
#define STATUS_PERIOD_MS          1000U
#define MIN_VALID_PULSE_US        300U
#define MAX_VALID_PULSE_US        100000U

static bool calibrated;
static bool previous_level;
static bool idle_level;
static bool pulse_active;
static uint32_t calibration_start_ms;
static uint32_t calibration_high_samples;
static uint32_t calibration_low_samples;
static uint32_t pulse_start_us;
static uint32_t last_status_ms;
static uint32_t valid_pulse_count;
static uint32_t rejected_pulse_count;
static uint32_t transition_count;

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

void drop_sensor_init(void)
{
  GPIO_PinModeSet(DROP_PORT, DROP_PIN, gpioModeInput, 0);
  previous_level = GPIO_PinInGet(DROP_PORT, DROP_PIN) != 0;
  calibration_start_ms = now_ms();
  last_status_ms = calibration_start_ms;

  printf("[DROP] D0/OUT=PD02. Keep beam clear for 1 second...\r\n");
}

void drop_sensor_poll(void)
{
  uint32_t ms = now_ms();
  bool level = GPIO_PinInGet(DROP_PORT, DROP_PIN) != 0;

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

  if (level != previous_level) {
    uint32_t us = now_us();
    transition_count++;

    if (level != idle_level) {
      pulse_active = true;
      pulse_start_us = us;
    } else if (pulse_active) {
      uint32_t width_us = us - pulse_start_us;
      pulse_active = false;
      if (width_us >= MIN_VALID_PULSE_US && width_us <= MAX_VALID_PULSE_US) {
        valid_pulse_count++;
        printf("[DROP] PULSE #%lu width=%lu us OK\r\n",
               (unsigned long)valid_pulse_count, (unsigned long)width_us);
      } else {
        rejected_pulse_count++;
        printf("[DROP] Noise/rejected width=%lu us\r\n",
               (unsigned long)width_us);
      }
    }
    previous_level = level;
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

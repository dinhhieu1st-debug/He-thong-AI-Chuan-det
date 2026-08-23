#include "app.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blood_oxygen.h"
#include "drop_sensor.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "hx711_sensor.h"
#include "oled_display.h"
#include "sl_iostream.h"
#include "sl_iostream_init_eusart_instances.h"
#include "sl_sleeptimer.h"
#include "af.h"
#include "software_i2c.h"
#include "vitals_ai.h"
#include "zap-id.h"

#define GREEN_LED_PORT  gpioPortA
#define GREEN_LED_PIN   7U
#define YELLOW_LED_PORT gpioPortA
#define YELLOW_LED_PIN  4U
#define RED_LED_PORT    gpioPortA
#define RED_LED_PIN     5U
#define BUZZER_PORT     gpioPortC
#define BUZZER_PIN      6U
#define TARE_PORT       gpioPortB
#define TARE_PIN        0U
#define SAMPLE_INTERVAL_MS 250U
#define SAMPLE_COUNT       4U
#define TARE_TIME_MS       10000U
#define VITALS_HOLD_MS     3000U

#define SMART_IV_ENDPOINT          2U
#define SMART_IV_CLUSTER_ID        0xFC01U
#define SMART_IV_MANUFACTURER_CODE 0x1049U

#define ATTR_HEART_RATE            0x0000U
#define ATTR_SPO2                  0x0001U
#define ATTR_FLOW_RATIO            0x0002U
#define ATTR_DROP_RATIO            0x0003U
#define ATTR_ALARM_BITMAP          0x0004U
#define ATTR_WEIGHT_G              0x0005U
#define ATTR_DROPS_PER_MIN         0x0006U
#define ATTR_TARGET_FLOW_ML_H      0x0007U
#define ATTR_TARGET_DROPS_PER_MIN  0x0008U
#define ATTR_TS_FLAGS              0x000FU
#define ATTR_HR_FORECAST_16S       0x0010U
#define ATTR_SPO2_FORECAST_16S     0x0011U
#define ATTR_HR_TREND_BPM_PER_MIN  0x0012U
#define ATTR_TS_ANOMALY_SCORE_X100 0x0013U
#define ATTR_DROPS_FORECAST_16S    0x0014U
#define ATTR_DROPS_TREND_DPM_MIN   0x0015U
#define ATTR_REMAINING_ML          0x0016U
#define ATTR_REMAINING_MIN         0x0017U
#define ATTR_MONITORING_ACTIVE     0x0018U

typedef enum { ALERT_GREEN = 0, ALERT_YELLOW = 1, ALERT_RED = 2 } alert_level_t;
typedef enum { SYSTEM_TARING = 0, SYSTEM_WAITING_AI_SET, SYSTEM_MONITORING } system_state_t;

static system_state_t system_state;
static alert_level_t alert_level;
static uint8_t drip_level = 1U;
static vitals_ai_result_t vitals_ai;
static uint32_t state_start_ms;
static uint32_t last_sample_ms;
static uint32_t last_screen_ms;
static uint32_t last_buzzer_toggle_ms;
static uint32_t last_button_ms;
static uint32_t last_sent_drop_count;
static uint32_t target_interval_ms;
static bool drop_timeout_sent;
static bool buzzer_on;
static bool previous_button;
static int16_t heart_samples[SAMPLE_COUNT];
static int16_t spo2_samples[SAMPLE_COUNT];
static float weight_samples[SAMPLE_COUNT];
static uint8_t heart_count;
static uint8_t spo2_count;
static uint8_t weight_count;
static uint8_t sample_count;
static int16_t heart_rate;
static int16_t spo2;
static float weight_kg;
static float filtered_heart_bpm;
static bool vitals_valid;
static uint32_t last_vitals_good_ms;
static bool fake_hr_enabled;
static bool fake_spo2_enabled;
static uint8_t fake_vitals_level;
static uint8_t large_hr_jump_streak;
static int8_t large_hr_jump_direction;
static char command_buffer[32];
static uint8_t command_length;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

static float absolute_float(float value) { return value < 0.0f ? -value : value; }

static uint16_t clamp_u16(float value)
{
  if (value <= 0.0f) { return 0U; }
  if (value >= 65535.0f) { return UINT16_MAX; }
  return (uint16_t)(value + 0.5f);
}

static int16_t clamp_i16(float value)
{
  if (value <= -32768.0f) { return INT16_MIN; }
  if (value >= 32767.0f) { return INT16_MAX; }
  return (int16_t)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

static void write_zcl_u16(uint16_t attribute_id, uint16_t value,
                          sl_zigbee_af_attribute_type_t type)
{
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, (uint8_t *)&value, type);
}

static void write_zcl_i16(uint16_t attribute_id, int16_t value)
{
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, (uint8_t *)&value, ZCL_INT16S_ATTRIBUTE_TYPE);
}

static void write_zcl_u8(uint16_t attribute_id, uint8_t value)
{
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, &value, ZCL_INT8U_ATTRIBUTE_TYPE);
}

static void publish_zigbee_attributes(int16_t current_hr,
                                      int16_t current_spo2,
                                      bool current_vitals_valid,
                                      float current_weight_kg,
                                      float current_drops_per_minute)
{
  uint16_t target_drops = target_interval_ms > 0U
                          ? clamp_u16(60000.0f / (float)target_interval_ms) : 0U;
  float ratio = target_drops > 0U
                ? current_drops_per_minute * 100.0f / (float)target_drops : 0.0f;
  uint16_t flow_ratio = clamp_u16(ratio);
  int16_t drop_ratio = clamp_i16(ratio);
  uint16_t alarm_bitmap = 0U;
  uint16_t ts_flags = 0U;

  /* Transport flags only: the alert levels themselves are computed elsewhere. */
  if (alert_level == ALERT_YELLOW) { alarm_bitmap |= 0x0001U; }
  if (alert_level == ALERT_RED) { alarm_bitmap |= 0x0002U; }
  if (vitals_ai.level > 1U) { alarm_bitmap |= 0x0004U; }
  if (drip_level > 1U) { alarm_bitmap |= 0x0008U; }
  if (vitals_ai.models_ready) { ts_flags |= 0x0001U; }
  if (vitals_ai.history_ready) { ts_flags |= 0x0002U; }
  if (vitals_ai.ai_anomaly) { ts_flags |= 0x0004U; }
  if (vitals_ai.hard_limit) { ts_flags |= 0x0008U; }

  uint16_t hr_forecast = current_vitals_valid && vitals_ai.history_ready
                         ? clamp_u16(vitals_ai.hr_forecast_16s) : UINT16_MAX;
  uint16_t spo2_forecast = current_vitals_valid && vitals_ai.history_ready
                           ? clamp_u16(vitals_ai.spo2_forecast_16s) : UINT16_MAX;
  int16_t hr_trend = current_vitals_valid && vitals_ai.history_ready
                     ? clamp_i16((vitals_ai.hr_forecast_16s - (float)current_hr) * 3.75f)
                     : 0;
  uint16_t weight_g = current_weight_kg > 0.0f
                      ? clamp_u16(current_weight_kg * 1000.0f) : 0U;
  uint16_t drops_per_min = clamp_u16(current_drops_per_minute);
  uint16_t unavailable = UINT16_MAX;
  int16_t no_drop_trend = 0;

  write_zcl_i16(ATTR_HEART_RATE, current_hr);
  write_zcl_u16(ATTR_SPO2, current_spo2 > 0 ? (uint16_t)current_spo2 : 0U,
                ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_FLOW_RATIO, flow_ratio, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_DROP_RATIO, drop_ratio);
  write_zcl_u16(ATTR_ALARM_BITMAP, alarm_bitmap, ZCL_BITMAP16_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_WEIGHT_G, weight_g, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_DROPS_PER_MIN, drops_per_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_TARGET_FLOW_ML_H, unavailable, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_TARGET_DROPS_PER_MIN, target_drops, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_TS_FLAGS, ts_flags, ZCL_BITMAP16_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_HR_FORECAST_16S, hr_forecast, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_SPO2_FORECAST_16S, spo2_forecast, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_HR_TREND_BPM_PER_MIN, hr_trend);
  write_zcl_u16(ATTR_TS_ANOMALY_SCORE_X100,
                vitals_ai.ai_anomaly ? 100U : 0U, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_DROPS_FORECAST_16S, unavailable, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_DROPS_TREND_DPM_MIN, no_drop_trend);
  write_zcl_u16(ATTR_REMAINING_ML, unavailable, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_REMAINING_MIN, unavailable, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u8(ATTR_MONITORING_ACTIVE,
               system_state == SYSTEM_MONITORING ? 1U : 0U);
}

static uint8_t fuse_alert_levels(uint8_t vitals_level, uint8_t drops_level)
{
  if (vitals_level == 1U && drops_level == 1U) { return 1U; }
  if (vitals_level == 3U && drops_level == 3U) { return 3U; }
  return 2U;
}

static void update_final_alert(void)
{
  uint8_t final_level = fuse_alert_levels(vitals_ai.level, drip_level);
  alert_level = (alert_level_t)(final_level - 1U);
}

static int16_t median_int16(const int16_t *values, uint8_t count)
{
  int16_t sorted[SAMPLE_COUNT];
  if (count == 0U) { return 0; }
  for (uint8_t i = 0U; i < count; i++) { sorted[i] = values[i]; }
  for (uint8_t i = 1U; i < count; i++) {
    int16_t value = sorted[i];
    uint8_t j = i;
    while (j > 0U && sorted[j - 1U] > value) { sorted[j] = sorted[j - 1U]; j--; }
    sorted[j] = value;
  }
  if ((count & 1U) != 0U) { return sorted[count / 2U]; }
  return (int16_t)((sorted[count / 2U - 1U] + sorted[count / 2U]) / 2);
}

static float average_float(const float *values, uint8_t count)
{
  float sum = 0.0f;
  if (count == 0U) { return 0.0f; }
  for (uint8_t i = 0U; i < count; i++) { sum += values[i]; }
  return sum / count;
}

static int16_t filter_heart_rate(const int16_t *values, uint8_t count)
{
  if (count == 0U) { return heart_rate; }
  int16_t median = median_int16(values, count);
  if (filtered_heart_bpm <= 0.0f) { filtered_heart_bpm = (float)median; return median; }
  float difference = (float)median - filtered_heart_bpm;
  if (absolute_float(difference) > 20.0f) {
    int8_t direction = difference > 0.0f ? 1 : -1;
    if (direction == large_hr_jump_direction) {
      if (large_hr_jump_streak < UINT8_MAX) { large_hr_jump_streak++; }
    } else { large_hr_jump_direction = direction; large_hr_jump_streak = 1U; }
    if (large_hr_jump_streak < 2U) { return (int16_t)(filtered_heart_bpm + 0.5f); }
  } else { large_hr_jump_streak = 0U; large_hr_jump_direction = 0; }
  float step = ((float)median - filtered_heart_bpm) * 0.35f;
  if (step > 8.0f) { step = 8.0f; }
  if (step < -8.0f) { step = -8.0f; }
  filtered_heart_bpm += step;
  return (int16_t)(filtered_heart_bpm + 0.5f);
}

static void all_alerts_off(void)
{
  GPIO_PinOutClear(GREEN_LED_PORT, GREEN_LED_PIN);
  GPIO_PinOutClear(YELLOW_LED_PORT, YELLOW_LED_PIN);
  GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN);
  GPIO_PinOutSet(BUZZER_PORT, BUZZER_PIN);
  buzzer_on = false;
}

static void start_tare(uint32_t now)
{
  system_state = SYSTEM_TARING;
  state_start_ms = now;
  last_screen_ms = 0U;
  target_interval_ms = 0U;
  alert_level = ALERT_GREEN;
  drip_level = 1U;
  memset(&vitals_ai, 0, sizeof(vitals_ai));
  vitals_ai.level = 1U;
  vitals_ai_reset();
  last_sent_drop_count = 0U;
  drop_timeout_sent = false;
  filtered_heart_bpm = 0.0f;
  last_vitals_good_ms = 0U;
  fake_hr_enabled = false;
  fake_spo2_enabled = false;
  fake_vitals_level = 0U;
  drop_sensor_set_min_gap_ms(200U);
  drop_sensor_reset_statistics();
  hx711_sensor_tare();
  all_alerts_off();
  write_zcl_u8(ATTR_MONITORING_ACTIVE, 0U);
  oled_display_message("STARTING", "DO NOT HANG BAG", "TARE IN 10 SEC", NULL);
}

static void process_command(const char *command)
{
  if (strncmp(command, "SET,", 4U) == 0) {
    unsigned long value = strtoul(command + 4, NULL, 10);
    if (value >= 250UL && value <= 60000UL) {
      target_interval_ms = (uint32_t)value;
      drop_sensor_set_min_gap_ms(target_interval_ms / 3U);
      drop_sensor_reset_statistics();
      last_sent_drop_count = 0U;
      drop_timeout_sent = false;
      alert_level = ALERT_GREEN;
      drip_level = 1U;
      system_state = SYSTEM_MONITORING;
      printf("AI_SET_OK,%lu\r\n", value);
      oled_display_message("RATE CONFIRMED", "MONITORING START", NULL, NULL);
    } else { printf("AI_SET_ERROR\r\n"); }
  } else if (strncmp(command, "LEVEL,", 6U) == 0) {
    int level = atoi(command + 6);
    if (system_state == SYSTEM_MONITORING && level >= 1 && level <= 3) {
      drip_level = (uint8_t)level;
      update_final_alert();
      printf("AI_LEVEL_OK,%d\r\n", level);
    }
  } else if (strncmp(command, "FAKE_HR,", 8U) == 0) {
    bool was_enabled = fake_hr_enabled;
    fake_hr_enabled = atoi(command + 8) != 0;
    if (was_enabled && !fake_hr_enabled) {
      vitals_ai_clear_history(&vitals_ai);
      update_final_alert();
      printf("VITAL_TEST_HISTORY_CLEARED\r\n");
    }
    printf("FAKE_HR_OK,%u\r\n", fake_hr_enabled ? 1U : 0U);
  } else if (strncmp(command, "FAKE_SPO2,", 10U) == 0) {
    bool was_enabled = fake_spo2_enabled;
    fake_spo2_enabled = atoi(command + 10) != 0;
    if (was_enabled && !fake_spo2_enabled) {
      vitals_ai_clear_history(&vitals_ai);
      update_final_alert();
      printf("VITAL_TEST_HISTORY_CLEARED\r\n");
    }
    printf("FAKE_SPO2_OK,%u\r\n", fake_spo2_enabled ? 1U : 0U);
  } else if (strncmp(command, "FAKE_VITAL,", 11U) == 0) {
    int level = atoi(command + 11);
    if (level >= 0 && level <= 3 && level != 1) {
      bool turning_on = fake_vitals_level == 0U && level != 0;
      bool turning_off = fake_vitals_level != 0U && level == 0;
      if (turning_on) { vitals_ai_begin_test(); }
      fake_vitals_level = (uint8_t)level;
      if (turning_off) {
        vitals_ai_end_test(&vitals_ai);
        update_final_alert();
        printf("VITAL_TEST_HISTORY_RESTORED\r\n");
      }
      printf("FAKE_VITAL_OK,%d\r\n", level);
    }
  }
}

static void poll_serial_commands(void)
{
  uint8_t byte;
  size_t bytes_read = 0U;
  while (sl_iostream_read(sl_iostream_vcom_handle, &byte, 1U, &bytes_read) == SL_STATUS_OK
         && bytes_read == 1U) {
    if (byte == '\r' || byte == '\n') {
      if (command_length > 0U) {
        command_buffer[command_length] = '\0';
        process_command(command_buffer);
        command_length = 0U;
      }
    } else if (command_length + 1U < sizeof(command_buffer)) {
      command_buffer[command_length++] = (char)byte;
    } else { command_length = 0U; }
    bytes_read = 0U;
  }
}

static void update_startup(uint32_t now)
{
  if (system_state == SYSTEM_WAITING_AI_SET) {
    if ((now - last_screen_ms) >= 1000U) {
      printf("AI_READY\r\n");
      last_screen_ms = now;
    }
    return;
  }
  if (system_state != SYSTEM_TARING) { return; }
  uint32_t elapsed = now - state_start_ms;
  if ((now - last_screen_ms) >= 1000U) {
    char text[20];
    uint32_t remaining = elapsed < TARE_TIME_MS ? (TARE_TIME_MS - elapsed + 999U) / 1000U : 0U;
    (void)snprintf(text, sizeof(text), "REMAIN:%lu SEC", (unsigned long)remaining);
    oled_display_message("DO NOT HANG BAG", "TARING SCALE", text, "PLEASE WAIT");
    last_screen_ms = now;
  }
  if (elapsed >= TARE_TIME_MS && hx711_sensor_tared()) {
    system_state = SYSTEM_WAITING_AI_SET;
    last_screen_ms = now;
    printf("AI_READY\r\n");
    oled_display_message("SYSTEM READY", "HANG IV BAG", "OPEN PC APP", "SELECT DROP RATE");
  }
}

static void update_alert_outputs(uint32_t now)
{
  static alert_level_t previous_level = ALERT_GREEN;
  if (system_state != SYSTEM_MONITORING) { all_alerts_off(); return; }
  if (alert_level == ALERT_GREEN) {
    GPIO_PinOutSet(GREEN_LED_PORT, GREEN_LED_PIN);
    GPIO_PinOutClear(YELLOW_LED_PORT, YELLOW_LED_PIN);
    GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN);
    GPIO_PinOutSet(BUZZER_PORT, BUZZER_PIN);
    buzzer_on = false;
    previous_level = ALERT_GREEN;
    return;
  }
  GPIO_PinOutClear(GREEN_LED_PORT, GREEN_LED_PIN);
  if (alert_level != previous_level) {
    previous_level = alert_level;
    last_buzzer_toggle_ms = now;
    buzzer_on = true;
    GPIO_PinOutClear(BUZZER_PORT, BUZZER_PIN);
    if (alert_level == ALERT_RED) {
      GPIO_PinOutClear(YELLOW_LED_PORT, YELLOW_LED_PIN);
      GPIO_PinOutSet(RED_LED_PORT, RED_LED_PIN);
    } else {
      GPIO_PinOutSet(YELLOW_LED_PORT, YELLOW_LED_PIN);
      GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN);
    }
    drop_sensor_blank_until(now + 30U);
    return;
  }
  if (alert_level == ALERT_YELLOW) {
    GPIO_PinOutSet(YELLOW_LED_PORT, YELLOW_LED_PIN);
    GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN);
  } else {
    GPIO_PinOutClear(YELLOW_LED_PORT, YELLOW_LED_PIN);
    if (buzzer_on) { GPIO_PinOutSet(RED_LED_PORT, RED_LED_PIN); }
    else { GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN); }
  }
  uint32_t phase;
  if (alert_level == ALERT_YELLOW) { phase = buzzer_on ? 500U : 3000U; }
  else { phase = 250U; }
  if ((now - last_buzzer_toggle_ms) >= phase) {
    last_buzzer_toggle_ms = now;
    buzzer_on = !buzzer_on;
    if (buzzer_on) { GPIO_PinOutClear(BUZZER_PORT, BUZZER_PIN); }
    else { GPIO_PinOutSet(BUZZER_PORT, BUZZER_PIN); }
    if (alert_level == ALERT_RED) {
      if (buzzer_on) { GPIO_PinOutSet(RED_LED_PORT, RED_LED_PIN); }
      else { GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN); }
    }
    drop_sensor_blank_until(now + 30U);
  }
}

static void send_new_drop_to_ai(void)
{
  uint32_t count = drop_sensor_count();
  if (count <= last_sent_drop_count || count < 2U) { return; }
  uint32_t actual_ms = (uint32_t)(drop_sensor_last_interval_seconds() * 1000.0f + 0.5f);

  if (system_state == SYSTEM_WAITING_AI_SET) {
    float rate = actual_ms > 0U ? 60000.0f / (float)actual_ms : 0.0f;
    printf("SETUP_DROP,%lu,%.1f\r\n", (unsigned long)actual_ms, (double)rate);
    last_sent_drop_count = count;
    return;
  }

  if (system_state != SYSTEM_MONITORING || target_interval_ms == 0U) { return; }
  printf("%lu,%lu,%lu,%lu\r\n", (unsigned long)now_ms(), (unsigned long)count,
         (unsigned long)target_interval_ms, (unsigned long)actual_ms);
  last_sent_drop_count = count;
}

static void monitor_drop_timeout(uint32_t now)
{
  if (system_state != SYSTEM_MONITORING || target_interval_ms == 0U) { return; }
  uint32_t last_drop = drop_sensor_last_drop_ms();
  if (last_drop == 0U) { return; }
  int32_t signed_elapsed = (int32_t)(now - last_drop);
  if (signed_elapsed < 0) {
    /* An IRQ can record a drop after this loop captured `now`. */
    drop_timeout_sent = false;
    return;
  }
  uint32_t elapsed = (uint32_t)signed_elapsed;
  uint32_t timeout = 2U * target_interval_ms + 200U;
  if (elapsed >= timeout) {
    if (!drop_timeout_sent) {
      drop_timeout_sent = true;
      printf("DROP_TIMEOUT,%lu\r\n", (unsigned long)elapsed);
    }
  } else {
    drop_timeout_sent = false;
  }
}

static void publish_display(void)
{
  uint32_t now = now_ms();
  bool new_vitals = heart_count > 0U && spo2_count > 0U;
  if (new_vitals) {
    heart_rate = filter_heart_rate(heart_samples, heart_count);
    spo2 = median_int16(spo2_samples, spo2_count);
    last_vitals_good_ms = now;
  }
  vitals_valid = last_vitals_good_ms != 0U
                  && (uint32_t)(now - last_vitals_good_ms) <= VITALS_HOLD_MS;
  if (weight_count > 0U) { weight_kg = average_float(weight_samples, weight_count); }
  if (system_state == SYSTEM_MONITORING) {
    int16_t ai_heart_rate = heart_rate;
    int16_t ai_spo2 = spo2;
    bool fake_ready = vitals_ai.baseline_samples >= 60U;
    if (fake_ready && fake_hr_enabled) {
      /* Use a downward deviation so it is >30% from baseline while staying
         inside the hard red limits (45..150 BPM): this tests level 2. */
      float fake_hr = vitals_ai.hr_baseline * 0.65f;
      if (fake_hr < 46.0f) { fake_hr = 46.0f; }
      ai_heart_rate = (int16_t)(fake_hr + 0.5f);
    }
    if (fake_ready && fake_spo2_enabled) {
      ai_spo2 = (int16_t)(vitals_ai.spo2_baseline * 0.65f + 0.5f);
    }
    if (fake_ready && fake_vitals_level == 2U) {
      ai_heart_rate = (int16_t)(vitals_ai.hr_baseline * 0.83f + 0.5f);
      ai_spo2 = (int16_t)(vitals_ai.spo2_baseline + 0.5f);
    } else if (fake_ready && fake_vitals_level == 3U) {
      ai_heart_rate = (int16_t)(vitals_ai.hr_baseline * 0.75f + 0.5f);
      ai_spo2 = (int16_t)(vitals_ai.spo2_baseline + 0.5f);
    }
    /* Feed each real measurement to AI once; held display values are not
       repeated as if they were new sensor samples. */
    if (new_vitals) {
      vitals_ai_step(ai_heart_rate, ai_spo2, true, &vitals_ai);
    }
    update_final_alert();
    float interval = drop_sensor_last_interval_seconds();
    float rate = interval > 0.0f ? 60.0f / interval : 0.0f;
    float relative_hr = vitals_ai.hr_baseline > 0.0f
                        ? absolute_float((float)ai_heart_rate - vitals_ai.hr_baseline)
                          / vitals_ai.hr_baseline : 0.0f;
    float relative_spo2 = vitals_ai.spo2_baseline > 0.0f
                          ? absolute_float((float)ai_spo2 - vitals_ai.spo2_baseline)
                            / vitals_ai.spo2_baseline : 0.0f;
    bool hr_cause = ai_heart_rate < 45 || ai_heart_rate > 150 || relative_hr >= 0.15f;
    bool spo2_cause = ai_spo2 < 90 || relative_spo2 >= 0.15f;
    /* Keep the learned/AI values intact, but never expose a held value as a
     * current measurement after the sensor signal has expired. */
    int16_t output_heart_rate = vitals_valid ? ai_heart_rate : 0;
    int16_t output_spo2 = vitals_valid ? ai_spo2 : 0;
    oled_display_monitor(ai_heart_rate, ai_spo2, vitals_valid, weight_kg,
                         hx711_sensor_connected() && hx711_sensor_tared(),
                         interval, (float)target_interval_ms / 1000.0f,
                         rate, (uint8_t)alert_level + 1U, vitals_ai.level,
                         drip_level, hr_cause, spo2_cause, vitals_ai.baseline_samples,
                         vitals_ai.history_samples);
    publish_zigbee_attributes(output_heart_rate, output_spo2, vitals_valid,
                              weight_kg, rate);
    printf("TELEMETRY,%d,%d,%.3f,%.3f,%.3f,%.1f,%u,%u,%u,%.1f,%.1f,%u,%u,%u,%u,%u\r\n",
           output_heart_rate, output_spo2, (double)weight_kg, (double)interval,
           (double)target_interval_ms / 1000.0,
           (double)rate, (unsigned int)alert_level + 1U,
           (unsigned int)vitals_ai.level, (unsigned int)drip_level,
           (double)vitals_ai.hr_baseline, (double)vitals_ai.spo2_baseline,
           (unsigned int)vitals_ai.baseline_samples,
           (unsigned int)vitals_ai.history_samples,
           fake_hr_enabled ? 1U : 0U, fake_spo2_enabled ? 1U : 0U,
           (unsigned int)fake_vitals_level);
  }
  heart_count = spo2_count = weight_count = sample_count = 0U;
}

static void sample_sensors(void)
{
  int16_t hr;
  int16_t oxygen;
  if (blood_oxygen_sample(&hr, &oxygen)) {
    printf("VITAL_RAW,%lu,%d,%d\r\n", (unsigned long)now_ms(), hr, oxygen);
    if (heart_count < SAMPLE_COUNT) { heart_samples[heart_count++] = hr; }
    if (spo2_count < SAMPLE_COUNT) { spo2_samples[spo2_count++] = oxygen; }
  }
  if (hx711_sensor_connected() && hx711_sensor_tared() && weight_count < SAMPLE_COUNT) {
    float value = hx711_sensor_weight_kg();
    if (value > -10.0f && value < 10.0f) { weight_samples[weight_count++] = value; }
  }
  if (++sample_count >= SAMPLE_COUNT) { publish_display(); }
}

void app_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(GREEN_LED_PORT, GREEN_LED_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(YELLOW_LED_PORT, YELLOW_LED_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(RED_LED_PORT, RED_LED_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(BUZZER_PORT, BUZZER_PIN, gpioModePushPull, 1);
  GPIO_PinModeSet(TARE_PORT, TARE_PIN, gpioModeInputPullFilter, 1);
  software_i2c_init();
  bool oled_ok = oled_display_init();
  bool max_ok = blood_oxygen_init();
  bool vitals_models_ok = vitals_ai_init();
  drop_sensor_init();
  hx711_sensor_init();
  previous_button = GPIO_PinInGet(TARE_PORT, TARE_PIN) == 0;
  uint32_t now = now_ms();
  last_sample_ms = now;
  start_tare(now);
  printf("SMART_IV_G26,OLED=%s,MAX30102=%s,VITALS_AI=%s\r\n",
         oled_ok ? "OK" : "NO", max_ok ? "OK" : "NO",
         vitals_models_ok ? "OK" : "NO");
}

void app_process_action(void)
{
  uint32_t now = now_ms();
  poll_serial_commands();
  drop_sensor_poll();
  hx711_sensor_poll();
  bool pressed = GPIO_PinInGet(TARE_PORT, TARE_PIN) == 0;
  if (pressed && !previous_button && (now - last_button_ms) >= 200U) {
    last_button_ms = now;
    start_tare(now);
  }
  previous_button = pressed;
  update_startup(now);
  send_new_drop_to_ai();
  monitor_drop_timeout(now);
  update_alert_outputs(now);
  if ((now - last_sample_ms) >= SAMPLE_INTERVAL_MS) {
    last_sample_ms = now;
    sample_sensors();
  }
}

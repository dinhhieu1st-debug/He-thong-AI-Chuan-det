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
#include "sl_cli.h"
#include "sl_cli_handles.h"
#include "sl_sleeptimer.h"
#include "af.h"
#include "network-steering.h"
#include "app/framework/plugin/reporting/reporting.h"
#include "nvm3_default.h"
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
#define VITALS_FILTER_WINDOW      12U
#define VITALS_FILTER_MIN_SAMPLES 8U
#define TARE_TIME_MS       10000U
#define VITALS_HOLD_MS     3000U
#define DROP_TRAINING_REQUIRED   20U
#define VITALS_TRAINING_REQUIRED 64U

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
#define ATTR_TARE_COMMAND          0x0009U
#define ATTR_HR_RECALIBRATE        0x000AU
#define ATTR_HR_BASELINE_REMAINING 0x000BU
#define ATTR_HR_BASELINE_BPM       0x000CU
#define ATTR_TARE_EVENT_COUNT      0x000DU
#define ATTR_HR_BASELINE_EVENTS    0x000EU
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
#define ATTR_DROP_TRAINING_SAMPLES 0x0019U
#define ATTR_VITALS_TRAINING_SAMPLES 0x001AU
#define ATTR_ALERTS_ARMED          0x001BU
#define ATTR_DROP_INTERVAL_MS      0x001CU
#define ATTR_DROP_EVENT_COUNT      0x001DU
#define ATTR_SERVER_DROP_LEVEL     0x001EU
#define ATTR_VITALS_TEST_MODE      0x001FU
#define ATTR_AI_INPUT_HEART_RATE   0x0020U
#define ATTR_AI_INPUT_SPO2         0x0021U
#define ATTR_VITALS_LEVEL          0x0022U

typedef enum { ALERT_GREEN = 0, ALERT_YELLOW = 1, ALERT_RED = 2 } alert_level_t;
typedef enum { SYSTEM_TARING = 0, SYSTEM_WAITING_AI_SET, SYSTEM_MONITORING } system_state_t;

static system_state_t system_state;
static alert_level_t alert_level;
static uint8_t drip_level = 1U;
/* physical_drip_level is the firmware's own verdict from the raw drop
 * timing (see evaluate_physical_drip_level()); ai_drip_level is whatever
 * the desktop MLP/LSTM last reported via LEVEL,x. drip_level is always
 * their max: the desktop AI can raise the alarm the firmware physics
 * already saw, but it can never quietly lower one. */
static uint8_t physical_drip_level = 1U;
static uint8_t ai_drip_level = 1U;
/* Consecutive normal-range drop intervals required before physical_drip_level
 * is allowed to fall back to 1 (NORMAL). Escalation is immediate; recovery
 * is debounced so a single clean interval right after an occlusion clears
 * doesn't instantly wipe the alarm. */
#define DRIP_RECOVERY_STREAK_REQUIRED 3U
static uint8_t drip_recovery_streak;
/* Timestamp the current SYSTEM_MONITORING session started, so a total flow
 * stoppage from the very first drop (never any drop at all) can still be
 * timed out - drop_sensor_last_drop_ms() alone is 0 in that case and would
 * never trip. */
static uint32_t monitoring_start_ms;
static vitals_ai_result_t vitals_ai;
static uint32_t state_start_ms;
static uint32_t last_sample_ms;
static uint32_t last_screen_ms;
static uint32_t last_buzzer_toggle_ms;
static uint32_t last_button_ms;
static uint32_t last_sent_drop_count;
static uint32_t target_interval_ms = 3000U;
static uint16_t target_drops_per_min = 20U;
static uint16_t target_flow_ml_h = 100U;
static bool drop_timeout_sent;
static bool buzzer_on;
static bool previous_button;
static bool monitoring_requested;
static bool alerts_armed;
static uint8_t drop_training_samples;
static bool zb_join_started;
static bool zcl_internal_write;
static bool tare_just_completed;
static bool hr_baseline_just_completed;
static uint8_t tare_event_count;
static uint8_t hr_baseline_event_count;
static uint8_t previous_baseline_samples;
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
static float filtered_spo2_percent;
static bool vitals_valid;
static uint32_t last_vitals_good_ms;
static bool fake_hr_enabled;
static bool fake_spo2_enabled;
static uint8_t fake_vitals_level;
static bool runtime_tare_in_progress;
static uint32_t runtime_tare_start_ms;
static uint8_t large_hr_jump_streak;
static int8_t large_hr_jump_direction;
static uint8_t large_spo2_jump_streak;
static int8_t large_spo2_jump_direction;
static int16_t heart_filter_history[VITALS_FILTER_WINDOW];
static int16_t spo2_filter_history[VITALS_FILTER_WINDOW];
static uint8_t heart_filter_count;
static uint8_t spo2_filter_count;

#define DROP_FORECAST_WINDOW 20U
static float drop_rate_history[DROP_FORECAST_WINDOW];
static uint32_t drop_time_history[DROP_FORECAST_WINDOW];
static uint8_t drop_forecast_count;
static uint8_t drop_forecast_head;
static float drops_forecast_16s;
static float drops_trend_dpm_per_min;
static bool drops_forecast_trusted;

#define ZB_REPORT_MAX_INTERVAL_S 60U

typedef struct {
  uint16_t attribute_id;
  uint16_t min_interval_s;
  uint16_t reportable_change;
} zb_report_cfg_t;

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
  zcl_internal_write = true;
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, (uint8_t *)&value, type);
  zcl_internal_write = false;
}

static void write_zcl_i16(uint16_t attribute_id, int16_t value)
{
  zcl_internal_write = true;
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, (uint8_t *)&value, ZCL_INT16S_ATTRIBUTE_TYPE);
  zcl_internal_write = false;
}

static void write_zcl_u8(uint16_t attribute_id, uint8_t value)
{
  zcl_internal_write = true;
  (void)sl_zigbee_af_write_manufacturer_specific_server_attribute(
    SMART_IV_ENDPOINT, SMART_IV_CLUSTER_ID, attribute_id,
    SMART_IV_MANUFACTURER_CODE, &value, ZCL_INT8U_ATTRIBUTE_TYPE);
  zcl_internal_write = false;
}

static void configure_zigbee_reporting(void)
{
  static const zb_report_cfg_t configs[] = {
    { ATTR_ALARM_BITMAP,          1U,  1U },
    { ATTR_HEART_RATE,            2U,  1U },
    { ATTR_SPO2,                  2U,  1U },
    { ATTR_FLOW_RATIO,            5U,  5U },
    { ATTR_DROP_RATIO,            5U,  5U },
    { ATTR_WEIGHT_G,             10U,  5U },
    { ATTR_DROPS_PER_MIN,         5U,  1U },
    { ATTR_TARGET_FLOW_ML_H,      1U,  1U },
    { ATTR_TARGET_DROPS_PER_MIN,  1U,  1U },
    { ATTR_HR_BASELINE_REMAINING, 5U,  1U },
    { ATTR_HR_BASELINE_BPM,       1U,  1U },
    { ATTR_TARE_EVENT_COUNT,      1U,  1U },
    { ATTR_HR_BASELINE_EVENTS,    1U,  1U },
    { ATTR_MONITORING_ACTIVE,     1U,  1U },
    { ATTR_DROP_TRAINING_SAMPLES, 1U,  1U },
    { ATTR_VITALS_TRAINING_SAMPLES, 1U, 1U },
    { ATTR_ALERTS_ARMED,          1U,  1U },
    { ATTR_VITALS_TEST_MODE,      1U,  1U },
    { ATTR_AI_INPUT_HEART_RATE,   1U,  1U },
    { ATTR_AI_INPUT_SPO2,         1U,  1U },
    { ATTR_VITALS_LEVEL,          1U,  1U },
    { ATTR_TS_FLAGS,              1U,  1U },
    { ATTR_HR_FORECAST_16S,       5U,  2U },
    { ATTR_SPO2_FORECAST_16S,     5U,  1U },
    { ATTR_HR_TREND_BPM_PER_MIN,  5U,  5U },
    { ATTR_TS_ANOMALY_SCORE_X100, 5U, 50U },
    { ATTR_DROPS_FORECAST_16S,    5U,  1U },
    { ATTR_DROPS_TREND_DPM_MIN,   5U,  1U },
    { ATTR_REMAINING_ML,         10U,  5U },
    { ATTR_REMAINING_MIN,        10U,  1U },
    { ATTR_DROP_INTERVAL_MS,      1U,  1U },
    { ATTR_DROP_EVENT_COUNT,      1U,  1U },
    { ATTR_SERVER_DROP_LEVEL,     1U,  1U },
  };

  for (uint8_t i = 0U; i < (uint8_t)(sizeof(configs) / sizeof(configs[0])); i++) {
    sl_zigbee_af_plugin_reporting_entry_t entry = { 0 };
    entry.direction = SL_ZIGBEE_ZCL_REPORTING_DIRECTION_REPORTED;
    entry.endpoint = SMART_IV_ENDPOINT;
    entry.clusterId = SMART_IV_CLUSTER_ID;
    entry.attributeId = configs[i].attribute_id;
    entry.mask = CLUSTER_MASK_SERVER;
    entry.manufacturerCode = SMART_IV_MANUFACTURER_CODE;
    entry.data.reported.minInterval = configs[i].min_interval_s;
    entry.data.reported.maxInterval = ZB_REPORT_MAX_INTERVAL_S;
    entry.data.reported.reportableChange = configs[i].reportable_change;
    sl_status_t status = sl_zigbee_af_reporting_configure_reported_attribute(&entry);
    if (status != SL_STATUS_OK) {
      printf("[ZB] Reporting config failed attr=0x%04X status=0x%02X.\r\n",
             configs[i].attribute_id, (unsigned)status);
    }
  }
}

static void apply_target_drops_per_min(uint16_t drops_per_min)
{
  if (drops_per_min < 1U || drops_per_min > 240U) { return; }
  target_drops_per_min = drops_per_min;
  target_interval_ms = 60000U / drops_per_min;
  drop_sensor_set_min_gap_ms(target_interval_ms / 3U);
  drop_sensor_reset_statistics();
  last_sent_drop_count = 0U;
  drop_timeout_sent = false;
}

/* Pure per-interval verdict from the agreed physical safety bands. Keep this
 * independent from the smoothed display rate: a median is good for a readable
 * graph, but must never hide a genuinely missing or late drop. */
static uint8_t evaluate_physical_drip_level(uint32_t target_ms, uint32_t actual_ms)
{
  if (target_ms == 0U || actual_ms == 0U) { return 1U; }

  uint32_t difference = actual_ms > target_ms
                        ? actual_ms - target_ms : target_ms - actual_ms;
  if (difference > 800U) { return 3U; }
  if (difference > 200U) { return 2U; }
  return 1U;
}

static void reset_drop_forecast(void)
{
  memset(drop_rate_history, 0, sizeof(drop_rate_history));
  memset(drop_time_history, 0, sizeof(drop_time_history));
  drop_forecast_count = 0U;
  drop_forecast_head = 0U;
  drops_forecast_16s = 0.0f;
  drops_trend_dpm_per_min = 0.0f;
  drops_forecast_trusted = false;
}

/* Lightweight on-chip time-series forecast over the same 20 physical drops
 * used by drip training. Least-squares is intentionally bounded and only
 * trusted after the complete window; isolated invalid pulses are already
 * rejected by drop_sensor before reaching this function. */
static void update_drop_forecast(uint32_t sample_ms, uint32_t interval_ms)
{
  if (interval_ms == 0U) { return; }
  float rate = 60000.0f / (float)interval_ms;
  drop_rate_history[drop_forecast_head] = rate;
  drop_time_history[drop_forecast_head] = sample_ms;
  drop_forecast_head = (uint8_t)((drop_forecast_head + 1U) % DROP_FORECAST_WINDOW);
  if (drop_forecast_count < DROP_FORECAST_WINDOW) { drop_forecast_count++; }
  if (drop_forecast_count < DROP_FORECAST_WINDOW) {
    drops_forecast_trusted = false;
    return;
  }

  uint8_t oldest = drop_forecast_head;
  uint32_t origin = drop_time_history[oldest];
  float sum_x = 0.0f, sum_y = 0.0f, sum_xx = 0.0f, sum_xy = 0.0f;
  for (uint8_t i = 0U; i < DROP_FORECAST_WINDOW; i++) {
    uint8_t index = (uint8_t)((oldest + i) % DROP_FORECAST_WINDOW);
    float x = (float)(drop_time_history[index] - origin) / 1000.0f;
    float y = drop_rate_history[index];
    sum_x += x; sum_y += y; sum_xx += x * x; sum_xy += x * y;
  }
  const float n = (float)DROP_FORECAST_WINDOW;
  float denominator = n * sum_xx - sum_x * sum_x;
  float slope_per_second = absolute_float(denominator) > 0.001f
                           ? (n * sum_xy - sum_x * sum_y) / denominator : 0.0f;
  /* Reject implausible regression slopes caused by timestamp wrap/noise. */
  if (slope_per_second > 2.0f) { slope_per_second = 2.0f; }
  if (slope_per_second < -2.0f) { slope_per_second = -2.0f; }
  float latest = drop_rate_history[(drop_forecast_head + DROP_FORECAST_WINDOW - 1U)
                                   % DROP_FORECAST_WINDOW];
  drops_trend_dpm_per_min = slope_per_second * 60.0f;
  drops_forecast_16s = latest + slope_per_second * 16.0f;
  if (drops_forecast_16s < 0.0f) { drops_forecast_16s = 0.0f; }
  if (drops_forecast_16s > 240.0f) { drops_forecast_16s = 240.0f; }
  drops_forecast_trusted = physical_drip_level == 1U;
}

/* Wire value is state+1 because zero is reserved for "not available". */
static uint8_t encoded_line_state(float weight_kg, float drops_per_minute)
{
  if (!hx711_sensor_connected() || !hx711_sensor_tared()) { return 0U; }
  float grams = weight_kg * 1000.0f;
  if (grams <= 20.0f) { return 6U; } /* empty */
  if (!drop_sensor_calibrated()) { return 5U; } /* drop sensor fault */
  if (drop_timeout_sent) { return 3U; } /* occlusion / no new drop */
  if (target_drops_per_min > 0U
      && drops_per_minute > (float)target_drops_per_min * 1.5f) { return 4U; }
  if (grams <= 100.0f) { return 2U; } /* running low */
  return 1U; /* OK */
}

/* Feeds one freshly-measured drop interval into the firmware's own drip
 * verdict. Escalation (level going up) applies immediately - a single
 * dangerously fast or slow interval is real signal, not noise, and must not
 * wait. Recovery down to NORMAL requires DRIP_RECOVERY_STREAK_REQUIRED
 * consecutive normal intervals, so one clean drop right after an occlusion
 * clears doesn't instantly erase the alarm a nurse still needs to see. */
static void update_physical_drip_level(uint32_t actual_ms)
{
  uint8_t sample_level = evaluate_physical_drip_level(target_interval_ms, actual_ms);

  if (sample_level == 1U) {
    if (physical_drip_level == 1U) {
      drip_recovery_streak = 0U;
      return;
    }
    drip_recovery_streak++;
    if (drip_recovery_streak < DRIP_RECOVERY_STREAK_REQUIRED) {
      printf("[DROP-AI] target=%lu actual=%lu ratio=%.2f physical=%u(recovering %u/%u) ai=%u\r\n",
             (unsigned long)target_interval_ms, (unsigned long)actual_ms,
             (double)((float)actual_ms / (float)target_interval_ms),
             (unsigned)physical_drip_level, (unsigned)drip_recovery_streak,
             (unsigned)DRIP_RECOVERY_STREAK_REQUIRED, (unsigned)ai_drip_level);
      return;
    }
    drip_recovery_streak = 0U;
  } else {
    drip_recovery_streak = 0U;
  }

  physical_drip_level = sample_level;
  drip_level = physical_drip_level > ai_drip_level ? physical_drip_level : ai_drip_level;
  printf("[DROP-AI] target=%lu actual=%lu ratio=%.2f physical=%u ai=%u final_drop=%u\r\n",
         (unsigned long)target_interval_ms, (unsigned long)actual_ms,
         (double)((float)actual_ms / (float)target_interval_ms),
         (unsigned)physical_drip_level, (unsigned)ai_drip_level, (unsigned)drip_level);
}

/* Application-owned NVM3 object recording whether monitoring was armed and
 * the doctor's configured targets. Key 1 sits in NVM3's user domain
 * (0x00000-0x0FFFF), clear of the Zigbee stack's own domain (0x10000+, see
 * token-stack.h), so it can never collide with network/security state.
 *
 * monitoring_requested is otherwise a plain static bool that resets to
 * false on every reboot (crash, OTA reflash, power cycle). Without this,
 * a board that resets while a real patient is connected drops silently
 * into SYSTEM_WAITING_AI_SET - no telemetry, no alarms, nothing on the
 * dashboard - until a human notices and manually re-sends SET/starts
 * monitoring from the web. Persisting the flag lets boot resume exactly
 * where it left off; a board that was never started (or was deliberately
 * paused) still boots to WAITING_AI_SET, since monitoring_requested was
 * false when it was last saved. */
#define NVM3KEY_APP_MONITORING_STATE 0x0001U

typedef struct {
  uint8_t monitoring_requested;
  uint16_t target_drops_per_min;
  uint16_t target_flow_ml_h;
} persisted_monitoring_state_t;

static void save_monitoring_state(void)
{
  persisted_monitoring_state_t state = {
    .monitoring_requested = monitoring_requested ? 1U : 0U,
    .target_drops_per_min = target_drops_per_min,
    .target_flow_ml_h = target_flow_ml_h,
  };
  (void)nvm3_writeData(nvm3_defaultHandle, NVM3KEY_APP_MONITORING_STATE,
                       &state, sizeof(state));
}

/* Called once from app_init(), before start_initial_tare() reads
 * monitoring_requested. Silently leaves the defaults (monitoring off,
 * compiled-in target) in place on the very first boot ever, when the key
 * does not exist yet. */
static void load_monitoring_state(void)
{
  persisted_monitoring_state_t state;
  if (nvm3_readData(nvm3_defaultHandle, NVM3KEY_APP_MONITORING_STATE,
                    &state, sizeof(state)) != ECODE_NVM3_OK) {
    return;
  }
  /* A stored target is useful after reboot, but a previous running session
   * must never silently start a new patient's training.  A fresh target
   * confirmation from HIS Web is the start gate for every boot. */
  monitoring_requested = false;
  if (state.target_drops_per_min >= 1U && state.target_drops_per_min <= 240U) {
    apply_target_drops_per_min(state.target_drops_per_min);
  }
  if (state.target_flow_ml_h > 0U) {
    target_flow_ml_h = state.target_flow_ml_h;
  }
}

static void reset_hr_baseline(void)
{
  vitals_ai_begin_baseline_recalibration();
  if (vitals_ai_baseline_recalibrating()) {
    printf("[HR] Collecting 60 candidate baseline samples; old baseline remains active.\r\n");
  } else {
    printf("[HR] Recalibration ignored until the first 60-sample baseline is ready.\r\n");
  }
}

static void publish_zigbee_attributes(int16_t current_hr,
                                      int16_t current_spo2,
                                      int16_t ai_input_hr,
                                      int16_t ai_input_spo2,
                                      bool current_vitals_valid,
                                      float current_weight_kg,
                                      float current_drops_per_minute)
{
  float ratio = target_drops_per_min > 0U
                ? current_drops_per_minute * 100.0f / (float)target_drops_per_min : 0.0f;
  uint16_t flow_ratio = clamp_u16(ratio);
  int16_t drop_ratio = clamp_i16(ratio);
  uint16_t alarm_bitmap = 0U;
  uint16_t ts_flags = 0U;

  /* AlarmBitmap layout is shared with zigbee2mqtt_smart_iv_converter.js. */
  if (alerts_armed && !current_vitals_valid) { alarm_bitmap |= 0x0001U; }
  if (alerts_armed && current_vitals_valid && (ai_input_spo2 < 90
      || (vitals_ai.baseline_samples >= 60U && vitals_ai.spo2_baseline > 0.0f
          && absolute_float((float)ai_input_spo2 - vitals_ai.spo2_baseline)
             / vitals_ai.spo2_baseline >= 0.15f))) { alarm_bitmap |= 0x0002U; }
  if (alerts_armed && current_vitals_valid && (ai_input_hr < 45 || ai_input_hr > 150
      || (vitals_ai.baseline_samples >= 60U && vitals_ai.hr_baseline > 0.0f
          && absolute_float((float)ai_input_hr - vitals_ai.hr_baseline)
             / vitals_ai.hr_baseline >= 0.15f))) { alarm_bitmap |= 0x0004U; }
  if (alerts_armed && drip_level > 1U) { alarm_bitmap |= 0x0008U; }
  if (alerts_armed && vitals_ai.ai_anomaly) { alarm_bitmap |= 0x0010U; }
  if (current_vitals_valid) { alarm_bitmap |= 0x0020U | 0x0040U; }
  if (hx711_sensor_connected() && hx711_sensor_tared()) { alarm_bitmap |= 0x0080U; }
  if (drop_sensor_count() > 0U) { alarm_bitmap |= 0x0100U; }
  if (system_state == SYSTEM_TARING || runtime_tare_in_progress) { alarm_bitmap |= 0x0200U; }
  if (tare_just_completed) { alarm_bitmap |= 0x0400U; }
  if (hr_baseline_just_completed) { alarm_bitmap |= 0x0800U; }

  uint16_t hr_forecast = current_vitals_valid && vitals_ai.history_ready
                         ? clamp_u16(vitals_ai.hr_forecast_16s) : UINT16_MAX;
  uint16_t spo2_forecast = current_vitals_valid && vitals_ai.history_ready
                           ? clamp_u16(vitals_ai.spo2_forecast_16s) : UINT16_MAX;
  int16_t hr_trend = current_vitals_valid && vitals_ai.history_ready
                     ? clamp_i16((vitals_ai.hr_forecast_16s - (float)current_hr) * 3.75f)
                     : 0;
  uint16_t hr_trend_code = hr_trend > 1 ? 1U : (hr_trend < -1 ? 2U : 0U);
  uint16_t drops_trend_code = drops_trend_dpm_per_min > 1.0f ? 1U
                              : (drops_trend_dpm_per_min < -1.0f ? 2U : 0U);
  bool early_warning = current_vitals_valid && vitals_ai.history_ready
                       && (vitals_ai.hr_forecast_16s < 60.0f
                           || vitals_ai.hr_forecast_16s > 110.0f
                           || vitals_ai.spo2_forecast_16s < 95.0f);
  if (alerts_armed && vitals_ai.history_ready) { ts_flags |= 0x0001U; }
  if (alerts_armed && vitals_ai.ai_anomaly) { ts_flags |= 0x0002U; }
  if (alerts_armed && early_warning) { ts_flags |= 0x0004U; }
  ts_flags |= (uint16_t)(hr_trend_code << 3);
  ts_flags |= (uint16_t)(drops_trend_code << 5);
  if (current_vitals_valid && vitals_ai.history_ready) { ts_flags |= 0x0080U; }
  if (drops_forecast_trusted) { ts_flags |= 0x0100U; }
  /* Keep the physical 0/1/2 enum on the existing wire bits. The converter
   * publishes a separate final_alert_level=1/2/3, so legacy consumers keep
   * working while the server receives the exact main-branch severity. */
  if (alerts_armed) {
    ts_flags |= (uint16_t)((uint16_t)alert_level << 9);
    if (drip_level > 1U) { ts_flags |= 0x0800U; }
    if (vitals_ai.level > 1U) { ts_flags |= 0x1000U; }
  }
  ts_flags |= (uint16_t)((uint16_t)encoded_line_state(current_weight_kg,
                                                       current_drops_per_minute) << 13);
  uint16_t weight_g = current_weight_kg > 0.0f
                      ? clamp_u16(current_weight_kg * 1000.0f) : 0U;
  uint16_t drops_per_min = clamp_u16(current_drops_per_minute);
  uint16_t drop_forecast = drops_forecast_trusted
                           ? clamp_u16(drops_forecast_16s) : UINT16_MAX;
  int16_t drop_trend = drop_forecast_count >= DROP_FORECAST_WINDOW
                       ? clamp_i16(drops_trend_dpm_per_min) : 0;
  bool weight_valid = hx711_sensor_connected() && hx711_sensor_tared();
  uint16_t remaining_ml = weight_valid ? clamp_u16(current_weight_kg * 1000.0f)
                                        : UINT16_MAX;
  uint16_t remaining_min = weight_valid && target_flow_ml_h > 0U
                           ? clamp_u16((float)remaining_ml * 60.0f
                                       / (float)target_flow_ml_h) : UINT16_MAX;

  /* Publish the alarm gate before any clinical value. Zigbee reports may be
   * delivered one attribute at a time; consumers must learn that alarms are
   * still off before they see a startup HR/SpO2 value. */
  write_zcl_u8(ATTR_MONITORING_ACTIVE,
               system_state == SYSTEM_MONITORING ? 1U : 0U);
  write_zcl_u8(ATTR_DROP_TRAINING_SAMPLES, drop_training_samples);
  write_zcl_u8(ATTR_VITALS_TRAINING_SAMPLES, vitals_ai.history_samples);
  write_zcl_u8(ATTR_ALERTS_ARMED, alerts_armed ? 1U : 0U);
  write_zcl_u16(ATTR_DROP_INTERVAL_MS,
                (uint16_t)(drop_sensor_last_interval_seconds() * 1000.0f + 0.5f),
                ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_DROP_EVENT_COUNT, (uint16_t)drop_sensor_count(),
                ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u8(ATTR_SERVER_DROP_LEVEL, drip_level);
  write_zcl_u8(ATTR_VITALS_TEST_MODE, fake_vitals_level);
  write_zcl_i16(ATTR_AI_INPUT_HEART_RATE,
                current_vitals_valid ? ai_input_hr : (int16_t)0x8000);
  write_zcl_u16(ATTR_AI_INPUT_SPO2,
                current_vitals_valid ? (uint16_t)ai_input_spo2 : UINT16_MAX,
                ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u8(ATTR_VITALS_LEVEL, vitals_ai.level);
  write_zcl_i16(ATTR_HEART_RATE,
                current_vitals_valid ? current_hr : (int16_t)0x8000);
  write_zcl_u16(ATTR_SPO2,
                current_vitals_valid ? (uint16_t)current_spo2 : UINT16_MAX,
                ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_FLOW_RATIO, flow_ratio, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_DROP_RATIO, drop_ratio);
  write_zcl_u16(ATTR_ALARM_BITMAP, alarm_bitmap, ZCL_BITMAP16_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_WEIGHT_G, weight_g, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_DROPS_PER_MIN, drops_per_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_TARGET_FLOW_ML_H, target_flow_ml_h, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_TARGET_DROPS_PER_MIN, target_drops_per_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  uint8_t baseline_samples = vitals_ai_baseline_recalibrating()
                             ? vitals_ai_baseline_recalibration_samples()
                             : vitals_ai.baseline_samples;
  write_zcl_u8(ATTR_HR_BASELINE_REMAINING,
               baseline_samples < 60U ? (uint8_t)(60U - baseline_samples) : 0U);
  write_zcl_u16(ATTR_HR_BASELINE_BPM,
                clamp_u16(vitals_ai.hr_baseline), ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u8(ATTR_TARE_EVENT_COUNT, tare_event_count);
  write_zcl_u8(ATTR_HR_BASELINE_EVENTS, hr_baseline_event_count);
  write_zcl_u16(ATTR_TS_FLAGS, ts_flags, ZCL_BITMAP16_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_HR_FORECAST_16S, hr_forecast, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_SPO2_FORECAST_16S, spo2_forecast, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_HR_TREND_BPM_PER_MIN, hr_trend);
  write_zcl_u16(ATTR_TS_ANOMALY_SCORE_X100,
                clamp_u16(vitals_ai.anomaly_score_x100), ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_DROPS_FORECAST_16S, drop_forecast, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_i16(ATTR_DROPS_TREND_DPM_MIN, drop_trend);
  write_zcl_u16(ATTR_REMAINING_ML, remaining_ml, ZCL_INT16U_ATTRIBUTE_TYPE);
  write_zcl_u16(ATTR_REMAINING_MIN, remaining_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  tare_just_completed = false;
  hr_baseline_just_completed = false;
}

/* Exact two-branch fusion contract:
 * 1+1 -> 1, 3+3 -> 3, every other combination -> 2. */
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
  printf("[ALERT] vitals=%u drop=%u final=%u\r\n",
         (unsigned)vitals_ai.level, (unsigned)drip_level, (unsigned)final_level);
}

static int16_t median_int16(const int16_t *values, uint8_t count)
{
  int16_t sorted[VITALS_FILTER_WINDOW];
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

static void append_filter_samples(int16_t *history, uint8_t *history_count,
                                  const int16_t *values, uint8_t count)
{
  for (uint8_t i = 0U; i < count; i++) {
    if (*history_count < VITALS_FILTER_WINDOW) {
      history[(*history_count)++] = values[i];
    } else {
      memmove(&history[0], &history[1],
              (VITALS_FILTER_WINDOW - 1U) * sizeof(history[0]));
      history[VITALS_FILTER_WINDOW - 1U] = values[i];
    }
  }
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
  append_filter_samples(heart_filter_history, &heart_filter_count, values, count);
  if (heart_filter_count < VITALS_FILTER_MIN_SAMPLES) { return heart_rate; }
  int16_t median = median_int16(heart_filter_history, heart_filter_count);
  if (filtered_heart_bpm <= 0.0f) { filtered_heart_bpm = (float)median; return median; }
  float difference = (float)median - filtered_heart_bpm;
  if (absolute_float(difference) > 20.0f) {
    int8_t direction = difference > 0.0f ? 1 : -1;
    if (direction == large_hr_jump_direction) {
      if (large_hr_jump_streak < UINT8_MAX) { large_hr_jump_streak++; }
    } else { large_hr_jump_direction = direction; large_hr_jump_streak = 1U; }
    if (large_hr_jump_streak < 3U) { return (int16_t)(filtered_heart_bpm + 0.5f); }
  } else { large_hr_jump_streak = 0U; large_hr_jump_direction = 0; }
  float step = ((float)median - filtered_heart_bpm) * 0.20f;
  if (step > 4.0f) { step = 4.0f; }
  if (step < -4.0f) { step = -4.0f; }
  filtered_heart_bpm += step;
  return (int16_t)(filtered_heart_bpm + 0.5f);
}

static int16_t filter_spo2(const int16_t *values, uint8_t count)
{
  append_filter_samples(spo2_filter_history, &spo2_filter_count, values, count);
  if (spo2_filter_count < VITALS_FILTER_MIN_SAMPLES) { return spo2; }
  int16_t median = median_int16(spo2_filter_history, spo2_filter_count);
  if (filtered_spo2_percent <= 0.0f) {
    filtered_spo2_percent = (float)median;
    return median;
  }
  float difference = (float)median - filtered_spo2_percent;
  if (absolute_float(difference) > 3.0f) {
    int8_t direction = difference > 0.0f ? 1 : -1;
    if (direction == large_spo2_jump_direction) {
      if (large_spo2_jump_streak < UINT8_MAX) { large_spo2_jump_streak++; }
    } else {
      large_spo2_jump_direction = direction;
      large_spo2_jump_streak = 1U;
    }
    if (large_spo2_jump_streak < 3U) {
      return (int16_t)(filtered_spo2_percent + 0.5f);
    }
  } else {
    large_spo2_jump_streak = 0U;
    large_spo2_jump_direction = 0;
  }
  float step = ((float)median - filtered_spo2_percent) * 0.25f;
  if (step > 2.0f) { step = 2.0f; }
  if (step < -2.0f) { step = -2.0f; }
  filtered_spo2_percent += step;
  return (int16_t)(filtered_spo2_percent + 0.5f);
}

static void all_alerts_off(void)
{
  GPIO_PinOutClear(GREEN_LED_PORT, GREEN_LED_PIN);
  GPIO_PinOutClear(YELLOW_LED_PORT, YELLOW_LED_PIN);
  GPIO_PinOutClear(RED_LED_PORT, RED_LED_PIN);
  GPIO_PinOutSet(BUZZER_PORT, BUZZER_PIN);
  buzzer_on = false;
}

static void reset_monitoring_training(void)
{
  vitals_ai_reset();
  memset(&vitals_ai, 0, sizeof(vitals_ai));
  vitals_ai.level = 1U;
  previous_baseline_samples = 0U;
  drop_sensor_set_min_gap_ms(target_interval_ms / 3U);
  drop_sensor_reset_statistics();
  reset_drop_forecast();
  last_sent_drop_count = 0U;
  drop_training_samples = 0U;
  alerts_armed = false;
  drop_timeout_sent = false;
  alert_level = ALERT_GREEN;
  drip_level = 1U;
  physical_drip_level = 1U;
  ai_drip_level = 1U;
  drip_recovery_streak = 0U;
  monitoring_start_ms = now_ms();
  all_alerts_off();
}

static void reset_drop_training_for_target(void)
{
  drop_sensor_reset_statistics();
  reset_drop_forecast();
  last_sent_drop_count = 0U;
  drop_training_samples = 0U;
  drop_timeout_sent = false;
  drip_level = 1U;
  physical_drip_level = 1U;
  ai_drip_level = 1U;
  drip_recovery_streak = 0U;
  monitoring_start_ms = now_ms();
  alerts_armed = false;
  alert_level = ALERT_GREEN;
  all_alerts_off();
  printf("[MONITOR] Drop target changed; collecting a fresh 0/20 drip window.\r\n");
}

static void start_initial_tare(uint32_t now)
{
  monitoring_requested = monitoring_requested || system_state == SYSTEM_MONITORING;
  system_state = SYSTEM_TARING;
  state_start_ms = now;
  last_screen_ms = 0U;
  alert_level = ALERT_GREEN;
  drip_level = 1U;
  physical_drip_level = 1U;
  ai_drip_level = 1U;
  drip_recovery_streak = 0U;
  memset(&vitals_ai, 0, sizeof(vitals_ai));
  vitals_ai.level = 1U;
  vitals_ai_reset();
  last_sent_drop_count = 0U;
  drop_timeout_sent = false;
  drop_training_samples = 0U;
  alerts_armed = false;
  filtered_heart_bpm = 0.0f;
  filtered_spo2_percent = 0.0f;
  heart_filter_count = 0U;
  spo2_filter_count = 0U;
  large_hr_jump_streak = 0U;
  large_hr_jump_direction = 0;
  large_spo2_jump_streak = 0U;
  large_spo2_jump_direction = 0;
  last_vitals_good_ms = 0U;
  fake_hr_enabled = false;
  fake_spo2_enabled = false;
  fake_vitals_level = 0U;
  drop_sensor_set_min_gap_ms(200U);
  drop_sensor_reset_statistics();
  reset_drop_forecast();
  hx711_sensor_tare();
  tare_just_completed = false;
  all_alerts_off();
  write_zcl_u8(ATTR_MONITORING_ACTIVE, 0U);
  oled_display_message("STARTING", "DO NOT HANG BAG", "TARE IN 10 SEC", NULL);
}

static void start_runtime_tare(uint32_t now)
{
  if (runtime_tare_in_progress || system_state == SYSTEM_TARING) { return; }
  runtime_tare_in_progress = true;
  runtime_tare_start_ms = now;
  tare_just_completed = false;
  hx711_sensor_tare();
  oled_display_message(system_state == SYSTEM_MONITORING ? "MONITORING ON" : "MONITORING PAUSED",
                       "TARING SCALE", "AI STATE KEPT", "PLEASE WAIT");
}

static void update_runtime_tare(uint32_t now)
{
  if (!runtime_tare_in_progress) { return; }
  if ((now - runtime_tare_start_ms) >= TARE_TIME_MS && hx711_sensor_tared()) {
    runtime_tare_in_progress = false;
    tare_event_count++;
    tare_just_completed = true;
    printf("[TARE] Scale reset complete; monitoring and alarm state were preserved.\r\n");
  }
}

static bool process_command(const char *command)
{
  if (strncmp(command, "SET,", 4U) == 0) {
    unsigned long value = strtoul(command + 4, NULL, 10);
    /* The external control unit is drops/minute everywhere (web, gateway and
     * desktop GUI). Milliseconds/drop remain an implementation detail used by
     * the timing model below. */
    if (value >= 1UL && value <= 240UL) {
      apply_target_drops_per_min((uint16_t)value);
      alert_level = ALERT_GREEN;
      drip_level = 1U;
      system_state = SYSTEM_MONITORING;
      monitoring_requested = true;
      reset_monitoring_training();
      save_monitoring_state();
      printf("AI_SET_OK,%lu\r\n", value);
      oled_display_message("COLLECTING DATA", "DROP: 0/20",
                           "HR+SPO2: 0/64", "ALARMS: OFF");
    } else { printf("AI_SET_ERROR\r\n"); }
    return true;
  } else if (strncmp(command, "LEVEL,", 6U) == 0) {
    int level = atoi(command + 6);
    if (system_state == SYSTEM_MONITORING && level >= 1 && level <= 3) {
      /* The desktop AI can only ever raise the drip alarm the firmware's
       * own physical reading already found - it must never quietly lower
       * one (see drip_level = max(physical, ai) below). */
      ai_drip_level = (uint8_t)level;
      drip_level = physical_drip_level > ai_drip_level ? physical_drip_level : ai_drip_level;
      if (alerts_armed) { update_final_alert(); }
      printf("AI_LEVEL_OK,%d\r\n", level);
    }
    return true;
  } else if (strncmp(command, "FAKE_HR,", 8U) == 0) {
    bool was_enabled = fake_hr_enabled;
    fake_hr_enabled = atoi(command + 8) != 0;
    if (was_enabled && !fake_hr_enabled) {
      vitals_ai_clear_history(&vitals_ai);
      if (alerts_armed) { update_final_alert(); }
      printf("VITAL_TEST_HISTORY_CLEARED\r\n");
    }
    printf("FAKE_HR_OK,%u\r\n", fake_hr_enabled ? 1U : 0U);
    return true;
  } else if (strncmp(command, "FAKE_SPO2,", 10U) == 0) {
    bool was_enabled = fake_spo2_enabled;
    fake_spo2_enabled = atoi(command + 10) != 0;
    if (was_enabled && !fake_spo2_enabled) {
      vitals_ai_clear_history(&vitals_ai);
      if (alerts_armed) { update_final_alert(); }
      printf("VITAL_TEST_HISTORY_CLEARED\r\n");
    }
    printf("FAKE_SPO2_OK,%u\r\n", fake_spo2_enabled ? 1U : 0U);
    return true;
  } else if (strncmp(command, "FAKE_VITAL,", 11U) == 0) {
    int level = atoi(command + 11);
    if (level >= 0 && level <= 3 && level != 1) {
      bool turning_on = fake_vitals_level == 0U && level != 0;
      bool turning_off = fake_vitals_level != 0U && level == 0;
      if (turning_on) { vitals_ai_begin_test(); }
      fake_vitals_level = (uint8_t)level;
      if (turning_off) {
        vitals_ai_end_test(&vitals_ai);
        if (alerts_armed) { update_final_alert(); }
        printf("VITAL_TEST_HISTORY_RESTORED\r\n");
      }
      printf("FAKE_VITAL_OK,%d\r\n", level);
    }
    return true;
  }

  return false;
}

/* sl_cli owns VCOM RX. This dispatcher preserves the legacy comma commands,
 * then hands every other complete line back to the Silicon Labs command
 * interpreter (info, plugin network-steering, network leave, OTA, and so on).
 * No application code reads the VCOM stream directly. */
static void smart_iv_cli_dispatch(char *command, void *user)
{
  sl_cli_handle_t cli_handle = (sl_cli_handle_t)user;

  if (process_command(command)) {
    return;
  }

  /* Temporarily restore normal dispatch so the SDK keeps its standard error
   * reporting and all generated command groups remain unchanged. */
  sl_cli_redirect_command(cli_handle, NULL, NULL, NULL);
  (void)sl_cli_handle_input(cli_handle, command);
  sl_cli_redirect_command(cli_handle, smart_iv_cli_dispatch, NULL, cli_handle);
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
    system_state = monitoring_requested ? SYSTEM_MONITORING : SYSTEM_WAITING_AI_SET;
    if (monitoring_requested) { reset_monitoring_training(); }
    tare_event_count++;
    tare_just_completed = true;
    last_screen_ms = now;
    printf("AI_READY\r\n");
    if (monitoring_requested) {
      oled_display_message("COLLECTING DATA", "DROP: 0/20",
                           "HR+SPO2: 0/64", "ALARMS: OFF");
    } else {
      oled_display_message("SYSTEM READY", "HANG IV BAG",
                           "OPEN HIS WEB", "SET DROP RATE");
    }
  }
}

void sl_zigbee_af_post_attribute_change_cb(uint8_t endpoint,
                                           sl_zigbee_af_cluster_id_t cluster_id,
                                           sl_zigbee_af_attribute_id_t attribute_id,
                                           uint8_t mask,
                                           uint16_t manufacturer_code,
                                           uint8_t type,
                                           uint8_t size,
                                           uint8_t *value)
{
  (void)mask;
  (void)type;
  (void)size;
  if (zcl_internal_write) { return; }
  if (endpoint != SMART_IV_ENDPOINT || cluster_id != SMART_IV_CLUSTER_ID
      || manufacturer_code != SMART_IV_MANUFACTURER_CODE || value == NULL) {
    return;
  }

  if (attribute_id == ATTR_TARGET_FLOW_ML_H) {
    uint16_t next;
    memcpy(&next, value, sizeof(next));
    if (next != target_flow_ml_h) {
      target_flow_ml_h = next;
      printf("[ZB] Target flow set to %u ml/h.\r\n", (unsigned)next);
    }
    return;
  }

  if (attribute_id == ATTR_TARGET_DROPS_PER_MIN) {
    uint16_t next;
    memcpy(&next, value, sizeof(next));
    if (next >= 1U && next <= 240U) {
      bool target_changed = next != target_drops_per_min;
      monitoring_requested = true;
      if (system_state == SYSTEM_WAITING_AI_SET) {
        apply_target_drops_per_min(next);
        system_state = SYSTEM_MONITORING;
        reset_monitoring_training();
        oled_display_message("TARGET RECEIVED", "COLLECTING DATA",
                             "DROP: 0/20", "HR+SPO2: 0/64");
        printf("[MONITOR] Target confirmed by HIS Web; starting 20 drop and 64 vitals samples.\r\n");
      } else if (system_state == SYSTEM_MONITORING && target_changed) {
        apply_target_drops_per_min(next);
        reset_drop_training_for_target();
      }
      save_monitoring_state();
      printf("[ZB] Target drop rate set to %u dpm.\r\n", (unsigned)next);
    }
    return;
  }

  if (attribute_id == ATTR_MONITORING_ACTIVE) {
    bool want = *value != 0U;
    bool active = system_state == SYSTEM_MONITORING;
    if (want == active && want == monitoring_requested) { return; }
    monitoring_requested = want;
    save_monitoring_state();
    if (!want && system_state != SYSTEM_TARING) {
      system_state = SYSTEM_WAITING_AI_SET;
      alerts_armed = false;
      alert_level = ALERT_GREEN;
      drip_level = 1U;
      physical_drip_level = 1U;
      ai_drip_level = 1U;
      drip_recovery_streak = 0U;
      all_alerts_off();
      oled_display_message("MONITORING PAUSED", "ALARMS: OFF",
                           "AI: STOPPED", "START FROM HIS WEB");
      printf("[MONITOR] Monitoring stopped by HIS Server.\r\n");
    } else if (want && system_state != SYSTEM_TARING) {
      system_state = SYSTEM_MONITORING;
      reset_monitoring_training();
      oled_display_message("COLLECTING DATA", "DROP: 0/20",
                           "HR+SPO2: 0/64", "ALARMS: OFF");
      printf("[MONITOR] Collecting 20 drip intervals and 64 vitals samples; alarms off.\r\n");
    }
    return;
  }

  if (attribute_id == ATTR_SERVER_DROP_LEVEL) {
    uint8_t next = *value;
    if (system_state == SYSTEM_MONITORING && next >= 1U && next <= 3U) {
      /* A server/AI verdict may escalate the physical safety verdict, never
       * clear it. Otherwise a delayed level-1 write can turn the LED green
       * while the local watchdog is still reporting a blocked line. */
      ai_drip_level = next;
      drip_level = physical_drip_level > ai_drip_level
                   ? physical_drip_level : ai_drip_level;
      if (alerts_armed) { update_final_alert(); }
      printf("[ZB] Server drip decision=%u, physical=%u, effective=%u.\r\n",
             (unsigned)next, (unsigned)physical_drip_level,
             (unsigned)drip_level);
    }
    return;
  }

  if (attribute_id == ATTR_VITALS_TEST_MODE) {
    uint8_t next = *value;
    if (next != 0U && next != 2U && next != 3U) {
      write_zcl_u8(ATTR_VITALS_TEST_MODE, fake_vitals_level);
      return;
    }
    if (fake_vitals_level == next) { return; }
    if (fake_vitals_level == 0U && next != 0U) {
      vitals_ai_begin_test();
    } else if (fake_vitals_level != 0U && next == 0U) {
      vitals_ai_end_test(&vitals_ai);
    }
    fake_vitals_level = next;
    if (alerts_armed) { update_final_alert(); }
    printf("[ZB] Vitals test mode=%u (0=real,2=attention,3=alarm).\r\n",
           (unsigned)next);
    return;
  }

  if (attribute_id == ATTR_TARE_COMMAND) {
    if (*value != 0U) {
      start_runtime_tare(now_ms());
      write_zcl_u8(ATTR_TARE_COMMAND, 0U);
      printf("[ZB] Remote tare started.\r\n");
    }
    return;
  }

  if (attribute_id == ATTR_HR_RECALIBRATE) {
    if (*value != 0U) {
      reset_hr_baseline();
      write_zcl_u8(ATTR_HR_RECALIBRATE, 0U);
    }
  }
}

void sl_zigbee_af_stack_status_cb(sl_status_t status)
{
  if (status == SL_STATUS_NETWORK_DOWN) {
    if (!zb_join_started) {
      zb_join_started = true;
      sl_status_t join_status = sl_zigbee_af_network_steering_start();
      printf("[ZB] Starting network join: 0x%02X\r\n", (unsigned)join_status);
      if (join_status != SL_STATUS_OK) { zb_join_started = false; }
    }
  } else if (status == SL_STATUS_NETWORK_UP) {
    zb_join_started = false;
    configure_zigbee_reporting();
    printf("[ZB] Network up; Smart IV reporting configured.\r\n");
  }
}

void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t total_beacons,
                                               uint8_t join_attempts,
                                               uint8_t final_state)
{
  (void)total_beacons;
  (void)join_attempts;
  (void)final_state;
  printf("[ZB] Network join result: 0x%02X\r\n", (unsigned)status);
  if (status != SL_STATUS_OK) { zb_join_started = false; }
}

static void update_alert_outputs(uint32_t now)
{
  static alert_level_t previous_level = ALERT_GREEN;
  if (system_state != SYSTEM_MONITORING || !alerts_armed) { all_alerts_off(); return; }
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
    /* Only expose the human-facing unit during setup. The raw millisecond
     * interval is still retained by the firmware/AI data path. */
    printf("SETUP_DROP,%.1f\r\n", (double)rate);
    last_sent_drop_count = count;
    return;
  }

  if (system_state != SYSTEM_MONITORING || target_interval_ms == 0U) { return; }
  uint32_t intervals = count - 1U;
  drop_training_samples = (uint8_t)(intervals < DROP_TRAINING_REQUIRED
                          ? intervals : DROP_TRAINING_REQUIRED);
  update_physical_drip_level(actual_ms);
  update_drop_forecast(now_ms(), actual_ms);
  if (alerts_armed) { update_final_alert(); }
  printf("%lu,%lu,%lu,%lu\r\n", (unsigned long)now_ms(), (unsigned long)count,
         (unsigned long)target_interval_ms, (unsigned long)actual_ms);
  last_sent_drop_count = count;
}

/* Covers three physical faults as one timeout check:
 *   - a drop that stops arriving after monitoring was already armed
 *     (last_drop is recent, then goes stale);
 *   - a line that is fully blocked from the very first drop, so
 *     drop_sensor_last_drop_ms() is still 0 - timed from monitoring_start_ms
 *     instead;
 *   - recovery, once a fresh drop arrives and moves last_drop forward again.
 * A total stoppage detected before the 20/64-sample training window
 * completes forces alerts_armed on early: silence during ordinary setup
 * noise is intentional elsewhere in this firmware, but "zero drops at all
 * since Start" for two full target intervals is not setup noise, it is the
 * exact physical signature of a fully occluded or disconnected line, and
 * must not wait for a baseline that a blocked line will never produce. */
static void monitor_drop_timeout(uint32_t now)
{
  if (system_state != SYSTEM_MONITORING || target_interval_ms == 0U) { return; }
  uint32_t last_drop = drop_sensor_last_drop_ms();
  uint32_t reference = last_drop != 0U ? last_drop : monitoring_start_ms;
  if (reference == 0U) { return; }
  int32_t signed_elapsed = (int32_t)(now - reference);
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
      printf("[DROP-AI] TIMEOUT elapsed=%lu target=%lu physical=3\r\n",
             (unsigned long)elapsed, (unsigned long)target_interval_ms);
    }
    if (physical_drip_level != 3U) {
      physical_drip_level = 3U;
      drip_recovery_streak = 0U;
      drip_level = physical_drip_level > ai_drip_level ? physical_drip_level : ai_drip_level;
      if (!alerts_armed) {
        alerts_armed = true;
        printf("[MONITOR] Total flow stoppage before training completed; arming alarms early.\r\n");
      }
      update_final_alert();
    }
  } else {
    drop_timeout_sent = false;
  }
}

static void publish_display(void)
{
  uint32_t now = now_ms();
  bool received_vitals = heart_count > 0U && spo2_count > 0U;
  if (received_vitals) {
    heart_rate = filter_heart_rate(heart_samples, heart_count);
    spo2 = filter_spo2(spo2_samples, spo2_count);
  }
  bool new_vitals = received_vitals
                    && heart_filter_count >= VITALS_FILTER_MIN_SAMPLES
                    && spo2_filter_count >= VITALS_FILTER_MIN_SAMPLES;
  if (new_vitals) {
    last_vitals_good_ms = now;
  }
  vitals_valid = last_vitals_good_ms != 0U
                  && (uint32_t)(now - last_vitals_good_ms) <= VITALS_HOLD_MS;
  if (!vitals_valid && last_vitals_good_ms != 0U) {
    /* A real signal loss starts a fresh filter window.  Otherwise old finger
     * readings can pull the first measurements after contact is restored. */
    heart_filter_count = 0U;
    spo2_filter_count = 0U;
    filtered_heart_bpm = 0.0f;
    filtered_spo2_percent = 0.0f;
    large_hr_jump_streak = 0U;
    large_hr_jump_direction = 0;
    large_spo2_jump_streak = 0U;
    large_spo2_jump_direction = 0;
    last_vitals_good_ms = 0U;
  }
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
      ai_spo2 = (int16_t)(vitals_ai.spo2_baseline * 0.75f + 0.5f);
    }
    /* Feed each real measurement to AI once; held display values are not
       repeated as if they were new sensor samples. */
    if (new_vitals) {
      vitals_ai_step(ai_heart_rate, ai_spo2, true, &vitals_ai);
      if (previous_baseline_samples < 60U && vitals_ai.baseline_samples >= 60U) {
        hr_baseline_event_count++;
        hr_baseline_just_completed = true;
      }
      previous_baseline_samples = vitals_ai.baseline_samples;
      if (vitals_ai_take_baseline_recalibration_completed()) {
        hr_baseline_event_count++;
        hr_baseline_just_completed = true;
        printf("[HR] Candidate baseline committed atomically; history and alert state kept.\r\n");
      }
    }
    if (!alerts_armed
        && drop_training_samples >= DROP_TRAINING_REQUIRED
        && vitals_ai.history_samples >= VITALS_TRAINING_REQUIRED) {
      alerts_armed = true;
      alert_level = ALERT_GREEN;
      printf("[MONITOR] Training complete; AI and alarms armed.\r\n");
    }
    if (alerts_armed) {
      update_final_alert();
    } else {
      alert_level = ALERT_GREEN;
      all_alerts_off();
    }
    float interval = drop_sensor_last_interval_seconds();
    float stable_interval = drop_sensor_median_interval_seconds();
    float rate = stable_interval > 0.0f ? 60.0f / stable_interval : 0.0f;
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
    /* Fake values exist only on the AI decision path. OLED, telemetry and
       Zigbee continue to expose the real sensor readings for comparison. */
    int16_t output_heart_rate = vitals_valid ? heart_rate : 0;
    int16_t output_spo2 = vitals_valid ? spo2 : 0;
    if (runtime_tare_in_progress) {
      oled_display_message("MONITORING ON", "TARING SCALE",
                           "AI STATE KEPT", "ALARM STATE KEPT");
    } else {
      oled_display_monitor(heart_rate, spo2, vitals_valid, weight_kg,
                           hx711_sensor_connected() && hx711_sensor_tared(),
                           interval, (float)target_interval_ms / 1000.0f,
                           rate, (uint8_t)alert_level + 1U, vitals_ai.level,
                           drip_level, hr_cause, spo2_cause,
                           vitals_ai_baseline_recalibrating()
                           ? vitals_ai_baseline_recalibration_samples()
                           : vitals_ai.baseline_samples,
                           vitals_ai.history_samples, drop_training_samples, true,
                           alerts_armed,
                           vitals_ai_baseline_recalibrating());
    }
    publish_zigbee_attributes(output_heart_rate, output_spo2,
                              ai_heart_rate, ai_spo2, vitals_valid,
                              weight_kg, rate);
    printf("TELEMETRY,%d,%d,%.3f,%.3f,%.1f,%.1f,%u,%u,%u,%.1f,%.1f,%u,%u,%u,%u,%u\r\n",
           output_heart_rate, output_spo2, (double)weight_kg, (double)interval,
           (double)target_drops_per_min,
           (double)rate, (unsigned int)alert_level + 1U,
           (unsigned int)vitals_ai.level, (unsigned int)drip_level,
           (double)vitals_ai.hr_baseline, (double)vitals_ai.spo2_baseline,
           (unsigned int)vitals_ai.baseline_samples,
           (unsigned int)vitals_ai.history_samples,
           fake_hr_enabled ? 1U : 0U, fake_spo2_enabled ? 1U : 0U,
           (unsigned int)fake_vitals_level);
  } else if (system_state == SYSTEM_WAITING_AI_SET) {
    /* Waiting is a live-preview state: sensors and drop timing are visible on
     * OLED/HIS, but no sample is admitted to either AI training window and no
     * alarm decision is made until the web target write arrives. */
    float interval = drop_sensor_last_interval_seconds();
    float stable_interval = drop_sensor_median_interval_seconds();
    float rate = stable_interval > 0.0f ? 60.0f / stable_interval : 0.0f;
    int16_t output_heart_rate = vitals_valid ? heart_rate : 0;
    int16_t output_spo2 = vitals_valid ? spo2 : 0;
    oled_display_monitor(heart_rate, spo2, vitals_valid, weight_kg,
                         hx711_sensor_connected() && hx711_sensor_tared(),
                         interval, (float)target_interval_ms / 1000.0f,
                         rate, 1U, 1U, 1U, false, false,
                         0U, 0U, 0U, false, false, false);
    publish_zigbee_attributes(output_heart_rate, output_spo2,
                              output_heart_rate, output_spo2, vitals_valid,
                              weight_kg, rate);
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
  load_monitoring_state();
  start_initial_tare(now);
  sl_cli_redirect_command(sl_cli_inst0_handle, smart_iv_cli_dispatch,
                          NULL, sl_cli_inst0_handle);
  printf("SMART_IV_G26,OLED=%s,MAX30102=%s,VITALS_AI=%s\r\n",
         oled_ok ? "OK" : "NO", max_ok ? "OK" : "NO",
         vitals_models_ok ? "OK" : "NO");
}

/* --- OTA progress on OLED --------------------------------------------------
 *
 * Called from real Zigbee OTA client code (see the SDK edits in
 * ota-client.c / ota-client-policy.c - not a timer simulation of progress).
 * These only set OLED state; oled_display_ota_step() below, called every
 * app_process_action() tick like the rest of this app, owns the actual I2C
 * writes so nothing here can block the OTA client's own event loop.
 */
void smart_iv_ota_progress_cb(uint8_t percent)
{
  oled_display_set_ota_progress(true, percent);
}

/* Mirrors sl_zigbee_af_ota_download_result_t (af-types.h) - kept as plain
 * uint8_t at the call site so ota-client-policy.c does not need this app's
 * headers, just a forward declaration. */
static const char *ota_failure_reason_text(uint8_t resultCode)
{
  switch (resultCode) {
    case 1U: return "TIMEOUT";        /* SL_ZIGBEE_AF_OTA_DOWNLOAD_TIME_OUT */
    case 2U: return "INVALID IMAGE";  /* SL_ZIGBEE_AF_OTA_VERIFY_FAILED */
    case 3U: return "SERVER ABORTED"; /* SL_ZIGBEE_AF_OTA_SERVER_ABORTED */
    case 4U: return "CLIENT ABORTED"; /* SL_ZIGBEE_AF_OTA_CLIENT_ABORTED */
    case 5U: return "ERASE FAILED";   /* SL_ZIGBEE_AF_OTA_ERASE_FAILED */
    default: return "UPDATE ERROR";
  }
}

void smart_iv_ota_failed_cb(uint8_t resultCode)
{
  oled_display_set_ota_result(false, ota_failure_reason_text(resultCode));
}

void smart_iv_ota_success_cb(void)
{
  /* This fires exactly once, immediately before the OTA client reboots into
   * the new image - control never returns to app_process_action()'s next
   * tick, so this is the one place an immediate render (not a delay, just
   * the existing bounded I2C write oled_display_ota_step() already does) is
   * required for "UPDATE DONE / REBOOT" to ever reach the screen at all. */
  oled_display_set_ota_result(true, NULL);
  oled_display_ota_step(now_ms());
}

void app_process_action(void)
{
  uint32_t now = now_ms();
  oled_display_ota_step(now);
  drop_sensor_poll();
  hx711_sensor_poll();
  bool pressed = GPIO_PinInGet(TARE_PORT, TARE_PIN) == 0;
  if (pressed && !previous_button && (now - last_button_ms) >= 200U) {
    last_button_ms = now;
    start_runtime_tare(now);
  }
  previous_button = pressed;
  update_startup(now);
  update_runtime_tare(now);
  send_new_drop_to_ai();
  monitor_drop_timeout(now);
  update_alert_outputs(now);
  if ((now - last_sample_ms) >= SAMPLE_INTERVAL_MS) {
    last_sample_ms = now;
    sample_sensors();
  }
}

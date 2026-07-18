/* ============================================================================
 *  app.c — AI Smart IV project (BRD2709A / EFR32xG26). PASTE this into app.c.
 *
 *  Architecture as required:
 *    - Channels that ARE connected (see #define in sensor_hub.h) -> read real data.
 *    - Channels NOT yet connected -> reported as "not present" (DISABLED), NO false alarms.
 *    - Adding a sensor later -> just flip its #define -> automatically read + monitored.
 *
 *  Main loop:
 *    - sensor_hub_poll(): called EVERY iteration (reads drops continuously, never misses a pulse).
 *    - Every 1 second: ai_monitor_step() -> runs the autoencoder + rules -> prints status.
 *
 *  COMPONENTS to install (.slcp -> SOFTWARE COMPONENTS):
 *    - IO Stream: EUSART (instance "vcom")  + IO Stream: STDIO   -> printf over VCOM
 *    - Sleep Timer                                               -> replaces millis()
 *    - Tensorflow Lite Micro                                     -> runs the AI model
 *  (When enabling more channels: I2CSPM for MAX30102, GPIO/uDelay for HX711... add later)
 * ========================================================================== */

#include <stdio.h>
#include <string.h>
#include "sl_sleeptimer.h"
#include "sensor_hub.h"
#include "ai_monitor.h"

#include "app/framework/include/af.h"
#include "network-steering.h"
#include "app/framework/plugin/reporting/reporting.h"

#define AI_PERIOD_MS    1000U   // AI run period (1 second)
#define HR_CALIB_MS    60000U   // first 60s: calibrate the per-patient HR baseline

/* Endpoint sending data over Zigbee (defined in config/zcl/zcl_config.zap):
 * a single custom "Smart IV Vitals" cluster (ZCL_SMART_IV_VITALS_CLUSTER_ID,
 * 0xFC01, mfgCode 0x1049) on a single endpoint, with 5 properly-named
 * attributes (HeartRate/Spo2/FlowRatio/DropRatio/AlarmBitmap) - replacing the
 * old approach of "borrowing" 5 standard measurement clusters
 * (Temperature/Humidity/Flow/Pressure/Illuminance Measurement) across 5
 * separate endpoints and repurposing their MeasuredValue. See
 * config/zcl/smart-iv-vitals.xml for the cluster definition. */
#define ZB_EP_VITALS       2
#define SMART_IV_MFG_CODE  0x1049

/* "No real data" sentinel values when sending over Zigbee - following the
 * standard ZCL convention per attribute type (NOT using a plausible bpm/%
 * number like HR_BASE_FILL anymore, to avoid a gateway/log reader mistaking
 * it for a real reading):
 *   - Temperature Measurement (int16s)      : 0x8000 = invalid value
 *   - Relative Humidity Measurement (uint16): 0xFFFF = invalid value
 * Both are standard ZCL special values that never collide with any real bpm/%. */
#define ZCL_HR_INVALID     ((int16_t)0x8000)
#define ZCL_SPO2_INVALID   ((uint16_t)0xFFFF)

static bool zb_join_started = false;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

/* HR baseline calibration window - file-scope (not a function-local static)
 * so it can be reset by a remote "recalibrate HR baseline" command (see
 * app_trigger_hr_recalibration()), not just run once automatically at
 * boot. calib_start_ms is the reference point the 60s window counts from -
 * initially 0 (i.e. starts immediately at boot, same as before), but reset
 * to the current uptime whenever recalibration is triggered later. */
static bool     calib_done      = false;
static uint32_t calib_start_ms  = 0;

/* Persistent count of completed HR baseline calibrations (never resets) -
 * exists alongside the one-shot hr_baseline_just_completed pulse for the
 * same reason as sensor_hub.c's tare_event_count: a transient pulse can be
 * missed if it fires before Zigbee reporting has been configured (which
 * only happens after network join completes). The HIS Server instead
 * detects "count increased since last seen" and stamps its own wall-clock
 * timestamp when it does - this can't race with boot timing. */
static uint8_t  hr_baseline_event_count = 0;

/* Re-arms the 60s HR baseline calibration window - same effect as a fresh
 * boot's automatic calibration, but callable at any time (e.g. from
 * sl_zigbee_af_post_attribute_change_cb() when the doctor sends a
 * "recalibrate HR baseline" command from the HIS Server). */
static void app_trigger_hr_recalibration(void)
{
  calib_start_ms = now_ms();
  calib_done     = false;
  printf("[HR] Recalibrating baseline - measuring for the next 60s...\r\n");
}

/* Live countdown for the HIS Server dashboard, so the doctor can see "Xs
 * remaining" instead of guessing when the 60s baseline window will finish.
 * Returns 0 once calibration is done (or hasn't started counting down from
 * a sane reference yet). */
static uint8_t app_hr_baseline_seconds_remaining(void)
{
  if (calib_done) {
    return 0;
  }
  uint32_t elapsed = now_ms() - calib_start_ms;
  if (elapsed >= HR_CALIB_MS) {
    return 0;
  }
  return (uint8_t)((HR_CALIB_MS - elapsed) / 1000U);
}

/* Default reporting configuration for the attributes carrying AI data, so
 * the device automatically sends reports to the coordinator (zigbee2mqtt)
 * even before the Pi-side external converter gets a chance to configure its
 * own reporting. All share the same endpoint + cluster (only the
 * attributeId differs), and must declare the correct manufacturerCode since
 * this is a manufacturer-specific cluster. WeightG/DropsPerMin are raw
 * telemetry (so a doctor viewing the bed dashboard can see actual numbers,
 * not just percentages); TargetFlowMlH is reported too so the dashboard can
 * confirm a rate change was actually applied by the device. */
static void zb_configure_reporting(void)
{
  uint16_t attributeIds[] = {
    ZCL_HEART_RATE_ATTRIBUTE_ID,
    ZCL_SPO2_ATTRIBUTE_ID,
    ZCL_FLOW_RATIO_ATTRIBUTE_ID,
    ZCL_DROP_RATIO_ATTRIBUTE_ID,
    ZCL_ALARM_BITMAP_ATTRIBUTE_ID,
    ZCL_WEIGHT_G_ATTRIBUTE_ID,
    ZCL_DROPS_PER_MIN_ATTRIBUTE_ID,
    ZCL_TARGET_FLOW_ML_H_ATTRIBUTE_ID,
    ZCL_TARGET_DROPS_PER_MIN_ATTRIBUTE_ID,
    ZCL_HR_BASELINE_SECONDS_REMAINING_ATTRIBUTE_ID,
    ZCL_HR_BASELINE_BPM_ATTRIBUTE_ID,
    ZCL_TARE_EVENT_COUNT_ATTRIBUTE_ID,
    ZCL_HR_BASELINE_EVENT_COUNT_ATTRIBUTE_ID,
  };

  for (uint8_t i = 0; i < sizeof(attributeIds) / sizeof(attributeIds[0]); i++) {
    sl_zigbee_af_plugin_reporting_entry_t reportingEntry;
    reportingEntry.direction = SL_ZIGBEE_ZCL_REPORTING_DIRECTION_REPORTED;
    reportingEntry.endpoint = ZB_EP_VITALS;
    reportingEntry.clusterId = ZCL_SMART_IV_VITALS_CLUSTER_ID;
    reportingEntry.attributeId = attributeIds[i];
    reportingEntry.mask = CLUSTER_MASK_SERVER;
    reportingEntry.manufacturerCode = SMART_IV_MFG_CODE;
    reportingEntry.data.reported.minInterval = 1;
    reportingEntry.data.reported.maxInterval = 60;
    reportingEntry.data.reported.reportableChange = 1;
    sl_zigbee_af_reporting_configure_reported_attribute(&reportingEntry);
  }
}

/* Writes the AI values + raw telemetry into the custom Smart IV Vitals
 * cluster -> the Reporting plugin will automatically send a report when a
 * value changes (per the configuration above). Uses the "manufacturer
 * specific" write function (as opposed to the regular one) since the
 * cluster has an mfgCode. */
static void zb_report_ai_result(int16_t hr, uint16_t spo2, uint16_t flow_x100,
                                 int16_t drop_x100, uint16_t alarm_bitmap,
                                 uint16_t weight_g, uint16_t drops_per_min,
                                 uint16_t target_flow_ml_h, uint16_t target_drops_per_min,
                                 uint8_t hr_baseline_seconds_remaining, uint16_t hr_baseline_bpm,
                                 uint8_t tare_event_count, uint8_t hr_baseline_event_count)
{
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_HEART_RATE_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&hr, ZCL_INT16S_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_SPO2_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&spo2, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_FLOW_RATIO_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&flow_x100, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_DROP_RATIO_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&drop_x100, ZCL_INT16S_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_ALARM_BITMAP_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&alarm_bitmap, ZCL_BITMAP16_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_WEIGHT_G_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&weight_g, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_DROPS_PER_MIN_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&drops_per_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_TARGET_FLOW_ML_H_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&target_flow_ml_h, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_TARGET_DROPS_PER_MIN_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&target_drops_per_min, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_HR_BASELINE_SECONDS_REMAINING_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&hr_baseline_seconds_remaining, ZCL_INT8U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_HR_BASELINE_BPM_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&hr_baseline_bpm, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_TARE_EVENT_COUNT_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&tare_event_count, ZCL_INT8U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_manufacturer_specific_server_attribute(
      ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_HR_BASELINE_EVENT_COUNT_ATTRIBUTE_ID,
      SMART_IV_MFG_CODE, (uint8_t *)&hr_baseline_event_count, ZCL_INT8U_ATTRIBUTE_TYPE);
}

/** @brief Post Attribute Change
 *
 * Fires for ANY attribute write, regardless of cluster - including a
 * network "Write Attributes" command from the coordinator (i.e. the doctor
 * changing a setting or triggering an action from the HIS Server, relayed
 * down through zigbee2mqtt as a Zigbee attribute write). We only care about
 * writes to our own cluster's settings/command attributes:
 *   - TargetFlowMlH / TargetDropsPerMin: apply the new target immediately.
 *   - TareCommand / HrRecalibrateCommand: any nonzero write is a "fire" -
 *     trigger the action, then write the attribute back to 0 so the NEXT
 *     write of the same value (e.g. the doctor pressing the button again)
 *     is still seen as a 0->nonzero change and fires again.
 */
void sl_zigbee_af_post_attribute_change_cb(uint8_t endpoint,
                                           sl_zigbee_af_cluster_id_t clusterId,
                                           sl_zigbee_af_attribute_id_t attributeId,
                                           uint8_t mask,
                                           uint16_t manufacturerCode,
                                           uint8_t type,
                                           uint8_t size,
                                           uint8_t *value)
{
  (void)mask;
  (void)type;
  (void)size;

  if (endpoint != ZB_EP_VITALS
      || clusterId != ZCL_SMART_IV_VITALS_CLUSTER_ID
      || manufacturerCode != SMART_IV_MFG_CODE) {
    return;
  }

  if (attributeId == ZCL_TARGET_FLOW_ML_H_ATTRIBUTE_ID) {
    uint16_t new_target_ml_h;
    memcpy(&new_target_ml_h, value, sizeof(new_target_ml_h));

    /* zb_report_ai_result() also writes this SAME attribute every AI tick
     * (just to report the currently-active value back up for dashboard
     * confirmation), which would otherwise trigger this callback and log a
     * misleading "doctor changed it" message once a second even when
     * nothing changed. Only react when the incoming value actually differs
     * from what's already active. */
    if ((uint16_t)(sh_target_flow_ml_h() + 0.5f) == new_target_ml_h) {
      return;
    }
    sh_set_target_flow_ml_h((float)new_target_ml_h);
    printf("[ZB] Doctor set a new target infusion rate: %u ml/h\r\n", (unsigned)new_target_ml_h);
    return;
  }

  if (attributeId == ZCL_TARGET_DROPS_PER_MIN_ATTRIBUTE_ID) {
    uint16_t new_target_dpm;
    memcpy(&new_target_dpm, value, sizeof(new_target_dpm));

    if ((uint16_t)(sh_target_drops_per_min() + 0.5f) == new_target_dpm) {
      return;   // same "reported back every tick" self-write issue as above
    }
    sh_set_target_drops_per_min((float)new_target_dpm);
    printf("[ZB] Doctor set a new target drop rate: %u dpm\r\n", (unsigned)new_target_dpm);
    return;
  }

  if (attributeId == ZCL_TARE_COMMAND_ATTRIBUTE_ID) {
    if (*value != 0) {
      sh_flow_trigger_tare();
      uint8_t reset_value = 0;
      sl_zigbee_af_write_manufacturer_specific_server_attribute(
          ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_TARE_COMMAND_ATTRIBUTE_ID,
          SMART_IV_MFG_CODE, &reset_value, ZCL_INT8U_ATTRIBUTE_TYPE);
    }
    return;
  }

  if (attributeId == ZCL_HR_RECALIBRATE_COMMAND_ATTRIBUTE_ID) {
    if (*value != 0) {
      app_trigger_hr_recalibration();
      uint8_t reset_value = 0;
      sl_zigbee_af_write_manufacturer_specific_server_attribute(
          ZB_EP_VITALS, ZCL_SMART_IV_VITALS_CLUSTER_ID, ZCL_HR_RECALIBRATE_COMMAND_ATTRIBUTE_ID,
          SMART_IV_MFG_CODE, &reset_value, ZCL_INT8U_ATTRIBUTE_TYPE);
    }
    return;
  }
}

/** @brief Stack Status
 *
 * Starts joining the network (Network Steering) when there is no network
 * yet; once the network is up, configures reporting for the 5 AI attributes.
 */
void sl_zigbee_af_stack_status_cb(sl_status_t status)
{
  if (status == SL_STATUS_NETWORK_DOWN) {
    if (!zb_join_started) {
      zb_join_started = true;
      sl_status_t joinStatus = sl_zigbee_af_network_steering_start();
      printf("[ZB] Starting network join: 0x%02X\r\n", (unsigned)joinStatus);
    }
  } else if (status == SL_STATUS_NETWORK_UP) {
    zb_configure_reporting();
    printf("[ZB] Joined the Zigbee network, reporting configured.\r\n");
  }
}

/** @brief Network Steering Complete
 *
 * Allows retrying the join if this attempt failed (e.g. permit-join was not
 * enabled on the coordinator).
 */
void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState)
{
  (void)totalBeacons;
  (void)joinAttempts;
  (void)finalState;

  printf("[ZB] Network join result: 0x%02X\r\n", (unsigned)status);
  if (status != SL_STATUS_OK) {
    zb_join_started = false;   // allow sl_zigbee_af_stack_status_cb to retry
  }
}

/* ================= CALLED ONCE AT STARTUP ================= */
void app_init(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);   // disable buffering -> printf shows up immediately

  sensor_hub_init();
  ai_monitor_init();

  printf("\r\n=== Smart IV - AI module ready ===\r\n");
  printf("Channels: DROPS=%s HR=%s SpO2=%s FLOW=%s\r\n",
         DROPS_ENABLED ? "ON" : "OFF",
         HR_ENABLED    ? "ON" : "OFF",
         SPO2_ENABLED  ? "ON" : "OFF",
         FLOW_ENABLED  ? "ON" : "OFF");
  printf("Model: int8 6-feature autoencoder + clinical rules.\r\n\r\n");
}

/* ====== CALLED REPEATEDLY (like Arduino's loop()) ====== */
void app_process_action(void)
{
  /* 1) Continuously read the drop sensor — NO delay here (drop pulses are very short). */
  sensor_hub_poll();

  uint32_t now = now_ms();

  /* 2) Calibrate the HR baseline during the first 60s after boot OR after a
   *    remote recalibration trigger (only meaningful when HR_ENABLED=1).
   *    While the HR channel isn't connected, sh_hr() returns the baseline
   *    value so the baseline stays at the default. calib_done/calib_start_ms
   *    are file-scope (see above) so app_trigger_hr_recalibration() can
   *    re-arm this window at any time, not just once at boot. */
  static bool hr_baseline_just_completed = false;
  if (!calib_done && (now - calib_start_ms) < HR_CALIB_MS) {
    /* Waiting for the settling window; currently HR is DISABLED so this is safely skipped. */
  } else if (!calib_done) {
    ai_monitor_set_hr_baseline(sh_hr());  // lock in the baseline after 60s
    calib_done = true;
    hr_baseline_just_completed = true;   // one-shot pulse, consumed below in the same tick it fires
    hr_baseline_event_count++;           // persistent, wraps 255->0 harmlessly - see comment above
    printf("[HR] 60s baseline sample complete.\r\n");
  }

  /* 3) Every AI_PERIOD_MS: run the AI + print status. */
  static uint32_t last_ai = 0;
  if (now - last_ai >= AI_PERIOD_MS) {
    last_ai = now;

    ai_result_t r;
    ai_monitor_step(&r);

    /* Only treat HR/SpO2 as REAL numbers when the channel is CH_OK (fresh
     * sample from a real chip). CH_LOST/CH_DISABLED -> do NOT print/send the
     * fake baseline numbers (81/97) anymore, since that would look like a
     * real reading while nothing has actually been read yet - print "--"
     * and send the standard ZCL "invalid" value instead. */
    bool hr_ok   = (sh_hr_state()   == CH_OK);
    bool spo2_ok = (sh_spo2_state() == CH_OK);

    /* Compact print: values + result. Scaled by *1000 / rounded to avoid printf %f. */
    int hr   = (int)(r.feat[0] + 0.5f);
    int spo2 = (int)(r.feat[1] + 0.5f);
    int flow = (int)(r.feat[2] * 100.0f + 0.5f);   // %
    int drop = (int)(r.feat[3] * 100.0f + 0.5f);   // %
    int err  = (int)(r.recon_error * 1000.0f + 0.5f);

    char hr_str[8], spo2_str[8];
    if (hr_ok)   { snprintf(hr_str,   sizeof(hr_str),   "%d", hr);   } else { snprintf(hr_str,   sizeof(hr_str),   "--"); }
    if (spo2_ok) { snprintf(spo2_str, sizeof(spo2_str), "%d", spo2); } else { snprintf(spo2_str, sizeof(spo2_str), "--"); }

    printf("[AI] HR=%s SpO2=%s Flow=%d%% Drop=%d%% (dpm=%d) | err=%d/1000 | ",
           hr_str, spo2_str, flow, drop, (int)(sh_drops_per_min() + 0.5f), err);

    if (r.alarm) {
      printf("*** ALARM ***");
      if (r.reason_missing) printf(" [SIGNAL_LOST]");
      if (r.reason_spo2)    printf(" [LOW_SpO2]");
      if (r.reason_hr)      printf(" [HEART_RATE]");
      if (r.reason_flow)    printf(" [INFUSION_LINE]");
      if (r.reason_ae)      printf(" [AE]");
      printf("\r\n");
    } else {
      printf("Normal\r\n");
    }

    /* Alarm bitmap: bit0=signal lost, bit1=low SpO2, bit2=heart rate,
     * bit3=infusion line, bit4=autoencoder.
     * bit5-8: per-channel CONNECTION status (1 = has signal/CH_OK, 0 = not
     * yet connected or signal lost) - so the gateway/app can tell "sensor
     * not installed yet" apart from "installed but currently alarming".
     * bit9=loadcell tare in progress, bit10=tare just completed (one-shot),
     * bit11=HR 60s baseline just completed (one-shot) - lets the doctor/
     * nurse see these events on the bed dashboard instead of only in the
     * device's own serial log. */
    bool tare_just_done = sh_flow_tare_just_completed();   // one-shot: consumes itself here
    uint16_t alarm_bitmap = (uint16_t)((r.reason_missing ? 0x01 : 0)
                                        | (r.reason_spo2    ? 0x02 : 0)
                                        | (r.reason_hr      ? 0x04 : 0)
                                        | (r.reason_flow    ? 0x08 : 0)
                                        | (r.reason_ae      ? 0x10 : 0)
                                        | (sh_hr_state()    == CH_OK ? 0x20  : 0)
                                        | (sh_spo2_state()  == CH_OK ? 0x40  : 0)
                                        | (sh_flow_state()  == CH_OK ? 0x80  : 0)
                                        | (sh_drops_state() == CH_OK ? 0x100 : 0)
                                        | (sh_flow_tare_in_progress() ? 0x200 : 0)
                                        | (tare_just_done              ? 0x400 : 0)
                                        | (hr_baseline_just_completed  ? 0x800 : 0));
    hr_baseline_just_completed = false;   // one-shot: consumed here, same tick it was set

    int16_t  hr_report   = hr_ok   ? (int16_t)hr    : ZCL_HR_INVALID;
    uint16_t spo2_report = spo2_ok ? (uint16_t)spo2  : ZCL_SPO2_INVALID;
    uint16_t weight_g_report = (uint16_t)(sh_flow_weight_g() < 0.0f ? 0 : sh_flow_weight_g() + 0.5f);
    uint16_t drops_per_min_report = (uint16_t)(sh_drops_per_min() + 0.5f);
    uint16_t target_flow_report = (uint16_t)(sh_target_flow_ml_h() + 0.5f);
    uint16_t target_drops_report = (uint16_t)(sh_target_drops_per_min() + 0.5f);
    uint16_t hr_baseline_bpm_report = (uint16_t)(ai_monitor_get_hr_baseline() + 0.5f);
    zb_report_ai_result(hr_report, spo2_report, (uint16_t)flow,
                        (int16_t)drop, alarm_bitmap,
                        weight_g_report, drops_per_min_report,
                        target_flow_report, target_drops_report,
                        app_hr_baseline_seconds_remaining(), hr_baseline_bpm_report,
                        sh_flow_tare_event_count(), hr_baseline_event_count);
  }
}

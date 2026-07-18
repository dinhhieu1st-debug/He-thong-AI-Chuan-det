/* ============================================================================
 *  app.c — Project AI Smart IV (BRD2709A / EFR32xG26). CHEP DE vao app.c.
 *
 *  Kien truc theo yeu cau:
 *    - Kenh nao DA noi cam bien (xem #define trong sensor_hub.h) -> doc that.
 *    - Kenh CHUA noi -> bao "khong co" (DISABLED), KHONG bao dong nham.
 *    - Sau nay noi them cam bien -> chi can bat #define -> tu dong doc + giam sat.
 *
 *  Vong lap:
 *    - sensor_hub_poll(): goi MOI vong (doc giot lien tuc, khong bo lo xung).
 *    - Moi 1 giay: ai_monitor_step() -> chay autoencoder + luat -> in trang thai.
 *
 *  COMPONENT can Install (.slcp -> SOFTWARE COMPONENTS):
 *    - IO Stream: EUSART (instance "vcom")  + IO Stream: STDIO   -> printf ra VCOM
 *    - Sleep Timer                                               -> thay millis()
 *    - Tensorflow Lite Micro                                     -> chay model AI
 *  (Khi bat them kenh: I2CSPM cho MAX30102, GPIO/uDelay cho HX711... bo sung sau)
 * ========================================================================== */

#include <stdio.h>
#include "sl_sleeptimer.h"
#include "sensor_hub.h"
#include "ai_monitor.h"

#include "app/framework/include/af.h"
#include "network-steering.h"
#include "app/framework/plugin/reporting/reporting.h"

#define AI_PERIOD_MS    1000U   // chu ky chay AI (1 giay)
#define HR_CALIB_MS    60000U   // 60s dau: hieu chuan baseline HR ca nhan

/* Endpoint gui du lieu qua Zigbee (dinh nghia trong config/zcl/zcl_config.zap):
 * moi kenh "muon" 1 cluster do luong chuan de cho MeasuredValue, gateway
 * (zigbee2mqtt) doc lai bang external converter rieng, khong theo dung
 * y nghia goc cua cluster. */
#define ZB_EP_HR     2   // Temperature Measurement -> HR (bpm)
#define ZB_EP_SPO2   3   // Relative Humidity Measurement -> SpO2 (%)
#define ZB_EP_FLOW   4   // Flow Measurement -> Flow% x100
#define ZB_EP_DROP   5   // Pressure Measurement -> Drop% x100
#define ZB_EP_ALARM  6   // Illuminance Measurement -> bitmap canh bao

/* Gia tri "khong co du lieu that" khi gui qua Zigbee - theo dung quy uoc ZCL
 * cho tung kieu attribute (KHONG dung so bpm/% hop ly nhu HR_BASE_FILL nua,
 * tranh gateway/nguoi doc log tuong nham day la so do that):
 *   - Temperature Measurement (int16s)      : 0x8000 = invalid value
 *   - Relative Humidity Measurement (uint16): 0xFFFF = invalid value
 * Ca hai deu la gia tri dac biet chuan ZCL, khong trung voi bpm/% that nao. */
#define ZCL_HR_INVALID     ((int16_t)0x8000)
#define ZCL_SPO2_INVALID   ((uint16_t)0xFFFF)

static bool zb_join_started = false;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

/* Cau hinh bao cao (reporting) mac dinh cho 5 attribute mang du lieu AI,
 * de thiet bi tu dong gui report len coordinator (zigbee2mqtt) ngay ca khi
 * external converter ben Pi chua kip configure reporting rieng. */
static void zb_configure_reporting(void)
{
  struct {
    uint8_t  endpoint;
    uint16_t clusterId;
    uint16_t attributeId;
  } entries[] = {
    { ZB_EP_HR,    ZCL_TEMP_MEASUREMENT_CLUSTER_ID,              ZCL_TEMP_MEASURED_VALUE_ATTRIBUTE_ID },
    { ZB_EP_SPO2,  ZCL_RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER_ID, ZCL_RELATIVE_HUMIDITY_MEASURED_VALUE_ATTRIBUTE_ID },
    { ZB_EP_FLOW,  ZCL_FLOW_MEASUREMENT_CLUSTER_ID,              ZCL_FLOW_MEASURED_VALUE_ATTRIBUTE_ID },
    { ZB_EP_DROP,  ZCL_PRESSURE_MEASUREMENT_CLUSTER_ID,          ZCL_PRESSURE_MEASURED_VALUE_ATTRIBUTE_ID },
    { ZB_EP_ALARM, ZCL_ILLUM_MEASUREMENT_CLUSTER_ID,             ZCL_ILLUM_MEASURED_VALUE_ATTRIBUTE_ID },
  };

  for (uint8_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
    sl_zigbee_af_plugin_reporting_entry_t reportingEntry;
    reportingEntry.direction = SL_ZIGBEE_ZCL_REPORTING_DIRECTION_REPORTED;
    reportingEntry.endpoint = entries[i].endpoint;
    reportingEntry.clusterId = entries[i].clusterId;
    reportingEntry.attributeId = entries[i].attributeId;
    reportingEntry.mask = CLUSTER_MASK_SERVER;
    reportingEntry.manufacturerCode = SL_ZIGBEE_AF_NULL_MANUFACTURER_CODE;
    reportingEntry.data.reported.minInterval = 1;
    reportingEntry.data.reported.maxInterval = 60;
    reportingEntry.data.reported.reportableChange = 1;
    sl_zigbee_af_reporting_configure_reported_attribute(&reportingEntry);
  }
}

/* Ghi 5 gia tri AI vao cac attribute Zigbee tuong ung -> Reporting plugin
 * se tu gui report khi gia tri thay doi (theo cau hinh o tren). */
static void zb_report_ai_result(int16_t hr, uint16_t spo2, uint16_t flow_x100,
                                 int16_t drop_x100, uint16_t alarm_bitmap)
{
  sl_zigbee_af_write_attribute(ZB_EP_HR, ZCL_TEMP_MEASUREMENT_CLUSTER_ID,
                               ZCL_TEMP_MEASURED_VALUE_ATTRIBUTE_ID, CLUSTER_MASK_SERVER,
                               (uint8_t *)&hr, ZCL_INT16S_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_attribute(ZB_EP_SPO2, ZCL_RELATIVE_HUMIDITY_MEASUREMENT_CLUSTER_ID,
                               ZCL_RELATIVE_HUMIDITY_MEASURED_VALUE_ATTRIBUTE_ID, CLUSTER_MASK_SERVER,
                               (uint8_t *)&spo2, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_attribute(ZB_EP_FLOW, ZCL_FLOW_MEASUREMENT_CLUSTER_ID,
                               ZCL_FLOW_MEASURED_VALUE_ATTRIBUTE_ID, CLUSTER_MASK_SERVER,
                               (uint8_t *)&flow_x100, ZCL_INT16U_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_attribute(ZB_EP_DROP, ZCL_PRESSURE_MEASUREMENT_CLUSTER_ID,
                               ZCL_PRESSURE_MEASURED_VALUE_ATTRIBUTE_ID, CLUSTER_MASK_SERVER,
                               (uint8_t *)&drop_x100, ZCL_INT16S_ATTRIBUTE_TYPE);
  sl_zigbee_af_write_attribute(ZB_EP_ALARM, ZCL_ILLUM_MEASUREMENT_CLUSTER_ID,
                               ZCL_ILLUM_MEASURED_VALUE_ATTRIBUTE_ID, CLUSTER_MASK_SERVER,
                               (uint8_t *)&alarm_bitmap, ZCL_INT16U_ATTRIBUTE_TYPE);
}

/** @brief Stack Status
 *
 * Bat dau join mang (Network Steering) khi chua co mang; khi mang len thi
 * cau hinh reporting cho 5 attribute AI.
 */
void sl_zigbee_af_stack_status_cb(sl_status_t status)
{
  if (status == SL_STATUS_NETWORK_DOWN) {
    if (!zb_join_started) {
      zb_join_started = true;
      sl_status_t joinStatus = sl_zigbee_af_network_steering_start();
      printf("[ZB] Bat dau join mang: 0x%02X\r\n", (unsigned)joinStatus);
    }
  } else if (status == SL_STATUS_NETWORK_UP) {
    zb_configure_reporting();
    printf("[ZB] Da vao mang Zigbee, da cau hinh reporting.\r\n");
  }
}

/** @brief Network Steering Complete
 *
 * Cho phep thu join lai neu lan nay khong thanh cong (vi du chua bat
 * permit-join tren coordinator).
 */
void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState)
{
  (void)totalBeacons;
  (void)joinAttempts;
  (void)finalState;

  printf("[ZB] Join mang ket qua: 0x%02X\r\n", (unsigned)status);
  if (status != SL_STATUS_OK) {
    zb_join_started = false;   // cho phep sl_zigbee_af_stack_status_cb thu lai
  }
}

/* ================= GOI 1 LAN LUC KHOI DONG ================= */
void app_init(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);   // tat dem -> printf hien ngay

  sensor_hub_init();
  ai_monitor_init();

  printf("\r\n=== Smart IV - Phan AI san sang ===\r\n");
  printf("Kenh: DROPS=%s HR=%s SpO2=%s FLOW=%s\r\n",
         DROPS_ENABLED ? "BAT" : "TAT",
         HR_ENABLED    ? "BAT" : "TAT",
         SPO2_ENABLED  ? "BAT" : "TAT",
         FLOW_ENABLED  ? "BAT" : "TAT");
  printf("Model: autoencoder int8 6-feat + luat lam sang.\r\n\r\n");
}

/* ====== GOI LAP LAI LIEN TUC (giong loop() Arduino) ====== */
void app_process_action(void)
{
  /* 1) Doc cam bien giot lien tuc — KHONG delay o day (xung giot rat ngan). */
  sensor_hub_poll();

  uint32_t now = now_ms();

  /* 2) Hieu chuan baseline HR trong 60s dau (chi co nghia khi HR_ENABLED=1).
   *    Khi kenh HR chua noi, sh_hr() tra gia tri nen nen baseline ~ mac dinh. */
  static int   calib_done = 0;
  if (!calib_done && now < HR_CALIB_MS) {
    /* Cho cua so on dinh; o ban hien tai HR DISABLED nen bo qua an toan. */
  } else if (!calib_done) {
    ai_monitor_set_hr_baseline(sh_hr());  // chot baseline sau 60s
    calib_done = 1;
  }

  /* 3) Moi AI_PERIOD_MS: chay AI + in trang thai. */
  static uint32_t last_ai = 0;
  if (now - last_ai >= AI_PERIOD_MS) {
    last_ai = now;

    ai_result_t r;
    ai_monitor_step(&r);

    /* Chi coi HR/SpO2 la so THAT khi kenh dang CH_OK (co mau tuoi tu chip
     * that). CH_LOST/CH_DISABLED -> KHONG in/gui so nen gia (81/97) nua, vi
     * de nhin giong so do that trong khi chua doc duoc gi - in "--" va gui
     * gia tri "invalid" chuan ZCL thay vao. */
    bool hr_ok   = (sh_hr_state()   == CH_OK);
    bool spo2_ok = (sh_spo2_state() == CH_OK);

    /* In gon: gia tri + ket qua. Nhan *1000 / lam tron de tranh printf %f. */
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
      printf("*** BAO DONG ***");
      if (r.reason_missing) printf(" [MAT_TIN_HIEU]");
      if (r.reason_spo2)    printf(" [SpO2_THAP]");
      if (r.reason_hr)      printf(" [NHIP_TIM]");
      if (r.reason_flow)    printf(" [DUONG_TRUYEN]");
      if (r.reason_ae)      printf(" [AE]");
      printf("\r\n");
    } else {
      printf("Binh thuong\r\n");
    }

    /* Bitmap canh bao: bit0=mat tin hieu, bit1=SpO2 thap, bit2=nhip tim,
     * bit3=duong truyen, bit4=autoencoder.
     * bit5-8: trang thai KET NOI tung kenh (1 = co tin hieu/CH_OK, 0 = chua
     * noi hoac mat tin hieu) - de gateway/app phan biet duoc "chua lap cam
     * bien" voi "da lap nhung dang bao dong". */
    uint16_t alarm_bitmap = (uint16_t)((r.reason_missing ? 0x01 : 0)
                                        | (r.reason_spo2    ? 0x02 : 0)
                                        | (r.reason_hr      ? 0x04 : 0)
                                        | (r.reason_flow    ? 0x08 : 0)
                                        | (r.reason_ae      ? 0x10 : 0)
                                        | (sh_hr_state()    == CH_OK ? 0x20  : 0)
                                        | (sh_spo2_state()  == CH_OK ? 0x40  : 0)
                                        | (sh_flow_state()  == CH_OK ? 0x80  : 0)
                                        | (sh_drops_state() == CH_OK ? 0x100 : 0));
    int16_t  hr_report   = hr_ok   ? (int16_t)hr    : ZCL_HR_INVALID;
    uint16_t spo2_report = spo2_ok ? (uint16_t)spo2  : ZCL_SPO2_INVALID;
    zb_report_ai_result(hr_report, spo2_report, (uint16_t)flow,
                        (int16_t)drop, alarm_bitmap);
  }
}

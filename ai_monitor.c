/* ============================================================================
 *  ai_monitor.c — Gom dac trung -> autoencoder + luat lam sang -> bao dong
 *  Phan AI cua Smart IV. Doc trang thai tung kenh tu sensor_hub:
 *    - CH_DISABLED: chua noi cam bien -> dien gia tri NEN -> chuan hoa ~0 ->
 *      autoencoder tai tao gan dung -> KHONG bao nham, flag = 0.
 *    - CH_OK      : doc gia tri that.
 *    - CH_LOST    : da noi nhung mat tin hieu -> bat flag missing -> BAO.
 * ========================================================================== */
#include "ai_monitor.h"
#include "sensor_hub.h"
#include <math.h>

/* Glue TFLM viet bang C++ (model_runner.cc), expose ra C */
extern void  ai_model_init(void);
extern float ai_recon_error(const float feat6[6]);  // tra MSE tai tao (da chuan hoa)

/* ===== Tham so chuan hoa (StandardScaler) — KHOP scaler.json =====
 * Chi 4 kenh lien tuc dau duoc chuan hoa; 2 co (flag) giu nguyen 0/1. */
static const float MEAN[4]  = { 81.68079988f, 97.80115772f, 0.99432714f, 0.99472891f };
static const float SCALE[4] = {  6.61951456f,  0.95788093f, 0.06244021f, 0.06359013f };

/* Baseline HR ca nhan (median ~60s dau). Mac dinh = MEAN[0]. */
static float hr_baseline = 81.68079988f;

void ai_monitor_set_hr_baseline(float hr_base)
{
  if (hr_base > 30.0f && hr_base < 200.0f) hr_baseline = hr_base;
}

void ai_monitor_init(void)
{
  ai_model_init();
}

/* Gom 6 dac trung tho tu sensor_hub + dat 2 co missing theo TRANG THAI kenh. */
static void gather_features(float feat[6], uint8_t *vital_missing, uint8_t *line_missing)
{
  ch_state_t hr_s   = sh_hr_state();
  ch_state_t spo2_s = sh_spo2_state();
  ch_state_t flow_s = sh_flow_state();
  ch_state_t drop_s = sh_drops_state();

  /* Gia tri tho: neu kenh DISABLED, sensor_hub da tra gia tri NEN trung tinh.
   * Neu LOST, ta cung lay gia tri nen (de khong day autoencoder lech), va
   * bat co missing ben duoi -> bao dong qua luat, khong qua AE. */
  feat[0] = sh_hr();          // heart_rate
  feat[1] = sh_spo2();        // spo2
  feat[2] = sh_flow_ratio();  // flow_ratio
  feat[3] = sh_drops_ratio(); // drops_ratio

  /* Co MAT TIN HIEU = chi bat khi kenh DA noi nhung mat (CH_LOST).
   * CH_DISABLED (chua noi) -> KHONG bat -> khong bao nham. */
  *vital_missing = (hr_s == CH_LOST || spo2_s == CH_LOST) ? 1 : 0;
  *line_missing  = (flow_s == CH_LOST || drop_s == CH_LOST) ? 1 : 0;

  feat[4] = (float)(*vital_missing);
  feat[5] = (float)(*line_missing);
}

/* Luat lam sang: tra ve cac co ly do. */
static void clinical_rules(const float feat[6], ai_result_t *r)
{
  float hr   = feat[0];
  float spo2 = feat[1];
  float flow = feat[2];
  float drop = feat[3];

  /* HR: lech % so baseline ca nhan HOAC vuot tran/san tuyet doi.
   * Chi xet khi kenh HR thuc su co du lieu (CH_OK). */
  r->reason_hr = 0;
  if (sh_hr_state() == CH_OK) {
    float dev = fabsf(hr - hr_baseline) / (hr_baseline > 1.0f ? hr_baseline : 1.0f);
    if (dev > AI_HR_PCT || hr < AI_HR_ABS_LOW || hr > AI_HR_ABS_HIGH)
      r->reason_hr = 1;
  }

  /* SpO2: nguong TUYET DOI (khong dung %). Chi xet khi CH_OK. */
  r->reason_spo2 = 0;
  if (sh_spo2_state() == CH_OK && spo2 < AI_SPO2_ABS)
    r->reason_spo2 = 1;

  /* Duong truyen: ratio ngoai [LO, HI]. Chi xet kenh dang chay (CH_OK). */
  r->reason_flow = 0;
  if (sh_flow_state() == CH_OK && (flow > AI_FLOW_HI || flow < AI_FLOW_LO))
    r->reason_flow = 1;
  if (sh_drops_state() == CH_OK && (drop > AI_FLOW_HI || drop < AI_FLOW_LO))
    r->reason_flow = 1;

  /* Mat tin hieu (da noi nhung mat) */
  r->reason_missing = (feat[4] > 0.5f || feat[5] > 0.5f) ? 1 : 0;
}

void ai_monitor_step(ai_result_t *out)
{
  uint8_t vm = 0, lm = 0;
  float feat[6];
  gather_features(feat, &vm, &lm);

  /* Chuan hoa 4 kenh lien tuc cho autoencoder; 2 co giu nguyen 0/1. */
  float norm[6];
  for (int i = 0; i < 4; i++)
    norm[i] = (feat[i] - MEAN[i]) / SCALE[i];
  norm[4] = feat[4];
  norm[5] = feat[5];

  /* Autoencoder: sai so tai tao (model_runner lo quantize/dequantize). */
  float err = ai_recon_error(norm);

  /* Luat lam sang */
  ai_result_t r;
  for (int i = 0; i < 6; i++) r.feat[i] = feat[i];
  r.recon_error = err;
  r.reason_ae = (err > AI_AE_THRESHOLD) ? 1 : 0;
  clinical_rules(feat, &r);

  /* Bao dong = OR cac ly do (uu tien recall) */
  r.alarm = (r.reason_ae || r.reason_hr || r.reason_spo2 ||
             r.reason_flow || r.reason_missing) ? 1 : 0;

  *out = r;
}

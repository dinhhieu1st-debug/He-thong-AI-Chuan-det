/* ============================================================================
 *  ai_monitor.h — Lop AI phat hien bat thuong cho Smart IV (Doi ICTU)
 *  Y tuong tong the:
 *    1) sensor_hub cung cap gia tri + TRANG THAI tung kenh (DISABLED/OK/LOST)
 *    2) ai_monitor gom 6 dac trung -> chuan hoa -> chay autoencoder (TFLM)
 *    3) Ket hop autoencoder VOI luat lam sang (OR): bao dong neu bat ky dieu
 *       kien nao dung -> giam bo sot (recall cao).
 *
 *  6 dac trung (DUNG thu tu nay, khop scaler.json / model int8):
 *    [0] heart_rate   (nhip tim, bpm)
 *    [1] spo2         (% bao hoa oxy)
 *    [2] flow_ratio   (luu luong thuc / muc dat)
 *    [3] drops_ratio  (giot/phut thuc / muc dat)
 *    [4] vital_missing (co: mat tin hieu HR hoac SpO2 sau khi DA gan may)
 *    [5] line_missing  (co: mat tin hieu duong truyen FLOW hoac DROPS)
 * ========================================================================== */
#ifndef AI_MONITOR_H
#define AI_MONITOR_H

#include <stdint.h>

/* ===== Tham so luat lam sang (lay tu threshold.json sau khi train) ===== */
#define AI_HR_PCT       0.30f   // HR lech > 30% so baseline ca nhan -> nghi ngo
#define AI_HR_ABS_LOW   45.0f   // chan duoi tuyet doi (bradycardia nang)
#define AI_HR_ABS_HIGH  150.0f  // chan tren tuyet doi (tachycardia nang)
#define AI_SPO2_ABS     90.0f   // SpO2 < 90% -> tut oxy (nguong tuyet doi)
#define AI_FLOW_HI      1.5f    // > 1.5x muc dat -> chay tu do (free-flow)
#define AI_FLOW_LO      0.3f    // < 0.3x muc dat -> tac (occlusion)

/* Nguong sai so tai tao cua autoencoder (tu threshold.json) */
#define AI_AE_THRESHOLD 1.4335810089111334f

/* Ket qua mot lan danh gia */
typedef struct {
  int    alarm;          // 1 = co bao dong, 0 = binh thuong
  float  recon_error;    // sai so tai tao cua autoencoder (de debug)
  uint8_t reason_ae;     // autoencoder vuot nguong
  uint8_t reason_hr;     // HR bat thuong (% hoac tuyet doi)
  uint8_t reason_spo2;   // SpO2 < nguong
  uint8_t reason_flow;   // flow/drops ratio ngoai khoang
  uint8_t reason_missing;// mat tin hieu (vital hoac line)
  float   feat[6];       // 6 dac trung tho (sau khi gom, truoc chuan hoa)
} ai_result_t;

/* Khoi tao: nap model TFLM + dat baseline HR (median ~60s dau) */
void ai_monitor_init(void);

/* Goi dinh ky (vi du moi 1 giay): gom dac trung, chay AI, tra ket qua */
void ai_monitor_step(ai_result_t *out);

/* Hieu chuan baseline HR ca nhan: goi trong ~60s dau khi gan may.
 * Neu khong goi, baseline mac dinh = scaler.mean[0] (81.68). */
void ai_monitor_set_hr_baseline(float hr_base);

#endif /* AI_MONITOR_H */

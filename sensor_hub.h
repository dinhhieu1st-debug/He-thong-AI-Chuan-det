/* ============================================================================
 *  sensor_hub.h — Quan ly cac kenh cam bien cho phan AI (Smart IV, Doi ICTU)
 *  Y tuong: moi kenh co 3 TRANG THAI
 *    CH_DISABLED : chua noi cam bien  -> bo qua, KHONG bao nham
 *    CH_OK       : co du lieu tuoi    -> doc that
 *    CH_LOST     : da noi nhung mat tin hieu -> set co missing -> BAO
 *  Bat/tat tung kenh bang #define ben duoi. Hien chi bat DROPS (cam bien giot).
 * ========================================================================== */
#ifndef SENSOR_HUB_H
#define SENSOR_HUB_H

#include <stdint.h>

/* ===== CONG TAC: 0 = chua noi (bo qua) | 1 = da noi (doc that) =====
 * Chi bat dung kenh THAT SU dang cam vao board luc nay. Hien tai (theo
 * xac nhan cua nguoi dung) chi co MAX30102 (HR+SpO2) da noi; loadcell
 * (HX711) va cam bien giot CHUA cam lai - de DISABLED, tranh doc nhieu
 * tren chan floating. Khi cam lai tung cam bien, bat #define tuong ung
 * len 1. */
#define HR_ENABLED     1   // MAX30102 - nhip tim (I2C, chung bus voi SPO2) - DA CAM
#define SPO2_ENABLED   1   // MAX30102 - SpO2 (cung 1 chip voi HR) - DA CAM
#define FLOW_ENABLED   1   // HX711 + loadcell - toc do truyen dich - DA CAM, dang test
#define DROPS_ENABLED  0   // cam bien giot - CHUA CAM (rut ra de test HR/SpO2 rieng)

/* ===== MUC BAC SI DAT (doctor-set) — chinh theo don ke ===== */
#define SET_FLOW_ML_H   100.0f   // toc do truyen dat (ml/gio)
#define SET_DROPS_DPM   20.0f    // so giot/phut dat

/* ===== Hieu chuan loadcell (HX711) — CHINH LAI cho dung can that =====
 * Cach hieu chuan: treo 1 vat nang biet truoc, so sanh gia tri tho doc duoc
 * voi luc chua treo gi, roi tinh factor = delta_raw / khoi_luong_KG (khop
 * cong thuc weight_g = delta_raw / FACTOR * 1000 dang dung o sensor_hub.c).
 * Da hieu chuan thuc te ngay 2026-07-17 (lan 2, do CHINH XAC trong CUNG 1
 * phien tare+treo, tranh sai so troi giua cac lan nap lai): treo bich dich
 * ~500ml (~500g nuoc) dung dung vach do tren bich, tare_offset=-39289,
 * delta_raw sau khi on dinh (EMA settle) ~101267 -> factor = 101267/0.5kg
 * = 202534. Cac gia tri cu (14000 tu hx711_test.ino, 269334 tu lan do dau
 * bi sai vi lay tare/tai o 2 phien nap khac nhau) deu KHONG dung nua. */
#define HX711_CALIBRATION_FACTOR   202534.0f

/* ===== Thoi gian coi la "mat tin hieu" neu kenh da bat ma khong co mau (ms) ===== */
#define VITAL_TIMEOUT_MS  3000U

typedef enum { CH_DISABLED = 0, CH_OK = 1, CH_LOST = 2 } ch_state_t;

void sensor_hub_init(void);
void sensor_hub_poll(void);   // goi MOI VONG loop (doc giot lien tuc)

/* Gia tri tho hien tai cua tung kenh (chi co nghia khi trang thai = CH_OK) */
float      sh_hr(void);
float      sh_spo2(void);
float      sh_flow_ratio(void);    // flow / SET_FLOW
float      sh_drops_ratio(void);   // drops_per_min / SET_DROPS
float      sh_drops_per_min(void);

/* Trang thai tung kenh */
ch_state_t sh_hr_state(void);
ch_state_t sh_spo2_state(void);
ch_state_t sh_flow_state(void);
ch_state_t sh_drops_state(void);

uint32_t   sh_total_drops(void);

#endif /* SENSOR_HUB_H */

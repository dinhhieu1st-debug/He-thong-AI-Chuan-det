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

/* ===== CONG TAC: 0 = chua noi (bo qua) | 1 = da noi (doc that) ===== */
#define HR_ENABLED     0   // MAX30102 - nhip tim
#define SPO2_ENABLED   0   // MAX30102 - SpO2
#define FLOW_ENABLED   0   // loadcell / cam bien luu luong
#define DROPS_ENABLED  1   // cam bien giot (DA TEST DUOC)

/* ===== MUC BAC SI DAT (doctor-set) — chinh theo don ke ===== */
#define SET_FLOW_ML_H   100.0f   // toc do truyen dat (ml/gio)
#define SET_DROPS_DPM   20.0f    // so giot/phut dat

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

/* ============================================================================
 *  sensor_hub.c — Doc cam bien + quan ly trang thai tung kenh
 *  Board: BRD2709A (EFR32xG26 Explorer Kit). Cam bien giot D0 -> PD02.
 *  Hien chi bat DROPS; cac kenh khac DISABLED (chua noi) -> tra gia tri NEN
 *  trung tinh de model khong bao nham, va trang thai = CH_DISABLED.
 * ========================================================================== */
#include "sensor_hub.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_sleeptimer.h"

/* ---- Chan cam bien giot (BRD2709A, mikroBUS "AN" = PD02) ---- */
#define SENSOR_PORT   gpioPortD
#define SENSOR_PIN    2
#define DEBOUNCE_MS   200U

/* Gia tri NEN (= scaler.mean) cho kenh chua noi -> chuan hoa ~0 -> AE de tai tao */
#define HR_BASE_FILL    81.68f
#define SPO2_BASE_FILL  97.80f

static uint32_t total_drops  = 0;
static uint32_t last_drop_ms = 0;
static uint32_t last_interval= 0;
static int      last_state   = 1;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

void sensor_hub_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
#if DROPS_ENABLED
  GPIO_PinModeSet(SENSOR_PORT, SENSOR_PIN, gpioModeInput, 0);
  last_state = GPIO_PinInGet(SENSOR_PORT, SENSOR_PIN);
#endif
  /* TODO khi noi them: khoi tao MAX30102 (I2C) cho HR/SpO2, loadcell cho FLOW */
}

void sensor_hub_poll(void)
{
#if DROPS_ENABLED
  int      cur = GPIO_PinInGet(SENSOR_PORT, SENSOR_PIN);
  uint32_t now = now_ms();
  if (cur != last_state) {
    if (now - last_drop_ms > DEBOUNCE_MS) {
      total_drops++;
      last_interval = (total_drops > 1) ? (now - last_drop_ms) : 0;
      last_drop_ms  = now;
    }
    last_state = cur;
  }
#endif
  /* TODO: khi co MAX30102/loadcell, cap nhat mau + dau thoi gian o day */
}

/* ---------------- HR ---------------- */
float sh_hr(void)
{
#if HR_ENABLED
  return 0.0f;   // TODO: tra HR that tu MAX30102
#else
  return HR_BASE_FILL;        // chua noi -> gia tri nen (chuan hoa ~0)
#endif
}
ch_state_t sh_hr_state(void)
{
#if HR_ENABLED
  return CH_OK;   // TODO: tra CH_LOST neu qua VITAL_TIMEOUT_MS khong co mau
#else
  return CH_DISABLED;
#endif
}

/* ---------------- SpO2 ---------------- */
float sh_spo2(void)
{
#if SPO2_ENABLED
  return 0.0f;   // TODO: tra SpO2 that
#else
  return SPO2_BASE_FILL;
#endif
}
ch_state_t sh_spo2_state(void)
{
#if SPO2_ENABLED
  return CH_OK;
#else
  return CH_DISABLED;
#endif
}

/* ---------------- FLOW (loadcell) ---------------- */
float sh_flow_ratio(void)
{
#if FLOW_ENABLED
  return 1.0f;   // TODO: flow_that / SET_FLOW_ML_H
#else
  return 1.0f;               // chua noi -> coi nhu dung muc dat (ratio 1.0)
#endif
}
ch_state_t sh_flow_state(void)
{
#if FLOW_ENABLED
  return CH_OK;
#else
  return CH_DISABLED;
#endif
}

/* ---------------- DROPS (cam bien giot) ---------------- */
float sh_drops_per_min(void)
{
#if DROPS_ENABLED
  uint32_t now = now_ms();
  uint32_t gap = now - last_drop_ms;
  uint32_t eff = (last_interval > gap) ? last_interval : gap;  // lau chua co giot -> rate giam
  if (eff == 0) return 0.0f;
  return 60000.0f / (float)eff;
#else
  return SET_DROPS_DPM;
#endif
}
float sh_drops_ratio(void)
{
  return sh_drops_per_min() / SET_DROPS_DPM;
}
ch_state_t sh_drops_state(void)
{
#if DROPS_ENABLED
  return CH_OK;   // cam bien dang chay; "khong co giot" se thanh occlusion qua luat ratio
#else
  return CH_DISABLED;
#endif
}

uint32_t sh_total_drops(void) { return total_drops; }

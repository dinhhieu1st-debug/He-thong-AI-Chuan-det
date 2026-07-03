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

#define AI_PERIOD_MS    1000U   // chu ky chay AI (1 giay)
#define HR_CALIB_MS    60000U   // 60s dau: hieu chuan baseline HR ca nhan

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

/* ================= GOI 1 LAN LUC KHOI DONG ================= */
void app_init(void)
{
  setvbuf(stdout, NULL, _IONBF, 0);   // tat dem -> printf hien ngay

  sensor_hub_init();
  ai_monitor_init();

  printf("\r\n=== Smart IV - Phan AI san sang ===\r\n");
  printf("Kenh dang BAT: DROPS (cam bien giot). HR/SpO2/FLOW: chua noi (DISABLED).\r\n");
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

    /* In gon: gia tri + ket qua. Nhan *1000 / lam tron de tranh printf %f. */
    int hr   = (int)(r.feat[0] + 0.5f);
    int spo2 = (int)(r.feat[1] + 0.5f);
    int flow = (int)(r.feat[2] * 100.0f + 0.5f);   // %
    int drop = (int)(r.feat[3] * 100.0f + 0.5f);   // %
    int err  = (int)(r.recon_error * 1000.0f + 0.5f);

    printf("[AI] HR=%d SpO2=%d Flow=%d%% Drop=%d%% (dpm=%d) | err=%d/1000 | ",
           hr, spo2, flow, drop, (int)(sh_drops_per_min() + 0.5f), err);

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
  }
}

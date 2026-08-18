/* ============================================================================
 *  fusion_test.c — host-side test for ai_fusion.c and line_rules.c
 *
 *  Runs the real decision logic against stubbed sensors and stubbed models, on
 *  a PC, so the 4-level matrix and the K=11 persistence filter can be checked
 *  without flashing a board. sensor_hub.h deliberately depends on nothing but
 *  stdint/stdbool, which is what makes this possible.
 *
 *  What is real here: ai_fusion.c and line_rules.c, compiled unmodified.
 *  What is stubbed: the sensors, and the three interpreters.
 *
 *  The drip/vitals model stubs predict "the same value again", so a steady
 *  signal produces zero residual and a ramp produces a residual equal to the
 *  ramp step. That makes an anomaly something the test can dial in precisely,
 *  which is what these cases need to check - the persistence counter, not the
 *  network.
 *
 *  Build and run:  cc -I firmware -o /tmp/fusion_test tools/fusion_test.c \
 *                     firmware/ai_fusion.c firmware/line_rules.c -lm && /tmp/fusion_test
 * ========================================================================== */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ai_engine.h"
#include "ai_fusion.h"
#include "sensor_hub.h"

/* ---------------------------------------------------------------- sensors -- */
static float      g_hr = 80.0f, g_spo2 = 98.0f;
static float      g_drops_ratio = 1.0f, g_dpm = 60.0f;
static float      g_weight = 500.0f;
static ch_state_t g_hr_st = CH_OK, g_spo2_st = CH_OK;
static ch_state_t g_drops_st = CH_OK, g_flow_st = CH_OK;

float      sh_hr(void)              { return g_hr; }
float      sh_spo2(void)            { return g_spo2; }
float      sh_drops_ratio(void)     { return g_drops_ratio; }
float      sh_drops_per_min(void)   { return g_dpm; }
float      sh_flow_weight_g(void)   { return g_weight; }
float      sh_flow_ratio(void)      { return g_drops_ratio; }
ch_state_t sh_hr_state(void)        { return g_hr_st; }
ch_state_t sh_spo2_state(void)      { return g_spo2_st; }
ch_state_t sh_drops_state(void)     { return g_drops_st; }
ch_state_t sh_flow_state(void)      { return g_flow_st; }
float      sh_target_drops_per_min(void) { return 60.0f; }

/* ----------------------------------------------------------------- models -- */
static bool g_models_ready = true;

bool ai_engine_init(void)         { return g_models_ready; }
bool ai_engine_drip_ready(void)   { return g_models_ready; }
bool ai_engine_vitals_ready(void) { return g_models_ready; }
bool ai_engine_ae_ready(void)     { return g_models_ready; }
unsigned ai_engine_drip_arena_used(void)   { return 0; }
unsigned ai_engine_vitals_arena_used(void) { return 0; }
unsigned ai_engine_ae_arena_used(void)     { return 0; }

bool ai_engine_run_drip(const float *win, float *out)
{
  for (int i = 0; i < AI_HORIZON; i++) out[i] = win[AI_WINDOW - 1];
  return g_models_ready;
}

bool ai_engine_run_vitals(const float *win, float *out)
{
  for (int i = 0; i < AI_HORIZON; i++) {
    out[i * 2 + 0] = win[(AI_WINDOW - 1) * 2 + 0];
    out[i * 2 + 1] = win[(AI_WINDOW - 1) * 2 + 1];
  }
  return g_models_ready;
}

/* Reproduces the real autoencoder's behaviour closely enough for these tests:
 * error grows with distance from a normal operating point, and the two channels
 * contribute additively - which is what the measured probe showed the trained
 * model does. */
bool ai_engine_run_ae(const float *in, float *err)
{
  *err = 0.6f * in[0] * in[0] + 1.2f * in[1] * in[1];
  return g_models_ready;
}

/* ------------------------------------------------------------------- test -- */
static int g_failures = 0;

static void check(const char *what, bool ok, const char *detail)
{
  printf("  [%s] %-52s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "");
  if (!ok) g_failures++;
}

static const char *lvl(alert_level_t l)
{
  switch (l) {
    case ALERT_LEVEL_CRITICAL:     return "CRITICAL";
    case ALERT_LEVEL_VITALS_ALERT: return "VITALS_ALERT";
    case ALERT_LEVEL_LINE_WARNING: return "LINE_WARNING";
    default:                       return "NORMAL";
  }
}

static void reset_all(void)
{
  g_hr = 80.0f; g_spo2 = 98.0f;
  g_drops_ratio = 1.0f; g_dpm = 60.0f; g_weight = 500.0f;
  g_hr_st = g_spo2_st = g_drops_st = g_flow_st = CH_OK;
  g_models_ready = true;
  ai_fusion_init();
  ai_fusion_set_hr_baseline(80.0f);
}

/* Runs `n` steady seconds so the ring buffers and the weight trend fill up. */
static fusion_result_t settle(int n)
{
  fusion_result_t r;
  for (int i = 0; i < n; i++) {
    /* 60 dpm through a 20 gtt/mL set = 3 mL/min = 0.05 g/s. Real numbers
     * matter here: an earlier absolute "steady" threshold happened to equal
     * exactly this rate, and reported a healthy infusion as a sensor fault. */
    g_weight -= 0.05f;
    ai_fusion_step(&r);
  }
  return r;
}

int main(void)
{
  fusion_result_t r;
  char msg[160];

  printf("\n== 1. Steady normal infusion ==\n");
  reset_all();
  r = settle(90);
  snprintf(msg, sizeof msg, "level=%s headline=\"%s\"", lvl(r.level), r.headline);
  check("stays NORMAL", r.level == ALERT_LEVEL_NORMAL, msg);

  printf("\n== 2. A 5-second drip blip must NOT alarm ==\n");
  reset_all();
  settle(90);
  for (int i = 0; i < 5; i++) {          /* ramp = sustained residual */
    g_drops_ratio -= 0.05f;
    ai_fusion_step(&r);
  }
  snprintf(msg, sizeof msg, "persist=%u/%d level=%s",
           r.drip_persist, AI_PERSIST_K, lvl(r.level));
  check("5 s of anomaly stays quiet", r.level == ALERT_LEVEL_NORMAL, msg);

  printf("\n== 3. A sustained drip anomaly DOES alarm, at exactly K=11 ==\n");
  reset_all();
  settle(90);
  int fired_at = -1;
  for (int i = 1; i <= 15; i++) {
    g_drops_ratio -= 0.05f;
    ai_fusion_step(&r);
    if (fired_at < 0 && r.drip_anomaly) fired_at = i;
  }
  snprintf(msg, sizeof msg, "confirmed on second %d", fired_at);
  check("fires on the 11th consecutive second", fired_at == AI_PERSIST_K, msg);
  snprintf(msg, sizeof msg, "level=%s headline=\"%s\"", lvl(r.level), r.headline);
  check("and the level is LINE_WARNING, not a patient alarm",
        r.level == ALERT_LEVEL_LINE_WARNING, msg);

  printf("\n== 4. SpO2 below 90%% alarms IMMEDIATELY - no persistence ==\n");
  reset_all();
  settle(90);
  g_spo2 = 88.0f;
  ai_fusion_step(&r);
  snprintf(msg, sizeof msg, "level=%s on the very first tick, rule_spo2=%d",
           lvl(r.level), r.rule_spo2);
  check("first tick raises VITALS_ALERT",
        r.level == ALERT_LEVEL_VITALS_ALERT && r.rule_spo2, msg);

  printf("\n== 5. Line problem AND patient problem together -> CRITICAL ==\n");
  /* The line fault has to be a SUSTAINED one, not a ramp. A forecast residual
   * fires on the transition and then goes quiet once the fault settles - that
   * is inherent to a level-invariant forecaster. What holds the line branch up
   * afterwards is the load-cell rule, which is the realistic case anyway: a
   * blocked line stays blocked. */
  reset_all();
  settle(90);
  for (int i = 0; i < 70; i++) {
    g_drops_ratio = 0.2f;  g_dpm = 12.0f;   /* slowed, and weight not moving */
    ai_fusion_step(&r);
  }
  g_spo2 = 87.0f;
  ai_fusion_step(&r);
  snprintf(msg, sizeof msg, "line=%d patient=%d level=%s headline=\"%s\"",
           r.line_branch, r.patient_branch, lvl(r.level), r.headline);
  check("both branches -> CRITICAL", r.level == ALERT_LEVEL_CRITICAL, msg);

  printf("\n== 6. THE CASE THE LOAD CELL EXISTS FOR ==\n");
  printf("   Same slowing drops, two different physical causes.\n");

  /* Both cases run for longer than LINE_TREND_WINDOW_S, so the 60 s weight
   * window contains only the scenario's own samples. Running them shorter left
   * the settle() phase's readings in the window and the trend was measuring the
   * wrong thing - which is itself the reason the window has to fill before
   * line_result_t.valid goes true. */

  /* 6a: a bag genuinely near its end - drops slow because the fluid column is
   * short, and the weight falls in proportion. Normal, not a fault. */
  reset_all();
  settle(90);
  g_weight = 120.0f;
  for (int i = 0; i < 70; i++) {
    g_drops_ratio = 0.25f;            /* below AI_FLOW_LO */
    g_dpm = 15.0f;
    g_weight -= 0.0125f;              /* 15 dpm really is leaving the bag */
    ai_fusion_step(&r);
  }
  snprintf(msg, sizeof msg, "line_state=%s level=%s",
           line_state_text(r.line.state), lvl(r.level));
  check("emptying bag reads as RUNNING LOW, not an occlusion",
        r.line.state == LINE_RUNNING_LOW, msg);

  /* 6b: occlusion - identical drop rate, weight NOT moving. */
  reset_all();
  settle(90);
  g_weight = 120.0f;
  for (int i = 0; i < 70; i++) {
    g_drops_ratio = 0.25f;            /* the same slow drops as 6a */
    g_dpm = 15.0f;
    /* weight held still - fluid is not leaving the bag */
    ai_fusion_step(&r);
  }
  snprintf(msg, sizeof msg, "line_state=%s level=%s headline=\"%s\"",
           line_state_text(r.line.state), lvl(r.level), r.headline);
  check("steady weight with the same slow drops IS an occlusion",
        r.line.state == LINE_OCCLUSION
        && r.level == ALERT_LEVEL_LINE_WARNING, msg);

  printf("\n== 7. Drops counted while the bag never gets lighter ==\n");
  reset_all();
  settle(90);
  for (int i = 0; i < 70; i++) {
    g_drops_ratio = 1.0f;             /* looks perfectly normal */
    g_dpm = 60.0f;
    /* but the weight does not move at all */
    ai_fusion_step(&r);
  }
  snprintf(msg, sizeof msg, "line_state=%s headline=\"%s\"",
           line_state_text(r.line.state), r.headline);
  check("reported as a SENSOR fault, not a patient alarm",
        r.line.state == LINE_SENSOR_MISMATCH
        && r.level == ALERT_LEVEL_LINE_WARNING, msg);

  printf("\n== 8. No model loads at all - the device must still alarm ==\n");
  reset_all();
  g_models_ready = false;
  settle(90);
  g_spo2 = 85.0f;
  ai_fusion_step(&r);
  snprintf(msg, sizeof msg, "level=%s with every model unavailable", lvl(r.level));
  check("clinical rules carry the device on their own",
        r.level == ALERT_LEVEL_VITALS_ALERT, msg);

  printf("\n== 9. A sensor that was working goes dead ==\n");
  reset_all();
  settle(90);
  g_spo2_st = CH_LOST;
  ai_fusion_step(&r);
  snprintf(msg, sizeof msg, "level=%s headline=\"%s\"", lvl(r.level), r.headline);
  check("lost signal is neither ignored nor called critical",
        r.level == ALERT_LEVEL_LINE_WARNING && r.rule_missing, msg);

  printf("\n== 10. Vitals autoencoder catches a sub-threshold combination ==\n");
  reset_all();
  settle(90);
  g_hr = 104.0f;      /* inside 45..150, and only +30% of an 80 bpm baseline */
  g_spo2 = 91.5f;     /* above the 90% hard limit */
  for (int i = 0; i < 12; i++) ai_fusion_step(&r);
  snprintf(msg, sizeof msg,
           "ae_err=%.2f (threshold %.2f) rule_spo2=%d ae_anomaly=%d level=%s",
           r.ae_error, AI_AE_THRESHOLD, r.rule_spo2, r.ae_anomaly, lvl(r.level));
  check("no hard rule fires, but the AE does", !r.rule_spo2 && r.ae_anomaly, msg);

  printf("\n%s  (%d failure%s)\n\n",
         g_failures ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED",
         g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? 1 : 0;
}

/* ============================================================================
 *  drop_filter_test.c — host-side test for firmware/drop_filter.c
 *
 *  Replays beam patterns against the REAL detector, on a PC, at 1 ms per step -
 *  so noise that would take a bench rig and a lot of patience to reproduce is
 *  just a few lines here.
 *
 *  Build and run:
 *      cc -I firmware -o /tmp/drop_filter_test tools/drop_filter_test.c \
 *         firmware/drop_filter.c -lm && /tmp/drop_filter_test
 * ========================================================================== */

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "drop_filter.h"

#define IDLE   1       /* beam clear reads HIGH on the bench rig */
#define BROKEN 0

static int failures = 0;
static uint32_t clock_ms = 0;

static void check(const char *what, bool ok, const char *detail)
{
  printf("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", what, detail);
  if (!ok) failures++;
}

/* Holds one level for a number of milliseconds, one sample per ms - the same
 * rate the main loop polls at, near enough. */
/* Steps in 100 us increments, so pulses shorter than a millisecond - the ones
 * the real sensor produces, and the ones a spike-rejection threshold has to
 * discriminate - can be expressed at all. */
static uint32_t clock_us = 0;

static void hold_us(drop_filter_t *f, int level, uint32_t us)
{
  for (uint32_t i = 0; i < us; i += 100) {
    drop_filter_step(f, level, clock_us / 1000U, clock_us);
    clock_us += 100;
  }
  clock_ms = clock_us / 1000U;
}

static void hold(drop_filter_t *f, int level, uint32_t ms)
{
  hold_us(f, level, ms * 1000U);
}

/* One drop: beam broken for shadow_ms, then clear for the rest of gap_ms. */
static void drop(drop_filter_t *f, uint32_t shadow_ms, uint32_t gap_ms)
{
  hold(f, BROKEN, shadow_ms);
  hold(f, IDLE, gap_ms > shadow_ms ? gap_ms - shadow_ms : 1);
}

static void begin(drop_filter_t *f)
{
  drop_filter_init(f, IDLE);
  clock_ms = 0;
  clock_us = 0;
}

int main(void)
{
  drop_filter_t f;
  char msg[160];

  printf("\n== Đếm giọt ==\n");

  /* --- a steady infusion is counted exactly once per drop ---------------- */
  begin(&f);
  for (int i = 0; i < 20; i++) drop(&f, 30, 1000);   /* 60 dpm */
  snprintf(msg, sizeof msg, "%u giọt", f.total_drops);
  check("60 dpm đều đặn -> đếm đúng 20 giọt", f.total_drops == 20, msg);

  float dpm = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%.1f dpm", dpm);
  check("...và ra đúng ~60 dpm", fabsf(dpm - 60.0f) < 2.0f, msg);

  /* This is the old bug, in one case: the shadow outlasts the old 200 ms dead
   * time, so the beam-restored edge was counted as a second drop. */
  begin(&f);
  for (int i = 0; i < 10; i++) drop(&f, 250, 1000);
  snprintf(msg, sizeof msg, "%u giọt (bản cũ đếm 20)", f.total_drops);
  check("giọt có bóng dài 250 ms vẫn chỉ là MỘT giọt", f.total_drops == 10, msg);

  /* --- THE REAL SENSOR ---------------------------------------------------
   *
   * Taken from 95 pulses measured on the bench, not invented. Each drop shows
   * up as TWO short pulses 16 ms apart - the droplet necks, then detaches -
   * followed by ~578 ms of clear beam. This is the case the first version of
   * this filter failed completely: with a 12 ms confirm, a 3 ms pulse never
   * qualifies, and the device counted zero drops while dripping steadily. */
  begin(&f);
  hold(&f, IDLE, 50);
  for (int i = 0; i < 15; i++) {
    hold(&f, BROKEN, 3);           /* droplet necks   */
    hold(&f, IDLE, 16);
    hold(&f, BROKEN, 3);           /* droplet detaches */
    hold(&f, IDLE, 578);
  }
  snprintf(msg, sizeof msg, "%u giọt (bản 12 ms đếm 0)", f.total_drops);
  check("dạng sóng THẬT: mỗi giọt hai xung 3 ms cách 16 ms -> 15 giọt",
        f.total_drops == 15, msg);

  dpm = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%.1f dpm (chu kỳ 600 ms)", dpm);
  check("...và ra đúng ~100 dpm", fabsf(dpm - 100.0f) < 5.0f, msg);

  printf("\n== Chống nhiễu ==\n");

  /* --- short spikes are not drops --------------------------------------- */
  begin(&f);
  for (int i = 0; i < 50; i++) { hold_us(&f, BROKEN, 200); hold(&f, IDLE, 18); }
  snprintf(msg, sizeof msg, "%u giọt, %u xung bị loại", f.total_drops, f.rejected_spikes);
  check("50 xung nhiễu 200 us -> không giọt nào", f.total_drops == 0, msg);

  /* --- noise between real drops must not corrupt the rate ----------------
   *
   * The spikes here are 200 us, which is what electrical noise on this rig
   * actually looks like. They were 3 ms in an earlier version of this test,
   * written before anyone had measured the sensor - and 3 ms turned out to be
   * the width of a REAL drop pulse. Width cannot separate those, and no
   * threshold could: a 3 ms interference pulse on this hardware is a drop as
   * far as any detector can tell. MIN_GAP_MS is what stands behind this, and
   * the two cases below test it. */
  begin(&f);
  for (int i = 0; i < 10; i++) {
    hold(&f, BROKEN, 30);          /* real drop */
    hold(&f, IDLE, 200);
    hold_us(&f, BROKEN, 200);      /* spike */
    hold(&f, IDLE, 200);
    hold_us(&f, BROKEN, 200);      /* spike */
    hold(&f, IDLE, 564);
  }
  dpm = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%u giọt, %.1f dpm", f.total_drops, dpm);
  check("nhiễu xen giữa các giọt: vẫn 10 giọt, ~60 dpm",
        f.total_drops == 10 && fabsf(dpm - 60.0f) < 5.0f, msg);

  /* --- a drop that breaks into two shadows stays one drop ---------------- */
  begin(&f);
  for (int i = 0; i < 10; i++) {
    hold(&f, BROKEN, 20);
    hold(&f, IDLE, 8);             /* shorter than RELEASE -> same drop */
    hold(&f, BROKEN, 20);
    hold(&f, IDLE, 952);
  }
  snprintf(msg, sizeof msg, "%u giọt", f.total_drops);
  check("giọt vỡ làm hai bóng -> vẫn một giọt", f.total_drops == 10, msg);

  /* --- two pulses closer than MIN_GAP: the second is a double count ------
   * Written out by hand rather than with drop(), because the separation being
   * tested is between the two pulse STARTS - which is exactly the quantity the
   * MIN_GAP rule looks at. */
  begin(&f);
  hold(&f, IDLE, 50);
  hold(&f, BROKEN, 30); hold(&f, IDLE, 70);    /* pulse at t=50           */
  hold(&f, BROKEN, 30); hold(&f, IDLE, 1000);  /* pulse at t=150, +100 ms */
  snprintf(msg, sizeof msg, "%u giọt, %u lần bị loại vì quá gần",
           f.total_drops, f.rejected_close);
  check("hai xung cách nhau 100 ms -> chỉ tính một",
        f.total_drops == 1 && f.rejected_close == 1, msg);

  /* The subtle half of that rule: a rejected pulse must NOT reset the clock,
   * or the interval being measured gets halved and the rate reads double. */
  begin(&f);
  hold(&f, IDLE, 50);
  hold(&f, BROKEN, 30); hold(&f, IDLE, 70);    /* drop 1, t=50            */
  hold(&f, BROKEN, 30); hold(&f, IDLE, 870);   /* rejected, t=150         */
  hold(&f, BROKEN, 30); hold(&f, IDLE, 100);   /* drop 2, t=1050          */
  snprintf(msg, sizeof msg, "interval = %u ms (nếu tính nhầm sẽ là ~900)",
           f.last_interval);
  check("xung bị loại KHÔNG làm ngắn khoảng cách đo được",
        f.last_interval >= 950 && f.last_interval <= 1050, msg);

  /* --- one drop is not a rate -------------------------------------------
   * Measured on the board: a 652 dpm reading one second after boot, from a
   * single drop and a very short gap. */
  begin(&f);
  hold(&f, IDLE, 50);
  hold(&f, BROKEN, 3); hold(&f, IDLE, 40);
  dpm = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%u giọt -> %.1f dpm", f.total_drops, dpm);
  check("mới một giọt thì KHÔNG bịa ra tốc độ",
        f.total_drops == 1 && dpm == 0.0f, msg);

  printf("\n== Làm mượt bằng trung vị ==\n");

  /* --- one odd interval must not move the reported rate ------------------ */
  begin(&f);
  hold(&f, IDLE, 50);
  for (int i = 0; i < 4; i++) drop(&f, 30, 1000);
  drop(&f, 30, 1600);              /* one late drop */
  for (int i = 0; i < 3; i++) drop(&f, 30, 1000);
  uint32_t med = drop_filter_median_interval(&f);
  snprintf(msg, sizeof msg, "trung vị %u ms (trung bình sẽ là ~1120)", med);
  check("một giọt trễ không kéo được tốc độ hiển thị",
        med >= 950 && med <= 1050, msg);

  printf("\n== Im lặng vẫn là bằng chứng ==\n");

  /* --- the rule the AI depends on: silence must pull the rate down ------- */
  begin(&f);
  hold(&f, IDLE, 50);
  for (int i = 0; i < 6; i++) drop(&f, 30, 1000);
  float before = drop_filter_rate_dpm(&f, clock_ms);
  hold(&f, IDLE, 15000);           /* line stops */
  float after = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%.1f -> %.1f dpm", before, after);
  check("dừng nhỏ giọt 15 s -> tốc độ tụt về gần 0",
        before > 50.0f && after < 5.0f, msg);

  /* --- and smoothing must not blunt it ---------------------------------- */
  hold(&f, IDLE, 45000);
  float much_later = drop_filter_rate_dpm(&f, clock_ms);
  snprintf(msg, sizeof msg, "%.1f dpm sau 60 s im lặng", much_later);
  check("càng im lâu càng gần 0, trung vị không giữ nó lại",
        much_later < after, msg);

  printf("\n== Tự dò trạng thái nền ==\n");

  /* --- an inverted sensor must work identically once idle is known ------- */
  drop_filter_init(&f, 0);         /* clear beam reads LOW on this rig */
  clock_ms = 0;
  clock_us = 0;
  for (int i = 0; i < 10; i++) {
    hold(&f, 1, 30);               /* broken = HIGH here */
    hold(&f, 0, 970);
  }
  snprintf(msg, sizeof msg, "%u giọt", f.total_drops);
  check("cảm biến đấu ngược vẫn đếm đúng", f.total_drops == 10, msg);

  printf("\n%s  (%d failures)\n\n",
         failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
  return failures == 0 ? 0 : 1;
}

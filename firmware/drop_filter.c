/* ============================================================================
 *  drop_filter.c — confirmed-pulse drop detection
 *
 *  WHAT WAS WRONG BEFORE
 *
 *  The first detector counted EVERY edge on the sensor pin, with a single
 *  200 ms dead time. That is wrong twice over. One drop crossing the beam
 *  produces two edges - beam broken, beam restored - so a drop whose shadow
 *  outlasted the dead time was counted twice. And because the dead time was
 *  measured from the last accepted count, a burst of electrical noise could
 *  swallow the real drop that came after it. On a steady infusion the reported
 *  rate swung between 193, 100, 51 and 32 dpm within a few seconds.
 *
 *  WHAT REPLACES IT
 *
 *  A four-state machine, the shape used in the reference ESP8266 sketch:
 *
 *      WAIT ──beam broken──► CONFIRMING ──held CONFIRM_MS──► (count)
 *        ▲                       │
 *        │                  cleared early = spike, discard
 *        │
 *      CONFIRMING_RELEASE ◄──beam clear── WAIT_RELEASE
 *        └── clear for RELEASE_MS ──► armed again
 *
 *  A drop is one count, taken on the beam breaking. Nothing is counted again
 *  until the beam has been clear long enough for that drop to have passed.
 *
 *  THE NUMBERS
 *
 *  CONFIRM_MS 12  - a real drop shadows the beam for several ms; an electrical
 *                   spike is gone in microseconds. Costs no timing accuracy,
 *                   because the drop is timestamped at the START of the pulse.
 *  RELEASE_MS 25  - a drop that clings and then falls throws two shadows in
 *                   quick succession; requiring a clear beam this long keeps
 *                   that as one drop.
 *  MIN_GAP_MS 250 - 250 ms is 240 dpm. Nothing clinical runs that fast, so a
 *                   pulse closer than this to the previous drop is a double
 *                   count, not a drop.
 *
 *  A rejected pulse does NOT move last_drop_ms. That is the subtle half: if a
 *  spike were allowed to reset the clock, the genuine interval being measured
 *  would be cut in half and the rate would read double - the detector would
 *  report noise as a fast infusion.
 * ========================================================================== */

#include "drop_filter.h"

#include <string.h>

void drop_filter_init(drop_filter_t *f, int idle_level)
{
  memset(f, 0, sizeof(*f));
  f->idle_level = idle_level ? 1 : 0;
  f->phase      = DROP_WAIT;
}

static void iv_push(drop_filter_t *f, uint32_t interval)
{
  f->iv[f->iv_idx] = interval;
  f->iv_idx = (uint8_t)((f->iv_idx + 1U) % DROP_MEDIAN_N);
  if (f->iv_count < DROP_MEDIAN_N) f->iv_count++;
}

uint32_t drop_filter_median_interval(const drop_filter_t *f)
{
  if (f->iv_count == 0U) return 0U;

  /* Insertion sort on at most five values - cheaper here than anything
   * cleverer, and obvious to read. */
  uint32_t s[DROP_MEDIAN_N];
  for (uint8_t i = 0; i < f->iv_count; i++) s[i] = f->iv[i];

  for (uint8_t i = 1; i < f->iv_count; i++) {
    uint32_t v = s[i];
    int j = (int)i - 1;
    while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; }
    s[j + 1] = v;
  }
  return s[f->iv_count / 2U];
}

/* Accepts one drop, timestamped at the moment the beam BROKE rather than at
 * confirmation, so the measured interval carries no confirm delay. */
static bool register_drop(drop_filter_t *f, uint32_t pulse_start_ms)
{
  if (f->total_drops > 0U) {
    uint32_t interval = pulse_start_ms - f->last_drop_ms;

    if (interval < DROP_MIN_GAP_MS) {
      f->rejected_close++;
      return false;                 /* note: last_drop_ms left untouched */
    }

    f->last_interval = interval;
    iv_push(f, interval);
  } else {
    f->last_interval = 0U;          /* first drop: nothing to measure yet */
  }

  f->total_drops++;
  f->last_drop_ms = pulse_start_ms;
  return true;
}

bool drop_filter_step(drop_filter_t *f, int level, uint32_t now_ms)
{
  bool counted = false;
  int  lv = level ? 1 : 0;

  switch (f->phase) {
    case DROP_WAIT:
      if (lv != f->idle_level) {
        f->edge_ms = now_ms;
        f->phase   = DROP_CONFIRMING;
      }
      break;

    case DROP_CONFIRMING:
      if (lv == f->idle_level) {
        f->rejected_spikes++;
        f->phase = DROP_WAIT;
      } else if (now_ms - f->edge_ms >= DROP_CONFIRM_MS) {
        counted  = register_drop(f, f->edge_ms);
        f->phase = DROP_WAIT_RELEASE;
      }
      break;

    case DROP_WAIT_RELEASE:
      if (lv == f->idle_level) {
        f->edge_ms = now_ms;
        f->phase   = DROP_CONFIRMING_RELEASE;
      }
      break;

    case DROP_CONFIRMING_RELEASE:
      if (lv != f->idle_level) {
        f->phase = DROP_WAIT_RELEASE;
      } else if (now_ms - f->edge_ms >= DROP_RELEASE_MS) {
        f->phase = DROP_WAIT;
      }
      break;
  }

  return counted;
}

float drop_filter_rate_dpm(const drop_filter_t *f, uint32_t now_ms)
{
  uint32_t gap  = now_ms - f->last_drop_ms;
  uint32_t base = drop_filter_median_interval(f);
  if (base == 0U) base = f->last_interval;

  /* "Gap as evidence", load-bearing and unchanged: silence is not a missing
   * measurement, it IS the measurement. A line that has stopped dripping must
   * decay toward zero rather than hold whatever rate it had when the last drop
   * fell. Smoothing must never blunt that - hence max() against the live gap,
   * not an average with it. */
  uint32_t eff = (base > gap) ? base : gap;
  if (eff == 0U) return 0.0f;
  return 60000.0f / (float)eff;
}

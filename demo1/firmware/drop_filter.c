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
 *      WAIT ──beam broken──► CONFIRMING ──held CONFIRM_US──► (count)
 *        ▲                       │
 *        │                  cleared early = spike, discard
 *        │
 *      CONFIRMING_RELEASE ◄──beam clear── WAIT_RELEASE
 *        └── clear for RELEASE_MS ──► armed again
 *
 *  A drop is one count, taken on the beam breaking. Nothing is counted again
 *  until the beam has been clear long enough for that drop to have passed.
 *
 *  WHAT THE SENSOR ACTUALLY DOES (measured on the bench, 95 pulses)
 *
 *  This matters more than any of the reasoning above, because the first
 *  attempt at these numbers was copied from the reference sketch and counted
 *  exactly zero drops.
 *
 *    - The pulse is 1-6 ms wide. The reference sketch's 12 ms confirm rejects
 *      every single one. Our module does not stretch the pulse the way that
 *      one evidently does.
 *    - ONE DROP PRODUCES TWO PULSES, 16-17 ms apart - the droplet necks, then
 *      detaches. The measured gap sequence is 16, 578, 16, 583, 16, 583, ...
 *      so a drop cycle is about 595 ms, i.e. roughly 100 dpm.
 *    - The main loop polls at ~5500 Hz, so a 1 ms pulse is sampled several
 *      times over. Nothing is being missed for want of sampling.
 *
 *  So the 25 ms release window is not a theoretical nicety - it is the thing
 *  that merges each 16 ms pair into one drop, and it is why RELEASE_MS must
 *  stay comfortably above 17 ms and comfortably below the ~580 ms between
 *  drops.
 *
 *  THE NUMBERS
 *
 *  CONFIRM_US 600 - in MICROSECONDS, because the pulses are 1-6 ms and a
 *                   millisecond clock quantises the short ones to zero. Long
 *                   enough to reject electrical spikes, short enough to accept
 *                   the shortest real pulse seen with room to spare. Costs no
 *                   timing accuracy: the drop is timestamped at the START of
 *                   the pulse.
 *  RELEASE_MS 25  - merges the neck-then-detach pair above into one drop.
 *  MIN_GAP_MS 250 - 240 dpm. Nothing clinical runs that fast, so a pulse
 *                   closer than this to the previous drop is a double count.
 *                   A second line of defence behind RELEASE_MS.
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
  f->idle_level   = idle_level ? 1 : 0;
  f->phase        = DROP_WAIT;
  f->pulse_min_ms = 0xFFFFFFFFU;
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

bool drop_filter_step(drop_filter_t *f, int level,
                      uint32_t now_ms, uint32_t now_us)
{
  bool counted = false;
  int  lv = level ? 1 : 0;

  if (lv == f->idle_level) f->samples_idle++; else f->samples_active++;


  switch (f->phase) {
    case DROP_WAIT:
      if (lv != f->idle_level) {
        f->edge_ms = now_ms;
        f->edge_us = now_us;
        f->phase   = DROP_CONFIRMING;
      }
      break;

    case DROP_CONFIRMING:
      if (lv == f->idle_level) {
        /* Too short to be a drop by our rule - but record how long it WAS, so
         * "the drops are real and my threshold is too strict" cannot be
         * mistaken for "there is no signal". */
        uint32_t width = now_ms - f->edge_ms;
        f->pulses_seen++;
        if (width < f->pulse_min_ms) f->pulse_min_ms = width;
        if (width > f->pulse_max_ms) f->pulse_max_ms = width;

        f->rejected_spikes++;
        f->phase = DROP_WAIT;
      } else if (now_us - f->edge_us >= DROP_CONFIRM_US) {
        counted  = register_drop(f, f->edge_ms);
        f->phase = DROP_WAIT_RELEASE;
      }
      break;

    case DROP_WAIT_RELEASE:
      if (lv == f->idle_level) {
        /* A pulse that was long enough to count also belongs in the width
         * statistics, or the numbers would only ever describe the rejects. */
        uint32_t width = now_ms - f->edge_ms;
        f->pulses_seen++;
        if (width < f->pulse_min_ms) f->pulse_min_ms = width;
        if (width > f->pulse_max_ms) f->pulse_max_ms = width;

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
  /* Nothing measurable yet. Seen on the bench as a 652 dpm reading one second
   * after boot: with a single drop counted there is no interval to reason
   * from, so the fallback below was left computing a rate from however long
   * ago that one drop happened - a few tens of milliseconds, which divides
   * into a spectacular number. One drop tells you a drop happened; it does not
   * tell you a rate, and inventing one puts a spike into the AI's very first
   * window. */
  if (f->total_drops < 2U) return 0.0f;

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

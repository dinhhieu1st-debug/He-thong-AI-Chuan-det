/* ============================================================================
 *  drop_filter.h — turning a noisy beam into confirmed drops
 *
 *  Deliberately knows nothing about GPIO, this board, or Zigbee: it is fed a
 *  level and a millisecond timestamp and answers "was that a drop?". That is
 *  what lets tools/drop_filter_test.c replay noise patterns against the real
 *  code on a PC, instead of the team having to reproduce electrical noise on a
 *  bench to find out whether a change helped.
 * ========================================================================== */
#ifndef DROP_FILTER_H
#define DROP_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/* See drop_filter.c for the reasoning behind each number, and for the bench
 * measurements they were fitted to. */
#define DROP_CONFIRM_US   600U   /* microseconds - the pulses are only 1-6 ms */
#define DROP_RELEASE_MS    25U
#define DROP_MIN_GAP_MS   250U
#define DROP_MEDIAN_N      5U

typedef enum {
  DROP_WAIT,
  DROP_CONFIRMING,
  DROP_WAIT_RELEASE,
  DROP_CONFIRMING_RELEASE
} drop_phase_t;

typedef struct {
  int          idle_level;      /* what the beam reads when nothing falls    */
  drop_phase_t phase;
  uint32_t     edge_ms;         /* when the current pulse or gap started     */
  uint32_t     edge_us;         /* same instant, for the sub-ms confirm      */

  uint32_t     total_drops;
  uint32_t     last_drop_ms;    /* start of the last accepted pulse          */
  uint32_t     last_interval;   /* raw interval between the last two drops   */

  uint32_t     iv[DROP_MEDIAN_N];
  uint8_t      iv_idx;
  uint8_t      iv_count;

  uint32_t     rejected_spikes; /* pulses too short to be a drop             */
  uint32_t     rejected_close;  /* pulses too soon after the previous drop   */

  /* --- what the pin is actually doing -------------------------------------
   * Kept because guessing at this cost a wasted flash cycle once already: a
   * detector that counts nothing and a detector fed a pin that never moves
   * look identical from the outside. These make them distinguishable without
   * an oscilloscope. Counters are cheap; being wrong about the hardware is
   * not. */
  uint32_t     pulses_seen;     /* every excursion away from idle, any length */
  uint32_t     pulse_min_ms;    /* shortest excursion seen (0xFFFFFFFF = none)*/
  uint32_t     pulse_max_ms;
  uint32_t     samples_idle;    /* level == idle_level                        */
  uint32_t     samples_active;  /* level != idle_level                        */
} drop_filter_t;

/* idle_level is what the pin reads with the beam clear - measure it, do not
 * assume it. */
void drop_filter_init(drop_filter_t *f, int idle_level);

/* Feed one sample. Returns true on the sample where a drop is confirmed.
 *
 * Two clocks on purpose. now_ms drives everything that can span minutes -
 * intervals, the rate, the silence rule - where a 32-bit millisecond counter
 * is safe for weeks. now_us drives only the confirm window, which is always
 * under a few milliseconds; a 32-bit microsecond counter wraps every 71
 * minutes, so using it for a gap would eventually report a stopped line as a
 * fast one. */
bool drop_filter_step(drop_filter_t *f, int level,
                      uint32_t now_ms, uint32_t now_us);

/* Median of the recent intervals in ms, or 0 before any are known. */
uint32_t drop_filter_median_interval(const drop_filter_t *f);

/* Drops per minute now, including the "gap as evidence" rule: a line that has
 * stopped dripping must decay toward zero rather than hold its last rate. */
float drop_filter_rate_dpm(const drop_filter_t *f, uint32_t now_ms);

#endif /* DROP_FILTER_H */

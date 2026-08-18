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

/* See drop_filter.c for the reasoning behind each number. */
#define DROP_CONFIRM_MS    12U
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

  uint32_t     total_drops;
  uint32_t     last_drop_ms;    /* start of the last accepted pulse          */
  uint32_t     last_interval;   /* raw interval between the last two drops   */

  uint32_t     iv[DROP_MEDIAN_N];
  uint8_t      iv_idx;
  uint8_t      iv_count;

  uint32_t     rejected_spikes; /* pulses too short to be a drop             */
  uint32_t     rejected_close;  /* pulses too soon after the previous drop   */
} drop_filter_t;

/* idle_level is what the pin reads with the beam clear - measure it, do not
 * assume it. */
void drop_filter_init(drop_filter_t *f, int idle_level);

/* Feed one sample. Returns true on the sample where a drop is confirmed. */
bool drop_filter_step(drop_filter_t *f, int level, uint32_t now_ms);

/* Median of the recent intervals in ms, or 0 before any are known. */
uint32_t drop_filter_median_interval(const drop_filter_t *f);

/* Drops per minute now, including the "gap as evidence" rule: a line that has
 * stopped dripping must decay toward zero rather than hold its last rate. */
float drop_filter_rate_dpm(const drop_filter_t *f, uint32_t now_ms);

#endif /* DROP_FILTER_H */

#ifndef VITALS_AI_H
#define VITALS_AI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t level;              /* 1 normal, 2 attention, 3 warning */
  bool models_ready;
  bool history_ready;
  bool ai_anomaly;
  bool hard_limit;
  float hr_baseline;
  float spo2_baseline;
  float hr_forecast_16s;
  float spo2_forecast_16s;
  float anomaly_score;
  uint8_t baseline_samples;
  uint8_t history_samples;
} vitals_ai_result_t;

bool vitals_ai_init(void);
void vitals_ai_reset(void);
void vitals_ai_begin_baseline_recalibration(void);
bool vitals_ai_baseline_recalibrating(void);
uint8_t vitals_ai_baseline_recalibration_samples(void);
bool vitals_ai_take_baseline_recalibration_completed(void);
void vitals_ai_clear_history(vitals_ai_result_t *result);
void vitals_ai_begin_test(void);
void vitals_ai_end_test(vitals_ai_result_t *result);
void vitals_ai_step(int16_t heart_rate, int16_t spo2, bool valid,
                    vitals_ai_result_t *result);

#ifdef __cplusplus
}
#endif

#endif

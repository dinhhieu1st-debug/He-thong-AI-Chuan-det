#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

bool oled_display_init(void);
void oled_display_message(const char *line1,
                          const char *line2,
                          const char *line3,
                          const char *line4);
void oled_display_monitor(int16_t heart_rate,
                          int16_t spo2,
                          bool vitals_valid,
                          float weight_kg,
                          bool weight_valid,
                          float last_interval_s,
                          float learned_interval_s,
                          float drops_per_minute,
                          uint8_t final_level,
                          uint8_t vitals_level,
                          uint8_t drops_level,
                          bool hr_cause,
                          bool spo2_cause,
                          uint8_t hr_baseline_samples,
                          uint8_t vitals_history_samples,
                          uint8_t drop_training_samples,
                          bool alerts_armed,
                          bool baseline_recalibrating);

#endif

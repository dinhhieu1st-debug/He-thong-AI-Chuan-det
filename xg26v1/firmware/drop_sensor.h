#ifndef DROP_SENSOR_H
#define DROP_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

void drop_sensor_init(void);
void drop_sensor_poll(void);
uint32_t drop_sensor_count(void);
float drop_sensor_last_interval_seconds(void);
float drop_sensor_average_interval_seconds(void);
float drop_sensor_median_interval_seconds(void);
float drop_sensor_drops_per_minute(void);
uint8_t drop_sensor_interval_count(void);
uint32_t drop_sensor_last_drop_ms(void);
bool drop_sensor_calibrated(void);
void drop_sensor_reset_statistics(void);
void drop_sensor_blank_until(uint32_t time_ms);
void drop_sensor_set_min_gap_ms(uint32_t min_gap_ms);

#endif

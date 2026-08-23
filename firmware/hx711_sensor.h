#ifndef HX711_SENSOR_H
#define HX711_SENSOR_H

#include <stdbool.h>

void hx711_sensor_init(void);
void hx711_sensor_poll(void);
void hx711_sensor_tare(void);
bool hx711_sensor_connected(void);
bool hx711_sensor_tared(void);
float hx711_sensor_weight_kg(void);

#endif

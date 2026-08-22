#ifndef DROP_SENSOR_H
#define DROP_SENSOR_H

#include <stdint.h>

void drop_sensor_init(void);
void drop_sensor_poll(void);
uint32_t drop_sensor_count(void);

#endif

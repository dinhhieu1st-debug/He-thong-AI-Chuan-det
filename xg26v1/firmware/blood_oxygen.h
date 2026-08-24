#ifndef BLOOD_OXYGEN_H
#define BLOOD_OXYGEN_H

#include <stdbool.h>
#include <stdint.h>

bool blood_oxygen_init(void);
bool blood_oxygen_sample(int16_t *heart_rate, int16_t *spo2);
bool blood_oxygen_connected(void);

#endif

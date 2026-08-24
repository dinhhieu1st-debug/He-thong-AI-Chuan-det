#ifndef SOFTWARE_I2C_H
#define SOFTWARE_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void software_i2c_init(void);
bool software_i2c_probe(uint8_t address);
bool software_i2c_write(uint8_t address, const uint8_t *data, size_t length);
bool software_i2c_write_read(uint8_t address,
                             const uint8_t *write_data,
                             size_t write_length,
                             uint8_t *read_data,
                             size_t read_length);

#endif

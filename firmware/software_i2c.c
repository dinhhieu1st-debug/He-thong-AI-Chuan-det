#include "software_i2c.h"

#include "em_gpio.h"
#include "sl_udelay.h"

#define I2C_SCL_PORT gpioPortC
#define I2C_SCL_PIN  5U
#define I2C_SDA_PORT gpioPortC
#define I2C_SDA_PIN  7U

static void delay_bus(void) { sl_udelay_wait(5U); }
static void set_scl(bool high)
{
  if (high) { GPIO_PinOutSet(I2C_SCL_PORT, I2C_SCL_PIN); }
  else { GPIO_PinOutClear(I2C_SCL_PORT, I2C_SCL_PIN); }
  delay_bus();
}
static void set_sda(bool high)
{
  if (high) { GPIO_PinOutSet(I2C_SDA_PORT, I2C_SDA_PIN); }
  else { GPIO_PinOutClear(I2C_SDA_PORT, I2C_SDA_PIN); }
  delay_bus();
}
static void start_bus(void) { set_sda(true); set_scl(true); set_sda(false); set_scl(false); }
static void stop_bus(void) { set_sda(false); set_scl(true); set_sda(true); }

static bool write_byte(uint8_t value)
{
  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1) {
    set_sda((value & mask) != 0U);
    set_scl(true);
    set_scl(false);
  }
  set_sda(true);
  set_scl(true);
  bool acknowledged = GPIO_PinInGet(I2C_SDA_PORT, I2C_SDA_PIN) == 0;
  set_scl(false);
  return acknowledged;
}

static uint8_t read_byte(bool acknowledge)
{
  uint8_t value = 0U;
  set_sda(true);
  for (uint8_t bit = 0U; bit < 8U; bit++) {
    set_scl(true);
    value = (uint8_t)((value << 1) |
                      (GPIO_PinInGet(I2C_SDA_PORT, I2C_SDA_PIN) ? 1U : 0U));
    set_scl(false);
  }
  set_sda(!acknowledge);
  set_scl(true);
  set_scl(false);
  set_sda(true);
  return value;
}

void software_i2c_init(void)
{
  GPIO_PinModeSet(I2C_SCL_PORT, I2C_SCL_PIN, gpioModeWiredAndPullUp, 1);
  GPIO_PinModeSet(I2C_SDA_PORT, I2C_SDA_PIN, gpioModeWiredAndPullUp, 1);
  set_sda(true);
  for (uint8_t i = 0U; i < 9U; i++) { set_scl(false); set_scl(true); }
  stop_bus();
}

bool software_i2c_probe(uint8_t address)
{
  start_bus();
  bool ok = write_byte((uint8_t)(address << 1));
  stop_bus();
  return ok;
}

bool software_i2c_write(uint8_t address, const uint8_t *data, size_t length)
{
  start_bus();
  bool ok = write_byte((uint8_t)(address << 1));
  for (size_t i = 0U; ok && i < length; i++) { ok = write_byte(data[i]); }
  stop_bus();
  return ok;
}

bool software_i2c_write_read(uint8_t address,
                             const uint8_t *write_data,
                             size_t write_length,
                             uint8_t *read_data,
                             size_t read_length)
{
  start_bus();
  bool ok = write_byte((uint8_t)(address << 1));
  for (size_t i = 0U; ok && i < write_length; i++) { ok = write_byte(write_data[i]); }
  if (ok && read_length > 0U) {
    start_bus();
    ok = write_byte((uint8_t)((address << 1) | 1U));
    for (size_t i = 0U; ok && i < read_length; i++) {
      read_data[i] = read_byte(i + 1U < read_length);
    }
  }
  stop_bus();
  return ok;
}

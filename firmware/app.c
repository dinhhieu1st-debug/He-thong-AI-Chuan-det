/* Staged hardware validation firmware for BRD2709A / EFR32xG26.
 * Stage 1 (retained): drop sensor on PD02.
 * Stage 2 (current):  HX711 DOUT=PC01, SCK=PC03.
 */

#include "app.h"

#include <stdio.h>

#include "drop_sensor.h"
#include "em_cmu.h"
#include "hx711_sensor.h"

void app_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  printf("\r\n=== SENSOR TEST 2: DROP + HX711 ===\r\n");
  drop_sensor_init();
  hx711_sensor_init();
}

void app_process_action(void)
{
  drop_sensor_poll();
  hx711_sensor_poll();
}

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
                          bool sampling_active,
                          bool alerts_armed,
                          bool baseline_recalibrating);

/* --- OTA progress screen ---------------------------------------------------
 *
 * Overrides the vitals/notification screens while an OTA is in flight:
 * oled_display_monitor() and oled_display_message() become no-ops until the
 * OTA screen clears, so a vitals update mid-download cannot knock the OLED
 * off the progress screen.
 *
 * Callable from a Zigbee OTA callback: these two setters only update state
 * and a dirty flag, they never touch I2C. oled_display_ota_step(), called
 * every app_process_action() iteration like the rest of this app's polling,
 * owns all actual rendering/flushing and its own redraw/animation timing -
 * matching how the OTA client itself only ever reports real progress (never
 * simulated by a timer).
 */

/* percent is the real downloaded_bytes * 100 / total_image_size from the OTA
 * client (clamped 0..100), not a timer simulation. active=false clears the
 * OTA screen back to idle immediately (no held result screen). */
void oled_display_set_ota_progress(bool active, uint8_t percent);

/* success=true is meant to be called exactly once, right before the OTA
 * client reboots into the new image - hold "UPDATE DONE / REBOOT" until the
 * actual reset. success=false starts a ~4s "UPDATE FAIL / <message>" hold,
 * then returns to the normal screen automatically. message may be NULL/long;
 * it is copied and truncated to fit the screen, never causes a crash. */
void oled_display_set_ota_result(bool success, const char *message);

/* Advances the UPDATE... animation and (re)draws the OTA screen at a bounded
 * rate (see oled_display.c) so frequent progress updates cannot flood I2C or
 * block the Zigbee stack. No-op when no OTA screen is active. */
void oled_display_ota_step(uint32_t now_ms);

#endif

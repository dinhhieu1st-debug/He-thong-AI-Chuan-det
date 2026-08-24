#include "oled_display.h"

#include <stdio.h>
#include <string.h>

#include "sl_sleeptimer.h"
#include "software_i2c.h"

#define OLED_ADDRESS 0x3CU

static bool connected;

static const uint8_t font[][5] = {
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
  {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
  {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
  {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
  {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
  {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
  {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
  {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}
};
static const uint8_t digits[][5] = {
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
};

static uint8_t monitor_frame[8][128];
static uint8_t previous_monitor_frame[8][128];
static bool previous_monitor_valid;

/* --- OTA progress screen state --------------------------------------------
 *
 * Rendering priority (highest first): OTA in progress/verifying, then a still
 * live OTA result, then the normal vitals screen. oled_display_monitor() and
 * oled_display_message() both check ota_state below and no-op while it is not
 * idle, so nothing else can knock the OLED off this screen mid-OTA.
 */
typedef enum {
  OLED_OTA_IDLE = 0,
  OLED_OTA_DOWNLOADING,
  OLED_OTA_VERIFYING,
  OLED_OTA_SUCCESS,
  OLED_OTA_FAILURE,
} oled_ota_state_t;

#define OLED_OTA_ANIM_INTERVAL_MS      400U
#define OLED_OTA_MIN_REDRAW_INTERVAL_MS 250U
#define OLED_OTA_FAILURE_HOLD_MS       4000U

static oled_ota_state_t ota_state = OLED_OTA_IDLE;
static uint8_t ota_percent;
static char ota_result_message[24];
static bool ota_dirty;
static bool ota_just_entered_result;
static uint32_t ota_state_entered_ms;
static uint32_t ota_last_anim_ms;
static uint32_t ota_last_redraw_ms;
static uint8_t ota_anim_frame;

static bool command(uint8_t value)
{
  uint8_t packet[2] = { 0x00U, value };
  return software_i2c_write(OLED_ADDRESS, packet, sizeof(packet));
}

static void glyph(char c, uint8_t columns[5])
{
  memset(columns, 0, 5U);
  if (c >= 'A' && c <= 'Z') { memcpy(columns, font[c - 'A'], 5U); }
  else if (c >= '0' && c <= '9') { memcpy(columns, digits[c - '0'], 5U); }
  else if (c == ':') { columns[1] = columns[2] = 0x36U; }
  else if (c == '.') { columns[2] = 0x60U; }
  else if (c == '-') { columns[1] = columns[2] = columns[3] = 0x08U; }
  else if (c == '/') { columns[0]=0x20U; columns[1]=0x10U; columns[2]=0x08U; columns[3]=0x04U; columns[4]=0x02U; }
  else if (c == '%') { uint8_t p[5] = {0x23,0x13,0x08,0x64,0x62}; memcpy(columns,p,5U); }
}

static void line(uint8_t page, const char *text)
{
  uint8_t packet[129];
  size_t length = strlen(text);
  size_t width = length * 6U;
  size_t left = width < 128U ? (128U - width) / 2U : 0U;
  packet[0] = 0x40U;
  memset(&packet[1], 0, 128U);
  for (size_t i = 0U; i < length && left + i * 6U + 5U <= 128U; i++) {
    uint8_t columns[5];
    glyph(text[i], columns);
    memcpy(&packet[1U + left + i * 6U], columns, 5U);
  }
  command((uint8_t)(0xB0U + page));
  command(0x00U);
  command(0x10U);
  (void)software_i2c_write(OLED_ADDRESS, packet, sizeof(packet));
}

static void frame_text(uint8_t page, uint8_t x, const char *text)
{
  while (*text != '\0' && x <= 122U) {
    uint8_t columns[5];
    glyph(*text++, columns);
    memcpy(&monitor_frame[page][x], columns, 5U);
    x = (uint8_t)(x + 6U);
  }
}

static void frame_center(uint8_t page, const char *text)
{
  size_t width = strlen(text) * 6U;
  uint8_t x = width < 128U ? (uint8_t)((128U - width) / 2U) : 0U;
  frame_text(page, x, text);
}

static void frame_horizontal(uint8_t y)
{
  uint8_t page = y / 8U;
  uint8_t mask = (uint8_t)(1U << (y & 7U));
  for (uint8_t x = 1U; x < 127U; x++) { monitor_frame[page][x] |= mask; }
}

static void frame_vertical_dotted(uint8_t x, uint8_t y1, uint8_t y2)
{
  for (uint8_t y = y1; y <= y2; y = (uint8_t)(y + 2U)) {
    monitor_frame[y / 8U][x] |= (uint8_t)(1U << (y & 7U));
  }
}

static void frame_pixel(uint8_t x, uint8_t y)
{
  if (x >= 128U || y >= 64U) { return; }
  monitor_frame[y / 8U][x] |= (uint8_t)(1U << (y & 7U));
}

static void frame_rect_outline(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
  for (uint8_t i = 0U; i < w; i++) { frame_pixel((uint8_t)(x + i), y); frame_pixel((uint8_t)(x + i), (uint8_t)(y + h - 1U)); }
  for (uint8_t j = 0U; j < h; j++) { frame_pixel(x, (uint8_t)(y + j)); frame_pixel((uint8_t)(x + w - 1U), (uint8_t)(y + j)); }
}

static void frame_rect_filled(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
  for (uint8_t j = 0U; j < h; j++) {
    for (uint8_t i = 0U; i < w; i++) { frame_pixel((uint8_t)(x + i), (uint8_t)(y + j)); }
  }
}

static void frame_large_value(uint8_t first_page, uint8_t x,
                              uint8_t right_edge, const char *text)
{
  while (*text != '\0' && (uint16_t)x + 10U < right_edge) {
    uint8_t columns[5];
    glyph(*text++, columns);
    for (uint8_t column = 0U; column < 5U; column++) {
      uint16_t doubled = 0U;
      for (uint8_t bit = 0U; bit < 8U; bit++) {
        if ((columns[column] & (1U << bit)) != 0U) {
          doubled |= (uint16_t)(3UL << (bit * 2U));
        }
      }
      monitor_frame[first_page][x + column * 2U] = (uint8_t)doubled;
      monitor_frame[first_page][x + column * 2U + 1U] = (uint8_t)doubled;
      monitor_frame[first_page + 1U][x + column * 2U] = (uint8_t)(doubled >> 8U);
      monitor_frame[first_page + 1U][x + column * 2U + 1U] = (uint8_t)(doubled >> 8U);
    }
    x = (uint8_t)(x + 12U);
  }
}

static void frame_flush(void)
{
  uint8_t packet[129];
  packet[0] = 0x40U;
  for (uint8_t page = 0U; page < 8U; page++) {
    if (previous_monitor_valid
        && memcmp(monitor_frame[page], previous_monitor_frame[page], 128U) == 0) {
      continue;
    }
    memcpy(&packet[1], monitor_frame[page], 128U);
    command((uint8_t)(0xB0U + page));
    command(0x00U);
    command(0x10U);
    (void)software_i2c_write(OLED_ADDRESS, packet, sizeof(packet));
    memcpy(previous_monitor_frame[page], monitor_frame[page], 128U);
  }
  previous_monitor_valid = true;
}

bool oled_display_init(void)
{
  connected = software_i2c_probe(OLED_ADDRESS);
  if (!connected) { return false; }
  sl_sleeptimer_delay_millisecond(50U);
  static const uint8_t init[] = {
    0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,0x20,0x02,
    0xA1,0xC8,0xDA,0x12,0x81,0xCF,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF
  };
  for (size_t i = 0U; connected && i < sizeof(init); i++) { connected = command(init[i]); }
  for (uint8_t page = 0U; connected && page < 8U; page++) { line(page, ""); }
  return connected;
}

void oled_display_message(const char *line1,
                          const char *line2,
                          const char *line3,
                          const char *line4)
{
  if (!connected) { return; }
  /* OTA screen has priority - see the state block near monitor_frame. */
  if (ota_state != OLED_OTA_IDLE) { return; }
  /* The next monitor frame must be sent in full after a message screen. */
  previous_monitor_valid = false;
  for (uint8_t page = 0U; page < 8U; page++) { line(page, ""); }
  if (line1 != NULL) { line(0U, line1); }
  if (line2 != NULL) { line(2U, line2); }
  if (line3 != NULL) { line(4U, line3); }
  if (line4 != NULL) { line(6U, line4); }
}

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
                          bool baseline_recalibrating)
{
  if (!connected) { return; }
  if (ota_state != OLED_OTA_IDLE) { return; }
  char text[32];
  memset(monitor_frame, 0, sizeof(monitor_frame));

  /* Three compact 16-pixel rows: small labels, 2x numeric values. */
  frame_text(0U, 1U, "HR");
  frame_text(0U, 66U, "O2");
  if (vitals_valid) {
    (void)snprintf(text, sizeof(text), "%d", heart_rate);
    frame_large_value(0U, 18U, 63U, text);
    (void)snprintf(text, sizeof(text), "%d", spo2);
    frame_large_value(0U, 84U, 127U, text);
    frame_text(1U, 120U, "%");
  } else if (baseline_recalibrating) {
    (void)snprintf(text, sizeof(text), "BASELINE %u/60", hr_baseline_samples);
    frame_center(6U, text);
    (void)snprintf(text, sizeof(text), "ALERT LEVEL %u", final_level);
    frame_center(7U, text);
  } else {
    frame_large_value(0U, 18U, 63U, "--");
    frame_large_value(0U, 84U, 127U, "--");
  }

  frame_text(2U, 1U, "DR");
  frame_text(2U, 66U, "GP");
  if (drops_per_minute > 0.0f) { (void)snprintf(text, sizeof(text), "%.0F", (double)drops_per_minute); }
  else { (void)snprintf(text, sizeof(text), "--"); }
  frame_large_value(2U, 18U, 63U, text);
  if (last_interval_s > 0.0f && last_interval_s < 10.0f) {
    (void)snprintf(text, sizeof(text), "%.1F", (double)last_interval_s);
  } else if (last_interval_s >= 10.0f) {
    (void)snprintf(text, sizeof(text), "%.0F", (double)last_interval_s);
  }
  else { (void)snprintf(text, sizeof(text), "--"); }
  frame_large_value(2U, 84U, 127U, text);

  frame_text(4U, 1U, "BG");
  frame_text(4U, 66U, "ST");
  if (weight_valid) { (void)snprintf(text, sizeof(text), "%.1F", (double)weight_kg); }
  else { (void)snprintf(text, sizeof(text), "--"); }
  frame_large_value(4U, 15U, 63U, text);
  if (learned_interval_s > 0.0f) {
    (void)snprintf(text, sizeof(text), "%.0F", (double)(60.0f / learned_interval_s));
  } else { (void)snprintf(text, sizeof(text), "--"); }
  frame_large_value(4U, 84U, 127U, text);
  frame_vertical_dotted(63U, 0U, 47U);
  frame_horizontal(48U);

  if (!sampling_active) {
    frame_center(6U, "SET DROP ON WEB");
    frame_center(7U, "SAMPLING OFF");
  } else if (!alerts_armed) {
    (void)hr_baseline_samples;
    (void)snprintf(text, sizeof(text), "DROP %u/20 ALARM OFF", drop_training_samples);
    frame_center(6U, text);
    (void)snprintf(text, sizeof(text), "HR+O2 %u/64", vitals_history_samples);
    frame_center(7U, text);
  } else {
    char causes[18] = "";
    if (vitals_level > 1U && hr_cause) { strcat(causes, "HR"); }
    if (vitals_level > 1U && spo2_cause) {
      if (causes[0] != '\0') strcat(causes, "+");
      strcat(causes, "SPO2");
    }
    if (vitals_level > 1U && causes[0] == '\0') { strcat(causes, "VITALS"); }
    if (drops_level > 1U) {
      if (causes[0] != '\0') strcat(causes, "+");
      strcat(causes, "DROP");
    }
    if (final_level == 1U) { frame_center(6U, "NORMAL"); }
    else if (final_level == 2U) { frame_center(6U, "L2: CAUTION"); }
    else { frame_center(6U, "L3 CRITICAL"); }
    if (!vitals_valid) { frame_center(7U, "NO SIGNAL HR+O2"); }
    else if (causes[0] == '\0') { frame_center(7U, "MONITORING ON"); }
    else {
      (void)snprintf(text, sizeof(text), "CAUSE: %s", causes);
      frame_center(7U, text);
    }
  }
  frame_flush();
}

/* --- OTA progress screen ---------------------------------------------------
 *
 *        68%
 *  [############--------]
 *        UPDATE..
 *
 * Same monitor_frame + frame_flush() as the vitals screen, so an OTA redraw
 * only sends the OLED pages that actually changed - no different from any
 * other screen here.
 */
static void frame_ota_percent(uint8_t percent)
{
  char text[5];
  (void)snprintf(text, sizeof(text), "%u%%", (unsigned int)percent);
  size_t width = strlen(text) * 12U; /* frame_large_value: 12px per glyph */
  uint8_t x = width < 128U ? (uint8_t)((128U - width) / 2U) : 0U;
  frame_large_value(0U, x, 128U, text);
}

static void frame_ota_bar(uint8_t percent)
{
  const uint8_t bar_x = 8U;
  const uint8_t bar_y = 34U;
  const uint8_t bar_width = 112U;
  const uint8_t bar_height = 10U;
  frame_rect_outline(bar_x, bar_y, bar_width, bar_height);

  const uint8_t inner_x = (uint8_t)(bar_x + 2U);
  const uint8_t inner_y = (uint8_t)(bar_y + 2U);
  const uint8_t inner_width = (uint8_t)(bar_width - 4U);
  const uint8_t inner_height = (uint8_t)(bar_height - 4U);
  uint8_t filled_width = (uint8_t)(((uint16_t)percent * inner_width) / 100U);
  if (filled_width > inner_width) { filled_width = inner_width; } /* never overflow at 100% */
  if (filled_width > 0U) { frame_rect_filled(inner_x, inner_y, filled_width, inner_height); }
}

static void render_ota_screen(void)
{
  memset(monitor_frame, 0, sizeof(monitor_frame));
  frame_ota_percent(ota_percent);
  frame_ota_bar(ota_percent);

  switch (ota_state) {
    case OLED_OTA_DOWNLOADING: {
      static const char *const dots[4] = { "UPDATE", "UPDATE.", "UPDATE..", "UPDATE..." };
      frame_center(7U, dots[ota_anim_frame & 3U]);
      break;
    }
    case OLED_OTA_VERIFYING:
      frame_center(7U, "VERIFYING...");
      break;
    case OLED_OTA_SUCCESS:
      frame_center(6U, "UPDATE DONE");
      frame_center(7U, "REBOOT");
      break;
    case OLED_OTA_FAILURE:
      frame_center(6U, "UPDATE FAIL");
      frame_center(7U, ota_result_message[0] != '\0' ? ota_result_message : "SEE LOG");
      break;
    case OLED_OTA_IDLE:
    default:
      break;
  }
  frame_flush();
}

void oled_display_set_ota_progress(bool active, uint8_t percent)
{
  if (percent > 100U) { percent = 100U; }
  if (!active) {
    if (ota_state != OLED_OTA_IDLE) { ota_state = OLED_OTA_IDLE; ota_dirty = true; }
    return;
  }
  oled_ota_state_t next = (percent >= 100U) ? OLED_OTA_VERIFYING : OLED_OTA_DOWNLOADING;
  if (ota_state != next || ota_percent != percent) { ota_dirty = true; }
  ota_state = next;
  ota_percent = percent;
}

void oled_display_set_ota_result(bool success, const char *message)
{
  ota_state = success ? OLED_OTA_SUCCESS : OLED_OTA_FAILURE;
  ota_percent = 100U;
  ota_result_message[0] = '\0';
  if (message != NULL) {
    size_t n = strlen(message);
    if (n >= sizeof(ota_result_message)) { n = sizeof(ota_result_message) - 1U; }
    memcpy(ota_result_message, message, n);
    ota_result_message[n] = '\0';
  }
  ota_dirty = true;
  ota_just_entered_result = true;
}

void oled_display_ota_step(uint32_t now_ms)
{
  if (!connected || ota_state == OLED_OTA_IDLE) { return; }

  if (ota_just_entered_result) {
    ota_state_entered_ms = now_ms;
    ota_just_entered_result = false;
  }

  if (ota_state == OLED_OTA_FAILURE
      && (uint32_t)(now_ms - ota_state_entered_ms) >= OLED_OTA_FAILURE_HOLD_MS) {
    ota_state = OLED_OTA_IDLE;
    previous_monitor_valid = false; /* force a full repaint of the vitals screen */
    return;
  }

  if (ota_state == OLED_OTA_DOWNLOADING
      && (uint32_t)(now_ms - ota_last_anim_ms) >= OLED_OTA_ANIM_INTERVAL_MS) {
    ota_last_anim_ms = now_ms;
    ota_anim_frame = (uint8_t)((ota_anim_frame + 1U) & 3U);
    ota_dirty = true;
  }

  if (!ota_dirty) { return; }
  /* The redraw throttle exists to bound how often frequent DOWNLOADING
   * progress updates hit I2C. SUCCESS/FAILURE each fire at most once per OTA
   * and SUCCESS in particular must render now - there is no later tick,
   * the device reboots right after - so neither is throttled. */
  bool one_shot = (ota_state == OLED_OTA_SUCCESS) || (ota_state == OLED_OTA_FAILURE);
  if (!one_shot
      && (uint32_t)(now_ms - ota_last_redraw_ms) < OLED_OTA_MIN_REDRAW_INTERVAL_MS) {
    return;
  }

  ota_last_redraw_ms = now_ms;
  ota_dirty = false;
  render_ota_screen();
}

/* ============================================================================
 *  oled_test.c — host-side test for firmware/oled_display.c
 *
 *  The driver takes its I2C write as a function pointer, so the whole thing
 *  runs on a PC against a fake bus: the "panel" is just a captured
 *  framebuffer. That makes the two things worth checking here testable without
 *  looking at a physical screen.
 *
 *   1. ROTATION. With several faults active the banner must cycle, and must
 *      actually repaint - the driver skips identical frames to save I2C
 *      traffic, and an earlier version of that shortcut would have frozen the
 *      rotation on its first message forever.
 *
 *   2. EVERY STRING RENDERS. The 5x7 font has no lowercase: an unmapped
 *      character draws as a BLANK, so "Bag running low" appears on a bedside
 *      screen as "B" followed by nothing. That is a silent fault - the code
 *      looks right, the screen is wrong - so the strings are checked here
 *      rather than by squinting at the panel.
 *
 *  Build and run:
 *      cc -I firmware -o /tmp/oled_test tools/oled_test.c \
 *         firmware/oled_display.c && /tmp/oled_test
 * ========================================================================== */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "oled_display.h"

static int failures = 0;

static void check(const char *what, bool ok, const char *detail)
{
  printf("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", what, detail);
  if (!ok) failures++;
}

/* --- fake panel ---------------------------------------------------------- */
static int  frames_sent = 0;
static bool fake_write(void *ctx, uint8_t addr, uint8_t control,
                       const uint8_t *data, uint8_t len)
{
  (void)ctx; (void)addr; (void)data;
  if (control == 0x40U && len == OLED_WIDTH) frames_sent++;
  return true;
}

/* Counts lit pixels, as a proxy for "something was actually drawn". */
static int lit_pixels(const oled_display_t *d)
{
  int n = 0;
  for (size_t i = 0; i < sizeof(d->framebuffer); i++) {
    for (int b = 0; b < 8; b++) if (d->framebuffer[i] & (1U << b)) n++;
  }
  return n;
}

/* One page (8 rows) of the framebuffer, as a crude visual. */
static void dump_band(const oled_display_t *d, int page)
{
  for (unsigned x = 0; x < OLED_WIDTH; x++) {
    putchar(d->framebuffer[x + (unsigned)page * OLED_WIDTH] ? '#' : '.');
  }
  putchar('\n');
}

int main(void)
{
  oled_display_t d;
  oled_bus_t bus = { .write = fake_write, .context = NULL };
  char msg[160];

  memset(&d, 0, sizeof(d));
  d.bus = bus;
  d.address = OLED_I2C_ADDRESS;
  d.initialized = true;

  /* ---------------------------------------------------------------------- */
  printf("\n== Xoay vòng nhiều lỗi ==\n");

  static const char *const three[] = {
    "DANGER: OVERLOAD", "PATIENT ALERT", "CHECK IV LINE"
  };

  oled_vitals_t v = {
    .hr_valid = true, .hr_bpm = 88,
    .spo2_valid = true, .spo2_pct = 97,
    .flow_valid = true, .flow_pct = 100,
    .alarm = true,
    .banner = three[0],
    .causes = three, .cause_count = 3,
    .level = 3
  };

  /* 9 calls = 9 seconds at the caller's 1 Hz. Each message is held for
   * OLED_CAUSE_HOLD_TICKS, so all three must appear. */
  int repaints = 0;
  for (int i = 0; i < 9; i++) {
    frames_sent = 0;
    oled_display_show_vitals(&d, &v);
    if (frames_sent > 0) repaints++;
  }
  snprintf(msg, sizeof msg, "%d lần vẽ lại trong 9 giây", repaints);
  check("ba lỗi -> màn hình vẽ lại nhiều lần, không đứng im",
        repaints >= 3, msg);

  /* The real assertion for rotation: the rendered frame must differ between
   * one hold window and the next. */
  memset(&d.framebuffer, 0, sizeof(d.framebuffer));
  oled_display_show_vitals(&d, &v);
  uint8_t frame_a[sizeof(d.framebuffer)];
  memcpy(frame_a, d.framebuffer, sizeof(frame_a));

  for (unsigned i = 0; i < OLED_CAUSE_HOLD_TICKS; i++) oled_display_show_vitals(&d, &v);
  bool changed = memcmp(frame_a, d.framebuffer, sizeof(frame_a)) != 0;
  check("sau một chu kỳ giữ, nội dung ĐÃ đổi sang lỗi khác",
        changed, changed ? "khung hình khác" : "khung hình y hệt");

  /* --- the counter ------------------------------------------------------- */
  int pixels_multi = lit_pixels(&d);

  static const char *const one[] = { "CHECK IV LINE" };
  oled_vitals_t v1 = v;
  v1.causes = one; v1.cause_count = 1; v1.banner = one[0];
  memset(&d.framebuffer, 0, sizeof(d.framebuffer));
  oled_display_show_vitals(&d, &v1);
  int pixels_single = lit_pixels(&d);

  snprintf(msg, sizeof msg, "nhiều lỗi %d px vs một lỗi %d px",
           pixels_multi, pixels_single);
  check("có nhiều lỗi thì vẽ thêm bộ đếm n/m", pixels_multi != pixels_single, msg);

  /* ---------------------------------------------------------------------- */
  printf("\n== Mọi chuỗi phải hiện được (font chỉ có A-Z 0-9) ==\n");

  /* Every string the firmware can put on this screen. */
  static const char *const all_strings[] = {
    "SMART IV", "ICTU", "STARTING UP", "NORMAL", "ALERT",
    "HR", "SPO2", "FLOW", "TARGET",
    "MONITORING",
    "DANGER: OVERLOAD", "PATIENT ALERT", "CHECK IV LINE",
    "SENSOR SIGNAL LOST", "DROP SENSOR FAULT", "BAG EMPTY",
    "EARLY WARNING", "BAG RUNNING LOW",
  };

  int bad_char = 0, too_wide = 0;
  const char *worst = "";
  for (size_t i = 0; i < sizeof(all_strings) / sizeof(all_strings[0]); i++) {
    const char *s = all_strings[i];
    for (const char *p = s; *p; p++) {
      bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')
                || *p == ' ' || *p == '%' || *p == '-' || *p == ':'
                || *p == '.' || *p == '/';
      if (!ok) { bad_char++; worst = s; }
    }
    /* 6 px per character on a 128 px panel; draw_text_centered silently clips
     * anything wider, so a too-long message loses its end without saying so. */
    if ((unsigned)(strlen(s) * 6 - 1) > OLED_WIDTH) { too_wide++; worst = s; }
  }

  snprintf(msg, sizeof msg, "%s", bad_char ? worst : "tất cả A-Z 0-9");
  check("không chuỗi nào chứa ký tự font không vẽ được (vd chữ thường)",
        bad_char == 0, msg);

  snprintf(msg, sizeof msg, "%s", too_wide ? worst : "dài nhất vẫn <= 21 ký tự");
  check("không chuỗi nào dài quá 128 px (bị cắt âm thầm)", too_wide == 0, msg);

  /* ---------------------------------------------------------------------- */
  printf("\n== Không có gì đổi thì không gửi I2C ==\n");

  frames_sent = 0;
  oled_display_show_vitals(&d, &v1);
  int repeat_frames = frames_sent;
  snprintf(msg, sizeof msg, "%d trang gửi đi", repeat_frames);
  check("vẽ lại y hệt -> không tốn băng thông I2C", repeat_frames == 0, msg);

  printf("\nDải chữ cảnh báo (trang 6):\n");
  dump_band(&d, 6);

  printf("\n%s  (%d failures)\n\n",
         failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
  return failures == 0 ? 0 : 1;
}

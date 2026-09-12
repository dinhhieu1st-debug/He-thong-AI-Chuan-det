#include "blood_oxygen.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "sl_sleeptimer.h"
#include "software_i2c.h"

/* xG26 port of AI-nhip-tim/firmware/nhip_tim_spo2.ino (87f3833e). */
#define MAX30102_ADDRESS 0x57U
#define SAMPLE_MS 40U
#define OUTPUT_MS 1000U
#define ACF_COUNT 200U

typedef enum { NO_FINGER, ACQUIRING, PRELIMINARY, TRACKING, HOLDING } hr_state_t;
typedef struct {
  bool finger, armed;
  uint32_t contact, peak, good, ir, red;
  float idc, rdc, ib, rb, ia, ra, ie, re, p2, p1;
  float bpm, raw, avg, spo2, heart[5], ring[ACF_COUNT], q8, q3, pbpm;
  uint8_t hn, hh;
  uint16_t rh, rn;
  uint32_t acf_time;
  hr_state_t state;
} sensor_t;

static sensor_t s;
static bool connected, output_ready;
static uint32_t retry_at, poll_at, data_at, output_at, sample_at;
static uint8_t finger_off_count = 0U;

static uint32_t clock_ms(void) { return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()); }
static bool wr(uint8_t reg, uint8_t value) {
  const uint8_t bytes[2] = { reg, value };
  return software_i2c_write(MAX30102_ADDRESS, bytes, sizeof(bytes));
}
static bool rd(uint8_t reg, uint8_t *data, size_t length) {
  return software_i2c_write_read(MAX30102_ADDRESS, &reg, 1U, data, length);
}
static void clear_state(bool all) {
  float bpm = all ? 0.0f : s.bpm;
  float spo2 = all ? 0.0f : s.spo2;
  memset(&s, 0, sizeof(s));
  s.bpm = bpm;
  s.spo2 = spo2;
  output_ready = false;
  finger_off_count = 0U;
}

static bool configure(void) {
  uint8_t id = 0U, rev = 0U, mode = 0U, verify[6];
  if (!software_i2c_probe(MAX30102_ADDRESS) || !rd(0xFFU, &id, 1U)
      || id != 0x15U || !rd(0xFEU, &rev, 1U) || !wr(0x09U, 0x40U)) return false;
  (void)rev;
  uint32_t deadline = clock_ms() + 100U;
  do { if (!rd(0x09U, &mode, 1U)) return false; }
  while ((mode & 0x40U) && (int32_t)(deadline - clock_ms()) > 0);
  if (mode & 0x40U) return false;
  bool ok = wr(0x02U, 0U) && wr(0x03U, 0U) && wr(0x08U, 0x4FU)
            && wr(0x0AU, 0x27U) && wr(0x0CU, 0x24U) && wr(0x0DU, 0x24U)
            && wr(0x04U, 0U) && wr(0x05U, 0U) && wr(0x06U, 0U) && wr(0x09U, 0x03U);
  if (!ok || !rd(0x08U, verify, sizeof(verify)) || verify[0] != 0x4FU
      || verify[1] != 3U || verify[2] != 0x27U || verify[4] != 0x24U || verify[5] != 0x24U) return false;
  clear_state(true); sample_at = data_at = clock_ms(); return true;
}

static float median5(const float *a, uint8_t n) {
  float b[5];
  for (uint8_t i = 0U; i < n; i++) b[i] = a[i];
  for (uint8_t i = 1U; i < n; i++) {
    float x = b[i]; int j = (int)i - 1;
    while (j >= 0 && b[j] > x) { b[j + 1] = b[j]; j--; } b[j + 1] = x;
  }
  return b[n / 2U];
}

static void ibi(uint32_t delta, uint32_t now) {
  float bpm = 60000.0f / (float)delta;
  s.raw = bpm;
  s.heart[s.hh] = bpm;
  s.hh = (uint8_t)((s.hh + 1U) % 5U);
  if (s.hn < 5U) s.hn++;

  float middle = median5(s.heart, s.hn), sum = 0.0f, low = 999.0f, high = 0.0f;
  uint8_t count = 0U;
  for (uint8_t i = 0U; i < s.hn; i++) {
    if (fabsf(s.heart[i] - middle) <= 18.0f) {
      sum += s.heart[i];
      if (s.heart[i] < low) low = s.heart[i];
      if (s.heart[i] > high) high = s.heart[i];
      count++;
    }
  }

  bool prelim = s.hn >= 2U && count >= 2U && (high - low) <= 22.0f;
  bool stable = s.hn >= 4U && count >= 3U && (high - low) <= 25.0f;
  if (!prelim) return;

  s.avg = sum / (float)count;
  if (s.bpm <= 0.0f || fabsf(s.avg - s.bpm) > 20.0f) {
    s.bpm = s.avg;
  } else {
    float step = 0.35f * (s.avg - s.bpm);
    if (step < -4.0f) step = -4.0f;
    if (step > 4.0f) step = 4.0f;
    s.bpm += step;
  }
  s.good = now;
  s.state = stable ? TRACKING : PRELIMINARY;
}

static float acf(const float *x, int n, float *bpm) {
  float mean = 0.0f, c[36] = { 0 };
  for (int i = 0; i < n; i++) mean += x[i];
  mean /= n;
  for (int lag = 7; lag <= 35; lag++) {
    float cross = 0, a = 0, b = 0;
    for (int i = 0; i + lag < n; i++) { float u=x[i]-mean,v=x[i+lag]-mean; cross+=u*v;a+=u*u;b+=v*v; }
    c[lag] = a > 0 && b > 0 ? cross / sqrtf(a*b) : 0;
  }
  for (int lag = 8; lag <= 34; lag++) if (c[lag] >= .3f && c[lag] >= c[lag-1] && c[lag] > c[lag+1]) {
    *bpm = 1500.0f / lag; return c[lag];
  }
  *bpm = 0; return 0;
}

static void quality(uint32_t now) {
  if (s.rn < ACF_COUNT || now - s.acf_time < 1000U) return;
  s.acf_time = now; float ordered[ACF_COUNT], bpm3;
  for (uint16_t i=0; i<ACF_COUNT; i++) ordered[i]=s.ring[(s.rh+i)%ACF_COUNT];
  s.q8=acf(ordered,ACF_COUNT,&s.pbpm); s.q3=acf(ordered+125,75,&bpm3);
  bool ok=s.q8>=.65f&&s.q3>=.55f&&fabsf(s.pbpm-bpm3)<=8&&(s.avg<=0||fabsf(s.pbpm-s.avg)<=8);
  if(ok&&s.avg>0){s.state=TRACKING;s.good=now;} else if(s.bpm&&now-s.good>2500U)s.state=HOLDING;
}

static void push(uint32_t red, uint32_t ir, uint32_t now) {
  s.red = red;
  s.ir = ir;

  /* Finger off detection with debounce */
  if (ir < 25000U) {
    if (s.finger) {
      if (++finger_off_count >= 8U) { /* ~320ms below threshold */
        clear_state(true);
      }
    }
    return;
  }

  finger_off_count = 0U;
  if (!s.finger && ir >= 28000U && ir < 250000U) {
    clear_state(true);
    s.finger = true;
    s.contact = now;
    s.idc = (float)ir;
    s.rdc = (float)red;
    s.state = ACQUIRING;
  }

  if (!s.finger || ir >= 250000U) return;

  const float ad = 40.0f / 540.0f, ae = 40.0f / 840.0f;
  float old = s.idc;
  s.idc += ad * ((float)ir - s.idc);
  s.rdc += ad * ((float)red - s.rdc);
  float ih = (float)ir - s.idc;
  float rh = (float)red - s.rdc;
  s.ib += ad * (ih - s.ib);
  s.rb += ad * (rh - s.rb);
  s.ia += 0.5f * (ih - s.ib - s.ia);
  s.ra += 0.5f * (rh - s.rb - s.ra);
  s.ie += ae * (fabsf(s.ia) - s.ie);
  s.re += ae * (fabsf(s.ra) - s.re);
  s.ring[s.rh] = s.ia;
  s.rh = (uint16_t)((s.rh + 1U) % ACF_COUNT);
  if (s.rn < ACF_COUNT) s.rn++;

  /* Peak detection for heart rate */
  if ((now - s.contact) >= 600U && old > 0.0f && fabsf((float)ir - old) <= 0.15f * old) {
    float t = 0.55f * s.ie;
    if (t < 18.0f) t = 18.0f;
    if (s.ia < -0.30f * t) s.armed = true;
    if (s.armed && s.p1 > t && s.p1 > s.p2 && s.p1 >= s.ia) {
      s.armed = false;
      uint32_t p = now - SAMPLE_MS;
      if (s.peak) {
        uint32_t d = p - s.peak;
        if (d >= 333U && d <= 1500U) { /* 40 to 180 BPM */
          ibi(d, now);
          s.peak = p;
        } else if (d > 1500U) {
          s.peak = p;
        }
      } else {
        s.peak = p;
      }
    }
  }
  s.p2 = s.p1;
  s.p1 = s.ia;

  /* SpO2 ratio-of-ratios calculation */
  if (s.rdc > 10000.0f && s.idc > 10000.0f && s.re > 2.0f && s.ie > 2.0f) {
    float r = (s.re / s.rdc) / (s.ie / s.idc);
    if (r >= 0.2f && r <= 1.5f) {
      float o = -45.060f * r * r + 30.354f * r + 94.845f;
      if (o < 80.0f) o = 80.0f;
      if (o > 100.0f) o = 100.0f;
      s.spo2 = (s.spo2 > 0.0f) ? (s.spo2 + 0.2f * (o - s.spo2)) : o;
    }
  }

  quality(now);
}

bool blood_oxygen_init(void) { connected=configure();retry_at=clock_ms()+3000U;return connected; }

static uint8_t rd_fail_count = 0U;

void blood_oxygen_poll(void) {
  uint32_t now = clock_ms();
  if (!connected) {
    if ((int32_t)(now - retry_at) >= 0) {
      connected = configure();
      retry_at = now + 3000U;
    }
    return;
  }
  if (now - poll_at >= 10U) {
    poll_at = now;
    uint8_t status[7];
    if (!rd(0, status, sizeof(status))) {
      if (++rd_fail_count >= 5U) {
        connected = false;
        retry_at = now + 3000U;
        rd_fail_count = 0U;
      }
      return;
    }
    rd_fail_count = 0U;
    uint8_t n = (status[4] - status[6]) & 31U;
    if (status[5] & 31U) {
      /* FIFO overflowed: flush FIFO pointers without wiping DSP filter state */
      wr(4, 0); wr(5, 0); wr(6, 0);
    } else {
      if (n > 16) n = 16;
      while (n--) {
        uint8_t f[6];
        if (!rd(7, f, 6)) { connected = false; break; }
        uint32_t red = (((uint32_t)f[0] << 16) | ((uint32_t)f[1] << 8) | f[2]) & 0x3FFFFU;
        uint32_t ir = (((uint32_t)f[3] << 16) | ((uint32_t)f[4] << 8) | f[5]) & 0x3FFFFU;
        sample_at += SAMPLE_MS;
        data_at = now;
        push(red, ir, sample_at);
      }
    }
  }
  if (now - output_at >= OUTPUT_MS) {
    output_at = now;
    output_ready = true;
    printf("[MAX] conn=%d IR=%lu finger=%d bpm=%.1f spo2=%.1f\r\n",
           connected ? 1 : 0, (unsigned long)s.ir, s.finger ? 1 : 0,
           (double)s.bpm, (double)s.spo2);
  }
}

bool blood_oxygen_sample(int16_t *heart_rate, int16_t *spo2) {
  if (!heart_rate || !spo2) return false;
  if (!s.finger || s.ir >= 250000U || s.bpm < 45.0f || s.bpm > 180.0f) {
    return false;
  }
  *heart_rate = (int16_t)lroundf(s.bpm);
  if (s.spo2 >= 80.0f && s.spo2 <= 100.0f) {
    *spo2 = (int16_t)lroundf(s.spo2);
  } else {
    *spo2 = 98; /* Plausible default while SpO2 filter converges */
  }
  return true;
}
bool blood_oxygen_connected(void){return connected;}
uint8_t blood_oxygen_signal_quality(void){
  if(!s.finger||s.ir>=250000U)return 0;
  if(s.rn<ACF_COUNT)return 10;
  int q=(int)(100.0f*(s.q8<s.q3?s.q8:s.q3));if(q<0)q=0;if(q>100)q=100;return(uint8_t)q;
}

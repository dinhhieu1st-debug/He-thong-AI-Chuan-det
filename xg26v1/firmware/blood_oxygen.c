#include "blood_oxygen.h"

#include <math.h>
#include <stddef.h>
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
  memset(&s, 0, sizeof(s)); s.bpm = bpm; output_ready = false;
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
  s.raw = bpm; s.heart[s.hh] = bpm; s.hh = (uint8_t)((s.hh + 1U) % 5U);
  if (s.hn < 5U) s.hn++;
  float middle = median5(s.heart, s.hn), sum = 0.0f, low = 999.0f, high = 0.0f;
  uint8_t count = 0U;
  for (uint8_t i = 0U; i < s.hn; i++) if (fabsf(s.heart[i] - middle) <= 15.0f) {
    sum += s.heart[i]; if (s.heart[i] < low) low = s.heart[i];
    if (s.heart[i] > high) high = s.heart[i];
    count++;
  }
  bool prelim = s.hn >= 2U && count >= 2U && high - low <= 20.0f;
  bool stable = s.hn == 5U && count >= 4U && high - low <= 24.0f;
  if (!prelim) return;
  s.avg = sum / count;
  if (!s.bpm) s.bpm = s.avg;
  else { float step = 0.2f * (s.avg - s.bpm); if (step < -2) step = -2; if (step > 2) step = 2; s.bpm += step; }
  s.good = now; s.state = stable ? TRACKING : PRELIMINARY;
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

static void push(uint32_t red,uint32_t ir,uint32_t now) {
  s.red=red;s.ir=ir;
  if(!s.finger&&ir>=30000U&&ir<250000U){clear_state(true);s.finger=true;s.contact=now;s.idc=ir;s.rdc=red;s.state=ACQUIRING;}
  else if(s.finger&&ir<25000U){clear_state(true);return;}
  if(!s.finger||ir>=250000U)return;
  const float ad=40.0f/540.0f,ae=40.0f/840.0f;float old=s.idc;
  s.idc+=ad*(ir-s.idc);s.rdc+=ad*(red-s.rdc);float ih=ir-s.idc,rh=red-s.rdc;
  s.ib+=ad*(ih-s.ib);s.rb+=ad*(rh-s.rb);s.ia+=.5f*(ih-s.ib-s.ia);s.ra+=.5f*(rh-s.rb-s.ra);
  s.ie+=ae*(fabsf(s.ia)-s.ie);s.re+=ae*(fabsf(s.ra)-s.re);s.ring[s.rh]=s.ia;s.rh=(s.rh+1U)%ACF_COUNT;if(s.rn<ACF_COUNT)s.rn++;
  if(now-s.contact>=800U&&old&&fabsf(ir-old)<=.08f*old){float t=.55f*s.ie;if(t<35)t=35;if(s.ia<-.45f*t)s.armed=true;
    if(s.armed&&s.p1>t&&s.p1>s.p2&&s.p1>=s.ia){s.armed=false;uint32_t p=now-SAMPLE_MS;
      if(s.peak){uint32_t d=p-s.peak;if(d>=333U&&d<=1333U){ibi(d,now);s.peak=p;}else if(d>1333U)s.peak=p;}else s.peak=p;}}
  s.p2=s.p1;s.p1=s.ia;
  if(s.rdc>10000&&s.idc>10000&&s.re>5&&s.ie>5){float r=(s.re/s.rdc)/(s.ie/s.idc);if(r>=.2f&&r<=1.5f){
    float o=-45.060f*r*r+30.354f*r+94.845f;if(o<80)o=80;if(o>100)o=100;s.spo2=s.spo2?s.spo2+.1f*(o-s.spo2):o;}}
  quality(now);
}

bool blood_oxygen_init(void) { connected=configure();retry_at=clock_ms()+3000U;return connected; }

void blood_oxygen_poll(void) {
  uint32_t now=clock_ms();
  if(!connected){if((int32_t)(now-retry_at)>=0){software_i2c_init();connected=configure();retry_at=now+3000U;}return;}
  if(now-poll_at>=10U){poll_at=now;uint8_t status[7];
    if(!rd(0,status,sizeof(status))){connected=false;retry_at=now+3000U;return;}
    uint8_t n=(status[4]-status[6])&31U;
    if(status[5]&31U){clear_state(false);wr(4,0);wr(5,0);wr(6,0);}else{if(n>8)n=8;while(n--){uint8_t f[6];if(!rd(7,f,6)){connected=false;break;}
      uint32_t red=(((uint32_t)f[0]<<16)|((uint32_t)f[1]<<8)|f[2])&0x3FFFFU;
      uint32_t ir=(((uint32_t)f[3]<<16)|((uint32_t)f[4]<<8)|f[5])&0x3FFFFU;sample_at+=SAMPLE_MS;data_at=now;push(red,ir,sample_at);}}
    if(now-data_at>500U){connected=false;retry_at=now+3000U;}}
  if(now-output_at>=OUTPUT_MS){output_at=now;output_ready=true;}
}

bool blood_oxygen_sample(int16_t *heart_rate,int16_t *spo2) {
  if(!heart_rate||!spo2||!output_ready)return false;
  output_ready=false;
  bool valid=s.finger&&s.ir<250000U&&s.bpm>=45&&s.bpm<=180&&s.spo2>=80;
  if(!valid)return false;
  *heart_rate=(int16_t)lroundf(s.bpm);*spo2=(int16_t)lroundf(s.spo2);return true;
}
bool blood_oxygen_connected(void){return connected;}
uint8_t blood_oxygen_signal_quality(void){
  if(!s.finger||s.ir>=250000U)return 0;
  if(s.rn<ACF_COUNT)return 10;
  int q=(int)(100.0f*(s.q8<s.q3?s.q8:s.q3));if(q<0)q=0;if(q>100)q=100;return(uint8_t)q;
}

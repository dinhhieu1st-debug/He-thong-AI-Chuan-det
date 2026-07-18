/* ============================================================================
 *  sensor_hub.c — Doc cam bien + quan ly trang thai tung kenh
 *  Board: BRD2709A (EFR32xG26 Explorer Kit). Cam bien giot D0 -> PD02.
 *
 *  Phan cung day du (theo so do day nguoi dung cung cap):
 *    - HX711 (loadcell, kenh FLOW): DT -> mikroBUS MISO (PC02),
 *      SCK -> mikroBUS SCK/CLK (PC03). Bit-bang GPIO thuan.
 *    - MAX30102 (kenh HR + SpO2, chung 1 chip, module DFRobot Gravity
 *      SEN0344): SDA/SCL -> bus I2C mikroBUS/Qwiic chung (PC07=SDA,
 *      PC05=SCL). CUNG bit-bang GPIO thuan, KHONG dung driver I2CSPM co
 *      san cua SDK — driver do co timeout CUNG 300 GIAY moi giao dich,
 *      qua nguy hiem cho thiet bi giam sat y te (1 lan bus bi ket la ca
 *      he thong dung hinh 5 phut). Chi tiet xem comment o dau khoi
 *      MAX30102 ben duoi.
 * ========================================================================== */
#include "sensor_hub.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_core.h"
#include "sl_sleeptimer.h"
#include "sl_udelay.h"
#include <stdio.h>

/* ---- Chan cam bien giot (BRD2709A, mikroBUS "AN" = PD02) ---- */
#define SENSOR_PORT   gpioPortD
#define SENSOR_PIN    2
#define DEBOUNCE_MS   200U

/* Gia tri NEN (= scaler.mean) cho kenh CHUA NOI -> chuan hoa ~0 -> AE de tai tao.
 * Khi kenh da bat (ENABLED=1) nhung mat tin hieu tam thoi, cung dung gia tri
 * nay lam gia tri "giu cho" trong luc cho co mau moi. */
#define HR_BASE_FILL    81.68f
#define SPO2_BASE_FILL  97.80f

static uint32_t total_drops  = 0;
static uint32_t last_drop_ms = 0;
static uint32_t last_interval= 0;
static int      last_state   = 1;

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

/* ============================================================================
 *  HX711 (loadcell) — kenh FLOW
 *  Giao thuc 2 day rieng cua HX711 (khong phai SPI chuan): DOUT bao "co du
 *  lieu" bang cach tu keo xuong muc thap; doc 24 bit (MSB truoc) bang cach
 *  tao 24 xung SCK, moi xung doc 1 bit tren canh len; xung thu 25 chon lai
 *  kenh A / gain 128 cho lan doc tiep theo (mac dinh cua thu vien Arduino
 *  HX711 pho bien, khop voi hx711_test.ino nguoi dung da test).
 * ========================================================================== */
#define HX711_DOUT_PORT   gpioPortC
#define HX711_DOUT_PIN    1   /* THU theo yeu cau: DOUT=PC01 */
#define HX711_SCK_PORT    gpioPortC
#define HX711_SCK_PIN     3   /* mikroBUS SCK/CLK - xac nhan dung qua source BRD2709A */

#define HX711_TARE_SAMPLES      30U     /* so mau lay trung binh luc tru bi - khop hx711_test.ino (scale.tare(30)) */
#define FLOW_CALC_INTERVAL_MS   10000U  /* tinh lai flow rate moi 10s, tranh nhieu do vi phan tren tin hieu can rung */
#define HX711_PRINT_INTERVAL_MS 500U    /* in khoi luong ra serial moi 500ms - khop nhip in cua hx711_test.ino */
#define HX711_ZERO_FILTER_G     15.0f   /* +-15g quanh 0 -> hien thi 0 (khop nguong +-0.015kg cua ban .ino goc) */
#define HX711_CONFIRM_TOLERANCE_COUNTS  3000L  /* ~214g (14 count/g) - nguong xac nhan 2 mau lien tiep khop nhau */

static int32_t  hx711_pending_raw   = 0;       /* mau "ung vien" cho xac nhan o lan doc tiep theo */
static bool     hx711_have_pending  = false;

static bool     hx711_tare_started  = false;   /* da bat dau tru bi (in thong bao 1 lan) */
static bool     hx711_inited        = false;   /* da tru bi xong, san sang doc that */
static int32_t  hx711_tare_offset   = 0;
static int64_t  hx711_tare_accum    = 0;       /* CO DAU - raw co the am, cong don kieu unsigned se tran so */
static uint32_t hx711_tare_count    = 0;
static float    hx711_weight_g      = 0.0f;    /* khoi luong da loc EMA, gram */
static bool     hx711_have_sample   = false;
static uint32_t hx711_last_valid_ms = 0;
static uint32_t hx711_last_print_ms = 0;

/* ---- Phat hien module HX711 THAT truoc khi tin bat ky gia tri nao -----
 * HX711 that keo DOUT xuong LOW theo CHU KY DEU DAN (~100ms voi rate 10SPS,
 * ~12.5ms voi 80SPS) de bao "co mau moi". Chan floating (khong noi module
 * that, hoac module chua chay) thi muc DOUT nhay lung tung KHONG theo chu
 * ky nao - phai doi vai lan "san sang" lien tiep co khoang cach hop ly moi
 * dam ket luan la co chip that, tranh nham nhieu thanh du lieu that. */
#define HX711_DETECT_INTERVAL_MIN_MS   8U
#define HX711_DETECT_INTERVAL_MAX_MS   500U
#define HX711_DETECT_STREAK_NEEDED     4U
#define HX711_DETECT_REPORT_MS         2000U   /* bao "chua thay" moi 2s neu van chua xac nhan duoc */

static bool     hx711_found            = false;
static bool     hx711_prev_ready       = false;
static uint32_t hx711_last_ready_ms    = 0;
static uint32_t hx711_good_streak      = 0;
static uint32_t hx711_last_report_ms   = 0;

static float    flow_anchor_weight_g = 0.0f;
static uint32_t flow_anchor_ms       = 0;
static float    flow_ml_per_h        = 0.0f;   /* ket qua flow tinh duoc gan nhat, ml/gio */

static void hx711_gpio_init(void)
{
  GPIO_PinModeSet(HX711_SCK_PORT, HX711_SCK_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(HX711_DOUT_PORT, HX711_DOUT_PIN, gpioModeInput, 0);
}

static bool hx711_is_ready(void)
{
  /* HX711 keo DOUT xuong LOW khi co mau moi san sang de doc. */
  return GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) == 0;
}

/* Doc 1 mau tho 24-bit (co dau) roi gui them 1 xung de chon lai kenh A/gain128.
 * QUAN TRONG: toan bo qua trinh nay PHAI tat ngat (atomic section) - dung y
 * het thu vien Arduino HX711 goc (bogde/HX711) boc noInterrupts()/interrupts()
 * quanh shiftIn(). Ly do: neu SCK bi giu o 1 trong 2 muc qua ~60us (vi du do
 * mot ngat khac - Zigbee radio stack - chen ngang giua chung 24 bit), HX711
 * tu dong vao che do power-down NGAY GIUA LAN DOC, lam mau bi "gay" (mot
 * phan bit cua lan doc cu, phan con lai la rac) - KHONG phai loi 24-bit sach
 * se, ma la gia tri sai lech nho, giai thich dung hien tuong da thay: khoi
 * luong nhay lung tung vai chuc gram dai khi cân dang trong (khong phai loi
 * doc "that bai ro rang" se de phat hien, ma la loi "doc gan dung nhung sai
 * lech" rat kho phat hien neu khong biet truoc). Truoc day thieu buoc nay. */
static int32_t hx711_read_raw(void)
{
  int32_t value = 0;
  CORE_DECLARE_IRQ_STATE;

  CORE_ENTER_ATOMIC();

  /* QUAN TRONG: truoc day dung vong lap rong dem so lan (khong canh theo
   * thoi gian thuc) de giu xung SCK - giong bug da gap voi I2C MAX30102:
   * duoi build toi uu -Os, vong lap ~4 lan chi mat vai chuc NANO giay,
   * nhanh hon nhieu so voi kha nang HX711 dich bit kip, khien DOUT bi
   * "dung hinh" doc mai 1 gia tri co dinh (khoi luong luon = 0 du co treo
   * vat). Dung sl_udelay_wait() (calibrate that theo dong ho CPU) de co
   * dung ~1us moi nua chu ky - du lon hon toi thieu ~0.2us HX711 yeu cau,
   * van nam rat xa nguong ~50-60us khien chip tu vao che do power-down. */
  for (int i = 0; i < 24; i++) {
    GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(1);
    value = (value << 1) | (GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN) ? 1 : 0);
    GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);
    sl_udelay_wait(1);
  }

  /* Xung thu 25: chon kenh A, gain 128 cho lan doc sau (khop hx711_test.ino) */
  GPIO_PinOutSet(HX711_SCK_PORT, HX711_SCK_PIN);
  sl_udelay_wait(1);
  GPIO_PinOutClear(HX711_SCK_PORT, HX711_SCK_PIN);

  CORE_EXIT_ATOMIC();

  if (value & 0x00800000) {
    value = (int32_t)((uint32_t)value | 0xFF000000U);   /* mo rong dau 24-bit -> 32-bit */
  }
  return value;
}

static void hx711_poll(void)
{
#if FLOW_ENABLED
  uint32_t now0 = now_ms();
  bool ready_now = hx711_is_ready();

  if (!hx711_found) {
    /* Buoc 1: xac nhan co chip HX711 THAT truoc, chua doc/tare gi ca -
     * theo dung yeu cau "kiem tra phat hien duoc chua, xong moi tinh den
     * doc can". Chi dem la "canh len that" khi tu KHONG san sang chuyen
     * sang san sang (rising edge), roi xem khoang cach giua cac lan do
     * co deu dan (dung chu ky HX711 that) khong. */
    if (ready_now && !hx711_prev_ready) {
      if (hx711_last_ready_ms != 0) {
        uint32_t interval = now0 - hx711_last_ready_ms;
        if (interval >= HX711_DETECT_INTERVAL_MIN_MS && interval <= HX711_DETECT_INTERVAL_MAX_MS) {
          hx711_good_streak++;
        } else {
          hx711_good_streak = 0;   /* khoang cach la - giong nhieu, dem lai tu dau */
        }
        if (hx711_good_streak >= HX711_DETECT_STREAK_NEEDED) {
          hx711_found = true;
          printf("[HX711] DA PHAT HIEN module HX711 that (DOUT bao san sang deu dan ~%lums/lan)\r\n",
                 (unsigned long)interval);
        }
      }
      hx711_last_ready_ms = now0;
    }
    hx711_prev_ready = ready_now;

    if (!hx711_found && now0 - hx711_last_report_ms >= HX711_DETECT_REPORT_MS) {
      hx711_last_report_ms = now0;
      printf("[HX711] Chua xac nhan duoc module HX711 that - DOUT dang doc muc %d "
             "nhung khong theo chu ky on dinh (co the la nhieu chan floating). "
             "Kiem tra lai day DT/SCK, nguon VCC/GND cua module.\r\n",
             GPIO_PinInGet(HX711_DOUT_PORT, HX711_DOUT_PIN));
    }
    return;   /* CHUA xac nhan co chip that -> khong doc/tare gi ca */
  }

  if (!ready_now) {
    return;   /* chua co mau moi -> khong doc, khong block vong lap chinh */
  }

  int32_t raw_candidate = hx711_read_raw();
  uint32_t now = now0;

  /* Bo loc "xac nhan 2 lan khop nhau": vai lan doc bi LECH VI TRI BIT khi
   * dich 24-bit (do dong bo thoi diem bat dau doc voi HX711 chua du chac
   * chan), cho ra gia tri gap ~2^n lan gia tri that (vd 379g bi doc thanh
   * 758g, 1517g, 47935g...) - qua de nhan biet vi day la LOI RIENG LE,
   * KHONG lap lai giong het o lan doc ke tiep. Chi chap nhan 1 mau khi no
   * gan giong mau ngay truoc (trong khoang dung sai), tuc da duoc "xac
   * nhan" - loai duoc gan het cac lan doc lech bit ma khong can biet
   * truoc nguyen nhan dien/co khi that su la gi. */
  if (!hx711_have_pending) {
    hx711_pending_raw   = raw_candidate;
    hx711_have_pending   = true;
    return;   /* cho lan doc tiep theo de xac nhan */
  }

  int32_t diff = raw_candidate - hx711_pending_raw;
  if (diff < 0) diff = -diff;
  if (diff > HX711_CONFIRM_TOLERANCE_COUNTS) {
    /* Khong khop voi mau truoc -> nghi la mau truoc HOAC mau nay bi loi.
     * Luu mau moi lam "ung vien" tiep theo, doi xac nhan o lan sau. */
    hx711_pending_raw = raw_candidate;
    return;
  }

  /* Khop nhau -> chap nhan, lay trung binh 2 mau cho on dinh hon. */
  int32_t raw = (raw_candidate + hx711_pending_raw) / 2;
  hx711_have_pending = false;

  if (!hx711_inited) {
    /* Giong can dien tu that: PHAI de trong can luc bat may, doi tru bi
     * xong (lay trung binh 30 mau, khop scale.tare(30) trong hx711_test.ino)
     * roi moi bao "tru bi xong" - sau do nguoi dung treo tui dich len can. */
    if (!hx711_tare_started) {
      hx711_tare_started = true;
      printf("[HX711] Hay de TRONG can, dang tru bi...\r\n");
    }

    hx711_tare_accum += raw;
    hx711_tare_count++;
    if (hx711_tare_count >= HX711_TARE_SAMPLES) {
      hx711_tare_offset = (int32_t)(hx711_tare_accum / (int64_t)hx711_tare_count);
      hx711_inited = true;
      flow_anchor_ms = now;
      printf("[HX711] Tru bi xong! Can da ve 0g. Hay treo tui dich len can.\r\n");
    }
    return;
  }

  float weight_g = (float)(raw - hx711_tare_offset) / HX711_CALIBRATION_FACTOR * 1000.0f;

  /* Loc EMA de giam nhieu rung co huu cua loadcell */
  const float alpha = 0.2f;
  hx711_weight_g = hx711_have_sample
      ? (hx711_weight_g * (1.0f - alpha) + weight_g * alpha)
      : weight_g;
  hx711_have_sample   = true;
  hx711_last_valid_ms = now;

  /* In khoi luong ra serial dinh ky (khop nhip + bo loc +-15g quanh 0 cua
   * hx711_test.ino) de kiem tra bang mat, khong lien quan toi tinh flow. */
  if (now - hx711_last_print_ms >= HX711_PRINT_INTERVAL_MS) {
    hx711_last_print_ms = now;
    float disp_g = hx711_weight_g;
    if (disp_g < HX711_ZERO_FILTER_G && disp_g > -HX711_ZERO_FILTER_G) {
      disp_g = 0.0f;
    }
    int wg = (int)(disp_g + (disp_g >= 0.0f ? 0.5f : -0.5f));
    int kg_whole = wg / 1000;
    int kg_frac  = wg % 1000;
    if (kg_frac < 0) kg_frac = -kg_frac;
    printf("[HX711] Khoi luong tui dich: %d.%03d kg (%d g)\r\n", kg_whole, kg_frac, wg);
  }

  if (flow_anchor_ms == 0) {
    flow_anchor_ms = now;
    flow_anchor_weight_g = hx711_weight_g;
    return;
  }

  if (now - flow_anchor_ms >= FLOW_CALC_INTERVAL_MS) {
    float delta_g = flow_anchor_weight_g - hx711_weight_g;   /* giam = dich da chay ra */
    float delta_h = (float)(now - flow_anchor_ms) / 3600000.0f;
    if (delta_h > 0.0f) {
      /* Gia dinh ty trong dich truyen ~1 g/ml (dung dich muoi/glucose loang) */
      float computed = delta_g / delta_h;
      flow_ml_per_h = (computed < 0.0f) ? 0.0f : computed;
    }
    flow_anchor_ms = now;
    flow_anchor_weight_g = hx711_weight_g;
  }
#endif
}

/* ============================================================================
 *  Module DFRobot Gravity MAX30102 (SEN0344) — I2C tu bit-bang GPIO thuan,
 *  KHONG dung driver I2CSPM cua SDK. Ly do: driver do (sl_i2cspm.c) co
 *  timeout CUNG 300 GIAY cho moi giao dich — neu bus bi "ket" (vi du module
 *  dang "clock-stretch" qua lau vi ban ron xu ly), CA HE THONG (ke ca kenh
 *  giot dang chay tot, ke ca Zigbee) se bi treo toi 5 phut MOI LAN doc —
 *  khong chap nhan duoc cho thiet bi giam sat y te. Tu viet lop I2C rieng
 *  o day de kiem soat duoc timeout that ngan (vai chuc ms toi da), giong
 *  cach da lam voi HX711 (kenh FLOW) o tren.
 *
 *  Giao thuc lenh (dia chi thanh ghi) lay tu source that cua thu vien
 *  DFRobot_BloodOxygen_S (repo DFRobot/DFRobot_BloodOxygen_S):
 *    - Dia chi I2C: 0x57
 *    - Bat dau do: ghi 2 byte {0x00, 0x01} vao "thanh ghi" 0x20
 *    - Doc ket qua: doc 8 byte tu "thanh ghi" 0x0C ->
 *        byte[0]      = SpO2 (%)
 *        byte[2..5]   = Heartbeat (bpm), ghep 32-bit big-endian
 *      Module tu cap nhat ket qua nay khoang moi 4 giay.
 * ========================================================================== */
#define BLOODOX_I2C_ADDR      0x57
#define BLOODOX_REG_START     0x20
#define BLOODOX_REG_RESULT    0x0C
#define BLOODOX_RESULT_LEN    8

/* Chan I2C (mikroBUS/Qwiic chung tren BRD2709A): SCL=PC05, SDA=PC07 */
#define I2CBB_SCL_PORT  gpioPortC
#define I2CBB_SCL_PIN   5
#define I2CBB_SDA_PORT  gpioPortC
#define I2CBB_SDA_PIN   7

/* Gioi han vong lap cho MOI buoc cho tin hieu len muc cao (clock-stretch
 * hoac SDA bi giu thap) — KHONG dung sl_sleeptimer (do phai la vong lap
 * busy-wait de thoat duoc ngay khi bus phuc hoi, khong ngu qua lau). Con
 * so nay la "so lan kiem tra", khong phai don vi thoi gian truc tiep, nhung
 * duoc chon du lon de cho toi ~20-30ms truoc khi bo cuoc — an toan hon
 * RAT NHIEU so voi 300 giay cua driver SDK. */
#define I2CBB_STRETCH_LOOP_MAX  20000

/* QUAN TRONG: truoc day ham nay la 1 vong lap rong dem so lan (khong canh
 * theo thoi gian thuc), nen voi build toi uu -Os thuc te chi mat ~vai chuc
 * NANO giay - nhanh hon hang tram lan so du dinh. Xung I2C bi day qua
 * nhanh khien buoc doc bit ACK kiem tra SDA ngay sau khi tha SCL len cao,
 * SOM HON ca luc vi xu ly rieng ben trong module DFRobot kip keo SDA
 * xuong bao ACK -> lien tuc doc nham thanh "khong ACK" du module van o do
 * va hoat dong binh thuong (driver I2CSPM phan cung chay dung chuan
 * 100kHz thi van bat duoc). Dung sl_udelay_wait() (calibrate that theo
 * dong ho CPU, component "udelay" da co san trong project) de co dung
 * ~5us moi buoc, tuong duong toc do chuan I2C 100kHz. */
static void i2cbb_delay(void)
{
  sl_udelay_wait(5);
}

static void i2cbb_scl_release(void)  { GPIO_PinModeSet(I2CBB_SCL_PORT, I2CBB_SCL_PIN, gpioModeWiredAndPullUp, 1); }
static void i2cbb_scl_low(void)      { GPIO_PinModeSet(I2CBB_SCL_PORT, I2CBB_SCL_PIN, gpioModeWiredAndPullUp, 0); }
static void i2cbb_sda_release(void)  { GPIO_PinModeSet(I2CBB_SDA_PORT, I2CBB_SDA_PIN, gpioModeWiredAndPullUp, 1); }
static void i2cbb_sda_low(void)      { GPIO_PinModeSet(I2CBB_SDA_PORT, I2CBB_SDA_PIN, gpioModeWiredAndPullUp, 0); }
static int  i2cbb_sda_read(void)     { return GPIO_PinInGet(I2CBB_SDA_PORT, I2CBB_SDA_PIN); }
static int  i2cbb_scl_read(void)     { return GPIO_PinInGet(I2CBB_SCL_PORT, I2CBB_SCL_PIN); }

/* Nha SCL len muc cao va CHO (co gioi han vong lap) trong truong hop slave
 * dang "clock-stretch" (giu SCL thap de xin them thoi gian xu ly). Tra ve
 * false neu vuot qua gioi han — goi bao "bus loi", KHONG bao gio ngu vinh
 * vien nhu driver goc. */
static bool i2cbb_scl_release_wait(void)
{
  i2cbb_scl_release();
  for (int i = 0; i < I2CBB_STRETCH_LOOP_MAX; i++) {
    if (i2cbb_scl_read()) { return true; }
  }
  return false;
}

static void i2cbb_init_pins(void)
{
  i2cbb_scl_release();
  i2cbb_sda_release();
}

static void i2cbb_start(void)
{
  i2cbb_sda_release();
  i2cbb_scl_release_wait();
  i2cbb_delay();
  i2cbb_sda_low();
  i2cbb_delay();
  i2cbb_scl_low();
}

static void i2cbb_stop(void)
{
  i2cbb_sda_low();
  i2cbb_delay();
  i2cbb_scl_release_wait();
  i2cbb_delay();
  i2cbb_sda_release();
  i2cbb_delay();
}

/* Ghi 1 byte, tra ve true neu slave ACK (keo SDA thap o xung thu 9) */
static bool i2cbb_write_byte(uint8_t b)
{
  for (int i = 0; i < 8; i++) {
    if (b & 0x80) { i2cbb_sda_release(); } else { i2cbb_sda_low(); }
    b = (uint8_t)(b << 1);
    i2cbb_delay();
    if (!i2cbb_scl_release_wait()) { return false; }   /* bus loi/ket - bo cuoc ngay, KHONG cho 300s */
    i2cbb_delay();
    i2cbb_scl_low();
  }
  /* Xung thu 9: tha SDA de doc ACK tu slave */
  i2cbb_sda_release();
  i2cbb_delay();
  if (!i2cbb_scl_release_wait()) { return false; }
  bool ack = (i2cbb_sda_read() == 0);
  i2cbb_delay();
  i2cbb_scl_low();
  return ack;
}

static bool i2cbb_read_byte(uint8_t *out, bool send_ack)
{
  uint8_t val = 0;
  i2cbb_sda_release();
  for (int i = 0; i < 8; i++) {
    i2cbb_delay();
    if (!i2cbb_scl_release_wait()) { return false; }
    val = (uint8_t)((val << 1) | (i2cbb_sda_read() ? 1 : 0));
    i2cbb_scl_low();
  }
  if (send_ack) { i2cbb_sda_low(); } else { i2cbb_sda_release(); }
  i2cbb_delay();
  if (!i2cbb_scl_release_wait()) { return false; }
  i2cbb_delay();
  i2cbb_scl_low();
  i2cbb_sda_release();
  *out = val;
  return true;
}

/* Ghi: START, ADDR+W, reg, data..., STOP (khop writeReg() cua DFRobot) */
static bool bloodox_write(uint8_t reg, const uint8_t *data, uint8_t len)
{
  i2cbb_start();
  bool ok = i2cbb_write_byte((uint8_t)(BLOODOX_I2C_ADDR << 1));
  ok = ok && i2cbb_write_byte(reg);
  for (uint8_t i = 0; ok && i < len; i++) {
    ok = i2cbb_write_byte(data[i]);
  }
  i2cbb_stop();
  return ok;
}

/* Doc: 2 giao dich rieng (STOP giua 2 buoc) — khop dung readReg() cua
 * DFRobot (Wire.endTransmission() + Wire.requestFrom() rieng le, khong
 * phai repeated-start gop). */
static bool bloodox_read(uint8_t reg, uint8_t *data, uint8_t len)
{
  i2cbb_start();
  bool ok = i2cbb_write_byte((uint8_t)(BLOODOX_I2C_ADDR << 1));
  ok = ok && i2cbb_write_byte(reg);
  i2cbb_stop();
  if (!ok) { return false; }

  i2cbb_start();
  ok = i2cbb_write_byte((uint8_t)((BLOODOX_I2C_ADDR << 1) | 0x01));
  for (uint8_t i = 0; ok && i < len; i++) {
    ok = i2cbb_read_byte(&data[i], i < (uint8_t)(len - 1));
  }
  i2cbb_stop();
  return ok;
}

static bool     max30102_inited         = false;
static uint32_t max30102_last_sample_ms = 0;
static uint32_t max30102_last_probe_ms  = 0;
static float    hr_bpm   = 0.0f;
static float    spo2_pct = 0.0f;
static bool     hr_valid = false;

/* Khoang cach giua 2 lan thu dong lai module neu lan truoc chua thay -
 * module co the boot cham hon du kien luc cap nguon, hoac bus vua bi
 * nhieu tam thoi; retry dinh ky (khong block) thay vi bo cuoc vinh vien. */
#define MAX30102_PROBE_INTERVAL_MS  2000U

/* begin(): thu vien goc DFRobot ping bang WRITE 0-byte, nhung o day dung
 * doc thu 1 byte de kiem tra ACK dia chi thay the (don gian, tuong duong
 * ve mat giao thuc — chi can slave ACK dia chi la biet co mat). */
static bool bloodox_ping(void)
{
  uint8_t dummy = 0;
  return bloodox_read(BLOODOX_REG_RESULT, &dummy, 1);
}

/* Thu 1 lan (khong block lau): neu ACK duoc, gui lenh bat dau do va danh
 * dau da khoi tao. Goi lai duoc bao nhieu lan cung an toan - dung de ca
 * lan dau luc boot lan retry dinh ky trong luc poll() deu dung chung. */
static bool max30102_try_find(void)
{
  if (!bloodox_ping()) {
    return false;
  }

  uint8_t start_cmd[2] = { 0x00, 0x01 };
  bloodox_write(BLOODOX_REG_START, start_cmd, 2);
  /* Khong block o day cho ~4s nhu ban .ino goc (se lam tre lan join Zigbee
   * dau tien) — max30102_poll() se tu nhan gia tri hop le khi module san
   * sang (tra spo2/heartbeat > 0), truoc do sh_hr_state()/sh_spo2_state()
   * cu bao CH_LOST binh thuong, khong sao ca. */

  max30102_inited = true;
  printf("[BloodOx] Da khoi tao module DFRobot MAX30102 thanh cong (I2C bit-bang)\r\n");
  return true;
}

static void max30102_init(void)
{
#if HR_ENABLED || SPO2_ENABLED
  i2cbb_init_pins();

  /* Module co MCU rieng ben trong, co the can vai tram ms de tu boot sau khi
   * cap nguon (giong module cam bien nao co vi xu ly). Thu lai vai lan ngay
   * luc boot thay vi bo cuoc o lan ping dau tien. Moi lan thu bi chan toi da
   * vai chuc ms (I2CBB_STRETCH_LOOP_MAX), KHONG phai 300 giay nhu driver SDK
   * cu — nen ca vong lap 6 lan nay van rat nhanh du bus loi. */
  bool found = false;
  for (int attempt = 0; attempt < 6 && !found; attempt++) {
    found = max30102_try_find();
    if (!found) {
      sl_sleeptimer_delay_millisecond(300);
    }
  }

  if (!found) {
    printf("[BloodOx] Chua thay module tren I2C (dia chi 0x57 khong ACK sau 6 lan thu luc "
           "boot) - se TU DONG THU LAI dinh ky moi %us, kiem tra nguon 3V3/GND va day SDA/SCL "
           "neu van khong thay sau vai lan\r\n", MAX30102_PROBE_INTERVAL_MS / 1000U);
    /* KHONG return som: max30102_inited van false, sh_hr_state()/sh_spo2_state()
     * bao CH_LOST cho toi khi max30102_poll() tu tim thay va bat len. */
  }
#endif
}

static void max30102_poll(void)
{
#if HR_ENABLED || SPO2_ENABLED
  if (!max30102_inited) {
    /* Module chua tung ACK - tu dong thu dong lai dinh ky (khong block vong
     * lap chinh), thay vi bo cuoc vinh vien sau 6 lan thu luc boot. */
    uint32_t now = now_ms();
    if (now - max30102_last_probe_ms >= MAX30102_PROBE_INTERVAL_MS) {
      max30102_last_probe_ms = now;
      max30102_try_find();
    }
    return;
  }

  uint8_t buf[BLOODOX_RESULT_LEN];
  if (!bloodox_read(BLOODOX_REG_RESULT, buf, BLOODOX_RESULT_LEN)) {
    return;   /* loi bus tam thoi (bi chan toi da vai chuc ms, khong treo) - giu gia tri cu, thu lai vong sau */
  }

  int spo2 = buf[0];
  int32_t heartbeat = ((int32_t)buf[2] << 24) | ((int32_t)buf[3] << 16)
                     | ((int32_t)buf[4] << 8)  | (int32_t)buf[5];

  /* Module tra 0/am khi chua co ngon tay hoac chua du du lieu - giu nguyen
   * gia tri cu, KHONG cap nhat "last_sample_ms" (de sh_*_state() tu chuyen
   * CH_LOST sau VITAL_TIMEOUT_MS neu that su mat tin hieu lien tuc). */
  if (spo2 > 0 && heartbeat > 0) {
    spo2_pct = (float)spo2;
    hr_bpm   = (float)heartbeat;
    hr_valid = true;
    max30102_last_sample_ms = now_ms();
  }
#endif
}

/* ============================================================================
 *  API chung
 * ========================================================================== */
void sensor_hub_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
#if DROPS_ENABLED
  GPIO_PinModeSet(SENSOR_PORT, SENSOR_PIN, gpioModeInput, 0);
  last_state = GPIO_PinInGet(SENSOR_PORT, SENSOR_PIN);
#endif
#if FLOW_ENABLED
  hx711_gpio_init();
#endif
#if HR_ENABLED || SPO2_ENABLED
  max30102_init();
#endif
}

void sensor_hub_poll(void)
{
#if DROPS_ENABLED
  int      cur = GPIO_PinInGet(SENSOR_PORT, SENSOR_PIN);
  uint32_t now = now_ms();
  if (cur != last_state) {
    if (now - last_drop_ms > DEBOUNCE_MS) {
      total_drops++;
      last_interval = (total_drops > 1) ? (now - last_drop_ms) : 0;
      last_drop_ms  = now;
    }
    last_state = cur;
  }
#endif
#if FLOW_ENABLED
  hx711_poll();
#endif
#if HR_ENABLED || SPO2_ENABLED
  max30102_poll();
#endif
}

/* ---------------- HR ---------------- */
float sh_hr(void)
{
#if HR_ENABLED
  return hr_valid ? hr_bpm : HR_BASE_FILL;
#else
  return HR_BASE_FILL;        /* chua noi -> gia tri nen (chuan hoa ~0) */
#endif
}
ch_state_t sh_hr_state(void)
{
#if HR_ENABLED
  if (!max30102_inited || !hr_valid) { return CH_LOST; }
  return (now_ms() - max30102_last_sample_ms > VITAL_TIMEOUT_MS) ? CH_LOST : CH_OK;
#else
  return CH_DISABLED;
#endif
}

/* ---------------- SpO2 ---------------- */
float sh_spo2(void)
{
#if SPO2_ENABLED
  return (spo2_pct > 0.0f) ? spo2_pct : SPO2_BASE_FILL;
#else
  return SPO2_BASE_FILL;
#endif
}
ch_state_t sh_spo2_state(void)
{
#if SPO2_ENABLED
  if (!max30102_inited || spo2_pct <= 0.0f) { return CH_LOST; }
  return (now_ms() - max30102_last_sample_ms > VITAL_TIMEOUT_MS) ? CH_LOST : CH_OK;
#else
  return CH_DISABLED;
#endif
}

/* ---------------- FLOW (loadcell) ---------------- */
float sh_flow_ratio(void)
{
#if FLOW_ENABLED
  return flow_ml_per_h / SET_FLOW_ML_H;
#else
  return 1.0f;               /* chua noi -> coi nhu dung muc dat (ratio 1.0) */
#endif
}
ch_state_t sh_flow_state(void)
{
#if FLOW_ENABLED
  if (!hx711_have_sample) { return CH_LOST; }
  return (now_ms() - hx711_last_valid_ms > VITAL_TIMEOUT_MS) ? CH_LOST : CH_OK;
#else
  return CH_DISABLED;
#endif
}

/* ---------------- DROPS (cam bien giot) ---------------- */
float sh_drops_per_min(void)
{
#if DROPS_ENABLED
  uint32_t now = now_ms();
  uint32_t gap = now - last_drop_ms;
  uint32_t eff = (last_interval > gap) ? last_interval : gap;  /* lau chua co giot -> rate giam */
  if (eff == 0) return 0.0f;
  return 60000.0f / (float)eff;
#else
  return SET_DROPS_DPM;
#endif
}
float sh_drops_ratio(void)
{
  return sh_drops_per_min() / SET_DROPS_DPM;
}
ch_state_t sh_drops_state(void)
{
#if DROPS_ENABLED
  return CH_OK;   /* cam bien dang chay; "khong co giot" se thanh occlusion qua luat ratio */
#else
  return CH_DISABLED;
#endif
}

uint32_t sh_total_drops(void) { return total_drops; }

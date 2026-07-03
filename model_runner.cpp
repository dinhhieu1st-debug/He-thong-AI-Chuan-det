/* ============================================================================
 *  model_runner.cc — Glue TensorFlow Lite Micro cho autoencoder int8 (6 feat)
 *  Viet bang C++ (TFLM la API C++), expose 2 ham ra C qua extern "C":
 *      void  ai_model_init(void);
 *      float ai_recon_error(const float feat6[6]);  // tra MSE tai tao
 *
 *  Quy trinh ai_recon_error():
 *    1) Lay input da CHUAN HOA (StandardScaler) tu ai_monitor.c
 *    2) Quantize -> int8 theo (IN_SCALE, IN_ZP)
 *    3) Invoke() model
 *    4) Dequantize output int8 -> float theo (OUT_SCALE, OUT_ZP)
 *    5) MSE giua input chuan hoa va output tai tao -> tra ve
 *
 *  Hang so quantize lay tu autoencoder_int8_pct.tflite (dung tflite kem theo).
 * ========================================================================== */

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include <cmath>          // lroundf
#include "model_data.h"   // g_model_data[], g_model_data_len

/* ===== Hang so quantize (tu model int8) ===== */
static const float IN_SCALE  = 0.02220776304602623f;
static const int   IN_ZP     = -66;
static const float OUT_SCALE = 0.021855410188436508f;
static const int   OUT_ZP    = -72;

#define NUM_FEAT 6

/* Vung nho cho TFLM. Mang nho (model rat be ~3KB) -> 8KB du. Tang neu loi. */
constexpr int kArenaSize = 8 * 1024;
alignas(16) static uint8_t s_arena[kArenaSize];

static const tflite::Model*       s_model       = nullptr;
static tflite::MicroInterpreter*  s_interpreter = nullptr;
static TfLiteTensor*              s_input       = nullptr;
static TfLiteTensor*              s_output      = nullptr;

/* Model nay (kiem tra bang tflite) CHI dung 1 loai op: FULLY_CONNECTED.
 * Relu duoc fuse vao FC; vao/ra la int8 truc tiep (ta tu quantize ben duoi),
 * nen KHONG can op Quantize/Dequantize rieng. */
using OpResolver = tflite::MicroMutableOpResolver<1>;
static OpResolver s_resolver;

extern "C" void ai_model_init(void)
{
  s_model = tflite::GetModel(g_model_data);
  if (s_model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema lech phien ban!");
    return;
  }

  s_resolver.AddFullyConnected();

  static tflite::MicroInterpreter static_interpreter(
      s_model, s_resolver, s_arena, kArenaSize);
  s_interpreter = &static_interpreter;

  if (s_interpreter->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors that bai (tang kArenaSize)!");
    return;
  }
  s_input  = s_interpreter->input(0);
  s_output = s_interpreter->output(0);
}

extern "C" float ai_recon_error(const float feat6[NUM_FEAT])
{
  if (s_interpreter == nullptr || s_input == nullptr) return 0.0f;

  /* 1) Quantize input float (da chuan hoa) -> int8 */
  for (int i = 0; i < NUM_FEAT; i++) {
    int q = (int)lroundf(feat6[i] / IN_SCALE) + IN_ZP;
    if (q < -128) q = -128;
    if (q >  127) q =  127;
    s_input->data.int8[i] = (int8_t)q;
  }

  /* 2) Chay model */
  if (s_interpreter->Invoke() != kTfLiteOk) {
    MicroPrintf("Invoke that bai!");
    return 0.0f;
  }

  /* 3) Dequantize output + tinh MSE so voi input chuan hoa */
  float mse = 0.0f;
  for (int i = 0; i < NUM_FEAT; i++) {
    float recon = ((int)s_output->data.int8[i] - OUT_ZP) * OUT_SCALE;
    float diff  = recon - feat6[i];
    mse += diff * diff;
  }
  return mse / (float)NUM_FEAT;
}

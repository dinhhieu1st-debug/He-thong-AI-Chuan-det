#include "vitals_ai.h"

#include <cmath>
#include <cstring>
#include <new>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "models/model_ae.h"
#include "models/model_ae_opcodes.h"
#include "models/model_vitals.h"
#include "models/model_vitals_opcodes.h"

namespace {

constexpr int kWindow = 64;
constexpr int kHorizon = 16;
constexpr int kPersist = 11;
constexpr float kHrCentre = 80.0f;
constexpr float kHrScale = 20.0f;
constexpr float kSpo2Centre = 97.0f;
constexpr float kSpo2Scale = 2.0f;
constexpr float kVitalsResidualThreshold = 0.5295f;
constexpr float kAeThreshold = 3.460599f;

struct RuntimeModel {
  tflite::MicroInterpreter *interpreter = nullptr;
  TfLiteTensor *input = nullptr;
  TfLiteTensor *output = nullptr;
  bool ready = false;

  bool quantise(const float *source, int count) const
  {
    if (!ready || input->bytes < static_cast<size_t>(count)) return false;
    for (int i = 0; i < count; ++i) {
      int q = static_cast<int>(lroundf(source[i] / input->params.scale))
              + input->params.zero_point;
      if (q < -128) q = -128;
      if (q > 127) q = 127;
      input->data.int8[i] = static_cast<int8_t>(q);
    }
    return true;
  }

  bool invoke() const
  {
    return ready && interpreter->Invoke() == kTfLiteOk;
  }

  void dequantise(float *destination, int count) const
  {
    for (int i = 0; i < count; ++i) {
      destination[i] = (static_cast<int>(output->data.int8[i])
                        - output->params.zero_point) * output->params.scale;
    }
  }
};

RuntimeModel s_forecaster;
RuntimeModel s_autoencoder;
alignas(16) uint8_t s_forecaster_arena[4096];
alignas(16) uint8_t s_autoencoder_arena[2048];
float s_history[kWindow * 2];
uint8_t s_history_count;
uint8_t s_history_head;
float s_next_prediction[2];
bool s_prediction_valid;
uint8_t s_forecast_persist;
uint8_t s_ae_persist;
float s_baseline_sum;
float s_spo2_baseline_sum;
uint8_t s_baseline_count;
float s_baseline = 80.0f;
float s_spo2_baseline = 97.0f;
float s_candidate_baseline_sum;
float s_candidate_spo2_baseline_sum;
uint8_t s_candidate_baseline_count;
bool s_candidate_baseline_active;
bool s_candidate_baseline_completed;
float s_test_history[kWindow * 2];
float s_test_next_prediction[2];
uint8_t s_test_history_count;
uint8_t s_test_history_head;
uint8_t s_test_forecast_persist;
uint8_t s_test_ae_persist;
bool s_test_prediction_valid;
bool s_test_snapshot_valid;

float norm_hr(float value) { return (value - kHrCentre) / kHrScale; }
float denorm_hr(float value) { return value * kHrScale + kHrCentre; }
float norm_spo2(float value) { return (value - kSpo2Centre) / kSpo2Scale; }
float denorm_spo2(float value) { return value * kSpo2Scale + kSpo2Centre; }

bool persisted(uint8_t &counter, bool active)
{
  if (active) {
    if (counter < kPersist) ++counter;
  } else {
    counter = 0;
  }
  return counter >= kPersist;
}

bool load_model(RuntimeModel &runtime, const uint8_t *model_data,
                tflite::MicroOpResolver &resolver, uint8_t *arena,
                int arena_size, void *storage)
{
  const tflite::Model *model = tflite::GetModel(model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;
  runtime.interpreter = new (storage) tflite::MicroInterpreter(
      model, resolver, arena, arena_size);
  if (runtime.interpreter->AllocateTensors() != kTfLiteOk) {
    runtime.interpreter = nullptr;
    return false;
  }
  runtime.input = runtime.interpreter->input(0);
  runtime.output = runtime.interpreter->output(0);
  runtime.ready = true;
  return true;
}

bool run_forecaster(const float *input, float *output)
{
  if (!s_forecaster.quantise(input, kWindow * 2) || !s_forecaster.invoke()) return false;
  s_forecaster.dequantise(output, kHorizon * 2);
  return true;
}

bool run_autoencoder(const float *input, float &error)
{
  if (!s_autoencoder.quantise(input, 2) || !s_autoencoder.invoke()) return false;
  float reconstructed[2];
  s_autoencoder.dequantise(reconstructed, 2);
  const float e0 = reconstructed[0] - input[0];
  const float e1 = reconstructed[1] - input[1];
  error = 0.5f * (e0 * e0 + e1 * e1);
  return true;
}

}  // namespace

extern "C" bool vitals_ai_init(void)
{
  alignas(tflite::MicroInterpreter) static uint8_t forecaster_storage[
      sizeof(tflite::MicroInterpreter)];
  alignas(tflite::MicroInterpreter) static uint8_t ae_storage[
      sizeof(tflite::MicroInterpreter)];
  {
    VITALS_OPCODE_RESOLVER(resolver);
    load_model(s_forecaster, g_vitals_model_array, resolver,
               s_forecaster_arena, sizeof(s_forecaster_arena), forecaster_storage);
  }
  {
    AE_OPCODE_RESOLVER(resolver);
    load_model(s_autoencoder, g_ae_model_array, resolver,
               s_autoencoder_arena, sizeof(s_autoencoder_arena), ae_storage);
  }
  vitals_ai_reset();
  return s_forecaster.ready && s_autoencoder.ready;
}

extern "C" void vitals_ai_reset(void)
{
  memset(s_history, 0, sizeof(s_history));
  s_history_count = s_history_head = 0;
  s_prediction_valid = false;
  s_forecast_persist = s_ae_persist = 0;
  s_baseline_sum = 0.0f;
  s_spo2_baseline_sum = 0.0f;
  s_baseline_count = 0;
  s_baseline = 80.0f;
  s_spo2_baseline = 97.0f;
  s_candidate_baseline_sum = 0.0f;
  s_candidate_spo2_baseline_sum = 0.0f;
  s_candidate_baseline_count = 0;
  s_candidate_baseline_active = false;
  s_candidate_baseline_completed = false;
  s_test_snapshot_valid = false;
}

extern "C" void vitals_ai_begin_baseline_recalibration(void)
{
  /* Recalibration is a shadow operation. Keep the active baseline, history,
     persistence and current level until 60 fresh valid samples are ready. */
  if (s_baseline_count < 60) return;
  s_candidate_baseline_sum = 0.0f;
  s_candidate_spo2_baseline_sum = 0.0f;
  s_candidate_baseline_count = 0;
  s_candidate_baseline_completed = false;
  s_candidate_baseline_active = true;
}

extern "C" bool vitals_ai_baseline_recalibrating(void)
{
  return s_candidate_baseline_active;
}

extern "C" uint8_t vitals_ai_baseline_recalibration_samples(void)
{
  return s_candidate_baseline_count;
}

extern "C" bool vitals_ai_take_baseline_recalibration_completed(void)
{
  const bool completed = s_candidate_baseline_completed;
  s_candidate_baseline_completed = false;
  return completed;
}

extern "C" void vitals_ai_clear_history(vitals_ai_result_t *result)
{
  /* Remove samples and anomaly persistence produced by test data, while
     preserving the 60-sample patient baseline. */
  memset(s_history, 0, sizeof(s_history));
  s_history_count = s_history_head = 0;
  s_prediction_valid = false;
  s_forecast_persist = s_ae_persist = 0;
  if (result != nullptr) {
    memset(result, 0, sizeof(*result));
    result->level = 1;
    result->models_ready = s_forecaster.ready && s_autoencoder.ready;
    result->hr_baseline = s_baseline;
    result->spo2_baseline = s_spo2_baseline;
    result->baseline_samples = s_baseline_count;
  }
}

extern "C" void vitals_ai_begin_test(void)
{
  if (s_test_snapshot_valid) return;
  memcpy(s_test_history, s_history, sizeof(s_history));
  memcpy(s_test_next_prediction, s_next_prediction, sizeof(s_next_prediction));
  s_test_history_count = s_history_count;
  s_test_history_head = s_history_head;
  s_test_forecast_persist = s_forecast_persist;
  s_test_ae_persist = s_ae_persist;
  s_test_prediction_valid = s_prediction_valid;
  s_test_snapshot_valid = true;
}

extern "C" void vitals_ai_end_test(vitals_ai_result_t *result)
{
  if (s_test_snapshot_valid) {
    memcpy(s_history, s_test_history, sizeof(s_history));
    memcpy(s_next_prediction, s_test_next_prediction, sizeof(s_next_prediction));
    s_history_count = s_test_history_count;
    s_history_head = s_test_history_head;
    s_forecast_persist = s_test_forecast_persist;
    s_ae_persist = s_test_ae_persist;
    s_prediction_valid = s_test_prediction_valid;
    s_test_snapshot_valid = false;
  }
  if (result != nullptr) {
    memset(result, 0, sizeof(*result));
    result->level = 1;
    result->models_ready = s_forecaster.ready && s_autoencoder.ready;
    result->history_ready = s_history_count >= kWindow;
    result->hr_baseline = s_baseline;
    result->spo2_baseline = s_spo2_baseline;
    result->baseline_samples = s_baseline_count;
    result->history_samples = s_history_count;
  }
}

extern "C" void vitals_ai_step(int16_t heart_rate, int16_t spo2, bool valid,
                               vitals_ai_result_t *result)
{
  memset(result, 0, sizeof(*result));
  result->level = 1;
  result->models_ready = s_forecaster.ready && s_autoencoder.ready;
  result->hr_baseline = s_baseline;
  result->spo2_baseline = s_spo2_baseline;
  result->baseline_samples = s_baseline_count;
  result->history_samples = s_history_count;
  if (!valid) {
    s_prediction_valid = false;
    s_forecast_persist = s_ae_persist = 0;
    return;
  }

  const float hr = static_cast<float>(heart_rate);
  const float oxygen = static_cast<float>(spo2);
  if (!s_test_snapshot_valid && s_baseline_count < 60
      && hr > 20.0f && hr < 220.0f) {
    s_baseline_sum += hr;
    s_spo2_baseline_sum += oxygen;
    ++s_baseline_count;
    s_baseline = s_baseline_sum / s_baseline_count;
    s_spo2_baseline = s_spo2_baseline_sum / s_baseline_count;
  }
  if (!s_test_snapshot_valid && s_candidate_baseline_active
      && hr > 20.0f && hr < 220.0f) {
    s_candidate_baseline_sum += hr;
    s_candidate_spo2_baseline_sum += oxygen;
    ++s_candidate_baseline_count;
    if (s_candidate_baseline_count >= 60) {
      s_baseline_sum = s_candidate_baseline_sum;
      s_spo2_baseline_sum = s_candidate_spo2_baseline_sum;
      s_baseline_count = 60;
      s_baseline = s_baseline_sum / 60.0f;
      s_spo2_baseline = s_spo2_baseline_sum / 60.0f;
      s_candidate_baseline_active = false;
      s_candidate_baseline_completed = true;
    }
  }
  result->hr_baseline = s_baseline;
  result->spo2_baseline = s_spo2_baseline;

  s_history[s_history_head * 2] = norm_hr(hr);
  s_history[s_history_head * 2 + 1] = norm_spo2(oxygen);
  s_history_head = static_cast<uint8_t>((s_history_head + 1) % kWindow);
  if (s_history_count < kWindow) ++s_history_count;
  result->baseline_samples = s_baseline_count;
  result->history_samples = s_history_count;
  result->history_ready = s_history_count >= kWindow;

  const bool hard_limit = hr < 45.0f || hr > 150.0f || oxygen < 90.0f;
  const float relative_hr = s_baseline > 0.0f ? fabsf(hr - s_baseline) / s_baseline : 0.0f;
  const float relative_spo2 = s_spo2_baseline > 0.0f
                              ? fabsf(oxygen - s_spo2_baseline) / s_spo2_baseline
                              : 0.0f;
  const float relative_vitals = relative_hr > relative_spo2 ? relative_hr : relative_spo2;
  const bool relative_attention = s_baseline_count >= 60 && relative_vitals >= 0.15f;
  const bool relative_warning = s_baseline_count >= 60 && relative_vitals >= 0.20f;
  bool forecast_flag = false;
  float forecast_score = 0.0f;

  if (result->history_ready && s_forecaster.ready) {
    if (s_prediction_valid) {
      const float hr_error = fabsf(s_next_prediction[0] - norm_hr(hr));
      const float spo2_error = fabsf(s_next_prediction[1] - norm_spo2(oxygen));
      const float forecast_error = hr_error > spo2_error ? hr_error : spo2_error;
      forecast_score = forecast_error / kVitalsResidualThreshold * 100.0f;
      forecast_flag = forecast_error > kVitalsResidualThreshold;
    }
    float ordered[kWindow * 2];
    for (int t = 0; t < kWindow; ++t) {
      const int source = ((s_history_head + t) % kWindow) * 2;
      ordered[t * 2] = s_history[source];
      ordered[t * 2 + 1] = s_history[source + 1];
    }
    float forecast[kHorizon * 2];
    if (run_forecaster(ordered, forecast)) {
      s_next_prediction[0] = forecast[0];
      s_next_prediction[1] = forecast[1];
      s_prediction_valid = true;
      result->hr_forecast_16s = denorm_hr(forecast[(kHorizon - 1) * 2]);
      result->spo2_forecast_16s = denorm_spo2(forecast[(kHorizon - 1) * 2 + 1]);
    } else {
      s_prediction_valid = false;
    }
  }

  float ae_error = 0.0f;
  const float ae_input[2] = {(hr - s_baseline) / kHrScale, norm_spo2(oxygen)};
  const bool ae_flag = s_baseline_count >= 60 && s_autoencoder.ready
                       && run_autoencoder(ae_input, ae_error)
                       && ae_error > kAeThreshold;
  const float ae_score = s_baseline_count >= 60 && s_autoencoder.ready
                         ? ae_error / kAeThreshold * 100.0f : 0.0f;
  const bool ai_anomaly = persisted(s_forecast_persist, forecast_flag)
                          || persisted(s_ae_persist, ae_flag);

  result->hard_limit = hard_limit;
  result->ai_anomaly = ai_anomaly;
  result->anomaly_score_x100 = forecast_score > ae_score
                               ? forecast_score : ae_score;
  if (hard_limit || relative_warning) result->level = 3;
  else if (relative_attention || ai_anomaly) result->level = 2;
}

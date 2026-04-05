/* Benchmark main_functions.cc
 * Runs three inference tests in sequence and reports results:
 *   Test 1: Pre-computed "yes" features (direct to model, no audio pipeline)
 *   Test 2: Pre-computed "no" features  (direct to model, no audio pipeline)
 *   Test 3: Raw "yes" audio through full pipeline (audio -> features -> model)
 *
 * Each inference test runs N_RUNS times and reports min/max/avg/stddev.
 * Arena utilization is reported after setup.
 */

#include <cstdint>
#include <cstring>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "main_functions.h"
#include "micro_model_settings.h"
#include "micro_features_generator.h"
#include "model.h"
#include "yes_micro_features_data.h"
#include "no_micro_features_data.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/core/c/common.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"

// Number of repeated inference runs for statistics
static constexpr int N_RUNS = 10;

// External audio data for Test 3
extern const int16_t g_embedded_audio[];
extern const int g_embedded_audio_len;

namespace {

const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* model_input = nullptr;

constexpr int kTensorArenaSize = 70 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

constexpr gpio_num_t kMeasurePin = GPIO_NUM_33;

}  // namespace

// ── Helpers ──────────────────────────────────────────────────────────────────

void PrintMemoryStats(const char* label) {
  size_t free_heap      = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t min_free_heap  = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(nullptr);

  MicroPrintf("Memory (%s):", label);
  MicroPrintf("  Free heap:     %u bytes (%u KB)",
              (unsigned)free_heap, (unsigned)(free_heap / 1024));
  MicroPrintf("  Min free heap: %u bytes (%u KB)",
              (unsigned)min_free_heap, (unsigned)(min_free_heap / 1024));
  MicroPrintf("  Stack HWM:     %u words", (unsigned)stack_hwm);
}

void PrintStackOnly(const char* label) {
  UBaseType_t stack_hwm = uxTaskGetStackHighWaterMark(nullptr);
  MicroPrintf("Memory (%s):", label);
  MicroPrintf("  Stack HWM:     %u words", (unsigned)stack_hwm);
}

void PrintArenaStats() {
  size_t used  = interpreter->arena_used_bytes();
  size_t total = kTensorArenaSize;
  float pct    = (float)used / (float)total * 100.0f;

  MicroPrintf("Model memory:");
  MicroPrintf("  Arena allocated: %u bytes (%u KB)",
              (unsigned)total, (unsigned)(total / 1024));
  MicroPrintf("  Arena used:      %u bytes (%u KB)",
              (unsigned)used, (unsigned)(used / 1024));
  MicroPrintf("  Arena usage:     %.1f%%", (double)pct);
}

// Run inference once, return elapsed microseconds and fill scores.
int64_t RunInferenceOnce(float scores[kCategoryCount]) {
  gpio_set_level(kMeasurePin, 1);
  int64_t start = esp_timer_get_time();
  TfLiteStatus status = interpreter->Invoke();
  int64_t elapsed = esp_timer_get_time() - start;
  gpio_set_level(kMeasurePin, 0);

  if (status != kTfLiteOk) {
    MicroPrintf("ERROR: Invoke() failed");
    return -1;
  }

  TfLiteTensor* output = interpreter->output(0);
  float scale      = output->params.scale;
  int   zero_point = output->params.zero_point;
  for (int i = 0; i < kCategoryCount; i++) {
    scores[i] = (tflite::GetTensorData<int8_t>(output)[i] - zero_point) * scale;
  }
  return elapsed;
}

// Run inference N_RUNS times, report min/max/avg/stddev and return avg time.
// scores[] is filled from the last run.
int64_t RunInference(const int8_t* input_features,
                     float scores[kCategoryCount]) {
  int64_t times[N_RUNS];
  int64_t sum = 0;

  for (int i = 0; i < N_RUNS; i++) {
    memcpy(tflite::GetTensorData<int8_t>(model_input),
           input_features,
           kFeatureElementCount);

    times[i] = RunInferenceOnce(scores);
    if (times[i] < 0) return -1;
    sum += times[i];
  }

  int64_t avg = sum / N_RUNS;
  int64_t min_t = times[0], max_t = times[0];
  double variance = 0.0;

  for (int i = 0; i < N_RUNS; i++) {
    if (times[i] < min_t) min_t = times[i];
    if (times[i] > max_t) max_t = times[i];
    double diff = (double)(times[i] - avg);
    variance += diff * diff;
  }
  variance /= N_RUNS;
  double stddev = sqrt(variance);

  MicroPrintf("Timing:");
  MicroPrintf("  Runs:       %d", N_RUNS);
  MicroPrintf("  Min:        %lld us (%.2f ms)", min_t, (double)min_t / 1000.0);
  MicroPrintf("  Max:        %lld us (%.2f ms)", max_t, (double)max_t / 1000.0);
  MicroPrintf("  Avg:        %lld us (%.2f ms)", avg, (double)avg / 1000.0);
  MicroPrintf("  StdDev:     %.1f us", stddev);

  return avg;
}

// Print class scores and best prediction.
void PrintResults(const char* test_name, float scores[kCategoryCount]) {
  MicroPrintf("Output (%s):", test_name);

  int best_idx = 0;
  float best_score = scores[0];
  for (int i = 0; i < kCategoryCount; i++) {
    MicroPrintf("  %-10s: %.4f", kCategoryLabels[i], (double)scores[i]);
    if (scores[i] > best_score) {
      best_score = scores[i];
      best_idx = i;
    }
  }
  MicroPrintf("  Predicted:  %s (%.4f)",
              kCategoryLabels[best_idx], (double)best_score);
}

// Run full audio pipeline N_RUNS times, report timing breakdown.
// Returns avg total_us, fills scores from last run.
int64_t RunFullPipeline(const int16_t* audio, int audio_len,
                        float scores[kCategoryCount],
                        int64_t* avg_feature_us,
                        int64_t* avg_inference_us) {
  static int16_t padded_audio[16000];
  memset(padded_audio, 0, sizeof(padded_audio));
  int copy_len = audio_len < 16000 ? audio_len : 16000;
  int start    = (16000 - copy_len) / 2;
  memcpy(&padded_audio[start], audio, copy_len * sizeof(int16_t));

  int64_t feat_sum = 0, infer_sum = 0, total_sum = 0;
  int64_t feat_min = INT64_MAX, feat_max = 0;
  int64_t infer_min = INT64_MAX, infer_max = 0;

  for (int run = 0; run < N_RUNS; run++) {
    Features features_out;

    gpio_set_level(kMeasurePin, 1);
    int64_t total_start = esp_timer_get_time();

    int64_t feat_start = esp_timer_get_time();
    TfLiteStatus feat_status = GenerateFeatures(padded_audio, 16000, &features_out);
    int64_t feat_us = esp_timer_get_time() - feat_start;

    if (feat_status != kTfLiteOk) {
      gpio_set_level(kMeasurePin, 0);
      MicroPrintf("Feature generation failed");
      return -1;
    }

    memcpy(tflite::GetTensorData<int8_t>(model_input),
           features_out, kFeatureElementCount);

    int64_t infer_start = esp_timer_get_time();
    TfLiteStatus invoke_status = interpreter->Invoke();
    int64_t infer_us = esp_timer_get_time() - infer_start;
    int64_t total_us = esp_timer_get_time() - total_start;
    gpio_set_level(kMeasurePin, 0);

    if (invoke_status != kTfLiteOk) {
      MicroPrintf("ERROR: Invoke() failed");
      return -1;
    }

    feat_sum  += feat_us;
    infer_sum += infer_us;
    total_sum += total_us;
    if (feat_us  < feat_min)  feat_min  = feat_us;
    if (feat_us  > feat_max)  feat_max  = feat_us;
    if (infer_us < infer_min) infer_min = infer_us;
    if (infer_us > infer_max) infer_max = infer_us;
  }

  // Fill scores from last run
  TfLiteTensor* output = interpreter->output(0);
  float scale      = output->params.scale;
  int   zero_point = output->params.zero_point;
  for (int i = 0; i < kCategoryCount; i++) {
    scores[i] = (tflite::GetTensorData<int8_t>(output)[i] - zero_point) * scale;
  }

  *avg_feature_us   = feat_sum  / N_RUNS;
  *avg_inference_us = infer_sum / N_RUNS;
  int64_t avg_total = total_sum / N_RUNS;

  MicroPrintf("Timing:");
  MicroPrintf("  Runs:        %d", N_RUNS);
  MicroPrintf("  Feature min: %lld us", feat_min);
  MicroPrintf("  Feature max: %lld us", feat_max);
  MicroPrintf("  Feature avg: %lld us (%.2f ms)",
              *avg_feature_us, (double)*avg_feature_us / 1000.0);
  MicroPrintf("  Infer min:   %lld us", infer_min);
  MicroPrintf("  Infer max:   %lld us", infer_max);
  MicroPrintf("  Infer avg:   %lld us (%.2f ms)",
              *avg_inference_us, (double)*avg_inference_us / 1000.0);
  MicroPrintf("  Total avg:   %lld us (%.2f ms)",
              avg_total, (double)avg_total / 1000.0);

  return avg_total;
}

// ── Setup ────────────────────────────────────────────────────────────────────

void setup() {
  tflite::InitializeTarget();

  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask   = (1ULL << kMeasurePin);
  io_conf.mode           = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en     = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en   = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type      = GPIO_INTR_DISABLE;
  gpio_config(&io_conf);
  gpio_set_level(kMeasurePin, 0);

  model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema mismatch: got %d, want %d",
                model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();

  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  if (interpreter->AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  model_input = interpreter->input(0);

  if (model_input->dims->size != 2 ||
      model_input->dims->data[0] != 1 ||
      model_input->dims->data[1] != kFeatureElementCount ||
      model_input->type != kTfLiteInt8) {
    MicroPrintf("Bad input tensor shape or type");
    return;
  }

  if (InitializeMicroFeatures() != kTfLiteOk) {
    MicroPrintf("InitializeMicroFeatures() failed");
    return;
  }

  MicroPrintf("====================================");
  MicroPrintf("ESP32 TinyML Inference Benchmark");
  MicroPrintf("Configuration:");
  MicroPrintf("  Model input:     [1, %d] int8", kFeatureElementCount);
  MicroPrintf("  Repeated runs:   %d", N_RUNS);
  MicroPrintf("  Measure pin:     GPIO %d", (int)kMeasurePin);
  PrintArenaStats();
  PrintMemoryStats("after setup");
  MicroPrintf("====================================");
}

// ── Loop ─────────────────────────────────────────────────────────────────────

void loop() {
  float scores[kCategoryCount];

  // ── Test 1: Pre-computed YES features ────────────────────────────────────
  MicroPrintf("\n====================================");
  MicroPrintf("TEST 1: YES (pre-computed features)");
  MicroPrintf("====================================");
  MicroPrintf("Runs: %d", N_RUNS);
  int64_t yes_time = RunInference(
      g_yes_micro_f2e59fea_nohash_1_data,
      scores);
  if (yes_time >= 0) {
    PrintResults("YES features (pre-computed)", scores);
    PrintStackOnly("after YES inference");
  }

  // ── Test 2: Pre-computed NO features ─────────────────────────────────────
  MicroPrintf("\n====================================");
  MicroPrintf("TEST 2: NO (pre-computed features)");
  MicroPrintf("====================================");
  MicroPrintf("Runs: %d", N_RUNS);
  int64_t no_time = RunInference(
    g_no_micro_f9643d42_nohash_4_data,
    scores);
  if (no_time >= 0) {
    PrintResults("NO features (pre-computed)", scores);
    PrintStackOnly("after NO inference");
  }

  // ── Combined inference summary (YES + NO) ───────────────────────────────
  if (yes_time >= 0 && no_time >= 0) {
    int64_t avg_inference = (yes_time + no_time) / 2;

    MicroPrintf("\n------------------------------------");
    MicroPrintf("INFERENCE SUMMARY (YES + NO)");
    MicroPrintf("------------------------------------");
    MicroPrintf("  Avg inference time: %lld us (%.2f ms)",
                avg_inference, (double)avg_inference / 1000.0);
  }

  // ── Test 3: Full audio pipeline ───────────────────────────────────────────
  MicroPrintf("\n====================================");
  MicroPrintf("TEST 3: YES (full pipeline)");
  MicroPrintf("====================================");
  MicroPrintf("Runs: %d", N_RUNS);
  {
    int64_t avg_feature_us = 0;
    int64_t avg_inference_us = 0;
    int64_t avg_total = RunFullPipeline(
        g_embedded_audio, g_embedded_audio_len,
        scores, &avg_feature_us, &avg_inference_us);

    if (avg_total >= 0) {
      PrintResults("YES audio (full pipeline)", scores);
      PrintStackOnly("after full pipeline");
    }
  }

  MicroPrintf("\n====================================");
  MicroPrintf("BENCHMARK SUMMARY");
  MicroPrintf("====================================");
  PrintMemoryStats("end of benchmark cycle");
  MicroPrintf("====================================");
  MicroPrintf("Benchmark complete. Restarting in 240s...");
  MicroPrintf("====================================\n");

  vTaskDelay(pdMS_TO_TICKS(240000));
}